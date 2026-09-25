#ifndef TANK_FLUX_CONFIG_H
#define TANK_FLUX_CONFIG_H

#include <stdint.h>

// Tank Flux tuning. `inline constexpr` rather than class-static const, so
// passing one to std::min/max by reference never needs an out-of-line
// definition. Speeds and turn rates are per frame; times are milliseconds.

namespace tankflux {

// --- Arena -------------------------------------------------------------------
inline constexpr int32_t ARENA_HALF = 4200;   // playable area is +/- this in X and Z

// The ground is one static mesh at the world origin. Its half-extent must
// cover ARENA_HALF + depthFogFar (JetConfig.hpp) so a tank in one corner
// looking out never sees its edge. Triangle count depends only on
// GROUND_CELLS (Jet's render queue costs ~100 bytes per triangle and has
// run out of contiguous heap before), so the physical size is free.
inline constexpr int32_t GROUND_SIZE  = 20800;   // half-extent 10400
inline constexpr int32_t GROUND_CELLS = 12;      // 242 triangles

// River: a fixed diagonal strip. RIVER_Y lifts it off the terrain so the two
// meshes don't z-fight. Purely visual apart from slowing tanks down.
inline constexpr int32_t RIVER_X0 = -4000, RIVER_Z0 = -3800;
inline constexpr int32_t RIVER_X1 =  2600, RIVER_Z1 =  4000;
inline constexpr int32_t RIVER_Y        = 6;
inline constexpr int32_t RIVER_WIDTH    = 260;
inline constexpr int32_t RIVER_SEGMENTS = 8;
inline constexpr float   RIVER_SPEED_MULT = 0.5f;

// --- Arena layout (ArenaLayout::generate) ------------------------------------
inline constexpr int32_t ARENA_MARGIN          = 300;   // keep objects off the arena edge
inline constexpr int32_t PLACEMENT_MIN_GAP     = 150;   // clearance beyond two circles' radii
inline constexpr int32_t SPAWN_CLEARANCE       = 700;   // open space around the player
inline constexpr int32_t TREE_PLACEMENT_RADIUS = 160;   // canopy footprint
inline constexpr int32_t KIT_PLACEMENT_RADIUS  = 150;
inline constexpr int     PLACEMENT_ATTEMPTS    = 40;

inline constexpr int OBSTACLE_COUNT = 12;
inline constexpr int TREE_COUNT     = 6;
inline constexpr int REPAIR_COUNT   = 3;

// Pine tree: trunk plus two stacked canopy pyramids (24 triangles).
inline constexpr int32_t TREE_TRUNK_W = 34,  TREE_TRUNK_H = 90;
inline constexpr int32_t TREE_LO_BASE = 260, TREE_LO_H    = 190;
inline constexpr int32_t TREE_HI_BASE = 150, TREE_HI_H    = 150;

// --- Camera ------------------------------------------------------------------
// Wide FOV on purpose: at 70 an attacker slid out of frame as soon as you
// turned, making it impossible to track while manoeuvring.
inline constexpr int     CAMERA_FOV  = 88;
inline constexpr int32_t CAMERA_NEAR = 48;
inline constexpr int32_t CAMERA_FAR  = 5200;

// --- Player tank -------------------------------------------------------------
inline constexpr int32_t EYE_HEIGHT  = 120;
inline constexpr int32_t TANK_RADIUS = 150;

// The stick is mounted rotated relative to a landscape game, so X/Y read
// swapped. If driving, turning or strafing comes out backwards on hardware,
// flip the relevant sign.
inline constexpr float DRIVE_SIGN  = -1.0f;
inline constexpr float TURN_SIGN   =  1.0f;
inline constexpr float STRAFE_SIGN =  1.0f;

inline constexpr float TURN_RATE    = 2.2f;    // deg/frame at full deflection
inline constexpr float FWD_SPEED    = 26.0f;
inline constexpr float REV_SPEED    = 14.0f;   // reverse is deliberately slower
inline constexpr float STRAFE_SPEED = 20.0f;   // hold B: joyX strafes instead of driving
inline constexpr float SPEED_SMOOTH = 0.2f;    // 0 = no response, 1 = instant

// --- Health / repair kits ----------------------------------------------------
inline constexpr int           HEALTH_MAX           = 100;
inline constexpr int           REPAIR_AMOUNT        = 30;
inline constexpr int32_t       REPAIR_PICKUP_RADIUS = 240;
inline constexpr unsigned long REPAIR_RESPAWN_MS    = 12000;
// Upright "+" repair cross. It floats REPAIR_Y_OFFSET above the terrain:
// its own vertical half-extent (REPAIR_EXT_HALF) plus ~25 units clearance.
inline constexpr int32_t REPAIR_ARM_HALF = 26;
inline constexpr int32_t REPAIR_EXT_HALF = 62;
inline constexpr int32_t REPAIR_DEPTH    = 22;
inline constexpr int32_t REPAIR_Y_OFFSET = 87;

// --- Player combat -----------------------------------------------------------
// Shells fly flat at a fixed height; gameplay is entirely on the ground plane.
inline constexpr int32_t SHELL_Y    = 95;
inline constexpr int32_t SHELL_SIZE = 46;
inline constexpr int32_t SHELL_OBSTACLE_RADIUS = 20;   // shells die on obstacles: that's what makes cover
inline constexpr float   PLAYER_SHELL_SPEED = 70.0f;
inline constexpr int32_t PLAYER_SHELL_RANGE = 2600;
inline constexpr float   PLAYER_MUZZLE      = 200.0f;
// One shell in flight at a time, Battlezone-style: each shot is a decision.
inline constexpr unsigned long PLAYER_RELOAD_MS = 400;

inline constexpr int     HIT_DAMAGE  = 20;
inline constexpr int32_t HIT_RADIUS  = 190;   // enemy shell vs player
inline constexpr int     BUMP_DAMAGE = 5;     // ramming a tank: light, a side-effect not an attack
inline constexpr int     SCORE_PER_KILL = 100;

// --- Enemy tanks -------------------------------------------------------------
inline constexpr int     MAX_ENEMIES      = 3;
inline constexpr int     MAX_ENEMY_SHELLS = 6;   // room for a boss spread (3) plus regular fire
inline constexpr float   ENEMY_SPEED      = 11.0f;
inline constexpr float   ENEMY_SPEED_PER_LEVEL = 0.9f;
// The turn-rate cap is what makes enemies beatable: you can flank one
// that's committed to a heading.
inline constexpr float   ENEMY_TURN_RATE     = 1.3f;
inline constexpr float   ENEMY_AIM_TOLERANCE = 12.0f;   // degrees off target it will still fire at
inline constexpr int32_t ENEMY_FIRE_RANGE    = 2200;
// Stays out of point-blank range: a shell fired from close up can't be
// driven out of the way of.
inline constexpr int32_t ENEMY_STANDOFF      = 900;
inline constexpr float   ENEMY_SCRAPE_TURN_DEG = 9.0f;   // turn applied when a move is blocked
// Slow enough that breaking sideways outruns a shell: this decides whether
// "keep moving broadside" is a real defence.
inline constexpr float   ENEMY_SHELL_SPEED = 32.0f;
inline constexpr int32_t ENEMY_SHELL_RANGE = 2600;

// Fire cadence at level 1; each level cuts both ends, down to the floor.
inline constexpr unsigned long ENEMY_FIRE_MIN_MS   = 3600;
inline constexpr unsigned long ENEMY_FIRE_MAX_MS   = 6200;
inline constexpr unsigned long ENEMY_FIRE_FLOOR_MS = 1900;
inline constexpr unsigned long ENEMY_FIRE_FLOOR_SPREAD_MS = 800;
inline constexpr unsigned long FIRE_CUT_PER_LEVEL_MS = 320;
// Between an enemy committing to a shot (barrel glows, tone) and the shell
// leaving: the player's cue to move.
inline constexpr unsigned long FIRE_TELEGRAPH_MS = 350;

inline constexpr unsigned long ENEMY_RESPAWN_MS  = 3500;
inline constexpr unsigned long SPAWN_RETRY_MS    = 400;    // every perimeter spot was blocked
inline constexpr unsigned long FIRST_SPAWN_GRACE_MS = 1200;
inline constexpr int     SPAWN_ATTEMPTS       = 12;
inline constexpr int32_t ENEMY_SPAWN_INSET    = 400;    // spawn circle radius = ARENA_HALF - this
inline constexpr int32_t ENEMY_SPAWN_MIN_DIST = 1600;   // from the player

// Classes 2/3 only appear once more than one tank is on the field, so a
// tougher tank is a variation within a fight rather than a surprise opener.
// Class 3 (yellow turret) also leads its aim.
inline constexpr int CLASS_HP[3] = { 1, 2, 3 };

// --- Escalation --------------------------------------------------------------
// The run opens with one tank and earns its way up to three.
inline constexpr int KILLS_PER_LEVEL = 4;
inline constexpr int MAX_LEVEL       = 6;

// --- Boss --------------------------------------------------------------------
// A one-off outside the regular pool; the field doesn't backfill during the
// fight. Its own death doesn't count toward the next trigger.
inline constexpr int BOSS_EVERY_KILLS = 15;
inline constexpr int BOSS_HP      = 8;
inline constexpr int BOSS_HP_STEP = 2;      // each later boss is tougher by this
inline constexpr int BOSS_SCORE   = 1000;
inline constexpr unsigned long BOSS_ALERT_MS  = 2500;   // warning before it spawns
inline constexpr unsigned long BOSS_KLAXON_MS = 400;
// Spawns opposite the player (with jitter) so it's always seen coming.
inline constexpr int32_t BOSS_SPAWN_INSET      = 500;
inline constexpr int32_t BOSS_MIN_SPAWN_DIST   = 2200;
inline constexpr int32_t BOSS_SPAWN_JITTER_DEG = 40;
// Slower turning than a regular tank so it can be flanked; wider standoff
// and a tighter aim cone buy reaction time over a long fight.
inline constexpr float   BOSS_TURN_RATE     = 0.8f;
inline constexpr int32_t BOSS_STANDOFF      = 1500;
inline constexpr float   BOSS_AIM_TOLERANCE = 8.0f;
inline constexpr float   BOSS_SPEED_MULT    = 0.8f;
// Volleys alternate a spread and a burst (see fireVolley()).
inline constexpr float   BOSS_SPREAD_DEG  = 14.0f;
inline constexpr int     BOSS_BURST_SHOTS = 3;
inline constexpr unsigned long BOSS_BURST_GAP_MS = 220;
// Rear armour: a shell travelling within 60 deg of the boss's facing
// (cos 60 = 0.5) came from behind and does extra damage.
inline constexpr float BOSS_REAR_ARC_COS = 0.5f;
inline constexpr int   BOSS_REAR_DAMAGE  = 2;
// Kill-speed bonus: MAX minus PER_SEC for each second of the fight, min 0.
inline constexpr long  BOSS_TIME_BONUS_MAX     = 1000;
inline constexpr long  BOSS_TIME_BONUS_PER_SEC = 10;
inline constexpr unsigned long BOSS_BONUS_SHOW_MS = 3000;

// --- Tank models + per-type tuning -------------------------------------------
// Regular tanks and the boss share all AI, collision and placement code;
// everything that differs between them lives here. The boss is the
// regular tank at ~1.6x.
struct TankSpec {
    float   turnRate;        // deg/frame
    int32_t standoff;        // stops closing in at this distance
    float   aimTolerance;    // deg
    float   speedMult;       // on top of the level's enemy speed
    int32_t radius;          // collision
    int32_t killRadius;      // player shell hit
    float   muzzle;          // shell spawn distance ahead of the hull centre
    int32_t hullW, hullH, hullD;
    int32_t turretW, turretH;
    int32_t barrelR, barrelLen;
    int32_t trackW, trackH, trackD, trackOffset;
    int32_t hullY, turretY, trackY;
};

inline constexpr TankSpec REGULAR_TANK{
    ENEMY_TURN_RATE, ENEMY_STANDOFF, ENEMY_AIM_TOLERANCE, 1.0f,
    /*radius*/ 170, /*killRadius*/ 250, /*muzzle*/ 220.0f,
    /*hull*/ 280, 110, 380,  /*turret*/ 150, 90,  /*barrel*/ 20, 170,
    /*track*/ 36, 50, 400, 130,
    /*y: hull, turret, track*/ 55, 155, 30,
};

inline constexpr TankSpec BOSS_TANK{
    BOSS_TURN_RATE, BOSS_STANDOFF, BOSS_AIM_TOLERANCE, BOSS_SPEED_MULT,
    /*radius*/ 270, /*killRadius*/ 400, /*muzzle*/ 360.0f,
    /*hull*/ 448, 176, 608,  /*turret*/ 240, 144,  /*barrel*/ 32, 272,
    /*track*/ 58, 80, 640, 208,
    /*y: hull, turret, track*/ 88, 248, 48,
};

// --- HUD / presentation ------------------------------------------------------
// Covers the whole arena corner to corner, so nothing is ever off the dial.
inline constexpr int32_t RADAR_RANGE = 6000;
inline constexpr unsigned long MUZZLE_FLASH_MS = 70;
inline constexpr unsigned long HIT_FLASH_MS    = 160;
inline constexpr unsigned long BUMP_FLASH_MS   = 120;
// The arena-shift chime waits for the boss kill fanfare (300ms) to finish.
inline constexpr unsigned long ARENA_SHIFT_CUE_DELAY_MS = 350;
inline constexpr unsigned long ARENA_SHIFT_FLASH_MS     = 320;

// --- Menus / session ---------------------------------------------------------
inline constexpr unsigned long ATTRACT_SLIDE_MS    = 8000;
inline constexpr unsigned long GAMEOVER_TIMEOUT_MS = 30000;
inline constexpr unsigned long EXIT_HOLD_MS        = 2000;   // hold B on attract to exit
// B alone is strafe during play, so quitting mid-game needs A+B held. The
// hint only shows after a longer-than-a-shot press, since firing while
// strafing is common.
inline constexpr unsigned long QUIT_HOLD_MS       = 2000;
inline constexpr unsigned long QUIT_HINT_DELAY_MS = 650;
// playTankStartSound()'s SD path reports "not playing" until the file has
// opened, so the attract loop waits this long before it may take over the
// channel. Same deadline AudioEngine uses for its own SD-latency checks.
inline constexpr unsigned long ATTRACT_MUSIC_GRACE_MS = 300;

}  // namespace tankflux

#endif  // TANK_FLUX_CONFIG_H
