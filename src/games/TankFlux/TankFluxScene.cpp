#include "TankFluxGame.h"

namespace tankflux {

void TankFluxGame::buildSkyGround(int h) {
    const int horizon = h / 2;
    for (int y = 0; y < h; ++y) {
        if (y < horizon) {
            // Deep blue overhead fading to pale haze at the horizon.
            float t = (float)y / (float)horizon;
            _skyGround[y] = rgb565((uint8_t)(2  + t * 13.0f),
                                   (uint8_t)(8  + t * 34.0f),
                                   (uint8_t)(18 + t * 13.0f));
        } else {
            // Ground haze: most of the visible ground band fades towards
            // this, so it has to match the ground colours. Runs from
            // _groundMatB (at the horizon) to _groundMatA (close in).
            float t = (float)(y - horizon) / (float)(h - horizon);
            _skyGround[y] = rgb565((uint8_t)(20 - t * 5.0f),
                                   (uint8_t)(33 - t * 8.0f),
                                   (uint8_t)(11 - t * 3.0f));
        }
    }
}

// Colours use RGB565's own ranges (r 0-31, g 0-63, b 0-31). Under bright
// lighting jetModulateRGB565 pushes warm colours with a low green fraction
// towards pink, so warm tones keep green's fraction close to red's.
void TankFluxGame::setupMaterials() {
    _groundMatA.color = rgb565(15, 25, 8);          // dark brown
    _groundMatB.color = rgb565(20, 33, 11);         // lighter brown
    _obstacleCubeMat.color    = rgb565(19, 38, 19); // neutral grey
    _obstaclePyramidMat.color = rgb565(10, 38, 10); // dark grassy green: hills
    _obstacleRockMat.color    = rgb565(9, 16, 13);  // dark slate
    _riverMat.color        = rgb565(9, 24, 27);     // pale blue-green
    _enemyHullMat.color    = rgb565(31,  6,  4);    // vivid red
    _enemyTurretMat.color  = rgb565(16, 28, 8);     // olive drab: a different hue from the hull
    _enemyTurretMatClass2.color = rgb565(14, 26, 30);  // steel blue-grey
    _enemyTurretMatClass3.color = rgb565(30, 40, 4);   // warning yellow
    _enemyBarrelMat.color  = rgb565(22, 44, 22);    // bright grey: dark UNLIT parts vanish
    _enemyTrackMat.color   = rgb565(10, 20, 10);    // neutral dark grey
    _bossHullMat.color     = rgb565(24, 3, 3);      // deep red
    _bossTurretMat.color   = rgb565(18, 17, 16);    // armour grey
    _treeTrunkMat.color    = rgb565(15, 21, 7);     // bark brown
    _treeCanopyLoMat.color = rgb565(5, 25, 7);      // deep pine green
    _treeCanopyHiMat.color = rgb565(11, 37, 10);    // brighter tip, so the tiers read

    // Ground is FLAT (it has real per-cell slope normals); obstacles and
    // trees are GOURAUD so faces shade as you drive round them; tanks,
    // pickups and shells are UNLIT so they're readable from any side.
    _groundMatA.shadingMode         = Renderer::ShadingMode::FLAT;
    _groundMatB.shadingMode         = Renderer::ShadingMode::FLAT;
    _obstacleCubeMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
    _obstaclePyramidMat.shadingMode = Renderer::ShadingMode::GOURAUD;
    _obstacleRockMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
    _treeTrunkMat.shadingMode       = Renderer::ShadingMode::GOURAUD;
    _treeCanopyLoMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
    _treeCanopyHiMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
    _riverMat.shadingMode           = Renderer::ShadingMode::UNLIT;
    _enemyHullMat.shadingMode   = Renderer::ShadingMode::UNLIT;
    _enemyTurretMat.shadingMode = Renderer::ShadingMode::UNLIT;
    _enemyBarrelMat.shadingMode = Renderer::ShadingMode::UNLIT;
    _enemyTrackMat.shadingMode  = Renderer::ShadingMode::UNLIT;
    _enemyTurretMatClass2.shadingMode = Renderer::ShadingMode::UNLIT;
    _enemyTurretMatClass3.shadingMode = Renderer::ShadingMode::UNLIT;
    _bossHullMat.shadingMode    = Renderer::ShadingMode::UNLIT;
    _bossTurretMat.shadingMode  = Renderer::ShadingMode::UNLIT;
    _kitMat.shadingMode         = Renderer::ShadingMode::UNLIT;
    _playerShellMat.shadingMode = Renderer::ShadingMode::UNLIT;
    _enemyShellMat.shadingMode  = Renderer::ShadingMode::UNLIT;
    _barrelHotMat.shadingMode   = Renderer::ShadingMode::UNLIT;
}

// Hull, turret, barrel and two track strips from Jet primitives, hidden
// until the tank spawns. The barrel is a 4-segment cylinder (cheap; the
// facets don't show at this resolution) with closed caps, so a tank
// driving straight at you doesn't show a see-through tube.
void TankFluxGame::buildTankModel(Enemy &e, const TankSpec &spec, Renderer::Material* hullMat,
                                  Renderer::Material* turretMat, Renderer::Material* barrelMat) {
    e.spec = &spec;
    e.barrelMat = barrelMat;
    e.hull   = Primitives::createCube(spec.hullW, spec.hullH, spec.hullD, hullMat);
    e.turret = Primitives::createCube(spec.turretW, spec.turretH, spec.turretW, turretMat);
    e.barrel = Primitives::createCylinder(spec.barrelR, spec.barrelLen, 4, true, barrelMat);
    e.trackL = Primitives::createCube(spec.trackW, spec.trackH, spec.trackD, &_enemyTrackMat);
    e.trackR = Primitives::createCube(spec.trackW, spec.trackH, spec.trackD, &_enemyTrackMat);
    setTankVisible(e, false);
    _scene->addObject(e.hull);
    _scene->addObject(e.turret);
    _scene->addObject(e.barrel);
    _scene->addObject(e.trackL);
    _scene->addObject(e.trackR);
}

void TankFluxGame::ensureSceneReady(GFXcanvas16 &canvas) {
    if (_scene) return;

    _scene = new Renderer::Scene(canvas.getBuffer(), nullptr,
                                 canvas.width(), canvas.height());
    _scene->setClearBuffer(true);
    buildSkyGround(canvas.height());
    _scene->backgroundGradientColors = _skyGround;

    _camera.setFOV(CAMERA_FOV, canvas.width());
    _camera.nearPlane = CAMERA_NEAR;
    _camera.farPlane  = CAMERA_FAR;
    _scene->setCamera(&_camera);

    setupMaterials();
    _scene->setDirectionalLight(&_sun);
    _scene->setAmbientLight(&_amb);

    _ground = buildTerrain(GROUND_SIZE, GROUND_SIZE, GROUND_CELLS, GROUND_CELLS,
                           &_groundMatA, &_groundMatB);
    _scene->addObject(_ground);

    _river = buildRiverStrip(RIVER_X0, RIVER_Z0, RIVER_X1, RIVER_Z1, RIVER_WIDTH, RIVER_SEGMENTS, &_riverMat);
    _river->cullingMode = Renderer::CullingMode::NO_CULLING;   // hand-authored winding, unverified
    _scene->addObject(_river);

    // Positions first, so even the first arena is placed clear of the river.
    generateArenaLayout();

    for (int i = 0; i < OBSTACLE_COUNT; ++i) {
        const ObstacleDef &o = _arena.obstacles[i];
        Renderer::Object* obj;
        switch (o.shape) {
            case SHAPE_PYRAMID: {
                // Three hill variants, chosen by slot (not at random) so a
                // slot keeps its shape across arena regenerations.
                Renderer::Material* mat = &_obstaclePyramidMat;
                int variant = i % 3;
                if (variant == 0) {
                    obj = Primitives::createPyramid(o.size, (o.size * 5) / 4, mat);
                } else if (variant == 1) {
                    obj = buildPyramidFrustum(o.size / 2, (o.size * 2) / 5, (o.size * 9) / 10, mat);
                } else {
                    obj = buildStumpyPyramid(o.size / 2, (o.size * 17) / 40, (o.size * 1) / 2,
                                             (o.size * 11) / 10, mat);
                }
                break;
            }
            case SHAPE_ROCK:
                // Irregular proportions plus a rotation (see repositionObstacle()).
                obj = Primitives::createCube((o.size * 9) / 10, (o.size * 11) / 20,
                                             (o.size * 6) / 5, &_obstacleRockMat);
                break;
            default:  // SHAPE_CUBE
                obj = Primitives::createCube(o.size, (o.size * 3) / 4, o.size, &_obstacleCubeMat);
                break;
        }
        _obstacleObjs[i] = obj;
        _scene->addObject(obj);
        repositionObstacle(i);
    }

    for (int i = 0; i < TREE_COUNT; ++i) {
        const PointDef &t = _arena.trees[i];
        buildPineTree(t.x, hillHeight(t.x, t.z), t.z,
                      &_treeTrunkMat, &_treeCanopyLoMat, &_treeCanopyHiMat,
                      _treeTrunks[i], _treeCanopyLo[i], _treeCanopyHi[i]);
        _scene->addObject(_treeTrunks[i]);
        _scene->addObject(_treeCanopyLo[i]);
        _scene->addObject(_treeCanopyHi[i]);
    }

    for (int i = 0; i < REPAIR_COUNT; ++i) {
        _kits[i].obj = buildRepairCross(REPAIR_ARM_HALF, REPAIR_EXT_HALF, REPAIR_DEPTH, &_kitMat);
        _scene->addObject(_kits[i].obj);
        repositionKit(i);
    }

    for (auto &e : _enemies) {
        buildTankModel(e, REGULAR_TANK, &_enemyHullMat, &_enemyTurretMat, &_enemyBarrelMat);
    }
    buildTankModel(_boss, BOSS_TANK, &_bossHullMat, &_bossTurretMat, &_bossTurretMat);

    _playerShell.obj = Primitives::createCube(SHELL_SIZE, SHELL_SIZE, SHELL_SIZE, &_playerShellMat);
    _playerShell.obj->enabled = false;
    _scene->addObject(_playerShell.obj);

    for (auto &s : _enemyShells) {
        s.obj = Primitives::createCube(SHELL_SIZE, SHELL_SIZE, SHELL_SIZE, &_enemyShellMat);
        s.obj->enabled = false;
        _scene->addObject(s.obj);
    }
}

}  // namespace tankflux
