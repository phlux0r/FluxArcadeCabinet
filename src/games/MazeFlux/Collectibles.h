#ifndef COLLECTIBLES_H
#define COLLECTIBLES_H

#include <Arduino.h>

// Keys, doors, the exit, pickups and teleport pads. Positions are cells.

// A key: opens the door of its colour (or, the last one, the exit).
struct Key {
    int     x, y;
    uint8_t colourId;
    bool    collected = false;
};

// A door across the way out of cell (x, y) towards dir (a WALL_ bit): it
// blocks that crossing, both ways, until its key is collected.
struct Door {
    int     x, y;
    uint8_t dir;
    uint8_t colourId;
    bool    open = false;
};

struct SpeedBoost {
    int  x, y;
    bool active = false;
};

struct TimeBonus {
    int  x, y;
    bool active = false;
};

// The exit: open once every key is collected.
struct Exit {
    int  x, y;
    bool unlocked = false;
};

// A teleport pad: walking onto it puts you on its partner.
struct Teleport {
    int x, y;
    int partner;     // index of the other pad
};

#endif // COLLECTIBLES_H
