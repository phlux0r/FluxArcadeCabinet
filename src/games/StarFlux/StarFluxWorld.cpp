#include "StarFluxGame.h"

// Stages 2 and 3: the planet's surface and the station trench. Their
// backdrops are filled straight into the canvas, pixel by pixel, from
// Jet's own projection (as Tube Flux draws its tunnel): a ray per pixel
// against the ground plane, or the trench's floor and walls. Obstacles
// (pillars, turret towers, barriers) are lit Jet boxes; laser gates are
// drawn in 2D and blink; turrets sit on towers and shoot at you.

namespace starflux {

namespace {

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t mix565(uint16_t a, uint16_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((ar + (int)((br - ar) * t)) << 11) |
                      ((ag + (int)((bg - ag) * t)) << 5) |
                       (ab + (int)((bb - ab) * t)));
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// Fog: each base colour faded towards the horizon in FOG_STEPS, by depth.
constexpr int   FOG_STEPS = 8;
constexpr float FOG_FAR   = 9000.0f;
struct Fogged { uint16_t c[FOG_STEPS]; };
Fogged fogged(uint16_t base, uint16_t horizon) {
    Fogged f;
    for (int i = 0; i < FOG_STEPS; ++i) f.c[i] = mix565(base, horizon, (float)i / (float)(FOG_STEPS - 1));
    return f;
}
inline int fogStep(float z) {
    int i = (int)(z * ((float)FOG_STEPS / FOG_FAR));
    return i < 0 ? 0 : i >= FOG_STEPS ? FOG_STEPS - 1 : i;
}

// A box of 6 faces with 4 vertices each (FLAT lighting needs every face's
// own vertices), wound to face outwards as the rocks are.
void addBox(Renderer::Object* o, float cx, float cy, float cz, float w, float h, float d, Renderer::Material* m) {
    static const int8_t F[6][4][3] = {
        { { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { 1, -1, 1 } },
        { { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, -1 } },
        { { -1, 1, -1 }, { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 } },
        { { -1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 } },
        { { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }, { -1, -1, 1 } },
        { { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 }, { 1, -1, -1 } },
    };
    static const int8_t N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int f = 0; f < 6; ++f) {
        float p[4][3];
        for (int k = 0; k < 4; ++k) {
            p[k][0] = cx + F[f][k][0] * w * 0.5f;
            p[k][1] = cy + F[f][k][1] * h * 0.5f;
            p[k][2] = cz + F[f][k][2] * d * 0.5f;
        }
        float ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1], uz = p[1][2] - p[0][2];
        float vx = p[2][0] - p[0][0], vy = p[2][1] - p[0][1], vz = p[2][2] - p[0][2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        bool out = nx * N[f][0] + ny * N[f][1] + nz * N[f][2] > 0;
        if (ROCK_WINDING_FLIP) out = !out;
        uint16_t idx[4];
        for (int k = 0; k < 4; ++k) {
            const float* q = p[out ? k : 3 - k];
            idx[k] = (uint16_t)o->vertices.size();
            o->addVertex({ { (int32_t)lroundf(q[0]), (int32_t)lroundf(q[1]), (int32_t)lroundf(q[2]) }, { 0, 0 },
                           { 0, FIXED_POINT_SCALE, 0 } });
        }
        o->addTriangle(idx[0], idx[1], idx[2], m);
        o->addTriangle(idx[0], idx[2], idx[3], m);
    }
}

Renderer::Object* finishSolid(Renderer::Object* o) {
    o->computeFlatNormals();
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::CULL_BACKFACES;
    o->enabled = false;
    return o;
}

// Resizes a single-box object (built centred on its origin) in place.
void setBoxSize(Renderer::Object* o, float w, float h, float d) {
    for (auto &v : o->vertices) {
        v.position.x = (int32_t)lroundf((v.position.x > 0 ? 0.5f : -0.5f) * w);
        v.position.y = (int32_t)lroundf((v.position.y > 0 ? 0.5f : -0.5f) * h);
        v.position.z = (int32_t)lroundf((v.position.z > 0 ? 0.5f : -0.5f) * d);
    }
    o->invalidatePositions();
    o->calculateBoundingBox();
}

}  // namespace

const char* StarFluxGame::stageName() const {
    static const char* const names[STAGE_COUNT] = { "AURORA BELT", "EMBER REACH", "TRENCH RUN" };
    return names[_stageNum];
}

float StarFluxGame::floorY() const {
    switch (_stageNum) {
        case STAGE_PLANET: return GROUND_Y;
        case STAGE_TRENCH: return TRENCH_FLOOR;
        default:           return -1e9f;
    }
}

// Space dust: everywhere in space; none on the planet (it's sky and
// ground); in the trench only where the walls don't hide it.
bool StarFluxGame::starVisible(float x, float y) const {
    switch (_stageNum) {
        case STAGE_PLANET: return false;
        case STAGE_TRENCH: return y > TRENCH_TOP || fabsf(x) < TRENCH_HALF_W;
        default:           return true;
    }
}

// Obstacle and turret colours to suit the stage: red rock on the planet,
// steel and red lights in the trench.
void StarFluxGame::applyStagePalette() {
    if (_stageNum == STAGE_TRENCH) {
        _boxMat.color = rgb(196, 156, 70);
        _turretMat.color = rgb(255, 70, 60);
        _turretMat2.color = rgb(140, 30, 30);
    } else {
        _boxMat.color = rgb(176, 96, 64);
        _turretMat.color = rgb(255, 190, 60);
        _turretMat2.color = rgb(150, 90, 20);
    }
}

// A ridge of hills round the horizon, heights 0-255, smooth enough to
// read as mountains: a few summed sines with a fixed seed's phases.
void StarFluxGame::buildMountains() {
    for (int i = 0; i < 64; ++i) {
        float a = (float)i * (2.0f * PI / 64.0f);
        float h = 0.55f + 0.25f * sinf(a * 2.0f + 1.3f) + 0.15f * sinf(a * 5.0f + 0.4f) + 0.08f * sinf(a * 11.0f + 2.1f);
        _mountains[i] = (uint8_t)(clampf(h, 0.0f, 1.0f) * 255.0f);
    }
}

// The planet: sky by height above the horizon, a sun, a mountain ridge,
// and a checkerboard ground fading into the haze. Each pixel's ray, in the
// camera's frame before its roll, is (dx, dy, 1): see project().
//
// This fills every pixel every frame, so it's done in spans of SPAN
// pixels. Where a whole span is ground, only its ends are worked out
// exactly (depth = height / dy); across it the square it's in steps in
// 16.16 fixed point, and the haze is the span's. Clear sky is a gradient
// stepped the same way. Near the horizon, the ridge and the sun, each
// pixel is worked out on its own.
void StarFluxGame::drawPlanet(GFXcanvas16 &canvas) {
    const int w = canvas.width(), h = canvas.height();
    uint16_t* buf = canvas.getBuffer();
    const float invF = 1.0f / _camera.fovFactor;
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float c = _rollCos, s = _rollSin;

    // Colour tables: constants, worked out on first use.
    const uint16_t horizon = rgb(255, 150, 90);
    static uint16_t skyLut[32];
    static Fogged groundA, groundB, ridge;
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 32; ++i) {
            float t = (float)i / 31.0f;
            skyLut[i] = t < 0.35f ? mix565(horizon, rgb(190, 70, 110), t / 0.35f)
                                  : mix565(rgb(190, 70, 110), rgb(30, 12, 60), (t - 0.35f) / 0.65f);
        }
        groundA = fogged(rgb(150, 70, 42), mix565(horizon, 0, 0.25f));
        groundB = fogged(rgb(104, 46, 32), mix565(horizon, 0, 0.25f));
        ridge = fogged(rgb(70, 30, 66), rgb(120, 60, 90));
        built = true;
    }
    const float drop = GROUND_Y - _camY;               // negative: the ground's below
    const float sunA = 0.45f, sunE = 0.16f, sunR = 0.055f;
    const float ridgeShift = _camX * 0.00004f;
    const float invCell = 1.0f / GROUND_CELL;
    const float RIDGE_MAX = 0.11f;                     // tan of the highest peak

    auto pixelAt = [&](float dx, float dy) -> uint16_t {
        if (dy < 0.0f) {
            const float z = drop / dy;
            const int ix = (int)((_camX + dx * z) * invCell + 4096.0f);   // biased: the cast floors
            const int iz = (int)((z + _groundScroll) * invCell);
            return (((ix + iz) & 1) ? groundA.c : groundB.c)[fogStep(z)];
        }
        float a = (dx + ridgeShift + 2.0f) * 10.0f;
        int i0 = (int)a;
        float fr = a - (float)i0;
        float hgt = ((float)_mountains[i0 & 63] * (1.0f - fr) + (float)_mountains[(i0 + 1) & 63] * fr) * (RIDGE_MAX / 255.0f);
        if (dy < hgt) return ridge.c[(int)((1.0f - dy / (hgt + 0.001f)) * (FOG_STEPS - 1))];
        float sd = (dx - sunA) * (dx - sunA) + (dy - sunE) * (dy - sunE);
        if (sd < sunR * sunR) return rgb(255, 240, 190);
        int si = (int)(dy * 60.0f);
        uint16_t sky = skyLut[si > 31 ? 31 : si];
        if (sd < sunR * sunR * 2.2f) sky = mix565(sky, rgb(255, 210, 150), 0.5f);
        return sky;
    };

    constexpr int SPAN = 16;
    const float sx = invF * c, sy = -invF * s;          // the ray's step per pixel
    for (int y = 0; y < h; ++y) {
        const float v = (cy - ((float)y + 0.5f)) * invF;
        uint16_t* row = buf + y * w;
        for (int x0 = 0; x0 < w; x0 += SPAN) {
            const int n = (x0 + SPAN <= w ? SPAN : w - x0);
            const float uA = ((float)x0 + 0.5f - cx) * invF;
            const float dxA = uA * c + v * s, dyA = -uA * s + v * c;
            const float dxB = dxA + sx * (float)(n - 1), dyB = dyA + sy * (float)(n - 1);
            uint16_t* out = row + x0;
            if (n > 1 && dyA < -0.03f && dyB < -0.03f) {
                // Ground: which square, stepped in 16.16.
                const float zA = drop / dyA, zB = drop / dyB;
                const int step = fogStep((zA + zB) * 0.5f);
                const uint16_t ca = groundA.c[step], cb = groundB.c[step];
                int32_t fx = (int32_t)(((_camX + dxA * zA) * invCell + 4096.0f) * 65536.0f);
                int32_t fz = (int32_t)((zA + _groundScroll) * invCell * 65536.0f);
                const int32_t gx = (int32_t)(((_camX + dxB * zB) * invCell + 4096.0f) * 65536.0f);
                const int32_t gz = (int32_t)((zB + _groundScroll) * invCell * 65536.0f);
                const int32_t stx = (gx - fx) / (n - 1), stz = (gz - fz) / (n - 1);
                for (int k = 0; k < n; ++k, fx += stx, fz += stz) out[k] = (((fx >> 16) + (fz >> 16)) & 1) ? ca : cb;
            } else if (n > 1 && dyA > RIDGE_MAX && dyB > RIDGE_MAX &&
                       (fminf(dxA, dxB) > sunA + sunR * 1.5f || fmaxf(dxA, dxB) < sunA - sunR * 1.5f ||
                        fminf(dyA, dyB) > sunE + sunR * 1.5f || fmaxf(dyA, dyB) < sunE - sunR * 1.5f)) {
                // Clear sky, away from the sun: the gradient, stepped.
                int32_t fi = (int32_t)(dyA * 60.0f * 65536.0f);
                const int32_t st = (int32_t)(sy * 60.0f * 65536.0f);
                for (int k = 0; k < n; ++k, fi += st) {
                    int si = fi >> 16;
                    out[k] = skyLut[si > 31 ? 31 : si < 0 ? 0 : si];
                }
            } else {
                float dx = dxA, dy = dyA;
                for (int k = 0; k < n; ++k, dx += sx, dy += sy) out[k] = pixelAt(dx, dy);
            }
        }
    }
}

// The trench: a floor with runway lights down the middle, walls of steel
// panels with orange lamps, and space above. Nearest of floor and walls
// wins; everything fades into the dark far off. Spans as drawPlanet():
// where a span is wholly floor or wholly one wall, its ends are worked out
// exactly and the pattern stepped across it in fixed point.
void StarFluxGame::drawTrench(GFXcanvas16 &canvas) {
    const int w = canvas.width(), h = canvas.height();
    uint16_t* buf = canvas.getBuffer();
    const float invF = 1.0f / _camera.fovFactor;
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float c = _rollCos, s = _rollSin;

    const uint16_t dark = rgb(8, 8, 22), space = rgb(4, 3, 16);
    // Colour tables: constants, worked out on first use.
    static Fogged floorA, floorB, lamp, wallL, wallR, seam, amber;
    static bool built = false;
    if (!built) {
        floorA = fogged(rgb(74, 80, 102), dark);
        floorB = fogged(rgb(56, 60, 80), dark);
        lamp   = fogged(rgb(90, 230, 255), dark);
        wallL  = fogged(rgb(104, 110, 136), dark);
        wallR  = fogged(rgb(66, 70, 92), dark);
        seam   = fogged(rgb(30, 32, 46), dark);
        amber  = fogged(rgb(255, 160, 60), dark);
        built = true;
    }
    const float floorDrop = TRENCH_FLOOR - _camY;
    const float rightGap = TRENCH_HALF_W - _camX, leftGap = -TRENCH_HALF_W - _camX;
    const int scroll = (int)_groundScroll;
    const int top = (int)(TRENCH_TOP - TRENCH_FLOOR);

    // Floor at depth z, across-position gx; wall at depth z, height wy
    // (above the floor). Patterns: powers of two, so integer masks.
    auto floorPx = [&](int gx, int gz, int step) -> uint16_t {
        if (gx > 4096 - 30 && gx < 4096 + 30 && (gz & 1023) < 130) return lamp.c[step];
        return ((((gx * 117) >> 15) + (gz >> 9)) & 1 ? floorA : floorB).c[step];   // gx/280
    };
    auto wallPx = [&](int gz, int wy, int step, bool right) -> uint16_t {
        if (wy > top) return space;
        const int pz = gz & 511, py = wy & 255;
        if (pz < 30 || py < 16) return seam.c[step];
        if (py > 180 && py < 212 && (gz & 2047) < 160) return amber.c[step];
        return (right ? wallR : wallL).c[step];
    };
    auto floorZ = [&](float dy) { return dy < 0.0f ? floorDrop / dy : 1e9f; };
    auto wallZ = [&](float dx) { return dx > 0.0f ? rightGap / dx : dx < 0.0f ? leftGap / dx : 1e9f; };
    auto pixelAt = [&](float dx, float dy) -> uint16_t {
        const float zf = floorZ(dy), zw = wallZ(dx);
        if (zf < zw) {
            if (zf > FOG_FAR) return dark;
            return floorPx((int)(_camX + dx * zf) + 4096, (int)zf + scroll, fogStep(zf));
        }
        if (zw > 1e8f) return space;
        const float wy = _camY + dy * zw - TRENCH_FLOOR;
        if (wy > (float)top) return space;
        if (zw > FOG_FAR) return dark;
        return wallPx((int)zw + scroll, (int)wy, fogStep(zw), dx > 0);
    };

    // A span of floor, its ends' depths known: squares and runway lights
    // stepped across in fixed point, the haze the span's.
    auto floorSpan = [&](uint16_t* out, int n, float zfA, float zfB, float dxA, float dxB) {
        const int step = fogStep((zfA + zfB) * 0.5f);
        const uint16_t ca = floorA.c[step], cb = floorB.c[step], cl = lamp.c[step];
        int32_t gx = (int32_t)((_camX + dxA * zfA + 4096.0f) * 256.0f);
        int32_t gz = (int32_t)((zfA + (float)scroll) * 256.0f);
        const int32_t sgx = ((int32_t)((_camX + dxB * zfB + 4096.0f) * 256.0f) - gx) / (n - 1);
        const int32_t sgz = ((int32_t)((zfB + (float)scroll) * 256.0f) - gz) / (n - 1);
        for (int k = 0; k < n; ++k, gx += sgx, gz += sgz) {
            const int x = gx >> 8, z = gz >> 8;
            if (x > 4096 - 30 && x < 4096 + 30 && (z & 1023) < 130) { out[k] = cl; continue; }
            out[k] = ((((x * 117) >> 15) + (z >> 9)) & 1) ? ca : cb;   // x * 117 >> 15: x / 280
        }
    };

    constexpr int SPAN = 16;
    const float sx = invF * c, sy = -invF * s;
    // Walls further off than the haze: rays this close to straight down the trench.
    const float farDx = (TRENCH_HALF_W - fabsf(_camX)) / FOG_FAR;
    // Straight down the trench, a ray this far above level clears the wall tops.
    const float clearDy = (TRENCH_TOP - _camY) / FOG_FAR;
    for (int y = 0; y < h; ++y) {
        const float v = (cy - ((float)y + 0.5f)) * invF;
        uint16_t* row = buf + y * w;
        for (int x0 = 0; x0 < w; x0 += SPAN) {
            const int n = (x0 + SPAN <= w ? SPAN : w - x0);
            const float uA = ((float)x0 + 0.5f - cx) * invF;
            const float dxA = uA * c + v * s, dyA = -uA * s + v * c;
            const float dxB = dxA + sx * (float)(n - 1), dyB = dyA + sy * (float)(n - 1);
            uint16_t* out = row + x0;
            const bool floorAll = dyA < -0.03f && dyB < -0.03f;
            // Down the middle: only the floor, or the dark far end, or space.
            if (n > 1 && fabsf(dxA) < farDx && fabsf(dxB) < farDx) {
                if (floorAll) {
                    const float zfA = floorZ(dyA), zfB = floorZ(dyB);
                    if (zfA < FOG_FAR && zfB < FOG_FAR) { floorSpan(out, n, zfA, zfB, dxA, dxB); continue; }
                } else if (dyA >= 0.0f && dyB >= 0.0f) {
                    float dy = dyA;
                    for (int k = 0; k < n; ++k, dy += sy) out[k] = dy < clearDy ? dark : space;
                    continue;
                }
            }
            const bool sameWall = (dxA > 0.03f && dxB > 0.03f) || (dxA < -0.03f && dxB < -0.03f);
            if (n > 1 && sameWall) {
                const float zfA = floorZ(dyA), zfB = floorZ(dyB), zwA = wallZ(dxA), zwB = wallZ(dxB);
                const bool floorBoth = floorAll && zfA < zwA && zfB < zwB;
                const bool wallBoth = zwA <= zfA && zwB <= zfB;
                if (floorBoth && zfA < FOG_FAR && zfB < FOG_FAR) { floorSpan(out, n, zfA, zfB, dxA, dxB); continue; }
                if (floorBoth && zfA >= FOG_FAR && zfB >= FOG_FAR) {
                    for (int k = 0; k < n; ++k) out[k] = dark;
                    continue;
                }
                if (wallBoth) {
                    // Wholly above the walls (space), or wholly lost in the dark?
                    const float wyA = _camY + dyA * zwA - TRENCH_FLOOR, wyB = _camY + dyB * zwB - TRENCH_FLOOR;
                    if (wyA > (float)top && wyB > (float)top) {
                        for (int k = 0; k < n; ++k) out[k] = space;
                        continue;
                    }
                    if (zwA >= FOG_FAR && zwB >= FOG_FAR && wyA <= (float)top && wyB <= (float)top) {
                        for (int k = 0; k < n; ++k) out[k] = dark;
                        continue;
                    }
                    if (zwA < FOG_FAR && zwB < FOG_FAR) {
                        // Panels, seams and lamps, stepped across in fixed point.
                        const int step = fogStep((zwA + zwB) * 0.5f);
                        const uint16_t cw = (dxA > 0 ? wallR : wallL).c[step], cs = seam.c[step], ca = amber.c[step];
                        int32_t gz = (int32_t)((zwA + (float)scroll) * 256.0f);
                        int32_t wy = (int32_t)(wyA * 256.0f);
                        const int32_t sgz = ((int32_t)((zwB + (float)scroll) * 256.0f) - gz) / (n - 1);
                        const int32_t swy = ((int32_t)(wyB * 256.0f) - wy) / (n - 1);
                        for (int k = 0; k < n; ++k, gz += sgz, wy += swy) {
                            const int z = gz >> 8, yy = wy >> 8;
                            if (yy > top) { out[k] = space; continue; }
                            const int py = yy & 255;
                            out[k] = ((z & 511) < 30 || py < 16) ? cs : (py > 180 && py < 212 && (z & 2047) < 160) ? ca : cw;
                        }
                        continue;
                    }
                }
            }
            float dx = dxA, dy = dyA;
            for (int k = 0; k < n; ++k, dx += sx, dy += sy) out[k] = pixelAt(dx, dy);
        }
    }
}

// The ship's shadow on the ground or the trench floor: the pixels under it
// at a quarter brightness, with a dotted drop line up to the ship, so how
// high you are over the ground (and short pillars and towers) reads at a
// glance.
void StarFluxGame::drawShadow(GFXcanvas16 &canvas) {
    if (_stageNum == STAGE_BELT || !_shipSprite.enabled) return;
    float sx, sy, tx, ty;
    if (!project(_shipX, floorY(), SHIP_Z, sx, sy)) return;
    const float half = 110.0f * pixelsPerUnit(SHIP_Z);
    const int w = canvas.width(), h = canvas.height();
    uint16_t* buf = canvas.getBuffer();
    for (int dy = -2; dy <= 2; ++dy) {
        int y = (int)sy + dy;
        if (y < 0 || y >= h) continue;
        int span = (int)(half * (1.0f - 0.18f * (float)(dy * dy)));
        for (int x = (int)sx - span; x <= (int)sx + span; ++x) {
            if (x < 0 || x >= w) continue;
            uint16_t &p = buf[y * w + x];
            p = (p >> 2) & 0x39E7;
        }
    }
    if (!project(_shipX, _shipY, SHIP_Z, tx, ty)) return;
    const int steps = (int)fabsf(sy - ty);
    for (int i = 3; i < steps; i += 3) {
        float t = (float)i / (float)steps;
        int x = (int)(sx + (tx - sx) * t), y = (int)(sy + (ty - sy) * t);
        if (x < 0 || x >= w || y < 0 || y >= h) continue;
        uint16_t &p = buf[y * w + x];
        p = (p >> 2) & 0x39E7;
    }
}

bool StarFluxGame::gateOn(const Box &b, unsigned long at) const {
    return (((at + b.phase) / GATE_MS) & 1) == 0;
}

// Laser gates: three red beams across the trench while on, a faint line
// where they'll be while off.
void StarFluxGame::drawGates(GFXcanvas16 &canvas) {
    for (const auto &b : _boxes) {
        if (!b.active || !b.gate || b.z < CAMERA_NEAR + 20) continue;
        const bool on = gateOn(b, millis());
        const float ys[3] = { b.y0, (b.y0 + b.y1) * 0.5f, b.y1 };
        for (int i = 0; i < 3; ++i) {
            if (!on && i == 1) continue;
            float x0, y0, x1, y1;
            if (!project(b.x0, ys[i], b.z, x0, y0) || !project(b.x1, ys[i], b.z, x1, y1)) continue;
            uint16_t col = on ? ((millis() / 60) & 1 ? rgb(255, 60, 60) : rgb(255, 170, 170)) : rgb(90, 20, 30);
            canvas.drawLine((int)x0, (int)y0, (int)x1, (int)y1, col);
            if (on) canvas.drawLine((int)x0, (int)y0 + 1, (int)x1, (int)y1 + 1, rgb(200, 30, 40));
        }
    }
}

// The reactor's shield fan, over its core: four steel blades and a gap.
void StarFluxGame::drawFan(GFXcanvas16 &canvas) {
    if (!_bossActive || _bossKind != STAGE_TRENCH || !coreOpen() || _coreHp <= 0) return;
    float cx, cy, cz;
    bossPartPos(2, cx, cy, cz);
    const float R = CORE_R * 1.7f, z = cz - 70.0f;
    float hx, hy;
    if (!project(cx, cy, z, hx, hy)) return;
    for (int k = (int)(FAN_GAP_DEG / 60.0f); k < 6; ++k) {
        for (int half = 0; half < 2; ++half) {
            float a0 = radians(_fanAngle + 60.0f * (float)k + 30.0f * (float)half);
            float a1 = a0 + radians(30.0f);
            float x0, y0, x1, y1;
            if (!project(cx + cosf(a0) * R, cy + sinf(a0) * R, z, x0, y0)) continue;
            if (!project(cx + cosf(a1) * R, cy + sinf(a1) * R, z, x1, y1)) continue;
            canvas.fillTriangle((int)hx, (int)hy, (int)x0, (int)y0, (int)x1, (int)y1,
                                (k & 1) ? rgb(150, 160, 190) : rgb(110, 118, 150));
        }
    }
    canvas.fillCircle((int)hx, (int)hy, 2, rgb(40, 44, 60));
}

// Obstacle slots are single boxes, resized for each use.
StarFluxGame::Box* StarFluxGame::spawnBox(float x0, float x1, float y0, float y1, float z, float depth) {
    for (auto &b : _boxes) {
        if (b.active || !b.obj) continue;
        b.active = true;
        b.hit = false;
        b.gate = false;
        b.x0 = x0; b.x1 = x1; b.y0 = y0; b.y1 = y1; b.z = z; b.depth = depth;
        setBoxSize(b.obj, x1 - x0, y1 - y0, depth);
        b.obj->setPosition((int32_t)((x0 + x1) * 0.5f), (int32_t)((y0 + y1) * 0.5f), (int32_t)z);
        b.obj->enabled = true;
        return &b;
    }
    return nullptr;
}

// A laser gate: a band of height across the whole trench, blinking.
StarFluxGame::Box* StarFluxGame::spawnGate(float y0, float y1, float z) {
    for (auto &b : _boxes) {
        if (b.active) continue;
        b.active = true;
        b.hit = false;
        b.gate = true;
        b.x0 = -TRENCH_HALF_W; b.x1 = TRENCH_HALF_W; b.y0 = y0; b.y1 = y1; b.z = z; b.depth = 60.0f;
        b.phase = (unsigned long)random(0, (long)(GATE_MS * 2));
        if (b.obj) b.obj->enabled = false;
        return &b;
    }
    return nullptr;
}

// A tower up to TOWER_TOP, low enough to fly over, with a turret on top
// (if one's free and wanted).
void StarFluxGame::spawnTower(float x, float z, bool withTurret) {
    if (!spawnBox(x - 100.0f, x + 100.0f, floorY(), TOWER_TOP, z, 200.0f)) return;
    if (!withTurret) return;
    for (auto &t : _turrets) {
        if (t.active) continue;
        t.active = true;
        t.hp = TURRET_HP;
        t.x = x; t.y = TOWER_TOP + TURRET_R * 0.8f; t.z = z;
        t.fireAt = millis() + (unsigned long)random(400, 1400);
        t.flashUntil = 0;
        t.obj->enabled = true;
        return;
    }
}

// The planet's fields: pillars (tall, to go round; short, to hop over),
// turret towers, arches to fly under, and pairs of pillars with a gap.
void StarFluxGame::spawnPlanetHazard() {
    const float z = BOX_SPAWN_Z;
    const int r = (int)random(0, 100);
    if (r < 40) {
        float x = random(0, 100) < 30 ? _shipX : (float)random(-(long)(BOX_X + 100), (long)(BOX_X + 101));
        float half = (float)random(90, 131);
        float top = random(0, 100) < 60 ? 900.0f : -40.0f;
        spawnBox(x - half, x + half, GROUND_Y, top, z, 2.0f * half);
    } else if (r < 68) {
        spawnTower((float)random(-320, 321), z, true);
    } else if (r < 84) {
        float gx = (float)random(-200, 201), gap = 230.0f;
        spawnBox(gx - gap - 150.0f, gx - gap, GROUND_Y, 420.0f, z, 150.0f);
        spawnBox(gx + gap, gx + gap + 150.0f, GROUND_Y, 420.0f, z, 150.0f);
        spawnBox(gx - gap - 150.0f, gx + gap + 150.0f, 170.0f, 420.0f, z, 150.0f);
    } else {
        float gx = (float)random(-220, 221), gap = 190.0f;
        spawnBox(gx - gap - 260.0f, gx - gap, GROUND_Y, 900.0f, z, 200.0f);
        spawnBox(gx + gap, gx + gap + 260.0f, GROUND_Y, 900.0f, z, 200.0f);
    }
}

// The trench's fields: barriers with a slot to fly through (upright, level,
// or a window), laser gates later in the stage, and turret towers.
void StarFluxGame::spawnTrenchHazard() {
    const float z = BOX_SPAWN_Z, W = TRENCH_HALF_W, F = TRENCH_FLOOR, T = TRENCH_TOP;
    const float g = BARRIER_GAP * 0.5f, d = 160.0f;
    const int r = (int)random(0, 100);
    if (_seg >= 4 && r < 30) {
        float y0 = (float)random((long)BOX_Y_MIN - 40, (long)BOX_Y_MAX - 200);
        spawnGate(y0, y0 + 200.0f, z);
    } else if (r < 55) {
        float gx = (float)random(-(long)(BOX_X - 120), (long)(BOX_X - 119));
        spawnBox(-W, gx - g, F, T, z, d);
        spawnBox(gx + g, W, F, T, z, d);
    } else if (r < 80) {
        float gy = (float)random((long)BOX_Y_MIN + 110, (long)BOX_Y_MAX - 109);
        spawnBox(-W, W, F, gy - g, z, d);
        spawnBox(-W, W, gy + g, T, z, d);
    } else {
        float gx = (float)random(-(long)(BOX_X - 150), (long)(BOX_X - 149));
        float gy = (float)random((long)BOX_Y_MIN + 130, (long)BOX_Y_MAX - 129);
        const float wg = g + 20.0f;
        spawnBox(-W, gx - wg, F, T, z, d);
        spawnBox(gx + wg, W, F, T, z, d);
        spawnBox(gx - wg, gx + wg, F, gy - wg, z, d);
        spawnBox(gx - wg, gx + wg, gy + wg, T, z, d);
    }
    // A turret tower half way to the next one.
    if (random(0, 100) < 35) spawnTower((float)random(-300, 301), z + 1000.0f, true);
}

void StarFluxGame::updateBoxes(AudioEngine &audio) {
    const float dz = FLY_SPEED * _frameScale;
    for (auto &b : _boxes) {
        if (!b.active) continue;
        b.z -= dz;
        if (b.z + b.depth * 0.5f < CAMERA_NEAR + 10) {
            b.active = false;
            if (b.obj) b.obj->enabled = false;
            continue;
        }
        if (b.obj) b.obj->setPosition((int32_t)((b.x0 + b.x1) * 0.5f), (int32_t)((b.y0 + b.y1) * 0.5f), (int32_t)b.z);
        if (b.hit || _stage != STAGE_RUN || fabsf(b.z - SHIP_Z) > b.depth * 0.5f + 20.0f) continue;
        if (b.gate && !gateOn(b, millis())) continue;
        // The ship's circle against the box's face.
        float nx = clampf(_shipX, b.x0, b.x1), ny = clampf(_shipY, b.y0, b.y1);
        float ddx = _shipX - nx, ddy = _shipY - ny, r = SHIP_HIT_R * 0.7f;
        if (ddx * ddx + ddy * ddy < r * r) {
            b.hit = true;
            if (b.gate) addBlast(_shipX, _shipY, SHIP_Z, 120.0f, ArcadeConfig::COLOR_RED);
            damageShip(BOX_DAMAGE, audio);
        }
    }
}

// Does a solid obstacle stop this shot?
bool StarFluxGame::shotBlocked(const Shot &s) const {
    for (const auto &b : _boxes) {
        if (!b.active || b.gate) continue;
        const float h = b.depth * 0.5f;
        if (b.z + h < s.pz || b.z - h > s.z) continue;
        if (s.x >= b.x0 && s.x <= b.x1 && s.y >= b.y0 && s.y <= b.y1) return true;
    }
    return false;
}

// The nearest obstacle not yet passed, within AID_RANGE: the z of its
// front face. A barrier's boxes all share it.
bool StarFluxGame::nextObstacle(float &front) const {
    bool found = false;
    for (const auto &b : _boxes) {
        if (!b.active || b.z + b.depth * 0.5f < SHIP_Z) continue;
        const float f = b.z - b.depth * 0.5f;
        if (f - SHIP_Z > AID_RANGE) continue;
        if (!found || f < front) { front = f; found = true; }
    }
    return found;
}

// Would the ship, where it is now, get through the obstacle at this front?
// The same test updateBoxes() makes, against every box of it; a gate
// counts if it will be on when it gets here.
bool StarFluxGame::passClear(float front) const {
    const float r = SHIP_HIT_R * 0.7f;
    for (const auto &b : _boxes) {
        if (!b.active || fabsf(b.z - b.depth * 0.5f - front) > 60.0f) continue;
        if (b.gate) {
            const float ms = (b.z - SHIP_Z) / FLY_SPEED * (float)REFERENCE_FRAME_MS;
            if (!gateOn(b, millis() + (unsigned long)max(0.0f, ms))) continue;
        }
        float nx = clampf(_shipX, b.x0, b.x1), ny = clampf(_shipY, b.y0, b.y1);
        float dx = _shipX - nx, dy = _shipY - ny;
        if (dx * dx + dy * dy < r * r) return false;
    }
    return true;
}

// Flight aids for the next obstacle: its front face outlined, so the gaps
// stand out, and a diamond on it where the ship will pass. Yellow and green
// while that's clear; the outline and diamond blink red on a collision
// course.
void StarFluxGame::drawFlightAids(GFXcanvas16 &canvas) {
    if (_phase != PHASE_PLAYING || _stage != STAGE_RUN) return;
    float front;
    if (!nextObstacle(front)) return;
    const float z = max(front, SHIP_Z + 30.0f);
    const bool clear = passClear(front);
    const bool blink = (millis() / 120) & 1;
    const uint16_t edge = clear ? rgb(255, 214, 80) : blink ? rgb(150, 20, 20) : rgb(255, 50, 50);
    for (const auto &b : _boxes) {
        if (!b.active || b.gate || fabsf(b.z - b.depth * 0.5f - front) > 60.0f) continue;
        float xa, ya, xb, yb, xc, yc, xd, yd;
        if (!project(b.x0, b.y0, z, xa, ya) || !project(b.x1, b.y0, z, xb, yb) ||
            !project(b.x1, b.y1, z, xc, yc) || !project(b.x0, b.y1, z, xd, yd)) continue;
        canvas.drawLine((int)xa, (int)ya, (int)xb, (int)yb, edge);
        canvas.drawLine((int)xb, (int)yb, (int)xc, (int)yc, edge);
        canvas.drawLine((int)xc, (int)yc, (int)xd, (int)yd, edge);
        canvas.drawLine((int)xd, (int)yd, (int)xa, (int)ya, edge);
    }
    float sx, sy;
    if (!project(_shipX, _shipY, z, sx, sy)) return;
    if (!clear && blink) return;
    const uint16_t col = clear ? rgb(90, 255, 120) : rgb(255, 60, 60);
    int s = (int)(SHIP_HIT_R * 0.7f * pixelsPerUnit(z) + 0.5f);
    if (s < 3) s = 3;
    const int x = (int)sx, y = (int)sy;
    for (int k = s; k <= s + 1; ++k) {   // two pixels thick
        canvas.drawLine(x - k, y, x, y - k, col);
        canvas.drawLine(x, y - k, x + k, y, col);
        canvas.drawLine(x + k, y, x, y + k, col);
        canvas.drawLine(x, y + k, x - k, y, col);
    }
}

// Turrets ride their towers towards you, spinning, and fire while they're
// in range (sometimes leading you, as fighters do).
void StarFluxGame::updateTurrets(AudioEngine &audio) {
    const float dz = FLY_SPEED * _frameScale;
    const int spin = (int)((millis() / 10) % 360);
    for (auto &t : _turrets) {
        if (!t.active) continue;
        t.z -= dz;
        if (t.z < CAMERA_NEAR + 10) { t.active = false; t.obj->enabled = false; continue; }
        t.obj->setPosition((int32_t)t.x, (int32_t)t.y, (int32_t)t.z);
        t.obj->setRotation(0, spin, 0);
        bool flash = before(t.flashUntil);
        if (flash != (t.obj->triangles[0].material == &_flashMat)) setFlash(t.obj, flash, &_turretMat, &_turretMat2);
        if (_stage == STAGE_RUN && t.z > TURRET_FIRE_NEAR && t.z < TURRET_FIRE_FAR && reached(t.fireAt)) {
            float tx = _shipX, ty = _shipY;
            if (random(0, 100) < LEAD_PCT) {
                float frames = (t.z - SHIP_Z) / eshotSpeed();
                tx += _shipVX * frames;
                ty += _shipVY * frames;
            }
            fireAt(t.x, t.y + 40.0f, t.z - 40.0f, tx, ty);
            unsigned long gap = TURRET_FIRE_MS - (unsigned long)min(500, 100 * (_loop - 1));
            t.fireAt = millis() + gap + (unsigned long)random(0, 400);
        }
    }
}

void StarFluxGame::destroyTurret(Turret &t, bool byPlayer, AudioEngine &audio) {
    t.active = false;
    t.obj->enabled = false;
    setFlash(t.obj, false, &_turretMat, &_turretMat2);
    addBlast(t.x, t.y, t.z, 200.0f, ArcadeConfig::COLOR_ORANGE);
    _particles.emitSparks(Renderer::Vec3f{ t.x, t.y, t.z }, Renderer::Vec3f{ 0, 1, 0 }, 500.0f, 12);
    sfx(audio, SFX_POP);
    if (!byPlayer) return;
    _score += TURRET_POINTS;
    ++_targetsDowned;
}

void StarFluxGame::hideWorld() {
    for (auto &b : _boxes) { b.active = false; if (b.obj) b.obj->enabled = false; }
    for (auto &t : _turrets) { t.active = false; if (t.obj) t.obj->enabled = false; }
}

// A turret: a spinning two-tone diamond.
Renderer::Object* StarFluxGame::buildTurret() {
    return buildGem(TURRET_R, &_turretMat, &_turretMat2);
}

// The crawler, built on the ground (its origin): treads, a hull, a raised
// deck with mounts for the missile pods, and red lights across its front.
// Its dome core sits in front of the hull (StarFluxBoss.cpp).
Renderer::Object* StarFluxGame::buildCrawler() {
    auto* o = new Renderer::Object();
    addBox(o, -560, 100, 0, 240, 200, 1000, &_crawlerDarkMat);
    addBox(o, 560, 100, 0, 240, 200, 1000, &_crawlerDarkMat);
    addBox(o, 0, 280, 0, 900, 260, 900, &_crawlerMat);
    addBox(o, 0, 470, 80, 560, 140, 600, &_crawlerMat);
    addBox(o, -300, 555, -100, 150, 30, 150, &_crawlerDarkMat);
    addBox(o, 300, 555, -100, 150, 30, 150, &_crawlerDarkMat);
    addBox(o, 0, 330, -455, 760, 26, 8, &_boxLightMat);
    return finishSolid(o);
}

// The reactor: a wall of steel across the end of the trench, a recess for
// the core, housings for the two emitters, cyan light strips.
Renderer::Object* StarFluxGame::buildReactor() {
    auto* o = new Renderer::Object();
    addBox(o, 0, 0, 0, 1100, 820, 300, &_reactorMat);
    addBox(o, 0, -10, -165, 460, 460, 30, &_reactorDarkMat);
    addBox(o, -400, 170, -180, 220, 220, 60, &_reactorDarkMat);
    addBox(o, 400, 170, -180, 220, 220, 60, &_reactorDarkMat);
    addBox(o, 0, 370, -156, 1000, 24, 10, &_boxLightMat);
    addBox(o, 0, -370, -156, 1000, 24, 10, &_boxLightMat);
    return finishSolid(o);
}

// An obstacle slot: a unit box, resized by spawnBox().
Renderer::Object* StarFluxGame::buildObstacleBox() {
    auto* o = new Renderer::Object();
    addBox(o, 0, 0, 0, 100, 100, 100, &_boxMat);
    return finishSolid(o);
}

}  // namespace starflux
