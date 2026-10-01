#ifndef BRICK_BALL_H
#define BRICK_BALL_H

#include <math.h>
#include "BrickConfig.h"

// =============================================================================
// BRICK FLUX — balls, capsules and shots, and the bat's bounce.
//
// A ball keeps a unit direction; its speed is the game's (the level's,
// slowed by the Slow capsule), so every ball speeds up or slows together.
// Angles are in degrees from the +x axis with up positive, as a player
// thinks of them: 90 is straight up.
// =============================================================================

namespace brickflux {

struct Ball {
    bool  active = false;
    float x = 0, y = 0;          // centre
    float dx = 0, dy = -1;       // unit direction (screen y down)
    bool  held = false;          // on the bat: waiting to be served, or caught
    float heldOffset = 0;        // where on the bat, from its centre
    unsigned long heldSince = 0;
    bool  pierce = false;        // a Flux Smash: through everything to the top wall
    bool  perfect = false;       // ...three columns wide
    uint8_t idleBounces = 0;     // walls and steel since a brick or the bat
    unsigned long contactAt = 0; // last time it left the bat (for a late release)
    uint8_t pol = POL_CYAN;      // its colour, taken from the bat at each touch
    unsigned long portalUntil = 0; // just came out of a portal: ignore portals till then
};

struct Capsule { bool active = false; float x = 0, y = 0; uint8_t kind = 0; };
struct Shot    { bool active = false; float x = 0, y = 0; };
struct Popup   { bool active = false; int16_t x = 0, y = 0; int32_t pts = 0; uint8_t mult = 1; unsigned long at = 0; };
// A gun's or a boss's shot, falling at the bat in its colour.
struct Bolt    { bool active = false; float x = 0, y = 0, vx = 0, vy = 0; uint8_t pol = POL_CYAN; };
// From a spark brick: wobbles down to be caught.
struct Spark   { bool active = false; float x0 = 0, y = 0; unsigned long at = 0; };

// A boss's core: a block the ball damages (a coloured one only by a ball
// of its colour), with HP.
struct Core {
    bool  active = false;
    float x = 0, y = 0, w = 0, h = 0;   // centre and size
    int   hp = 0, maxHp = 0;
    uint8_t pol = POL_NONE;
    float vx = 0, homeX = 0;
    unsigned long flashUntil = 0, fireAt = 0;
    unsigned long immuneUntil = 0;      // a ball's hit doesn't count again till then
};
// A loose brick belonging to a boss: a shield (orbiting or fixed to a
// core) or one of the Hive's budded guns, drifting down.
struct Sat {
    bool  active = false;
    float x = 0, y = 0;                 // top-left, a brick's 7x4
    uint8_t pol = POL_NONE, hits = 1;
    bool  gun = false;
    int8_t core = -1;                   // the core it rides with, or -1 (drifting)
    float angle = 0, ox = 0, oy = 0;    // orbit angle, or offset from its core
    int16_t timer = 0;                  // a gun's ms to its next shot
};

inline float degToRad(float d) { return d * (float)PI / 180.0f; }
inline float radToDeg(float r) { return r * 180.0f / (float)PI; }

inline void dirFromAngle(float deg, float &dx, float &dy) {
    dx = cosf(degToRad(deg));
    dy = -sinf(degToRad(deg));
}
inline float angleOf(float dx, float dy) { return radToDeg(atan2f(-dy, dx)); }

// Where the bat sends a straight-up smash: along its tilted face's normal.
// Tilt is positive with the right end raised, which sends the ball left.
inline float batAimDeg(float tiltDeg) { return 90.0f + tiltDeg; }

// The bounce off the bat: a reflection about its tilted face's normal,
// then English from where it met the bat (`offset`, -1 at the left end to
// +1 at the right, sending it further that way), clamped so it always
// leaves upwards and never flatter than MIN_LAUNCH_DEG.
inline void bounceOffBat(float &dx, float &dy, float tiltDeg, float offset) {
    const float t = degToRad(tiltDeg);
    const float nx = -sinf(t), ny = -cosf(t);
    const float dot = dx * nx + dy * ny;
    float rx = dx - 2.0f * dot * nx, ry = dy - 2.0f * dot * ny;
    float a = angleOf(rx, ry);
    if (a < -90.0f) a += 360.0f;             // straight down-left reads as 180+, not -
    a -= offset * ENGLISH_DEG;
    a = constrain(a, MIN_LAUNCH_DEG, 180.0f - MIN_LAUNCH_DEG);
    dirFromAngle(a, dx, dy);
}

// Where a ball at (x, y) going (dx, dy) crosses y = lineY, bouncing off
// the side walls and ceiling but ignoring bricks: the autopilot's guess.
// `dist` gets the path length, `outDx` (if given) its direction across on
// arrival. False if it can't get there (e.g. flat).
inline bool predictCrossing(float x, float y, float dx, float dy, float lineY, float &outX, float &dist,
                            float *outDx = nullptr) {
    const float lo = FIELD_L + BALL_HALF, hi = FIELD_R - BALL_HALF, top = FIELD_T + BALL_HALF;
    dist = 0;
    for (int bounce = 0; bounce < 12; ++bounce) {
        if (fabsf(dy) < 1e-3f) return false;
        // Time to each boundary along the path; take the nearest.
        float tY = dy > 0 ? (lineY - y) / dy : (top - y) / dy;
        float tX = dx > 0 ? (hi - x) / dx : dx < 0 ? (lo - x) / dx : 1e9f;
        if (tY < 0) tY = 0;
        if (tX < 0) tX = 0;
        if (tY <= tX) {
            x += dx * tY; y += dy * tY; dist += tY;
            if (dy > 0) { outX = x; if (outDx) *outDx = dx; return true; }
            dy = -dy;                        // the ceiling
        } else {
            x += dx * tX; y += dy * tX; dist += tX;
            dx = -dx;
        }
    }
    return false;
}

}  // namespace brickflux

#endif  // BRICK_BALL_H
