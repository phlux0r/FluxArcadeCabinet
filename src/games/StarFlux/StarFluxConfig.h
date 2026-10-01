#ifndef STAR_FLUX_CONFIG_H
#define STAR_FLUX_CONFIG_H

#include <stdint.h>

// All of Star Flux's tuning in one place. World units match Jet's (the
// same scale Tank and Tube Flux use); "per frame" means per
// REFERENCE_FRAME_MS, and is scaled by the real frame time at runtime.
//
// Coordinates: +X right, +Y up, +Z ahead. The world is a treadmill, as in
// Tube Flux: the camera stays at z = 0 and z is always depth ahead of it.
// Rocks and rings drift towards the camera at FLY_SPEED; fighters fly
// their own paths in that same camera-relative space.

namespace starflux {

// --- Frame pacing (as Tank and Tube Flux) --------------------------------------
inline constexpr unsigned long REFERENCE_FRAME_MS = 33;   // ~30fps
inline constexpr unsigned long MIN_FRAME_MS = 8;
inline constexpr unsigned long MAX_FRAME_MS = 100;

// --- Camera -------------------------------------------------------------------
// Behind and above the ship, following it part of the way round its box,
// so moving the ship both moves it on screen and pans the world a little.
inline constexpr int     CAMERA_FOV  = 70;
inline constexpr int32_t CAMERA_NEAR = 40;
inline constexpr int32_t CAMERA_FAR  = 9000;
inline constexpr float   CAM_FOLLOW  = 0.35f;   // share of the ship's offset the camera takes
inline constexpr float   CAM_RISE    = 120.0f;  // camera height above the ship's line
// The world banks with the ship, a little: degrees of camera roll at full bank.
inline constexpr float   CAM_BANK_DEG = 9.0f;
// Jet's rotation directions relative to this file's conventions. Checked
// against rendered frames in the host harness (test/starflux_harness.cpp pose).
inline constexpr float   CAMERA_ROLL_SIGN = -1.0f;
inline constexpr float   YAW_SIGN   = 1.0f;
inline constexpr float   PITCH_SIGN = -1.0f;
inline constexpr float   ROLL_SIGN  = 1.0f;
// Whether Jet's front faces wind the other way from outward-by-right-hand-rule.
inline constexpr bool    ROCK_WINDING_FLIP = false;

// --- Ship ---------------------------------------------------------------------
// It flies at a fixed depth ahead of the camera, moving round a box.
inline constexpr float SHIP_Z      = 600.0f;
inline constexpr float BOX_X       = 340.0f;    // +-
inline constexpr float BOX_Y_MIN   = -190.0f;
inline constexpr float BOX_Y_MAX   = 260.0f;
inline constexpr float SHIP_SPEED  = 16.0f;     // units per frame at full stick
inline constexpr float SHIP_SMOOTH = 0.30f;     // ease towards the stick
inline constexpr float SHIP_HIT_R  = 62.0f;     // for shots and rams (xy, at SHIP_Z)
inline constexpr float BANK_FRAME  = 0.35f;     // |bank| for the bank frame
inline constexpr float BANK_HARD   = 0.80f;     // and the hard-bank frame
// Joystick signs (landscape): joyY is screen horizontal, joyX vertical with up negative.
inline constexpr float STEER_X_SIGN = 1.0f;
inline constexpr float STEER_Y_SIGN = -1.0f;
inline constexpr float FLY_SPEED   = 45.0f;     // the world's speed past you, per frame

// --- Shield and lives ---------------------------------------------------------------
inline constexpr int SHIELD_MAX        = 100;
// Rings overcharge past full, up to this: shown orange, then red, on the bar.
inline constexpr int SHIELD_CAP        = 3 * SHIELD_MAX;
inline constexpr int SHOT_DAMAGE       = 14;
inline constexpr int ROCK_DAMAGE       = 22;
inline constexpr int RAM_DAMAGE        = 20;
inline constexpr int RING_SHIELD       = 40;
inline constexpr int LIVES             = 3;
inline constexpr unsigned long HIT_INVULN_MS  = 700;
inline constexpr unsigned long SPAWN_INVULN_MS = 2200;
inline constexpr unsigned long DOWN_MS        = 1800;  // ship destroyed: pause before the retry

// --- Lasers -------------------------------------------------------------------
inline constexpr int   SHOT_POOL       = 24;    // room for rapid fire
inline constexpr float SHOT_SPEED      = 150.0f;    // per frame
inline constexpr float SHOT_RANGE      = 6500.0f;
inline constexpr float SHOT_LEN        = 380.0f;    // drawn length
inline constexpr float LASER_SPREAD    = 48.0f;     // the twin beams, +- x
inline constexpr float SHOT_HIT_PAD    = 40.0f;     // added to a target's radius
inline constexpr unsigned long FIRE_TAP_MS  = 110;  // fastest tapping
inline constexpr unsigned long FIRE_HOLD_MS = 190;  // held: steady fire
inline constexpr unsigned long RAPID_TAP_MS  = FIRE_TAP_MS / 2;    // with the rapid-fire pod
inline constexpr unsigned long RAPID_HOLD_MS = FIRE_HOLD_MS / 2;
inline constexpr float RETICLE_NEAR_Z  = SHIP_Z + 1300.0f;
inline constexpr float RETICLE_FAR_Z   = SHIP_Z + 3000.0f;

// --- Bombs --------------------------------------------------------------------
// B tapped (released within BOMB_TAP_MS) drops one; held, B quits.
inline constexpr int   BOMBS_START     = 3;
inline constexpr int   BOMBS_MAX       = 5;
inline constexpr unsigned long BOMB_TAP_MS = 400;
inline constexpr float BOMB_SPEED      = 70.0f;
inline constexpr float BOMB_FUSE_Z     = SHIP_Z + 1700.0f;   // blows here, or on contact
inline constexpr float BOMB_RADIUS     = 750.0f;
inline constexpr int   BOMB_BOSS_DAMAGE = 6;
inline constexpr unsigned long BLAST_MS = 650;

// --- Fighters -----------------------------------------------------------------
inline constexpr int   FIGHTER_POOL    = 10;
inline constexpr float FIGHTER_SCALE   = 75.0f;     // model units to world
inline constexpr float FIGHTER_R       = 95.0f;     // hit radius
inline constexpr int   FIGHTER_POINTS  = 100;
inline constexpr int   WAVE_PERFECT_POINTS = 500;
inline constexpr unsigned long FORMATION_STAGGER_MS = 330;
// A wave waits this long at most for slots the last one's fighters hold.
inline constexpr unsigned long WAVE_LAUNCH_MS = 10000;
inline constexpr int   FIRE_PCT        = 70;        // chance a fighter takes a shot it's due, loop 1
inline constexpr int   FIRE_PCT_PER_LOOP = 8;

// --- Loops ----------------------------------------------------------------------
// Each loop round the stages is harder, up to LOOP_CAP; after that it stays
// as hard as loop LOOP_CAP. "Steps" below are loops past the first, capped.
inline constexpr int   LOOP_CAP          = 5;
inline constexpr int   WAVE_EXTRA_PER_LOOP = 1;     // fighters added to every wave
inline constexpr int   WAVE_MAX          = 9;
inline constexpr int   FIGHTER_PACE_PER_LOOP = 6;   // % faster along their paths
inline constexpr int   BURST_FROM_LOOP   = 3;       // fighters can fire pairs from here
inline constexpr int   BURST_PCT         = 15;      // chance a shot is a pair, at BURST_FROM_LOOP...
inline constexpr int   BURST_PCT_PER_LOOP = 10;     // ...and up this much each loop after
inline constexpr int   LEAD_PCT_PER_LOOP = 6;
inline constexpr int   BOSS_PACE_PER_LOOP = 8;      // % quicker boss attacks
inline constexpr int   BOSS_HP_PER_LOOP  = 20;      // % more boss health
inline constexpr int   FIELD_DENSER_PER_LOOP = 8;   // % shorter gaps between field hazards
inline constexpr int   ROCK_AIMED_PER_LOOP = 6;

// --- Extra lives ----------------------------------------------------------------
inline constexpr long  EXTRA_LIFE_FIRST  = 75000;
inline constexpr long  EXTRA_LIFE_EVERY  = 50000;
inline constexpr int   LIVES_MAX         = 9;

// --- Enemy shots --------------------------------------------------------------
inline constexpr int   ESHOT_POOL      = 18;
inline constexpr float ESHOT_SPEED     = 46.0f;     // per frame, towards where you were (or will be)
inline constexpr float ESHOT_SPEED_PER_LOOP = 5.0f;
inline constexpr float ESHOT_R         = 26.0f;     // drawn size (world)
inline constexpr int   LEAD_PCT        = 50;        // fighter shots aimed where you're heading
inline constexpr float MIN_FIRE_Z      = SHIP_Z + 600.0f;   // nothing fires point-blank

// --- Rocks --------------------------------------------------------------------
inline constexpr int   ROCK_POOL       = 8;
inline constexpr int   ROCK_BIG_SLOTS  = 3;         // slots 0..2 big, the rest small
inline constexpr float ROCK_BIG_R      = 150.0f;
inline constexpr float ROCK_SMALL_R    = 85.0f;
inline constexpr int   ROCK_BIG_HP     = 3;
inline constexpr int   ROCK_BIG_POINTS = 60;
inline constexpr int   ROCK_SMALL_POINTS = 30;
inline constexpr float ROCK_SPAWN_Z    = 7200.0f;
inline constexpr float ROCK_FIELD_X    = 900.0f;    // +- spawn spread
inline constexpr float ROCK_FIELD_Y    = 650.0f;
inline constexpr int   ROCK_AIMED_PCT  = 40;        // spawned on your line

// --- Shield rings -------------------------------------------------------------
inline constexpr int   RING_POOL       = 2;
inline constexpr float RING_R          = 170.0f;
inline constexpr float RING_CATCH_R    = 150.0f;    // centre within this: collected
inline constexpr int   RING_POINTS     = 200;

// --- Rapid-fire pod -----------------------------------------------------------------
// From the second loop, one flies in during segments POD_SEG_A and POD_SEG_B
// of each stage while you haven't got it. Doubles the fire rate until you
// lose a life.
inline constexpr int   POD_SEG_A       = 2;
inline constexpr int   POD_SEG_B       = 6;
inline constexpr unsigned long POD_AFTER_MS = 2500; // into the segment
inline constexpr float POD_R           = 80.0f;
// Rings and pods are placed clear of any obstacle within this much depth,
// and no obstacle is put within it while one is on its way.
inline constexpr float PICKUP_CLEAR_Z  = 900.0f;
inline constexpr float PICKUP_CLEAR_R  = 110.0f;
inline constexpr float POD_CATCH_R     = 140.0f;
inline constexpr int   POD_POINTS      = 500;

// --- Flight aids ------------------------------------------------------------------
// The next obstacle ahead gets its front face outlined, and a marker on it
// where the ship will pass: green clear, red a hit.
inline constexpr float AID_RANGE       = 4200.0f;   // shown this far ahead of the ship

// --- Stage 2: the planet --------------------------------------------------------
inline constexpr float GROUND_Y        = -430.0f;   // world height of the ground
inline constexpr float GROUND_CELL     = 420.0f;    // checkerboard square
inline constexpr float TOWER_TOP       = -150.0f;   // turret towers: low enough to fly over
inline constexpr unsigned long PLANET_FIELD_MS = 850;   // a hazard this often in a field

// --- Stage 3: the trench ----------------------------------------------------------
inline constexpr float TRENCH_HALF_W   = 560.0f;    // walls at +-
inline constexpr float TRENCH_FLOOR    = -380.0f;
inline constexpr float TRENCH_TOP      = 440.0f;    // top of the walls
inline constexpr unsigned long TRENCH_FIELD_MS = 1400;
inline constexpr float BARRIER_GAP     = 300.0f;    // the way through a barrier
inline constexpr unsigned long GATE_MS = 1100;      // laser gates: on this long, then off as long

// --- Obstacles and turrets ----------------------------------------------------------
inline constexpr int   BOX_POOL        = 14;
inline constexpr int   BOX_DAMAGE      = 25;
inline constexpr float BOX_SPAWN_Z     = 7600.0f;
inline constexpr int   TURRET_POOL     = 6;
inline constexpr int   TURRET_HP       = 2;
inline constexpr float TURRET_R        = 90.0f;
inline constexpr int   TURRET_POINTS   = 150;
inline constexpr unsigned long TURRET_FIRE_MS = 1500;
inline constexpr float TURRET_FIRE_NEAR = 1800.0f;  // fires while this far ahead...
inline constexpr float TURRET_FIRE_FAR  = 6000.0f;  // ...and no further

// --- Missiles (the crawler's) ----------------------------------------------------------
inline constexpr float MISSILE_SPEED   = 26.0f;     // per frame
inline constexpr float MISSILE_TURN    = 0.07f;     // share of the way to "at you" it turns per frame
inline constexpr float MISSILE_R       = 45.0f;     // shootable radius
inline constexpr float MISSILE_STOP_Z  = SHIP_Z + 900.0f;   // stops homing here: dodgeable
inline constexpr int   MISSILE_POINTS  = 50;

// --- Boss ---------------------------------------------------------------------
inline constexpr float BOSS_Z          = 2000.0f;    // the dreadnought; see bossZ() for the others
inline constexpr float BOSS_ENTER_Z    = 8500.0f;
inline constexpr unsigned long BOSS_ENTER_MS = 3200;
inline constexpr float BOSS_SWAY_X     = 260.0f;
inline constexpr float BOSS_SWAY_Y     = 140.0f;
inline constexpr float BOSS_BASE_Y     = 90.0f;
inline constexpr float BOSS_HULL_R     = 330.0f;    // shots here are absorbed
inline constexpr float CANNON_X        = 480.0f;    // from the hull centre: within reach at every sway
inline constexpr float CANNON_R        = 110.0f;
inline constexpr float CORE_R          = 110.0f;
inline constexpr int   CANNON_HP       = 12;
inline constexpr int   CORE_HP         = 22;
inline constexpr int   CANNON_POINTS   = 1000;
inline constexpr int   CORE_POINTS     = 3000;
inline constexpr unsigned long CANNON_FIRE_MS = 950;
inline constexpr unsigned long RING_BURST_MS  = 3300;   // once a cannon is lost
inline constexpr unsigned long CORE_FIRE_MS   = 1150;   // once the core is open
inline constexpr unsigned long CORE_BURST_MS  = 2500;
inline constexpr int   BURST_SHOTS     = 8;
inline constexpr float BURST_RADIUS    = 170.0f;    // ring size where it reaches you
inline constexpr unsigned long BOSS_DEATH_MS = 2400;
// The reactor's shield fan: turn rate (degrees per frame) and the gap.
inline constexpr float FAN_SPIN    = 1.7f;
inline constexpr float FAN_GAP_DEG = 120.0f;

// --- Stage --------------------------------------------------------------------
inline constexpr unsigned long INTRO_MS   = 3000;   // fly-in with the stage name
inline constexpr unsigned long ROCK_SPAWN_MS = 380;   // stage 1 fields
inline constexpr unsigned long RESULTS_MIN_MS = 2500;
inline constexpr unsigned long RESULTS_MAX_MS = 9000;
inline constexpr int   SHIELD_BONUS_PER_POINT = 20;

// --- Attract demo -------------------------------------------------------------
inline constexpr unsigned long DEMO_MIN_MS = 22000;
inline constexpr unsigned long DEMO_MAX_MS = 32000;
// How often the demo's pilot rethinks where to fly, like a player's reaction time.
inline constexpr unsigned long DEMO_REPLAN_MS = 120;

// --- Menus and music ----------------------------------------------------------
inline constexpr unsigned long ATTRACT_SLIDE_MS    = 8000;
inline constexpr unsigned long EXIT_HOLD_MS        = 2000;   // hold B
inline constexpr unsigned long QUIT_HINT_DELAY_MS  = 650;
inline constexpr unsigned long GAMEOVER_TIMEOUT_MS = 15000;
inline constexpr const char* STAR_MUSIC = "/audio/flux-star.wav";

// --- Backdrop -----------------------------------------------------------------
inline constexpr int   STAR_COUNT  = 70;
inline constexpr int   PLANET_D    = 40;       // pre-rendered gas giant, pixels

}  // namespace starflux

#endif  // STAR_FLUX_CONFIG_H
