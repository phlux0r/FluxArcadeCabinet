#ifndef ROLL_FLUX_GAME_H
#define ROLL_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
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
// ball), RollFluxScene.cpp (drawing), RollFluxHud.cpp (HUD and screens).
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
    enum GamePhase { PHASE_PLAYING, PHASE_CLEAR, PHASE_GAMEOVER };
    enum Kind : uint8_t { K_VOID, K_FLOOR, K_START, K_GOAL, K_RAMP_N, K_RAMP_S, K_RAMP_E, K_RAMP_W,
                          K_ICE, K_CHECK, K_BOOST_N, K_BOOST_S, K_BOOST_E, K_BOOST_W };
    enum CellFlag : uint8_t { F_RAIL = 1, F_GEM = 2, F_TAKEN = 4 };
    enum Dir : uint8_t { D_N, D_S, D_E, D_W };
    struct Cell { uint8_t kind = K_VOID, h = 0, flags = 0; };

    static bool isRamp(uint8_t k)  { return k >= K_RAMP_N && k <= K_RAMP_W; }
    static bool isBoost(uint8_t k) { return k >= K_BOOST_N && k <= K_BOOST_W; }

    // --- RollFluxGame.cpp ---
    void updateFrameScale();
    void startNewGame(AudioEngine &audio);
    void loadCourse(int index);
    void startCourse(AudioEngine &audio);
    void respawn();
    void loseLife(AudioEngine &audio, const char* why);
    void stepRules(AudioEngine &audio);
    void collectGems(AudioEngine &audio);
    void reachGoal(AudioEngine &audio);
    void updateLean(const InputState &in);
    void updateCamera(bool snap);
    bool updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateClear(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void banner(const char* text, uint16_t colour, unsigned long ms = 1500);
    // Gameplay sound goes through these, so the demo can be silent.
    void sfxTone(AudioEngine &audio, int hz, int ms) { if (!_silent) audio.playTone(hz, ms); }
    void sfxMelody(AudioEngine &audio, const int* n, const int* d, int len) {
        if (!_silent) audio.playMelody(n, d, len);
    }

    // --- RollFluxPhysics.cpp ---
    void stepBall(const InputState &in);
    void rollBall(float dx, float dz);
    bool floorAt(float x, float z, float &y) const;
    void stickToWorld(const InputState &in, float &ax, float &az) const;
    bool railed(int c, int r, int dir) const;
    int  colAt(float x) const { return x < 0 ? -1 : (int)(x / CELL); }
    int  rowAt(float z) const { return z < 0 ? _h : _h - 1 - (int)(z / CELL); }

    // --- RollFluxScene.cpp ---
    void ensureReady(GFXcanvas16 &canvas);
    void cornerHeights(int c, int r, float out[4]) const;   // sw, se, ne, nw
    float planeAt(int c, int r, float x, float z) const;     // the cell's top, extended
    bool solid(int c, int r) const { return c >= 0 && r >= 0 && c < _w && r < _h && _cells[r][c].kind != K_VOID; }
    void computeCameraMatrix();
    void toCam(float x, float y, float z, float* out) const;
    void drawSky(GFXcanvas16 &canvas);
    void drawStars(GFXcanvas16 &canvas);
    void drawWorld(GFXcanvas16 &canvas);
    void drawPoly(uint16_t* buf, int w, int h, const float (*p)[3], int n, uint16_t colour, bool shade = false);
    void drawCell(uint16_t* buf, int w, int h, int c, int r, float depth);
    void drawRail(uint16_t* buf, int w, int h, int c, int r, int dir, float depth);
    void drawBall(uint16_t* buf, int w, int h);
    void drawShadow(uint16_t* buf, int w, int h, float floorY);
    void drawGem(uint16_t* buf, int w, int h, int c, int r);
    void renderFrame(GFXcanvas16 &canvas);
    uint16_t fog(uint16_t col, float depth) const;
    float gemY(int c, int r) const;

    // --- RollFluxHud.cpp ---
    void drawHUD(GFXcanvas16 &canvas);
    void drawCentred(GFXcanvas16 &canvas, const char* text, int y, uint16_t colour, uint8_t size = 1);
    void renderClear(GFXcanvas16 &canvas);
    void renderGameOver(GFXcanvas16 &canvas);

    // Cell (c, r) is r rows from the north end. World x, z of its corners:
    float cellX0(int c) const { return (float)(c * CELL); }
    float cellZ0(int r) const { return (float)((_h - 1 - r) * CELL); }   // its south edge

    // --- Session ---
    GamePhase _phase = PHASE_PLAYING;
    unsigned long _phaseAt = 0;
    bool  _silent = false;
    unsigned long _lastFrameMs = 0;
    float _frameScale = 1.0f, _dt = 0.033f;

    // --- The course ---
    Cell _cells[MAX_COURSE_H][MAX_COURSE_W];
    int  _w = 0, _h = 0;
    int  _course = 0, _loop = 0;
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
    float _extraSpeed = 0;            // past the usual top speed, from a boost pad
    bool  _falling = false;
    bool  _fellOut = false;           // set by stepBall: below the course
    float _bump = 0;                  // the hardest knock this frame (wall or rail)
    int   _boostCell = -1;            // the boost pad it's on, for the sound
    unsigned long _holdUntil = 0;     // after a fall, the ball waits

    // --- Score ---
    long  _score = 0;
    int   _lives = START_LIVES;
    long  _gemsTotal = 0;             // every gem this game, for extra lives
    long  _goals = 0, _falls = 0;
    long  _clearTime = 0, _clearNoFall = 0, _clearAllGems = 0;   // the tally

    unsigned long _bannerUntil = 0;
    const char*   _banner = "";
    uint16_t      _bannerColour = 0xFFFF;
    unsigned long _tickAt = 0;        // the last-seconds tick

    // --- The camera, eased after the ball ---
    float _camX = 0, _camY = 0, _camZ = 0, _yaw = 0;
    float _leanRoll = 0, _leanPitch = 0;
    float _m[9] = {};                 // Jet's camera matrix this frame
    Renderer::Camera _camera;
    bool  _ready = false;

    // --- Drawing ---
    uint16_t _sky[ArcadeConfig::LANDSCAPE_HEIGHT];
    float    _stars[STAR_COUNT][3];
    uint16_t _starCol[STAR_COUNT];
    // Floor pieces in view, far to near; slot says which item each must
    // follow (RollFluxScene.cpp's drawWorld).
    enum Piece : uint8_t { P_CELL, P_RAIL };
    struct DrawEntry { int16_t z; uint8_t type, c, r, dir, slot; };
    DrawEntry _draw[MAX_DRAW], _drawSorted[MAX_DRAW];
    enum ItemType : uint8_t { I_BALL, I_SHADOW, I_GEM };
    struct Item { float depth, bottom, x, z; uint8_t type, c, r; };
    Item _items[MAX_ITEMS];
    unsigned long _renderUs = 0;
};

}  // namespace rollflux

using RollFluxGame = rollflux::RollFluxGame;

#endif  // ROLL_FLUX_GAME_H
