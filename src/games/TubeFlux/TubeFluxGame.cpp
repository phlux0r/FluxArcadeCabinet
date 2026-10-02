#include "TubeFluxGame.h"

namespace tubeflux {

// Quitting (the cabinet's Back button): a run in progress still goes on the
// table (if it makes it), under the last name entered; a name being entered
// is kept.
void TubeFluxGame::onQuit(AudioEngine &audio) {
    if (_phase == PHASE_NAME) _scores.finishNow();
    else if (_phase == PHASE_PLAYING) _scores.record(_score);
    audio.mute();
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
    _scores.begin("tube");
    enterAttract();
    _lastFrameMs = millis();
    _angle = 0.0f;
    _rollVel = 0.0f;
    static const int n[] = { 440, 660, 880, 1320 };
    static const int d[] = {  80,  80,  80,  200 };
    audio.playMelody(n, d, 4);
    // Decoded into the mixer's cache now, so the first play isn't late.
    static const char* const sfx[] = { "/audio/tube_shot.wav", "/audio/explosion.wav",
                                       "/audio/tube_bump.wav", "/audio/powerup.wav" };
    for (const char* f : sfx) audio.preload(f);
}

void TubeFluxGame::startNewGame(AudioEngine &audio) {
    resetRun();
    _phase = PHASE_PLAYING;
    _phaseEnteredMs = millis();
    audio.playTone(900, 80);
    // Music plays during a run only: not on the attract screen or in its
    // demo, and it stops at game over (restarting with the next run).
    audio.loopWAV(TUBE_MUSIC);
}

// Everything a run starts from, shared by a real game and the attract demo.
void TubeFluxGame::resetRun() {
    _angle = 0.0f;
    _rollVel = 0.0f;
    _throttle = 1.0f;
    _dist = 0.0f;
    _tier = 1;
    _speed = tierSpeed();
    _shield = SHIELD_MAX;
    _score = 0;
    _bonus = 0;
    _scores.forget();
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
    _chaseCount = 0;
    _dronesDestroyed = _dronesEscaped = 0;
    _nextChaseAt = (float)(CHASE_FIRST_TIER - 1) * TIER_DISTANCE;   // the tier-6 gate
    _nextPortalAt = PORTAL_FIRST_AT;
    _portalsEntered = _portalsMissed = _bonusPerfects = 0;
    _gemsHit = _gemsTotal = 0;
    for (auto &p : _particles.pool) p.active = false;
    applyTierPalette();
}

// The shield's gone: a name for the table first, if the score made it.
void TubeFluxGame::enterGameOver(AudioEngine &audio) {
    _phase = _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
    _phaseEnteredMs = millis();
    _shipSprite.enabled = false;
    // Shots and the pickup would hang in mid-air while the world drifts on.
    for (auto &s : _shots) { s.active = false; s.obj->enabled = false; }
    _pickupActive = false;
    _chevronObj->enabled = false;
    _crossObj->enabled = false;
    hideChase();
    hideBonus();
    audio.stopLoop();
    static const int n[] = { 520, 390, 260, 130 };
    static const int d[] = { 120, 120, 120, 320 };
    audio.playMelody(n, d, 4);
}

bool TubeFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    ensureSceneReady(canvas);
    updateFrameScale();

    // Quitting is the cabinet's Back button (main.cpp, then onQuit()).
    switch (_phase) {
        case PHASE_ATTRACT:  return updateAttract(canvas, input, audio);
        case PHASE_NAME:     return updateName(canvas, input, audio);
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
    hideChase();
    hideBonus();
}

void TubeFluxGame::enterAttract() {
    _phase = PHASE_ATTRACT;
    _shipSprite.enabled = false;   // the demo flies it
    // The how-to-play slide flies a straight tunnel, whatever the last run ended on.
    _bendX = _bendY = _bendTargetX = _bendTargetY = 0.0f;
    _phaseEnteredMs = millis();
    _attractSlide = SLIDE_TITLE;
    _attractSlideAt = millis();
}

// Title image and how-to-play alternate. Behind how-to-play the tunnel
// keeps flowing, empty, at tier 1 speed; the title is a full-screen image,
// so the world isn't rendered at all while it shows.
// Title, how-to-play, the high scores, then a demo (TubeFluxDemo.cpp),
// round and round. A starts a game from any of them.
bool TubeFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnAPressed) {
        startNewGame(audio);
        return updatePlaying(canvas, InputState{}, audio);
    }
    if (_attractSlide != SLIDE_DEMO && millis() - _attractSlideAt > ATTRACT_SLIDE_MS) {
        if (_attractSlide == SLIDE_TITLE || _attractSlide == SLIDE_INFO) {
            _attractSlide = _attractSlide == SLIDE_TITLE ? SLIDE_INFO : SLIDE_SCORES;
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
        case SLIDE_SCORES:
            _dist += BASE_SPEED * _frameScale;
            _angle = fmodf(_angle + 0.6f * _frameScale, 360.0f);
            renderWorld(canvas);
            if (_attractSlide == SLIDE_INFO) renderAttractInfo(canvas);
            else renderAttractScores(canvas);
            break;
        case SLIDE_DEMO:
            updateDemo(canvas, audio);
            break;
    }
    return true;
}

// The tunnel drifts on behind the name entry; when it's done (or timed
// out), the game-over screen.
bool TubeFluxGame::updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _dist += BASE_SPEED * 0.5f * _frameScale;
    for (auto &o : _obstacles) if (o.active) placeObstacle(o);
    for (auto &c : _crystals) if (c.active) placeObstacle(c);
    renderWorld(canvas);
    _scores.draw(canvas);
    if (_scores.update(input, getRotation())) {
        _phase = PHASE_GAMEOVER;
        _phaseEnteredMs = millis();
        audio.playTone(1047, 80);
    }
    return true;
}

bool TubeFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _dist += BASE_SPEED * 0.5f * _frameScale;
    for (auto &o : _obstacles) if (o.active) placeObstacle(o);
    for (auto &c : _crystals) if (c.active) placeObstacle(c);
    renderWorld(canvas);
    renderGameOver(canvas);

    unsigned long elapsed = millis() - _phaseEnteredMs;
    if (elapsed > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS && input.btnAPressed) {
        startNewGame(audio);
    } else if (elapsed > GAMEOVER_TIMEOUT_MS) {
        for (auto &o : _obstacles) { o.active = false; o.obj->enabled = false; }
        hideTransients();
        enterAttract();
    }
    return true;
}

bool TubeFluxGame::updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    stepRun(input, audio);
    renderRun(canvas);
    if (_shield <= 0) enterGameOver(audio);
    return true;
}

// One frame of a run: the same for a real game and the demo.
void TubeFluxGame::stepRun(const InputState &input, AudioEngine &audio) {
    // The bonus tally pauses the game: nothing moves until it's done.
    if (_bonusPhase == BONUS_TALLY) {
        if (millis() - _tallyAt >= BONUS_TALLY_MS) endBonus();
        return;
    }
    updateSteering(input);
    updateSpeed(input);
    updateTier(audio);
    updateBend();
    updateChase(audio);
    updatePortal(audio);
    spawnObstacles();
    updatePickup(audio);
    tryFire(input, audio);
    updateShots(audio);        // before collisions: a point-blank shot still saves you
    updateBolts(audio);
    updateObstacles(audio);
    _score = (long)(_dist * SCORE_PER_UNIT) + _bonus;
}

void TubeFluxGame::renderRun(GFXcanvas16 &canvas) {
    updateShipSprite();
    renderWorld(canvas);
    drawHUD(canvas);
    drawOverlays(canvas);
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
