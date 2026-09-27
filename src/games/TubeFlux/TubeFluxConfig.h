#ifndef TUBE_FLUX_CONFIG_H
#define TUBE_FLUX_CONFIG_H

#include <stdint.h>

// All of Tube Flux's tuning in one place. World units match Jet's (the
// same scale Tank Flux uses); "per frame" means per REFERENCE_FRAME_MS, and
// is scaled by the real frame time at runtime.
//
// Coordinates: the tunnel runs along +Z. Angles go round it in degrees,
// 0 at the bottom, increasing towards +X (so rolling right increases the
// angle). A point at angle a on radius r is (r sin a, -r cos a).

namespace tubeflux {

// --- Frame pacing (as Tank Flux) ----------------------------------------------
inline constexpr unsigned long REFERENCE_FRAME_MS = 33;   // ~30fps
inline constexpr unsigned long MIN_FRAME_MS = 8;
inline constexpr unsigned long MAX_FRAME_MS = 100;

// --- Tunnel -------------------------------------------------------------------
// An octagon: each wall panel is one lane.
inline constexpr int     TUBE_SIDES   = 8;
inline constexpr float   LANE_DEG     = 360.0f / TUBE_SIDES;
inline constexpr int32_t TUBE_RADIUS  = 400;    // axis to octagon corner
inline constexpr int32_t RING_SPACING = 500;
// Rings drawn ahead of the camera. The tunnel is drawn directly into the
// canvas (see drawTunnel()), not by Jet, so this is just how far it goes:
// 10 rings is 5000 units, where TUNNEL_FOG_FAR has faded it to the backdrop.
inline constexpr int     RING_COUNT   = 10;
// The walls darken towards the backdrop colour between these depths.
inline constexpr float   TUNNEL_FOG_NEAR = 1200.0f;
inline constexpr float   TUNNEL_FOG_FAR  = 5000.0f;

// --- Camera ---------------------------------------------------------------
// The camera sits between the axis and the ship's panel and rolls with the
// ship, so the ship's lane is always at the bottom of the screen and the
// tunnel turns around it.
inline constexpr int     CAMERA_FOV    = 80;
inline constexpr int32_t CAMERA_NEAR   = 48;
inline constexpr int32_t CAMERA_FAR    = 5200;
inline constexpr float   CAMERA_OFFSET = 170.0f;  // from the axis, towards the ship
// Jet's roll and object Z-rotation directions, relative to this file's
// angle convention. Checked against rendered frames in the host harness.
inline constexpr float   CAMERA_ROLL_SIGN   = 1.0f;
inline constexpr int     OBSTACLE_ROLL_SIGN = 1;

// The ship sprite's screen position, and the depth ahead of the camera
// where the floor under it is: obstacles hit the ship as they pass this.
inline constexpr int     SHIP_SCREEN_Y = 102;
inline constexpr float   SHIP_Z        = 560.0f;
inline constexpr float   SHIP_DEPTH    = 90.0f;    // front-to-back, for collisions
inline constexpr float   SHIP_HALF_DEG = 12.0f;    // angular half-width (the sprite is ~half a lane wide)

// --- Steering -----------------------------------------------------------------
inline constexpr float ROLL_RATE      = 7.0f;    // deg per frame at full stick
inline constexpr float ROLL_SMOOTH    = 0.35f;   // ease towards the stick
inline constexpr float BANK_THRESHOLD = 2.0f;    // deg per frame before the bank frame shows
inline constexpr float STEER_SIGN     = 1.0f;    // joyY (screen horizontal in landscape)
inline constexpr float THROTTLE_SIGN  = -1.0f;   // joyX: pushing up is negative

// --- Speed and progression -------------------------------------------------------
// Speed is units per frame. The floor rises with each tier; the stick only
// nudges it, so the game keeps getting faster whatever you do.
inline constexpr float BASE_SPEED     = 55.0f;
inline constexpr float SPEED_PER_TIER = 9.0f;
inline constexpr float MAX_SPEED      = 150.0f;
inline constexpr float BOOST_MULT     = 1.35f;   // stick fully up
inline constexpr float BRAKE_MULT     = 0.75f;   // stick fully down
inline constexpr float THROTTLE_SMOOTH = 0.08f;
// A tier gate every ~30s at the speeds a tier runs at.
inline constexpr float TIER_DISTANCE  = 55000.0f;
inline constexpr int   MAX_TIER       = 9;

// --- Obstacles ----------------------------------------------------------------
inline constexpr int     OBSTACLE_POOL  = 10;
inline constexpr int     MAX_BLOCK_LANES = 3;
inline constexpr int32_t BLOCK_HEIGHT   = 150;   // how far a block stands off the wall
inline constexpr int32_t BLOCK_DEPTH    = 140;
inline constexpr float   SPAWN_AHEAD    = 5300.0f;  // just past the fog, so blocks fade in
// Time between blocks, in frames, at tier 1 and how much each tier takes
// off it. It's set in time rather than distance because speed rises with
// the tier too: a fixed distance would compound the two, and the gap fell
// from ~0.9s to ~0.15s by tier 9, faster than anyone can read blocks.
// This way tier 9 is ~11 frames (~0.35s) between blocks at nearly 2.5x
// tier 1's speed, so you also see each one for well under half as long.
inline constexpr float   GAP_FRAMES_TIER1    = 27.0f;
inline constexpr float   GAP_FRAMES_PER_TIER = 2.0f;
inline constexpr float   GAP_FRAMES_MIN      = 11.0f;
inline constexpr float   GAP_JITTER     = 0.35f;   // +/- fraction
// Fairness: one lane is always left open. It drifts by at most one lane
// per SAFE_LANE_SHIFT of distance, so at any speed there's a route you can
// actually steer along, however dense the blocks get. While it moves, both
// the old and the new lane stay open for SAFE_LANE_TRANSITION: enough
// distance to roll one lane at the top tier's speed with full boost
// (~10 frames at ~170 units a frame). Without that, the first block after
// a move could land on the lane you were in, a few frames later.
inline constexpr float   SAFE_LANE_SHIFT      = 1800.0f;
inline constexpr float   SAFE_LANE_TRANSITION = 1800.0f;
// Passing a block within this many degrees of its edge, unhurt, is a near miss.
inline constexpr float   NEAR_MISS_DEG  = 16.0f;

// --- Shield and scoring ---------------------------------------------------------
inline constexpr int           SHIELD_MAX       = 3;
inline constexpr unsigned long HIT_INVULN_MS    = 1400;
inline constexpr unsigned long HIT_FLASH_MS     = 180;
inline constexpr float         SCORE_PER_UNIT   = 0.01f;  // 100 points per 10000 units
inline constexpr int           NEAR_MISS_POINTS = 50;
inline constexpr int           TIER_POINTS      = 500;
inline constexpr unsigned long NEAR_MISS_SHOW_MS = 600;
inline constexpr unsigned long TIER_BANNER_MS    = 1600;

// --- Menus ----------------------------------------------------------------------
// The attract screen alternates the title image with a how-to-play slide.
inline constexpr unsigned long ATTRACT_SLIDE_MS    = 8000;
inline constexpr unsigned long EXIT_HOLD_MS        = 2000;   // hold B
inline constexpr unsigned long QUIT_HINT_DELAY_MS  = 650;
inline constexpr unsigned long GAMEOVER_TIMEOUT_MS = 15000;
inline constexpr unsigned long GAMEOVER_INPUT_DELAY_MS = 800;  // so a held stick/button can't skip it

}  // namespace tubeflux

#endif  // TUBE_FLUX_CONFIG_H
