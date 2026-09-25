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
            // Ground hazes out toward the horizon and darkens close in,
            // so the checkerboard mesh has something to sit against.
            // Raised well off near-black: with depth fog now finishing
            // well short of this background showing through at all,
            // this colour is still what most of the visible ground
            // band fades toward at typical viewing distance (a low,
            // near-horizontal view covers a lot of far terrain even a
            // little below the horizon), so it needs to read as lit
            // terrain haze on its own, not a dark void the checkerboard
            // pops out of. Endpoints match _groundMatB (far/hazier) and
            // _groundMatA (near/darker) — this was still the OLD green
            // ground's own tones after the brown palette swap, which is
            // why distant terrain kept reading grey-green regardless of
            // the checkerboard's own colour: most of the visible ground
            // band is this haze, not the up-close mesh.
            float t = (float)(y - horizon) / (float)(h - horizon);
            _skyGround[y] = rgb565((uint8_t)(20 - t * 5.0f),
                                   (uint8_t)(33 - t * 8.0f),
                                   (uint8_t)(11 - t * 3.0f));
        }
    }
}

void TankFluxGame::ensureSceneReady(GFXcanvas16 &canvas) {
    if (_scene) return;

    _scene = new Renderer::Scene(canvas.getBuffer(), nullptr,
                                 canvas.width(), canvas.height());
    _scene->setClearBuffer(true);
    buildSkyGround(canvas.height());
    _scene->backgroundGradientColors = _skyGround;

    // Wide on purpose. At 70 a tank slid out of frame almost as soon as
    // you started turning toward it, which made a head-on attacker
    // impossible to track while manoeuvring. The extra peripheral vision
    // costs some perspective distortion at the edges and is worth it.
    _camera.setFOV(88, canvas.width());
    _camera.nearPlane = 48;
    _camera.farPlane  = 5200;
    _scene->setCamera(&_camera);

    // EXPERIMENTAL PALETTE SWAP — ground green->brown, obstacles
    // warm-tan/amber->grey/slate, pyramids (all three hill variants,
    // not just the stumpy one) ->dark grassy green. Requested as an
    // easy-to-revert trial: `git revert` this single commit restores
    // the previous tan/amber/green palette if this doesn't read well
    // on hardware. R:G ratios were kept close to the previous
    // (playtested) values' own proportions rather than picked
    // freehand, to avoid the reddish-pink-under-bright-lighting
    // jetModulateRGB565 quirk a prior playtest round already hit here.
    //
    // Two ground tones that differ enough to actually read as a
    // checkerboard at this resolution — an earlier attempt in Combat Flux
    // used two near-identical navies and the floor looked like one flat
    // slab. Warm brown now that the obstacles themselves are moving to
    // grey/slate/green, so nothing needs to stay "cooler than the
    // obstacles" to pop anymore.
    _groundMatA.color = rgb565(15, 25, 8);    // dark brown
    _groundMatB.color = rgb565(20, 33, 11);   // lighter brown
    _obstacleCubeMat.color    = rgb565(19, 38, 19);   // neutral grey
    _obstaclePyramidMat.color = rgb565(10, 38, 10);   // dark grassy green — "hills"
    _obstacleRockMat.color    = rgb565(9, 16, 13);    // dark slate, distinct from the cube's grey
    _riverMat.color        = rgb565(9, 24, 27);       // pale blue-green
    _enemyHullMat.color    = rgb565(31,  6,  4);   // vivid red
    // Was amber (31,22,4) — playtest feedback was that hull+turret
    // both just read as "orange" together, since amber is still in
    // the same red-family hue as the hull, just lighter. Olive-drab
    // is a real tank colour and, more importantly here, a genuinely
    // different hue (green-dominant, not red-dominant) rather than
    // another shade of the same one.
    _enemyTurretMat.color  = rgb565(16, 28, 8);    // olive drab
    _enemyBarrelMat.color  = rgb565(22, 44, 22);   // bright neutral silver-grey
    // Balanced so R:G:B match their channel depths at the same
    // brightness fraction (32% of each channel's own max) — a
    // genuinely neutral dark grey rather than the slightly purple
    // cast an uneven ratio gives at low brightness (the same issue
    // diagnosed for the ground/ambient earlier this session).
    _enemyTrackMat.color   = rgb565(10, 20, 10);   // neutral dark grey
    _enemyTurretMatClass2.color = rgb565(14, 26, 30);  // steel blue-grey
    _enemyTurretMatClass3.color = rgb565(30, 40, 4);   // warning yellow
    _bossHullMat.color     = rgb565(24, 3, 3);     // deep vivid red
    _bossTurretMat.color   = rgb565(18, 17, 16);   // neutral armour grey
    _treeTrunkMat.color    = rgb565(15, 21, 7);    // bark brown
    _treeCanopyLoMat.color = rgb565(5, 25, 7);     // deep pine green
    _treeCanopyHiMat.color = rgb565(11, 37, 10);   // brighter sunlit tip

    // FLAT, not UNLIT: the terrain has real per-face slope normals now
    // (see buildTerrain()), and FLAT is what actually uses them to
    // shade hills instead of rendering them as a flat, uniform colour
    // regardless of height.
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

    _scene->setDirectionalLight(&_sun);
    _scene->setAmbientLight(&_amb);

    _ground = buildTerrain(GROUND_SIZE, GROUND_SIZE, GROUND_CELLS, GROUND_CELLS,
                           &_groundMatA, &_groundMatB);
    _scene->addObject(_ground);

    _river = buildRiverStrip(RIVER_X0, RIVER_Z0, RIVER_X1, RIVER_Z1, RIVER_WIDTH, RIVER_SEGMENTS, &_riverMat);
    _river->cullingMode = Renderer::CullingMode::NO_CULLING;   // hand-authored winding, unverified
    _scene->addObject(_river);

    // Randomize every obstacle/tree/kit position before building any of
    // them, so the very first arena is already clear of the river —
    // regenerateArena() re-runs this same call on every boss kill.
    generateArenaLayout();

    for (int i = 0; i < OBSTACLE_COUNT; ++i) {
        const ObstacleDef &o = OBSTACLES[i];
        Renderer::Object* obj;
        Renderer::Material* mat;
        switch (o.shape) {
            case SHAPE_PYRAMID: {
                // Mixed to make hills read as varied rather than
                // identical cones — deterministic per obstacle (index)
                // rather than random, so the same slot keeps the same
                // shape/variant across every regenerateArena() call.
                // All three variants put their base at local y=0.
                mat = &_obstaclePyramidMat;
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
                // "Rock" = an irregular cube (non-uniform proportions +
                // rotation), not a round primitive — see the material
                // declaration for why createCapsule isn't affordable
                // here. createCube centres on its own origin, so it
                // needs lifting by half its height.
                mat = &_obstacleRockMat;
                obj = Primitives::createCube((o.size * 9) / 10, (o.size * 11) / 20,
                                             (o.size * 6) / 5, mat);
                break;
            default:  // SHAPE_CUBE
                mat = &_obstacleCubeMat;
                obj = Primitives::createCube(o.size, (o.size * 3) / 4, o.size, mat);
                break;
        }
        _obstacleObjs[i] = obj;
        _scene->addObject(obj);
        repositionObstacle(i);   // sets position (and rock rotation) from OBSTACLES[i]
    }

    for (int i = 0; i < TREE_COUNT; ++i) {
        const TreeDef &t = TREES[i];
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

    // Enemy tanks: a low hull, a smaller turret box on top, and a gun
    // barrel — proportions loosely referenced from a Blockbench-style
    // low-poly tank model the user found (hull noticeably wider than
    // the turret, a thin barrel roughly half the hull's width long),
    // rebuilt from Jet's own primitives rather than imported: that
    // model was 872 triangles and textured, both well outside what
    // this hardware/JetConfig can use. createCylinder's segments
    // parameter drives BOTH its radial sides and its vertical bands
    // (checked directly in Primitives.cpp — they share one loop), so
    // segments=4 keeps it cheap (8 triangles) at the cost of a
    // slightly faceted barrel, unnoticeable at this resolution.
    // Radius bumped from a first-pass 12 to 20 after playtest feedback
    // that it "didn't look much different" — a true-to-scale barrel is
    // only 1-2 pixels wide on a 160x128 panel and all but disappears
    // regardless of colour. closedCaps=true (was false): an open tube
    // has nothing at the muzzle end, so head-on — the single most
    // common viewing angle, since enemies drive toward the player —
    // you looked straight through it at the turret behind, which is
    // exactly what playtest reported as "the barrel hole blends in."
    // +4 triangles for the two end caps at segments=4.
    for (int i = 0; i < MAX_ENEMIES; ++i) {
        _enemies[i].hull   = Primitives::createCube(280, 110, 380, &_enemyHullMat);
        _enemies[i].turret = Primitives::createCube(150, 90, 150, &_enemyTurretMat);
        _enemies[i].barrel = Primitives::createCylinder(20, BARREL_LENGTH, 4, true, &_enemyBarrelMat);
        _enemies[i].trackL = Primitives::createCube(TRACK_WIDTH, TRACK_HEIGHT, TRACK_DEPTH, &_enemyTrackMat);
        _enemies[i].trackR = Primitives::createCube(TRACK_WIDTH, TRACK_HEIGHT, TRACK_DEPTH, &_enemyTrackMat);
        _enemies[i].hull->enabled   = false;
        _enemies[i].turret->enabled = false;
        _enemies[i].barrel->enabled = false;
        _enemies[i].trackL->enabled = false;
        _enemies[i].trackR->enabled = false;
        _scene->addObject(_enemies[i].hull);
        _scene->addObject(_enemies[i].turret);
        _scene->addObject(_enemies[i].barrel);
        _scene->addObject(_enemies[i].trackL);
        _scene->addObject(_enemies[i].trackR);
    }

    // Boss: same construction as the regular tanks, at 1.6x every
    // dimension/offset, its own materials, and its own hp pool
    // (BOSS_HP) — see trySpawnBoss()/BOSS_EVERY_KILLS.
    _boss.hull   = Primitives::createCube(BOSS_HULL_W, BOSS_HULL_H, BOSS_HULL_D, &_bossHullMat);
    _boss.turret = Primitives::createCube(BOSS_TURRET_W, BOSS_TURRET_H, BOSS_TURRET_W, &_bossTurretMat);
    _boss.barrel = Primitives::createCylinder(BOSS_BARREL_R, BOSS_BARREL_LEN, 4, true, &_bossTurretMat);
    _boss.trackL = Primitives::createCube(BOSS_TRACK_W, BOSS_TRACK_H, BOSS_TRACK_D, &_enemyTrackMat);
    _boss.trackR = Primitives::createCube(BOSS_TRACK_W, BOSS_TRACK_H, BOSS_TRACK_D, &_enemyTrackMat);
    _boss.hull->enabled   = false;
    _boss.turret->enabled = false;
    _boss.barrel->enabled = false;
    _boss.trackL->enabled = false;
    _boss.trackR->enabled = false;
    _scene->addObject(_boss.hull);
    _scene->addObject(_boss.turret);
    _scene->addObject(_boss.barrel);
    _scene->addObject(_boss.trackL);
    _scene->addObject(_boss.trackR);

    _playerShell.obj = Primitives::createCube(46, 46, 46, &_playerShellMat);
    _playerShell.obj->enabled = false;
    _scene->addObject(_playerShell.obj);

    for (int i = 0; i < MAX_ENEMY_SHELLS; ++i) {
        _enemyShells[i].obj = Primitives::createCube(46, 46, 46, &_enemyShellMat);
        _enemyShells[i].obj->enabled = false;
        _scene->addObject(_enemyShells[i].obj);
    }
}

}  // namespace tankflux
