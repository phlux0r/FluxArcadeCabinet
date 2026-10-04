#include "RollFluxGame.h"
#include <algorithm>

namespace rollflux {

namespace {

// Components are in RGB565's own ranges: r 0-31, g 0-63, b 0-31.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// t = 0 gives a, t = 1 gives b, per RGB565 channel.
uint16_t lerp565(uint16_t a, uint16_t b, float t) {
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((ar + (int)((br - ar) * t)) << 11) |
                      ((ag + (int)((bg - ag) * t)) << 5) |
                       (ab + (int)((bb - ab) * t)));
}

// A colour lit by k (1 = as it is), each channel held to its range.
uint16_t shade565(uint16_t c, float k) {
    int r = (int)((c >> 11) * k), g = (int)(((c >> 5) & 63) * k), b = (int)((c & 31) * k);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Fills a convex polygon straight into an RGB565 buffer, sampling pixel
// centres with a half-open rule so neighbouring faces neither gap nor
// overlap (Tube Flux's). With shade, it halves what's there instead: the
// ball's shadow.
void fillConvex(uint16_t* buf, int w, int h, const float* xs, const float* ys, int n, uint16_t colour, bool shade) {
    float ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < n; ++i) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    int y0 = (int)ceilf(ymin - 0.5f), y1 = (int)ceilf(ymax - 0.5f) - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > h - 1) y1 = h - 1;
    for (int y = y0; y <= y1; ++y) {
        const float sy = (float)y + 0.5f;
        float xl = 1e30f, xr = -1e30f;
        for (int i = 0; i < n; ++i) {
            int j = (i + 1 == n) ? 0 : i + 1;
            float ya = ys[i], yb = ys[j];
            if ((sy < ya) == (sy < yb)) continue;
            float x = xs[i] + (sy - ya) * (xs[j] - xs[i]) / (yb - ya);
            if (x < xl) xl = x;
            if (x > xr) xr = x;
        }
        int xa = (int)ceilf(xl - 0.5f), xb = (int)ceilf(xr - 0.5f) - 1;
        if (xa < 0) xa = 0;
        if (xb > w - 1) xb = w - 1;
        uint16_t* row = buf + y * w;
        if (shade) for (int x = xa; x <= xb; ++x) row[x] = (row[x] >> 1) & 0x7BEF;
        else       for (int x = xa; x <= xb; ++x) row[x] = colour;
    }
}

// The palettes, by world: the floor gets lighter with height, so levels
// read apart; ramps amber; ice pale (whiter in the Ice Relay, where the
// floor's cold too); checkpoints dark (blue, or violet on the slate; lit
// once reached); boost pads orange with a yellow arrow; the goal a
// chequered flag; sides dark. The Prism Works is dusky violet, so cyan and
// magenta stand out on it.
const uint16_t FLOOR_COL[4][MAX_FLOOR_LEVEL + 1][2] = {
    {   // Orbit Garden: greens
        { rgb565( 5, 30,  9), rgb565( 4, 23,  7) },
        { rgb565( 9, 40, 13), rgb565( 7, 31, 10) },
        { rgb565(13, 48, 17), rgb565(10, 38, 13) },
        { rgb565(17, 56, 21), rgb565(13, 45, 16) },
        { rgb565(21, 62, 25), rgb565(16, 51, 19) },
    },
    {   // Ice Relay: slate blues
        { rgb565( 5, 18, 15), rgb565( 4, 14, 12) },
        { rgb565( 8, 26, 19), rgb565( 6, 20, 15) },
        { rgb565(11, 33, 22), rgb565( 9, 27, 18) },
        { rgb565(14, 40, 25), rgb565(11, 33, 21) },
        { rgb565(17, 47, 28), rgb565(14, 39, 24) },
    },
    {   // Prism Works: greyed violets
        { rgb565( 9, 16, 14), rgb565( 7, 12, 11) },
        { rgb565(12, 22, 17), rgb565( 9, 17, 14) },
        { rgb565(15, 28, 20), rgb565(12, 22, 16) },
        { rgb565(18, 34, 23), rgb565(14, 27, 19) },
        { rgb565(21, 40, 26), rgb565(17, 32, 21) },
    },
    {   // Flux Core: dark metal
        { rgb565( 6, 13,  8), rgb565( 5, 10,  6) },
        { rgb565( 8, 17, 10), rgb565( 6, 13,  8) },
        { rgb565(10, 21, 12), rgb565( 8, 17, 10) },
        { rgb565(12, 25, 14), rgb565(10, 20, 12) },
        { rgb565(14, 29, 16), rgb565(11, 23, 13) },
    },
};
const uint16_t ICE_COL[4][2] = {
    { rgb565(21, 54, 30), rgb565(17, 46, 27) },
    { rgb565(27, 61, 31), rgb565(23, 55, 30) },
    { rgb565(24, 56, 31), rgb565(20, 48, 28) },
    { rgb565(22, 52, 30), rgb565(18, 44, 26) },
};
// The sky, bottom and top: a dark blue, a deep violet night, a purple
// dusk, and a core's dark with an ember glow low down.
const uint16_t SKY_COL[4][2] = {
    { rgb565(8, 10, 18), rgb565(1, 2, 6) },
    { rgb565(10, 4, 16), rgb565(2, 0, 5) },
    { rgb565(16, 6, 14), rgb565(3, 0, 6) },
    { rgb565(12, 8, 1), rgb565(0, 0, 2) },
};
const uint16_t RAMP_COL[2]  = { rgb565(26, 42, 6), rgb565(21, 33, 4) };
const uint16_t CHECK_COL[4][2] = {
    { rgb565(4, 18, 24), rgb565(3, 13, 19) },     // blue on the greens
    { rgb565(12, 8, 22), rgb565(9, 6, 17) },      // violet on the slate
    { rgb565(3, 22, 8), rgb565(2, 17, 6) },       // green on the violet
    { rgb565(2, 24, 18), rgb565(1, 19, 14) },     // teal on the metal
};
const uint16_t CHECK_LIT[4][2] = {
    { rgb565(6, 44, 31), rgb565(4, 36, 26) },
    { rgb565(24, 30, 31), rgb565(20, 24, 27) },
    { rgb565(8, 56, 14), rgb565(6, 46, 11) },
    { rgb565(6, 48, 30), rgb565(4, 40, 25) },
};
// Moving parts: platforms and lifts hazard orange, bridges pale steel,
// sweepers' arms glowing red; tops then sides.
const uint16_t PLATFORM_COL[2] = { rgb565(29, 34, 2), rgb565(16, 16, 1) };
// The guardians: pistons steel (orange flashing as they warn), the core
// dark red (glowing yellow when it's open to a hit), the ring teal (red
// flashing as it's about to open).
const uint16_t PISTON_COL[2] = { rgb565(14, 28, 16), rgb565(11, 22, 13) };
const uint16_t WARN_COL[2]   = { rgb565(31, 30, 0), rgb565(28, 12, 0) };
const uint16_t CORE_COL[2]   = { rgb565(18, 4, 4), rgb565(11, 2, 3) };
const uint16_t CORE_LIT[2]   = { rgb565(31, 52, 6), rgb565(31, 30, 2) };
const uint16_t RING_COL[2]   = { rgb565(8, 22, 18), rgb565(6, 18, 15) };
const uint16_t NODE_COL[3]   = { rgb565(14, 4, 4), rgb565(31, 63, 31), rgb565(31, 56, 0) };   // shut, lit, lit
const uint16_t BRIDGE_COL[2]   = { rgb565(20, 44, 26), rgb565(10, 22, 14) };
const uint16_t SWEEPER_COL[2]  = { rgb565(31, 22, 2), rgb565(22, 6, 2) };
// The two colours, cyan and magenta: bright (gates' bars, bridges, the
// ball), and dark (what the ball's own colour lets through looks like).
const uint16_t POLE_COL[2][2] = {
    { rgb565(0, 58, 31), rgb565(0, 46, 26) },
    { rgb565(31, 12, 27), rgb565(26, 8, 22) },
};
const uint16_t CRYSTAL_TOP[2]  = { rgb565(26, 56, 31), rgb565(31, 40, 31) };
const uint16_t CRYSTAL_SIDE[2] = { rgb565(14, 36, 28), rgb565(22, 22, 28) };
const uint16_t CONVEYOR_COL[2] = { rgb565(9, 18, 10), rgb565(7, 14, 8) };
const uint16_t STRIPE_COL      = rgb565(26, 44, 4);
const uint16_t BUMPER_COL[3]   = { rgb565(28, 8, 4), rgb565(31, 30, 20), rgb565(31, 63, 31) };   // side, top, lit
const uint16_t BOOST_COL[2] = { rgb565(28, 28, 2), rgb565(24, 22, 1) };
const uint16_t ARROW_COL[2] = { rgb565(31, 63, 10), rgb565(31, 48, 0) };
const uint16_t GOAL_COL[2]  = { rgb565(31, 63, 31), rgb565(3, 6, 5) };
const uint16_t SIDE_COL[2]  = { rgb565(4, 14, 8), rgb565(3, 10, 6) };    // north/south faces, east/west
const uint16_t RAIL_COL[2]  = { rgb565(25, 52, 27), rgb565(19, 40, 21) };
const uint16_t BALL_COL[3]  = { rgb565(0, 52, 31), rgb565(31, 63, 31), rgb565(31, 54, 2) };   // gores, caps
const uint16_t BALL_MAGENTA = rgb565(31, 12, 27);   // its coloured gores, magenta
const uint16_t GEM_COL[2]   = { rgb565(31, 60, 6), rgb565(31, 63, 26) };

// The sun, for the ball: from behind the camera (mostly), high.
const float SUN[3] = { -0.40f, 0.80f, -0.45f };

// The ball: a sphere of 6 bands and 8 gores, poles up and down, unit
// radius; built once.
constexpr int BALL_LON = 8, BALL_RINGS = 5;
constexpr int BALL_VERTS = 2 + BALL_RINGS * BALL_LON;
struct BallMesh {
    float v[BALL_VERTS][3];
    BallMesh() {
        v[0][0] = 0; v[0][1] = 1; v[0][2] = 0;
        for (int i = 1; i <= BALL_RINGS; ++i) {
            const float phi = i * (float)PI / (BALL_RINGS + 1);
            for (int g = 0; g < BALL_LON; ++g) {
                const float th = g * 2.0f * (float)PI / BALL_LON;
                float* p = v[1 + (i - 1) * BALL_LON + g];
                p[0] = sinf(phi) * cosf(th);
                p[1] = cosf(phi);
                p[2] = sinf(phi) * sinf(th);
            }
        }
        v[BALL_VERTS - 1][0] = 0; v[BALL_VERTS - 1][1] = -1; v[BALL_VERTS - 1][2] = 0;
    }
};
const BallMesh &ballMesh() { static const BallMesh m; return m; }
inline int ringVert(int ring, int g) { return 1 + (ring - 1) * BALL_LON + (g % BALL_LON); }

// The four sides of a cell, as (dc, dr, our two corners on that edge, the
// neighbour's two on it, the side's colour), walking each edge in the same
// order for both.
struct Edge { int dc, dr, a, b, na, nb, col; };
const Edge EDGES[4] = {
    {  0,  1, 0, 1, 3, 2, 0 },   // south: our sw, se against its nw, ne
    {  0, -1, 2, 3, 1, 0, 0 },   // north: our ne, nw against its se, sw
    {  1,  0, 1, 2, 0, 3, 1 },   // east:  our se, ne against its sw, nw
    { -1,  0, 3, 0, 2, 1, 1 },   // west:  our nw, sw against its ne, se
};

}  // namespace

// Corner heights of a cell's top: south-west, south-east, north-east,
// north-west. A ramp's high edge is one step up.
void RollFluxGame::cornerHeights(int c, int r, float out[4]) const {
    const Cell &k = _cells[r][c];
    const float b = (float)(k.h * HEIGHT_STEP), t = b + HEIGHT_STEP;
    out[0] = out[1] = out[2] = out[3] = b;
    switch (k.kind) {
        case K_RAMP_N: out[2] = out[3] = t; break;
        case K_RAMP_S: out[0] = out[1] = t; break;
        case K_RAMP_E: out[1] = out[2] = t; break;
        case K_RAMP_W: out[0] = out[3] = t; break;
        case K_CRYSTAL: out[0] = out[1] = out[2] = out[3] = b + CRYSTAL_HEIGHT; break;
        case K_PISTON: case K_CORE: out[0] = out[1] = out[2] = out[3] = b + extraLift(c, r); break;
        default: break;
    }
}

// The plane of a cell's top at (x, z), carried on past its edges.
float RollFluxGame::planeAt(int c, int r, float x, float z) const {
    const Cell &k = _cells[r][c];
    const float fx = (x - cellX0(c)) / CELL, fz = (z - cellZ0(r)) / CELL;
    float y = (float)(k.h * HEIGHT_STEP);
    switch (k.kind) {
        case K_RAMP_N: y += HEIGHT_STEP * fz; break;
        case K_RAMP_S: y += HEIGHT_STEP * (1.0f - fz); break;
        case K_RAMP_E: y += HEIGHT_STEP * fx; break;
        case K_RAMP_W: y += HEIGHT_STEP * (1.0f - fx); break;
        case K_CRYSTAL: y += CRYSTAL_HEIGHT; break;
        case K_PISTON: case K_CORE: y += extraLift(c, r); break;
        default: break;
    }
    return y;
}

float RollFluxGame::gemY(int c, int r) const {
    return _cells[r][c].h * HEIGHT_STEP + 85.0f + 12.0f * sinf(millis() * 0.005f + c + r);
}

uint16_t RollFluxGame::fog(uint16_t col, float depth) const {
    if (depth <= FOG_NEAR) return col;
    const float t = (depth - FOG_NEAR) / (FOG_FAR - FOG_NEAR);
    return lerp565(col, _sky[ArcadeConfig::LANDSCAPE_HEIGHT - 1], t > 1.0f ? 1.0f : t);
}

// Jet's camera transform for this frame, composed as Scene::render() does
// (same trig tables, same fixed-point order), so it projects exactly as
// Jet would.
void RollFluxGame::computeCameraMatrix() {
    int32_t cX, sX, cY, sY, cZ, sZ;
    _camera.getRotationMatrix(cX, sX, cY, sY, cZ, sZ);
    const int32_t k00 = cY, k01 = 0, k02 = sY;
    const int32_t k10 = (int32_t)((int64_t)sX * sY / FIXED_POINT_SCALE);
    const int32_t k11 = cX;
    const int32_t k12 = (int32_t)(-(int64_t)sX * cY / FIXED_POINT_SCALE);
    const int32_t k20 = (int32_t)(-(int64_t)cX * sY / FIXED_POINT_SCALE);
    const int32_t k21 = sX;
    const int32_t k22 = (int32_t)((int64_t)cX * cY / FIXED_POINT_SCALE);
    const int32_t m00 = (int32_t)(((int64_t)cZ * k00 - (int64_t)sZ * k10) / FIXED_POINT_SCALE);
    const int32_t m01 = (int32_t)(((int64_t)cZ * k01 - (int64_t)sZ * k11) / FIXED_POINT_SCALE);
    const int32_t m02 = (int32_t)(((int64_t)cZ * k02 - (int64_t)sZ * k12) / FIXED_POINT_SCALE);
    const int32_t m10 = (int32_t)(((int64_t)sZ * k00 + (int64_t)cZ * k10) / FIXED_POINT_SCALE);
    const int32_t m11 = (int32_t)(((int64_t)sZ * k01 + (int64_t)cZ * k11) / FIXED_POINT_SCALE);
    const int32_t m12 = (int32_t)(((int64_t)sZ * k02 + (int64_t)cZ * k12) / FIXED_POINT_SCALE);
    const int32_t all[9] = { m00, m01, m02, m10, m11, m12, k20, k21, k22 };
    for (int i = 0; i < 9; ++i) _m[i] = (float)all[i] / FIXED_POINT_SCALE;
}

void RollFluxGame::toCam(float x, float y, float z, float* out) const {
    const float px = x - (float)_camera.position.x, py = y - (float)_camera.position.y,
                pz = z - (float)_camera.position.z;
    out[0] = _m[0] * px + _m[1] * py + _m[2] * pz;
    out[1] = _m[3] * px + _m[4] * py + _m[5] * pz;
    out[2] = _m[6] * px + _m[7] * py + _m[8] * pz;
}

void RollFluxGame::drawSky(GFXcanvas16 &canvas) {
    uint16_t* buf = canvas.getBuffer();
    const int w = canvas.width(), h = canvas.height();
    for (int y = 0; y < h; ++y) {
        uint16_t* row = buf + y * w;
        const uint16_t c = _sky[y];
        for (int x = 0; x < w; ++x) row[x] = c;
    }
}

// Stars all round, infinitely far: turned with the camera, never moved.
// The course is drawn over them, so they show only in the void.
void RollFluxGame::drawStars(GFXcanvas16 &canvas) {
    uint16_t* buf = canvas.getBuffer();
    const int w = canvas.width(), h = canvas.height();
    const float f = _camera.fovFactor;
    for (int i = 0; i < STAR_COUNT; ++i) {
        const float* d = _stars[i];
        const float z = _m[6] * d[0] + _m[7] * d[1] + _m[8] * d[2];
        if (z < 0.1f) continue;
        const int sx = (int)(w / 2 + (_m[0] * d[0] + _m[1] * d[1] + _m[2] * d[2]) * f / z);
        const int sy = (int)(h / 2 - (_m[3] * d[0] + _m[4] * d[1] + _m[5] * d[2]) * f / z);
        if (sx >= 0 && sx < w && sy >= 0 && sy < h) buf[sy * w + sx] = _starCol[i];
    }
}

// A polygon given in camera space: clipped to the near plane, projected
// as Jet projects (x * f / z from the centre, y up), filled.
void RollFluxGame::drawPoly(uint16_t* buf, int w, int h, const float (*p)[3], int n, uint16_t colour, bool shade) {
    const float zn = (float)CAMERA_NEAR;
    float cl[10][3];
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const float* a = p[i];
        const float* b = p[(i + 1) % n];
        const bool ina = a[2] >= zn, inb = b[2] >= zn;
        if (ina) { cl[m][0] = a[0]; cl[m][1] = a[1]; cl[m][2] = a[2]; ++m; }
        if (ina != inb) {
            const float t = (zn - a[2]) / (b[2] - a[2]);
            cl[m][0] = a[0] + (b[0] - a[0]) * t;
            cl[m][1] = a[1] + (b[1] - a[1]) * t;
            cl[m][2] = zn;
            ++m;
        }
    }
    if (m < 3) return;
    const float f = _camera.fovFactor, cx = (float)(w / 2), cy = (float)(h / 2);
    float xs[10], ys[10];
    for (int i = 0; i < m; ++i) {
        const float s = f / cl[i][2];
        xs[i] = cx + cl[i][0] * s;
        ys[i] = cy - cl[i][1] * s;
    }
    fillConvex(buf, w, h, xs, ys, m, colour, shade);
}

// A cell: its sides that face the camera, then its top (a goal's in four
// chequered squares, a boost pad's with its arrow).
void RollFluxGame::drawCell(uint16_t* buf, int w, int h, int c, int r, float depth) {
    const Cell &k = _cells[r][c];
    float hgt[4];
    cornerHeights(c, r, hgt);
    const float x0 = cellX0(c), x1 = x0 + CELL, z0 = cellZ0(r), z1 = z0 + CELL;
    const float cx[4] = { x0, x1, x1, x0 }, cz[4] = { z0, z0, z1, z1 };
    // A phase bridge of the other colour: just a dim ghost of a tile, set
    // in from its edges, nothing to stand on.
    if (isBridge(k.kind) && needsColour(c, r) != _polarity) {
        const float in = CELL * 0.12f, y = hgt[0];
        float q[4][3];
        toCam(x0 + in, y, z0 + in, q[0]);
        toCam(x1 - in, y, z0 + in, q[1]);
        toCam(x1 - in, y, z1 - in, q[2]);
        toCam(x0 + in, y, z1 - in, q[3]);
        drawPoly(buf, w, h, q, 4, fog(shade565(POLE_COL[needsColour(c, r)][1], 0.35f), depth));
        return;
    }
    const float sideDepth = isBridge(k.kind) ? BRIDGE_DEPTH : SIDE_DEPTH;
    float top[4][3];
    for (int i = 0; i < 4; ++i) toCam(cx[i], hgt[i], cz[i], top[i]);
    const float camX = (float)_camera.position.x, camZ = (float)_camera.position.z;
    for (const Edge &e : EDGES) {
        // Only sides facing the camera: the camera is beyond the edge.
        const float ex = e.dc > 0 ? x1 : e.dc < 0 ? x0 : 0, ez = e.dr > 0 ? z0 : e.dr < 0 ? z1 : 0;
        if ((e.dc > 0 && camX <= ex) || (e.dc < 0 && camX >= ex) ||
            (e.dr > 0 && camZ >= ez) || (e.dr < 0 && camZ <= ez)) continue;
        float nh[4];
        const bool nsolid = present(c + e.dc, r + e.dr);
        if (nsolid) cornerHeights(c + e.dc, r + e.dr, nh);
        const float ba = nsolid ? nh[e.na] : hgt[e.a] - sideDepth;
        const float bb = nsolid ? nh[e.nb] : hgt[e.b] - sideDepth;
        if (hgt[e.a] <= ba + 1 && hgt[e.b] <= bb + 1) continue;
        float q[4][3];
        for (int i = 0; i < 3; ++i) { q[0][i] = top[e.a][i]; q[1][i] = top[e.b][i]; }
        toCam(cx[e.b], fminf(bb, hgt[e.b]), cz[e.b], q[2]);
        toCam(cx[e.a], fminf(ba, hgt[e.a]), cz[e.a], q[3]);
        const uint16_t sc = k.kind == K_CRYSTAL ? CRYSTAL_SIDE[e.col]
                          : k.kind == K_CORE ? (courseDef().guardian == 3 ? CRYSTAL_SIDE[e.col] : CORE_COL[1])
                          : isBridge(k.kind) ? POLE_COL[needsColour(c, r)][1] : SIDE_COL[e.col];
        drawPoly(buf, w, h, q, 4, fog(sc, depth));
    }
    const int chk = (c + r) & 1;
    if (k.kind == K_GOAL) {
        // Four squares, chequered: the flag of a finish line.
        const float y = hgt[0];
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const float ax = x0 + i * CELL * 0.5f, az = z0 + j * CELL * 0.5f;
                float q[4][3];
                toCam(ax, y, az, q[0]);
                toCam(ax + CELL * 0.5f, y, az, q[1]);
                toCam(ax + CELL * 0.5f, y, az + CELL * 0.5f, q[2]);
                toCam(ax, y, az + CELL * 0.5f, q[3]);
                drawPoly(buf, w, h, q, 4, fog(GOAL_COL[(i + j + chk) & 1], depth));
            }
        return;
    }
    uint16_t col;
    if (k.kind == K_CORE) {
        if (courseDef().guardian == 3) col = CRYSTAL_TOP[(millis() / 250) & 1];
        else col = _gLit >= 0 ? CORE_LIT[(millis() / 120) & 1] : CORE_COL[0];
    }
    else if (k.kind == K_PISTON) col = pistonWarning(c, r) ? WARN_COL[(millis() / 120) & 1] : PISTON_COL[chk];
    else if (k.kind == K_RING) col = ringWarning(c, r) ? WARN_COL[1 - ((millis() / 120) & 1)] : RING_COL[chk];
    else if (k.kind == K_CRYSTAL) col = CRYSTAL_TOP[chk];
    else if (isBridge(k.kind)) col = POLE_COL[needsColour(c, r)][chk];
    else if (isConveyor(k.kind)) col = CONVEYOR_COL[chk];
    else if (isRamp(k.kind)) col = RAMP_COL[chk];
    else if (k.kind == K_ICE) col = ICE_COL[_skyWorld][chk];
    else if (k.kind == K_CHECK) col = (r == _checkR ? CHECK_LIT : CHECK_COL)[_skyWorld][chk];
    else if (isBoost(k.kind)) col = BOOST_COL[chk];
    else col = FLOOR_COL[_skyWorld][k.h > MAX_FLOOR_LEVEL ? MAX_FLOOR_LEVEL : k.h][chk];
    drawPoly(buf, w, h, top, 4, fog(col, depth));
    if (isBoost(k.kind)) {
        // The arrow: a triangle pointing the way the pad pushes, flashing.
        static const float FX[4] = { 0, 0, 1, -1 }, FZ[4] = { 1, -1, 0, 0 };
        const int d = k.kind - K_BOOST_N;
        const float fx = FX[d], fz = FZ[d], rx = fz, rz = -fx;
        const float mx = x0 + CELL * 0.5f, mz = z0 + CELL * 0.5f, y = hgt[0] + 1.0f;
        const float pts[3][2] = { { 0.0f, 0.36f }, { -0.30f, -0.22f }, { 0.30f, -0.22f } };   // (right, forward)
        float q[3][3];
        for (int i = 0; i < 3; ++i)
            toCam(mx + (pts[i][0] * rx + pts[i][1] * fx) * CELL, y,
                  mz + (pts[i][0] * rz + pts[i][1] * fz) * CELL, q[i]);
        drawPoly(buf, w, h, q, 3, fog(ARROW_COL[(millis() / 150) & 1], depth));
    }
    if (isConveyor(k.kind)) {
        // Two stripes across the belt, moving the way it carries.
        static const float FX[4] = { 0, 0, 1, -1 }, FZ[4] = { 1, -1, 0, 0 };
        const int d = k.kind - K_CONV_N;
        const float fx = FX[d], fz = FZ[d], rx = fz, rz = -fx;
        const float mx = x0 + CELL * 0.5f, mz = z0 + CELL * 0.5f, y = hgt[0] + 1.0f;
        const float run = (millis() % 1000) * (CONVEYOR_SPEED / 1000.0f) / CELL;   // cells moved this second
        for (int i = 0; i < 2; ++i) {
            float s0 = run + i * 0.5f;
            s0 -= floorf(s0);
            const float s1 = fminf(s0 + 0.16f, 1.0f);
            float q[4][3];
            const float f0 = (s0 - 0.5f) * CELL, f1 = (s1 - 0.5f) * CELL, half = CELL * 0.5f;
            toCam(mx + f0 * fx - half * rx, y, mz + f0 * fz - half * rz, q[0]);
            toCam(mx + f0 * fx + half * rx, y, mz + f0 * fz + half * rz, q[1]);
            toCam(mx + f1 * fx + half * rx, y, mz + f1 * fz + half * rz, q[2]);
            toCam(mx + f1 * fx - half * rx, y, mz + f1 * fz - half * rz, q[3]);
            drawPoly(buf, w, h, q, 4, fog(STRIPE_COL, depth));
        }
    }
}

// A colour gate's bars along one edge of its cell: two strips in its
// colour, bright to a ball of the other colour (shut), dark to its own
// (open).
void RollFluxGame::drawGateBars(uint16_t* buf, int w, int h, int c, int r, int dir, float depth) {
    const float x0 = cellX0(c), x1 = x0 + CELL, z0 = cellZ0(r), z1 = z0 + CELL;
    const float base = (float)(_cells[r][c].h * HEIGHT_STEP);
    float ax, az, bx, bz;
    switch (dir) {
        case D_N: ax = x0; az = z1; bx = x1; bz = z1; break;
        case D_S: ax = x0; az = z0; bx = x1; bz = z0; break;
        case D_E: ax = x1; az = z0; bx = x1; bz = z1; break;
        default:  ax = x0; az = z0; bx = x0; bz = z1; break;
    }
    const int colour = needsColour(c, r);
    const uint16_t col = colour == _polarity ? shade565(POLE_COL[colour][1], 0.45f) : POLE_COL[colour][(millis() / 200) & 1];
    for (int bar = 0; bar < 2; ++bar) {
        const float y0 = base + GATE_HEIGHT * (bar ? 0.70f : 0.30f), y1 = y0 + GATE_HEIGHT * 0.18f;
        float q[4][3];
        toCam(ax, y0, az, q[0]);
        toCam(bx, y0, bz, q[1]);
        toCam(bx, y1, bz, q[2]);
        toCam(ax, y1, az, q[3]);
        drawPoly(buf, w, h, q, 4, fog(col, depth));
    }
}

// A bumper: an eight-sided red post with a pale top, white when it's just
// knocked the ball.
void RollFluxGame::drawBumper(uint16_t* buf, int w, int h, int c, int r) {
    const float mx = cellX0(c) + CELL * 0.5f, mz = cellZ0(r) + CELL * 0.5f;
    const float y0 = _cells[r][c].h * HEIGHT_STEP, y1 = y0 + BUMPER_HEIGHT;
    const bool lit = c == _bumpC && r == _bumpR && (long)(millis() - _bumpUntil) < 0;
    float lo[8][3], hi[8][3];
    for (int i = 0; i < 8; ++i) {
        const float a = i * (float)PI / 4;
        toCam(mx + cosf(a) * BUMPER_RADIUS, y0, mz + sinf(a) * BUMPER_RADIUS, lo[i]);
        toCam(mx + cosf(a) * BUMPER_RADIUS, y1, mz + sinf(a) * BUMPER_RADIUS, hi[i]);
    }
    float centre[3];
    toCam(mx, (y0 + y1) * 0.5f, mz, centre);
    for (int i = 0; i < 8; ++i) {
        const int j = (i + 1) & 7;
        const float fx = (lo[i][0] + lo[j][0]) * 0.5f, fz = (lo[i][2] + lo[j][2]) * 0.5f, fy = (lo[i][1] + hi[i][1]) * 0.5f;
        if ((fx - centre[0]) * fx + (fy - centre[1]) * fy + (fz - centre[2]) * fz >= 0) continue;
        float q[4][3];
        for (int e = 0; e < 3; ++e) { q[0][e] = lo[i][e]; q[1][e] = lo[j][e]; q[2][e] = hi[j][e]; q[3][e] = hi[i][e]; }
        drawPoly(buf, w, h, q, 4, fog(lit ? BUMPER_COL[2] : shade565(BUMPER_COL[0], 0.7f + 0.3f * (i & 1)), centre[2]));
    }
    drawPoly(buf, w, h, hi, 8, fog(lit ? BUMPER_COL[2] : BUMPER_COL[1], centre[2]));
}

// A box turned `ang` about the vertical (0 lies along z), halfLen along
// it, halfWid across, from y0 to y1: the faces turned to the camera, top
// last.
void RollFluxGame::drawBox(uint16_t* buf, int w, int h, float cx, float cz, float ang, float halfLen, float halfWid,
                           float y0, float y1, uint16_t top, uint16_t side, float depth) {
    const float ax = sinf(ang), az = cosf(ang), bx = az, bz = -ax;   // along, across
    const float sx[4] = { -1, 1, 1, -1 }, sz[4] = { -1, -1, 1, 1 };
    float lo[4][3], hi[4][3];
    for (int i = 0; i < 4; ++i) {
        const float x = cx + ax * halfLen * sz[i] + bx * halfWid * sx[i];
        const float z = cz + az * halfLen * sz[i] + bz * halfWid * sx[i];
        toCam(x, y0, z, lo[i]);
        toCam(x, y1, z, hi[i]);
    }
    float centre[3];
    toCam(cx, (y0 + y1) * 0.5f, cz, centre);
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) & 3;
        float fc[3];
        for (int e = 0; e < 3; ++e) fc[e] = (lo[i][e] + lo[j][e] + hi[i][e] + hi[j][e]) * 0.25f;
        if ((fc[0] - centre[0]) * fc[0] + (fc[1] - centre[1]) * fc[1] + (fc[2] - centre[2]) * fc[2] >= 0) continue;
        float q[4][3];
        for (int e = 0; e < 3; ++e) { q[0][e] = lo[i][e]; q[1][e] = lo[j][e]; q[2][e] = hi[j][e]; q[3][e] = hi[i][e]; }
        drawPoly(buf, w, h, q, 4, fog(shade565(side, (i & 1) ? 1.0f : 0.8f), depth));
    }
    if (_camera.position.y > y1) drawPoly(buf, w, h, hi, 4, fog(top, depth));
}

// A moving part: a platform or lift (orange, a cell square), a bridge
// (steel, a cell wide), a sweeper's arm (glowing red) with its pivot post.
void RollFluxGame::drawMover(uint16_t* buf, int w, int h, int k, float depth) {
    const MoverDef &m = courseDef().movers[k];
    const Mover &mv = _movers[k];
    switch (m.type) {
        case M_BRIDGE:
            drawBox(buf, w, h, mv.x, mv.z, mv.ang, m.len * CELL * 0.5f, BRIDGE_HALF_WIDTH,
                    mv.y - MOVER_THICK, mv.y, BRIDGE_COL[0], BRIDGE_COL[1], depth);
            break;
        case M_SWEEPER: {
            const float half = m.len * CELL * 0.5f;
            drawBox(buf, w, h, mv.x, mv.z, 0, 24.0f, 24.0f, mv.y, mv.y + SWEEPER_HIGH + 12.0f,
                    SWEEPER_COL[1], SWEEPER_COL[1], depth);
            drawBox(buf, w, h, mv.x + sinf(mv.ang) * half, mv.z + cosf(mv.ang) * half, mv.ang, half, SWEEPER_HALF_WIDTH,
                    mv.y + SWEEPER_LOW, mv.y + SWEEPER_HIGH, SWEEPER_COL[0], SWEEPER_COL[1], depth);
            break;
        }
        default:
            drawBox(buf, w, h, mv.x, mv.z, 0, PLATFORM_HALF, PLATFORM_HALF, mv.y - MOVER_THICK, mv.y,
                    PLATFORM_COL[0], PLATFORM_COL[1], depth);
            break;
    }
}

// A guardian's weak point: a big spinning diamond, dark red when shut,
// flashing white and yellow when open to a hit.
void RollFluxGame::drawNode(uint16_t* buf, int w, int h, int n) {
    const float s = NODE_RADIUS * 0.8f, spin = millis() * 0.006f;
    {
        const float mx = _gNodeX[n], my = _gNodeY[n], mz = _gNodeZ[n];
        float tip[2][3], ring[4][3], centre[3];
        toCam(mx, my + s * 1.4f, mz, tip[0]);
        toCam(mx, my - s * 1.4f, mz, tip[1]);
        for (int i = 0; i < 4; ++i) {
            const float a = spin + i * (float)PI / 2;
            toCam(mx + cosf(a) * s, my, mz + sinf(a) * s, ring[i]);
        }
        toCam(mx, my, mz, centre);
        const bool lit = n == _gLit;
        for (int half = 0; half < 2; ++half)
            for (int i = 0; i < 4; ++i) {
                float q[3][3];
                for (int e = 0; e < 3; ++e) { q[0][e] = tip[half][e]; q[1][e] = ring[i][e]; q[2][e] = ring[(i + 1) & 3][e]; }
                const float fx = (q[0][0] + q[1][0] + q[2][0]) / 3, fy = (q[0][1] + q[1][1] + q[2][1]) / 3,
                            fz = (q[0][2] + q[1][2] + q[2][2]) / 3;
                if ((fx - centre[0]) * fx + (fy - centre[1]) * fy + (fz - centre[2]) * fz >= 0) continue;
                const uint16_t col = lit ? NODE_COL[1 + (((millis() / 120) + i + half) & 1)] : shade565(NODE_COL[0], 0.8f + 0.3f * ((i + half) & 1));
                drawPoly(buf, w, h, q, 3, fog(col, centre[2]));
            }
    }
}

// A smashed crystal's shards: specks flying and falling.
void RollFluxGame::drawShards(uint16_t* buf, int w, int h) {
    if ((long)(millis() - _shardsUntil) >= 0) return;
    const float f = _camera.fovFactor;
    for (const Shard &s : _shards) {
        float p[3];
        toCam(s.x, s.y, s.z, p);
        if (p[2] < CAMERA_NEAR) continue;
        const int sx = (int)(w / 2 + p[0] * f / p[2]), sy = (int)(h / 2 - p[1] * f / p[2]);
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
                if (sx + dx >= 0 && sx + dx < w && sy + dy >= 0 && sy + dy < h) buf[(sy + dy) * w + sx + dx] = s.colour;
    }
}

// A rail: a low wall on a cell's edge, no thickness, seen from either side.
void RollFluxGame::drawRail(uint16_t* buf, int w, int h, int c, int r, int dir, float depth) {
    const float x0 = cellX0(c), x1 = x0 + CELL, z0 = cellZ0(r), z1 = z0 + CELL;
    const float y0 = (float)(_cells[r][c].h * HEIGHT_STEP), y1 = y0 + RAIL_HEIGHT;
    float ax, az, bx, bz;
    switch (dir) {
        case D_N: ax = x0; az = z1; bx = x1; bz = z1; break;
        case D_S: ax = x0; az = z0; bx = x1; bz = z0; break;
        case D_E: ax = x1; az = z0; bx = x1; bz = z1; break;
        default:  ax = x0; az = z0; bx = x0; bz = z1; break;
    }
    float q[4][3];
    toCam(ax, y0, az, q[0]);
    toCam(bx, y0, bz, q[1]);
    toCam(bx, y1, bz, q[2]);
    toCam(ax, y1, az, q[3]);
    drawPoly(buf, w, h, q, 4, fog(RAIL_COL[dir >= D_E], depth));
}

// The ball: its faces turned to the camera, each lit by the sun. Striped
// so its turn shows: gores in two colours, the poles capped. It flickers
// white charging a dash, and glows white dashing.
void RollFluxGame::drawBall(uint16_t* buf, int w, int h) {
    const BallMesh &mesh = ballMesh();
    const bool glow = (_charging && ((millis() / 80) & 1)) || (long)(millis() - _dashUntil) < 0;
    float cam[BALL_VERTS][3];
    for (int i = 0; i < BALL_VERTS; ++i) {
        const float* v = mesh.v[i];
        const float wx = _rot[0] * v[0] + _rot[1] * v[1] + _rot[2] * v[2];
        const float wy = _rot[3] * v[0] + _rot[4] * v[1] + _rot[5] * v[2];
        const float wz = _rot[6] * v[0] + _rot[7] * v[1] + _rot[8] * v[2];
        toCam(_bx + wx * BALL_RADIUS, _by + BALL_RADIUS + wy * BALL_RADIUS, _bz + wz * BALL_RADIUS, cam[i]);
    }
    float centre[3];
    toCam(_bx, _by + BALL_RADIUS, _bz, centre);
    for (int band = 0; band <= BALL_RINGS; ++band)
        for (int g = 0; g < BALL_LON; ++g) {
            int idx[4], n;
            if (band == 0) { idx[0] = 0; idx[1] = ringVert(1, g + 1); idx[2] = ringVert(1, g); n = 3; }
            else if (band == BALL_RINGS) {
                idx[0] = ringVert(band, g); idx[1] = ringVert(band, g + 1); idx[2] = BALL_VERTS - 1; n = 3;
            } else {
                idx[0] = ringVert(band, g); idx[1] = ringVert(band, g + 1);
                idx[2] = ringVert(band + 1, g + 1); idx[3] = ringVert(band + 1, g); n = 4;
            }
            // Facing the camera: the face's outward direction (from the
            // ball's centre) points back towards the eye.
            float fc[3] = { 0, 0, 0 }, lv[3] = { 0, 0, 0 };
            for (int k = 0; k < n; ++k)
                for (int e = 0; e < 3; ++e) { fc[e] += cam[idx[k]][e] / n; lv[e] += mesh.v[idx[k]][e] / n; }
            const float nx = fc[0] - centre[0], ny = fc[1] - centre[1], nz = fc[2] - centre[2];
            if (nx * fc[0] + ny * fc[1] + nz * fc[2] >= 0) continue;
            // Lit by the sun in the world: the face's direction turned with the ball.
            const float wx = _rot[0] * lv[0] + _rot[1] * lv[1] + _rot[2] * lv[2];
            const float wy = _rot[3] * lv[0] + _rot[4] * lv[1] + _rot[5] * lv[2];
            const float wz = _rot[6] * lv[0] + _rot[7] * lv[1] + _rot[8] * lv[2];
            const float len = sqrtf(wx * wx + wy * wy + wz * wz) + 1e-6f;
            const float lit = (wx * SUN[0] + wy * SUN[1] + wz * SUN[2]) / len;
            uint16_t base = (band == 0 || band == BALL_RINGS) ? BALL_COL[2] : BALL_COL[g & 1];
            if (base == BALL_COL[0] && _polarity == 1) base = BALL_MAGENTA;
            if (glow && (band == 0 || band == BALL_RINGS || (g & 1) == 0)) base = ArcadeConfig::COLOR_WHITE;
            float q[4][3];
            for (int k = 0; k < n; ++k)
                for (int e = 0; e < 3; ++e) q[k][e] = cam[idx[k]][e];
            drawPoly(buf, w, h, q, n, shade565(base, 0.45f + 0.6f * (lit > 0 ? lit : 0)));
        }
}

// The ball's shadow: a dark octagon on the floor under it, following the
// floor's height at each corner so it lies on a ramp too.
void RollFluxGame::drawShadow(uint16_t* buf, int w, int h, float floorY) {
    float q[8][3];
    const float rad = BALL_RADIUS * 0.85f;
    for (int i = 0; i < 8; ++i) {
        const float a = i * (float)PI / 4;
        const float x = _bx + cosf(a) * rad, z = _bz - sinf(a) * rad;
        float y;
        if (!floorAt(x, z, y)) y = floorY;
        toCam(x, y + 1.0f, z, q[i]);
    }
    drawPoly(buf, w, h, q, 8, 0, true);
}

// A gem: a spinning diamond (an octahedron) bobbing over its cell.
void RollFluxGame::drawGem(uint16_t* buf, int w, int h, int c, int r) {
    const float mx = cellX0(c) + CELL * 0.5f, mz = cellZ0(r) + CELL * 0.5f, my = gemY(c, r);
    const float s = 32.0f, spin = millis() * 0.004f;
    float tip[2][3], ring[4][3];
    toCam(mx, my + s * 1.4f, mz, tip[0]);
    toCam(mx, my - s * 1.4f, mz, tip[1]);
    for (int i = 0; i < 4; ++i) {
        const float a = spin + i * (float)PI / 2;
        toCam(mx + cosf(a) * s, my, mz + sinf(a) * s, ring[i]);
    }
    float centre[3];
    toCam(mx, my, mz, centre);
    for (int half = 0; half < 2; ++half)
        for (int i = 0; i < 4; ++i) {
            float q[3][3];
            for (int e = 0; e < 3; ++e) {
                q[0][e] = tip[half][e];
                q[1][e] = ring[i][e];
                q[2][e] = ring[(i + 1) & 3][e];
            }
            const float fx = (q[0][0] + q[1][0] + q[2][0]) / 3, fy = (q[0][1] + q[1][1] + q[2][1]) / 3,
                        fz = (q[0][2] + q[1][2] + q[2][2]) / 3;
            if ((fx - centre[0]) * fx + (fy - centre[1]) * fy + (fz - centre[2]) * fz >= 0) continue;
            drawPoly(buf, w, h, q, 3, fog(GEM_COL[(i + half) & 1], centre[2]));
        }
}

// Everything, far to near. The floor pieces (cells, and rails as their
// own pieces) are sorted by depth; the ball, its shadow and the gems
// (items) by theirs. A floor piece nearer than an item is drawn after it
// only if it could hide it: a rail standing above the item's bottom, or a
// cell whose top (its plane carried on to under the item) is above the
// item's bottom. Otherwise it's floor the item sits on or over, and goes
// first. So a ball rolling on a cell isn't painted over by that cell's
// near half, and a ball falling behind an edge goes behind it.
void RollFluxGame::drawWorld(GFXcanvas16 &canvas, bool withBall) {
    uint16_t* buf = canvas.getBuffer();
    const int w = canvas.width(), h = canvas.height();
    const float f = _camera.fovFactor;

    // Floor pieces in view: in front (or close enough to straddle the near
    // plane), within reach, and inside the frustum's sides with a cell's
    // margin.
    int count = 0;
    const float margin = CELL * 0.9f;
    for (int r = 0; r < _h; ++r)
        for (int c = 0; c < _w; ++c) {
            if (!present(c, r) || count >= MAX_DRAW - 4) continue;
            float p[3];
            const float base = (float)(_cells[r][c].h * HEIGHT_STEP);
            toCam(cellX0(c) + CELL * 0.5f, base, cellZ0(r) + CELL * 0.5f, p);
            if (p[2] < -margin || p[2] > VIEW_DIST) continue;
            const float zz = p[2] > 1.0f ? p[2] : 1.0f;
            if ((fabsf(p[0]) - margin) * f > (w / 2) * zz) continue;
            if ((fabsf(p[1]) - margin) * f > (h / 2) * zz) continue;
            _draw[count++] = DrawEntry{ (int16_t)p[2], P_CELL, (uint8_t)c, (uint8_t)r, 0, 0 };
            // A gate's bars, on each edge not shared with a gate of its colour.
            if (isGate(_cells[r][c].kind)) {
                static const int GDC[4] = { 0, 0, 1, -1 }, GDR[4] = { -1, 1, 0, 0 };
                static const float MX[4] = { 0.5f, 0.5f, 1.0f, 0.0f }, MZ[4] = { 1.0f, 0.0f, 0.5f, 0.5f };
                for (int d = 0; d < 4; ++d) {
                    const int nc = c + GDC[d], nr = r + GDR[d];
                    if (solid(nc, nr) && _cells[nr][nc].kind == _cells[r][c].kind) continue;
                    float q[3];
                    toCam(cellX0(c) + MX[d] * CELL, base + GATE_HEIGHT * 0.5f, cellZ0(r) + MZ[d] * CELL, q);
                    _draw[count++] = DrawEntry{ (int16_t)q[2], P_GATE, (uint8_t)c, (uint8_t)r, (uint8_t)d, 0 };
                }
            }
            if (!(_cells[r][c].flags & F_RAIL)) continue;
            for (int d = 0; d < 4; ++d) {
                if (!railed(c, r, d)) continue;
                static const float MX[4] = { 0.5f, 0.5f, 1.0f, 0.0f }, MZ[4] = { 1.0f, 0.0f, 0.5f, 0.5f };
                float q[3];
                toCam(cellX0(c) + MX[d] * CELL, base + RAIL_HEIGHT * 0.5f, cellZ0(r) + MZ[d] * CELL, q);
                _draw[count++] = DrawEntry{ (int16_t)q[2], P_RAIL, (uint8_t)c, (uint8_t)r, (uint8_t)d, 0 };
            }
        }
    // The floor-like moving parts (sliders, lifts, bridges) are floor pieces
    // too, at their middles.
    const CourseDef &def = courseDef();
    for (int k = 0; k < _moverCount && count < MAX_DRAW; ++k) {
        if (def.movers[k].type == M_SWEEPER) continue;
        float p[3];
        toCam(_movers[k].x, _movers[k].y, _movers[k].z, p);
        if (p[2] < -CELL * 2 || p[2] > VIEW_DIST) continue;
        _draw[count++] = DrawEntry{ (int16_t)p[2], P_MOVER, (uint8_t)k, 0, 0, 0 };
    }
    std::sort(_draw, _draw + count, [](const DrawEntry &a, const DrawEntry &b) { return a.z > b.z; });

    // Items in view. The ball blinks while it waits after a fall.
    int items = 0;
    float bc[3];
    toCam(_bx, _by + BALL_RADIUS, _bz, bc);
    const bool ballShown = withBall && bc[2] > -BALL_RADIUS && bc[2] < VIEW_DIST &&
                           ((long)(millis() - _holdUntil) >= 0 || ((millis() / 100) & 1));
    if (ballShown) {
        float fy;
        if (floorAt(_bx, _bz, fy) && _by >= fy - 1.0f && _by - fy < 600.0f) {
            float sc[3];
            toCam(_bx, fy, _bz, sc);
            _items[items++] = Item{ fmaxf(sc[2], bc[2] + 1.0f), fy, _bx, _bz, I_SHADOW, 0, 0 };
        }
        _items[items++] = Item{ bc[2], _by, _bx, _bz, I_BALL, 0, 0 };
    }
    // A guardian's weak points, as items.
    for (int i = 0; i < _gNodes && _gHp > 0 && items < MAX_ITEMS; ++i) {
        float p[3];
        toCam(_gNodeX[i], _gNodeY[i], _gNodeZ[i], p);
        if (p[2] < -CELL || p[2] > VIEW_DIST) continue;
        _items[items++] = Item{ p[2], _gNodeY[i] - NODE_RADIUS, _gNodeX[i], _gNodeZ[i], I_NODE, (uint8_t)i, 0 };
    }
    // Sweepers' arms, as items: at their middles, standing on their floor.
    for (int k = 0; k < _moverCount && items < MAX_ITEMS; ++k) {
        const MoverDef &m = def.movers[k];
        if (m.type != M_SWEEPER) continue;
        const Mover &mv = _movers[k];
        const float half = m.len * CELL * 0.5f;
        const float mx = mv.x + sinf(mv.ang) * half, mz = mv.z + cosf(mv.ang) * half;
        float p[3];
        toCam(mx, mv.y + SWEEPER_HIGH * 0.5f, mz, p);
        if (p[2] < -CELL * 2 || p[2] > VIEW_DIST) continue;
        _items[items++] = Item{ p[2], mv.y + SWEEPER_LOW, mx, mz, I_SWEEPER, (uint8_t)k, 0 };
    }
    for (int i = 0; i < count && items < MAX_ITEMS; ++i) {
        const DrawEntry &e = _draw[i];
        if (e.type != P_CELL) continue;
        if (_cells[e.r][e.c].kind == K_BUMPER) {
            const float bxx = cellX0(e.c) + CELL * 0.5f, bzz = cellZ0(e.r) + CELL * 0.5f;
            const float by0 = (float)(_cells[e.r][e.c].h * HEIGHT_STEP);
            float pc[3];
            toCam(bxx, by0 + BUMPER_HEIGHT * 0.5f, bzz, pc);
            _items[items++] = Item{ pc[2], by0, bxx, bzz, I_BUMPER, e.c, e.r };
            continue;
        }
        if ((_cells[e.r][e.c].flags & (F_GEM | F_TAKEN)) != F_GEM) continue;
        const float gx = cellX0(e.c) + CELL * 0.5f, gz = cellZ0(e.r) + CELL * 0.5f, gy = gemY(e.c, e.r);
        float gc[3];
        toCam(gx, gy, gz, gc);
        _items[items++] = Item{ gc[2], gy - 45.0f, gx, gz, I_GEM, e.c, e.r };
    }
    std::sort(_items, _items + items, [](const Item &a, const Item &b) { return a.depth > b.depth; });

    // Which item each floor piece must follow, then a stable sort by that.
    int slotCount[MAX_ITEMS + 1] = {};
    for (int i = 0; i < count; ++i) {
        DrawEntry &e = _draw[i];
        int slot = 0;
        for (int j = 0; j < items; ++j) {
            const Item &it = _items[j];
            if (e.z >= it.depth) continue;
            const bool hides = e.type == P_RAIL
                ? it.bottom < _cells[e.r][e.c].h * HEIGHT_STEP + RAIL_HEIGHT - 2.0f
                : e.type == P_GATE
                ? it.bottom < _cells[e.r][e.c].h * HEIGHT_STEP + GATE_HEIGHT - 2.0f
                : e.type == P_MOVER
                ? it.bottom < _movers[e.c].y - 20.0f
                : it.bottom < planeAt(e.c, e.r, it.x, it.z) - 20.0f;
            if (hides) slot = j + 1;
        }
        e.slot = (uint8_t)slot;
        ++slotCount[slot];
    }
    int start[MAX_ITEMS + 2];
    start[0] = 0;
    for (int s = 0; s <= items; ++s) start[s + 1] = start[s] + slotCount[s];
    int fill[MAX_ITEMS + 1];
    for (int s = 0; s <= items; ++s) fill[s] = start[s];
    for (int i = 0; i < count; ++i) _drawSorted[fill[_draw[i].slot]++] = _draw[i];

    for (int s = 0; s <= items; ++s) {
        for (int i = start[s]; i < start[s + 1]; ++i) {
            const DrawEntry &e = _drawSorted[i];
            if (e.type == P_CELL) drawCell(buf, w, h, e.c, e.r, (float)e.z);
            else if (e.type == P_GATE) drawGateBars(buf, w, h, e.c, e.r, e.dir, (float)e.z);
            else if (e.type == P_MOVER) drawMover(buf, w, h, e.c, (float)e.z);
            else drawRail(buf, w, h, e.c, e.r, e.dir, (float)e.z);
        }
        if (s == items) break;
        const Item &it = _items[s];
        switch (it.type) {
            case I_BALL:   if (_charging) drawAim(buf, w, h);
                           drawBall(buf, w, h);
                           break;
            case I_SHADOW: drawShadow(buf, w, h, it.bottom); break;
            case I_BUMPER: drawBumper(buf, w, h, it.c, it.r); break;
            case I_SWEEPER: drawMover(buf, w, h, it.c, it.depth); break;
            case I_NODE:    drawNode(buf, w, h, it.c); break;
            default:       drawGem(buf, w, h, it.c, it.r); break;
        }
    }
}

void RollFluxGame::renderFrame(GFXcanvas16 &canvas, bool withBall) {
    if (_skyWorld != courseDef().world) buildSky();
    computeCameraMatrix();
    drawSky(canvas);
    drawStars(canvas);
    drawWorld(canvas, withBall);
    drawShards(canvas.getBuffer(), canvas.width(), canvas.height());
}

// While a dash charges: a dashed line on the floor ahead of the ball, the
// way it'll go, flashing. Over the void it stays at the ball's height, so
// it shows where a jump's heading too. Drawn just before the ball (so not
// over it), with whatever's nearer still drawn over it.
void RollFluxGame::drawAim(uint16_t* buf, int w, int h) {
    const float px = -_aimZ * 12.0f, pz = _aimX * 12.0f;   // half its width, across it
    const uint16_t col = ((millis() / 120) & 1) ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_YELLOW;
    for (int i = 0; i < 4; ++i) {
        const float d0 = BALL_RADIUS + 20.0f + i * 95.0f, d1 = d0 + 60.0f;
        float q[4][3];
        const float ds[2] = { d0, d1 };
        for (int k = 0; k < 2; ++k) {
            const float x = _bx + _aimX * ds[k], z = _bz + _aimZ * ds[k];
            float y;
            if (!floorAt(x, z, y) || y > _by + STEP_UP) y = _by;
            toCam(x - px, y + 2.0f, z - pz, q[k == 0 ? 0 : 1]);
            toCam(x + px, y + 2.0f, z + pz, q[k == 0 ? 3 : 2]);
        }
        drawPoly(buf, w, h, q, 4, col);
    }
}

// The sky for this course's world.
void RollFluxGame::buildSky() {
    _skyWorld = courseDef().world;
    const int h = ArcadeConfig::LANDSCAPE_HEIGHT;
    for (int y = 0; y < h; ++y)
        _sky[y] = lerp565(SKY_COL[_skyWorld][1], SKY_COL[_skyWorld][0], (float)y / (float)(h - 1));
}

// The sky, the stars and the camera's lens, once (the sky again when the
// world changes).
void RollFluxGame::ensureReady(GFXcanvas16 &canvas) {
    if (_ready) return;
    _ready = true;
    buildSky();
    // Stars spread evenly over the sphere, from a fixed seed (not random(),
    // which the game's play uses).
    uint32_t seed = 12345;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    for (int i = 0; i < STAR_COUNT; ++i) {
        const float z = 2.0f * rnd() - 1.0f, t = 2.0f * (float)PI * rnd(), s = sqrtf(1.0f - z * z);
        _stars[i][0] = s * cosf(t);
        _stars[i][1] = z;
        _stars[i][2] = s * sinf(t);
        const uint8_t v = (uint8_t)(10 + rnd() * 21);
        _starCol[i] = rgb565(v, (uint8_t)(v * 2), v);
    }
    // Jet's Scene fills its trig tables when it's made; with no Scene here,
    // the camera's rotation needs them filled first.
    Renderer::initializeTrigTables();
    _camera.setFOV(CAMERA_FOV, canvas.width());
    _camera.nearPlane = CAMERA_NEAR;
    _camera.farPlane  = CAMERA_FAR;
    updateCamera(true);
}

}  // namespace rollflux
