#include "TubeFluxGame.h"
#include <Preferences.h>

namespace tubeflux {

void TubeFluxGame::loadHighScore() {
    Preferences prefs;
    prefs.begin("tb_data", true);
    _highScore = prefs.getInt("highscore", 0);
    prefs.end();
}

void TubeFluxGame::saveHighScore() {
    Preferences prefs;
    prefs.begin("tb_data", false);
    prefs.putInt("highscore", (int32_t)_highScore);
    prefs.end();
}

void TubeFluxGame::recordHighScore() {
    if (_score > _highScore) {
        _highScore = _score;
        _newHighScore = true;
        saveHighScore();
    }
}

// Movement is per REFERENCE_FRAME_MS, scaled by the real frame time, as in
// Tank Flux: the game plays at the same speed whatever the frame rate.
void TubeFluxGame::updateFrameScale() {
    unsigned long now = millis();
    unsigned long dt = now - _lastFrameMs;
    _lastFrameMs = now;
    if (dt < MIN_FRAME_MS) dt = MIN_FRAME_MS;
    if (dt > MAX_FRAME_MS) dt = MAX_FRAME_MS;
    _frameScale = (float)dt / (float)REFERENCE_FRAME_MS;
}

void TubeFluxGame::init(AudioEngine &audio) {
    loadHighScore();
    enterAttract();
    _btnBWasHeld = true;
    _btnBHoldStart = 0;
    _lastFrameMs = millis();
    _angle = 0.0f;
    _rollVel = 0.0f;
    static const int n[] = { 440, 660, 880, 1320 };
    static const int d[] = {  80,  80,  80,  200 };
    audio.playMelody(n, d, 4);
}

void TubeFluxGame::startNewGame(AudioEngine &audio) {
    _angle = 0.0f;
    _rollVel = 0.0f;
    _throttle = 1.0f;
    _dist = 0.0f;
    _tier = 1;
    _speed = tierSpeed();
    _shield = SHIELD_MAX;
    _score = 0;
    _bonus = 0;
    _newHighScore = false;
    _invulnUntil = _hitFlashUntil = _nearMissUntil = _tierBannerUntil = 0;
    _safeLane = _prevSafeLane = 0;   // start straight down the lane you're in
    _bendX = _bendY = _bendTargetX = _bendTargetY = 0.0f;
    _nextBendAt = 0.0f;
    _safeLaneMovedAt = 0.0f;
    // First block a little way in, so the opening is a moment to settle.
    _nextSpawnAt = SPAWN_AHEAD * 0.6f;
    for (auto &o : _obstacles) {
        o.active = false;
        if (o.obj) o.obj->enabled = false;
    }
    hideTransients();
    _gunLevel = 0;
    _reloadAt = 0;
    _pickupBannerUntil = 0;
    _crystalsDestroyed = 0;
    _shieldsCollected = 0;
    _nextGunAt = WEAPON_FIRST_AT;
    _nextUpgradeAt = 0.0f;
    _nextShieldAt = 0.0f;
    for (auto &p : _particles.pool) p.active = false;
    applyTierPalette();
    _phase = PHASE_PLAYING;
    _phaseEnteredMs = millis();
    audio.playTone(900, 80);
}

void TubeFluxGame::enterGameOver(AudioEngine &audio) {
    recordHighScore();
    _phase = PHASE_GAMEOVER;
    _phaseEnteredMs = millis();
    _shipSprite.enabled = false;
    // Shots and the pickup would hang in mid-air while the world drifts on.
    for (auto &s : _shots) { s.active = false; s.obj->enabled = false; }
    _pickupActive = false;
    _chevronObj->enabled = false;
    _crossObj->enabled = false;
    static const int n[] = { 520, 390, 260, 130 };
    static const int d[] = { 120, 120, 120, 320 };
    audio.playMelody(n, d, 4);
}

bool TubeFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    ensureSceneReady(canvas);
    updateFrameScale();

    // Hold B to exit, in every phase: B does nothing else in this game.
    // After init() B must be released first, so the launcher press that
    // started us can't count.
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
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default:             return updatePlaying(canvas, input, audio);
    }
}

// Crystals, shots and the pickup: everything besides blocks that a new run
// or the attract screen mustn't inherit.
void TubeFluxGame::hideTransients() {
    for (auto &c : _crystals) { c.active = false; if (c.obj) c.obj->enabled = false; }
    for (auto &s : _shots) { s.active = false; if (s.obj) s.obj->enabled = false; }
    _pickupActive = false;
    if (_chevronObj) _chevronObj->enabled = false;
    if (_crossObj) _crossObj->enabled = false;
}

void TubeFluxGame::enterAttract() {
    _phase = PHASE_ATTRACT;
    // The how-to-play slide flies a straight tunnel, whatever the last run ended on.
    _bendX = _bendY = _bendTargetX = _bendTargetY = 0.0f;
    _phaseEnteredMs = millis();
    _attractSlide = SLIDE_TITLE;
    _attractSlideAt = millis();
}

// Title image and how-to-play alternate. Behind how-to-play the tunnel
// keeps flowing, empty, at tier 1 speed; the title is a full-screen image,
// so the world isn't rendered at all while it shows.
bool TubeFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (millis() - _attractSlideAt > ATTRACT_SLIDE_MS) {
        _attractSlide = (_attractSlide == SLIDE_TITLE) ? SLIDE_INFO : SLIDE_TITLE;
        _attractSlideAt = millis();
    }
    if (_attractSlide == SLIDE_TITLE) {
        renderAttractTitle(canvas);
    } else {
        _dist += BASE_SPEED * _frameScale;
        _angle = fmodf(_angle + 0.6f * _frameScale, 360.0f);
        renderWorld(canvas);
        renderAttractInfo(canvas);
    }
    if (input.btnAPressed) startNewGame(audio);
    return true;
}

bool TubeFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _dist += BASE_SPEED * 0.5f * _frameScale;
    for (auto &o : _obstacles) if (o.active) placeObstacle(o);
    for (auto &c : _crystals) if (c.active) placeObstacle(c);
    renderWorld(canvas);
    renderGameOver(canvas);

    unsigned long elapsed = millis() - _phaseEnteredMs;
    if (elapsed > GAMEOVER_INPUT_DELAY_MS && input.btnAPressed) {
        startNewGame(audio);
    } else if (elapsed > GAMEOVER_TIMEOUT_MS) {
        for (auto &o : _obstacles) { o.active = false; o.obj->enabled = false; }
        hideTransients();
        enterAttract();
    }
    return true;
}

bool TubeFluxGame::updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    updateSteering(input);
    updateSpeed(input);
    updateTier(audio);
    updateBend();
    spawnObstacles();
    updatePickup(audio);
    tryFire(input, audio);
    updateShots(audio);        // before collisions: a point-blank shot still saves you
    updateObstacles(audio);
    _score = (long)(_dist * SCORE_PER_UNIT) + _bonus;

    updateShipSprite();
    renderWorld(canvas);
    drawHUD(canvas);
    drawOverlays(canvas);
    drawQuitHint(canvas);

    if (_shield <= 0) enterGameOver(audio);
    return true;
}

// The tunnel (drawn directly), then Jet's pass for the blocks, then
// particles. Jet draws the ship sprite at the end of render(), over it all.
void TubeFluxGame::renderWorld(GFXcanvas16 &canvas) {
    placeCamera();
    drawTunnel(canvas);
    _scene->render();
    // Not the real frame time: the step the particles were tuned against,
    // scaled like everything else (as in Tank Flux).
    _particles.update((1.0f / 60.0f) * _frameScale);
    _particles.render(_scene, &_camera, canvas.width(), canvas.height());
}

}  // namespace tubeflux
