#ifndef BRICK_LEVELS_H
#define BRICK_LEVELS_H

#include "BrickConfig.h"

// =============================================================================
// BRICK FLUX — level layouts, 15 columns by up to 12 rows, top row first.
//   .  empty
//   n  neutral brick (its colour follows its row): any ball breaks it
//   h  hard neutral brick, 2 hits      H  hard neutral, 3 hits
//   c  cyan brick       m  magenta brick: only a ball of that colour breaks it
//   C  hard cyan        M  hard magenta, 2 hits each
//   s  steel: only a Flux Smash breaks it, and a level doesn't need it gone
//   g  gun with a cyan eye     G  gun with a magenta eye: 2 hits; fires
//      bolts of its eye's colour down at the bat when nothing's below it
//   o  magnet: 2 hits; bends a passing ball towards it
//   p  portal, and its partner (the other p); q  a second pair. The ball
//      goes in one and out of the other; only a smash breaks them
//   *  spark: drops a spark to catch
//
// A loop is LEVELS_PER_LOOP levels: every BOSS_EVERY-th is a boss, the rest
// take these layouts in order (layoutForLevel()). New kinds of brick come
// in a level at a time: colour on 3, magnets 6, guns 7, portals 11.
// =============================================================================

namespace brickflux {

inline constexpr int REGULAR_LAYOUTS = 16;
inline constexpr int ENGINE_LAYOUT = 16;          // the Flux Engine's own grid
inline constexpr int LAYOUT_COUNT = 17;

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
    {   // 3: Bunkers: steel shelves to bank off, and the first colours
        "ccccccc.mmmmmmm",
        "ccccccc.mmmmmmm",
        "nnnnnnn*nnnnnnn",
        "...............",
        "ss..sss.sss..ss",
        "...............",
        "..nnn.n*n.nnn..",
        "..nnn.nnn.nnn..",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 4: Fortress: a steel keep with a gate
        "HHHHHHHHHHHHHHH",
        "hs...........sh",
        "hs.cccc*mmmm.sh",
        "hs.ccccnmmmm.sh",
        "hs.ccHHHHHmm.sh",
        "hs.ccccnmmmm.sh",
        "hs...........sh",
        "hssss.....ssssh",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    // (5: the Warden)
    {   // 6: Magnetic
        "...............",
        "nnnnnnnnnnnnnnn",
        "ccc.o..n..o.mmm",
        "ccc.........mmm",
        "ccc..nn*nn..mmm",
        "ccc..nnnnn..mmm",
        "hhh.........hhh",
        "....o.....o....",
        "..mmmm...cccc..",
        "...............",
        "...............",
        "...............",
    },
    {   // 7: Gun Line
        "HHHHHHHHHHHHHHH",
        "ccccccccccccccc",
        "mmmmmmmmmmmmmmm",
        "..g...G...g....",
        "...............",
        "nnn..nnnnn..nnn",
        "n*n..n...n..n*n",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 8: Checker
        "cmcmcmcmcmcmcmc",
        "mcmcmcmcmcmcmcm",
        "cmcmcmcmcmcmcmc",
        "mcmcmc*cmcmcmcm",
        "...............",
        "hHhHhHhHhHhHhHh",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 9: Diamonds
        "...c.......m...",
        "..ccc.....mmm..",
        ".ccCcc...mmMmm.",
        "ccC*Ccc.mmM*Mmm",
        ".ccCcc.g.mmMmm.",
        "..ccc.....mmm..",
        "...c..o.o..m...",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    // (10: the Hive)
    {   // 11: Wormhole: a portal in the steel's gap, its partner low down
        "nnnnnnnnnnnnnnn",
        "nnnnnnnnnnnnnnn",
        "ssssss.p.ssssss",
        "ccccc.....mmmmm",
        "...............",
        "...............",
        ".......p.......",
        "mm.g.......G.cc",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 12: Zigzag
        "cc..mm..cc..mm.",
        ".cc..mm..cc..mm",
        "..cc..mm..cc..m",
        "m..cc..mm..cc..",
        "mm..cc..mm..cc.",
        ".mm..cc*.mm..cc",
        "...............",
        "s.g.s.o.s.G.s.s",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 13: Twin Towers
        "..HHH.....HHH..",
        "..ccc.....mmm..",
        "..cgc.....mGm..",
        "..ccc..*..mmm..",
        "..ccc.....mmm..",
        "..ccc.....mmm..",
        "..sss..o..sss..",
        "nnnnnnnnnnnnnnn",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 14: Spark Field
        "*.n.*.n.*.n.*.n",
        "ccccccccccccccc",
        ".n.*.n.*.n.*.n.",
        "mmmmmmmmmmmmmmm",
        "o.............o",
        "CCCCCCCMMMMMMMM",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    // (15: the Twins)
    {   // 16: Batteries
        "GGG.ccccccc.ggg",
        "...............",
        "mmmm.o...o.cccc",
        "mMMm.......cCCc",
        "mmmm..*.*..cccc",
        "...............",
        "nnhhnnhhnnhhnnh",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 17: Labyrinth
        "ccccccccccccccc",
        "s.....q.......s",
        "sssss.sssss.sss",
        "mmmmm.mmmmm.mmm",
        "s......p......s",
        "sss.sssss.sssss",
        "..q.........p..",
        "nnnnn*nnn*nnnnn",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 18: Prism
        ".......H.......",
        "......cHm......",
        ".....ccHmm.....",
        "....cccHmmm....",
        "...cccc*mmmm...",
        "..ccccGHgmmmm..",
        ".ccccccHmmmmmm.",
        "o.............o",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    {   // 19: Last Stand
        "HHHHHHHHHHHHHHH",
        "CCCCCCCMMMMMMMM",
        "cmcmcmcmcmcmcmc",
        "g.G.g.G.g.G.g.G",
        "...............",
        "s*sns*sns*sns*s",
        "..o..p...p..o..",
        "nnnnnnnnnnnnnnn",
        "...............",
        "...............",
        "...............",
        "...............",
    },
    // (20: the Flux Engine)
    {   // The Flux Engine's grid: its chamber above a steel band, portals
        // linking the chamber with the space below, and its own wall.
        "...............",
        "...............",
        ".p...........q.",
        "sssssssssssssss",
        "...............",
        ".q...........p.",
        "...............",
        "cmcmcmcmcmcmcmc",
        "nnnnnnnnnnnnnnn",
        "mcmcmcmcmcmcmcm",
        "...............",
        "...............",
    },
};

// Which boss, if any, a level is, and which layout a regular level uses.
inline int bossForLevel(int level) {
    const int inLoop = (level - 1) % LEVELS_PER_LOOP;
    return (inLoop + 1) % BOSS_EVERY == 0 ? (inLoop + 1) / BOSS_EVERY : BOSS_NONE;
}
inline int layoutForLevel(int level) {
    const int inLoop = (level - 1) % LEVELS_PER_LOOP;
    return inLoop - inLoop / BOSS_EVERY;
}

}  // namespace brickflux

#endif  // BRICK_LEVELS_H
