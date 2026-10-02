#ifndef ROLL_COURSES_H
#define ROLL_COURSES_H

#include <Arduino.h>
#include "RollFluxConfig.h"

// =============================================================================
// ROLL FLUX — courses, as text. Each row is a strip of cells, the top row
// the far (north) end; each cell is two characters, its height (0-7, in
// HEIGHT_STEPs) and its kind:
//   .   void (both characters)       #  floor
//   S   the start (floor)            G  the goal (floor)
//   n s e w  a ramp rising one step towards the north, south, east or west
//            (its height is the low end's)
//   R   floor with a rail along each edge that borders the void
//   i   ice                          *  floor with a gem over it
//   C   a checkpoint (floor)
//   ^ v > <  a boost pad pushing north, south, east or west
// =============================================================================

namespace rollflux {

struct CourseDef { const char* name; int w, h, seconds; const char* const* rows; };

// A tour of the basics: a railed run-up over a boost pad and up a ramp, a
// gap with a narrow way round each side (the left one bare, with a gem on
// it; the right one railed), a checkpoint, ice, a ramp down, a zigzag, a
// block to roll round and a ramp sideways up to the goal.
static const char* const COURSE_1_ROWS[] PROGMEM = {
    "............1#1#1#1#....",
    "............1#1G1G1#....",
    "............1#1#1#1#....",
    "..........1*1#1#1#1#....",
    "......0e0e1#1#1#1#1#....",
    "......0e0e1#1#1#1#1#....",
    "......0#0#0#0#0#0#......",
    "......0#0#2#2#0#0#......",
    "......0#0#2#2#0#0#......",
    "......0#0#0#0#0#0#......",
    "............0#0#........",
    "........0*0#0#0#........",
    "........0#0#............",
    "........0#0#............",
    "........0s0s0s0s........",
    "....1#1#1i1i1i1i1#1#....",
    "....1#1#1i1i1i1i1#1#....",
    "....1C1C1C1C1C1C1C1C....",
    "....1#1#1#1#1#1#1#1#....",
    "....1#............1R....",
    "....1*............1R....",
    "....1#............1R....",
    "....1*1#1#1#1#1#1#1*....",
    "....1#1#1#1#1#1#1#1#....",
    "....1#1#1#1#1#1#1#1#....",
    "....1*1#1#1#1#1#1#1*....",
    "..........0n0n..........",
    "..........0R0R..........",
    "..........0^0^..........",
    "..........0R0R..........",
    "..........0R0R..........",
    "......0#0#0#0#0#0#......",
    "......0#0#0#0#0#0#......",
    "......0#0#0S0#0#0#......",
    "......0#0#0#0#0#0#......",
    "......0#0#0#0#0#0#......",
};

static const CourseDef COURSES[] = {
    { "FIRST ROLL", 12, 36, 60, COURSE_1_ROWS },
};
inline constexpr int COURSE_COUNT = sizeof(COURSES) / sizeof(COURSES[0]);

}  // namespace rollflux

#endif  // ROLL_COURSES_H
