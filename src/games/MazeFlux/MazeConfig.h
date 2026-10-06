#ifndef MAZE_CONFIG_H
#define MAZE_CONFIG_H

// =============================================================================
// MAZE FLUX — TUNING
// Everything the level layout, hazards, timing and scoring are tuned by, in
// one place. Positions in the maze are cells; positions on screen are
// pixels, CELL to a cell.
// =============================================================================

namespace mazecfg {

constexpr int CELL  = 8;      // px a maze cell
constexpr int HUD_H = 10;     // px of HUD at the top
constexpr int VIEW_W = 128;   // px of maze in view
constexpr int VIEW_H = 160 - HUD_H;

// Moving: a cell every MOVE_MS (MOVE_BOOST_MS with the speed boost on).
constexpr unsigned long MOVE_MS       = 200;
constexpr unsigned long MOVE_BOOST_MS = 100;
constexpr unsigned long BOOST_MS      = 5000;

// The start's surroundings: no hazard, key or pickup within this many
// steps of it, and no trap's line of fire through them.
constexpr int START_CLEAR = 3;

// Losing a life: the ship-explodes pause, then this long safe after the
// respawn (flashing).
constexpr unsigned long DEATH_MS        = 1000;
constexpr unsigned long RESPAWN_SAFE_MS = 1500;

// The clock: TIME_BASE_S plus TIME_PER_CELL_S for every cell of the maze.
constexpr float TIME_BASE_S     = 30.0f;
constexpr float TIME_PER_CELL_S = 0.3f;
constexpr int   TIME_BONUS_S    = 15;

// Doors along the way to the exit, by level (each with its key before it;
// one more key opens the exit).
inline int doorsFor(int level) { return level < 4 ? 0 : level < 8 ? 1 : level < 12 ? 2 : 3; }
constexpr int MAX_DOORS = 3;
constexpr int MAX_KEYS  = MAX_DOORS + 1;

// Bombs: a fuse of FUSE_TICKS x FUSE_TICK_MS once you're within
// BOMB_RANGE steps (it resets if you back off); the blast reaches as far.
constexpr int           BOMB_RANGE   = 2;
constexpr int           FUSE_TICKS   = 3;
constexpr unsigned long FUSE_TICK_MS = 800;
constexpr int MAX_BOMBS = 6;

// Traps: a bullet every interval along a straight corridor, at
// BULLET_CELLS_PER_S, stopping at the first wall. Type A leaves room to
// walk its whole corridor between bullets, and TRAP_A_SPARE_MS more; type
// B fires too fast and needs its switch (A pressed in or beside its line
// of fire) to pause it for SWITCH_PAUSE_MS.
constexpr float         BULLET_CELLS_PER_S = 6.0f;
constexpr unsigned long TRAP_A_SPARE_MS    = 700;
constexpr unsigned long TRAP_B_INTERVAL_MS = 350;
constexpr unsigned long SWITCH_PAUSE_MS    = 3000;
constexpr int           TRAP_MIN_LANE      = 3;   // cells of straight corridor in front
constexpr int MAX_TRAPS   = 4;
constexpr int MAX_BULLETS = 4;    // in flight per trap

// Teleport pads come in pairs (from level 6, two pairs from 12).
inline int teleportPairsFor(int level) { return level < 6 ? 0 : level < 12 ? 1 : 2; }
constexpr int MAX_PADS = 4;
constexpr int PAD_MIN_APART = 8;  // steps between a pair's pads

constexpr int MAX_BOOSTS  = 2;
constexpr int MAX_BONUSES = 2;

// Points.
constexpr int PTS_KEY        = 25;
constexpr int PTS_PICKUP     = 10;
constexpr int PTS_LEVEL      = 50;   // times the level, at the exit
constexpr int PTS_PER_SECOND = 2;    // for every second left, at the exit

} // namespace mazecfg

#endif // MAZE_CONFIG_H
