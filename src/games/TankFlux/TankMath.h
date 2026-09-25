#ifndef TANK_MATH_H
#define TANK_MATH_H

#include <Arduino.h>
#include "TankFluxConfig.h"

// Small angle, distance, colour and timing helpers shared across Tank Flux.
// Headings are degrees, with forward = (sin h, 0, cos h).

namespace tankflux {

// Components are in RGB565's own ranges: r 0-31, g 0-63, b 0-31.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Wrap-safe "has this millis() deadline passed?".
inline bool reached(unsigned long deadline) {
    return (long)(millis() - deadline) >= 0;
}

// Shortest signed difference between two headings.
inline float angleDiff(float target, float current) {
    float d = target - current;
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

inline float wrapAngle(float a) {
    while (a >= 360.0f) a -= 360.0f;
    while (a <    0.0f) a += 360.0f;
    return a;
}

// Heading from (fromX,fromZ) towards (toX,toZ).
inline float bearingTo(float fromX, float fromZ, float toX, float toZ) {
    return degrees(atan2f(toX - fromX, toZ - fromZ));
}

inline bool within(float ax, float az, float bx, float bz, int32_t radius) {
    float dx = ax - bx, dz = az - bz;
    return dx * dx + dz * dz < (float)radius * (float)radius;
}

// If (x,z) is inside the circle, move it out to the edge along the line from
// the centre and return true. Pushing out (rather than rejecting the move)
// lets the player slide along whatever they drive into at any angle.
inline bool pushOutOfCircle(float &x, float &z, float cx, float cz, float r) {
    float dx = x - cx, dz = z - cz;
    float d2 = dx * dx + dz * dz;
    if (d2 >= r * r) return false;
    float d = sqrtf(d2);
    if (d < 0.0001f) { dx = r; dz = 0.0f; d = r; }   // exactly on centre
    float push = (r - d) / d;
    x += dx * push;
    z += dz * push;
    return true;
}

inline float distToSegment(float px, float pz, float x0, float z0, float x1, float z1) {
    float dx = x1 - x0, dz = z1 - z0;
    float lenSq = dx * dx + dz * dz;
    float t = (lenSq > 0.0001f) ? ((px - x0) * dx + (pz - z0) * dz) / lenSq : 0.0f;
    t = fmaxf(0.0f, fminf(1.0f, t));
    float cx = x0 + t * dx, cz = z0 + t * dz;
    float ex = px - cx, ez = pz - cz;
    return sqrtf(ex * ex + ez * ez);
}

inline float distToRiver(float x, float z) {
    return distToSegment(x, z, (float)RIVER_X0, (float)RIVER_Z0,
                         (float)RIVER_X1, (float)RIVER_Z1);
}

inline bool inRiver(float x, float z) {
    return distToRiver(x, z) < (float)RIVER_WIDTH / 2.0f;
}

}  // namespace tankflux

#endif  // TANK_MATH_H
