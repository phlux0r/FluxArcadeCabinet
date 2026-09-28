#ifndef TUBE_MATH_H
#define TUBE_MATH_H

#include <Arduino.h>
#include "TubeFluxConfig.h"

// Small angle and timing helpers shared across Tube Flux. Angles are
// degrees round the tunnel, 0 at the bottom (see TubeFluxConfig.h).

namespace tubeflux {

inline float wrapDeg(float a) {
    while (a >= 360.0f) a -= 360.0f;
    while (a <    0.0f) a += 360.0f;
    return a;
}

// Shortest signed difference between two angles round the tunnel.
inline float deltaDeg(float a, float b) {
    float d = a - b;
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

// The lane an angle is in.
inline int laneAt(float angle) {
    return ((int)lroundf(wrapDeg(angle) / LANE_DEG)) % TUBE_SIDES;
}

// Wrap-safe millis() deadlines.
inline bool reached(unsigned long deadline) { return (long)(millis() - deadline) >= 0; }
inline bool before(unsigned long deadline)  { return (long)(millis() - deadline) < 0; }

}  // namespace tubeflux

#endif  // TUBE_MATH_H
