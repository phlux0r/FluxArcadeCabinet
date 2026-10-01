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
//   - the advancing wall: the formation creeps down a row at a time, and a
//     brick reaching the danger line costs a life.
// Polarity, living bricks and bosses come in the second stage.
//
// Files: BrickFluxGame.h (phases, attract, sounds), BrickPlay.h (the bat,
// balls, bricks, capsules and the wall), BrickRender.h (drawing),
// BrickAutopilot.h (the attract demo's player), BrickBoard.h (the grid),
// BrickBall.h (ball types and bounce maths), BrickLevels.h (layouts).
//
// One frame of play is stepPlay(input): the same for a real game and the
// demo, which feeds it the autopilot's input instead of the stick's.
// =============================================================================

namespace brickflux {

class BrickFluxGame : public IGame {
public:
    void init(AudioEngine &audio) override;
    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override;
    uint8_t getRotation() const override { return 2; }
    const char* getName() const override { return "Brick Flux"; }

private:
    enum Phase : uint8_t { PHASE_ATTRACT, PHASE_PLAYING, PHASE_NAME, PHASE_GAMEOVER };
    enum Slide : uint8_t { SLIDE_TITLE, SLIDE_INFO, SLIDE_INFO2, SLIDE_SCORES, SLIDE_DEMO };
    // Within a game: the level dropping in, play, the pause after the last
    // ball's gone, and the level-clear tally.
    enum Round : uint8_t { ROUND_INTRO, ROUND_PLAY, ROUND_LOST, ROUND_CLEAR };
    enum Sfx : uint8_t { SFX_BAT, SFX_BREAK, SFX_CRACK, SFX_CLANK, SFX_READY, SFX_SMASH, SFX_PERFECT,
                         SFX_STEP, SFX_TICK, SFX_CAPSULE, SFX_LOST, SFX_CLEAR, SFX_LASER, SFX_EXTRA,
                         SFX_SERVE, SFX_COUNT };

    // ---- Phases (this file) ----
    void enterAttract();
    void startNewGame(AudioEngine &audio);
    void resetRun(int level);
    void enterGameOver(AudioEngine &audio);
    void startDemo();
    void endDemo();
    bool updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void updateFrameTime();
    void findSounds(AudioEngine &audio);
    void sfx(Sfx s);

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
    void hitCell(int r, int c, Ball *b, bool smashHit, int mult);
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

    // ---- Drawing (BrickRender.h) ----
    void renderPlay(GFXcanvas16 &cv);
    void drawField(GFXcanvas16 &cv);
    void drawBat(GFXcanvas16 &cv);
    void drawHud(GFXcanvas16 &cv);
    void drawRoundOverlay(GFXcanvas16 &cv);
    void drawQuitHint(GFXcanvas16 &cv);
    void renderTitle(GFXcanvas16 &cv);
    void renderInfo(GFXcanvas16 &cv, bool second);
    void renderScores(GFXcanvas16 &cv);
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

    // Bat
    float _batX = W / 2, _tilt = 0;
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

    // Quit (hold B), name entry, demo
    bool _btnBWasHeld = true;
    unsigned long _btnBHoldStart = 0;
    bool _demo = false;
    unsigned long _demoUntil = 0;
    bool _apHolding = false;                  // autopilot: A held for a smash
    unsigned long _apServeAt = 0, _apLaserAt = 0;
    float _apReleaseMs = 30;                  // its release lead for this smash
    bool _apPrevA = false;
    float _apTarget = 0;                      // the tilt it's settling on

    // Running totals, for the host harness (test/brickflux_harness.cpp).
    long _statSmashes = 0, _statPerfects = 0, _statCleared = 0, _statSteps = 0;
    long _statWallLives = 0, _statCapsules = 0;

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
};
static_assert(sizeof(BRICK_SFX) / sizeof(BRICK_SFX[0]) == 15, "one SfxDef per Sfx");

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

inline void BrickFluxGame::sfx(Sfx s) {
    if (_silent || !_audio) return;
    const SfxDef &d = BRICK_SFX[s];
    if (_sfxOnCard[s]) _audio->playWAV(d.path);
    else if (d.fallback) _audio->playWAV(d.fallback);
    else _audio->playTone(d.hz, d.ms);
}

inline void BrickFluxGame::init(AudioEngine &audio) {
    _audio = &audio;
    _scores.begin("brick");
    _btnBWasHeld = true;
    _btnBHoldStart = 0;
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

    // Hold B to exit, in every phase but the name entry (where B steps back
    // a letter). After init() B must be released first, so the launcher
    // press that started us can't count.
    if (_phase == PHASE_NAME) {
        _btnBHoldStart = 0;
    } else if (_btnBWasHeld) {
        if (!input.btnB) _btnBWasHeld = false;
    } else if (input.btnB) {
        if (_btnBHoldStart == 0) _btnBHoldStart = _now;
        if (_now - _btnBHoldStart > EXIT_HOLD_MS) {
            _btnBHoldStart = 0;
            // Quitting mid-game: the score still goes on the table, under
            // the last name entered.
            if (_phase == PHASE_PLAYING) _scores.record(_score);
            audio.mute();
            return false;
        }
    } else {
        _btnBHoldStart = 0;
    }

    switch (_phase) {
        case PHASE_ATTRACT:  return updateAttract(canvas, input, audio);
        case PHASE_NAME:     return updateName(canvas, input, audio);
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default: break;
    }
    stepPlay(input);
    renderPlay(canvas);
    drawQuitHint(canvas);
    if (_round == ROUND_LOST && _lives <= 0 && _now - _roundAt >= LOST_MS) enterGameOver(audio);
    return true;
}

inline void BrickFluxGame::enterAttract() {
    _phase = PHASE_ATTRACT;
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
    _aHeld = _charging = false;
    _releaseAt = 0;
    loadLevel();
}

inline void BrickFluxGame::startNewGame(AudioEngine &audio) {
    resetRun(1);
    _phase = PHASE_PLAYING;
    _phaseAt = millis();
    _aHeld = true;                 // the A that started it isn't a serve or a charge
    audio.playTone(900, 80);
    // Music plays during a game only, as in the other games.
    if (_musicOnCard) audio.loopWAV(MUSIC);
}

// The last life's gone: a name for the table first, if the score made it.
inline void BrickFluxGame::enterGameOver(AudioEngine &audio) {
    _phase = _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
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
    resetRun((int)random(DEMO_MIN_LEVEL, DEMO_MAX_LEVEL + 1));
    _demoUntil = millis() + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
    _apHolding = false;
    _apServeAt = 0;
    _apPrevA = false;
}

// Back to the title, leaving nothing of the demo behind.
inline void BrickFluxGame::endDemo() {
    _demo = false;
    resetRun(1);
    enterAttract();
}

// Title, two how-to-play slides, the high scores (ATTRACT_SLIDE_MS each),
// then the demo, round and round. A starts a game from any of them.
inline bool BrickFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
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
        case SLIDE_INFO:  renderInfo(canvas, false); break;
        case SLIDE_INFO2: renderInfo(canvas, true); break;
        default:          renderScores(canvas); break;
    }
    drawQuitHint(canvas);
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
        _btnBWasHeld = input.btnB;   // a B still down from the entry isn't the start of a quit
        audio.playTone(1047, 80);
    }
    return true;
}

inline bool BrickFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _particles.update();
    drawField(canvas);
    renderGameOver(canvas);
    drawQuitHint(canvas);
    const unsigned long elapsed = _now - _phaseAt;
    if (elapsed > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS && input.btnAPressed) {
        startNewGame(audio);
    } else if (elapsed > GAMEOVER_TIMEOUT_MS) {
        resetRun(1);
        enterAttract();
    }
    return true;
}

}  // namespace brickflux

#include "BrickPlay.h"
#include "BrickRender.h"
#include "BrickAutopilot.h"

using BrickFluxGame = brickflux::BrickFluxGame;

#endif  // BRICK_FLUX_GAME_H
