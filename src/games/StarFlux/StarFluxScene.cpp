#include "StarFluxGame.h"
#include "StarShipSprite.h"

namespace starflux {

namespace {

// From 8-bit components, as the art tools write them.
inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// t = 0 gives a, t = 1 gives b, per RGB565 channel.
uint16_t lerp565(uint16_t a, uint16_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((ar + (int)((br - ar) * t)) << 11) |
                      ((ag + (int)((bg - ag) * t)) << 5) |
                       (ab + (int)((bb - ab) * t)));
}

uint16_t addPoint(Renderer::Object* o, float x, float y, float z) {
    uint16_t n = (uint16_t)o->vertices.size();
    o->addVertex({ { (int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)lroundf(z) }, { 0, 0 },
                   { 0, FIXED_POINT_SCALE, 0 } });
    return n;
}

// A triangle scaled from model units, as tools/star_ship_model.py has them.
struct P3 { float x, y, z; };
void modelTri(Renderer::Object* o, float k, P3 a, P3 b, P3 c, Renderer::Material* m) {
    uint16_t i = addPoint(o, a.x * k, a.y * k, a.z * k);
    uint16_t j = addPoint(o, b.x * k, b.y * k, b.z * k);
    uint16_t l = addPoint(o, c.x * k, c.y * k, c.z * k);
    o->addTriangle(i, j, l, m);
}

// A small deterministic generator for mesh shapes, so building the scene
// doesn't disturb random() (the host harness replays runs from a seed).
struct ShapeRng {
    uint32_t s;
    float next() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFF) / 65535.0f; }
};

}  // namespace

// Sky rows: near black at the top, deep violet towards the bottom.
void StarFluxGame::buildBackdrop(int h) {
    for (int y = 0; y < h; ++y) {
        float t = (float)y / (float)(h - 1);
        _sky[y] = lerp565(rgb(4, 3, 16), rgb(22, 8, 38), t * t);
    }
}

// A gas giant, lit from the upper right, with tilted bands: rendered once
// into _planet (0 = transparent) and blitted each frame.
void StarFluxGame::buildPlanet() {
    const float r = PLANET_D * 0.5f - 0.5f;
    const float lx = 0.55f, ly = -0.55f, lz = 0.63f;
    for (int y = 0; y < PLANET_D; ++y) {
        for (int x = 0; x < PLANET_D; ++x) {
            float dx = (x - r) / r, dy = (y - r) / r, d2 = dx * dx + dy * dy;
            uint16_t c = 0;
            if (d2 < 1.0f) {
                float dz = sqrtf(1.0f - d2);
                float lam = dx * lx + dy * ly + dz * lz;
                if (lam < 0) lam = 0;
                float lat = dy * 0.94f + dx * 0.34f;
                float band = 0.5f + 0.5f * sinf(lat * 11.0f + 1.4f * sinf(lat * 5.0f));
                uint16_t base = lerp565(rgb(150, 70, 60), rgb(226, 150, 90), band);
                c = lerp565(rgb(8, 4, 16), base, 0.08f + 0.92f * lam);
                // Lit rim.
                if (d2 > 0.86f && lam > 0.2f) c = lerp565(c, rgb(255, 190, 140), (d2 - 0.86f) * 5.0f);
                if (c == 0) c = 0x0001;
            }
            _planet[y * PLANET_D + x] = c;
        }
    }
}

// The enemy fighter: tools/star_ship_model.py's fighter_triangles(), in
// world units. A red dart with four forward-swept blades in an X, so it
// has a shape from behind as well as side on; nose along +Z.
Renderer::Object* StarFluxGame::buildFighter() {
    auto* o = new Renderer::Object();
    const float k = FIGHTER_SCALE;
    P3 nose{ 0, 0, 1.7f };
    P3 top{ 0, 0.45f, -0.9f }, rgt{ 0.45f, 0, -0.9f }, bot{ 0, -0.45f, -0.9f }, lft{ -0.45f, 0, -0.9f };
    modelTri(o, k, nose, rgt, top, &_fHullMat);
    modelTri(o, k, nose, top, lft, &_fHullMat);
    modelTri(o, k, nose, bot, rgt, &_fDarkMat);
    modelTri(o, k, nose, lft, bot, &_fDarkMat);
    modelTri(o, k, top, rgt, bot, &_fEngineMat);   // the engine: the whole tail
    modelTri(o, k, top, bot, lft, &_fEngineMat);
    // Four blades in an X.
    for (float sx : { -1.0f, 1.0f }) {
        for (float sy : { -1.0f, 1.0f }) {
            P3 tip{ sx * 1.6f, sy * 1.1f, 0.4f };
            modelTri(o, k, P3{ sx * 0.25f, sy * 0.25f, -0.2f }, tip, P3{ sx * 0.25f, sy * 0.25f, -0.85f }, &_fBladeMat);
            modelTri(o, k, P3{ sx * 1.25f, sy * 0.86f, 0.3f }, tip, P3{ sx * 1.3f, sy * 0.9f, 0.05f }, &_fTipMat);
        }
    }
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::NO_CULLING;   // the blades are single sheets
    o->preciseDepthSort = true;
    o->enabled = false;
    return o;
}

// An icosahedron with its corners pushed in and out, lit (FLAT) so it
// shades as it tumbles. Every face has its own three vertices, which FLAT
// lighting needs (computeFlatNormals), wound to face outwards.
Renderer::Object* StarFluxGame::buildRock(int seed, float radius, Renderer::Material* mat) {
    static const float P = 1.6180339f;
    static const float V[12][3] = { { -1, P, 0 }, { 1, P, 0 }, { -1, -P, 0 }, { 1, -P, 0 },
                                    { 0, -1, P }, { 0, 1, P }, { 0, -1, -P }, { 0, 1, -P },
                                    { P, 0, -1 }, { P, 0, 1 }, { -P, 0, -1 }, { -P, 0, 1 } };
    static const uint8_t F[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
                                      { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                                      { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
                                      { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    ShapeRng rng{ (uint32_t)seed * 2654435761u + 12345u };
    float pts[12][3];
    const float n = sqrtf(1.0f + P * P);
    for (int i = 0; i < 12; ++i) {
        float k = radius * (1.0f + (rng.next() - 0.5f) * 0.56f) / n;
        pts[i][0] = V[i][0] * k;
        pts[i][1] = V[i][1] * k * 0.85f;
        pts[i][2] = V[i][2] * k;
    }
    auto* o = new Renderer::Object();
    for (const auto &f : F) {
        const float* a = pts[f[0]];
        const float* b = pts[f[1]];
        const float* c = pts[f[2]];
        // Outward: the face normal points away from the centre.
        float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        bool out = nx * (a[0] + b[0] + c[0]) + ny * (a[1] + b[1] + c[1]) + nz * (a[2] + b[2] + c[2]) > 0;
        if (ROCK_WINDING_FLIP) out = !out;
        if (!out) { const float* t = b; b = c; c = t; }
        uint16_t i0 = addPoint(o, a[0], a[1], a[2]);
        uint16_t i1 = addPoint(o, b[0], b[1], b[2]);
        uint16_t i2 = addPoint(o, c[0], c[1], c[2]);
        o->addTriangle(i0, i1, i2, mat);
    }
    o->computeFlatNormals();
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::CULL_BACKFACES;
    o->enabled = false;
    return o;
}

// The boss hull, in world units. You meet it head on, so it's designed
// from the front (its nose points -Z): an armoured body, broad wing plates
// out to the cannons with red running lights, red horns above and
// mandibles below. The cannons, core and the core's shield plate are
// separate objects (they flash, spin and get destroyed on their own).
Renderer::Object* StarFluxGame::buildBossHull() {
    auto* o = new Renderer::Object();
    uint16_t F = addPoint(o, 0, 0, -450), B = addPoint(o, 0, 0, 500);
    uint16_t T = addPoint(o, 0, 190, 0), D = addPoint(o, 0, -150, 0);
    uint16_t L = addPoint(o, -280, 0, 0), R = addPoint(o, 280, 0, 0);
    o->addTriangle(F, T, R, &_bossTopMat);
    o->addTriangle(F, L, T, &_bossTopMat);
    o->addTriangle(B, R, T, &_bossDarkMat);
    o->addTriangle(B, T, L, &_bossDarkMat);
    o->addTriangle(F, R, D, &_bossSideMat);
    o->addTriangle(F, D, L, &_bossSideMat);
    o->addTriangle(B, D, R, &_bossDarkMat);
    o->addTriangle(B, L, D, &_bossDarkMat);
    for (float s : { -1.0f, 1.0f }) {
        // Wing: a front plate you see head on, and a top that shows from above.
        uint16_t a = addPoint(o, s * 200, 60, -120), b = addPoint(o, s * 450, 20, -60);
        uint16_t c = addPoint(o, s * 450, -50, -60), d = addPoint(o, s * 200, -60, -120);
        o->addFace(a, b, c, d, &_bossSideMat);
        uint16_t e = addPoint(o, s * 450, 20, 200), f = addPoint(o, s * 200, 60, 300);
        o->addFace(a, f, e, b, &_bossTopMat);
        uint16_t l0 = addPoint(o, s * 220, 22, -126), l1 = addPoint(o, s * 430, 2, -66);
        uint16_t l2 = addPoint(o, s * 430, -14, -66), l3 = addPoint(o, s * 220, 2, -126);
        o->addFace(l0, l1, l2, l3, &_bossLightMat);
        // Horn and mandible.
        uint16_t h0 = addPoint(o, s * 90, 160, -40), h1 = addPoint(o, s * 190, 110, -40);
        uint16_t h2 = addPoint(o, s * 280, 380, -280);
        o->addTriangle(h0, h1, h2, &_bossFinMat);
        uint16_t m0 = addPoint(o, s * 110, -120, -110), m1 = addPoint(o, s * 210, -70, -60);
        uint16_t m2 = addPoint(o, s * 240, -300, -340);
        o->addTriangle(m0, m1, m2, &_bossFinMat);
    }
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::NO_CULLING;
    o->preciseDepthSort = true;
    o->enabled = false;
    return o;
}

// An octahedron in two tones: the boss's cannons and core.
Renderer::Object* StarFluxGame::buildGem(float r, Renderer::Material* a, Renderer::Material* b) {
    auto* o = new Renderer::Object();
    uint16_t T = addPoint(o, 0, r, 0), D = addPoint(o, 0, -r, 0);
    uint16_t q[4] = { addPoint(o, r * 0.8f, 0, 0), addPoint(o, 0, 0, r * 0.8f),
                      addPoint(o, -r * 0.8f, 0, 0), addPoint(o, 0, 0, -r * 0.8f) };
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        o->addTriangle(T, q[i], q[j], a);   // upper faces bright, lower dark:
        o->addTriangle(D, q[j], q[i], b);   // reads as lit from above (and setFlash's a, b order)
    }
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::NO_CULLING;
    o->preciseDepthSort = true;
    o->enabled = false;
    return o;
}

// The hexagonal plate covering the core until both cannons are gone.
Renderer::Object* StarFluxGame::buildShieldPlate() {
    auto* o = new Renderer::Object();
    uint16_t c = addPoint(o, 0, 0, 0);
    uint16_t rim[6];
    for (int i = 0; i < 6; ++i) {
        float a = (float)i * (PI / 3.0f);
        rim[i] = addPoint(o, 170.0f * cosf(a), 150.0f * sinf(a), 30.0f);
    }
    for (int i = 0; i < 6; ++i) o->addTriangle(c, rim[i], rim[(i + 1) % 6], (i & 1) ? &_shieldMat : &_bossSideMat);
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::NO_CULLING;
    o->enabled = false;
    return o;
}

// Hit flash: every face of `o` white, or back to its own materials (faces
// alternate a, b as the builders lay them out; pass a twice if it has one).
void StarFluxGame::setFlash(Renderer::Object* o, bool flash, Renderer::Material* a, Renderer::Material* b) {
    if (!o) return;
    int i = 0;
    for (auto &t : o->triangles) {
        t.material = flash ? &_flashMat : ((i & 1) ? b : a);
        ++i;
    }
}

// The camera follows the ship part of the way round its box, and rolls a
// little with its bank.
void StarFluxGame::placeCamera() {
    _camX = roundf(_shipX * CAM_FOLLOW);
    _camY = roundf(_shipY * CAM_FOLLOW + CAM_RISE);
    _camRoll = roundf(CAMERA_ROLL_SIGN * _bank * CAM_BANK_DEG);
    _camera.setPosition((int32_t)_camX, (int32_t)_camY, 0);
    _camera.setRotation(0, 0, (int32_t)_camRoll);
    // Jet rotates the world by minus the camera's roll.
    const float roll = radians(-_camRoll);
    _rollCos = cosf(roll);
    _rollSin = sinf(roll);
}

// Screen position of a world point, exactly as Jet projects it (see
// placeCamera()), so what's drawn in 2D lines up with the meshes.
bool StarFluxGame::project(float x, float y, float z, float &sx, float &sy) const {
    if (z < (float)CAMERA_NEAR) return false;
    const float rx = x - _camX, ry = y - _camY;
    const float px = rx * _rollCos - ry * _rollSin;
    const float py = rx * _rollSin + ry * _rollCos;
    const float s = _camera.fovFactor / z;
    sx = ArcadeConfig::LANDSCAPE_WIDTH * 0.5f + px * s;
    sy = ArcadeConfig::LANDSCAPE_HEIGHT * 0.5f - py * s;
    return true;
}

void StarFluxGame::drawBackdrop(GFXcanvas16 &canvas) {
    switch (_stageNum) {
        case STAGE_PLANET: drawPlanet(canvas); break;
        case STAGE_TRENCH: drawTrench(canvas); break;
        case STAGE_CANYON: drawCanyon(canvas); break;
        default:           drawSpace(canvas); break;
    }
}

// Sky gradient and the distant planet. The planet is so far off it only
// shifts a little with the camera, and turns with its roll.
void StarFluxGame::drawSpace(GFXcanvas16 &canvas) {
    const int w = canvas.width(), h = canvas.height();
    uint16_t* buf = canvas.getBuffer();
    for (int y = 0; y < h; ++y) {
        uint16_t c = _sky[y];
        uint16_t* row = buf + y * w;
        for (int x = 0; x < w; ++x) row[x] = c;
    }
    float px = 50.0f - _camX * 0.02f, py = 34.0f - _camY * 0.02f;   // from the screen centre, y up
    float rx = px * _rollCos - py * _rollSin, ry = px * _rollSin + py * _rollCos;
    int x0 = (int)(w * 0.5f + rx) - PLANET_D / 2, y0 = (int)(h * 0.5f - ry) - PLANET_D / 2;
    for (int y = 0; y < PLANET_D; ++y) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h) continue;
        uint16_t* row = buf + sy * w;
        const uint16_t* src = _planet + y * PLANET_D;
        for (int x = 0; x < PLANET_D; ++x) {
            int sx = x0 + x;
            if (src[x] && sx >= 0 && sx < w) row[sx] = src[x];
        }
    }
}

// Space dust streaming past: each star is a short streak from where it
// was to where it is, brighter the nearer it gets.
void StarFluxGame::drawStars(GFXcanvas16 &canvas) {
    const float dz = FLY_SPEED * 1.6f * _frameScale;
    const bool moving = _phase != PHASE_ATTRACT || _attractSlide != SLIDE_TITLE;
    for (auto &s : _stars) {
        if (moving) s.z -= dz;
        if (s.z < 250.0f) {
            s.z += 7800.0f;
            s.x = (float)random(-2400, 2401) + _camX;
            s.y = (float)random(-1800, 1801) + _camY;
        }
        if (!starVisible(s.x, s.y)) continue;
        float x0, y0, x1, y1;
        if (!project(s.x, s.y, s.z, x1, y1)) continue;
        if (!project(s.x, s.y, s.z + FLY_SPEED * 3.0f, x0, y0)) continue;
        if (x1 < -2 || x1 > canvas.width() + 2 || y1 < -2 || y1 > canvas.height() + 2) continue;
        float t = 1.0f - s.z / 8000.0f;
        uint16_t c = lerp565(rgb(70, 70, 120), rgb(240, 244, 255), t);
        canvas.drawLine((int)x0, (int)y0, (int)x1, (int)y1, c);
    }
}

// Shield rings: silver hoops with lights chasing round them.
void StarFluxGame::drawRings(GFXcanvas16 &canvas) {
    for (const auto &r : _rings) {
        if (!r.active) continue;
        float sx, sy;
        if (!project(r.x, r.y, r.z, sx, sy)) continue;
        float rad = RING_R * pixelsPerUnit(r.z);
        if (rad < 1.5f) { canvas.drawPixel((int)sx, (int)sy, rgb(200, 210, 230)); continue; }
        int ir = (int)rad;
        canvas.drawCircle((int)sx, (int)sy, ir, rgb(225, 232, 245));
        if (ir > 3) canvas.drawCircle((int)sx, (int)sy, ir - 1, rgb(130, 140, 170));
        if (ir > 8) canvas.drawCircle((int)sx, (int)sy, ir + 1, rgb(90, 96, 120));
        float spin = (float)(millis() % 2000) * (2.0f * PI / 2000.0f);
        for (int i = 0; i < 6; ++i) {
            float a = spin + (float)i * (PI / 3.0f);
            int lx = (int)(sx + cosf(a) * rad), ly = (int)(sy + sinf(a) * rad);
            if (ir > 6) canvas.fillCircle(lx, ly, 1, rgb(255, 255, 200));
            else canvas.drawPixel(lx, ly, rgb(255, 255, 200));
        }
    }
}

// The rapid-fire pod: a spinning gold diamond with a white core.
void StarFluxGame::drawPod(GFXcanvas16 &canvas) {
    if (!_pod.active) return;
    float sx, sy;
    if (!project(_pod.x, _pod.y, _pod.z, sx, sy)) return;
    const float r = POD_R * pixelsPerUnit(_pod.z);
    if (r < 1.5f) { canvas.drawPixel((int)sx, (int)sy, rgb(255, 220, 60)); return; }
    const float spin = (float)(millis() % 900) * (2.0f * PI / 900.0f);
    const int x = (int)sx, y = (int)sy, h = (int)(r + 0.5f);
    const int hw = (int)(r * fabsf(cosf(spin)) + 0.5f);   // turning: the width comes and goes
    const bool front = cosf(spin) >= 0;
    const uint16_t face = front ? rgb(255, 200, 40) : rgb(200, 130, 20);
    if (hw > 0) {
        canvas.fillTriangle(x - hw, y, x, y - h, x + hw, y, face);
        canvas.fillTriangle(x - hw, y, x, y + h, x + hw, y, lerp565(face, 0, 0.3f));
    }
    canvas.drawLine(x, y - h, x, y + h, rgb(255, 250, 200));
    if (h > 4) canvas.fillCircle(x, y, h / 4, rgb(255, 255, 255));
    if (h > 3 && ((millis() / 100) & 1)) canvas.drawCircle(x, y, h + 2, rgb(255, 230, 120));
}

// Mines: pale spheres with eight spikes and a red light that blinks
// faster as they close in.
void StarFluxGame::drawMines(GFXcanvas16 &canvas) {
    for (const auto &r : _rocks) {
        if (!r.active || !r.mine) continue;
        float sx, sy;
        if (!project(r.x, r.y, r.z, sx, sy)) continue;
        const float ppu = pixelsPerUnit(r.z);
        const int rad = (int)(MINE_R * ppu + 0.5f);
        const int x = (int)sx, y = (int)sy;
        const bool flash = before(r.flashUntil);
        if (rad < 2) { canvas.drawPixel(x, y, rgb(200, 220, 240)); continue; }
        const float spin = (float)(millis() % 3000) * (2.0f * PI / 3000.0f);
        const uint16_t spike = rgb(170, 190, 215);
        for (int i = 0; i < 8; ++i) {
            const float a = spin + (float)i * (PI / 4.0f);
            canvas.drawLine(x, y, x + (int)(cosf(a) * (float)rad * 1.45f), y + (int)(sinf(a) * (float)rad * 1.45f), spike);
        }
        canvas.fillCircle(x, y, rad, flash ? rgb(255, 255, 255) : rgb(120, 140, 170));
        canvas.drawCircle(x, y, rad, rgb(210, 226, 244));
        const unsigned long period = r.z < 3000.0f ? 160 : 400;
        if ((millis() / period) & 1) canvas.fillCircle(x, y, rad / 3 > 0 ? rad / 3 : 1, rgb(255, 50, 50));
    }
}

// Twin green lasers, enemy shots (hot orange balls), and the bomb.
void StarFluxGame::drawShots(GFXcanvas16 &canvas) {
    const uint16_t beam = rgb(70, 255, 110), core = rgb(220, 255, 220);
    for (const auto &s : _shots) {
        if (!s.active) continue;
        for (float off : { -LASER_SPREAD, LASER_SPREAD }) {
            float x0, y0, x1, y1;
            if (!project(s.x + off, s.y, s.z, x0, y0)) continue;
            if (!project(s.x + off, s.y, s.z + SHOT_LEN, x1, y1)) continue;
            canvas.drawLine((int)x0, (int)y0, (int)x1, (int)y1, beam);
            canvas.drawLine((int)x0 + 1, (int)y0, (int)x1 + 1, (int)y1, core);
        }
    }
    for (const auto &e : _eshots) {
        if (!e.active) continue;
        float sx, sy;
        if (!project(e.x, e.y, e.z, sx, sy)) continue;
        float r = ESHOT_R * pixelsPerUnit(e.z);
        if (r < 1.0f) r = 1.0f;
        if (e.frost) {   // an ice shard: a pale diamond
            const int x = (int)sx, y = (int)sy, k = (int)(r * 1.3f + 0.5f) + 1;
            const uint16_t col = (millis() / 60) & 1 ? rgb(140, 230, 255) : rgb(230, 250, 255);
            canvas.fillTriangle(x - k, y, x, y - k, x + k, y, col);
            canvas.fillTriangle(x - k, y, x, y + k, x + k, y, col);
            continue;
        }
        canvas.fillCircle((int)sx, (int)sy, (int)(r + 0.5f), (millis() / 60) & 1 ? rgb(255, 90, 30) : rgb(255, 40, 80));
        canvas.fillCircle((int)sx, (int)sy, (int)(r * 0.45f), rgb(255, 240, 200));
    }
    if (_bombActive) {
        float sx, sy;
        if (project(_bombX, _bombY, _bombZ, sx, sy)) {
            float r = 45.0f * pixelsPerUnit(_bombZ);
            if (r < 1.5f) r = 1.5f;
            canvas.fillCircle((int)sx, (int)sy, (int)(r + 0.5f), (millis() / 50) & 1 ? rgb(120, 170, 255) : rgb(255, 255, 255));
        }
    }
}

// Explosion flashes: a hot core that burns out, inside an expanding rim.
void StarFluxGame::drawBlasts(GFXcanvas16 &canvas) {
    for (auto &b : _blasts) {
        if (!b.active) continue;
        unsigned long age = millis() - b.at;
        unsigned long life = b.size >= BOMB_RADIUS ? BLAST_MS * 2 : BLAST_MS;
        if (age >= life) { b.active = false; continue; }
        float t = (float)age / (float)life;
        float sx, sy;
        if (!project(b.x, b.y, b.z, sx, sy)) continue;
        float r = b.size * pixelsPerUnit(b.z) * (0.25f + 0.75f * sqrtf(t));
        if (r < 1.0f) r = 1.0f;
        uint16_t rim = lerp565(b.colour, rgb(40, 10, 30), t);
        canvas.drawCircle((int)sx, (int)sy, (int)r, rim);
        if (r > 4) canvas.drawCircle((int)sx, (int)sy, (int)r - 1, lerp565(rim, 0, 0.4f));
        if (t < 0.45f) {
            float cr = r * (0.8f - t * 1.4f);
            if (cr >= 1.0f) canvas.fillCircle((int)sx, (int)sy, (int)cr, lerp565(rgb(255, 255, 220), b.colour, t * 2.0f));
        }
    }
}

// Two sights on your line of fire, near and far, as in the classics: the
// lasers fly straight ahead, so they pass through both. Red when something
// is on that line.
void StarFluxGame::drawReticle(GFXcanvas16 &canvas) {
    const bool locked = targetOnLine(_shipX, _shipY);
    const uint16_t col = locked ? rgb(255, 70, 70) : rgb(90, 255, 120);
    const float zs[2] = { RETICLE_NEAR_Z, RETICLE_FAR_Z };
    const int size[2] = { 6, 4 };
    for (int i = 0; i < 2; ++i) {
        float sx, sy;
        if (!project(_shipX, _shipY, zs[i], sx, sy)) continue;
        int x = (int)sx, y = (int)sy, s = size[i], k = s / 2 + 1;
        canvas.drawFastHLine(x - s, y - s, k, col); canvas.drawFastVLine(x - s, y - s, k, col);
        canvas.drawFastHLine(x + s - k + 1, y - s, k, col); canvas.drawFastVLine(x + s, y - s, k, col);
        canvas.drawFastHLine(x - s, y + s, k, col); canvas.drawFastVLine(x - s, y + s - k + 1, k, col);
        canvas.drawFastHLine(x + s - k + 1, y + s, k, col); canvas.drawFastVLine(x + s, y + s - k + 1, k, col);
        if (i == 0) canvas.drawPixel(x, y, col);
    }
}

// Bank frame by how hard you're banking; left is right, mirrored. Blinks
// while invulnerable; hidden while shot down.
void StarFluxGame::updateShipSprite() {
    float a = fabsf(_bank);
    int frame = a >= BANK_HARD ? 2 : a >= BANK_FRAME ? 1 : 0;
    _shipSprite.material = &_shipMat[frame];
    _shipSprite.textureFlags = (frame && _bank < 0) ? Renderer::Sprite2D::FLIP_X : 0;
    float sx, sy;
    placeCamera();
    if (project(_shipX, _shipY, SHIP_Z, sx, sy)) {
        _shipSprite.x = (int)sx - SHIP_W / 2;
        _shipSprite.y = (int)sy - SHIP_H / 2;
    }
    bool shown = _phase != PHASE_GAMEOVER && _stage != STAGE_DOWN && !(_phase == PHASE_ATTRACT && !inDemo());
    bool blink = before(_invulnUntil) && _stage == STAGE_RUN && ((millis() / 90) & 1);
    _shipSprite.enabled = shown && !blink;
}

// Jet's Scene doesn't own what's added to it, so delete it all here.
void StarFluxGame::releaseScene() {
    if (!_scene) return;
    for (Renderer::Object* obj : _scene->getObjects()) delete obj;
    delete _scene;
    _scene = nullptr;
    for (auto*& t : _shipTex) { delete t; t = nullptr; }
    for (auto &f : _fighters) { f.obj = nullptr; f.active = false; }
    for (auto &r : _rocks) { r.obj = nullptr; r.active = false; }
    _bossHull = _coreObj = _shieldObj = nullptr;
    for (auto*& h : _bossHulls) h = nullptr;
    for (auto &b : _boxes) { b.obj = nullptr; b.active = false; }
    for (auto &t : _turrets) { t.obj = nullptr; t.active = false; }
    _cannonObj[0] = _cannonObj[1] = nullptr;
    _bossActive = false;
    _phase = PHASE_ATTRACT;   // nothing may touch the (now missing) objects before a new game
}

void StarFluxGame::ensureSceneReady(GFXcanvas16 &canvas) {
    if (_scene) return;

    _scene = new Renderer::Scene(canvas.getBuffer(), nullptr, canvas.width(), canvas.height());
    // drawBackdrop() covers every pixel before Jet renders, so Jet mustn't clear.
    _scene->setClearBuffer(false);
    buildBackdrop(canvas.height());
    buildPlanet();

    _camera.setFOV(CAMERA_FOV, canvas.width());
    _camera.nearPlane = CAMERA_NEAR;
    _camera.farPlane  = CAMERA_FAR;
    _scene->setCamera(&_camera);
    _scene->setDirectionalLight(&_sun);
    _scene->setAmbientLight(&_amb);

    for (Renderer::Material* m : { &_fHullMat, &_fDarkMat, &_fBladeMat, &_fTipMat, &_fEngineMat, &_flashMat,
                                   &_bossTopMat, &_bossSideMat, &_bossDarkMat, &_bossFinMat, &_bossLightMat,
                                   &_cannonMat, &_cannonMat2, &_coreMat, &_coreMat2, &_shieldMat,
                                   &_boxLightMat, &_turretMat, &_turretMat2,
                                   &_shipMat[0], &_shipMat[1], &_shipMat[2] }) {
        m->shadingMode = Renderer::ShadingMode::UNLIT;
    }
    // Colours as tools/star_ship_model.py's fighter.
    _fHullMat.color   = rgb(178, 26, 58);
    _fDarkMat.color   = rgb(96, 14, 40);
    _fBladeMat.color  = rgb(110, 84, 150);
    _fTipMat.color    = rgb(255, 60, 190);
    _fEngineMat.color = rgb(190, 255, 80);
    _flashMat.color   = rgb(255, 255, 255);
    _rockMat[0].color = rgb(150, 128, 110);
    _rockMat[1].color = rgb(120, 126, 145);
    for (auto &m : _rockMat) m.shadingMode = Renderer::ShadingMode::FLAT;
    for (Renderer::Material* m : { &_boxMat, &_crawlerMat, &_crawlerDarkMat, &_reactorMat, &_reactorDarkMat,
                                   &_walkerMat, &_walkerDarkMat }) {
        m->shadingMode = Renderer::ShadingMode::FLAT;
    }
    _crawlerMat.color     = rgb(120, 110, 84);
    _crawlerDarkMat.color = rgb(60, 56, 50);
    _reactorMat.color     = rgb(120, 128, 152);
    _reactorDarkMat.color = rgb(48, 52, 70);
    _walkerMat.color      = rgb(150, 170, 196);
    _walkerDarkMat.color  = rgb(56, 66, 90);
    _boxLightMat.color    = rgb(255, 70, 70);
    applyStagePalette();
    buildMountains();
    _bossTopMat.color   = rgb(120, 126, 150);
    _bossSideMat.color  = rgb(78, 80, 104);
    _bossDarkMat.color  = rgb(40, 40, 58);
    _bossFinMat.color   = rgb(170, 30, 60);
    _bossLightMat.color = rgb(255, 60, 70);
    _cannonMat.color    = rgb(255, 150, 30);
    _cannonMat2.color   = rgb(200, 80, 10);
    _coreMat.color      = rgb(255, 60, 200);
    _coreMat2.color     = rgb(160, 20, 130);
    _shieldMat.color    = rgb(150, 160, 190);

    for (auto &f : _fighters) {
        f.obj = buildFighter();
        _scene->addObject(f.obj);
    }
    for (int i = 0; i < ROCK_POOL; ++i) {
        Rock &r = _rocks[i];
        r.r = i < ROCK_BIG_SLOTS ? ROCK_BIG_R : ROCK_SMALL_R;
        r.obj = buildRock(i + 1, r.r, &_rockMat[i & 1]);
        _scene->addObject(r.obj);
    }
    _bossHulls[STAGE_BELT] = buildBossHull();
    _bossHulls[STAGE_PLANET] = buildCrawler();
    _bossHulls[STAGE_TRENCH] = buildReactor();
    _bossHulls[STAGE_CANYON] = buildWalker();
    for (auto* h : _bossHulls) _scene->addObject(h);
    _bossHull = _bossHulls[STAGE_BELT];
    for (auto &b : _boxes) {
        b.obj = buildObstacleBox();
        _scene->addObject(b.obj);
    }
    for (auto &t : _turrets) {
        t.obj = buildTurret();
        _scene->addObject(t.obj);
    }
    for (auto*& c : _cannonObj) {
        c = buildGem(CANNON_R, &_cannonMat, &_cannonMat2);
        _scene->addObject(c);
    }
    _coreObj = buildGem(CORE_R, &_coreMat, &_coreMat2);
    _scene->addObject(_coreObj);
    _shieldObj = buildShieldPlate();
    _scene->addObject(_shieldObj);

    static const uint16_t* const frames[3] = { SHIP_LEVEL, SHIP_BANK, SHIP_BANK_HARD };
    for (int i = 0; i < 3; ++i) {
        _shipTex[i] = new Renderer::Texture(SHIP_W, SHIP_H, const_cast<uint16_t*>(frames[i]), true, 0x0000);
        _shipMat[i].diffuseMap = _shipTex[i];
    }
    _shipSprite.material = &_shipMat[0];
    _shipSprite.zOrder = 10;
    _shipSprite.enabled = false;
    _scene->addSprite(&_shipSprite);

    for (auto &s : _stars) {
        s.x = (float)random(-2400, 2401);
        s.y = (float)random(-1800, 1801);
        s.z = (float)random(300, 8000);
    }
    placeCamera();
}

}  // namespace starflux
