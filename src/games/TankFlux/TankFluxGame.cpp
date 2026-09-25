#include "TankFluxGame.h"
#include <Preferences.h>

namespace tankflux {

void TankFluxGame::loadHighScore() {
    Preferences prefs;
    prefs.begin("tf_data", true);
    _highScore = prefs.getInt("highscore", 0);
    prefs.end();
}

void TankFluxGame::saveHighScore() {
    Preferences prefs;
    prefs.begin("tf_data", false);
    prefs.putInt("highscore", _highScore);
    prefs.end();
}

void TankFluxGame::recordHighScore() {
    if (_score > _highScore) { _highScore = _score; saveHighScore(); }
}

void TankFluxGame::init(AudioEngine &audio) {
    loadHighScore();
    _phase = PHASE_ATTRACT;
    _attractSlide      = SLIDE_GAME;
    _attractSlideTimer = millis();
    _attractMusicStarted = false;
    _attractMusicEarliestAt = millis() + ATTRACT_MUSIC_GRACE_MS;
    _btnBWasHeld = true;
    _btnBHoldStart = 0;
    // /audio/tank_start.wav from SD, or a generated melody if it's missing.
    audio.playTankStartSound();
}

void TankFluxGame::startNewGame(AudioEngine &audio) {
    audio.stopLoop();   // ends the attract music
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
    for (auto &e : _enemies) {
        e.alive = false;
        resetFireState(e);
        setTankVisible(e, false);
        // enemyCap() holds the extras back at level 1; this just gives the
        // first arrival a moment's grace.
        e.respawnAt = millis() + FIRST_SPAWN_GRACE_MS;
    }
    _boss.alive = false;
    resetFireState(_boss);
    setTankVisible(_boss, false);
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

bool TankFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    ensureSceneReady(canvas);

    // Outside of play, holding B for EXIT_HOLD_MS exits. A fresh B press
    // already exits from both screens (see their handlers); this covers B
    // still held down from strafing when the game ended. After init(), B
    // must be released first so a press carried over from the launcher
    // doesn't count. During play B is strafe and A+B quits (updatePlaying()).
    if (_phase != PHASE_PLAYING) {
        if (_btnBWasHeld) {
            if (!input.btnB) _btnBWasHeld = false;
        } else if (input.btnB) {
            if (_btnBHoldStart == 0) _btnBHoldStart = millis();
            if (millis() - _btnBHoldStart > EXIT_HOLD_MS) {
                _btnBHoldStart = 0;
                audio.mute();
                return false;
            }
        } else {
            _btnBHoldStart = 0;
        }
    }

    switch (_phase) {
        case PHASE_ATTRACT:  return updateAttract(canvas, input, audio);
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default:             return updatePlaying(canvas, input, audio);
    }
}

bool TankFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    // Start the loop once, and not while the startup sound is still going:
    // loopWAV() stops whatever is playing. isSamplePlaying() can't see an SD
    // WAV that is still opening, hence the grace period as well.
    if (!_attractMusicStarted && !audio.isSamplePlaying() && !audio.isMelodyPlaying() &&
        reached(_attractMusicEarliestAt)) {
        audio.loopWAV("/audio/tank_loop.wav");
        _attractMusicStarted = true;
    }

    if (millis() - _attractSlideTimer > ATTRACT_SLIDE_MS) {
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

bool TankFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    renderGameOver(canvas);

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

bool TankFluxGame::updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnA && input.btnB) {
        if (_quitHoldStart == 0) {
            _quitHoldStart = millis();
        } else if (millis() - _quitHoldStart > QUIT_HOLD_MS) {
            _quitHoldStart = 0;
            recordHighScore();
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

    if (_arenaShiftCuePending && reached(_arenaShiftCueAt)) {
        _arenaShiftCuePending = false;
        // Rising chime: distinct from the single-tone boss fanfare and
        // level-up cue, so "the arena changed" reads as its own event.
        static const int n[] = { 700, 950, 1250, 1600 };
        static const int d[] = {  70,  70,   70,  160 };
        audio.playMelody(n, d, 4);
        _arenaShiftFlashUntil = millis() + ARENA_SHIFT_FLASH_MS;
    }

    _scene->render();
    drawSun(canvas);
    _particles.update(1.0f / 60.0f);
    _particles.render(_scene, &_camera, canvas.width(), canvas.height());

    drawBarrel(canvas);
    drawGunsight(canvas, canvas.width() / 2, 11 + (canvas.height() - 11) / 2);
    drawFlashes(canvas);
    drawRadar(canvas);
    drawHUD(canvas);
    drawBossAlert(canvas);
    drawBossBonus(canvas);
    drawQuitHint(canvas);

    if (_health <= 0) {
        recordHighScore();
        _phase = PHASE_GAMEOVER;
        _gameOverEnteredMs = millis();
        audio.playTone(150, 400);
    }
    return true;
}

}  // namespace tankflux
