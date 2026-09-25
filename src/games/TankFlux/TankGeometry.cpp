#include "TankGeometry.h"

namespace tankflux {

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Cosmetic height field for the terrain — two overlaid sine waves at
// different frequencies/phases, chosen only to avoid an obviously
// periodic single-wave look.
//
// Deliberately NOT sampled anywhere else: the tank's camera stays on a
// fixed EYE_HEIGHT plane (updateDriving()) and collision is a flat 2D
// distance test (blockedFor()), so a hill tall enough to matter can
// still visibly poke through the tank's fixed driving height near it.
// A real fix would mean sampling terrain height under the tank and
// every enemy each frame — real added cost for what's currently a
// Tier-1 cosmetic pass. Worth revisiting if hills are pushed taller
// still.
int32_t hillHeight(int32_t wx, int32_t wz) {
    float fx = (float)wx, fz = (float)wz;
    float h = 70.0f * sinf(fx * 0.0009f) * cosf(fz * 0.0011f)
            + 38.0f * sinf(fx * 0.0021f + 1.7f) * sinf(fz * 0.0017f);
    return (int32_t)h;
}

// Point-to-segment distance, used by placeCircle() to keep every placed
// circle clear of the river's own line (RIVER_X0/Z0 to RIVER_X1/Z1).
float distToSegment(float px, float pz, float x0, float z0, float x1, float z1) {
    float dx = x1 - x0, dz = z1 - z0;
    float lenSq = dx * dx + dz * dz;
    float t = (lenSq > 0.0001f) ? ((px - x0) * dx + (pz - z0) * dz) / lenSq : 0.0f;
    t = fmaxf(0.0f, fminf(1.0f, t));
    float cx = x0 + t * dx, cz = z0 + t * dz;
    float ex = px - cx, ez = pz - cz;
    return sqrtf(ex * ex + ez * ez);
}

bool inRiver(float x, float z) {
    return distToSegment(x, z, (float)RIVER_X0, (float)RIVER_Z0,
                         (float)RIVER_X1, (float)RIVER_Z1) < (float)RIVER_WIDTH / 2.0f;
}

// Same spacing and per-cell material alternation as
// Primitives::createGrid, but with real per-face geometry: playtest
// feedback was that the hills were barely visible even though the
// height field was there. The root cause wasn't amplitude, it was
// shading — the first version kept createGrid's convention of sharing
// each vertex between neighbouring cells with a hardcoded straight-up
// normal, which is correct for a genuinely flat plane but means a
// slope has NO shading cue at all: UNLIT ignores normals entirely, and
// even lit shading with a wrong-but-uniform normal can't show a bump.
//
// Fixed properly rather than just raising the amplitude further: each
// cell now gets its own 4 unique vertices (no sharing across cells)
// with a normal computed from the actual cross product of that cell's
// two edges, and the material is FLAT rather than UNLIT so the
// renderer actually uses it. FLAT rather than GOURAUD deliberately —
// with per-face (not shared) vertices, every vertex of a triangle
// already carries the same normal, so GOURAUD's per-vertex lighting
// would compute an identical result at up to 3x the cost.
//
// This does duplicate vertices relative to a shared-vertex grid (~4x),
// but that cost lands in Object::vertices (built once, not per frame)
// rather than Jet's render queue, which only scales with TRIANGLE
// count — unchanged at 242 — and is the thing that actually ran this
// hardware out of contiguous heap once.
//
// Built once at world origin and never repositioned (see the ARENA
// comment on GROUND_SIZE for why), so these coordinates ARE true world
// coordinates — buildRiverStrip() calls the same hillHeight() at the
// same world positions, so the river's surface always agrees with the
// terrain under it rather than needing to track a moving mesh.
Renderer::Object* buildTerrain(int32_t width, int32_t height, int32_t rows, int32_t cols,
                               Renderer::Material* matA, Renderer::Material* matB) {
    Renderer::Object* grid = new Renderer::Object();
    int32_t hw = width / 2, hh = height / 2;
    int32_t rowSpacing = height / rows, colSpacing = width / cols;

    for (int32_t r = 0; r < rows - 1; ++r) {
        for (int32_t c = 0; c < cols - 1; ++c) {
            int32_t x0 = c * colSpacing - hw,       z0 = r * rowSpacing - hh;
            int32_t x1 = (c + 1) * colSpacing - hw, z1 = (r + 1) * rowSpacing - hh;
            int32_t y00 = hillHeight(x0, z0), y10 = hillHeight(x1, z0);
            int32_t y11 = hillHeight(x1, z1), y01 = hillHeight(x0, z1);

            // Two edges of the cell: along +x (columns) and along +z
            // (rows). cross(edgeZ, edgeX) points up for a near-flat
            // surface (verified by construction: with dy terms at 0
            // its Y component reduces to +rowSpacing*colSpacing).
            //
            // SHADE_EXAGGERATION inflates the height deltas used ONLY
            // for this normal calculation — the actual vertex Y below
            // still uses the real y00/y10/y11/y01, so geometry/gameplay
            // are untouched. Verified numerically (not guessed): at
            // 1200-unit cell spacing, hillHeight()'s real slope tilts
            // the true normal by under 8 degrees at its steepest, which
            // after jetShadeBrightness's squared falloff produced a
            // brightness range of only ~161-210 (out of 285) across the
            // whole grid — checkerboard cells were all in the same
            // narrow midtone band, reading as uniformly flat no matter
            // how the material colours or ambient were tuned. An 8x
            // exaggeration here (same technique as normal-map bump
            // exaggeration) widens that to ~16-252, a real lit/shadow
            // split, while the hills themselves stay exactly as subtle
            // as before.
            const float SHADE_EXAGGERATION = 8.0f;
            float e1y = (float)(y10 - y00) * SHADE_EXAGGERATION;
            float e2y = (float)(y01 - y00) * SHADE_EXAGGERATION;
            float nx = -(float)rowSpacing * e1y;
            float ny =  (float)rowSpacing * (float)colSpacing;
            float nz = -(float)colSpacing * e2y;
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            float s = (len > 0.0001f) ? ((float)FIXED_POINT_SCALE / len) : 0.0f;
            Vector3 normal{ (int32_t)(nx * s), (int32_t)(ny * s), (int32_t)(nz * s) };

            uint16_t b = (uint16_t)grid->vertices.size();
            grid->addVertex(Renderer::Object::Vertex{ Vector3{x0, y00, z0}, Vector2{0, 0}, normal });
            grid->addVertex(Renderer::Object::Vertex{ Vector3{x1, y10, z0}, Vector2{0, 0}, normal });
            grid->addVertex(Renderer::Object::Vertex{ Vector3{x1, y11, z1}, Vector2{0, 0}, normal });
            grid->addVertex(Renderer::Object::Vertex{ Vector3{x0, y01, z1}, Vector2{0, 0}, normal });

            Renderer::Material* mat = (r + c) % 2 == 0 ? matA : matB;
            grid->addFace(b, b + 1, b + 2, b + 3, mat);
        }
    }
    grid->calculateBoundingBox();
    return grid;
}

// A cheap low-poly pine, inspired by the tiered trees in Jet's own
// Woodland example (github.com/CubeCoders/JetExamples/esp32-lod-billboards)
// — a trunk with a two-tier canopy above it — but built entirely from
// Jet's own proven primitives (createCube/createPyramid, the same calls
// the obstacles already use) rather than hand-rolled geometry, so there's
// no new winding/normal code to get wrong. 12 + 6 + 6 = 24 triangles per
// tree, against Woodland's own 60-220: this hardware doesn't have the
// queue headroom for their full LOD system, so there's just the one
// fixed representation. `trunkH`/`loH`/`hiH` are local Y extents;
// objects created here still need `setPosition()` by the caller.
void buildPineTree(int32_t x, int32_t groundY, int32_t z,
                   Renderer::Material* trunkMat,
                   Renderer::Material* loMat, Renderer::Material* hiMat,
                   Renderer::Object*& outTrunk,
                   Renderer::Object*& outLo, Renderer::Object*& outHi) {
    outTrunk = Primitives::createCube(TREE_TRUNK_W, TREE_TRUNK_H, TREE_TRUNK_W, trunkMat);

    // Base overlaps a little into the trunk top so there's no gap if
    // the trunk sways or the canopy doesn't sit perfectly flush.
    outLo = Primitives::createPyramid(TREE_LO_BASE, TREE_LO_H, loMat);

    // Base sits partway up the lower cone rather than at its apex, so
    // the two tiers read as a visible step rather than one smooth cone
    // — the same per-tier flare Woodland's profile-sweep produces.
    outHi = Primitives::createPyramid(TREE_HI_BASE, TREE_HI_H, hiMat);

    positionPineTree(x, groundY, z, outTrunk, outLo, outHi);
}

// Shared by buildPineTree() (right after creation) and
// repositionPineTree() (boss-kill regeneration, existing objects) — same
// Y offsets either way, just not tied to object creation.
void positionPineTree(int32_t x, int32_t groundY, int32_t z,
                             Renderer::Object* trunk, Renderer::Object* lo,
                             Renderer::Object* hi) {
    trunk->setPosition(x, groundY + TREE_TRUNK_H / 2, z);
    lo->setPosition(x, groundY + TREE_TRUNK_H - 10, z);
    hi->setPosition(x, groundY + TREE_TRUNK_H - 10 + TREE_LO_H / 2, z);
}

// A short, fixed-position decorative strip — not tied to the tank's
// position the way the terrain is, since it's meant to be a real arena
// landmark you navigate around rather than a texture that scrolls with
// you. Purely visual for now: no collision, no gameplay effect. Sits a
// few units above the terrain's own surface to avoid the two coplanar
// meshes flickering against each other (z-fighting) where they overlap.
Renderer::Object* buildRiverStrip(int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                                  int32_t width, int32_t segments,
                                  Renderer::Material* mat) {
    Renderer::Object* strip = new Renderer::Object();
    float dx = (float)(x1 - x0), dz = (float)(z1 - z0);
    float len = sqrtf(dx * dx + dz * dz);
    float ux = dx / len, uz = dz / len;          // unit vector along the river
    float px = -uz, pz = ux;                     // perpendicular (across the river)
    float hw = (float)width / 2.0f;

    for (int32_t i = 0; i <= segments; ++i) {
        float t = (float)i / (float)segments;
        float cx = (float)x0 + dx * t, cz = (float)z0 + dz * t;
        addRiverVertex(strip, cx + px * hw, cz + pz * hw, (float)i / (float)segments);
        addRiverVertex(strip, cx - px * hw, cz - pz * hw, (float)i / (float)segments);
    }
    for (int32_t i = 0; i < segments; ++i) {
        int32_t v0 = i * 2, v1 = v0 + 1, v2 = v0 + 3, v3 = v0 + 2;
        strip->addFace(v0, v1, v2, v3, mat);
    }
    strip->calculateBoundingBox();
    return strip;
}

// Y follows hillHeight() at this point plus RIVER_Y, rather than a flat
// constant — the terrain undulates by up to ~60 units, and a river at a
// fixed absolute height would sink visibly below it wherever a hill
// rises.
void addRiverVertex(Renderer::Object* strip, float x, float z, float v) {
    int32_t ix = (int32_t)x, iz = (int32_t)z;
    strip->addVertex(Renderer::Object::Vertex{
        Vector3{ix, hillHeight(ix, iz) + RIVER_Y, iz},
        Vector2{0, (uint16_t)(v * FIXED_POINT_SCALE)},
        Vector3{0, FIXED_POINT_SCALE, 0}
    });
}

// Repair kit: a 3D plus/cross rather than a cube, per explicit request
// — 14 facets (12 side walls + top + bottom). Hand-authored the same
// way buildTerrain() is (per-face, non-shared vertices; a real
// cross-product-derived normal per side quad), plus a centre-point
// triangle fan for the top/bottom caps. The fan is valid specifically
// because a plus shape is star-shaped from its own centroid — every
// boundary point is visible from the centre along a straight line
// that stays inside the shape, so it can't produce a flipped or
// self-intersecting triangle the way fanning an arbitrary concave
// polygon could.
//
// Built standing upright: the plus outline lives in the local X-Y
// (vertical) plane and is extruded a short distance along Z, rather
// than lying flat in X-Z extruded up in Y — so it reads as a "+" from
// the tank's eye-level view instead of a thin disc seen edge-on. The
// existing Y-axis rotate() call then spins it face-on to edge-on like
// a coin, which is the intended look for a rotating pickup.
//
// Side-wall outward normal ((-dy, dx, 0) from each edge's own (dx,dy)
// direction) was verified by hand against three edges in different
// quadrants of the outline before trusting it here, the same
// discipline as the barrel's rotation math earlier this session — but
// cullingMode is still set to NO_CULLING below regardless, the same
// safety net buildRiverStrip() already uses for hand-authored winding,
// since a normal only affects lighting here, not which way the
// rasteriser's own backface test (screen-space triangle winding,
// unrelated to the stored vertex normal) decides to cull.
//
// 12 side quads (24 tris) + 12+12 cap fan triangles = 48 triangles —
// 4x a plain cube, but there are only ever 3 of these on screen at
// once and they're otherwise motionless, unlike the ground/obstacles/
// trees this session was careful to keep cheap because there can be
// many of them or they move every frame.
Renderer::Object* buildRepairCross(int32_t armHalf, int32_t extHalf, int32_t depth,
                                   Renderer::Material* mat) {
    Renderer::Object* obj = new Renderer::Object();
    const int32_t halfD = depth / 2;
    const int N = 12;
    const int32_t ox[12] = {  armHalf,  extHalf,  extHalf,  armHalf,  armHalf, -armHalf,
                              -armHalf, -extHalf, -extHalf, -armHalf, -armHalf,  armHalf };
    const int32_t oy[12] = {  armHalf,  armHalf, -armHalf, -armHalf, -extHalf, -extHalf,
                              -armHalf, -armHalf,  armHalf,  armHalf,  extHalf,  extHalf };

    for (int i = 0; i < N; ++i) {
        int j = (i + 1) % N;
        int32_t dx = ox[j] - ox[i], dy = oy[j] - oy[i];
        float len = sqrtf((float)dx * dx + (float)dy * dy);
        float s = (len > 0.0001f) ? ((float)FIXED_POINT_SCALE / len) : 0.0f;
        Vector3 n{ (int32_t)(-(float)dy * s), (int32_t)((float)dx * s), 0 };

        uint16_t b = (uint16_t)obj->vertices.size();
        obj->addVertex(Renderer::Object::Vertex{ Vector3{ox[i], oy[i], -halfD}, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ Vector3{ox[j], oy[j], -halfD}, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ Vector3{ox[j], oy[j],  halfD}, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ Vector3{ox[i], oy[i],  halfD}, Vector2{0, 0}, n });
        obj->addFace(b, b + 1, b + 2, b + 3, mat);
    }

    const Vector3 frontN{0, 0, FIXED_POINT_SCALE}, backN{0, 0, -FIXED_POINT_SCALE};
    for (int i = 0; i < N; ++i) {
        int j = (i + 1) % N;
        uint16_t bf = (uint16_t)obj->vertices.size();
        obj->addVertex(Renderer::Object::Vertex{ Vector3{0, 0, halfD}, Vector2{0, 0}, frontN });
        obj->addVertex(Renderer::Object::Vertex{ Vector3{ox[i], oy[i], halfD}, Vector2{0, 0}, frontN });
        obj->addVertex(Renderer::Object::Vertex{ Vector3{ox[j], oy[j], halfD}, Vector2{0, 0}, frontN });
        obj->addTriangle(bf, bf + 1, bf + 2, mat);

        uint16_t bb = (uint16_t)obj->vertices.size();
        obj->addVertex(Renderer::Object::Vertex{ Vector3{0, 0, -halfD}, Vector2{0, 0}, backN });
        obj->addVertex(Renderer::Object::Vertex{ Vector3{ox[i], oy[i], -halfD}, Vector2{0, 0}, backN });
        obj->addVertex(Renderer::Object::Vertex{ Vector3{ox[j], oy[j], -halfD}, Vector2{0, 0}, backN });
        obj->addTriangle(bb, bb + 1, bb + 2, mat);
    }

    obj->calculateBoundingBox();
    obj->cullingMode = Renderer::CullingMode::NO_CULLING;   // hand-authored winding, unverified
    return obj;
}

// Shared by buildPyramidFrustum/buildStumpyPyramid: a slanted quad's
// outward normal isn't purely horizontal like the cross's vertical
// walls, so it's computed from the actual face rather than assumed —
// cross product of two edges, then flipped if it points back toward
// the Y axis instead of away from it (checked via the horizontal
// component of the face centroid, which is never at the axis for an
// off-centre side face).
Vector3 outwardQuadNormal(Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3) {
    float e1x = (float)(v1.x - v0.x), e1y = (float)(v1.y - v0.y), e1z = (float)(v1.z - v0.z);
    float e2x = (float)(v3.x - v0.x), e2y = (float)(v3.y - v0.y), e2z = (float)(v3.z - v0.z);
    float nx = e1y * e2z - e1z * e2y;
    float ny = e1z * e2x - e1x * e2z;
    float nz = e1x * e2y - e1y * e2x;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }
    float cx = (float)(v0.x + v1.x + v2.x + v3.x) / 4.0f;
    float cz = (float)(v0.z + v1.z + v2.z + v3.z) / 4.0f;
    if (nx * cx + nz * cz < 0.0f) { nx = -nx; ny = -ny; nz = -nz; }
    return Vector3{ (int32_t)(nx * FIXED_POINT_SCALE), (int32_t)(ny * FIXED_POINT_SCALE),
                    (int32_t)(nz * FIXED_POINT_SCALE) };
}

// Flat-topped pyramid ("frustum") — one of the mix of hill-like
// obstacle shapes alongside the pointed pyramid and the two-tier
// stumpy pyramid below. Base sits at local y=0 like createPyramid's
// own convention, so callers position it the same way. No bottom cap,
// same as createPyramid — the base is never seen once planted in the
// terrain. 4 side quads + 1 top cap quad = 5 faces, 10 triangles.
Renderer::Object* buildPyramidFrustum(int32_t baseHalf, int32_t topHalf, int32_t height,
                                      Renderer::Material* mat) {
    Renderer::Object* obj = new Renderer::Object();
    const int32_t bx[4] = {  baseHalf,  baseHalf, -baseHalf, -baseHalf };
    const int32_t bz[4] = {  baseHalf, -baseHalf, -baseHalf,  baseHalf };
    const int32_t tx[4] = {  topHalf,  topHalf, -topHalf, -topHalf };
    const int32_t tz[4] = {  topHalf, -topHalf, -topHalf,  topHalf };

    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        Vector3 v0{bx[i], 0, bz[i]}, v1{bx[j], 0, bz[j]};
        Vector3 v2{tx[j], height, tz[j]}, v3{tx[i], height, tz[i]};
        Vector3 n = outwardQuadNormal(v0, v1, v2, v3);

        uint16_t b = (uint16_t)obj->vertices.size();
        obj->addVertex(Renderer::Object::Vertex{ v0, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ v1, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ v2, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ v3, Vector2{0, 0}, n });
        obj->addFace(b, b + 1, b + 2, b + 3, mat);
    }

    const Vector3 upN{0, FIXED_POINT_SCALE, 0};
    uint16_t bt = (uint16_t)obj->vertices.size();
    for (int i = 0; i < 4; ++i) {
        obj->addVertex(Renderer::Object::Vertex{ Vector3{tx[i], height, tz[i]}, Vector2{0, 0}, upN });
    }
    obj->addFace(bt, bt + 1, bt + 2, bt + 3, mat);

    obj->calculateBoundingBox();
    obj->cullingMode = Renderer::CullingMode::NO_CULLING;   // hand-authored winding, unverified
    return obj;
}

// Two-tier "stumpy" pyramid — a hill silhouette where the slope
// changes partway up rather than running straight to the apex: a
// shallower lower band (base to waist) and a steeper upper band
// (waist to a point), so it reads as a rounded mound rather than a
// sharp cone. Base at local y=0, same convention as createPyramid.
// No bottom cap, same reasoning as buildPyramidFrustum. 4 lower side
// quads + 4 upper triangles = 8 + 4 = 12 triangles.
Renderer::Object* buildStumpyPyramid(int32_t baseHalf, int32_t waistHalf, int32_t waistY,
                                     int32_t height, Renderer::Material* mat) {
    Renderer::Object* obj = new Renderer::Object();
    const int32_t bx[4] = {  baseHalf,  baseHalf, -baseHalf, -baseHalf };
    const int32_t bz[4] = {  baseHalf, -baseHalf, -baseHalf,  baseHalf };
    const int32_t wx[4] = {  waistHalf,  waistHalf, -waistHalf, -waistHalf };
    const int32_t wz[4] = {  waistHalf, -waistHalf, -waistHalf,  waistHalf };

    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        Vector3 v0{bx[i], 0, bz[i]}, v1{bx[j], 0, bz[j]};
        Vector3 v2{wx[j], waistY, wz[j]}, v3{wx[i], waistY, wz[i]};
        Vector3 n = outwardQuadNormal(v0, v1, v2, v3);

        uint16_t b = (uint16_t)obj->vertices.size();
        obj->addVertex(Renderer::Object::Vertex{ v0, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ v1, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ v2, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ v3, Vector2{0, 0}, n });
        obj->addFace(b, b + 1, b + 2, b + 3, mat);
    }

    const Vector3 apex{0, height, 0};
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        Vector3 v0{wx[i], waistY, wz[i]}, v1{wx[j], waistY, wz[j]};
        float e1x = (float)(v1.x - v0.x), e1z = (float)(v1.z - v0.z);
        float e2x = (float)(apex.x - v0.x), e2y = (float)(apex.y - v0.y), e2z = (float)(apex.z - v0.z);
        // cross(e1, e2) with e1.y == 0 (waist ring is flat), expanded by hand
        float nx = -e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }
        float cx = (float)(v0.x + v1.x) / 2.0f, cz = (float)(v0.z + v1.z) / 2.0f;
        if (nx * cx + nz * cz < 0.0f) { nx = -nx; ny = -ny; nz = -nz; }
        Vector3 n{ (int32_t)(nx * FIXED_POINT_SCALE), (int32_t)(ny * FIXED_POINT_SCALE),
                  (int32_t)(nz * FIXED_POINT_SCALE) };

        uint16_t b = (uint16_t)obj->vertices.size();
        obj->addVertex(Renderer::Object::Vertex{ v0, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ v1, Vector2{0, 0}, n });
        obj->addVertex(Renderer::Object::Vertex{ apex, Vector2{0, 0}, n });
        obj->addTriangle(b, b + 1, b + 2, mat);
    }

    obj->calculateBoundingBox();
    obj->cullingMode = Renderer::CullingMode::NO_CULLING;   // hand-authored winding, unverified
    return obj;
}

// Repoints every triangle of an already-built mesh at a different
// material — cheap (Triangle::material is a public pointer, no
// geometry rebuild) and only ever called once per spawn, not per
// frame. Used to recolour a pooled enemy's turret by class without
// needing separate geometry per class.
void setObjectMaterial(Renderer::Object* obj, Renderer::Material* mat) {
    for (auto &tri : obj->triangles) tri.material = mat;
}

// Shortest signed difference between two headings, in degrees.
float angleDiff(float target, float current) {
    float d = target - current;
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

float wrapAngle(float a) {
    while (a >= 360.0f) a -= 360.0f;
    while (a <    0.0f) a += 360.0f;
    return a;
}

// Heading that points from (fromX,fromZ) at (toX,toZ), matching this
// game's forward convention of (sin(h), 0, cos(h)).
float bearingTo(float fromX, float fromZ, float toX, float toZ) {
    return degrees(atan2f(toX - fromX, toZ - fromZ));
}

bool within(float ax, float az, float bx, float bz, int32_t radius) {
    float dx = ax - bx, dz = az - bz;
    return dx * dx + dz * dz < (float)radius * (float)radius;
}

}  // namespace tankflux
