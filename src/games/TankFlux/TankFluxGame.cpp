#include "TankFluxGame.h"

namespace tankflux {

void TankFluxGame::loadHighScore() {
    _prefs.begin("tf_data", true);
    _highScore = _prefs.getInt("highscore", 0);
    _prefs.end();
}

void TankFluxGame::saveHighScore() {
    _prefs.begin("tf_data", false);
    _prefs.putInt("highscore", _highScore);
    _prefs.end();
}

void TankFluxGame::startNewGame(AudioEngine &audio) {
    audio.stopLoop();   // same call AsteroidFluxGame uses to end its attract loop
    _x = 0.0f;
    _z = 0.0f;
    _vx = 0.0f;
    _vz = 0.0f;
    _headingDeg = 0.0f;
    _speed = 0.0f;
    _health = HEALTH_MAX;
    _score = 0;
    _kills = 0;
    _level = 1;
    _reloadAt = 0;
    _muzzleFlashUntil = 0;
    _damageFlashUntil = 0;
    for (int i = 0; i < REPAIR_COUNT; ++i) {
        _kits[i].active = true;
        if (_kits[i].obj) _kits[i].obj->enabled = true;
    }
    killShell(_playerShell);
    for (auto &s : _enemyShells) killShell(s);
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        _enemies[i].alive = false;
        resetFireState(_enemies[i]);
        _enemies[i].hull->enabled   = false;
        _enemies[i].turret->enabled = false;
        _enemies[i].barrel->enabled = false;
        _enemies[i].trackL->enabled = false;
        _enemies[i].trackR->enabled = false;
        // enemyCap() holds the extras back at level 1 regardless; this
        // just gives the first arrival a moment's grace.
        _enemies[i].respawnAt = millis() + 1200;
    }
    _boss.alive = false;
    resetFireState(_boss);
    _boss.hull->enabled   = false;
    _boss.turret->enabled = false;
    _boss.barrel->enabled = false;
    _boss.trackL->enabled = false;
    _boss.trackR->enabled = false;
    _bossActive  = false;
    _bossPending = false;
    _nextBossAt  = BOSS_EVERY_KILLS;
    _bossesDefeated = 0;
    _bossBonusUntil = 0;
    _quitHoldStart = 0;
    _arenaShiftCuePending = false;
    _arenaShiftFlashUntil = 0;
    for (auto &p : _particles.pool) p.active = false;
    _phase = PHASE_PLAYING;
    audio.playTone(900, 80);
}

void TankFluxGame::init(AudioEngine &audio) {
    loadHighScore();
    _phase = PHASE_ATTRACT;
    _attractSlide      = SLIDE_GAME;
    _attractSlideTimer = millis();
    _attractMusicStarted = false;
    // playTankStartSound()'s SD-WAV path (AudioEngine::playWAV) sets
    // _audioState.playing FALSE immediately (stopAudioTask(), to halt
    // whatever came before) and only flips it back TRUE once its own
    // async audio task has opened the file over SPI and parsed the WAV
    // header — real SD latency, not instant. isSamplePlaying() reads
    // false the whole time that's in flight, so checking it on the very
    // next update() (one frame after init()) can't tell "hasn't started
    // yet" apart from "started, still opening" — the attract-loop guard
    // below saw a false negative and stole the channel with
    // loopWAV(/audio/tank_loop.wav) before tank_start.wav ever became
    // audible. 300ms mirrors the deferred-fallback deadline
    // AudioEngine.h already uses for this exact kind of SD-open-latency
    // check (playGameOverSound() etc).
    _attractMusicEarliestAt = millis() + 300;
    _btnBWasHeld = true;
    // Same convention as LanderFluxGame's playLanderStartSound(): try
    // /audio/tank_start.wav on SD first, else fall back to a short
    // generated melody (no PROGMEM sample needed).
    audio.playTankStartSound();
}

bool TankFluxGame::update(GFXcanvas16 &canvas,
            const InputState &input,
            AudioEngine &audio) {
    ensureSceneReady(canvas);

    // --- Button B: require release first, then hold 2s to exit ---
    // Skipped during PHASE_PLAYING: holding B there strafes instead
    // (see updateDriving()), and this 2s hold-to-exit would otherwise
    // yank the player to the launcher mid-strafe. Still available from
    // ATTRACT to back out before starting; GAMEOVER already has its
    // own explicit single-press "[BTN B] QUIT".
    static unsigned long btnBHoldStart = 0;
    if (_phase != PHASE_PLAYING) {
        if (_btnBWasHeld) {
            if (!input.btnB) _btnBWasHeld = false;
        } else if (input.btnB) {
            if (btnBHoldStart == 0) btnBHoldStart = millis();
            if (millis() - btnBHoldStart > 2000) {
                btnBHoldStart = 0;
                audio.mute();
                return false;
            }
        } else {
            btnBHoldStart = 0;
        }
    }

    // ---- PHASE: ATTRACT ----
    if (_phase == PHASE_ATTRACT) {
        // Loop the attract music once, guarded so it isn't re-issued
        // every frame. loopWAV() unconditionally stops whatever's
        // currently playing, so without these guards the startup sound
        // from init() — playTankStartSound() plays either a WAV
        // (isSamplePlaying()) or the fallback melody (isMelodyPlaying())
        // — would be cut off before it's ever heard. The extra
        // millis() check covers the SD-WAV path specifically: see
        // _attractMusicEarliestAt's own comment in init() for why
        // isSamplePlaying() alone isn't enough to catch that case.
        if (!_attractMusicStarted && !audio.isSamplePlaying() && !audio.isMelodyPlaying() &&
            millis() >= _attractMusicEarliestAt) {
            audio.loopWAV("/audio/tank_loop.wav");
            _attractMusicStarted = true;
        }

        if (millis() - _attractSlideTimer > 8000) {
            _attractSlide      = (_attractSlide == SLIDE_GAME) ? SLIDE_INFO : SLIDE_GAME;
            _attractSlideTimer = millis();
        }

        if (_attractSlide == SLIDE_GAME) renderAttractGame(canvas);
        else                              renderAttractInfo(canvas);

        if (input.btnBPressed) {
            audio.mute();
            return false;
        }
        if (input.btnAPressed) startNewGame(audio);
        return true;
    }

    // ---- PHASE: GAME OVER ----
    if (_phase == PHASE_GAMEOVER) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        canvas.setTextColor(ArcadeConfig::COLOR_RED);
        canvas.setTextSize(2);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4 - 12, 15);
        canvas.print("DESTROYED");

        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 45);
        canvas.print("SCORE: "); canvas.print(_score);

        if (_score >= _highScore && _score > 0) {
            canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
            canvas.print("NEW HIGH SCORE!!");
        } else {
            canvas.setTextColor(ArcadeConfig::COLOR_GREY);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
            canvas.print("BEST: "); canvas.print(_highScore);
        }

        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(20, 90);
        canvas.print("[BTN A] PLAY AGAIN");
        canvas.setCursor(20, 103);
        canvas.print("[BTN B] QUIT");

        unsigned long elapsed = millis() - _gameOverEnteredMs;
        if (input.btnAPressed) { startNewGame(audio); return true; }
        if (input.btnBPressed) { audio.mute(); return false; }
        if (elapsed > GAMEOVER_TIMEOUT_MS) {
            _phase = PHASE_ATTRACT;
            _attractSlide      = SLIDE_GAME;
            _attractSlideTimer = millis();
            _attractMusicStarted = false;
            _btnBWasHeld = false;
        }
        return true;
    }

    // ---- PHASE: PLAYING ----
    if (input.btnA && input.btnB) {
        if (_quitHoldStart == 0) {
            _quitHoldStart = millis();
        } else if (millis() - _quitHoldStart > QUIT_HOLD_MS) {
            _quitHoldStart = 0;
            if (_score > _highScore) { _highScore = _score; saveHighScore(); }
            audio.mute();
            return false;
        }
    } else {
        _quitHoldStart = 0;
    }

    updateDriving(input, audio);
    updateKits(audio);
    tryFire(input, audio);
    updateEnemies(audio);
    updateShells(audio);

    if (_arenaShiftCuePending && millis() >= _arenaShiftCueAt) {
        _arenaShiftCuePending = false;
        // A quick rising four-note chime — distinct from the boss's own
        // fanfare (a single 1900Hz tone) and from level-up (also single-
        // tone), so "the world just reset" reads as its own event.
        static const int n[] = { 700, 950, 1250, 1600 };
        static const int d[] = {  70,  70,   70,  160 };
        audio.playMelody(n, d, 4);
        _arenaShiftFlashUntil = millis() + 320;
    }

    _scene->render();
    drawSun(canvas);
    _particles.update(1.0f / 60.0f);
    _particles.render(_scene, &_camera, canvas.width(), canvas.height());

    drawBarrel(canvas);
    drawGunsight(canvas, canvas.width() / 2, 11 + (canvas.height() - 11) / 2);
    drawDamageFlash(canvas);
    drawArenaShiftFlash(canvas);
    drawRadar(canvas);
    drawHUD(canvas);
    drawBossAlert(canvas);
    drawBossBonus(canvas);
    drawQuitHint(canvas);

    if (_health <= 0) {
        if (_score > _highScore) { _highScore = _score; saveHighScore(); }
        _phase = PHASE_GAMEOVER;
        _gameOverEnteredMs = millis();
        audio.playTone(150, 400);
    }

    return true;
}

}  // namespace tankflux
