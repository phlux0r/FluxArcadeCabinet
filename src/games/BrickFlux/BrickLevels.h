#ifndef BRICK_LEVELS_H
#define BRICK_LEVELS_H

#include "BrickConfig.h"

// =============================================================================
// BRICK FLUX — level layouts, 15 columns by up to 12 rows, top row first.
//   .  empty
//   n  neutral brick (its colour follows its row)
//   h  hard brick, 2 hits      H  hard brick, 3 hits
//   s  steel: only a Flux Smash breaks it, and a level doesn't need it gone
// Stage 1 has the first four; the levels cycle through them, each loop of
// LEVELS_PER_LOOP levels harder (see BrickFluxGame::ballSpeed()).
// =============================================================================

namespace brickflux {

inline constexpr int LAYOUT_COUNT = 4;

static const char LAYOUTS[LAYOUT_COUNT][ROWS][COLS + 1] PROGMEM = {
    {   // 1: Opening
        "hhhhhhhhhhhhhhh",
        "nnnnnnnnnnnnnnn",
        "nnnnnnnnnnnnnnn",
        "nnnnnnnnnnnnnnn",
        "nnnnnnnnnnnnnnn",
        "..nnnnnnnnnnn..",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 2: Pyramid
        ".......H.......",
        "......hhh......",
        ".....nnnnn.....",
        "....nnnnnnn....",
        "...nnnnnnnnn...",
        "..nnnnnnnnnnn..",
        ".nnnnnnnnnnnnn.",
        "hhhhhhhhhhhhhhh",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 3: Bunkers: steel shelves to bank shots off
        "nnnnnnnnnnnnnnn",
        "nhhhnnhhhnnhhhn",
        "nnnnnnnnnnnnnnn",
        "...............",
        "ss..sss.sss..ss",
        "...............",
        "..nnn.nnn.nnn..",
        "..nnn.nnn.nnn..",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 4: Fortress: a steel keep with a gate
        "HHHHHHHHHHHHHHH",
        "hs...........sh",
        "hs.nnnnnnnnn.sh",
        "hs.nnnnnnnnn.sh",
        "hs.nnHHHHHnn.sh",
        "hs.nnnnnnnnn.sh",
        "hs...........sh",
        "hssss.....ssssh",
        "...............",
        "...............",
        "...............",
        "...............",
    },
};

}  // namespace brickflux

#endif  // BRICK_LEVELS_H
