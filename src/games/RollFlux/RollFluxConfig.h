#ifndef ROLL_FLUX_CONFIG_H
#define ROLL_FLUX_CONFIG_H

#include <stdint.h>

// =============================================================================
// ROLL FLUX — tuning. docs/design/RollFlux.md is the design.
//
// World: x east, y up, z north, in Jet's integer units. A course cell is
// CELL units square; heights go in HEIGHT_STEP steps. Everything is drawn
// straight into the canvas (RollFluxScene.cpp) with Jet's camera maths,
// which stage 0 showed holds ~55fps on the board.
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
inline constexpr float   RAIL_HEIGHT = 40.0f;

// --- Ball -------------------------------------------------------------------
// Per second; scaled by the real frame time.
inline constexpr float BALL_RADIUS   = 60.0f;
inline constexpr float BALL_ACCEL    = 1100.0f;   // full stick
inline constexpr float BALL_MAX_SPEED = 1300.0f;
inline constexpr float BALL_FRICTION = 1.1f;      // fraction of speed lost a second, about
inline constexpr float ICE_FRICTION  = 0.08f;     // ice: this much of the friction
inline constexpr float ICE_GRIP      = 0.28f;     // and this much of the stick's push
inline constexpr float SLOPE_GRAVITY = 900.0f;    // along a ramp's fall line
inline constexpr float GRAVITY       = 2600.0f;   // falling
inline constexpr float STEP_UP       = 30.0f;     // a rise bigger than this is a wall
inline constexpr float STEP_DOWN     = 24.0f;     // a drop bigger than this is a fall
inline constexpr float LAND_LIP      = 40.0f;     // in the air, it catches a floor edge up to this far above it
inline constexpr float WALL_PROBE    = 45.0f;     // walls and rails stop the ball this far from its centre
inline constexpr float FALL_DEPTH    = 900.0f;    // below the course: a fall
inline constexpr float WALL_BOUNCE   = 0.35f;
inline constexpr float RAIL_BOUNCE   = 0.5f;
inline constexpr float SUBSTEP       = 20.0f;     // the ball moves at most this far between checks

// Boost pads push along their arrow, and let the ball past its usual top
// speed for a moment (the extra cap fades at EXTRA_SPEED_DECAY a second).
inline constexpr float BOOST_ACCEL   = 3200.0f;
inline constexpr float BOOST_SPEED   = 1900.0f;
inline constexpr float EXTRA_SPEED_DECAY = 700.0f;

// The Flux Dash: gems fill a meter of DASH_STEPS steps, DASH_GEMS_PER_STEP
// gems a step. Hold A (with a step) to charge, the ball held to a fraction
// of its top speed; let go to dash, a step used: the ball's speed jumps to
// between DASH_SPEED_MIN and DASH_SPEED_MAX times its usual top speed (by
// how long it charged) and fades back over DASH_FADE_MS. At full speed it
// clears a two-cell gap, not a three.
inline constexpr int   DASH_GEMS_PER_STEP = 5;
inline constexpr int   DASH_STEPS         = 3;
inline constexpr unsigned long DASH_CHARGE_MS     = 400;
inline constexpr unsigned long DASH_MIN_CHARGE_MS = 100;   // let go sooner: no dash, no step used
inline constexpr float DASH_CHARGE_HOLD   = 0.6f;
inline constexpr float DASH_SPEED_MIN     = 1.5f;
inline constexpr float DASH_SPEED_MAX     = 2.2f;
inline constexpr unsigned long DASH_FADE_MS = 600;

// --- Rules -------------------------------------------------------------------
inline constexpr int   START_LIVES    = 4;
inline constexpr int   MAX_LIVES      = 9;
inline constexpr int   GEMS_PER_LIFE  = 100;
inline constexpr long  GEM_POINTS     = 100;
inline constexpr unsigned long GEM_TIME_MS = 2000;
inline constexpr long  TIME_POINTS    = 100;      // a second left at the goal
inline constexpr long  NO_FALL_BONUS  = 2000;
inline constexpr long  ALL_GEMS_BONUS = 5000;
inline constexpr unsigned long TIME_WARN_MS  = 10000;   // the clock goes red, and ticks
inline constexpr unsigned long CLEAR_MS      = 3500;    // the course-clear tally
inline constexpr unsigned long RESPAWN_HOLD_MS = 700;   // the ball waits after a fall
inline constexpr unsigned long GAMEOVER_TIMEOUT_MS = 20000;

// --- Attract cycle -------------------------------------------------------------
// Title, two how-to-play slides and the high scores over a slow orbit of
// course 1, then a silent demo on a course picked at random.
inline constexpr unsigned long ATTRACT_SLIDE_MS = 6000;
inline constexpr unsigned long DEMO_MS          = 35000;
inline constexpr float ORBIT_RADIUS = 1500.0f, ORBIT_HEIGHT = 1300.0f;
inline constexpr float ORBIT_SPEED  = 0.12f;      // radians a second

// --- Sound -------------------------------------------------------------------
inline constexpr const char* MUSIC = "/audio/flux-roll.wav";

// --- Camera -------------------------------------------------------------------
// Behind and above the ball (stage 0's middle height played best).
inline constexpr float   CAMERA_BACK = 480.0f;
inline constexpr float   CAMERA_UP   = 420.0f;
inline constexpr int     CAMERA_FOV  = 70;
inline constexpr int32_t CAMERA_NEAR = 48;
inline constexpr int32_t CAMERA_FAR  = 4000;
inline constexpr float   CAMERA_YAW_EASE = 1.7f;  // per second, towards the direction of travel
inline constexpr float   CAMERA_POS_EASE = 9.0f;  // per second, after the ball
inline constexpr float   CAMERA_LOOK_AHEAD = 120.0f;
inline constexpr float   YAW_FOLLOW_SPEED = 120.0f;   // slower than this, the camera holds its heading
inline constexpr float   YAW_FOLLOW_MAX = 1.75f;      // radians: rolling further round than this (back
                                                      // towards the camera), it holds its heading too
// The course leans with the stick, so the tilt shows (radians at full stick).
inline constexpr float   LEAN_ROLL  = 0.10f;
inline constexpr float   LEAN_PITCH = 0.06f;
inline constexpr float   LEAN_EASE  = 6.0f;       // per second

// --- Drawing -------------------------------------------------------------------
inline constexpr float VIEW_DIST = 3000.0f;       // cells further than this aren't drawn
inline constexpr float FOG_NEAR  = 1600.0f;       // the floor fades to the sky between these
inline constexpr float FOG_FAR   = 3000.0f;
inline constexpr int   MAX_DRAW  = 1200;          // floor pieces in view (cells and rails)
inline constexpr int   MAX_ITEMS = 32;            // ball, shadow, gems in view
inline constexpr int   MAX_FLOOR_LEVEL = 4;       // heights above this share its colour
inline constexpr int   STAR_COUNT = 48;

}  // namespace rollflux

#endif  // ROLL_FLUX_CONFIG_H
