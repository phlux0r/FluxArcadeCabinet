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

// --- Bends --------------------------------------------------------------------
// The tunnel ahead curves: each ring (and each block) is shifted sideways by
// bend * (z / BEND_REF_Z)^2, so the offset grows with distance and shrinks
// to nothing as a ring reaches you. Steering and collisions near the ship
// are unaffected (at SHIP_Z the shift is ~1% of the bend); what you lose is
// sight of what's coming. "bend" is the shift at BEND_REF_Z, in world units.
//
// Past ~4 x TUBE_RADIUS the far end would swing behind the near walls, and
// blocks there would show through them (Jet draws blocks after the tunnel),
// so BEND_SHARP stays under that.
inline constexpr float BEND_REF_Z      = 5000.0f;
inline constexpr int   BEND_START_TIER = 4;       // gentle curves from here
inline constexpr int   BEND_SHARP_TIER = 6;       // sharper ones from here
inline constexpr float BEND_GENTLE     = 700.0f;
inline constexpr float BEND_SHARP      = 1400.0f;
// A new curve every BEND_SEGMENT of distance (~5s at tier 4), easing in
// and out; BEND_STRAIGHT_PCT of them are straight, so curves come and go.
inline constexpr float BEND_SEGMENT      = 13000.0f;
inline constexpr int   BEND_STRAIGHT_PCT = 30;
inline constexpr float BEND_EASE         = 0.025f;   // per frame, towards the target

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

// --- Weapon and crystals --------------------------------------------------------
// You start unarmed. The gun is a pickup (a pulsing yellow chevron) that
// first appears late in tier 3, just before crystals start, and lies in the
// always-open safe lane, so it's never walled off. Miss it and it comes
// round again. Once collected it's yours for the rest of the run.
inline constexpr float   WEAPON_FIRST_AT   = 2.55f * TIER_DISTANCE;   // late tier 3
inline constexpr float   WEAPON_RETRY      = 15000.0f;                // after a miss
inline constexpr float   PICKUP_DEPTH      = 120.0f;
inline constexpr float   PICKUP_HALF_DEG   = 26.0f;    // generous: it's a reward, not a test
inline constexpr unsigned long PICKUP_BANNER_MS = 2500;

// Gun upgrades, one pickup each (the gun's chevron, in magenta): level 2 is
// twin guns, two bolts side by side covering the whole lane; level 3 is
// rapid fire, which also fires while A is held. Each appears once its tier
// is reached, in the safe lane, and comes round again if missed.
inline constexpr int           GUN_MAX_LEVEL   = 3;      // 1 gun, 2 twin, 3 rapid
inline constexpr int           TWIN_TIER       = 7;
inline constexpr int           RAPID_TIER      = 9;
inline constexpr float         TWIN_SPREAD_DEG = 12.0f;  // each bolt this far off the lane centre
inline constexpr unsigned long RAPID_RELOAD_MS = 120;
inline constexpr float         UPGRADE_RETRY   = 15000.0f;

// Shield pickup: a green cross worth one shield, offered from tier 2 and only
// while you're missing one, at most every SHIELD_PICKUP_EVERY: ~40s at tier
// 5, ~30s at tier 9. Much more often and a weak player never dies (the
// harness's short-sighted bot reached tier 9 at 40000).
inline constexpr int   SHIELD_PICKUP_TIER  = 2;
inline constexpr float SHIELD_PICKUP_EVERY = 110000.0f;

// Crystals: orange, spiky, one lane wide. They hurt like blocks but a shot
// destroys them. From CRYSTAL_TIER some spawns are crystals instead of
// blocks. Unarmed, they obey the safe lane like everything else; armed,
// CRYSTAL_ON_SAFE_PCT of them are put in the safe lane itself, so some
// routes have to be shot open.
inline constexpr int     CRYSTAL_TIER        = 4;
inline constexpr int     CRYSTAL_PCT         = 30;     // of spawns at CRYSTAL_TIER
inline constexpr int     CRYSTAL_PCT_PER_TIER = 4;
inline constexpr int     CRYSTAL_PCT_MAX     = 50;
inline constexpr int     CRYSTAL_ON_SAFE_PCT = 50;
inline constexpr int     CRYSTAL_POOL        = 6;
inline constexpr int32_t CRYSTAL_HEIGHT      = 210;    // how far it stands off the wall
inline constexpr int32_t CRYSTAL_WIDTH       = 170;
inline constexpr int     CRYSTAL_POINTS      = 150;

// Shots fly straight down the lane you fired from.
inline constexpr int           SHOT_POOL     = 6;   // twin guns fire two at a time
inline constexpr float         SHOT_SPEED    = 420.0f;   // units per frame, faster than any ship speed
inline constexpr float         SHOT_RANGE    = 4800.0f;  // ahead of the camera
inline constexpr float         SHOT_HALF_DEG = 9.0f;
inline constexpr unsigned long SHOT_RELOAD_MS = 220;
// Where shots and the pickup sit: off the floor at the ship's height.
inline constexpr float         FLY_HEIGHT    = 70.0f;

// --- Drone chase ------------------------------------------------------------------
// A two-part fight. First the drone is behind you, where you can't see it:
// before each shot the lane it's aiming down flashes red on the walls,
// then a bolt streaks past down that lane. No blocks spawn while it hunts
// you, so the bolts are the whole test. Then it overtakes, overhead, and
// holds station ahead, weaving between lanes and dropping crystals: now
// it's in front of your gun. Destroy it for a big bonus, or it escapes
// after DRONE_ESCAPE_MS, no penalty. Each chase is harder than the last.
//
// The first comes at the tier-6 gate (the 5th gate, when the gun is long
// since available); tiers stop at 9, so later ones are every CHASE_EVERY
// of distance after the last one ended.
inline constexpr int           CHASE_FIRST_TIER   = 6;
// Every 5th gate's worth of distance: ~70s of normal play at tier 9 between
// chases (at 2.5 tiers, chases were ~40% of top-tier play).
inline constexpr float         CHASE_EVERY        = 5.0f * TIER_DISTANCE;
inline constexpr unsigned long CHASE_PURSUE_MS    = 10000;  // drone behind you, firing
inline constexpr unsigned long CHASE_FIRST_SHOT_MS = 2500;  // lets the blocks already queued pass
inline constexpr unsigned long DRONE_SHOT_MS      = 1500;   // between volleys, first chase
inline constexpr unsigned long DRONE_SHOT_MS_STEP = 250;    // faster each chase...
inline constexpr unsigned long DRONE_SHOT_MS_MIN  = 800;    // ...down to this
inline constexpr unsigned long DRONE_WARN_MS      = 700;    // lane flashes red this long before a bolt
inline constexpr unsigned long DRONE_WARN_MS_MIN  = 500;
inline constexpr int           DRONE_TWIN_VOLLEY_CHASE = 2; // from the 3rd chase, two lanes at once
inline constexpr float         DRONE_BOLT_SPEED   = 170.0f; // relative to you, units per frame
inline constexpr float         DRONE_BOLT_HALF_DEG = 10.0f;
inline constexpr float         DRONE_BOLT_DEPTH   = 300.0f;
inline constexpr float         DRONE_WARN_DEPTH   = 3200.0f; // how far along the lane the warning shows
inline constexpr unsigned long CHASE_OVERTAKE_MS  = 1500;
inline constexpr float         DRONE_AHEAD_Z      = 1500.0f; // where it holds station
inline constexpr float         DRONE_HEIGHT       = 150.0f;  // off the wall it flies over
inline constexpr float         DRONE_HALF_DEG     = 22.0f;   // for shots
inline constexpr float         DRONE_DEPTH        = 280.0f;
inline constexpr float         DRONE_WEAVE_RATE   = 2.5f;    // degrees per frame
inline constexpr unsigned long DRONE_RETARGET_MS  = 1400;    // picks a new lane to weave to
inline constexpr unsigned long DRONE_DROP_MS      = 1100;    // drops a crystal
inline constexpr unsigned long DRONE_ESCAPE_MS    = 14000;   // time you get to shoot it down
inline constexpr int           DRONE_HP           = 8;
inline constexpr int           DRONE_HP_PER_CHASE = 3;
inline constexpr int           DRONE_POINTS       = 2000;
inline constexpr int           DRONE_POINTS_PER_CHASE = 1000;
inline constexpr unsigned long DRONE_HIT_FLASH_MS = 80;
inline constexpr unsigned long CHASE_BANNER_MS    = 2500;
inline constexpr int           DRONE_BOLT_POOL    = 4;

// --- Shield and scoring ---------------------------------------------------------
inline constexpr int           SHIELD_MAX       = 3;
inline constexpr unsigned long HIT_INVULN_MS    = 1400;
inline constexpr unsigned long HIT_FLASH_MS     = 180;
inline constexpr float         SCORE_PER_UNIT   = 0.01f;  // 100 points per 10000 units
inline constexpr int           NEAR_MISS_POINTS = 50;
inline constexpr int           TIER_POINTS      = 500;
inline constexpr unsigned long NEAR_MISS_SHOW_MS = 600;
inline constexpr unsigned long TIER_BANNER_MS    = 1600;

// --- Attract demo -----------------------------------------------------------------
// After the title and how-to-play, the autopilot plays a real run, silently,
// from a random tier, with the gear a player would have by then, for a
// random 30-45s (or until it loses its last shield). A starts a real game.
inline constexpr unsigned long DEMO_MIN_MS    = 30000;
inline constexpr unsigned long DEMO_MAX_MS    = 45000;
inline constexpr int           DEMO_MIN_TIER  = 2;
inline constexpr int           DEMO_MAX_TIER  = 8;
inline constexpr int           DEMO_CHASE_PCT = 40;      // from tier 6: start near a drone chase
// Good but not perfect, so it plays like a person: it only looks this far
// ahead, and only rethinks its lane every DEMO_REPLAN_MS.
inline constexpr float         DEMO_LOOKAHEAD = 1800.0f;
inline constexpr unsigned long DEMO_REPLAN_MS = 140;

// --- Menus ----------------------------------------------------------------------
// The attract screen alternates the title image with a how-to-play slide.
inline constexpr unsigned long ATTRACT_SLIDE_MS    = 8000;
inline constexpr unsigned long EXIT_HOLD_MS        = 2000;   // hold B
inline constexpr unsigned long QUIT_HINT_DELAY_MS  = 650;
inline constexpr unsigned long GAMEOVER_TIMEOUT_MS = 15000;
inline constexpr unsigned long GAMEOVER_INPUT_DELAY_MS = 800;  // so a held stick/button can't skip it

}  // namespace tubeflux

#endif  // TUBE_FLUX_CONFIG_H
