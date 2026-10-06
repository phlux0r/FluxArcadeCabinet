#ifndef RESONANCE_FIGURE_H
#define RESONANCE_FIGURE_H

#include <math.h>
#include <stdint.h>
#include "ResonanceConfig.h"

// =============================================================================
// The Lissajous maths: the clean ratios, which phases draw the same figure,
// and the match test. Plain C++, so the harness checks it directly.
//
// A ratio a:b (a <= b, no common factor) with phase p draws
//     x = sin(a*t + p),  y = sin(b*t),   t in [0, 2*pi)
// The dial has a stop for each ratio the game has reached, in the order
// below (a/b rising).
//
// Two phases draw the same figure when (a) they differ by a multiple of
// 2*pi/b (t shifted by 2*pi/b leaves y alone and turns x's phase by
// 2*pi*a/b, and a has every residue mod b), or (b) they sum to
// pi - a*pi/b (t -> pi/b - t leaves y alone and reflects x's phase). And
// since x's phase moves no point more than its own change in x, two
// phases within g of each other (so reduced) draw figures no more than g
// apart: the phase gap is a bound on how different they look.
// =============================================================================

namespace resonance {

constexpr float TWO_PI_F = 6.2831853f;
constexpr float PI_F = 3.1415927f;

struct Ratio { uint8_t a, b; uint8_t firstWave; };

// In dial order. firstWave: the wave a ratio starts turning up on (and
// gets its stop on the dial).
constexpr Ratio RATIOS[] = {
    { 1, 3, 3 },
    { 2, 5, 5 },
    { 1, 2, 1 },
    { 3, 5, 8 },
    { 2, 3, 3 },
    { 3, 4, 5 },
    { 4, 5, 8 },
    { 1, 1, 1 },
};
constexpr int RATIO_COUNT = sizeof(RATIOS) / sizeof(RATIOS[0]);

// x wrapped into [-m/2, m/2).
inline float wrapHalf(float x, float m) {
    x = fmodf(x, m);
    if (x < -m / 2) x += m;
    if (x >= m / 2) x -= m;
    return x;
}

// How far apart two phases of one ratio are, once the symmetries above
// are taken out: 0 for the same figure.
inline float phaseGap(const Ratio &r, float p1, float p2) {
    const float period = TWO_PI_F / r.b;
    const float mirror = PI_F - PI_F * r.a / r.b;
    const float shift = fabsf(wrapHalf(p1 - p2, period));
    const float flip = fabsf(wrapHalf(p1 + p2 - mirror, period));
    return shift < flip ? shift : flip;
}

// A point of the figure, unit size.
inline void figurePoint(const Ratio &r, float phase, float t, float &x, float &y) {
    x = sinf(r.a * t + phase);
    y = sinf(r.b * t);
}

// Enough segments that the figure's chords stay short at either size.
inline int segmentsFor(const Ratio &r, bool big) {
    return big ? 24 * (r.a + r.b) + 24 : 8 * (r.a + r.b) + 16;
}

}  // namespace resonance

#endif  // RESONANCE_FIGURE_H
