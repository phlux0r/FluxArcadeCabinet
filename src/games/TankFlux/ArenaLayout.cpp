#include "ArenaLayout.h"
#include "TankMath.h"

namespace tankflux {

// Random rejection sampling: a candidate is rejected if it's too close to
// the river or to anything already placed. Returns false (outX/outZ
// untouched) if no spot turns up within PLACEMENT_ATTEMPTS.
bool ArenaLayout::placeCircle(float radius, const Circle* placed, int placedCount,
                              float &outX, float &outZ) const {
    const int32_t bound = ARENA_HALF - ARENA_MARGIN;
    for (int attempt = 0; attempt < PLACEMENT_ATTEMPTS; ++attempt) {
        float x = (float)random(-bound, bound);
        float z = (float)random(-bound, bound);
        if (distToRiver(x, z) < (float)RIVER_WIDTH / 2.0f + radius + (float)PLACEMENT_MIN_GAP) continue;
        bool ok = true;
        for (int i = 0; i < placedCount; ++i) {
            float dx = x - placed[i].x, dz = z - placed[i].z;
            float minD = radius + placed[i].r + (float)PLACEMENT_MIN_GAP;
            if (dx * dx + dz * dz < minD * minD) { ok = false; break; }
        }
        if (!ok) continue;
        outX = x; outZ = z;
        return true;
    }
    return false;
}

void ArenaLayout::generate(const Circle* keepClear, int keepClearCount) {
    Circle placed[MAX_KEEP_CLEAR + OBSTACLE_COUNT + TREE_COUNT + REPAIR_COUNT];
    int placedCount = 0;
    if (keepClearCount > MAX_KEEP_CLEAR) keepClearCount = MAX_KEEP_CLEAR;
    for (int i = 0; i < keepClearCount; ++i) placed[placedCount++] = keepClear[i];

    // A slot that can't be placed keeps its previous position.
    auto place = [&](int32_t &x, int32_t &z, float r) {
        float nx, nz;
        if (placeCircle(r, placed, placedCount, nx, nz)) {
            x = (int32_t)nx;
            z = (int32_t)nz;
        }
        placed[placedCount++] = { (float)x, (float)z, r };
    };
    for (auto &o : obstacles) place(o.x, o.z, (float)obstacleRadius(o));
    for (auto &t : trees)     place(t.x, t.z, (float)TREE_PLACEMENT_RADIUS);
    for (auto &k : kits)      place(k.x, k.z, (float)KIT_PLACEMENT_RADIUS);
}

bool ArenaLayout::blocked(float x, float z, int32_t radius) const {
    for (const auto &o : obstacles) {
        if (within(x, z, (float)o.x, (float)o.z, radius + obstacleRadius(o))) return true;
    }
    return false;
}

void ArenaLayout::pushOut(float &x, float &z, int32_t radius) const {
    for (const auto &o : obstacles) {
        pushOutOfCircle(x, z, (float)o.x, (float)o.z, (float)(radius + obstacleRadius(o)));
    }
}

}  // namespace tankflux
