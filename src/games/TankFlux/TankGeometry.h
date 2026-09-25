#ifndef TANK_GEOMETRY_H
#define TANK_GEOMETRY_H

#include <Arduino.h>
#include <Jet.hpp>
#include "TankFluxConfig.h"

// Mesh builders and the terrain height field. Free functions: none of them
// touch game state. Hand-built meshes use one vertex set per face, so each
// face keeps its own correct normal for FLAT/GOURAUD shading.

namespace tankflux {

// Cosmetic terrain height at a world position.
int32_t hillHeight(int32_t wx, int32_t wz);

// Checkerboard ground with per-cell slope normals, centred on the origin.
Renderer::Object* buildTerrain(int32_t width, int32_t height, int32_t rows, int32_t cols,
                               Renderer::Material* matA, Renderer::Material* matB);

// Flat strip from (x0,z0) to (x1,z1) that follows the terrain.
Renderer::Object* buildRiverStrip(int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                                  int32_t width, int32_t segments,
                                  Renderer::Material* mat);

// Pine tree: creates the three parts and positions them on the ground.
void buildPineTree(int32_t x, int32_t groundY, int32_t z,
                   Renderer::Material* trunkMat,
                   Renderer::Material* loMat, Renderer::Material* hiMat,
                   Renderer::Object*& outTrunk,
                   Renderer::Object*& outLo, Renderer::Object*& outHi);
void positionPineTree(int32_t x, int32_t groundY, int32_t z,
                      Renderer::Object* trunk, Renderer::Object* lo, Renderer::Object* hi);

// Upright "+" extruded along Z, centred on its origin.
Renderer::Object* buildRepairCross(int32_t armHalf, int32_t extHalf, int32_t depth,
                                   Renderer::Material* mat);

// Hill shapes, base at local y=0 (same convention as Primitives::createPyramid).
Renderer::Object* buildPyramidFrustum(int32_t baseHalf, int32_t topHalf, int32_t height,
                                      Renderer::Material* mat);
Renderer::Object* buildStumpyPyramid(int32_t baseHalf, int32_t waistHalf, int32_t waistY,
                                     int32_t height, Renderer::Material* mat);

// Repoints every triangle at `mat`: cheap recolouring without new geometry.
void setObjectMaterial(Renderer::Object* obj, Renderer::Material* mat);

}  // namespace tankflux

#endif  // TANK_GEOMETRY_H
