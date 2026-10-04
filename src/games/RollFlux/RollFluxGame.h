#ifndef ROLL_FLUX_GAME_H
#define ROLL_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/HighScores.h"
#include <Jet.hpp>

#include "RollFluxConfig.h"
#include "RollCourses.h"

// =============================================================================
// ROLL FLUX: tilt the course to roll a ball to the goal before the clock
// runs out (docs/design/RollFlux.md).
//
// Everything is drawn straight into the canvas, far to near, with Jet's
// camera maths (its Camera's projection and rotation tables) but not its
// Scene: stage 0 measured the floor that way at ~55fps on the board against
// ~25 through Jet. Drawing the ball, its shadow and the gems in the same
// pass is what lets the course hide them, so a ball falling behind an edge
// goes behind it.
//
// Files: RollFluxGame.cpp (phases, rules, camera), RollFluxPhysics.cpp (the
// ball and the dash), RollFluxScene.cpp (drawing), RollFluxHud.cpp (HUD
// and screens), RollFluxDemo.cpp (the autopilot and the attract cycle).
// =============================================================================

namespace rollflux {

class RollFluxGame : public IGame {
public:
    RollFluxGame() {}

    void init(AudioEngine &audio) override;
    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override;
    void onQuit(AudioEngine &audio) override;

    uint8_t getRotation() const override { return 1; }
    const char* getName() const override { return "Roll Flux"; }

private:
    // NAME: entering a name for the high-score table, after the last ball.
    // PICK: the stage-select cheat, choosing a course to start a test run on.
    enum GamePhase { PHASE_ATTRACT, PHASE_PICK, PHASE_PLAYING, PHASE_CLEAR, PHASE_NAME, PHASE_GAMEOVER };
    enum AttractSlide { SLIDE_TITLE, SLIDE_ROLL, SLIDE_DASH, SLIDE_PRISM, SLIDE_SCORES, SLIDE_DEMO };
    enum Kind : uint8_t { K_VOID, K_FLOOR, K_START, K_GOAL, K_RAMP_N, K_RAMP_S, K_RAMP_E, K_RAMP_W,
                          K_ICE, K_CHECK, K_BOOST_N, K_BOOST_S, K_BOOST_E, K_BOOST_W,
                          // Stage 2 (the Prism Works): colour gates and phase bridges, cyan
                          // then magenta; crystal walls; bumpers; conveyors.
                          K_GATE_C, K_GATE_M, K_BRIDGE_C, K_BRIDGE_M, K_CRYSTAL, K_BUMPER,
                          K_CONV_N, K_CONV_S, K_CONV_E, K_CONV_W,
                          // The guardians' arenas: pistons, a core, a ring that opens.
                          K_PISTON, K_CORE, K_RING };
    enum CellFlag : uint8_t { F_RAIL = 1, F_GEM = 2, F_TAKEN = 4 };
    enum Dir : uint8_t { D_N, D_S, D_E, D_W };
    struct Cell { uint8_t kind = K_VOID, h = 0, flags = 0; };
    // Optional sounds on the card, each with a fallback (RollFluxGame.cpp).
    enum Sfx : uint8_t { SFX_BUMP, SFX_GEM, SFX_BOOST, SFX_CHARGE, SFX_DASH, SFX_CHECK, SFX_FALL,
                         SFX_GOAL, SFX_SWAP, SFX_GATE, SFX_CRYSTAL, SFX_BUMPER,
                         SFX_GUARD_WARN, SFX_GUARD_HIT, SFX_GUARD_DOWN, SFX_SLAM, SFX_COUNT };

    static bool isRamp(uint8_t k)  { return k >= K_RAMP_N && k <= K_RAMP_W; }
    static bool isBoost(uint8_t k) { return k >= K_BOOST_N && k <= K_BOOST_W; }
    static bool isGate(uint8_t k)   { return k == K_GATE_C || k == K_GATE_M; }
    static bool isBridge(uint8_t k) { return k == K_BRIDGE_C || k == K_BRIDGE_M; }
    static bool isConveyor(uint8_t k) { return k >= K_CONV_N && k <= K_CONV_W; }
    // The colour a cell needs the ball to be (a gate's to pass, a bridge's
    // to stand on): 0 cyan, 1 magenta, -1 either.
    int  needsColour(int c, int r) const {
        if (!solid(c, r)) return -1;
        const uint8_t k = _cells[r][c].kind;
        return k == K_GATE_C || k == K_BRIDGE_C ? 0 : k == K_GATE_M || k == K_BRIDGE_M ? 1 : -1;
    }
    // There and solid to this ball: a phase bridge only in its own colour.
    bool standable(int c, int r) const {
        return solid(c, r) && !(isBridge(_cells[r][c].kind) && needsColour(c, r) != _polarity)
            && !(_cells[r][c].kind == K_RING && ringOpen(c, r));
    }

    // --- RollFluxGame.cpp ---
    void updateFrameScale();
    void findSounds(AudioEngine &audio);
    void sfx(Sfx s);
    void startNewGame(AudioEngine &audio, int first = -1);
    void loadCourse(int index);
    void startCourse(AudioEngine &audio);
    void respawn();
    void loseLife(AudioEngine &audio, const char* why);
    void stepRules(AudioEngine &audio);
    void collectGems(AudioEngine &audio);
    void reachGoal(AudioEngine &audio);
    void updateDash(const InputState &in);
    void updateSwap(const InputState &in);
    void dashDirection(const InputState &in, float &dx, float &dz) const;
    void updateLean(const InputState &in);
    void updateCamera(bool snap);
    void enterGameOver(AudioEngine &audio);
    bool updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateClear(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void banner(const char* text, uint16_t colour, unsigned long ms = 1500);
    // The synth's sounds note when they'll be over, so the rolling ticks
    // (which share it, and would cut a sound short) wait for them.
    void sfxMelody(const int* n, const int* d, int len) {
        if (_silent || !_audio) return;
        _audio->playMelody(n, d, len);
        unsigned long ms = 0;
        for (int i = 0; i < len; ++i) ms += d[i];
        _synthUntil = millis() + ms;
    }
    void sfxTone(int hz, int ms) {
        if (_silent || !_audio) return;
        _audio->playTone(hz, ms);
        _synthUntil = millis() + ms;
    }
    void rollTicks();
    const CourseDef &courseDef() const { return *_def; }   // the course loaded

    // --- RollFluxPhysics.cpp ---
    void stepBall(const InputState &in);
    void rollBall(float dx, float dz);
    void startDash(float dx, float dz, float strength);
    int  blockedAt(float px, float pz, int ownC, int ownR, float base, float rise);
    // Moving parts (RollFluxMovers.cpp).
    void updateMovers();
    void carryBall();
    void findOnMover();
    void sweepers();
    // The guardians (RollFluxGuardians.cpp).
    void guardianSetup();
    void updateGuardian();
    void guardianHits();
    void hitGuardian();
    float extraLift(int c, int r) const;          // a piston's or core's rise now
    bool ringOpen(int c, int r) const;             // the Gyre's ring, open there now
    bool ringWarning(int c, int r) const;          // about to open
    bool pistonWarning(int c, int r) const;        // about to slam
    bool present(int c, int r) const { return solid(c, r) && !(_cells[r][c].kind == K_RING && ringOpen(c, r)); }
    bool guardianCourse() const { return courseDef().guardian != 0; }
    InputState guardianPilot();
    void drawNode(uint16_t* buf, int w, int h, int n);
    void drawGuardianBar(GFXcanvas16 &canvas);
    bool moverFloor(float x, float z, float &y, int* which = nullptr) const;
    bool moverReady(int k, bool atB, unsigned long needMs) const;
    void moverAt(const MoverDef &m, unsigned long t, float &x, float &z, float &y, float &ang) const;
    void smashCrystal(int c, int r);
    void bumpers();
    void swapColour();
    bool floorAt(float x, float z, float &y) const;
    void stickToWorld(const InputState &in, float &ax, float &az) const;
    bool railed(int c, int r, int dir) const;
    int  colAt(float x) const { return x < 0 ? -1 : (int)(x / CELL); }
    int  rowAt(float z) const { return z < 0 ? _h : _h - 1 - (int)(z / CELL); }
    int  dashSteps() const { return _dashGems / DASH_GEMS_PER_STEP; }

    // --- RollFluxDemo.cpp ---
    void enterAttract();
    bool updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void orbitCamera();
    void startDemo(AudioEngine &audio);
    void endDemo();
    void enterPicker();
    bool updatePicker(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool canRoll(int c, int r, int nc, int nr, bool &drop) const;
    bool exposed(int c, int r) const;
    void planTo(int tc, int tr);
    void pickTarget();
public:   // the autopilot is also the host harness's bot (test/rollflux_harness.cpp)
    InputState pilot(bool useDash);
    // Cost of the cheapest way from (c, r) to every cell, by planDistances.
    void planDistances(int c, int r);
    int  planDist(int c, int r) const { return _dist[r * MAX_COURSE_W + c]; }
private:

    // --- RollFluxScene.cpp ---
    void ensureReady(GFXcanvas16 &canvas);
    void buildSky();
    void cornerHeights(int c, int r, float out[4]) const;   // sw, se, ne, nw
    float planeAt(int c, int r, float x, float z) const;     // the cell's top, extended
    bool solid(int c, int r) const { return c >= 0 && r >= 0 && c < _w && r < _h && _cells[r][c].kind != K_VOID; }
    void computeCameraMatrix();
    void toCam(float x, float y, float z, float* out) const;
    void drawSky(GFXcanvas16 &canvas);
    void drawStars(GFXcanvas16 &canvas);
    void drawWorld(GFXcanvas16 &canvas, bool withBall);
    void drawPoly(uint16_t* buf, int w, int h, const float (*p)[3], int n, uint16_t colour, bool shade = false);
    void drawCell(uint16_t* buf, int w, int h, int c, int r, float depth);
    void drawRail(uint16_t* buf, int w, int h, int c, int r, int dir, float depth);
    void drawBall(uint16_t* buf, int w, int h);
    void drawShadow(uint16_t* buf, int w, int h, float floorY);
    void drawGem(uint16_t* buf, int w, int h, int c, int r);
    void drawAim(uint16_t* buf, int w, int h);
    void drawGateBars(uint16_t* buf, int w, int h, int c, int r, int dir, float depth);
    void drawBumper(uint16_t* buf, int w, int h, int c, int r);
    void drawShards(uint16_t* buf, int w, int h);
    void drawMover(uint16_t* buf, int w, int h, int k, float depth);
    void drawBox(uint16_t* buf, int w, int h, float cx, float cz, float ang, float halfLen, float halfWid,
                 float y0, float y1, uint16_t top, uint16_t side, float depth);
    void renderFrame(GFXcanvas16 &canvas, bool withBall = true);
    uint16_t fog(uint16_t col, float depth) const;
    float gemY(int c, int r) const;

    // --- RollFluxHud.cpp ---
    void drawHUD(GFXcanvas16 &canvas);
    void drawCentred(GFXcanvas16 &canvas, const char* text, int y, uint16_t colour, uint8_t size = 1);
    void drawShadowed(GFXcanvas16 &canvas, const char* text, int x, int y, uint16_t colour, uint8_t size);
    void renderClear(GFXcanvas16 &canvas);
    void renderGameOver(GFXcanvas16 &canvas);
    void renderTitle(GFXcanvas16 &canvas);
    void renderHowTo(GFXcanvas16 &canvas, int page);
    void renderScores(GFXcanvas16 &canvas);
    void renderPicker(GFXcanvas16 &canvas);

    // Cell (c, r) is r rows from the north end. World x, z of its corners:
    float cellX0(int c) const { return (float)(c * CELL); }
    float cellZ0(int r) const { return (float)((_h - 1 - r) * CELL); }   // its south edge

    // --- Session ---
    GamePhase _phase = PHASE_ATTRACT;
    unsigned long _phaseAt = 0;
    AttractSlide _slide = SLIDE_TITLE;
    unsigned long _slideAt = 0;
    bool  _demo = false;              // the attract demo: silent, nothing kept
    bool  _test = false;              // a stage-select test run: nothing goes on the table
    int   _testFrom = 0;              // the course it started on (A at its game over)
    int   _pick = 0;                  // the picker's course
    int   _pickDir = 0;               // the stick direction last stepped, for auto-repeat
    unsigned long _pickRepeatAt = 0, _pickAt = 0;
    unsigned long _demoUntil = 0;
    bool  _silent = false;
    AudioEngine* _audio = nullptr;    // for sfx() from deep in play
    bool  _sfxOnCard[SFX_COUNT] = {};
    bool  _musicOnCard = false;
    hiscore::ScoreBoard _scores;
    unsigned long _lastFrameMs = 0;
    float _frameScale = 1.0f, _dt = 0.033f;

    // --- The course ---
    Cell _cells[MAX_COURSE_H][MAX_COURSE_W];
    int  _w = 0, _h = 0;
    int  _course = 0, _loop = 0;
    const CourseDef* _def = &COURSES[0];
    float _startX = 0, _startZ = 0;
    long  _timeMs = 0;                // left on the clock
    long  _courseMs = 0;              // this course's limit
    int   _courseGems = 0, _courseGemsTaken = 0, _courseFalls = 0;
    // Where a fall puts the ball back: the start, or the last checkpoint
    // reached, with the time and heading it had there.
    float _respawnX = 0, _respawnZ = 0, _respawnYaw = 0;
    long  _respawnTimeMs = 0;
    int   _checkC = -1, _checkR = -1;

    // --- The ball: position (y is its lowest point), velocity per second,
    // and its turn (local to world, row-major), rolled with it.
    float _bx = 0, _by = 0, _bz = 0, _vx = 0, _vz = 0, _vy = 0;
    float _rot[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    // Past the usual top speed, from a boost pad or a dash, fading at
    // _extraFade a second.
    float _extraSpeed = 0, _extraFade = 0;
    unsigned long _extraHoldUntil = 0;   // a dash's extra holds until then, then fades
    bool  _falling = false;
    // Its colour, 0 cyan or 1 magenta: B tapped swaps it (on the release,
    // the stick untouched: B held with the stick turns the camera).
    int   _polarity = 0;
    unsigned long _swapReadyAt = 0;
    bool  _bDown = false, _bStick = false;
    unsigned long _bDownAt = 0;
    // The course's moving parts, where they are now and were last frame.
    struct Mover { float x, z, y, ang, px, pz, py, pang, vx, vz; };
    Mover _movers[MAX_MOVERS];
    int   _moverCount = 0;
    int   _onMover = -1;              // the one the ball's riding, if any
    unsigned long _courseAt = 0;      // movers run from the course's start
    // A bumper lit by a knock, and shards of smashed crystal.
    int   _bumpC = -1, _bumpR = -1;
    unsigned long _bumpUntil = 0;
    struct Shard { float x, y, z, vx, vy, vz; uint16_t colour; };
    Shard _shards[SHARD_COUNT];
    unsigned long _shardsUntil = 0;
    long  _crystals = 0, _bumps = 0, _swaps = 0;
    bool  _fellOut = false;           // set by stepBall: below the course
    float _bump = 0;                  // the hardest knock this frame (wall or rail)
    int   _boostCell = -1;            // the boost pad it's on, for the sound
    unsigned long _holdUntil = 0;     // after a fall, the ball waits

    // --- The Flux Dash ---
    int   _dashGems = 0;              // the meter, in gems (DASH_GEMS_PER_STEP a step)
    bool  _charging = false;
    bool  _prevA = false;             // so a held A doesn't charge again
    unsigned long _chargeAt = 0;
    unsigned long _dashUntil = 0;     // the burst (the ball glows) lasts until then
    float _dashReturn = 0;            // the speed the brake after it settles to
    bool  _dashBrakeDue = false;
    bool  _braking = false;
    unsigned long _brakeAt = 0;       // when the brake began
    float _aimX = 0, _aimZ = 1;       // which way a dash would go, shown while charging
    long  _dashes = 0;

    // --- Score ---
    long  _score = 0;
    int   _lives = START_LIVES;
    long  _gemsTotal = 0;             // every gem this game, for extra lives
    long  _goals = 0, _falls = 0;
    long  _clearTime = 0, _clearNoFall = 0, _clearAllGems = 0, _clearGuardian = 0;   // the tally

    // --- The guardian, on a world's fifth course: its weak points (lit one
    // to hit with a dash), what's left of it, and its rhythm.
    int   _gHp = 0;
    int   _gCentreC = 0, _gCentreR = 0;   // its core, pivot or the ring's middle
    float _gNodeVX[3] = {}, _gNodeVZ[3] = {};
    bool  _gSlammed = false;          // this cycle's slam heard
    int   _gLit = -1;                 // the weak point open to a dash, -1 none
    int   _gNodes = 0;
    float _gNodeX[3] = {}, _gNodeY[3] = {}, _gNodeZ[3] = {};
    unsigned long _gHitUntil = 0;     // flashing (and shut) after a hit
    int   _gCycle = -1;               // the Piston's slam, counted
    float _gyreTiltX = 0, _gyreTiltZ = 0;
    struct Regrow { int8_t c, r; unsigned long at; };
    Regrow _regrow[12];               // its arena's gems, growing back
    int   _regrowCount = 0;
    long  _guardianHits = 0;

    unsigned long _bannerUntil = 0;
    const char*   _banner = "";
    uint16_t      _bannerColour = 0xFFFF;
    char          _bannerBuf[28] = "";
    unsigned long _tickAt = 0;        // the last-seconds tick

    // --- The camera, eased after the ball ---
    float _camX = 0, _camY = 0, _camZ = 0, _yaw = 0;
    float _leanRoll = 0, _leanPitch = 0;
    // Turned by hand (B and the stick): held until the ball sets off a new
    // way, the way it was rolling then in _camHoldDir.
    bool  _camHold = false, _camHoldMoving = false;
    float _camHoldDir = 0;
    float _orbit = 0;                 // the attract screens' view, round course 1
    float _m[9] = {};                 // Jet's camera matrix this frame
    Renderer::Camera _camera;
    bool  _ready = false;

    // --- The autopilot (RollFluxDemo.cpp): a planned way through the cells
    // to a gem or the goal, followed a little ahead.
    static constexpr int MAX_PATH = MAX_COURSE_W * MAX_COURSE_H;
    int16_t _dist[MAX_COURSE_W * MAX_COURSE_H];
    int16_t _from[MAX_COURSE_W * MAX_COURSE_H];
    uint16_t _planOpen[MAX_COURSE_W * MAX_COURSE_H];   // the planner's frontier
    bool     _planDone[MAX_COURSE_W * MAX_COURSE_H];
    uint16_t _path[MAX_PATH];
    int   _pathLen = 0, _pathPos = 0;
    int   _targetC = -1, _targetR = -1;
    unsigned long _pilotChargeUntil = 0;   // holding A for a dash until then
    unsigned long _pilotNextDash = 0;
    unsigned long _pilotRepickAt = 0;      // heading for the goal, it looks for gems again then
    bool  _pilotB = false;                 // tapping B for a colour swap
    // Crossing by a moving part: which, between which landing cells, and
    // how far through (wait, board, ride, get off).
    int   _pilotLink = -1, _pilotLinkFrom = -1, _pilotLinkTo = -1, _pilotLinkPhase = 0;
    bool  _pilotLinkFromA = true;
    InputState linkPilot();
    unsigned long _pilotGuardNext = 0;     // its next dash at a guardian, not before
    InputState stickFor(float wantVx, float wantVz) const;

    // --- Drawing ---
    int      _skyWorld = -1;
    uint16_t _sky[ArcadeConfig::LANDSCAPE_HEIGHT];
    float    _stars[STAR_COUNT][3];
    uint16_t _starCol[STAR_COUNT];
    // Floor pieces in view, far to near; slot says which item each must
    // follow (RollFluxScene.cpp's drawWorld).
    enum Piece : uint8_t { P_CELL, P_RAIL, P_GATE, P_MOVER };
    struct DrawEntry { int16_t z; uint8_t type, c, r, dir, slot; };
    DrawEntry _draw[MAX_DRAW], _drawSorted[MAX_DRAW];
    enum ItemType : uint8_t { I_BALL, I_SHADOW, I_GEM, I_BUMPER, I_SWEEPER, I_NODE };
    struct Item { float depth, bottom, x, z; uint8_t type, c, r; };
    Item _items[MAX_ITEMS];
    unsigned long _renderUs = 0;
    long  _ticks = 0;                 // rolling ticks played (the harness counts them)
    int   _seamC = -1, _seamR = -1;   // the cell the ball was in, for the ticks
    unsigned long _synthUntil = 0;    // a synth sound of ours is playing until then
};

}  // namespace rollflux

using RollFluxGame = rollflux::RollFluxGame;

#endif  // ROLL_FLUX_GAME_H
