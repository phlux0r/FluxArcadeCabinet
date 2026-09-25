#include "TankFluxGame.h"

namespace tankflux {

// Re-rolls the layout keeping clear of where the player and every live tank
// are right now: on a boss-kill regenerate the player can be anywhere, and a
// tank left inside a new obstacle couldn't drive out.
void TankFluxGame::generateArenaLayout() {
    Circle keepClear[1 + MAX_ENEMIES + 1];
    int n = 0;
    keepClear[n++] = { _x, _z, (float)SPAWN_CLEARANCE };
    for (const auto &e : _enemies) {
        if (e.alive) keepClear[n++] = { e.x, e.z, (float)e.spec->radius };
    }
    if (_bossActive) keepClear[n++] = { _boss.x, _boss.z, (float)_boss.spec->radius };
    _arena.generate(keepClear, n);
}

// Sits each obstacle on the terrain: pyramids have their base at local y=0,
// cubes and rocks are centred on their origin so they lift by half height.
void TankFluxGame::repositionObstacle(int i) {
    const ObstacleDef &o = _arena.obstacles[i];
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

void TankFluxGame::repositionKit(int i) {
    const PointDef &k = _arena.kits[i];
    _kits[i].obj->setPosition(k.x, hillHeight(k.x, k.z) + REPAIR_Y_OFFSET, k.z);
}

// Boss-kill arena reset: new positions, same objects (nothing is rebuilt),
// and every repair kit back in play. The river stays put.
void TankFluxGame::regenerateArena() {
    generateArenaLayout();
    for (int i = 0; i < OBSTACLE_COUNT; ++i) repositionObstacle(i);
    for (int i = 0; i < TREE_COUNT; ++i) {
        const PointDef &t = _arena.trees[i];
        positionPineTree(t.x, hillHeight(t.x, t.z), t.z,
                         _treeTrunks[i], _treeCanopyLo[i], _treeCanopyHi[i]);
    }
    for (int i = 0; i < REPAIR_COUNT; ++i) {
        _kits[i].active = true;
        _kits[i].obj->enabled = true;
        repositionKit(i);
    }
    _arenaShiftCuePending = true;
    _arenaShiftCueAt = millis() + ARENA_SHIFT_CUE_DELAY_MS;
}

}  // namespace tankflux
