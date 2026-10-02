#ifndef ROLL_FLUX_CONFIG_H
#define ROLL_FLUX_CONFIG_H

#include <stdint.h>

// =============================================================================
// ROLL FLUX — tuning. docs/design/RollFlux.md is the design.
//
// This is stage 0: a renderer prototype. One course, a ball that rolls on
// it, the chase camera, and two ways of drawing the floor to compare on the
// board: Jet drawing the course meshes, or the floor filled directly into
// the canvas with Jet's own projection (as Tube Flux draws its tunnel),
// Jet then drawing only the ball.
//
// World: x east, y up, z north, in Jet's integer units. A course cell is
// CELL units square; heights go in HEIGHT_STEP steps.
// =============================================================================

namespace rollflux {

// --- Frame pacing (as the other 3D games) ---------------------------------------
inline constexpr unsigned long REFERENCE_FRAME_MS = 33;   // ~30fps
inline constexpr unsigned long MIN_FRAME_MS = 8;
inline constexpr unsigned long MAX_FRAME_MS = 100;

// --- Course -------------------------------------------------------------------
inline constexpr int32_t CELL        = 200;
inline constexpr int32_t HEIGHT_STEP = 100;
inline constexpr int     MAX_COURSE_W = 24, MAX_COURSE_H = 48;
inline constexpr int32_t SIDE_DEPTH  = 160;    // how far a course edge's side face reaches down
inline constexpr int     CHUNK_CELLS = 8;      // Jet floor: one mesh per 8x8 cells

// --- Ball -------------------------------------------------------------------
// Per second; scaled by the real frame time.
inline constexpr float BALL_RADIUS   = 60.0f;
inline constexpr float BALL_ACCEL    = 1100.0f;   // full stick
inline constexpr float BALL_MAX_SPEED = 1300.0f;
inline constexpr float BALL_FRICTION = 1.1f;      // fraction of speed lost a second, about
inline constexpr float SLOPE_GRAVITY = 900.0f;    // along a ramp's fall line
inline constexpr float GRAVITY       = 2600.0f;   // falling
inline constexpr float STEP_UP       = 30.0f;     // a rise bigger than this is a wall
inline constexpr float STEP_DOWN     = 24.0f;     // a drop bigger than this is a fall
inline constexpr float FALL_DEPTH    = 900.0f;    // below the course: a fall
inline constexpr float WALL_BOUNCE   = 0.35f;
inline constexpr float SUBSTEP       = 20.0f;     // the ball moves at most this far between checks

// --- Camera -------------------------------------------------------------------
// Three heights to compare in stage 0 (A cycles them): lower shows more of
// the course ahead and more floor; higher, less floor and more of what's
// round the ball.
struct CameraPreset { const char* name; float back, up; };
inline constexpr CameraPreset CAMERA_PRESETS[3] = {
    { "LOW",  560.0f, 300.0f },
    { "MID",  480.0f, 420.0f },
    { "HIGH", 360.0f, 560.0f },
};
inline constexpr int     CAMERA_FOV  = 70;
inline constexpr int32_t CAMERA_NEAR = 48;
inline constexpr int32_t CAMERA_FAR  = 4000;
inline constexpr float   CAMERA_YAW_EASE = 1.7f;  // per second, towards the direction of travel
inline constexpr float   CAMERA_POS_EASE = 9.0f;  // per second, after the ball
inline constexpr float   CAMERA_LOOK_AHEAD = 120.0f;
inline constexpr float   YAW_FOLLOW_SPEED = 120.0f;   // slower than this, the camera holds its heading

// --- Drawing -------------------------------------------------------------------
inline constexpr float VIEW_DIST = 3000.0f;       // cells further than this aren't drawn
inline constexpr float FOG_NEAR  = 1600.0f;       // the floor fades to the sky between these
inline constexpr float FOG_FAR   = 3000.0f;

}  // namespace rollflux

#endif  // ROLL_FLUX_CONFIG_H
