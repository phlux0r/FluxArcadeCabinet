#ifndef ROLL_FLUX_GAME_H
#define ROLL_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Jet.hpp>

#include "RollFluxConfig.h"
#include "RollCourses.h"

// =============================================================================
// ROLL FLUX: tilt the course to roll a ball to the goal (docs/design/
// RollFlux.md). This is stage 0, the renderer prototype: one course, the
// ball rolling on it (ramps, steps, falls), the chase camera, and the floor
// drawn one of two ways, to measure on the board which one holds the frame
// rate:
//   - JET: the course as Jet meshes, in chunks of 8x8 cells, those out of
//     reach disabled each frame; Jet clears to the sky gradient.
//   - DIRECT: the floor filled straight into the canvas, cell by cell far
//     to near, projected exactly as Jet projects (as Tube Flux's tunnel),
//     over a sky drawn first; Jet then draws only the ball.
// B swaps them, A cycles three camera heights; the top line shows which,
// the render time (averaged) and what Jet drew. Back quits, as ever.
//
// Files: RollFluxGame.cpp (update, ball, camera), RollFluxScene.cpp (the
// scene, both floor renderers, the sky, the overlay).
// =============================================================================

namespace rollflux {

class RollFluxGame : public IGame {
public:
    RollFluxGame() {}
    ~RollFluxGame() override { releaseScene(); }

    void init(AudioEngine &audio) override;
    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override;
    void onExit() override { releaseScene(); }

    uint8_t getRotation() const override { return 1; }
    const char* getName() const override { return "Roll Flux"; }

private:
    enum Kind : uint8_t { K_VOID, K_FLOOR, K_START, K_GOAL, K_RAMP_N, K_RAMP_S, K_RAMP_E, K_RAMP_W };
    struct Cell { uint8_t kind = K_VOID, h = 0; };

    // --- RollFluxGame.cpp ---
    void loadCourse(int index);
    void respawn();
    void updateFrameScale();
    void stepBall(const InputState &in);
    bool floorAt(float x, float z, float &y) const;
    void updateCamera(bool snap);
    void stickToWorld(const InputState &in, float &ax, float &az) const;

    // --- RollFluxScene.cpp ---
    void ensureSceneReady(GFXcanvas16 &canvas);
    void releaseScene();
    void buildChunks();
    void placeChunks();
    void placeBall();
    void cornerHeights(int c, int r, float out[4]) const;   // sw, se, ne, nw
    bool solid(int c, int r) const { return c >= 0 && r >= 0 && c < _w && r < _h && _cells[r][c].kind != K_VOID; }
    void computeCameraMatrix();
    void drawSky(GFXcanvas16 &canvas);
    void drawFloorDirect(GFXcanvas16 &canvas);
    void drawQuad(uint16_t* buf, int w, int h, const float (*p)[3], int n, uint16_t colour);
    void renderFrame(GFXcanvas16 &canvas);
    void drawOverlay(GFXcanvas16 &canvas);
    uint16_t cellColour(int c, int r, int side, float depth) const;

    // Cell (c, r) is r rows from the north end. World x, z of its corners:
    float cellX0(int c) const { return (float)(c * CELL); }
    float cellZ0(int r) const { return (float)((_h - 1 - r) * CELL); }   // its south edge

    // --- State ---
    Cell _cells[MAX_COURSE_H][MAX_COURSE_W];
    int  _w = 0, _h = 0;
    float _startX = 0, _startZ = 0;

    // The ball: position (y is its lowest point), velocity per second.
    float _bx = 0, _by = 0, _bz = 0, _vx = 0, _vz = 0, _vy = 0;
    bool  _falling = false;
    long  _goals = 0, _falls = 0;

    // The camera, eased after the ball.
    float _camX = 0, _camY = 0, _camZ = 0, _yaw = 0;
    int   _preset = 1;
    float _m[9] = {};                 // Jet's camera matrix this frame

    bool  _direct = true;             // the floor drawn directly (else by Jet)
    bool  _prevA = false, _prevB = false;

    unsigned long _lastFrameMs = 0;
    float _frameScale = 1.0f, _dt = 0.033f;

    // Render time, averaged over a second, for the overlay.
    unsigned long _renderUs = 0, _renderUsSum = 0, _renderAvgUs = 0;
    int   _renderFrames = 0;
    unsigned long _renderAvgAt = 0;
    int   _jetTris = 0;

    Renderer::Scene*   _scene = nullptr;
    Renderer::Camera   _camera;
    Renderer::Object*  _chunks[(MAX_COURSE_W / CHUNK_CELLS + 1) * (MAX_COURSE_H / CHUNK_CELLS + 1)] = {};
    float _chunkCX[(MAX_COURSE_W / CHUNK_CELLS + 1) * (MAX_COURSE_H / CHUNK_CELLS + 1)] = {};
    float _chunkCZ[(MAX_COURSE_W / CHUNK_CELLS + 1) * (MAX_COURSE_H / CHUNK_CELLS + 1)] = {};
    int   _chunkCount = 0;
    Renderer::Object*  _ballObj = nullptr;
    Renderer::Material _floorMat[2][4], _rampMat[2], _goalMat[2], _sideMat[2], _ballMat;
    Renderer::DirectionalLight _sun{ Vector3{ 40, 70, -30 }, Renderer::Color{ 255, 245, 225 }, 255 };
    Renderer::AmbientLight     _amb{ Renderer::Color{ 120, 120, 140 } };
    uint16_t _sky[ArcadeConfig::LANDSCAPE_HEIGHT];

    // The direct renderer's draw list: the cells in view, far to near.
    struct DrawCell { int16_t z; uint8_t c, r; };
    DrawCell _drawList[MAX_COURSE_W * MAX_COURSE_H];
};

}  // namespace rollflux

using RollFluxGame = rollflux::RollFluxGame;

#endif  // ROLL_FLUX_GAME_H
