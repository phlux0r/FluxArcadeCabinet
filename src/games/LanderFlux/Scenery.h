#ifndef LANDER_SCENERY_H
#define LANDER_SCENERY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

// =============================================================================
// LANDER FLUX — SCENERY
// Everything behind the play: a graded sky with stars (a few twinkle), a
// planet or moon hanging in it, a far ridge, and the ground's surface,
// layers, craters and pebbles. Purely looks: nothing here touches play.
//
// The world changes every LEVELS_PER_WORLD levels, cycling through WORLDS:
// each has its own sky, ridge, ground and rock colours and its own body in
// the sky. Each level lays its stars, ridge and craters out afresh.
//
// Layout comes from its own little generator seeded by the level and pad,
// not random(), so the game's random sequence (rocks, pad, the demo) is
// the same as without it.
// =============================================================================

class LanderScenery {
public:
    static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    enum SkyBody : uint8_t { RINGED, BANDED, SUN, TWIN_MOONS, EARTH };

    struct World {
        const char* name;
        uint16_t skyTop, skyLow;            // the sky's grade, top to horizon
        uint16_t ridge, ridgeTop;
        uint16_t rim, ground, deep, pebble; // surface, top layer, below, specks
        uint16_t rockFill, rockLit, rockShade;
        SkyBody  body;
        uint16_t bodyA, bodyB;              // the body's two colours
    };

    static const int WORLDS = 5;
    static const int LEVELS_PER_WORLD = 4;

    static const World& worldFor(int level) {
        static const World W[WORLDS] = {
            { "DUST WORLD",
              rgb(8, 8, 40),   rgb(70, 30, 90),   rgb(55, 28, 70),  rgb(85, 50, 100),
              rgb(230, 160, 70), rgb(150, 96, 0),  rgb(95, 55, 10),  rgb(200, 140, 60),
              rgb(105, 95, 90),  rgb(225, 215, 205), rgb(55, 45, 45),
              RINGED, rgb(230, 200, 140), rgb(170, 140, 100) },
            { "ICE WORLD",
              rgb(0, 8, 30),   rgb(20, 70, 110),  rgb(30, 55, 95),  rgb(60, 95, 140),
              rgb(235, 250, 255), rgb(150, 190, 225), rgb(90, 120, 165), rgb(255, 255, 255),
              rgb(70, 90, 125),  rgb(200, 230, 255), rgb(30, 40, 70),
              BANDED, rgb(240, 160, 80), rgb(180, 90, 50) },
            { "EMBER WORLD",
              rgb(20, 0, 12),  rgb(110, 22, 22),  rgb(60, 12, 18),  rgb(95, 25, 25),
              rgb(255, 150, 40), rgb(95, 32, 20),  rgb(50, 16, 10),  rgb(255, 110, 20),
              rgb(75, 45, 45),   rgb(255, 160, 90), rgb(35, 15, 15),
              SUN, rgb(255, 245, 170), rgb(255, 170, 40) },
            { "JADE WORLD",
              rgb(10, 0, 40),  rgb(70, 25, 115),  rgb(40, 30, 90),  rgb(70, 55, 130),
              rgb(130, 255, 170), rgb(30, 140, 95), rgb(15, 80, 60),  rgb(90, 220, 140),
              rgb(95, 75, 125),  rgb(225, 190, 255), rgb(40, 30, 60),
              TWIN_MOONS, rgb(255, 180, 210), rgb(230, 230, 255) },
            { "MOON BASE",
              rgb(0, 0, 0),    rgb(12, 14, 45),   rgb(40, 40, 52),  rgb(70, 70, 85),
              rgb(215, 215, 215), rgb(125, 125, 130), rgb(72, 72, 78), rgb(180, 180, 185),
              rgb(95, 88, 82),   rgb(215, 210, 200), rgb(45, 42, 40),
              EARTH, rgb(40, 110, 230), rgb(60, 190, 80) },
        };
        return W[((max(level, 1) - 1) / LEVELS_PER_WORLD) % WORLDS];
    }

    // Lays out this level's sky and ground details; the body in the sky
    // goes on the side away from rockX, the top rock's x (-1 for none).
    void generate(int level, int padX, int rockX) {
        _world = &worldFor(level);
        _seed = 0x9E3779B9u ^ (uint32_t)(level * 7919) ^ (uint32_t)(padX * 104729);

        for (int i = 0; i < STARS; i++) {
            _starX[i] = (uint8_t)roll(W);
            _starY[i] = (uint8_t)roll(RIDGE_HIGH);
            _starKind[i] = (uint8_t)roll(8);     // 0-4 dim, 5-6 bright, 7 twinkles
        }
        for (int i = 0; i < RIDGE_POINTS; i++)
            _ridgeY[i] = (int16_t)(RIDGE_HIGH + roll(RIDGE_LOW - RIDGE_HIGH));

        // The body: just under the HUD, on the other side from the top
        // rock (it can reach up to y 30).
        _bodyR = 6 + roll(3);
        const bool right = rockX < 0 ? roll(2) : rockX < W / 2;
        _bodyX = right ? (int16_t)(W - 18 - roll(12)) : (int16_t)(18 + roll(12));
        _bodyY = (int16_t)(31 + roll(4));

        for (int i = 0; i < CRATERS; i++) {
            _craterX[i] = (uint8_t)roll(W);
            _craterD[i] = (uint8_t)(3 + roll(4));
        }
        for (int i = 0; i < PEBBLES; i++) {
            _pebbleX[i] = (uint8_t)roll(W);
            _pebbleD[i] = (uint8_t)(2 + roll(8));
        }
    }

    const World& world() const { return *_world; }

    // Sky, stars, the body and the far ridge: the frame's first drawing,
    // in place of clearing it.
    void renderBack(GFXcanvas16 &canvas) const {
        const World& w = *_world;
        for (int y = 0; y < H; y++)
            canvas.drawFastHLine(0, y, W, blend(w.skyTop, w.skyLow, y * 255 / (H - 1)));

        const unsigned long now = millis();
        for (int i = 0; i < STARS; i++) {
            uint16_t c = STAR_DIM;
            if (_starKind[i] >= 5) c = STAR_BRIGHT;
            if (_starKind[i] == 7)               // twinkles: each in its own beat
                c = ((now / 180 + i * 3) % 7 == 0) ? STAR_DIM
                  : ((now / 180 + i * 3) % 7 == 1) ? ArcadeConfig::COLOR_WHITE : STAR_BRIGHT;
            canvas.drawPixel(_starX[i], _starY[i], c);
        }

        renderBody(canvas);

        // The ridge, joined point to point, lighter along its top.
        for (int x = 0; x < W; x++) {
            const int y = ridgeAt(x);
            canvas.drawFastVLine(x, y, H - y, w.ridge);
            canvas.drawPixel(x, y, w.ridgeTop);
        }
    }

    // The ground under the line through groundY[] (n points, stepX apart).
    void renderGround(GFXcanvas16 &canvas, const int* groundY, int n, int stepX) const {
        const World& w = *_world;
        for (int x = 0; x < W; x++) {
            const int y = surfaceAt(groundY, n, stepX, x);
            canvas.drawFastVLine(x, y, H - y, w.deep);
            canvas.drawFastVLine(x, y + 1, 4, w.ground);
            canvas.drawPixel(x, y, w.rim);
        }
        // Craters: a dark dip with a lit lip under it.
        for (int i = 0; i < CRATERS; i++) {
            const int x = _craterX[i], y = surfaceAt(groundY, n, stepX, x) + _craterD[i];
            canvas.drawFastHLine(x - 1, y, 4, w.deep);
            canvas.drawFastHLine(x, y + 1, 2, w.rim);
        }
        for (int i = 0; i < PEBBLES; i++) {
            const int x = _pebbleX[i];
            canvas.drawPixel(x, surfaceAt(groundY, n, stepX, x) + _pebbleD[i], w.pebble);
        }
    }

    // The pad's masts, either end, beacons blinking in turn.
    void renderPadBeacons(GFXcanvas16 &canvas, int padX, int padWidth, int padY) const {
        const bool phase = (millis() / 400) % 2;
        const int xs[2] = { padX - 3, padX + padWidth + 2 };
        for (int i = 0; i < 2; i++) {
            canvas.drawFastVLine(xs[i], padY - 5, 5, ArcadeConfig::COLOR_GREY);
            canvas.drawPixel(xs[i], padY - 6,
                (phase == (i == 0)) ? ArcadeConfig::COLOR_RED : rgb(90, 0, 0));
        }
    }

    static int surfaceAt(const int* groundY, int n, int stepX, int x) {
        int seg = x / stepX;
        if (seg >= n - 1) seg = n - 2;
        const float p = (float)(x - seg * stepX) / (float)stepX;
        return groundY[seg] + (int)(p * (groundY[seg + 1] - groundY[seg]));
    }

private:
    static const int W = ArcadeConfig::PORTRAIT_WIDTH;
    static const int H = ArcadeConfig::PORTRAIT_HEIGHT;
    static const int STARS = 40;
    static const int RIDGE_POINTS = 9;           // 16px apart
    static const int RIDGE_HIGH = 112, RIDGE_LOW = 136;   // the ridge's top, range
    static const int CRATERS = 5, PEBBLES = 14;
    static const uint16_t STAR_DIM    = 0x632C;  // grey-blue
    static const uint16_t STAR_BRIGHT = 0xBDF7;

    const World* _world = &worldFor(1);
    uint32_t _seed = 1;
    uint8_t _starX[STARS], _starY[STARS], _starKind[STARS];
    int16_t _ridgeY[RIDGE_POINTS];
    int16_t _bodyX = 20, _bodyY = 40;
    int     _bodyR = 7;
    uint8_t _craterX[CRATERS], _craterD[CRATERS];
    uint8_t _pebbleX[PEBBLES], _pebbleD[PEBBLES];

    int roll(int n) {
        _seed = _seed * 1664525u + 1013904223u;
        return (int)((_seed >> 8) % (uint32_t)n);
    }

    int ridgeAt(int x) const {
        const int step = W / (RIDGE_POINTS - 1);
        int seg = x / step;
        if (seg >= RIDGE_POINTS - 1) seg = RIDGE_POINTS - 2;
        return _ridgeY[seg] + (_ridgeY[seg + 1] - _ridgeY[seg]) * (x - seg * step) / step;
    }

    // a to b by t (0-255), channel by channel.
    static uint16_t blend(uint16_t a, uint16_t b, int t) {
        const int r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
        const int g = (((a >> 5) & 63) * (255 - t) + ((b >> 5) & 63) * t) / 255;
        const int bl = ((a & 31) * (255 - t) + (b & 31) * t) / 255;
        return (uint16_t)((r << 11) | (g << 5) | bl);
    }

    // A disc drawn row by row: colour by row (bands) or lit/shaded by a
    // second disc offset towards the bottom right (a crescent's shadow).
    void disc(GFXcanvas16 &canvas, int cx, int cy, int r, uint16_t lit, uint16_t dark,
              bool banded) const {
        for (int dy = -r; dy <= r; dy++) {
            const int half = (int)sqrtf((float)(r * r - dy * dy));
            if (banded) {
                canvas.drawFastHLine(cx - half, cy + dy, 2 * half + 1,
                                     (((dy + r) / 3) % 2) ? dark : lit);
                continue;
            }
            canvas.drawFastHLine(cx - half, cy + dy, 2 * half + 1, lit);
            // The shadow disc, r/2 to the lower right, clipped to this one.
            const int sy = dy - r / 2;
            if (sy * sy <= r * r) {
                const int sh = (int)sqrtf((float)(r * r - sy * sy));
                const int from = max(cx - half, cx + r / 2 - sh);
                const int to   = min(cx + half, cx + r / 2 + sh);
                if (to >= from) canvas.drawFastHLine(from, cy + dy, to - from + 1, dark);
            }
        }
    }

    void renderBody(GFXcanvas16 &canvas) const {
        const World& w = *_world;
        const int x = _bodyX, y = _bodyY, r = _bodyR;
        switch (w.body) {
        case RINGED: {
            // The ring's far half behind the planet, its near half in front.
            const float tilt = 0.35f;
            for (int pass = 0; pass < 2; pass++) {
                if (pass == 1) disc(canvas, x, y, r, w.bodyA, blend(w.bodyA, w.skyTop, 110), false);
                for (int a = 0; a < 64; a++) {
                    const float t = a * (2.0f * PI / 64.0f);
                    const bool near = sinf(t) > 0.0f;
                    if (near != (pass == 1)) continue;
                    const float rx = cosf(t) * (r + 6), ry = sinf(t) * (r + 6) * tilt;
                    canvas.drawPixel(x + (int)(rx * 0.94f - ry * 0.34f),
                                     y + (int)(rx * 0.34f + ry * 0.94f), w.bodyB);
                }
            }
            break;
        }
        case BANDED:
            disc(canvas, x, y, r + 2, w.bodyA, w.bodyB, true);
            break;
        case SUN:
            disc(canvas, x, y, r + 3, blend(w.bodyB, w.skyTop, 140), blend(w.bodyB, w.skyTop, 140), true);
            disc(canvas, x, y, r + 1, w.bodyB, w.bodyB, true);
            disc(canvas, x, y, r - 1, w.bodyA, w.bodyA, true);
            break;
        case TWIN_MOONS:
            disc(canvas, x, y, r - 1, w.bodyA, blend(w.bodyA, w.skyTop, 120), false);
            disc(canvas, x + (x < W / 2 ? 14 : -14), y + 9, 3, w.bodyB, blend(w.bodyB, w.skyTop, 120), false);
            break;
        case EARTH:
            disc(canvas, x, y, r, w.bodyA, blend(w.bodyA, w.skyTop, 130), false);
            // Two continents, well inside the disc.
            canvas.fillCircle(x - r / 3, y - r / 3, r / 3, w.bodyB);
            canvas.fillCircle(x + r / 3, y + r / 3, r / 4, w.bodyB);
            canvas.drawPixel(x - r / 3 + 2, y, w.bodyB);
            break;
        }
    }
};

#endif // LANDER_SCENERY_H
