#include "TankGeometry.h"

namespace tankflux {

namespace {

// River vertices follow the terrain (plus RIVER_Y), so the river never sinks
// below a hill it crosses.
void addRiverVertex(Renderer::Object* strip, float x, float z, float v) {
    int32_t ix = (int32_t)x, iz = (int32_t)z;
    strip->addVertex(Renderer::Object::Vertex{
        Vector3{ix, hillHeight(ix, iz) + RIVER_Y, iz},
        Vector2{0, (uint16_t)(v * FIXED_POINT_SCALE)},
        Vector3{0, FIXED_POINT_SCALE, 0}
    });
}

// Outward normal of a slanted side quad: cross product of two edges, flipped
// if it points back towards the Y axis (the face centroid is never on it).
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

}  // namespace

// Two overlaid sine waves, chosen only to avoid an obviously periodic look.
// Cosmetic: the camera stays at a fixed EYE_HEIGHT and collision is 2D, so a
// tall enough hill could poke through the driving height. Sampling terrain
// under every tank each frame would fix that, at a real per-frame cost.
int32_t hillHeight(int32_t wx, int32_t wz) {
    float fx = (float)wx, fz = (float)wz;
    float h = 70.0f * sinf(fx * 0.0009f) * cosf(fz * 0.0011f)
            + 38.0f * sinf(fx * 0.0021f + 1.7f) * sinf(fz * 0.0017f);
    return (int32_t)h;
}

// Same layout as Primitives::createGrid, but each cell gets its own 4
// vertices and a normal from its actual slope. A shared-vertex grid with a
// fixed up normal gives slopes no shading cue at all. The duplicated vertices
// cost memory once at build time; Jet's per-frame render queue only scales
// with triangle count, which is unchanged. Use FLAT shading: with per-face
// normals GOURAUD would compute the same result at up to 3x the cost.
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

            // Normal = cross(edgeZ, edgeX), which points up on flat ground.
            // Height deltas are exaggerated for the normal only (vertex Y is
            // real): the true slopes tilt normals under 8 degrees, which left
            // every cell in the same narrow brightness band. 8x widens that
            // to a visible lit/shadow split.
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

Renderer::Object* buildRiverStrip(int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                                  int32_t width, int32_t segments,
                                  Renderer::Material* mat) {
    Renderer::Object* strip = new Renderer::Object();
    float dx = (float)(x1 - x0), dz = (float)(z1 - z0);
    float len = sqrtf(dx * dx + dz * dz);
    float ux = dx / len, uz = dz / len;          // along the river
    float px = -uz, pz = ux;                     // across the river
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

// Built from Jet primitives (24 triangles), after the tiered trees in Jet's
// Woodland example.
void buildPineTree(int32_t x, int32_t groundY, int32_t z,
                   Renderer::Material* trunkMat,
                   Renderer::Material* loMat, Renderer::Material* hiMat,
                   Renderer::Object*& outTrunk,
                   Renderer::Object*& outLo, Renderer::Object*& outHi) {
    outTrunk = Primitives::createCube(TREE_TRUNK_W, TREE_TRUNK_H, TREE_TRUNK_W, trunkMat);
    outLo = Primitives::createPyramid(TREE_LO_BASE, TREE_LO_H, loMat);
    outHi = Primitives::createPyramid(TREE_HI_BASE, TREE_HI_H, hiMat);
    positionPineTree(x, groundY, z, outTrunk, outLo, outHi);
}

// The lower canopy overlaps the trunk top so there's no gap; the upper one
// starts partway up the lower, so the two tiers read as a visible step.
void positionPineTree(int32_t x, int32_t groundY, int32_t z,
                      Renderer::Object* trunk, Renderer::Object* lo, Renderer::Object* hi) {
    trunk->setPosition(x, groundY + TREE_TRUNK_H / 2, z);
    lo->setPosition(x, groundY + TREE_TRUNK_H - 10, z);
    hi->setPosition(x, groundY + TREE_TRUNK_H - 10 + TREE_LO_H / 2, z);
}

// 12 side walls plus a front and back cap: 48 triangles. The caps are a
// centre-point fan, which is safe because a plus shape is star-shaped from
// its centre. Upright so it reads as "+" at eye level; the kit's Y-axis spin
// then turns it face-on to edge-on like a coin.
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
        Vector3 n{ (int32_t)(-(float)dy * s), (int32_t)((float)dx * s), 0 };   // outward in X-Y

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

// Flat-topped hill: 4 slanted sides plus a top cap (10 triangles). No bottom
// cap: it's planted in the terrain.
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

// Two-slope hill: a shallow lower band up to the waist, then a steeper
// point, so it reads as a rounded mound (12 triangles, no bottom cap).
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
        // cross(e1, e2) with e1.y == 0 (the waist ring is flat)
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

void setObjectMaterial(Renderer::Object* obj, Renderer::Material* mat) {
    for (auto &tri : obj->triangles) tri.material = mat;
}

}  // namespace tankflux
