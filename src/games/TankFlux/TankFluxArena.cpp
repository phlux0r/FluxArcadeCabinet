#include "TankFluxGame.h"

namespace tankflux {

int32_t TankFluxGame::obstacleRadius(const ObstacleDef &o) {
    // Effective circular footprint for a square-ish block; a little under
    // its half-diagonal so you can just scrape past a corner.
    return (o.size * 3) / 5;
}

// Random rejection sampling within the arena: reject a candidate point
// if it's too close to the river or to anything already in `placed`
// (which generateArenaLayout() seeds with the player and any live
// tanks). Returns false (leaving outX/outZ untouched) if no spot is
// found; the caller then keeps that slot's previous position.
bool TankFluxGame::placeCircle(float radius, const PlacedCircle* placed, int placedCount,
                 float &outX, float &outZ) {
    const int32_t bound = ARENA_HALF - ARENA_MARGIN;
    for (int attempt = 0; attempt < PLACEMENT_ATTEMPTS; ++attempt) {
        float x = (float)random(-bound, bound);
        float z = (float)random(-bound, bound);
        if (distToSegment(x, z, (float)RIVER_X0, (float)RIVER_Z0,
                          (float)RIVER_X1, (float)RIVER_Z1) <
            (float)RIVER_WIDTH / 2.0f + radius + (float)PLACEMENT_MIN_GAP) continue;
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

// Randomizes OBSTACLES/TREES/REPAIRS x/z in place — sizes and shapes are
// untouched, only where each one sits. Called once at scene setup and
// again on every boss kill (see regenerateArena()); the river itself
// never moves, so this is what actually guarantees nothing gets placed
// on top of it rather than that being hand-verified once.
void TankFluxGame::generateArenaLayout() {
    PlacedCircle placed[1 + MAX_ENEMIES + 1 + OBSTACLE_COUNT + TREE_COUNT + REPAIR_COUNT];
    int placedCount = 0;

    // Keep clear of where the player and any live tanks are right now,
    // not just the origin: on a boss-kill regenerate the player can be
    // anywhere, and a tank left inside a new obstacle can't drive out.
    placed[placedCount++] = { _x, _z, (float)SPAWN_CLEARANCE };
    for (const auto &e : _enemies) {
        if (e.alive) placed[placedCount++] = { e.x, e.z, (float)ENEMY_RADIUS };
    }
    if (_bossActive) placed[placedCount++] = { _boss.x, _boss.z, (float)BOSS_RADIUS };

    for (int i = 0; i < OBSTACLE_COUNT; ++i) {
        float r = (float)obstacleRadius(OBSTACLES[i]);
        float x, z;
        if (placeCircle(r, placed, placedCount, x, z)) {
            OBSTACLES[i].x = (int32_t)x;
            OBSTACLES[i].z = (int32_t)z;
        }
        placed[placedCount++] = { (float)OBSTACLES[i].x, (float)OBSTACLES[i].z, r };
    }
    for (int i = 0; i < TREE_COUNT; ++i) {
        float r = (float)TREE_PLACEMENT_RADIUS;
        float x, z;
        if (placeCircle(r, placed, placedCount, x, z)) {
            TREES[i].x = (int32_t)x;
            TREES[i].z = (int32_t)z;
        }
        placed[placedCount++] = { (float)TREES[i].x, (float)TREES[i].z, r };
    }
    for (int i = 0; i < REPAIR_COUNT; ++i) {
        float r = (float)KIT_PLACEMENT_RADIUS;
        float x, z;
        if (placeCircle(r, placed, placedCount, x, z)) {
            REPAIRS[i].x = (int32_t)x;
            REPAIRS[i].z = (int32_t)z;
        }
        placed[placedCount++] = { (float)REPAIRS[i].x, (float)REPAIRS[i].z, r };
    }
}

// Ground is no longer flat (hillHeight()), so every obstacle's resting
// height is the local terrain height plus however far its own shape
// needs lifting to sit on top of it rather than through it. Shared by
// the initial build (ensureSceneReady()) and regenerateArena() — same
// per-shape Y offset either way, just not tied to creating the object.
void TankFluxGame::repositionObstacle(int i) {
    const ObstacleDef &o = OBSTACLES[i];
    int32_t groundY = hillHeight(o.x, o.z);
    int32_t baseY;
    switch (o.shape) {
        case SHAPE_PYRAMID:
            baseY = groundY;
            break;
        case SHAPE_ROCK:
            _obstacleObjs[i]->setRotation(0, (o.x * 7 + o.z * 3) % 360, 0);  // deterministic "random" facing
            baseY = groundY + (o.size * 11) / 40;
            break;
        default:  // SHAPE_CUBE
            baseY = groundY + (o.size * 3) / 8;
            break;
    }
    _obstacleObjs[i]->setPosition(o.x, baseY, o.z);
}

// Same idea as repositionObstacle() — shared by the initial build and
// regenerateArena().
void TankFluxGame::repositionKit(int i) {
    _kits[i].obj->setPosition(REPAIRS[i].x,
                              hillHeight(REPAIRS[i].x, REPAIRS[i].z) + REPAIR_Y_OFFSET,
                              REPAIRS[i].z);
}

// Fresh obstacle/tree/kit layout on every boss kill — reuses
// generateArenaLayout() (the same river/spawn/overlap avoidance the
// initial arena build uses) and then just moves the existing objects;
// no geometry is rebuilt, so this is cheap enough to call from
// destroyEnemy() directly. The river itself stays put. The transition
// cue (chime + flash) is deferred rather than fired here — see
// _arenaShiftCueAt's own comment for why.
void TankFluxGame::regenerateArena() {
    generateArenaLayout();
    for (int i = 0; i < OBSTACLE_COUNT; ++i) repositionObstacle(i);
    for (int i = 0; i < TREE_COUNT; ++i) {
        positionPineTree(TREES[i].x, hillHeight(TREES[i].x, TREES[i].z), TREES[i].z,
                         _treeTrunks[i], _treeCanopyLo[i], _treeCanopyHi[i]);
    }
    for (int i = 0; i < REPAIR_COUNT; ++i) {
        _kits[i].active = true;
        _kits[i].obj->enabled = true;
        repositionKit(i);
    }
    _arenaShiftCuePending = true;
    _arenaShiftCueAt = millis() + 350;   // let the boss's own 300ms kill fanfare finish first
}

bool TankFluxGame::blockedFor(float x, float z, int32_t radius) const {
    for (int i = 0; i < OBSTACLE_COUNT; ++i) {
        float dx = x - (float)OBSTACLES[i].x;
        float dz = z - (float)OBSTACLES[i].z;
        float r  = (float)(radius + obstacleRadius(OBSTACLES[i]));
        if (dx * dx + dz * dz < r * r) return true;
    }
    return false;
}

// Circle-vs-circle push-out, used only for the player's own movement.
// The old axis-separated approach (reject the X move if blocked, reject
// the Z move if blocked) could deadlock: driving nearly straight at an
// obstacle makes BOTH the pure-X and pure-Z probe land inside it every
// single frame, so neither axis ever moves and _speed just oscillates
// near zero as SPEED_SMOOTH rebuilds it and the next frame's probes
// knock it straight back down — "stuck against a low object" from
// playtest. Pushing the candidate position back out to the obstacle's
// boundary along the direction from its centre instead makes the tank
// slide along whatever it's driving into, at any approach angle.
void TankFluxGame::resolveObstacleCollision(float &x, float &z) const {
    for (int i = 0; i < OBSTACLE_COUNT; ++i) {
        float dx = x - (float)OBSTACLES[i].x;
        float dz = z - (float)OBSTACLES[i].z;
        float r  = (float)(TANK_RADIUS + obstacleRadius(OBSTACLES[i]));
        float d2 = dx * dx + dz * dz;
        if (d2 < r * r) {
            float d = sqrtf(d2);
            if (d < 0.0001f) { dx = r; dz = 0.0f; d = r; }  // exactly on centre
            float push = (r - d) / d;
            x += dx * push;
            z += dz * push;
        }
    }
}

}  // namespace tankflux
