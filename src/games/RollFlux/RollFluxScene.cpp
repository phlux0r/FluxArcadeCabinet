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

// Fills a convex polygon straight into an RGB565 buffer, sampling pixel
// centres with a half-open rule so neighbouring faces neither gap nor
// overlap (Tube Flux's, unchanged).
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
            if ((sy < ya) == (sy < yb)) continue;
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

// The palette: the floor gets lighter with height, so levels read apart;
// ramps are amber; sides dark; the goal gold.
const uint16_t FLOOR_COL[4][2] = {
    { rgb565( 5, 30,  9), rgb565( 4, 23,  7) },
    { rgb565( 9, 40, 13), rgb565( 7, 31, 10) },
    { rgb565(13, 48, 17), rgb565(10, 38, 13) },
    { rgb565(17, 56, 21), rgb565(13, 45, 16) },
};
const uint16_t RAMP_COL[2] = { rgb565(26, 42, 6), rgb565(21, 33, 4) };
const uint16_t GOAL_COL[2] = { rgb565(31, 58, 8), rgb565(28, 48, 4) };
const uint16_t SIDE_COL[2] = { rgb565(4, 14, 8), rgb565(3, 10, 6) };   // north/south faces, east/west

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
        default: break;
    }
}

// The four sides of a cell, as (dc, dr, our two corners on that edge, the
// neighbour's two on it, the side's colour), walking each edge in the same
// order for both.
namespace {
struct Edge { int dc, dr, a, b, na, nb, col; };
const Edge EDGES[4] = {
    {  0,  1, 0, 1, 3, 2, 0 },   // south: our sw, se against its nw, ne
    {  0, -1, 2, 3, 1, 0, 0 },   // north: our ne, nw against its se, sw
    {  1,  0, 1, 2, 0, 3, 1 },   // east:  our se, ne against its sw, nw
    { -1,  0, 3, 0, 2, 1, 1 },   // west:  our nw, sw against its ne, se
};
}  // namespace

// A cell's top (side < 0) or one of its sides (0 north/south, 1 east/west)
// in the direct renderer, faded towards the sky with depth.
uint16_t RollFluxGame::cellColour(int c, int r, int side, float depth) const {
    const Cell &k = _cells[r][c];
    const int chk = (c + r) & 1;
    uint16_t col;
    if (side >= 0) col = SIDE_COL[side];
    else if (k.kind == K_GOAL) col = GOAL_COL[chk];
    else if (k.kind >= K_RAMP_N) col = RAMP_COL[chk];
    else col = FLOOR_COL[k.h > 3 ? 3 : k.h][chk];
    if (depth > FOG_NEAR) {
        float t = (depth - FOG_NEAR) / (FOG_FAR - FOG_NEAR);
        col = lerp565(col, _sky[ArcadeConfig::LANDSCAPE_HEIGHT - 1], t > 1.0f ? 1.0f : t);
    }
    return col;
}

// --- The Jet floor -----------------------------------------------------------

// The course as meshes, CHUNK_CELLS square: each cell's top, and a side
// wherever it stands above its neighbour (or the void, SIDE_DEPTH down).
// Vertices are in world space, so the chunks sit at the origin.
void RollFluxGame::buildChunks() {
    auto &objs = _scene->getObjects();
    for (int i = 0; i < _chunkCount; ++i) {
        objs.erase(std::remove(objs.begin(), objs.end(), _chunks[i]), objs.end());
        delete _chunks[i];
        _chunks[i] = nullptr;
    }
    _chunkCount = 0;
    auto vert = [](Renderer::Object* o, float x, float y, float z) {
        uint16_t n = (uint16_t)o->vertices.size();
        o->addVertex({ Vector3{ (int32_t)x, (int32_t)y, (int32_t)z }, { 0, 0 }, { 0, FIXED_POINT_SCALE, 0 } });
        return n;
    };
    for (int r0 = 0; r0 < _h; r0 += CHUNK_CELLS)
        for (int c0 = 0; c0 < _w; c0 += CHUNK_CELLS) {
            auto* o = new Renderer::Object();
            for (int r = r0; r < r0 + CHUNK_CELLS && r < _h; ++r)
                for (int c = c0; c < c0 + CHUNK_CELLS && c < _w; ++c) {
                    if (!solid(c, r)) continue;
                    float hgt[4];
                    cornerHeights(c, r, hgt);
                    const float x0 = cellX0(c), x1 = x0 + CELL, z0 = cellZ0(r), z1 = z0 + CELL;
                    const float cx[4] = { x0, x1, x1, x0 }, cz[4] = { z0, z0, z1, z1 };
                    uint16_t top[4];
                    for (int i = 0; i < 4; ++i) top[i] = vert(o, cx[i], hgt[i], cz[i]);
                    const Cell &k = _cells[r][c];
                    const int chk = (c + r) & 1;
                    Renderer::Material* m = k.kind == K_GOAL ? &_goalMat[chk]
                                          : k.kind >= K_RAMP_N ? &_rampMat[chk]
                                          : &_floorMat[chk][k.h > 3 ? 3 : k.h];
                    o->addFace(top[0], top[3], top[2], top[1], m);
                    for (const Edge &e : EDGES) {
                        float nh[4];
                        const bool nsolid = solid(c + e.dc, r + e.dr);
                        if (nsolid) cornerHeights(c + e.dc, r + e.dr, nh);
                        const float ba = nsolid ? nh[e.na] : hgt[e.a] - SIDE_DEPTH;
                        const float bb = nsolid ? nh[e.nb] : hgt[e.b] - SIDE_DEPTH;
                        if (hgt[e.a] <= ba + 1 && hgt[e.b] <= bb + 1) continue;
                        uint16_t ta = top[e.a], tb = top[e.b];
                        uint16_t va = vert(o, cx[e.a], fminf(ba, hgt[e.a]), cz[e.a]);
                        uint16_t vb = vert(o, cx[e.b], fminf(bb, hgt[e.b]), cz[e.b]);
                        o->addFace(ta, tb, vb, va, &_sideMat[e.col]);
                    }
                }
            if (o->vertices.empty()) { delete o; continue; }
            o->calculateBoundingBox();
            o->cullingMode = Renderer::CullingMode::NO_CULLING;
            o->enabled = false;
            _scene->addObject(o);
            _chunkCX[_chunkCount] = (c0 + CHUNK_CELLS * 0.5f) * CELL;
            _chunkCZ[_chunkCount] = (_h - r0 - CHUNK_CELLS * 0.5f) * CELL;
            _chunks[_chunkCount++] = o;
        }
}

// Only chunks within reach, and only for the Jet floor; Jet's own frustum
// cull does the rest.
void RollFluxGame::placeChunks() {
    const float reach = VIEW_DIST + CHUNK_CELLS * CELL * 0.75f;
    for (int i = 0; i < _chunkCount; ++i) {
        const float dx = _chunkCX[i] - _camX, dz = _chunkCZ[i] - _camZ;
        _chunks[i]->enabled = !_direct && dx * dx + dz * dz < reach * reach;
    }
}

void RollFluxGame::placeBall() {
    _ballObj->position = Vector3{ (int32_t)lroundf(_bx), (int32_t)lroundf(_by + BALL_RADIUS),
                                  (int32_t)lroundf(_bz) };
}

// --- The direct floor ----------------------------------------------------------

// Jet's camera transform for this frame, composed as Scene::render() does
// (same trig tables, same fixed-point order), so what's drawn here lines up
// with what Jet draws on top.
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

void RollFluxGame::drawSky(GFXcanvas16 &canvas) {
    uint16_t* buf = canvas.getBuffer();
    const int w = canvas.width(), h = canvas.height();
    for (int y = 0; y < h; ++y) {
        uint16_t* row = buf + y * w;
        const uint16_t c = _sky[y];
        for (int x = 0; x < w; ++x) row[x] = c;
    }
}

// A polygon given in camera space: clipped to the near plane, projected
// as Jet projects (x * f / z from the centre, y up), filled.
void RollFluxGame::drawQuad(uint16_t* buf, int w, int h, const float (*p)[3], int n, uint16_t colour) {
    const float zn = (float)CAMERA_NEAR;
    float cl[8][3];
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
    float xs[8], ys[8];
    for (int i = 0; i < m; ++i) {
        const float s = f / cl[i][2];
        xs[i] = cx + cl[i][0] * s;
        ys[i] = cy - cl[i][1] * s;
    }
    fillConvex(buf, w, h, xs, ys, m, colour);
}

// Every cell in view, far to near (by its centre's depth): its sides that
// face the camera, then its top. Over the sky, so the void is just sky.
void RollFluxGame::drawFloorDirect(GFXcanvas16 &canvas) {
    computeCameraMatrix();
    uint16_t* buf = canvas.getBuffer();
    const int w = canvas.width(), h = canvas.height();
    const float f = _camera.fovFactor;
    const float camX = (float)_camera.position.x, camY = (float)_camera.position.y, camZ = (float)_camera.position.z;
    auto toCam = [&](float x, float y, float z, float* out) {
        const float px = x - camX, py = y - camY, pz = z - camZ;
        out[0] = _m[0] * px + _m[1] * py + _m[2] * pz;
        out[1] = _m[3] * px + _m[4] * py + _m[5] * pz;
        out[2] = _m[6] * px + _m[7] * py + _m[8] * pz;
    };

    // What's in view: in front (or close enough to straddle the near
    // plane), within reach, and inside the frustum's sides with a cell's
    // margin.
    int count = 0;
    const float margin = CELL * 0.9f;
    for (int r = 0; r < _h; ++r)
        for (int c = 0; c < _w; ++c) {
            if (!solid(c, r)) continue;
            float p[3];
            toCam(cellX0(c) + CELL * 0.5f, (float)(_cells[r][c].h * HEIGHT_STEP), cellZ0(r) + CELL * 0.5f, p);
            if (p[2] < -margin || p[2] > VIEW_DIST) continue;
            const float zz = p[2] > 1.0f ? p[2] : 1.0f;
            if ((fabsf(p[0]) - margin) * f > (w / 2) * zz) continue;
            if ((fabsf(p[1]) - margin) * f > (h / 2) * zz) continue;
            _drawList[count++] = DrawCell{ (int16_t)p[2], (uint8_t)c, (uint8_t)r };
        }
    std::sort(_drawList, _drawList + count, [](const DrawCell &a, const DrawCell &b) { return a.z > b.z; });

    for (int i = 0; i < count; ++i) {
        const int c = _drawList[i].c, r = _drawList[i].r;
        float hgt[4];
        cornerHeights(c, r, hgt);
        const float x0 = cellX0(c), x1 = x0 + CELL, z0 = cellZ0(r), z1 = z0 + CELL;
        const float cx[4] = { x0, x1, x1, x0 }, cz[4] = { z0, z0, z1, z1 };
        float top[4][3];
        for (int k = 0; k < 4; ++k) toCam(cx[k], hgt[k], cz[k], top[k]);
        const float depth = (float)_drawList[i].z;
        for (const Edge &e : EDGES) {
            // Only sides facing the camera: the camera is beyond the edge.
            const float ex = e.dc > 0 ? x1 : e.dc < 0 ? x0 : 0, ez = e.dr > 0 ? z0 : e.dr < 0 ? z1 : 0;
            if ((e.dc > 0 && camX <= ex) || (e.dc < 0 && camX >= ex) ||
                (e.dr > 0 && camZ >= ez) || (e.dr < 0 && camZ <= ez)) continue;
            float nh[4];
            const bool nsolid = solid(c + e.dc, r + e.dr);
            if (nsolid) cornerHeights(c + e.dc, r + e.dr, nh);
            const float ba = nsolid ? nh[e.na] : hgt[e.a] - SIDE_DEPTH;
            const float bb = nsolid ? nh[e.nb] : hgt[e.b] - SIDE_DEPTH;
            if (hgt[e.a] <= ba + 1 && hgt[e.b] <= bb + 1) continue;
            float q[4][3];
            for (int k = 0; k < 3; ++k) { q[0][k] = top[e.a][k]; q[1][k] = top[e.b][k]; }
            toCam(cx[e.b], fminf(bb, hgt[e.b]), cz[e.b], q[2]);
            toCam(cx[e.a], fminf(ba, hgt[e.a]), cz[e.a], q[3]);
            drawQuad(buf, w, h, q, 4, cellColour(c, r, e.col, depth));
        }
        drawQuad(buf, w, h, top, 4, cellColour(c, r, -1, depth));
    }
}

// --- Frame ----------------------------------------------------------------------

void RollFluxGame::renderFrame(GFXcanvas16 &canvas) {
    placeChunks();
    placeBall();
    if (_direct) {
        drawSky(canvas);
        drawFloorDirect(canvas);
        _scene->setClearBuffer(false);
    } else {
        _scene->setClearBuffer(true);     // to the sky gradient
    }
    _scene->render();
    _jetTris = _scene->lastFrameDrawnTriangles;
}

// Stage 0's readout: renderer, camera, render time (averaged over a
// second; it leaves out the display push), triangles Jet drew, and goals
// and falls so far.
void RollFluxGame::drawOverlay(GFXcanvas16 &canvas) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%s %s %lu.%lums %dt", _direct ? "DIRECT" : "JET",
             CAMERA_PRESETS[_preset].name, _renderAvgUs / 1000, (_renderAvgUs / 100) % 10, _jetTris);
    canvas.setFont();
    canvas.setTextSize(1);
    canvas.fillRect(0, 0, canvas.width(), 9, ArcadeConfig::COLOR_BLACK);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    canvas.setCursor(1, 1);
    canvas.print(buf);
    snprintf(buf, sizeof(buf), "G%ld F%ld", _goals, _falls);
    canvas.setTextColor(ArcadeConfig::COLOR_GREY);
    canvas.setCursor(1, canvas.height() - 8);
    canvas.print(buf);
    canvas.setCursor(70, canvas.height() - 8);
    canvas.print("A:CAM B:FLOOR");
}

// --- The scene --------------------------------------------------------------------

void RollFluxGame::ensureSceneReady(GFXcanvas16 &canvas) {
    if (_scene) return;
    _scene = new Renderer::Scene(canvas.getBuffer(), nullptr, canvas.width(), canvas.height());

    // The void: a dark sky, deepening towards the top.
    const int h = canvas.height();
    for (int y = 0; y < h && y < ArcadeConfig::LANDSCAPE_HEIGHT; ++y)
        _sky[y] = lerp565(rgb565(1, 2, 6), rgb565(8, 10, 18), (float)y / (float)(h - 1));
    _scene->backgroundGradientColors = _sky;

    _camera.setFOV(CAMERA_FOV, canvas.width());
    _camera.nearPlane = CAMERA_NEAR;
    _camera.farPlane  = CAMERA_FAR;
    _scene->setCamera(&_camera);
    _scene->setDirectionalLight(&_sun);
    _scene->setAmbientLight(&_amb);

    for (int k = 0; k < 2; ++k) {
        for (int lvl = 0; lvl < 4; ++lvl) {
            _floorMat[k][lvl].shadingMode = Renderer::ShadingMode::UNLIT;
            _floorMat[k][lvl].color = FLOOR_COL[lvl][k];
        }
        _rampMat[k].shadingMode = _goalMat[k].shadingMode = _sideMat[k].shadingMode = Renderer::ShadingMode::UNLIT;
        _rampMat[k].color = RAMP_COL[k];
        _goalMat[k].color = GOAL_COL[k];
        _sideMat[k].color = SIDE_COL[k];
    }
    _ballMat.shadingMode = Renderer::ShadingMode::FLAT;
    _ballMat.color = rgb565(20, 56, 31);

    buildChunks();
    _ballObj = Primitives::createSphere((int32_t)BALL_RADIUS, 8, &_ballMat);
    _scene->addObject(_ballObj);
    updateCamera(true);
}

// Jet's Scene doesn't own what's added to it, so delete it all here.
void RollFluxGame::releaseScene() {
    if (!_scene) return;
    for (Renderer::Object* obj : _scene->getObjects()) delete obj;
    delete _scene;
    _scene = nullptr;
    _ballObj = nullptr;
    for (auto &c : _chunks) c = nullptr;
    _chunkCount = 0;
}

}  // namespace rollflux
