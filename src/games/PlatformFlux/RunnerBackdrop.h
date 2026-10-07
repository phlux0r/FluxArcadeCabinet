#ifndef RUNNER_BACKDROP_H
#define RUNNER_BACKDROP_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

// =============================================================================
// RUNNER BACKDROP
// Moon Patrol-style parallax behind the play area: stars (barely moving), a
// far mountain range, and nearer hills, each scrolling at a fraction of the
// ground's speed. All dark, so platforms and hazards stay the brightest
// things on screen, and the hills stop short of the ground line so a
// platform-tier gap still reads as a black drop. Colours rotate with the
// loop, like the ground's; Night (every second loop) has its own, darker,
// with brighter stars and a moon.
//
// The skylines are sums of sines over a 256px repeat, fixed at compile
// time: nothing here calls random(), so the game's own random sequence
// (and the attract demo) is untouched.
// =============================================================================
class RunnerBackdrop {
public:
    static const int FAR_HORIZON  = 104;   // far range's heights are from here
    static const int NEAR_HORIZON = 112;   // hills filled down to here; black below
    static constexpr float STAR_RATE = 0.05f;   // shares of the ground's scroll speed
    static constexpr float FAR_RATE  = 0.2f;
    static constexpr float NEAR_RATE = 0.45f;

    RunnerBackdrop() {
        for (int i = 0; i < 256; i++) {
            float a = (float)i * (2.0f * PI / 256.0f);
            float f = 22.0f + 12.0f * sinf(2.0f * a + 0.3f) + 7.0f * sinf(5.0f * a + 1.1f) + 3.0f * sinf(11.0f * a + 2.0f);
            float n = 7.0f + 5.0f * sinf(3.0f * a + 2.2f) + 3.0f * sinf(7.0f * a + 0.5f) + 1.5f * sinf(15.0f * a + 1.7f);
            _far[i]  = (uint8_t)constrain((int)f, 4, 46);
            _near[i] = (uint8_t)constrain((int)n, 1, 18);
        }
        uint32_t s = 0x5EED1234u;   // own LCG: fixed star positions
        for (int i = 0; i < STARS; i++) {
            s = s * 1664525u + 1013904223u;
            _starX[i] = (uint8_t)(s >> 24);
            s = s * 1664525u + 1013904223u;
            _starY[i] = (uint8_t)(ArcadeConfig::UI_MARGIN_TOP + 2 + (s >> 24) % 60);
        }
        reset();
    }

    void reset() { _stars = _farX = _nearX = 0.0f; }

    // Once a playing frame, with the ground's scroll speed.
    void update(float scrollSpeed) {
        _stars += scrollSpeed * STAR_RATE;
        _farX  += scrollSpeed * FAR_RATE;
        _nearX += scrollSpeed * NEAR_RATE;
        if (_stars >= 256.0f) _stars -= 256.0f;
        if (_farX  >= 256.0f) _farX  -= 256.0f;
        if (_nearX >= 256.0f) _nearX -= 256.0f;
    }

    // Replaces the play area's black clear: from `top` to the bottom.
    void render(GFXcanvas16 &canvas, int top, int loop) {
        const int W = ArcadeConfig::LANDSCAPE_WIDTH, H = ArcadeConfig::LANDSCAPE_HEIGHT;
        canvas.fillRect(0, top, W, H - top, ArcadeConfig::COLOR_BLACK);

        const bool night = loop % 2 == 1;

        // Stars: dim, a few brighter; two twinkle. Brighter at Night.
        const int so = (int)_stars;
        for (int i = 0; i < STARS; i++) {
            int x = (_starX[i] - so) & 255;
            if (x >= W) continue;
            uint16_t c = (i % 5 == 0) ? rgb(150, 150, 170) : rgb(70, 70, 90);
            if (night) c = (i % 3 == 0) ? rgb(210, 210, 230) : rgb(110, 110, 140);
            if (i % 11 == 0 && ((millis() / 400 + i) & 1)) c = rgb(30, 30, 40);
            canvas.drawPixel(x, _starY[i], c);
        }

        // The moon, at Night: pale, a crater or two, far enough not to move.
        if (night) {
            canvas.fillCircle(126, top + 16, 6, rgb(210, 210, 190));
            canvas.fillCircle(124, top + 14, 1, rgb(170, 170, 150));
            canvas.fillCircle(129, top + 18, 1, rgb(170, 170, 150));
        }

        // The Outpost's loops rotate through the first four; Night is the fifth.
        const int l = night ? 4 : (loop / 2) & 3;
        static const uint8_t FAR[5][3]  = { { 34, 36, 78 }, { 70, 38, 30 }, { 22, 56, 60 }, { 60, 28, 72 }, { 14, 18, 42 } };
        static const uint8_t NEAR[5][3] = { { 20, 44, 40 }, { 44, 30, 22 }, { 18, 34, 56 }, { 40, 20, 44 }, { 8, 12, 26 } };
        const uint16_t farC  = rgb(FAR[l][0], FAR[l][1], FAR[l][2]);
        const uint16_t farTop = rgb(FAR[l][0] * 3 / 2, FAR[l][1] * 3 / 2, FAR[l][2] * 3 / 2);
        const uint16_t nearC = rgb(NEAR[l][0], NEAR[l][1], NEAR[l][2]);
        const uint16_t nearTop = rgb(NEAR[l][0] * 3 / 2, NEAR[l][1] * 3 / 2, NEAR[l][2] * 3 / 2);

        const int fo = (int)_farX, no = (int)_nearX;
        for (int x = 0; x < W; x++) {
            int fh = _far[(x + fo) & 255];
            int fy = FAR_HORIZON - fh;
            canvas.drawPixel(x, fy, farTop);
            canvas.drawFastVLine(x, fy + 1, NEAR_HORIZON - fy - 1, farC);   // down behind the hills
            int nh = _near[(x + no) & 255];
            int ny = NEAR_HORIZON - nh;
            canvas.drawPixel(x, ny, nearTop);
            canvas.drawFastVLine(x, ny + 1, nh - 1, nearC);
        }
    }

private:
    static const int STARS = 36;
    uint8_t _far[256], _near[256];
    uint8_t _starX[STARS], _starY[STARS];
    float   _stars = 0, _farX = 0, _nearX = 0;

    static uint16_t rgb(int r, int g, int b) {
        return (uint16_t)(((min(r, 255) & 0xF8) << 8) | ((min(g, 255) & 0xFC) << 3) | (min(b, 255) >> 3));
    }
};

#endif // RUNNER_BACKDROP_H
