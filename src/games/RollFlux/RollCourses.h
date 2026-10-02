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
// Stage 0 has one course, a tour of the basics: a ramp up, a gap with
// narrow ways round it, a ramp down, a zigzag, a block to roll round and a
// ramp sideways up to the goal.
// =============================================================================

namespace rollflux {

struct CourseDef { int w, h; const char* const* rows; };

static const char* const COURSE_1_ROWS[] PROGMEM = {
    "............1#1#1#1#....",
    "............1#1G1G1#....",
    "............1#1#1#1#....",
    "..........1#1#1#1#1#....",
    "......0e0e1#1#1#1#1#....",
    "......0e0e1#1#1#1#1#....",
    "......0#0#0#0#0#0#......",
    "......0#0#2#2#0#0#......",
    "......0#0#2#2#0#0#......",
    "......0#0#0#0#0#0#......",
    "............0#0#........",
    "........0#0#0#0#........",
    "........0#0#............",
    "........0#0#............",
    "........0s0s0s0s........",
    "....1#1#1#1#1#1#1#1#....",
    "....1#1#1#1#1#1#1#1#....",
    "....1#1#1#1#1#1#1#1#....",
    "....1#1#1#1#1#1#1#1#....",
    "....1#............1#....",
    "....1#............1#....",
    "....1#............1#....",
    "....1#1#1#1#1#1#1#1#....",
    "....1#1#1#1#1#1#1#1#....",
    "....1#1#1#1#1#1#1#1#....",
    "....1#1#1#1#1#1#1#1#....",
    "..........0n0n..........",
    "..........0#0#..........",
    "..........0#0#..........",
    "..........0#0#..........",
    "..........0#0#..........",
    "......0#0#0#0#0#0#......",
    "......0#0#0#0#0#0#......",
    "......0#0#0S0#0#0#......",
    "......0#0#0#0#0#0#......",
    "......0#0#0#0#0#0#......",
};

static const CourseDef COURSES[] = {
    { 12, 36, COURSE_1_ROWS },
};
inline constexpr int COURSE_COUNT = sizeof(COURSES) / sizeof(COURSES[0]);

}  // namespace rollflux

#endif  // ROLL_COURSES_H
