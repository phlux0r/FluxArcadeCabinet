#ifndef BRICK_CONFIG_H
#define BRICK_CONFIG_H

#include <Arduino.h>

// =============================================================================
// BRICK FLUX — constants. Portrait (128x160). Speeds are in pixels per
// second and scaled by the real frame time, so the game plays the same at
// 25 or 40fps. docs/design/BrickFlux.md is the design these follow.
// =============================================================================

namespace brickflux {

// ---- Screen and playfield ---------------------------------------------------
inline constexpr int W = 128, H = 160;
inline constexpr int HUD_H = 10;                 // score, level, lives
inline constexpr int METER_Y = 10;               // the Flux meter, 2px
inline constexpr int FIELD_L = 4, FIELD_R = 124; // inside the side walls
inline constexpr int FIELD_T = 13;               // the ball's ceiling

// ---- Brick grid ---------------------------------------------------------------
// 15 columns of 8px; rows 5px (a 7x4 brick and a 1px gap). The grid's top
// starts at GRID_Y0 and creeps down a row at a time (the advancing wall).
inline constexpr int COLS = 15, ROWS = 12;
inline constexpr int CELL_W = 8, CELL_H = 5;
inline constexpr int GRID_X = FIELD_L;
inline constexpr int GRID_Y0 = 16;
inline constexpr int DANGER_Y = 128;             // a breakable brick reaching it costs a life

// ---- Bat ------------------------------------------------------------------------
inline constexpr int BAT_Y = 148;                // top surface
inline constexpr int BAT_W = 24, BAT_W_WIDE = 36;
inline constexpr float BAT_SPEED = 150.0f;       // px/s at full push
inline constexpr float TILT_MAX_DEG = 25.0f;
inline constexpr float TILT_DEADZONE = 0.35f;    // stick deflection before the bat tilts
inline constexpr float TILT_EASE_S = 0.12f;      // time to settle on the stick's tilt
inline constexpr float ENGLISH_DEG = 12.0f;      // extra angle from where the ball meets the bat
inline constexpr float MIN_LAUNCH_DEG = 20.0f;   // off the bat: never flatter than this

// ---- Ball -----------------------------------------------------------------------
inline constexpr float BALL_HALF = 1.5f;         // 3x3
inline constexpr float BALL_SPEED0 = 90.0f;      // level 1
inline constexpr float BALL_SPEED_STEP = 0.03f;  // +3% a level...
inline constexpr float BALL_SPEED_MAX = 170.0f;  // ...up to this
inline constexpr float LOOP_SPEED_STEP = 0.10f;  // each loop of the levels starts 10% faster
inline constexpr float SUBSTEP_PX = 1.5f;        // the ball moves at most this far between checks
inline constexpr int   MAX_BALLS = 6;
inline constexpr int   IDLE_BOUNCES = 12;        // bounces with no brick or bat before a nudge
inline constexpr float MIN_DY = 0.25f;           // keeps the ball from skimming sideways forever

// ---- Flux Smash -----------------------------------------------------------------
inline constexpr int   METER_FULL = 24;
inline constexpr unsigned long CHARGE_HOLD_MS = 200;    // A held this long (meter full) charges
inline constexpr unsigned long SMASH_WINDOW_MS = 150;   // after release: contact smashes
inline constexpr unsigned long PERFECT_MS = 50;         // ...and this soon after, a Perfect
inline constexpr unsigned long LATE_RELEASE_MS = 60;    // release this soon after contact: Good
inline constexpr int   PERFECT_BONUS = 500;

// ---- Advancing wall ---------------------------------------------------------------
inline constexpr unsigned long WALL_STEP_MS0 = 14000;   // level 1
inline constexpr unsigned long WALL_STEP_DEC = 500;     // shorter each level...
inline constexpr unsigned long WALL_STEP_MIN = 7000;    // ...to this
inline constexpr unsigned long WALL_STEP_FLOOR = 4000;  // after the loops' 15% off each
inline constexpr unsigned long WALL_WARN_MS = 500;      // danger line flashes before a step
inline constexpr int   WALL_PUSHBACK_ROWS = 3;

// ---- Capsules ---------------------------------------------------------------------
enum CapsuleKind : uint8_t { CAP_WIDE, CAP_MULTI, CAP_CATCH, CAP_LASER, CAP_SLOW, CAP_FLUX, CAP_COUNT };
inline constexpr int   CAPSULE_CHANCE = 8;              // one broken brick in this many
inline constexpr float CAPSULE_SPEED = 45.0f;
inline constexpr int   CAPSULE_W = 10, CAPSULE_H = 8;
inline constexpr unsigned long WIDE_MS = 15000, CATCH_MS = 15000, LASER_MS = 10000, SLOW_MS = 10000;
inline constexpr unsigned long CATCH_AUTO_RELEASE_MS = 3000;
inline constexpr float SLOW_FACTOR = 0.65f;
inline constexpr int   MAX_SHOTS = 16;
inline constexpr float SHOT_SPEED = 200.0f;
inline constexpr unsigned long LASER_RELOAD_MS = 250;

// ---- Scoring and lives ------------------------------------------------------------
inline constexpr int PTS_NEUTRAL = 10, PTS_HARD = 30, PTS_STEEL = 300;
inline constexpr int CLEAR_BONUS = 1000, NO_LOSS_BONUS = 2000, HEADROOM_PER_ROW = 100;
inline constexpr int LIVES = 3, MAX_LIVES = 5;
inline constexpr long EXTRA_LIFE_FIRST = 30000, EXTRA_LIFE_EVERY = 50000;
inline constexpr int MAX_LOOPS = 5;              // difficulty stops rising after this
inline constexpr int LEVELS_PER_LOOP = 20;

// ---- Polarity ---------------------------------------------------------------------
// The bat is cyan or magenta (tap B); a ball takes the bat's colour when it
// touches it, and breaks bricks of its own colour (and neutral ones).
enum Pol : uint8_t { POL_NONE = 0, POL_CYAN = 1, POL_MAGENTA = 2, POL_ANY = 3 };
inline constexpr unsigned long SWAP_COOLDOWN_MS = 250;
inline constexpr int PTS_COLOURED = 20;          // x the chain
inline constexpr int CHAIN_MAX = 8;

// ---- Living bricks ------------------------------------------------------------------
inline constexpr int PTS_GUN = 100, PTS_MAGNET = 50, PTS_PORTAL = 200, PTS_SPARK = 50, PTS_CRACK = 10;
inline constexpr unsigned long GUN_MIN_MS = 3000, GUN_MAX_MS = 5000;   // between a gun's shots
inline constexpr int   MAX_BOLTS = 10;
inline constexpr float BOLT_SPEED = 60.0f;
inline constexpr unsigned long STUN_MS = 750;   // a bolt of the other colour freezes the bat
inline constexpr int   ABSORB_METER = 2;        // one of your colour charges the meter
inline constexpr float MAGNET_RADIUS = 28.0f;
inline constexpr float MAGNET_TURN_DEG = 150.0f; // per second, at the magnet, fading to 0 at the radius
inline constexpr unsigned long PORTAL_COOLDOWN_MS = 250;
inline constexpr int   MAX_SPARKS = 4;
inline constexpr float SPARK_SPEED = 40.0f;
inline constexpr int   PTS_SPARK_CATCH = 250;
inline constexpr int   SPARKS_FOR_LIFE = 3;      // caught in one level: an extra life (once)

// ---- Bosses -------------------------------------------------------------------------
// Every fifth level. A core takes 1 from a ball, 4 from a Good smash, 6
// from a Perfect; beaten, it scores BOSS_BONUS x the boss's number.
enum BossKind : uint8_t { BOSS_NONE, BOSS_WARDEN, BOSS_HIVE, BOSS_TWINS, BOSS_ENGINE };
inline constexpr int   BOSS_EVERY = 5;
inline constexpr int   SMASH_DAMAGE = 4, PERFECT_DAMAGE = 6;
inline constexpr long  BOSS_BONUS = 5000;
inline constexpr int   PTS_CORE_HIT = 50;
inline constexpr float BOSS_HP_LOOP_STEP = 0.2f;  // +20% a loop
inline constexpr int   MAX_SATS = 16;
inline constexpr unsigned long BOSS_INTRO_MS = 2400;
inline constexpr int   WARDEN_HP = 16, HIVE_HP = 20, TWIN_HP = 14, ENGINE_HP = 30;
inline constexpr unsigned long WARDEN_FIRE_MS = 2500, HIVE_BUD_MS = 6000, TWIN_FIRE_MS = 3000,
                               TWIN_SWAP_MS = 8000, ENGINE_FIRE_MS = 3000;
inline constexpr unsigned long CORE_IMMUNE_MS = 300;   // after a ball's hit, so it can't pinball one down
inline constexpr unsigned long WARDEN_REGROW_MS = 5000; // its ring grows back a brick
inline constexpr float HIVE_DRIFT = 2.5f;         // px/s, its budded guns creeping down
inline constexpr int   HIVE_MAX_BUDS = 10;

// ---- Phases and attract -------------------------------------------------------------
inline constexpr unsigned long INTRO_MS = 1200;         // "LEVEL n", the rows dropping in
inline constexpr unsigned long CLEAR_MS = 2500;         // the level-clear tally
inline constexpr unsigned long LOST_MS = 1000;          // after the last ball goes
inline constexpr unsigned long ATTRACT_SLIDE_MS = 8000;
inline constexpr unsigned long DEMO_MIN_MS = 30000, DEMO_MAX_MS = 40000;
inline constexpr int DEMO_MIN_LEVEL = 2, DEMO_MAX_LEVEL = 12;
inline constexpr unsigned long GAMEOVER_TIMEOUT_MS = 30000;
inline constexpr unsigned long EXIT_HOLD_MS = 2000;
inline constexpr unsigned long QUIT_HINT_DELAY_MS = 400;
inline constexpr unsigned long POPUP_MS = 700;
inline constexpr unsigned long SHAKE_MS = 133;          // ~4 frames at 30fps

inline constexpr const char* MUSIC = "/audio/flux-brick.wav";

// ---- Colours (RGB565) ----------------------------------------------------------------
inline constexpr uint16_t COL_WALL = 0x2945;
inline constexpr uint16_t COL_DOTS = 0x0866;
inline constexpr uint16_t COL_DANGER = 0x7800, COL_DANGER_HOT = 0xF800;
inline constexpr uint16_t COL_STEEL = 0x8C71, COL_STEEL_HI = 0xCE79;
inline constexpr uint16_t COL_PANEL = 0x0008;
inline constexpr uint16_t COL_CYAN = 0x07FF, COL_MAGENTA = 0xF81F;
inline constexpr uint16_t COL_CYAN_DIM = 0x0249, COL_MAGENTA_DIM = 0x4809;
inline constexpr uint16_t COL_CYAN_HARD = 0x04B2, COL_MAGENTA_HARD = 0xA012;

inline uint16_t polColour(uint8_t p) { return p == POL_CYAN ? COL_CYAN : p == POL_MAGENTA ? COL_MAGENTA : 0xFFFF; }

}  // namespace brickflux

#endif  // BRICK_CONFIG_H
