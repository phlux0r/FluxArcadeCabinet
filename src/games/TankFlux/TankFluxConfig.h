#ifndef TANK_FLUX_CONFIG_H
#define TANK_FLUX_CONFIG_H

#include <stdint.h>

// Tank Flux tuning constants. inline constexpr (not class-static const)
// so passing one to std::min/max by reference never needs an
// out-of-line definition.

namespace tankflux {

// --- Arena ---------------------------------------------------------------
inline constexpr int32_t ARENA_HALF   = 4200;   // playable area is +/- this in X and Z
// The ground is ONE static mesh covering the whole bounded arena, built
// once at world origin and never repositioned. An earlier version
// re-centred it on the tank in discrete steps — a trick carried over
// from a design where the play space scrolled endlessly, which isn't
// true here: the arena is bounded, obstacles and repair kits already
// sit at fixed world coordinates, so a mesh sized to cover the whole
// arena plus the fog band needs no repositioning at all. That also
// fixed a real correctness problem the recentring approach had: hills
// (hillHeight()) and the river both need to agree on what "the same
// point in the world" means, which a mesh that moves under them could
// not guarantee.
//
// Sizing: from the worst case (a tank in one corner looking straight out
// through the opposite side) the mesh needs a half-extent of at least
// ARENA_HALF + depthFogFar plus margin = 4200 + 5000 + margin (see
// JetConfig.hpp's depthFogFar). Cell COUNT, not physical size, drives
// triangle count — the ground still dominates it, and every queued
// triangle costs ~100 bytes in Jet's render queue, a single contiguous
// allocation that has already run this hardware out of contiguous heap
// space once. So the size below is generous (real headroom past the
// minimum) while CELLS stays low — bumping ARENA_HALF only grows this
// value, never GROUND_CELLS, so it's free in triangle count.
inline constexpr int32_t GROUND_SIZE  = 20800;   // half-extent 10400
inline constexpr int32_t GROUND_CELLS = 12;      // 242 triangles

// River: a fixed landmark strip, not tied to the tank's position the
// way the terrain is. RIVER_Y sits a few units above the terrain's own
// surface (which itself varies by hillHeight()) so the two meshes never
// go exactly coplanar and flicker against each other. Endpoints scale
// with ARENA_HALF so the diagonal keeps roughly the same corner-to-corner
// proportions as the arena grows; generateArenaLayout() below is what
// actually guarantees nothing else gets placed on top of it.
inline constexpr int32_t RIVER_X0 = -4000, RIVER_Z0 = -3800;
inline constexpr int32_t RIVER_X1 =  2600, RIVER_Z1 =  4000;
inline constexpr int32_t RIVER_Y       = 6;
inline constexpr int32_t RIVER_WIDTH   = 260;
inline constexpr int32_t RIVER_SEGMENTS = 8;
inline constexpr float RIVER_SPEED_MULT = 0.5f;   // player and enemies slow down in the water

// --- Arena layout placement (generateArenaLayout()) -----------------------
// Shared by every category placeCircle() finds a spot for.
inline constexpr int32_t ARENA_MARGIN       = 300;   // stay clear of the arena edge itself
inline constexpr int32_t PLACEMENT_MIN_GAP  = 150;   // extra clearance beyond two circles' own radii
inline constexpr int32_t SPAWN_CLEARANCE    = 700;   // open space kept around the player's current position
inline constexpr int32_t TREE_PLACEMENT_RADIUS = 160;   // canopy footprint, wider than the trunk itself
inline constexpr int32_t KIT_PLACEMENT_RADIUS  = 150;   // visual clearance around a repair cross
inline constexpr int PLACEMENT_ATTEMPTS = 40;

// --- Tank ----------------------------------------------------------------
inline constexpr int32_t EYE_HEIGHT  = 120;
inline constexpr int32_t TANK_RADIUS = 150;

// Both signs are here as named constants because this project has had to
// flip joystick axes by trial more than once — the cabinet's stick is
// physically mounted rotated relative to a landscape (rotation 1) game,
// so X/Y read swapped, same as AsteroidFluxGame. If driving or turning
// comes out backwards on hardware, flip the relevant sign here.
inline constexpr float DRIVE_SIGN = -1.0f;   // flipped after playtest
inline constexpr float TURN_SIGN  = 1.0f;

inline constexpr float TURN_RATE    = 2.2f;   // deg/frame at full deflection
inline constexpr float FWD_SPEED    = 26.0f;  // world units/frame
inline constexpr float REV_SPEED    = 14.0f;  // reverse is deliberately slower
inline constexpr float SPEED_SMOOTH = 0.2f;   // 0=no response, 1=instant
// Hold BTN B: joyX strafes sideways instead of driving forward/back.
// Same sign-flip caveat as DRIVE_SIGN above — flip here if strafe comes
// out backwards on hardware. Turning (joyY) is untouched, so you can
// strafe and adjust aim at the same time.
inline constexpr float STRAFE_SIGN  = 1.0f;
inline constexpr float STRAFE_SPEED = 20.0f;  // world units/frame

// --- Health / repair kits ------------------------------------------------
inline constexpr int HEALTH_MAX = 100;
inline constexpr int REPAIR_AMOUNT = 30;
inline constexpr int REPAIR_COUNT  = 3;
inline constexpr int32_t REPAIR_PICKUP_RADIUS = 240;
inline constexpr unsigned long REPAIR_RESPAWN_MS = 12000;
// Repair cross is upright now (see buildRepairCross()) — vertical
// half-extent is its own extHalf (62), not a height/2, so this is
// 62 + 25 to keep the same ~25-unit ground clearance the old cube had.
inline constexpr int32_t REPAIR_ARM_HALF = 26;   // half-width of the cross arms
inline constexpr int32_t REPAIR_EXT_HALF = 62;   // half-length from center to arm tip
inline constexpr int32_t REPAIR_DEPTH    = 22;   // thin front-to-back extrusion
inline constexpr int32_t REPAIR_Y_OFFSET = 87;

inline constexpr unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;
// B alone is strafe during play, so quitting mid-game needs both
// buttons held together.
inline constexpr unsigned long QUIT_HOLD_MS = 2000;
// Fire (A) while strafing (B) is common, so the quit hint only appears
// once both have been held clearly longer than a normal shot press.
inline constexpr unsigned long QUIT_HINT_DELAY_MS = 650;

// --- Combat --------------------------------------------------------------
// Shells fly flat at a fixed height: gameplay is entirely on the ground
// plane, so there's no reason to carry a Y velocity around.
inline constexpr int32_t SHELL_Y = 95;

inline constexpr float PLAYER_SHELL_SPEED = 70.0f;   // units/frame
inline constexpr int32_t   PLAYER_SHELL_RANGE = 2600;
// One shell in flight at a time, Battlezone-style — it's what stops the
// fire button being a mash and makes each shot a decision.
inline constexpr unsigned long PLAYER_RELOAD_MS = 400;

inline constexpr int MAX_ENEMIES = 3;
inline constexpr int32_t ENEMY_RADIUS   = 170;
// ~half the hull's 280 width, referenced from a low-poly tank model
// the user found (barrel length / hull width ratio there was ~0.62).
inline constexpr int32_t BARREL_LENGTH  = 170;
// Track strips flanking the 280-wide hull: thin, low, and a little
// longer than the hull's own 380 depth for a slight overhang. Offset
// hugs the hull's outer edge with a small overlap so there's no gap.
inline constexpr int32_t TRACK_WIDTH    = 36;
inline constexpr int32_t TRACK_HEIGHT   = 50;
inline constexpr int32_t TRACK_DEPTH    = 400;
inline constexpr int32_t TRACK_OFFSET   = 130;
inline constexpr float ENEMY_SPEED      = 11.0f;
// The turn rate cap is the whole reason enemies are beatable: it's what
// lets you flank one that's already committed to a heading.
inline constexpr float ENEMY_TURN_RATE  = 1.3f;
inline constexpr float ENEMY_AIM_TOLERANCE = 12.0f;  // degrees before it shoots
inline constexpr int32_t ENEMY_FIRE_RANGE = 2200;
// Kept well out rather than closing to point blank: a shell fired from
// close range can't be driven out of the way of, so an enemy parked on
// top of you would be unavoidable damage rather than a threat to play
// around.
inline constexpr int32_t ENEMY_STANDOFF   = 900;
// Cadence at level 1; escalation tightens it (see fireDelay). Slack on
// purpose — the pool is only five hits deep and shells from off-screen
// are the hardest thing in the game to answer.
inline constexpr unsigned long ENEMY_FIRE_MIN_MS = 3600;
inline constexpr unsigned long ENEMY_FIRE_MAX_MS = 6200;
inline constexpr unsigned long ENEMY_FIRE_FLOOR_MS = 1900;
inline constexpr unsigned long ENEMY_RESPAWN_MS  = 3500;

// --- Escalation ----------------------------------------------------------
// The run opens with a single tank and earns its way up to three. This is
// both the difficulty curve and the pacing fix: three simultaneous
// attackers from the first second left no room to learn the arena.
inline constexpr int KILLS_PER_LEVEL = 4;
inline constexpr int MAX_LEVEL = 6;

inline constexpr int MAX_ENEMY_SHELLS = 6;   // room for a boss spread (3) plus regular fire
// Delay between an enemy committing to a shot (barrel glows, warning
// tone) and the shell actually leaving — the player's cue to move.
inline constexpr unsigned long FIRE_TELEGRAPH_MS = 350;
// Slow enough that breaking sideways actually outruns the shell: you need
// to clear HIT_RADIUS before it arrives, so this is the number that
// decides whether "keep moving broadside" is a real defence or a
// suggestion.
inline constexpr float ENEMY_SHELL_SPEED = 32.0f;
inline constexpr int32_t   ENEMY_SHELL_RANGE = 2600;

inline constexpr int HIT_DAMAGE = 20;
inline constexpr int32_t HIT_RADIUS  = 190;   // enemy shell vs player
inline constexpr int32_t KILL_RADIUS = 250;   // player shell vs enemy
inline constexpr int SCORE_PER_KILL = 100;
// Ramming a tank costs a little health too, not just being shot —
// driving through enemies read as a free pass before resolveEnemyCollision()
// existed at all. Deliberately light next to HIT_DAMAGE: this is a
// side-effect of a bad approach, not a real attack.
inline constexpr int BUMP_DAMAGE = 5;

// --- Enemy classes & boss --------------------------------------------
// Class 1 is the original single-hit tank, unchanged. Classes 2/3 only
// enter the mix once _level puts more than one tank on the field at
// once (see pickEnemyClass()), so a tougher tank shows up as a
// variation within an already-multi-enemy fight rather than a surprise
// sprung on a lone opening attacker. No enemy healing/repair-kit use —
// the player has no visibility into enemy HP (no bar, by design, to
// keep the HUD simple), so a tank quietly undoing damage would read as
// unfair rather than clever.
inline constexpr int CLASS_HP[3] = { 1, 2, 3 };

// Boss: a one-off, separate from the regular MAX_ENEMIES pool, so the
// field doesn't backfill with regular tanks mid-fight. Every 15 kills
// (kept simple/round; regular-kill count only — see destroyEnemy(),
// the boss's own death doesn't count toward its own next trigger).
// Dimensions/offsets are the regular tank's own values x1.6 throughout.
inline constexpr int BOSS_EVERY_KILLS = 15;
inline constexpr int BOSS_HP = 8;
// Each subsequent boss is tougher: 1st is BOSS_HP, 2nd is +BOSS_HP_STEP,
// and so on — see _bossesDefeated, incremented in destroyEnemy()'s own
// boss branch right before this would apply to the NEXT one.
inline constexpr int BOSS_HP_STEP = 2;
inline constexpr int BOSS_SCORE = 1000;
inline constexpr int32_t BOSS_RADIUS      = 270;
inline constexpr int32_t BOSS_KILL_RADIUS = 400;
// Backstop minimum for trySpawnBoss()'s opposite-side placement — see
// there for why "always far" is a targeted angle, not just a distance
// floor on a random one.
inline constexpr int32_t BOSS_MIN_SPAWN_DIST = 2200;
inline constexpr int32_t BOSS_SPAWN_JITTER_DEG = 40;
// "BOSS ALERT" banner delay before trySpawnBoss() actually runs — gives
// the player a beat of warning instead of the boss just appearing.
inline constexpr unsigned long BOSS_ALERT_MS = 2500;
// Own AI tuning, used in updateEnemyAI() instead of the regular
// ENEMY_* values when the caller is the boss (see the isBoss check
// there) — playtest feedback was that reusing the regular tank's
// numbers made it home in and line up shots too easily for something
// meant to be a set-piece fight, not just a bigger regular tank.
// Slower turning is what actually lets you flank it (same idea
// ENEMY_TURN_RATE's own comment gives for the regular pool, just
// tuned further given how much longer a boss fight runs); the wider
// standoff and tighter aim cone both buy more reaction time before
// it's willing to fire.
inline constexpr float BOSS_TURN_RATE  = 0.8f;
inline constexpr int32_t   BOSS_STANDOFF   = 1500;
inline constexpr float BOSS_AIM_TOLERANCE = 8.0f;
inline constexpr float BOSS_SPEED_MULT = 0.8f;   // applied on top of enemySpeed()
// Boss volleys alternate between a spread and a burst (see fireVolley()).
inline constexpr float BOSS_SPREAD_DEG = 14.0f;
inline constexpr int BOSS_BURST_SHOTS = 3;
inline constexpr unsigned long BOSS_BURST_GAP_MS = 220;
// Rear armour: a shell travelling within 60° of the boss's own facing
// (cos 60° = 0.5) hit it from behind and does extra damage.
inline constexpr float BOSS_REAR_ARC_COS = 0.5f;
inline constexpr int BOSS_REAR_DAMAGE = 2;
// Kill-speed bonus: BOSS_TIME_BONUS_MAX, minus this per second the
// fight lasted, floored at 0 (100s to reach zero).
inline constexpr long BOSS_TIME_BONUS_MAX = 1000;
inline constexpr long BOSS_TIME_BONUS_PER_SEC = 10;
inline constexpr unsigned long BOSS_BONUS_SHOW_MS = 3000;
inline constexpr int32_t BOSS_HULL_W = 448, BOSS_HULL_H = 176, BOSS_HULL_D = 608;
inline constexpr int32_t BOSS_TURRET_W = 240, BOSS_TURRET_H = 144;
inline constexpr int32_t BOSS_BARREL_R = 32, BOSS_BARREL_LEN = 272;
inline constexpr int32_t BOSS_TRACK_W = 58, BOSS_TRACK_H = 80, BOSS_TRACK_D = 640, BOSS_TRACK_OFFSET = 208;
inline constexpr int32_t BOSS_HULL_Y = 88, BOSS_TURRET_Y = 248, BOSS_TRACK_Y = 48;

// Covers the whole arena corner-to-corner, so nothing is ever off-dial.
inline constexpr int32_t RADAR_RANGE = 6000;

// --- Scene object counts / tree dimensions ------------------------------
// Shared by buildPineTree() (initial construction) and
// repositionPineTree() (boss-kill regeneration) so the two can't drift
// out of sync with each other.
inline constexpr int OBSTACLE_COUNT = 12;
inline constexpr int TREE_COUNT = 6;
inline constexpr int32_t TREE_TRUNK_W = 34, TREE_TRUNK_H = 90;
inline constexpr int32_t TREE_LO_BASE = 260, TREE_LO_H = 190;
inline constexpr int32_t TREE_HI_BASE = 150, TREE_HI_H = 150;

}  // namespace tankflux

#endif  // TANK_FLUX_CONFIG_H
