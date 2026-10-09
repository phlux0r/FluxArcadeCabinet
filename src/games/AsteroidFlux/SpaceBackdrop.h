#ifndef SPACE_BACKDROP_H
#define SPACE_BACKDROP_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

// =============================================================================
// ASTEROID FLUX — SPACE BACKDROP
// Everything behind the asteroids, all scrolling left at its own depth:
//   - a nebula, soft dithered cloud, drawn from a density map laid out once
//     (half resolution, two screens wide, wrapping) and written straight
//     into the canvas row by row: it also clears the playfield;
//   - stars at three depths: slow dim ones, middling ones, fast bright
//     ones, a few twinkling;
//   - now and then one distant object drifting slowly across: a ringed
//     planet, a moon, a spiral galaxy, a turning space station, a tumbling
//     satellite or a pulsar. Dim, so none reads as something to dodge.
// The nebula's colours change every SECTOR_POINTS points, fading across.
//
// Layout and timing come from its own little generator, not random(), so
// the game's random sequence (the asteroids, power-ups, demo) doesn't
// depend on the scenery.
// =============================================================================

class SpaceBackdrop {
public:
    static const int SECTOR_POINTS = 600;
    static const int SECTORS = 5;

    SpaceBackdrop() { reset(1); }

    // A new game (or demo): a fresh nebula and field, the first sector.
    void reset(uint32_t seed) {
        _seed = 0x2545F491u ^ (seed * 2654435761u);
        layNebula();
        for (int i = 0; i < STARS; i++) placeStar(i, (float)roll(W));
        _nebX = 0.0f;
        _sector = _fromSector = 0;
        _fade = 255;
        _obj.active = false;
        _nextObjFrames = 90 + roll(150);      // the first soon, 3-8s in
        _frame = 0;
    }

    // One frame on: everything drifts; the sector follows the score.
    void update(int score) {
        ++_frame;
        _nebX += NEBULA_SPEED;
        if (_nebX >= NW * 2) _nebX -= NW * 2;

        for (int i = 0; i < STARS; i++) {
            _starX[i] -= LAYER_SPEED[layerOf(i)];
            if (_starX[i] < 0.0f) placeStar(i, W - 1 + (float)roll(8));
        }

        if (_obj.active) {
            _obj.x -= _obj.speed;
            if (_obj.x < -OBJ_REACH) {
                _obj.active = false;
                _nextObjFrames = 300 + roll(600);   // 10-30s till the next
            }
        } else if (--_nextObjFrames <= 0) {
            spawnObject();
        }

        const int sector = (score / SECTOR_POINTS) % SECTORS;
        if (sector != _sector) {
            _fromSector = _sector;
            _sector = sector;
            _fade = 0;
        }
        if (_fade < 255) _fade = min(255, _fade + FADE_STEP);
    }

    // The whole playfield (below the HUD), in place of clearing it.
    void render(GFXcanvas16 &canvas) {
        uint16_t pal[5];
        palette(pal);
        renderNebula(canvas, pal);
        for (int i = 0; i < STARS; i++) if (layerOf(i) == 0) drawStar(canvas, i);
        if (_obj.active) renderObject(canvas);
        for (int i = 0; i < STARS; i++) if (layerOf(i) != 0) drawStar(canvas, i);
    }

    int sector() const { return _sector; }
    bool objectActive() const { return _obj.active; }

private:
    static const int W = ArcadeConfig::SCREEN_WIDTH;
    static const int TOP = ArcadeConfig::UI_MARGIN_TOP;   // first row below the HUD line
    static const int H = ArcadeConfig::SCREEN_HEIGHT;
    // The nebula's map: a cell is 2x2 pixels, NW cells across two screens.
    static const int NW = W;
    static const int NH = (H - TOP + 1) / 2;
    static constexpr float NEBULA_SPEED = 0.08f;   // px a frame
    static const int FADE_STEP = 4;                // ~2s from one sector's colours to the next

    static const int STARS = 30;                   // 0-13 far, 14-23 mid, 24-29 near
    static constexpr float LAYER_SPEED[3] = { 0.12f, 0.3f, 0.65f };

    static const int OBJ_REACH = 24;               // furthest any object draws from its centre

    enum Kind : uint8_t { RINGED, MOON, GALAXY, STATION, SATELLITE, PULSAR, KINDS };
    struct Distant {
        bool active;
        Kind kind;
        float x, speed;
        int y;
        uint8_t size;
    };

    uint8_t  _neb[NH][NW];                         // density 0-8
    float    _starX[STARS];
    uint8_t  _starY[STARS];
    uint8_t  _twinkle[STARS];                      // 0: steady, else its beat
    Distant  _obj;
    int      _nextObjFrames = 0;
    float    _nebX = 0.0f;
    int      _sector = 0, _fromSector = 0, _fade = 255;
    uint32_t _seed = 1, _frame = 0;

    static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    // a to b by t (0-255), channel by channel.
    static uint16_t blend(uint16_t a, uint16_t b, int t) {
        const int r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
        const int g = (((a >> 5) & 63) * (255 - t) + ((b >> 5) & 63) * t) / 255;
        const int bl = ((a & 31) * (255 - t) + (b & 31) * t) / 255;
        return (uint16_t)((r << 11) | (g << 5) | bl);
    }

    int roll(int n) {
        _seed = _seed * 1664525u + 1013904223u;
        return (int)((_seed >> 8) % (uint32_t)n);
    }

    static int layerOf(int i) { return i < 14 ? 0 : i < 24 ? 1 : 2; }

    void placeStar(int i, float x) {
        _starX[i] = x;
        _starY[i] = (uint8_t)(TOP + 1 + roll(H - TOP - 2));
        _twinkle[i] = roll(6) == 0 ? (uint8_t)(3 + roll(5)) : 0;
    }

    void drawStar(GFXcanvas16 &canvas, int i) {
        static const uint16_t SHADE[3] = { 0x4208, 0x8C51, 0xFFFF };   // far, mid, near
        uint16_t c = SHADE[layerOf(i)];
        if (_twinkle[i] && ((_frame / 6 + i) % _twinkle[i]) == 0) c = 0x2104;
        canvas.drawPixel((int)_starX[i], _starY[i], c);
    }

    // Soft clouds: a few wide blobs, wrapping across the map's two screens,
    // their overlaps densest, with a little grain.
    void layNebula() {
        static const int MAX_BLOBS = 11;
        float bx[MAX_BLOBS], by[MAX_BLOBS], brx[MAX_BLOBS], bry[MAX_BLOBS], bw[MAX_BLOBS];
        const int blobs = 6 + roll(MAX_BLOBS - 5);
        for (int b = 0; b < blobs; b++) {
            bx[b] = (float)roll(NW);   by[b] = (float)roll(NH);
            brx[b] = 6.0f + roll(24); bry[b] = 4.0f + roll(12);
            bw[b] = 0.5f + roll(60) / 100.0f;
        }
        for (int y = 0; y < NH; y++)
            for (int x = 0; x < NW; x++) {
                float v = 0.0f;
                for (int b = 0; b < blobs; b++) {
                    const float dy = (y - by[b]) / bry[b];
                    if (dy * dy >= 1.0f) continue;
                    float dx = fabsf(x - bx[b]);
                    if (dx > NW / 2) dx = NW - dx;
                    dx /= brx[b];
                    const float f = 1.0f - (dx * dx + dy * dy);
                    if (f > 0.0f) v += bw[b] * f * f;
                }
                v = v * 8.5f + (roll(100) - 50) * 0.014f;
                _neb[y][x] = (uint8_t)constrain((int)v, 0, 8);
            }
    }

    // The nebula's four shades for the current sector, part way from the
    // last sector's while it fades across. [0] is empty space.
    void palette(uint16_t pal[5]) const {
        static const uint16_t SECTOR_A[SECTORS] = {
            rgb(70, 22, 100), rgb(12, 62, 90), rgb(100, 28, 16), rgb(12, 74, 54), rgb(110, 30, 62) };
        static const uint16_t SECTOR_B[SECTORS] = {
            rgb(150, 44, 130), rgb(44, 120, 160), rgb(170, 76, 22), rgb(56, 140, 96), rgb(200, 130, 60) };
        auto shades = [](int s, uint16_t out[5]) {
            const uint16_t a = SECTOR_A[s], b = SECTOR_B[s];
            out[0] = 0;
            out[1] = blend(0, a, 80);
            out[2] = blend(0, a, 150);
            out[3] = blend(0, blend(a, b, 128), 185);
            out[4] = blend(0, b, 210);
        };
        shades(_sector, pal);
        if (_fade < 255) {
            uint16_t from[5];
            shades(_fromSector, from);
            for (int k = 1; k < 5; k++) pal[k] = blend(from[k], pal[k], _fade);
        }
    }

    // Row by row into the buffer: each cell 2x2, an odd density dithered
    // between its two shades on a checkerboard.
    void renderNebula(GFXcanvas16 &canvas, const uint16_t pal[5]) const {
        uint16_t* buf = canvas.getBuffer();
        const int off = (int)_nebX;                    // 0 to 2W-1, in pixels
        for (int y = TOP; y < H; y++) {
            const uint8_t* row = _neb[(y - TOP) >> 1];
            uint16_t* out = buf + y * W;
            int px = off;
            for (int x = 0; x < W; x++, px++) {
                if (px >= 2 * NW) px -= 2 * NW;
                const uint8_t d = row[px >> 1];
                int level = d >> 1;
                if ((d & 1) && ((x ^ y) & 1)) level++;
                out[x] = pal[min(level, 4)];
            }
        }
    }

    void spawnObject() {
        _obj.active = true;
        _obj.kind = (Kind)roll(KINDS);
        _obj.x = (float)(W + OBJ_REACH);
        _obj.y = TOP + 18 + roll(H - TOP - 36);
        _obj.speed = 0.18f + roll(10) * 0.01f;   // 20-30s across
        _obj.size = (uint8_t)(5 + roll(4));
    }

    // A shaded disc: lit, with a crescent of shadow to the lower right.
    static void disc(GFXcanvas16 &canvas, int cx, int cy, int r, uint16_t lit, uint16_t dark) {
        for (int dy = -r; dy <= r; dy++) {
            const int half = (int)sqrtf((float)(r * r - dy * dy));
            canvas.drawFastHLine(cx - half, cy + dy, 2 * half + 1, lit);
            const int sy = dy - r / 2;
            if (sy * sy <= r * r) {
                const int sh = (int)sqrtf((float)(r * r - sy * sy));
                const int from = max(cx - half, cx + r / 2 - sh);
                const int to   = min(cx + half, cx + r / 2 + sh);
                if (to >= from) canvas.drawFastHLine(from, cy + dy, to - from + 1, dark);
            }
        }
    }

    static void plot(GFXcanvas16 &canvas, int x, int y, uint16_t c) {
        if (y > TOP && y < H) canvas.drawPixel(x, y, c);   // never over the HUD line
    }

    void renderObject(GFXcanvas16 &canvas) const {
        const int x = (int)_obj.x, y = _obj.y, r = _obj.size;
        const float t = _frame * 0.02f;
        switch (_obj.kind) {
        case RINGED: {
            const uint16_t body = rgb(110, 88, 60), ring = rgb(90, 76, 56);
            for (int pass = 0; pass < 2; pass++) {
                if (pass == 1) disc(canvas, x, y, r, body, rgb(55, 42, 30));
                for (int a = 0; a < 56; a++) {
                    const float th = a * (2.0f * PI / 56.0f);
                    if ((sinf(th) > 0.0f) != (pass == 1)) continue;
                    const float rx = cosf(th) * (r + 5), ry = sinf(th) * (r + 5) * 0.3f;
                    plot(canvas, x + (int)(rx * 0.95f - ry * 0.3f), y + (int)(rx * 0.3f + ry * 0.95f), ring);
                }
            }
            break;
        }
        case MOON:
            disc(canvas, x, y, r - 1, rgb(105, 105, 110), rgb(48, 48, 56));
            plot(canvas, x - 2, y - 1, rgb(70, 70, 76));
            plot(canvas, x + 1, y - 3, rgb(70, 70, 76));
            plot(canvas, x - 1, y + 2, rgb(70, 70, 76));
            break;
        case GALAXY: {
            // Two arms wound round a core, tilted, fading outwards.
            for (int arm = 0; arm < 2; arm++)
                for (int k = 2; k < 34; k++) {
                    const float a = k * 0.32f + arm * PI;
                    const float rad = 0.55f * k;
                    const int g = 150 - k * 3;
                    plot(canvas, x + (int)(cosf(a) * rad), y + (int)(sinf(a) * rad * 0.45f),
                         rgb(g, g - 20, g + 30 > 255 ? 255 : g + 30));
                }
            canvas.fillRect(x - 1, y - 1, 3, 2, rgb(200, 190, 230));
            break;
        }
        case STATION: {
            // A wheel turning slowly on its hub, a light blinking on the rim.
            const uint16_t hull = rgb(110, 115, 125);
            canvas.drawCircle(x, y, r + 2, hull);
            for (int s = 0; s < 4; s++) {
                const float a = t * 0.6f + s * (PI / 2.0f);
                canvas.drawLine(x, y, x + (int)(cosf(a) * (r + 2)), y + (int)(sinf(a) * (r + 2)), rgb(70, 74, 82));
            }
            canvas.fillRect(x - 1, y - 1, 3, 3, rgb(150, 150, 160));
            if ((_frame / 20) % 2) {
                const float a = t * 0.6f;
                plot(canvas, x + (int)(cosf(a) * (r + 2)), y + (int)(sinf(a) * (r + 2)), rgb(255, 60, 60));
            }
            break;
        }
        case SATELLITE: {
            // A small body with two solar panels, tumbling.
            const float a = t * 0.9f;
            const float c = cosf(a), s = sinf(a);
            for (int side = -1; side <= 1; side += 2)
                for (int k = 2; k <= 7; k++) {
                    const float px = c * k * side, py = s * k * side;
                    plot(canvas, x + (int)(px - s), y + (int)(py + c), rgb(40, 70, 140));
                    plot(canvas, x + (int)px, y + (int)py, rgb(60, 100, 180));
                }
            canvas.fillRect(x - 1, y - 1, 3, 3, rgb(170, 140, 60));
            plot(canvas, x + (int)(-s * 3), y + (int)(c * 3), rgb(200, 200, 200));
            break;
        }
        case PULSAR: {
            // A bright point whose beams sweep round, flashing.
            const uint16_t core = rgb(220, 235, 255), beam = rgb(110, 135, 200);
            const bool flash = (_frame / 4) % 6 == 0;
            const float a = t * 2.5f;
            canvas.drawCircle(x, y, 2, rgb(50, 60, 100));
            for (int k = 2; k <= (flash ? 13 : 9); k++)
                for (int side = -1; side <= 1; side += 2)
                    plot(canvas, x + (int)(cosf(a) * k * side), y + (int)(sinf(a) * k * side * 0.5f), beam);
            plot(canvas, x, y, core);
            if (flash) { plot(canvas, x - 1, y, core); plot(canvas, x + 1, y, core);
                         plot(canvas, x, y - 1, core); plot(canvas, x, y + 1, core); }
            break;
        }
        default: break;
        }
    }
};

constexpr float SpaceBackdrop::LAYER_SPEED[3];

#endif // SPACE_BACKDROP_H
