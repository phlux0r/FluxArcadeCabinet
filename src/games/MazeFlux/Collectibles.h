#ifndef COLLECTIBLES_H
#define COLLECTIBLES_H

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Key — collected to unlock matching Door
// -----------------------------------------------------------------------------
struct Key {
    int     x, y;
    uint8_t colourId;   // matches a Door colourId
    bool    collected = false;
};

// -----------------------------------------------------------------------------
// Door — blocks passage until matching Key is collected
// -----------------------------------------------------------------------------
struct Door {
    int     x, y;
    uint8_t colourId;
    bool    open = false;

    void tryOpen(uint8_t keyColourId) {
        if (keyColourId == colourId) open = true;
    }
};

// -----------------------------------------------------------------------------
// SpeedBoost pickup
// -----------------------------------------------------------------------------
struct SpeedBoost {
    int  x, y;
    bool collected = false;
};

// -----------------------------------------------------------------------------
// TimeBonus pickup
// -----------------------------------------------------------------------------
struct TimeBonus {
    int  x, y;
    int  bonusSeconds = 15;
    bool collected    = false;
};

// -----------------------------------------------------------------------------
// Exit — locked until all keys on this floor collected
// -----------------------------------------------------------------------------
struct Exit {
    int  x, y;
    bool unlocked = false;
};

// -----------------------------------------------------------------------------
// Teleport — auto on contact, links two floor positions
// -----------------------------------------------------------------------------
struct Teleport {
    int  x, y;
    int  destFloor;
    int  destX, destY;
    bool active = true;
};

#endif // COLLECTIBLES_H
