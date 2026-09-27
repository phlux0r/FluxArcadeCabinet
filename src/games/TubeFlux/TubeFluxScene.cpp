#include "TubeFluxGame.h"
#include "TubeShipSprite.h"

namespace tubeflux {

namespace {

// Components are in RGB565's own ranges: r 0-31, g 0-63, b 0-31.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// A point on the tunnel wall (or a smaller concentric octagon), where
// `corner` counts octagon corners: corner j sits half a lane before lane j's
// centre, so lane j's panel runs from corner j to corner j + 1.
Vector3 cornerPoint(int corner, float radius, int32_t z) {
    float a = radians(((float)corner - 0.5f) * LANE_DEG);
    return Vector3{ (int32_t)lroundf(radius * sinf(a)),
                    (int32_t)lroundf(-radius * cosf(a)), z };
}

uint16_t addPoint(Renderer::Object* o, const Vector3 &p) {
    uint16_t n = (uint16_t)o->vertices.size();
    // Normals are unused: everything in this game is UNLIT.
    o->addVertex({ p, { 0, 0 }, { 0, FIXED_POINT_SCALE, 0 } });
    return n;
}

// t = 0 gives a, t = 1 gives b, per RGB565 channel.
uint16_t lerp565(uint16_t a, uint16_t b, float t) {
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((ar + (int)((br - ar) * t)) << 11) |
                      ((ag + (int)((bg - ag) * t)) << 5) |
                       (ab + (int)((bb - ab) * t)));
}

// Fills a convex polygon straight into an RGB565 buffer, sampling pixel
// centres with a half-open rule so neighbouring panels neither gap nor
// overlap. Vertices may lie far off screen (the nearest ring does); only
// the visible rows and columns are touched.
void fillConvex(uint16_t* buf, int w, int h, const float* xs, const float* ys, int n, uint16_t colour) {
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
            if ((sy < ya) == (sy < yb)) continue;   // edge doesn't cross this row
            float x = xs[i] + (sy - ya) * (xs[j] - xs[i]) / (yb - ya);
            if (x < xl) xl = x;
            if (x > xr) xr = x;
        }
        int xa = (int)ceilf(xl - 0.5f), xb = (int)ceilf(xr - 0.5f) - 1;
        if (xa < 0) xa = 0;
        if (xb > w - 1) xb = w - 1;
        uint16_t* row = buf + y * w;
        for (int x = xa; x <= xb; ++x) row[x] = colour;
    }
}

// Per tier, cycling: the colour change is the gate's visual cue.
struct Palette { uint16_t a, b; };
const Palette TIER_PALETTES[] = {
    { rgb565(14,  6, 20), rgb565(6,  3, 12) },   // violet
    { rgb565( 2, 22, 16), rgb565(1, 10,  8) },   // teal
    { rgb565(18,  6,  6), rgb565(8,  3,  4) },   // crimson
    { rgb565(20, 24,  3), rgb565(9, 10,  2) },   // amber
};

}  // namespace

void TubeFluxGame::buildBackdrop(int h) {
    // Only the far end of the tunnel shows the backdrop, and the fog fades
    // everything towards it, so it's the colour of "far away": near black.
    for (int y = 0; y < h; ++y) _backdrop[y] = rgb565(1, 1, 3);
}

void TubeFluxGame::applyTierPalette() {
    const Palette &p = TIER_PALETTES[(_tier - 1) % (int)(sizeof(TIER_PALETTES) / sizeof(TIER_PALETTES[0]))];
    _wallA = p.a;
    _wallB = p.b;
}

// Draws the tunnel into the canvas: RING_COUNT sections ahead of the
// camera, plus the partial one the camera is in, checkerboarded by ring and
// lane and darkened towards the backdrop with depth. The far end is left as
// backdrop. It projects exactly as Jet does (camera position and roll as
// placeCamera() sets them, then x * f / z from the centre), so the blocks
// Jet draws on top sit on these walls.
//
// Straight, no panel could hide another from inside the tube; bent, a far
// ring can swing behind a nearer wall, so sections are painted far to near.
// Nothing needs clipping beyond keeping every ring in front of the camera.
void TubeFluxGame::drawTunnel(GFXcanvas16 &canvas) {
    const int w = canvas.width(), h = canvas.height();
    uint16_t* buf = canvas.getBuffer();

    const float a = radians(_angle);
    const float camX = (float)lroundf(CAMERA_OFFSET * sinf(a));
    const float camY = (float)lroundf(-CAMERA_OFFSET * cosf(a));
    // Jet rotates the world by minus the camera's roll.
    const float roll = radians(-(float)lroundf(CAMERA_ROLL_SIGN * _angle));
    const float cr = cosf(roll), sr = sinf(roll);
    const float f = _camera.fovFactor;
    const float cx = (float)(w / 2), cy = (float)(h / 2);

    // Screen positions of each octagon corner on every ring. Ring 0 is just
    // in front of the camera; ring k > 0 is the k-th ring boundary ahead.
    // Each ring's centre is shifted by the bend at its depth.
    float ringX[RING_COUNT + 2][TUBE_SIDES], ringY[RING_COUNT + 2][TUBE_SIDES];
    float ringZ[RING_COUNT + 2];
    auto project = [&](float z, float* xs, float* ys) {
        const float s = f / z;
        float bx, by;
        bendOffset(z, bx, by);
        for (int j = 0; j < TUBE_SIDES; ++j) {
            float ca = radians(((float)j - 0.5f) * LANE_DEG);
            float x = TUBE_RADIUS * sinf(ca) + bx - camX;
            float y = -TUBE_RADIUS * cosf(ca) + by - camY;
            xs[j] = cx + (x * cr - y * sr) * s;
            ys[j] = cy - (x * sr + y * cr) * s;
        }
    };

    // Ring n sits at distance n * RING_SPACING along the run. The first one
    // ahead is n0; the section before it starts just in front of the camera.
    const long n0 = (long)floorf(_dist / (float)RING_SPACING) + 1;
    ringZ[0] = (float)CAMERA_NEAR;
    for (int k = 0; k <= RING_COUNT; ++k) ringZ[k + 1] = (float)(n0 + k) * RING_SPACING - _dist;
    for (int k = 0; k < RING_COUNT + 2; ++k) project(ringZ[k], ringX[k], ringY[k]);

    // Far to near, so where a bend puts a far ring behind a nearer wall on
    // screen, the nearer wall covers it. The hole at the far end goes first.
    fillConvex(buf, w, h, ringX[RING_COUNT + 1], ringY[RING_COUNT + 1], TUBE_SIDES, _backdrop[0]);
    for (int k = RING_COUNT; k >= 0; --k) {
        const long n = n0 + k;                       // this section ends at ring n
        float t = ((ringZ[k] + ringZ[k + 1]) * 0.5f - TUNNEL_FOG_NEAR) / (TUNNEL_FOG_FAR - TUNNEL_FOG_NEAR);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const uint16_t colA = lerp565(_wallA, _backdrop[0], t);
        const uint16_t colB = lerp565(_wallB, _backdrop[0], t);
        // A lane the drone is about to fire down flashes red along the
        // near stretch of wall (drone chase).
        const bool warnOn = _warnLanes && ((millis() / 90) & 1) &&
                            (ringZ[k] + ringZ[k + 1]) * 0.5f < DRONE_WARN_DEPTH;
        const uint16_t colWarn = lerp565(rgb565(31, 6, 2), _backdrop[0], t);
        const float *px = ringX[k], *py = ringY[k], *nx = ringX[k + 1], *ny = ringY[k + 1];
        for (int j = 0; j < TUBE_SIDES; ++j) {
            int j1 = (j + 1 == TUBE_SIDES) ? 0 : j + 1;
            float qx[4] = { px[j], px[j1], nx[j1], nx[j] };
            float qy[4] = { py[j], py[j1], ny[j1], ny[j] };
            uint16_t col = ((n - 1 + j) & 1) ? colB : colA;
            if (warnOn && (_warnLanes & (1u << j))) col = colWarn;
            fillConvex(buf, w, h, qx, qy, 4, col);
        }
    }
}

// A slab standing off the wall across `lanes` panels, starting at lane 0
// (rotated into place by placeObstacle()). Its inner face is a smaller
// concentric octagon, so it's parallel to the wall. The face against the
// wall and the back face are never seen, so they aren't built.
Renderer::Object* TubeFluxGame::buildBlock(int lanes) {
    auto* o = new Renderer::Object();
    const float apothem = TUBE_RADIUS * cosf(radians(LANE_DEG / 2));
    const float outerR = TUBE_RADIUS * 0.99f;   // just off the wall
    const float innerR = TUBE_RADIUS * (1.0f - BLOCK_HEIGHT / apothem);
    const int32_t zf = -BLOCK_DEPTH / 2, zb = BLOCK_DEPTH / 2;

    // Per corner: outer front, inner front, inner back, outer back.
    uint16_t of[MAX_BLOCK_LANES + 1], inf[MAX_BLOCK_LANES + 1];
    uint16_t inb[MAX_BLOCK_LANES + 1], ob[MAX_BLOCK_LANES + 1];
    for (int c = 0; c <= lanes; ++c) {
        of[c]  = addPoint(o, cornerPoint(c, outerR, zf));
        inf[c] = addPoint(o, cornerPoint(c, innerR, zf));
        inb[c] = addPoint(o, cornerPoint(c, innerR, zb));
        ob[c]  = addPoint(o, cornerPoint(c, outerR, zb));
    }
    for (int p = 0; p < lanes; ++p) {
        o->addFace(of[p], of[p + 1], inf[p + 1], inf[p], &_blockFrontMat);   // towards you
        o->addFace(inf[p], inf[p + 1], inb[p + 1], inb[p], &_blockTopMat);   // facing the axis
    }
    o->addFace(of[0], inf[0], inb[0], ob[0], &_blockSideMat);                  // the two ends
    o->addFace(of[lanes], ob[lanes], inb[lanes], inf[lanes], &_blockSideMat);
    o->calculateBoundingBox();
    // Seen from any side as you roll past, and winding is easy to get wrong
    // by hand; four quads a block is cheap enough not to cull.
    o->cullingMode = Renderer::CullingMode::NO_CULLING;
    o->preciseDepthSort = true;   // its own faces overlap, and sit close together
    o->enabled = false;
    return o;
}

// A crystal: a stretched octahedron standing on lane 0's panel (rotated into
// place like blocks). Spiky and hot-coloured, where blocks are boxy and
// grey: the shape says "shoot me" before the colour does.
Renderer::Object* TubeFluxGame::buildCrystal() {
    auto* o = new Renderer::Object();
    const float floorY = -TUBE_RADIUS * cosf(radians(LANE_DEG / 2));
    const float w = CRYSTAL_WIDTH * 0.5f;
    const float ye = floorY + CRYSTAL_HEIGHT * 0.4f;     // widest point
    const Vector3 v[6] = {
        { 0, (int32_t)(floorY + CRYSTAL_HEIGHT), 0 },     // tip, towards the axis
        { 0, (int32_t)(floorY + 4), 0 },                   // base, on the wall
        { (int32_t)w, (int32_t)ye, 0 }, { (int32_t)-w, (int32_t)ye, 0 },
        { 0, (int32_t)ye, (int32_t)w }, { 0, (int32_t)ye, (int32_t)-w },
    };
    uint16_t idx[6];
    for (int i = 0; i < 6; ++i) idx[i] = addPoint(o, v[i]);
    static const uint8_t tris[8][3] = { {0,2,5},{0,5,3},{0,3,4},{0,4,2},{1,5,2},{1,3,5},{1,4,3},{1,2,4} };
    for (int t = 0; t < 8; ++t) {
        o->addTriangle(idx[tris[t][0]], idx[tris[t][1]], idx[tris[t][2]],
                       (t & 1) ? &_crystalMatB : &_crystalMatA);
    }
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::NO_CULLING;
    o->preciseDepthSort = true;
    o->enabled = false;
    return o;
}

// The weapon pickup: two yellow chevrons one behind the other, pointing
// towards the axis, which is "up the screen" when it's in your lane: an
// arrow saying forward/fire. Built in lane 0 at flying height.
Renderer::Object* TubeFluxGame::buildPickup() {
    auto* o = new Renderer::Object();
    const float floorY = -TUBE_RADIUS * cosf(radians(LANE_DEG / 2));
    const int32_t cy = (int32_t)(floorY + FLY_HEIGHT);
    for (int32_t z : { -60, 60 }) {
        // Each chevron is two arms: apex (0, cy+70), ends (+/-90, cy-10), 36 thick.
        uint16_t a  = addPoint(o, { 0, cy + 70, z });
        uint16_t ai = addPoint(o, { 0, cy + 34, z });
        uint16_t l  = addPoint(o, { -90, cy - 10, z });
        uint16_t li = addPoint(o, { -90, cy - 46, z });
        uint16_t r  = addPoint(o, { 90, cy - 10, z });
        uint16_t ri = addPoint(o, { 90, cy - 46, z });
        o->addFace(a, l, li, ai, &_pickupMat);
        o->addFace(a, ai, ri, r, &_pickupMat);
    }
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::NO_CULLING;
    o->enabled = false;
    return o;
}

// The shield pickup: a green plus, two layers deep like the chevron, facing
// you at flying height in lane 0. Deliberately the repair cross's shape from
// Tank Flux: same meaning, same look across the cabinet.
Renderer::Object* TubeFluxGame::buildCross() {
    auto* o = new Renderer::Object();
    const float floorY = -TUBE_RADIUS * cosf(radians(LANE_DEG / 2));
    const int32_t cy = (int32_t)(floorY + FLY_HEIGHT + 20);
    const int32_t arm = 70, half = 20;
    for (int32_t z : { -50, 50 }) {
        // Vertical bar, then horizontal bar.
        uint16_t a = addPoint(o, { -half, cy + arm, z }), b = addPoint(o, { half, cy + arm, z });
        uint16_t c = addPoint(o, { half, cy - arm, z }),  d = addPoint(o, { -half, cy - arm, z });
        o->addFace(a, b, c, d, &_crossMat);
        a = addPoint(o, { -arm, cy + half, z }); b = addPoint(o, { arm, cy + half, z });
        c = addPoint(o, { arm, cy - half, z });  d = addPoint(o, { -arm, cy - half, z });
        o->addFace(a, b, c, d, &_crossMat);
    }
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::NO_CULLING;
    o->enabled = false;
    return o;
}

// The drone, built flying over lane 0 and centred on its own origin, so
// placeDrone() can put it at any angle and height. You only ever see it
// from behind, so it's designed from behind: a broad armoured hexagonal
// tail plate, wider at the top, with two engines glowing on it, tapering
// forward to a nose. A flat wedge read as a 5-pixel sliver edge-on; this
// is ~26 x 11 pixels at DRONE_AHEAD_Z.
Renderer::Object* TubeFluxGame::buildDrone() {
    auto* o = new Renderer::Object();
    const int32_t zr = -110;                              // tail plate
    static const int16_t hex[6][2] = { { -195, 25 }, { -115, 90 }, { 115, 90 },
                                       { 195, 25 }, { 115, -75 }, { -115, -75 } };
    uint16_t rim[6];
    for (int i = 0; i < 6; ++i) rim[i] = addPoint(o, { hex[i][0], hex[i][1], zr });
    uint16_t c    = addPoint(o, { 0, 8, zr });            // tail centre
    uint16_t nose = addPoint(o, { 0, 10, 230 });
    for (int i = 0; i < 6; ++i) {
        int j = (i + 1) % 6;
        // Tail plate: the face you see, dark armour.
        o->addTriangle(c, rim[i], rim[j], &_droneDarkMat);
        // Sides to the nose: upper ones catch the light.
        bool upper = hex[i][1] > 0 && hex[j][1] > 0;
        o->addTriangle(rim[i], nose, rim[j], upper ? &_droneTopMat : &_droneHullMat);
    }
    // A red band round the tail plate's edge, just proud of it, so the
    // silhouette reads against dark walls.
    for (int i = 0; i < 6; ++i) {
        int j = (i + 1) % 6;
        uint16_t a0 = addPoint(o, { hex[i][0], hex[i][1], zr - 2 });
        uint16_t a1 = addPoint(o, { hex[j][0], hex[j][1], zr - 2 });
        uint16_t b1 = addPoint(o, { (int32_t)(hex[j][0] * 0.8f), (int32_t)(hex[j][1] * 0.8f + 2), zr - 2 });
        uint16_t b0 = addPoint(o, { (int32_t)(hex[i][0] * 0.8f), (int32_t)(hex[i][1] * 0.8f + 2), zr - 2 });
        o->addFace(a0, a1, b1, b0, &_droneHullMat);
    }
    // Two engines on the tail plate.
    for (int32_t ex : { -70, 70 }) {
        uint16_t e0 = addPoint(o, { ex - 42, 40, zr - 4 }), e1 = addPoint(o, { ex + 42, 40, zr - 4 });
        uint16_t e2 = addPoint(o, { ex + 42, -20, zr - 4 }), e3 = addPoint(o, { ex - 42, -20, zr - 4 });
        o->addFace(e0, e1, e2, e3, &_droneEngineMat);
    }
    o->calculateBoundingBox();
    o->cullingMode = Renderer::CullingMode::NO_CULLING;
    o->preciseDepthSort = true;
    o->enabled = false;
    return o;
}

// The camera sits CAMERA_OFFSET out from the axis towards the ship and
// rolls with it, so the ship's lane is always straight down the screen.
void TubeFluxGame::placeCamera() {
    float a = radians(_angle);
    _camera.setPosition((int32_t)lroundf(CAMERA_OFFSET * sinf(a)),
                        (int32_t)lroundf(-CAMERA_OFFSET * cosf(a)), 0);
    _camera.setRotation(0, 0, (int32_t)lroundf(CAMERA_ROLL_SIGN * _angle));
}

void TubeFluxGame::updateShipSprite() {
    // Roll right: the right wing dips. Left is the same frame, mirrored.
    if (_rollVel > BANK_THRESHOLD) {
        _shipSprite.material = &_shipBankMat;
        _shipSprite.textureFlags = 0;
    } else if (_rollVel < -BANK_THRESHOLD) {
        _shipSprite.material = &_shipBankMat;
        _shipSprite.textureFlags = Renderer::Sprite2D::FLIP_X;
    } else {
        _shipSprite.material = &_shipLevelMat;
        _shipSprite.textureFlags = 0;
    }
    // Blink while invulnerable after a hit.
    bool invuln = (long)(millis() - _invulnUntil) < 0;
    _shipSprite.enabled = !(invuln && ((millis() / 90) & 1));
}

// Jet's Scene doesn't own what's added to it, so delete it all here.
void TubeFluxGame::releaseScene() {
    if (!_scene) return;
    for (Renderer::Object* obj : _scene->getObjects()) delete obj;
    delete _scene;
    _scene = nullptr;
    delete _shipLevelTex;
    delete _shipBankTex;
    _shipLevelTex = _shipBankTex = nullptr;
    for (auto &o : _obstacles) { o.obj = nullptr; o.active = false; }
    for (auto &o : _crystals) { o.obj = nullptr; o.active = false; }
    for (auto &s : _shots) { s.obj = nullptr; s.active = false; }
    _chevronObj = _crossObj = nullptr;
    _droneObj = nullptr;
    for (auto &b : _bolts) { b.obj = nullptr; b.active = false; }
    _chase = CHASE_NONE;
    _pickupActive = false;
    _phase = PHASE_ATTRACT;   // nothing may touch the (now missing) objects before a new game
}

void TubeFluxGame::ensureSceneReady(GFXcanvas16 &canvas) {
    if (_scene) return;

    _scene = new Renderer::Scene(canvas.getBuffer(), nullptr, canvas.width(), canvas.height());
    // drawTunnel() covers every pixel before Jet renders, so Jet mustn't clear.
    _scene->setClearBuffer(false);
    buildBackdrop(canvas.height());

    _camera.setFOV(CAMERA_FOV, canvas.width());
    _camera.nearPlane = CAMERA_NEAR;
    _camera.farPlane  = CAMERA_FAR;
    _scene->setCamera(&_camera);

    for (Renderer::Material* m : { &_blockFrontMat, &_blockTopMat, &_blockSideMat,
                                   &_crystalMatA, &_crystalMatB, &_shotMat, &_pickupMat, &_crossMat,
                                   &_droneHullMat, &_droneTopMat, &_droneDarkMat, &_droneEngineMat, &_boltMat,
                                   &_shipLevelMat, &_shipBankMat }) {
        m->shadingMode = Renderer::ShadingMode::UNLIT;
    }
    _blockFrontMat.color = rgb565(10, 22, 14);   // gunmetal: darker than the ship, so they never blend
    _blockTopMat.color   = rgb565(26, 54, 28);   // bright top edge: the silhouette you read at speed
    _blockSideMat.color  = rgb565(5, 12, 8);
    _crystalMatA.color   = rgb565(31, 36, 4);    // hot orange
    _crystalMatB.color   = rgb565(26, 18, 2);    // deeper, so the facets show
    _shotMat.color       = rgb565(12, 60, 31);   // cyan bolt
    _pickupMat.color     = rgb565(31, 60, 4);    // yellow; flashes white (updatePickup)
    _boltMat.color       = rgb565(31, 10, 6);    // drone bolts: hot red, not your cyan
    applyTierPalette();

    // Slot widths cycle 1,2,3: with the pool sized well past what's ever on
    // screen at once, every width is always available.
    for (int i = 0; i < OBSTACLE_POOL; ++i) {
        Obstacle &o = _obstacles[i];
        o.lanes = 1 + i % MAX_BLOCK_LANES;
        o.obj = buildBlock(o.lanes);
        o.active = false;
        _scene->addObject(o.obj);
    }

    for (auto &c : _crystals) {
        c.lanes = 1;
        c.crystal = true;
        c.obj = buildCrystal();
        c.active = false;
        _scene->addObject(c.obj);
    }
    for (auto &s : _shots) {
        // Big for a bolt: at 1000 units a pixel is ~10 units, and it has to read.
        s.obj = Primitives::createCube(40, 40, 260, &_shotMat);
        s.obj->enabled = false;
        s.active = false;
        _scene->addObject(s.obj);
    }
    _chevronObj = buildPickup();
    _scene->addObject(_chevronObj);
    _crossObj = buildCross();
    _scene->addObject(_crossObj);
    _droneObj = buildDrone();
    _scene->addObject(_droneObj);
    for (auto &b : _bolts) {
        b.obj = Primitives::createCube(44, 44, (int32_t)DRONE_BOLT_DEPTH, &_boltMat);
        b.obj->enabled = false;
        _scene->addObject(b.obj);
    }

    _shipLevelTex = new Renderer::Texture(SHIP_W, SHIP_H, const_cast<uint16_t*>(SHIP_LEVEL), true, 0x0000);
    _shipBankTex  = new Renderer::Texture(SHIP_W, SHIP_H, const_cast<uint16_t*>(SHIP_BANK),  true, 0x0000);
    _shipLevelMat.diffuseMap = _shipLevelTex;
    _shipBankMat.diffuseMap  = _shipBankTex;
    _shipSprite.material = &_shipLevelMat;
    _shipSprite.x = (canvas.width() - SHIP_W) / 2;
    _shipSprite.y = SHIP_SCREEN_Y;
    _shipSprite.zOrder = 10;
    _shipSprite.enabled = false;   // only while playing
    _scene->addSprite(&_shipSprite);

    placeCamera();
}

}  // namespace tubeflux
