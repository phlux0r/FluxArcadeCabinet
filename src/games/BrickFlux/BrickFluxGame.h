#ifndef BRICK_FLUX_GAME_H
#define BRICK_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/HighScores.h"
#include "../../cabinet/ParticleManager.h"
#include "BrickConfig.h"
#include "BrickBoard.h"
#include "BrickBall.h"

// =============================================================================
// BRICK FLUX: a bat-and-ball brick breaker, portrait. What sets it apart
// (docs/design/BrickFlux.md has the whole design):
//   - the bat tilts (stick up/down), so each rebound is aimed;
//   - the Flux Smash: with the meter full, hold A and release it as the
//     ball meets the bat, and it ploughs through a column (Perfect: three);
//   - polarity: the bat is cyan or magenta (tap B), the ball takes its
//     colour, and coloured bricks break only to their own colour;
//   - the advancing wall: the formation creeps down a row at a time, and a
//     brick reaching the danger line costs a life;
//   - living bricks (guns, magnets, portals, sparks) and a boss every
//     fifth level.
//
// Files: BrickFluxGame.h (phases, attract, sounds), BrickPlay.h (the bat,
// balls, bricks, capsules and the wall), BrickLiving.h (polarity, guns,
// bolts, magnets, portals, sparks), BrickBosses.h, BrickRender.h
// (drawing), BrickAutopilot.h (the attract demo's player), BrickBoard.h
// (the grid), BrickBall.h (types and bounce maths), BrickLevels.h.
//
// One frame of play is stepPlay(input): the same for a real game and the
// demo, which feeds it the autopilot's input instead of the stick's.
// =============================================================================

namespace brickflux {

class BrickFluxGame : public IGame {
public:
    void init(AudioEngine &audio) override;
    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override;
    void onQuit(AudioEngine &audio) override;
    uint8_t getRotation() const override { return 2; }
    const char* getName() const override { return "Brick Flux"; }

private:
    // PICK: the stage-select cheat, choosing a level to start a test run on.
    enum Phase : uint8_t { PHASE_ATTRACT, PHASE_PICK, PHASE_PLAYING, PHASE_NAME, PHASE_GAMEOVER };
    enum Slide : uint8_t { SLIDE_TITLE, SLIDE_INFO, SLIDE_INFO2, SLIDE_INFO3, SLIDE_SCORES, SLIDE_DEMO };
    // Within a game: the level dropping in, play, the pause after the last
    // ball's gone, and the level-clear tally.
    enum Round : uint8_t { ROUND_INTRO, ROUND_PLAY, ROUND_LOST, ROUND_CLEAR };
    enum Sfx : uint8_t { SFX_BAT, SFX_BREAK, SFX_CRACK, SFX_CLANK, SFX_READY, SFX_SMASH, SFX_PERFECT,
                         SFX_STEP, SFX_TICK, SFX_CAPSULE, SFX_LOST, SFX_CLEAR, SFX_LASER, SFX_EXTRA,
                         SFX_SERVE, SFX_SWAP, SFX_ZAP, SFX_ABSORB, SFX_BOLT, SFX_PORTAL, SFX_SPARK,
                         SFX_BOSS_WARN, SFX_BOSS_HIT, SFX_BOSS_DIE, SFX_COUNT };

    // ---- Phases (this file) ----
    void enterAttract();
    void startNewGame(AudioEngine &audio, int first = -1);
    void resetRun(int level);
    void enterGameOver(AudioEngine &audio);
    void startDemo();
    void endDemo();
    bool updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void enterPicker();
    bool updatePicker(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void updateFrameTime();
    void findSounds(AudioEngine &audio);
    void sfx(Sfx s, int hz = 0);

    // ---- Play (BrickPlay.h) ----
    void loadLevel();
    void serveReset();
    void stepPlay(const InputState &in);
    void updateBat(const InputState &in);
    void updateButtonA(const InputState &in);
    void releaseHeld();
    void launchHeld(Ball &b);
    void moveBall(Ball &b, float dist);
    void batBounce(Ball &b);
    void smash(Ball &b, bool perfect);
    void hitCell(int r, int c, Ball *b, bool smashHit, int mult, uint8_t pol);
    void pierceCells(Ball &b);
    void afterBounce(Ball &b, bool useful);
    void brickBroken(float x, float y, uint16_t colour, int pts, int mult);
    void addScore(long pts);
    void updateCapsules();
    void applyCapsule(uint8_t kind);
    void endEffect();
    void updateShots();
    void fireLasers();
    void updateWall();
    void loseLife();
    void levelCleared();
    void addPopup(float x, float y, int32_t pts, uint8_t mult);
    float ballSpeed() const;
    float batW() const { return _effect == CAP_WIDE ? (float)BAT_W_WIDE : (float)BAT_W; }
    int  loopIndex() const { return min((_level - 1) / LEVELS_PER_LOOP, MAX_LOOPS - 1); }
    unsigned long wallStepMs() const;
    int  activeBalls() const;
    bool anyHeld() const;

    // ---- Polarity and living bricks (BrickLiving.h) ----
    void updateButtonB(const InputState &in);
    void mismatch(Ball &b, float x, float y);
    void flashAt(float x, float y, int w, int h);
    void applyMagnets(Ball &b);
    void checkPortal(Ball &b);
    void updateGuns();
    void fireBolt(float x, float y, float vx, float vy, uint8_t pol);
    void updateBolts();
    void dropSpark(float x, float y);
    void updateSparks();
    bool batCatches(float x0, float x1, float y0, float y1) const;

    // ---- Bosses (BrickBosses.h) ----
    void startBoss(int kind);
    void updateBoss();
    bool freeSolidAt(float x, float y, int &kind, int &idx) const;
    void hitFree(int kind, int idx, Ball *b, bool smashHit, bool perfect);
    void coreDown(int i);
    bool bossBeaten() const;
    int  bossHp(int base) const;
    void addSat(float x, float y, uint8_t pol, bool gun, int core, float angle, float ox, float oy);
    void hiveBreach();
    const char* bossName() const;
    void drawBoss(GFXcanvas16 &cv, int ox, int oy);

    // ---- Drawing (BrickRender.h) ----
    void renderPlay(GFXcanvas16 &cv);
    void drawField(GFXcanvas16 &cv);
    void drawBat(GFXcanvas16 &cv);
    void drawHud(GFXcanvas16 &cv);
    void drawRoundOverlay(GFXcanvas16 &cv);
    void renderTitle(GFXcanvas16 &cv);
    void renderInfo(GFXcanvas16 &cv, int page);
    void renderScores(GFXcanvas16 &cv);
    void renderPicker(GFXcanvas16 &cv);
    void renderGameOver(GFXcanvas16 &cv);
    void drawDemoOverlay(GFXcanvas16 &cv);
    static void panel(GFXcanvas16 &cv, int x, int y, int w, int h);

    // ---- Autopilot (BrickAutopilot.h) ----
    InputState autopilot();
    float apTiltFor(float landX, float dx, float dy, float targetX, float targetY) const;

    // ---- State ----
    hiscore::ScoreBoard _scores;
    ParticleManager _particles;
    BrickBoard _board;
    Ball    _balls[MAX_BALLS];
    Capsule _capsule;
    Shot    _shots[MAX_SHOTS];
    Popup   _popups[4];
    Bolt    _bolts[MAX_BOLTS];
    Spark   _sparks[MAX_SPARKS];
    struct Flash { float x = 0, y = 0; int8_t w = 0, h = 0; unsigned long until = 0; };
    Flash   _flashes[4];                     // a mismatched brick, briefly white-edged

    Phase _phase = PHASE_ATTRACT;
    Slide _slide = SLIDE_TITLE;
    Round _round = ROUND_INTRO;
    unsigned long _phaseAt = 0, _slideAt = 0, _roundAt = 0;
    unsigned long _now = 0, _lastFrameMs = 0;
    float _dt = 1.0f / 30.0f;                  // this frame's real time, seconds

    long _score = 0, _nextLifeAt = EXTRA_LIFE_FIRST;
    int  _lives = LIVES, _level = 1;
    int  _meter = 0;
    bool _lostThisLevel = false;
    float _lowestReached = 0;                 // the formation's lowest point this level
    long _clearBonus = 0;
    int  _headroomRows = 0;

    // Bat, and polarity
    float _batX = W / 2, _tilt = 0;
    uint8_t _batPol = POL_CYAN;
    bool _bHeld = false;
    unsigned long _swapAt = 0, _stunUntil = 0;
    int  _chain = 1;                          // the next coloured brick's multiplier
    int  _sparksCaught = 0;                   // this level
    bool _sparkLife = false;                  // its extra life given
    // A boss
    uint8_t _boss = BOSS_NONE;
    Core _cores[2];
    Sat  _sats[MAX_SATS];
    unsigned long _bossNextAt = 0, _twinSwapAt = 0;
    int  _bossVolley = 0;
    float _wardenTurn = 0;                    // the Warden's ring, degrees
    bool _twinSwapping = false;
    float _twinFrom[2] = {}, _twinTo[2] = {};
    // A and the Flux Smash
    bool _aHeld = false, _charging = false;
    unsigned long _aDownAt = 0, _releaseAt = 0;
    unsigned long _shakeUntil = 0, _perfectFlashUntil = 0;
    // Capsules: one timed effect at a time
    uint8_t _effect = CAP_COUNT;
    unsigned long _effectUntil = 0, _laserReadyAt = 0;
    // The wall
    unsigned long _wallElapsed = 0;
    bool _warned = false;
    bool _serving = true;                     // a ball on the bat, waiting for A

    // The stage-select cheat: a test run puts nothing on the table
    bool _test = false;
    int  _testFrom = 1;                       // the level it started on (A at its game over)
    int  _pick = 1, _pickDir = 0;             // the picker's level; the stick's last step
    unsigned long _pickRepeatAt = 0, _pickAt = 0;

    // Demo
    bool _demo = false;
    unsigned long _demoUntil = 0;
    bool _apHolding = false;                  // autopilot: A held for a smash
    unsigned long _apServeAt = 0, _apLaserAt = 0;
    float _apReleaseMs = 30;                  // its release lead for this smash
    bool _apPrevA = false, _apPrevB = false;
    float _apTarget = 0;                      // the tilt it's settling on

    // Running totals, for the host harness (test/brickflux_harness.cpp).
    long _statSmashes = 0, _statPerfects = 0, _statCleared = 0, _statSteps = 0;
    long _statWallLives = 0, _statCapsules = 0, _statBosses = 0, _statMismatches = 0;
    long _statAbsorbed = 0, _statStunned = 0, _statTeleports = 0, _statSparks = 0, _statSwaps = 0;

    bool _sfxOnCard[SFX_COUNT] = {};
    bool _musicOnCard = false;
    bool _silent = false;
    AudioEngine *_audio = nullptr;            // for sfx() from deep in play
};

// =============================================================================

namespace {
// The optional sounds, by Sfx, and what plays when one isn't on the card.
struct SfxDef { const char* path; const char* fallback; int hz, ms; };
const SfxDef BRICK_SFX[] = {
    { "/audio/brick_bat.wav",     nullptr, 880, 20 },                  // SFX_BAT
    { "/audio/brick_break.wav",   nullptr, 1200, 15 },                 // SFX_BREAK
    { "/audio/brick_crack.wav",   nullptr, 440, 25 },                  // SFX_CRACK
    { "/audio/brick_clank.wav",   nullptr, 220, 30 },                  // SFX_CLANK
    { "/audio/brick_ready.wav",   nullptr, 1568, 90 },                 // SFX_READY
    { "/audio/brick_smash.wav",   "/audio/explosion.wav", 0, 0 },      // SFX_SMASH
    { "/audio/brick_perfect.wav", "/audio/explosion.wav", 0, 0 },      // SFX_PERFECT
    { "/audio/brick_step.wav",    nullptr, 80, 60 },                   // SFX_STEP
    { "/audio/brick_tick.wav",    nullptr, 1400, 10 },                 // SFX_TICK
    { "/audio/powerup.wav",       nullptr, 1000, 120 },                // SFX_CAPSULE
    { "/audio/death.wav",         nullptr, 150, 300 },                 // SFX_LOST
    { "/audio/brick_clear.wav",   nullptr, 1047, 250 },                // SFX_CLEAR
    { "/audio/tube_shot.wav",     nullptr, 1800, 15 },                 // SFX_LASER
    { "/audio/brick_extra.wav",   nullptr, 1320, 220 },                // SFX_EXTRA
    { "/audio/brick_serve.wav",   nullptr, 660, 30 },                  // SFX_SERVE
    { "/audio/brick_swap.wav",    nullptr, 1000, 40 },                 // SFX_SWAP (pitch by colour)
    { "/audio/brick_zap.wav",     nullptr, 300, 120 },                 // SFX_ZAP (stunned)
    { "/audio/brick_absorb.wav",  nullptr, 1320, 15 },                 // SFX_ABSORB
    { "/audio/brick_bolt.wav",    nullptr, 500, 20 },                  // SFX_BOLT
    { "/audio/brick_portal.wav",  nullptr, 1600, 40 },                 // SFX_PORTAL
    { "/audio/pickup.wav",        nullptr, 1760, 60 },                 // SFX_SPARK
    { "/audio/brick_boss_warn.wav", nullptr, 880, 160 },               // SFX_BOSS_WARN
    { "/audio/brick_boss_hit.wav", nullptr, 600, 30 },                 // SFX_BOSS_HIT
    { "/audio/brick_boss_die.wav", "/audio/star_boss_die.wav", 0, 0 }, // SFX_BOSS_DIE
};
static_assert(sizeof(BRICK_SFX) / sizeof(BRICK_SFX[0]) == 24, "one SfxDef per Sfx");

// While a demo runs, new sounds are dropped (lifted again whichever way
// update() returns).
struct BrickSilence {
    AudioEngine &a; bool on;
    BrickSilence(AudioEngine &a_, bool on_) : a(a_), on(on_) { if (on) a.setSilenced(true); }
    ~BrickSilence() { if (on) a.setSilenced(false); }
};
}  // namespace

// Which optional sounds are on the card, checked once: a missing file
// would otherwise cost an SD open every time it's asked for. What will
// play is decoded into the mixer's cache now, so the first play isn't late.
inline void BrickFluxGame::findSounds(AudioEngine &audio) {
    for (int i = 0; i < SFX_COUNT; ++i) {
        _sfxOnCard[i] = audio.exists(BRICK_SFX[i].path);
        if (_sfxOnCard[i]) audio.preload(BRICK_SFX[i].path);
        else if (BRICK_SFX[i].fallback) audio.preload(BRICK_SFX[i].fallback);
    }
    _musicOnCard = audio.exists(MUSIC);
}

// `hz`, if given, replaces the fallback tone's pitch (a chain's breaks
// climb, a swap is high for cyan and low for magenta). Nothing plays in a
// demo, including what it sets off while starting (a boss's warning).
inline void BrickFluxGame::sfx(Sfx s, int hz) {
    if (_silent || _demo || !_audio) return;
    const SfxDef &d = BRICK_SFX[s];
    if (_sfxOnCard[s]) _audio->playWAV(d.path);
    else if (d.fallback) _audio->playWAV(d.fallback);
    else _audio->playTone(hz ? hz : d.hz, d.ms);
}

inline void BrickFluxGame::init(AudioEngine &audio) {
    _audio = &audio;
    _scores.begin("brick");
    _lastFrameMs = millis();
    findSounds(audio);
    resetRun(1);
    enterAttract();
    static const int n[] = { 523, 784, 659, 1047 };
    static const int d[] = {  70,  70,  70,  180 };
    audio.playMelody(n, d, 4);
}

// Movement is in pixels per second, scaled by the real frame time, so the
// game plays at the same speed whatever the frame rate. A long stall
// (an SD read) is capped, so nothing jumps.
inline void BrickFluxGame::updateFrameTime() {
    _now = millis();
    unsigned long dt = _now - _lastFrameMs;
    _lastFrameMs = _now;
    if (dt < 10) dt = 10;
    if (dt > 50) dt = 50;
    _dt = dt / 1000.0f;
}

inline bool BrickFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _audio = &audio;
    updateFrameTime();

    // Quitting is the cabinet's Back button (main.cpp, then onQuit()); B
    // swaps the bat's colour.
    switch (_phase) {
        case PHASE_ATTRACT:  return updateAttract(canvas, input, audio);
        case PHASE_PICK:     return updatePicker(canvas, input, audio);
        case PHASE_NAME:     return updateName(canvas, input, audio);
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default: break;
    }
    stepPlay(input);
    renderPlay(canvas);
    if (_round == ROUND_LOST && _lives <= 0 && _now - _roundAt >= LOST_MS) enterGameOver(audio);
    return true;
}

// Quitting (the Back button): a game in progress still goes on the table,
// under the last name entered (not a test run's); a name being entered is
// kept.
inline void BrickFluxGame::onQuit(AudioEngine &audio) {
    if (_phase == PHASE_NAME) _scores.finishNow();
    else if (_phase == PHASE_PLAYING && !_test) _scores.record(_score);
    audio.mute();
}

inline void BrickFluxGame::enterAttract() {
    _phase = PHASE_ATTRACT;
    _test = false;
    _slide = SLIDE_TITLE;
    _phaseAt = _slideAt = millis();
}

// Everything a run starts from, shared by a real game and the demo.
inline void BrickFluxGame::resetRun(int level) {
    _score = 0;
    _nextLifeAt = EXTRA_LIFE_FIRST;
    _lives = LIVES;
    _level = level;
    _meter = 0;
    _scores.forget();
    _particles.clearAll();
    for (auto &p : _popups) p.active = false;
    _batX = W / 2;
    _tilt = 0;
    _batPol = POL_CYAN;
    _chain = 1;
    _stunUntil = 0;
    _aHeld = _charging = false;
    _releaseAt = 0;
    loadLevel();
}

// `first`, if given, is the stage-select cheat's level: a test run that
// starts there and puts nothing on the table.
inline void BrickFluxGame::startNewGame(AudioEngine &audio, int first) {
    _test = first > 0;
    _testFrom = _test ? first : 1;
    resetRun(_testFrom);
    _phase = PHASE_PLAYING;
    _phaseAt = millis();
    _aHeld = true;                 // the A that started it isn't a serve or a charge
    audio.playTone(900, 80);
    // Music plays during a game only, as in the other games.
    if (_musicOnCard) audio.loopWAV(MUSIC);
}

// The last life's gone: a name for the table first, if the score made it
// (never for a test run).
inline void BrickFluxGame::enterGameOver(AudioEngine &audio) {
    _phase = !_test && _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
    _phaseAt = millis();
    audio.stopLoop();
    static const int n[] = { 520, 390, 260, 130 };
    static const int d[] = { 120, 120, 120, 320 };
    audio.playMelody(n, d, 4);
}

// The demo: a random level, the autopilot playing it silently.
inline void BrickFluxGame::startDemo() {
    _demo = true;
    _slide = SLIDE_DEMO;
    // A random level, one time in four the Warden (level 5).
    int level = (int)random(DEMO_MIN_LEVEL, DEMO_MAX_LEVEL + 1);
    if (random(4) == 0) level = BOSS_EVERY;
    resetRun(level);
    _demoUntil = millis() + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
    _apHolding = false;
    _apServeAt = 0;
    _apPrevA = _apPrevB = false;
}

// Back to the title, leaving nothing of the demo behind.
inline void BrickFluxGame::endDemo() {
    _demo = false;
    resetRun(1);
    enterAttract();
}

// Title, three how-to-play slides, the high scores (ATTRACT_SLIDE_MS each),
// then the demo, round and round. A starts a game from any of them; B held
// with A (not in the demo) opens the stage select.
inline bool BrickFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnAPressed && input.btnB && _slide != SLIDE_DEMO) {
        enterPicker();
        return updatePicker(canvas, InputState{}, audio);
    }
    if (input.btnAPressed) {
        if (_demo) endDemo();
        startNewGame(audio);
        renderPlay(canvas);
        return true;
    }
    if (_slide == SLIDE_DEMO) {
        if (_now >= _demoUntil || (_lives <= 0 && _round == ROUND_LOST && _now - _roundAt >= LOST_MS)) {
            endDemo();
            renderTitle(canvas);
            return true;
        }
        BrickSilence quiet(audio, true);
        _silent = true;
        stepPlay(autopilot());
        _silent = false;
        renderPlay(canvas);
        drawDemoOverlay(canvas);
        return true;
    }
    if (_now - _slideAt > ATTRACT_SLIDE_MS) {
        _slideAt = _now;
        if (_slide == SLIDE_SCORES) { startDemo(); renderPlay(canvas); return true; }
        _slide = (Slide)(_slide + 1);
    }
    switch (_slide) {
        case SLIDE_TITLE: renderTitle(canvas); break;
        case SLIDE_INFO:  renderInfo(canvas, 0); break;
        case SLIDE_INFO2: renderInfo(canvas, 1); break;
        case SLIDE_INFO3: renderInfo(canvas, 2); break;
        default:          renderScores(canvas); break;
    }
    return true;
}

// The stage-select cheat, for trying any level without playing up to it:
// the stick steps through one loop's levels (left and right by one, up and
// down by five, so boss to boss), each shown behind its number; A starts a
// test run there, B goes back to the title, as does leaving it alone.
inline void BrickFluxGame::enterPicker() {
    _phase = PHASE_PICK;
    _pick = 1;
    _pickDir = 0;
    _pickAt = _now;
    resetRun(_pick);
}

inline bool BrickFluxGame::updatePicker(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnAPressed) {
        startNewGame(audio, _pick);
        renderPlay(canvas);
        return true;
    }
    if (input.btnBPressed || _now - _pickAt > PICK_TIMEOUT_MS) {
        resetRun(1);
        enterAttract();
        renderTitle(canvas);
        return true;
    }
    bool up, down, left, right;
    hiscore::screenDirs(input, getRotation(), up, down, left, right);
    const int dir = right ? 1 : left ? -1 : up ? BOSS_EVERY : down ? -BOSS_EVERY : 0;
    bool step = false;
    if (dir != _pickDir) {
        _pickDir = dir;
        _pickRepeatAt = _now + PICK_REPEAT_DELAY_MS;
        step = dir != 0;
    } else if (dir != 0 && (long)(_now - _pickRepeatAt) >= 0) {
        _pickRepeatAt = _now + PICK_REPEAT_MS;
        step = true;
    }
    if (step) {
        _pick = (_pick - 1 + dir + LEVELS_PER_LOOP) % LEVELS_PER_LOOP + 1;
        _pickAt = _now;
        resetRun(_pick);
        audio.playTone(1200, 15);
    }
    drawField(canvas);
    renderPicker(canvas);
    return true;
}

// The board stays behind the name entry; when it's done (or timed out),
// the game-over screen.
inline bool BrickFluxGame::updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _particles.update();
    drawField(canvas);
    _scores.draw(canvas);
    if (_scores.update(input, getRotation())) {
        _phase = PHASE_GAMEOVER;
        _phaseAt = millis();
        audio.playTone(1047, 80);
    }
    return true;
}

inline bool BrickFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _particles.update();
    drawField(canvas);
    renderGameOver(canvas);
    const unsigned long elapsed = _now - _phaseAt;
    if (elapsed > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS && input.btnAPressed) {
        startNewGame(audio, _test ? _testFrom : -1);   // a test run: from its level again
    } else if (elapsed > GAMEOVER_TIMEOUT_MS) {
        resetRun(1);
        enterAttract();
    }
    return true;
}

}  // namespace brickflux

#include "BrickPlay.h"
#include "BrickLiving.h"
#include "BrickBosses.h"
#include "BrickRender.h"
#include "BrickAutopilot.h"

using BrickFluxGame = brickflux::BrickFluxGame;

#endif  // BRICK_FLUX_GAME_H
