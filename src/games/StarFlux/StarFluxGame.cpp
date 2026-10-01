#include "StarFluxGame.h"

namespace starflux {

// Quitting mid-run: the score still goes on the table (if it makes it),
// under the last name entered. Never from the demo: its score isn't yours.
void StarFluxGame::recordQuit() {
    if (!inDemo()) _scores.record(_score);
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
    _scores.begin("star");
    enterAttract();
    _btnBWasHeld = true;
    _btnBHoldStart = 0;
    _lastFrameMs = millis();
    static const int n[] = { 523, 784, 1047, 1568 };
    static const int d[] = {  70,  70,  90,  220 };
    audio.playMelody(n, d, 4);
    findSounds(audio);
}

namespace {
// The optional sounds, by Sfx, and what plays when one isn't on the card.
struct SfxDef { const char* path; const char* fallback; int hz, ms; };
const SfxDef SFX[] = {
    { "/audio/star_pop.wav",       "/audio/explosion.wav", 0, 0 },     // SFX_POP
    { "/audio/star_hit.wav",       nullptr, 950, 25 },                 // SFX_HIT
    { "/audio/star_armor.wav",     nullptr, 1900, 12 },                // SFX_ARMOR
    { "/audio/star_boss_warn.wav", nullptr, 880, 160 },                // SFX_BOSS_WARN
    { "/audio/star_boss_fire.wav", nullptr, 300, 40 },                 // SFX_BOSS_FIRE
    { "/audio/star_burst.wav",     nullptr, 200, 120 },                // SFX_BURST
    { "/audio/star_part_down.wav", "/audio/explosion.wav", 0, 0 },     // SFX_PART_DOWN
    { "/audio/star_core_open.wav", nullptr, 600, 180 },                // SFX_CORE_OPEN
    { "/audio/star_boss_die.wav",  "/audio/explosion.wav", 0, 0 },     // SFX_BOSS_DIE
    { "/audio/star_bomb.wav",      "/audio/explosion.wav", 0, 0 },     // SFX_BOMB
    { "/audio/star_ring.wav",      "/audio/powerup.wav", 0, 0 },       // SFX_RING
    { "/audio/star_power.wav",     "/audio/powerup.wav", 0, 0 },       // SFX_POWER
    { "/audio/star_extra.wav",     nullptr, 1320, 220 },               // SFX_EXTRA
};
static_assert(sizeof(SFX) / sizeof(SFX[0]) == 13, "one SfxDef per Sfx");
}  // namespace

// Which optional sounds are on the card, checked once: a missing file
// would otherwise cost an SD open every time it's asked for. What will
// play is decoded into the mixer's cache now, so the first play isn't late.
void StarFluxGame::findSounds(AudioEngine &audio) {
    static const char* const always[] = { "/audio/tube_shot.wav", "/audio/tube_bump.wav" };
    for (const char* f : always) audio.preload(f);
    for (int i = 0; i < SFX_COUNT; ++i) {
        _sfxOnCard[i] = audio.exists(SFX[i].path);
        // The loader skips a preload of what's already cached, so a
        // fallback shared by several sounds is only read once.
        if (_sfxOnCard[i]) audio.preload(SFX[i].path);
        else if (SFX[i].fallback) audio.preload(SFX[i].fallback);
    }
}

void StarFluxGame::sfx(AudioEngine &audio, Sfx s) {
    if (_silent) return;
    const SfxDef &d = SFX[s];
    if (_sfxOnCard[s]) audio.playWAV(d.path);
    else if (d.fallback) audio.playWAV(d.fallback);
    else audio.playTone(d.hz, d.ms);
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
    _rapid = false;
    _nextLifeAt = EXTRA_LIFE_FIRST;
    _shield = SHIELD_MAX;
    _stageNum = STAGE_BELT;
    _lives = LIVES;
    _score = 0;
    _scores.forget();
    startStage();
}

// The stage (_stageNum) from the top: the fly-in, then segment 0.
void StarFluxGame::startStage() {
    clearField();
    applyStagePalette();
    _groundScroll = 0;
    _shield = max(_shield, SHIELD_MAX);   // overcharge carries over
    if (_bombs < BOMBS_START) _bombs = BOMBS_START;
    _shipX = 0; _shipY = BOX_Y_MIN; _shipVX = _shipVY = 0; _bank = 0;
    _fightersSeen = _fightersDowned = _targetsDowned = _ringsCaught = 0;
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
    _pod.active = false;
    for (auto &b : _blasts) b.active = false;
    for (auto &p : _particles.pool) p.active = false;
    _bombActive = false;
    _frozenUntil = 0;
    hideWorld();
    hideBoss();
}

// The last life's gone: a name for the table first, if the score made it.
void StarFluxGame::enterGameOver(AudioEngine &audio) {
    _phase = !inDemo() && _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
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
    // Not while entering a name, where B steps back a letter.
    if (_phase == PHASE_NAME) {
        _btnBHoldStart = 0;
    } else if (_btnBWasHeld) {
        if (!input.btnB) _btnBWasHeld = false;
    } else if (input.btnB) {
        if (_btnBHoldStart == 0) _btnBHoldStart = millis();
        if (millis() - _btnBHoldStart > EXIT_HOLD_MS) {
            _btnBHoldStart = 0;
            if (_phase == PHASE_PLAYING || _phase == PHASE_RESULTS) recordQuit();
            audio.mute();
            return false;
        }
    } else {
        _btnBHoldStart = 0;
    }

    switch (_phase) {
        case PHASE_ATTRACT:  return updateAttract(canvas, input, audio);
        case PHASE_RESULTS:  return updateResults(canvas, input, audio);
        case PHASE_NAME:     return updateName(canvas, input, audio);
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default:             return updatePlaying(canvas, input, audio);
    }
}

// Title, how-to-play, the high scores, then a demo (StarFluxDemo.cpp),
// round and round. A starts a game from any of them.
bool StarFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnAPressed) {
        if (inDemo()) endDemo();
        startNewGame(audio);
        _prevA = true;   // the press that started the game isn't a shot
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
            // Empty space flying past behind the text.
            _shipX *= 0.95f; _shipY *= 0.95f; _bank *= 0.9f;
            _groundScroll = fmodf(_groundScroll + FLY_SPEED * _frameScale, 100000.0f);
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

// The world drifts on behind the name entry; when it's done (or timed
// out), the game-over screen.
bool StarFluxGame::updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    renderWorld(canvas);
    _scores.draw(canvas);
    if (_scores.update(input, getRotation())) {
        _phase = PHASE_GAMEOVER;
        _phaseEnteredMs = millis();
        _btnBWasHeld = input.btnB;   // a B still down from the entry isn't the start of a quit
        audio.playTone(1047, 80);
    }
    return true;
}

bool StarFluxGame::updateResults(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _bank *= 0.9f;
    renderWorld(canvas);
    renderResults(canvas);
    unsigned long elapsed = millis() - _phaseEnteredMs;
    if ((elapsed > RESULTS_MIN_MS && input.btnAPressed) || elapsed > RESULTS_MAX_MS) {
        // The next stage; after the last, round again, harder.
        if (++_stageNum >= STAGE_COUNT) {
            _stageNum = STAGE_BELT;
            ++_loop;
        }
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
    _groundScroll = fmodf(_groundScroll + FLY_SPEED * _frameScale, 100000.0f);
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
    updateBoxes(audio);
    updateTurrets(audio);
    updateRings(audio);
    checkExtraLife(audio);
    updatePod(audio);
}

void StarFluxGame::renderRun(GFXcanvas16 &canvas) {
    updateShipSprite();
    renderWorld(canvas);
    drawHUD(canvas);
    drawOverlays(canvas);
    drawQuitHint(canvas);
}

// Backdrop, the ship's shadow and stars (drawn directly), rings, Jet's
// pass for the fighters, rocks, obstacles, turrets and boss, particles,
// then the 2D gates, the reactor's fan, lasers, shots, blasts and the
// reticle. Jet draws the ship sprite at the end of render(), so it sits
// over the meshes; what's drawn after is over the ship too, which suits
// lasers and flashes.
void StarFluxGame::renderWorld(GFXcanvas16 &canvas) {
    placeCamera();
    drawBackdrop(canvas);
    drawShadow(canvas);
    drawStars(canvas);
    drawRings(canvas);
    drawPod(canvas);
    _scene->render();
    _particles.update((1.0f / 60.0f) * _frameScale);
    _particles.render(_scene, &_camera, canvas.width(), canvas.height());
    drawGates(canvas);
    drawMines(canvas);   // over the meshes, as the gates: they're mostly the nearer
    drawFlightAids(canvas);
    drawFan(canvas);
    drawShots(canvas);
    drawBlasts(canvas);
    if (_phase == PHASE_PLAYING && _stage == STAGE_RUN) drawReticle(canvas);
}

}  // namespace starflux
