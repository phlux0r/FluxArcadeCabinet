#ifndef TANK_GEOMETRY_H
#define TANK_GEOMETRY_H

#include <Arduino.h>
#include <Jet.hpp>
#include "TankFluxConfig.h"

// Mesh builders, terrain height, and small angle/distance helpers.
// Free functions: none of them touch game state.

namespace tankflux {

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
int32_t hillHeight(int32_t wx, int32_t wz);
float distToSegment(float px, float pz, float x0, float z0, float x1, float z1);
bool inRiver(float x, float z);
Renderer::Object* buildTerrain(int32_t width, int32_t height, int32_t rows, int32_t cols,
                               Renderer::Material* matA, Renderer::Material* matB);
void buildPineTree(int32_t x, int32_t groundY, int32_t z,
                   Renderer::Material* trunkMat,
                   Renderer::Material* loMat, Renderer::Material* hiMat,
                   Renderer::Object*& outTrunk,
                   Renderer::Object*& outLo, Renderer::Object*& outHi);
void positionPineTree(int32_t x, int32_t groundY, int32_t z,
                             Renderer::Object* trunk, Renderer::Object* lo,
                             Renderer::Object* hi);
Renderer::Object* buildRiverStrip(int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                                  int32_t width, int32_t segments,
                                  Renderer::Material* mat);
void addRiverVertex(Renderer::Object* strip, float x, float z, float v);
Renderer::Object* buildRepairCross(int32_t armHalf, int32_t extHalf, int32_t depth,
                                   Renderer::Material* mat);
Vector3 outwardQuadNormal(Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3);
Renderer::Object* buildPyramidFrustum(int32_t baseHalf, int32_t topHalf, int32_t height,
                                      Renderer::Material* mat);
Renderer::Object* buildStumpyPyramid(int32_t baseHalf, int32_t waistHalf, int32_t waistY,
                                     int32_t height, Renderer::Material* mat);
void setObjectMaterial(Renderer::Object* obj, Renderer::Material* mat);
float angleDiff(float target, float current);
float wrapAngle(float a);
float bearingTo(float fromX, float fromZ, float toX, float toZ);
bool within(float ax, float az, float bx, float bz, int32_t radius);

}  // namespace tankflux

#endif  // TANK_GEOMETRY_H
