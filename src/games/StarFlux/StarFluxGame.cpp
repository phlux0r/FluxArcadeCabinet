#include "StarFluxGame.h"
#include <Preferences.h>

namespace starflux {

void StarFluxGame::loadHighScore() {
    Preferences prefs;
    prefs.begin("sf_data", true);
    _highScore = prefs.getInt("highscore", 0);
    prefs.end();
}

void StarFluxGame::saveHighScore() {
    Preferences prefs;
    prefs.begin("sf_data", false);
    prefs.putInt("highscore", (int32_t)_highScore);
    prefs.end();
}

// Never from the demo: its score isn't yours.
void StarFluxGame::recordHighScore() {
    if (inDemo()) return;
    if (_score > _highScore) {
        _highScore = _score;
        _newHighScore = true;
        saveHighScore();
    }
}

// Movement is per REFERENCE_FRAME_MS, scaled by the real frame time, as in
// Tank and Tube Flux: the game plays at the same speed whatever the frame rate.
void StarFluxGame::updateFrameScale() {
    unsigned long now = millis();
    unsigned long dt = now - _lastFrameMs;
    _lastFrameMs = now;
    if (dt < MIN_FRAME_MS) dt = MIN_FRAME_MS;
    if (dt > MAX_FRAME_MS) dt = MAX_FRAME_MS;
    _frameScale = (float)dt / (float)REFERENCE_FRAME_MS;
}

void StarFluxGame::init(AudioEngine &audio) {
    loadHighScore();
    enterAttract();
    _btnBWasHeld = true;
    _btnBHoldStart = 0;
    _lastFrameMs = millis();
    static const int n[] = { 523, 784, 1047, 1568 };
    static const int d[] = {  70,  70,  90,  220 };
    audio.playMelody(n, d, 4);
    // Decoded into the mixer's cache now, so the first play isn't late.
    static const char* const sfx[] = { "/audio/tube_shot.wav", "/audio/explosion.wav",
                                       "/audio/tube_bump.wav", "/audio/powerup.wav" };
    for (const char* f : sfx) audio.preload(f);
}

void StarFluxGame::startNewGame(AudioEngine &audio) {
    resetRun();
    _phase = PHASE_PLAYING;
    _phaseEnteredMs = millis();
    audio.playTone(900, 80);
    // Music plays during a run only, as in the other games.
    audio.loopWAV(STAR_MUSIC);
}

// Everything a run starts from, shared by a real game and the attract demo.
void StarFluxGame::resetRun() {
    _loop = 1;
    _lives = LIVES;
    _score = 0;
    _newHighScore = false;
    startStage();
}

// The stage from the top: the fly-in, then segment 0.
void StarFluxGame::startStage() {
    clearField();
    _shield = SHIELD_MAX;
    if (_bombs < BOMBS_START) _bombs = BOMBS_START;
    _shipX = 0; _shipY = BOX_Y_MIN; _shipVX = _shipVY = 0; _bank = 0;
    _fightersSeen = _fightersDowned = _rocksDowned = _ringsCaught = 0;
    _stageStartScore = _score;
    _shieldBonus = 0;
    _stage = STAGE_INTRO;
    _stageAt = millis();
    _invulnUntil = millis() + INTRO_MS + SPAWN_INVULN_MS;
    _hitFlashUntil = 0;
    _bannerUntil = 0;
    _seg = 0;   // started when the fly-in ends (updateStage())
}

// Everything in flight goes: shots, bombs, enemies, rocks, rings, the boss.
void StarFluxGame::clearField() {
    for (auto &s : _shots) s.active = false;
    for (auto &e : _eshots) e.active = false;
    for (auto &f : _fighters) { f.active = false; if (f.obj) f.obj->enabled = false; }
    for (auto &r : _rocks) { r.active = false; if (r.obj) r.obj->enabled = false; }
    for (auto &r : _rings) r.active = false;
    for (auto &b : _blasts) b.active = false;
    for (auto &p : _particles.pool) p.active = false;
    _bombActive = false;
    hideBoss();
}

void StarFluxGame::enterGameOver(AudioEngine &audio) {
    recordHighScore();
    _phase = PHASE_GAMEOVER;
    _phaseEnteredMs = millis();
    _shipSprite.enabled = false;
    audio.stopLoop();
    static const int n[] = { 520, 390, 260, 130 };
    static const int d[] = { 120, 120, 120, 320 };
    audio.playMelody(n, d, 4);
}

// The boss is down: tally the stage. The shield you kept is the bonus.
void StarFluxGame::enterResults(AudioEngine &audio) {
    _phase = PHASE_RESULTS;
    _phaseEnteredMs = millis();
    _shieldBonus = (long)_shield * SHIELD_BONUS_PER_POINT;
    _score += _shieldBonus;
    recordHighScore();
    static const int n[] = { 784, 988, 1175, 1568, 1319, 1568 };
    static const int d[] = { 110, 110, 110, 220, 110, 380 };
    if (!_silent) audio.playMelody(n, d, 6);
}

bool StarFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    ensureSceneReady(canvas);
    updateFrameScale();

    // Hold B to exit, in every phase. After init() B must be released
    // first, so the launcher press that started us can't count. While
    // playing, a short tap of B drops a bomb instead (updateBombButton).
    if (_btnBWasHeld) {
        if (!input.btnB) _btnBWasHeld = false;
    } else if (input.btnB) {
        if (_btnBHoldStart == 0) _btnBHoldStart = millis();
        if (millis() - _btnBHoldStart > EXIT_HOLD_MS) {
            _btnBHoldStart = 0;
            if (_phase == PHASE_PLAYING) recordHighScore();
            audio.mute();
            return false;
        }
    } else {
        _btnBHoldStart = 0;
    }

    switch (_phase) {
        case PHASE_ATTRACT:  return updateAttract(canvas, input, audio);
        case PHASE_RESULTS:  return updateResults(canvas, input, audio);
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default:             return updatePlaying(canvas, input, audio);
    }
}

// Title, how-to-play, then a demo (StarFluxDemo.cpp), round and round. A
// starts a game from any of them.
bool StarFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnAPressed) {
        if (inDemo()) endDemo();
        startNewGame(audio);
        _prevA = true;   // the press that started the game isn't a shot
        return updatePlaying(canvas, InputState{}, audio);
    }
    if (_attractSlide != SLIDE_DEMO && millis() - _attractSlideAt > ATTRACT_SLIDE_MS) {
        if (_attractSlide == SLIDE_TITLE) {
            _attractSlide = SLIDE_INFO;
            _attractSlideAt = millis();
        } else {
            startDemo();
        }
    }
    switch (_attractSlide) {
        case SLIDE_TITLE:
            renderAttractTitle(canvas);
            break;
        case SLIDE_INFO:
            // Empty space flying past behind the text.
            _shipX *= 0.95f; _shipY *= 0.95f; _bank *= 0.9f;
            renderWorld(canvas);
            renderAttractInfo(canvas);
            break;
        case SLIDE_DEMO:
            updateDemo(canvas, audio);
            break;
    }
    return true;
}

bool StarFluxGame::updateResults(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _bank *= 0.9f;
    renderWorld(canvas);
    renderResults(canvas);
    unsigned long elapsed = millis() - _phaseEnteredMs;
    if ((elapsed > RESULTS_MIN_MS && input.btnAPressed) || elapsed > RESULTS_MAX_MS) {
        // Round again, harder.
        ++_loop;
        startStage();
        _phase = PHASE_PLAYING;
        _prevA = true;
    }
    return true;
}

bool StarFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    renderWorld(canvas);
    renderGameOver(canvas);
    unsigned long elapsed = millis() - _phaseEnteredMs;
    if (elapsed > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS && input.btnAPressed) {
        startNewGame(audio);
        _prevA = true;
    } else if (elapsed > GAMEOVER_TIMEOUT_MS) {
        clearField();
        enterAttract();
    }
    return true;
}

bool StarFluxGame::updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    stepRun(input, audio);
    renderRun(canvas);
    return true;
}

// One frame of a run: the same for a real game and the demo.
void StarFluxGame::stepRun(const InputState &input, AudioEngine &audio) {
    updateShip(input);
    if (shipControllable()) {
        tryFire(input, audio);
        updateBombButton(input, audio);
    }
    updateStage(audio);
    if (_phase == PHASE_RESULTS || _phase == PHASE_GAMEOVER) return;   // the run ended this frame
    updateBomb(audio);
    updateShots(audio);        // before collisions: a point-blank shot still counts
    updateFighters(audio);
    updateBoss(audio);
    updateEShots(audio);
    updateRocks(audio);
    updateRings(audio);
}

void StarFluxGame::renderRun(GFXcanvas16 &canvas) {
    updateShipSprite();
    renderWorld(canvas);
    drawHUD(canvas);
    drawOverlays(canvas);
    drawQuitHint(canvas);
}

// Backdrop and stars (drawn directly), rings, Jet's pass for the fighters,
// rocks and boss, particles, then the 2D lasers, shots, blasts and the
// reticle. Jet draws the ship sprite at the end of render(), so it sits
// over the meshes; what's drawn after is over the ship too, which suits
// lasers and flashes.
void StarFluxGame::renderWorld(GFXcanvas16 &canvas) {
    placeCamera();
    drawBackdrop(canvas);
    drawStars(canvas);
    drawRings(canvas);
    _scene->render();
    _particles.update((1.0f / 60.0f) * _frameScale);
    _particles.render(_scene, &_camera, canvas.width(), canvas.height());
    drawShots(canvas);
    drawBlasts(canvas);
    if (_phase == PHASE_PLAYING && _stage == STAGE_RUN) drawReticle(canvas);
}

}  // namespace starflux
