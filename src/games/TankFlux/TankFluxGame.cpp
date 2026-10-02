#include "TankFluxGame.h"

namespace tankflux {

// Quitting (the cabinet's Back button): a game in progress still goes on the
// table (if it makes it), under the last name entered; a name being entered
// is kept. The attract demo's score is nobody's.
void TankFluxGame::onQuit(AudioEngine &audio) {
    if (_phase == PHASE_NAME) _scores.finishNow();
    else if (_phase == PHASE_PLAYING && !inDemo()) _scores.record(_score);
    audio.mute();
}

// How far this frame should move things, relative to a frame at the rate the
// game was tuned at. Without it the whole game runs slow whenever the frame
// rate drops — three tanks on screen cost enough to be felt as lag.
void TankFluxGame::updateFrameScale() {
    unsigned long now = millis();
    unsigned long dt = now - _lastFrameMs;
    _lastFrameMs = now;
    if (dt < MIN_FRAME_MS) dt = MIN_FRAME_MS;
    if (dt > MAX_FRAME_MS) dt = MAX_FRAME_MS;
    _frameScale = (float)dt / (float)REFERENCE_FRAME_MS;
}

void TankFluxGame::init(AudioEngine &audio) {
    _scores.begin("tank");
    _phase = PHASE_ATTRACT;
    _attractSlide      = SLIDE_GAME;
    _attractSlideTimer = millis();
    _lastFrameMs = millis();   // so the first frame isn't a huge clamped step
    // /audio/tank_start.wav from SD, or a generated melody if it's missing.
    audio.playTankStartSound();
    // Decoded into the mixer's cache now, so the first play isn't late.
    static const char* const sfx[] = { "/audio/shot.wav", "/audio/explosion.wav",
                                       "/audio/repair.wav" };
    for (const char* f : sfx) audio.preload(f);
}

void TankFluxGame::startNewGame(AudioEngine &audio) {
    resetGame();
    audio.playTone(900, 80);
    // Music plays during a game only: not on the attract screen, and it
    // stops at game over (restarting with the next game).
    audio.loopWAV(TANK_MUSIC);
}

void TankFluxGame::resetGame() {
    _x = 0.0f;
    _z = 0.0f;
    _vx = 0.0f;
    _vz = 0.0f;
    _headingDeg = 0.0f;
    _speed = 0.0f;
    _health = HEALTH_MAX;
    _score = 0;
    _scores.forget();
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
    _arenaShiftCuePending = false;
    _arenaShiftFlashUntil = 0;
    for (auto &p : _particles.pool) p.active = false;
    _phase = PHASE_PLAYING;
}

bool TankFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    ensureSceneReady(canvas);
    updateFrameScale();

    // Quitting is the cabinet's Back button (main.cpp, then onQuit()); B
    // is strafe.

    switch (_phase) {
        case PHASE_ATTRACT:  return updateAttract(canvas, input, audio);
        case PHASE_NAME:     return updateName(canvas, input, audio);
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default:             return updatePlaying(canvas, input, audio);
    }
}

bool TankFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (_attractSlide == SLIDE_DEMO) return updateDemo(canvas, input, audio);
    // Title, how-to-play, high scores, then the demo, which returns to the title.
    if (millis() - _attractSlideTimer > ATTRACT_SLIDE_MS) {
        if (_attractSlide == SLIDE_GAME || _attractSlide == SLIDE_INFO) {
            _attractSlide      = _attractSlide == SLIDE_GAME ? SLIDE_INFO : SLIDE_SCORES;
            _attractSlideTimer = millis();
        } else {
            startDemo();
            return updateDemo(canvas, input, audio);
        }
    }

    if (_attractSlide == SLIDE_GAME)      renderAttractGame(canvas);
    else if (_attractSlide == SLIDE_INFO) renderAttractInfo(canvas);
    else                                  renderAttractScores(canvas);

    if (input.btnAPressed) startNewGame(audio);
    return true;
}

// Name entry, over a black screen; then the game-over screen.
bool TankFluxGame::updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
    _scores.draw(canvas);
    if (_scores.update(input, getRotation())) {
        _phase = PHASE_GAMEOVER;
        _gameOverEnteredMs = millis();
        audio.playTone(1047, 80);
    }
    return true;
}

bool TankFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    renderGameOver(canvas);

    unsigned long elapsed = millis() - _gameOverEnteredMs;
    // Only after the input delay, so mashing at the end doesn't.
    const bool inputOk = elapsed >= ArcadeConfig::GAMEOVER_INPUT_DELAY_MS;
    if (inputOk && input.btnAPressed) { startNewGame(audio); return true; }
    if (elapsed > GAMEOVER_TIMEOUT_MS) {
        _phase = PHASE_ATTRACT;
        _attractSlide      = SLIDE_GAME;
        _attractSlideTimer = millis();
    }
    return true;
}

bool TankFluxGame::updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
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
    // Not the real frame time: this is the step the particles were tuned
    // against, scaled the same way everything else is, so they keep the
    // look they have now while staying steady across frame rates.
    _particles.update((1.0f / 60.0f) * _frameScale);
    _particles.render(_scene, &_camera, canvas.width(), canvas.height());

    drawBarrel(canvas);
    drawGunsight(canvas, canvas.width() / 2, 11 + (canvas.height() - 11) / 2);
    drawFlashes(canvas);
    drawRadar(canvas);
    drawHUD(canvas);
    drawBossAlert(canvas);
    drawBossBonus(canvas);

    if (_health <= 0) {
        // A name for the table first, if the score made it.
        _phase = !inDemo() && _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
        _gameOverEnteredMs = millis();
        audio.stopLoop();
        audio.playTone(150, 400);
    }
    return true;
}

}  // namespace tankflux
