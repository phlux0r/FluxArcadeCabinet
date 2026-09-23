#ifndef TANK_FLUX_GAME_H
#define TANK_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Preferences.h>
#include <math.h>

#include <Jet.hpp>

// =============================================================================
// TANK FLUX — a Battlezone-style first-person tank game rendered with Jet
// (https://github.com/CubeCoders/Jet).
//
// You drive a tank around a bounded arena of wireframe ground and solid
// obstacles. Obstacles block movement (and, once shells exist, block shots —
// that's what makes them cover rather than scenery). Repair kits scattered
// around the arena restore health, so there's a reason to cross open ground
// rather than sit in one place.
//
// The camera sits inside the tank and never pitches, so the aim-forward
// vector collapses to the pitch=0 case of the convention verified against
// Jet's own source in CombatFluxGame.h: (sin(yaw), 0, cos(yaw)).
//
// Enemy tanks turn at a capped rate, which is what makes them flankable, and
// fire shells slow enough to drive out of the way of. Shells die on contact
// with obstacles, so cover works without needing any separate line-of-sight
// test. Damage is a 100-point pool; repair kits restore it.
//
// Jet's per-frontend render config lives in include/JetConfig.hpp at the
// project root.
// =============================================================================

class TankFluxGame : public IGame {
private:
    // --- Arena ---------------------------------------------------------------
    static const int32_t ARENA_HALF   = 3000;   // playable area is +/- this in X and Z
    static const int32_t GROUND_SIZE  = 9000;
    static const int32_t GROUND_CELLS = 12;
    static const int32_t GROUND_CELL  = GROUND_SIZE / GROUND_CELLS;
    // The checkerboard alternates per cell, so the mesh has to be re-centred
    // in TWO-cell steps — snapping by one would flip the parity and swap
    // every square's colour as you drove across the boundary.
    static const int32_t GROUND_SNAP  = GROUND_CELL * 2;
    // createGrid lays vertices from -size/2 to (cells-1)*spacing - size/2,
    // so the mesh isn't centred on its own origin; this re-centres it.
    static const int32_t GROUND_BIAS  = GROUND_CELL / 2;

    // --- Tank ----------------------------------------------------------------
    static const int32_t EYE_HEIGHT  = 120;
    static const int32_t TANK_RADIUS = 150;

    // Both signs are here as named constants because this project has had to
    // flip joystick axes by trial more than once — the cabinet's stick is
    // physically mounted rotated relative to a landscape (rotation 1) game,
    // so X/Y read swapped, same as AsteroidFluxGame. If driving or turning
    // comes out backwards on hardware, flip the relevant sign here.
    static constexpr float DRIVE_SIGN = -1.0f;   // flipped after playtest
    static constexpr float TURN_SIGN  = 1.0f;

    static constexpr float TURN_RATE    = 2.2f;   // deg/frame at full deflection
    static constexpr float FWD_SPEED    = 26.0f;  // world units/frame
    static constexpr float REV_SPEED    = 14.0f;  // reverse is deliberately slower
    static constexpr float SPEED_SMOOTH = 0.2f;   // 0=no response, 1=instant

    // --- Health / repair kits ------------------------------------------------
    static const int HEALTH_MAX = 100;
    static const int REPAIR_AMOUNT = 30;
    static const int REPAIR_COUNT  = 3;
    static const int32_t REPAIR_PICKUP_RADIUS = 240;
    static const unsigned long REPAIR_RESPAWN_MS = 12000;

    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    // --- Combat --------------------------------------------------------------
    // Shells fly flat at a fixed height: gameplay is entirely on the ground
    // plane, so there's no reason to carry a Y velocity around.
    static const int32_t SHELL_Y = 95;

    static constexpr float PLAYER_SHELL_SPEED = 70.0f;   // units/frame
    static const int32_t   PLAYER_SHELL_RANGE = 2600;
    // One shell in flight at a time, Battlezone-style — it's what stops the
    // fire button being a mash and makes each shot a decision.
    static const unsigned long PLAYER_RELOAD_MS = 400;

    static const int MAX_ENEMIES = 3;
    static const int32_t ENEMY_RADIUS   = 170;
    static constexpr float ENEMY_SPEED      = 11.0f;
    // The turn rate cap is the whole reason enemies are beatable: it's what
    // lets you flank one that's already committed to a heading.
    static constexpr float ENEMY_TURN_RATE  = 1.3f;
    static constexpr float ENEMY_AIM_TOLERANCE = 12.0f;  // degrees before it shoots
    static const int32_t ENEMY_FIRE_RANGE = 2200;
    // Kept well out rather than closing to point blank: a shell fired from
    // close range can't be driven out of the way of, so an enemy parked on
    // top of you would be unavoidable damage rather than a threat to play
    // around.
    static const int32_t ENEMY_STANDOFF   = 900;
    // Three enemies at the lower end of this range put ~60 damage a second
    // into the air, which empties a 100-point pool in well under four
    // seconds. Deliberately slack; easier to tighten after a playtest than
    // to discover the game is unsurvivable.
    static const unsigned long ENEMY_FIRE_MIN_MS = 2200;
    static const unsigned long ENEMY_FIRE_MAX_MS = 4000;
    static const unsigned long ENEMY_RESPAWN_MS  = 2500;

    static const int MAX_ENEMY_SHELLS = 4;
    static constexpr float ENEMY_SHELL_SPEED = 42.0f;    // slow enough to drive out of
    static const int32_t   ENEMY_SHELL_RANGE = 2600;

    static const int HIT_DAMAGE = 20;
    static const int32_t HIT_RADIUS  = 190;   // enemy shell vs player
    static const int32_t KILL_RADIUS = 250;   // player shell vs enemy
    static const int SCORE_PER_KILL = 100;

    // --- Obstacles -----------------------------------------------------------
    // Hand-placed rather than random: a fixed arena is learnable, so players
    // can come to know where cover is instead of re-reading the map each run.
    struct ObstacleDef { int32_t x, z, size; uint8_t pyramid; };
    static const int OBSTACLE_COUNT = 10;
    static constexpr ObstacleDef OBSTACLES[OBSTACLE_COUNT] = {
        { -1500,   800, 440, 0 },
        {   900,  1500, 380, 1 },
        {  1900,  -400, 500, 0 },
        {  -600, -1600, 360, 1 },
        { -2200, -1100, 420, 0 },
        {  2100,  1900, 340, 1 },
        {   200,  2300, 460, 0 },
        { -1900,  2000, 360, 1 },
        {  1300, -1900, 420, 0 },
        {  -300,  -600, 300, 1 },
    };

    struct RepairDef { int32_t x, z; };
    static constexpr RepairDef REPAIRS[REPAIR_COUNT] = {
        {  2400,   600 },
        { -2400,  -300 },
        {   500, -2400 },
    };

    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_GAMEOVER };
    GamePhase _phase = PHASE_ATTRACT;

    struct RepairKit {
        Renderer::Object* obj = nullptr;
        bool active = true;
        unsigned long respawnAt = 0;
    };
    RepairKit _kits[REPAIR_COUNT];

    struct Shell {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        float x = 0, z = 0;
        float vx = 0, vz = 0;    // per-frame delta, baked at fire time
        float travelled = 0;
    };
    Shell _playerShell;
    Shell _enemyShells[MAX_ENEMY_SHELLS];

    struct Enemy {
        Renderer::Object* hull   = nullptr;
        Renderer::Object* turret = nullptr;
        bool  alive = false;
        float x = 0, z = 0;
        float headingDeg = 0;
        unsigned long respawnAt = 0;
        unsigned long nextFireAt = 0;
    };
    Enemy _enemies[MAX_ENEMIES];

    // --- Jet scene state -----------------------------------------------------
    // Scene needs a framebuffer pointer, which only exists once update() hands
    // us the launcher's canvas, so everything is built lazily on the first
    // update() call rather than in init().
    Renderer::Scene*  _scene  = nullptr;
    Renderer::Camera  _camera;
    Renderer::Object* _ground = nullptr;
    Renderer::Object* _obstacleObjs[OBSTACLE_COUNT] = { nullptr };
    // Vector3 is declared at global scope in Jet (Shader.hpp), unlike Color.
    Renderer::DirectionalLight _sun{ Vector3{35, 60, 0}, Renderer::Color{255, 240, 215}, 235 };
    Renderer::AmbientLight     _amb{ Renderer::Color{60, 66, 90} };
    // Ground is a two-tone checkerboard, not a wireframe grid: Jet declares
    // ShadingMode::WIREFRAME in its enum but never implements it anywhere in
    // the rasterizer, so a "wireframe" material silently renders as a solid
    // fill. (Rasterizer::wireframeMode is real, but it's a global debug
    // toggle that forces a black background, which would throw away the
    // sky/ground split below.) Colours are assigned in ensureSceneReady.
    Renderer::Material _groundMatA;
    Renderer::Material _groundMatB;
    // Obstacles are the one lit (GOURAUD) surface: their faces shade
    // differently as you drive around them, which is a real depth cue in a
    // first-person game where the camera is constantly moving.
    // Warm tan rather than slate: the ground is green-grey and the sky is
    // blue, so a warm hue is the one thing on screen that can't be mistaken
    // for either. Colour assigned in ensureSceneReady.
    Renderer::Material _obstacleMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 30 };
    Renderer::Material _kitMat{ ArcadeConfig::COLOR_GREEN };
    Renderer::Material _enemyMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 40 };
    Renderer::Material _playerShellMat{ ArcadeConfig::COLOR_CYAN };
    Renderer::Material _enemyShellMat{ ArcadeConfig::COLOR_AMBER };
    Renderer::ParticleSystem _particles{ (float)JET32_WORLD_SCALE };

    // Per-row background colours: sky above the horizon, ground below, which
    // Scene uses for the frame clear. With pitch locked at 0 the true horizon
    // sits at exactly screenHeight/2 every frame (a ground point at infinite
    // distance projects there), so a fixed split is always correct — and it
    // means the world still reads as ground below the horizon even past the
    // far edge of the ground mesh.
    uint16_t _skyGround[ArcadeConfig::LANDSCAPE_HEIGHT];

    // --- Tank state ----------------------------------------------------------
    float _x = 0.0f, _z = 0.0f;
    float _headingDeg = 0.0f;
    float _speed = 0.0f;

    int _health = HEALTH_MAX;
    int _score = 0;
    int _highScore = 0;

    unsigned long _reloadAt = 0;
    unsigned long _muzzleFlashUntil = 0;
    unsigned long _damageFlashUntil = 0;

    bool _btnBWasHeld = false;
    unsigned long _gameOverEnteredMs = 0;

    Preferences _prefs;

    void loadHighScore() {
        _prefs.begin("tf_data", true);
        _highScore = _prefs.getInt("highscore", 0);
        _prefs.end();
    }

    void saveHighScore() {
        _prefs.begin("tf_data", false);
        _prefs.putInt("highscore", _highScore);
        _prefs.end();
    }

    static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    // floorf rather than integer truncation: truncation rounds toward zero,
    // which makes the cell straddling the origin twice as wide and lets the
    // mesh sit off-centre near spawn.
    static int32_t snapTo(float v, int32_t period) {
        return (int32_t)floorf(v / (float)period) * period;
    }

    void buildSkyGround(int h) {
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
                float t = (float)(y - horizon) / (float)(h - horizon);
                _skyGround[y] = rgb565((uint8_t)(7  - t * 4.0f),
                                       (uint8_t)(16 - t * 9.0f),
                                       (uint8_t)(9  - t * 5.0f));
            }
        }
    }

    static int32_t obstacleRadius(const ObstacleDef &o) {
        // Effective circular footprint for a square-ish block; a little under
        // its half-diagonal so you can just scrape past a corner.
        return (o.size * 3) / 5;
    }

    void ensureSceneReady(GFXcanvas16 &canvas) {
        if (_scene) return;

        _scene = new Renderer::Scene(canvas.getBuffer(), nullptr,
                                     canvas.width(), canvas.height());
        _scene->setClearBuffer(true);
        buildSkyGround(canvas.height());
        _scene->backgroundGradientColors = _skyGround;

        _camera.setFOV(70, canvas.width());
        _camera.nearPlane = 48;
        _camera.farPlane  = 5200;
        _scene->setCamera(&_camera);

        // Two ground tones that differ enough to actually read as a
        // checkerboard at this resolution — an earlier attempt in Combat Flux
        // used two near-identical navies and the floor looked like one flat
        // slab. Kept greener than the obstacles' slate so obstacles pop.
        _groundMatA.color = rgb565(5, 16, 7);
        _groundMatB.color = rgb565(9, 26, 11);
        _obstacleMat.color = rgb565(23, 33, 13);   // warm tan
        _enemyMat.color    = rgb565(29, 13, 6);    // red-orange

        _groundMatA.shadingMode     = Renderer::ShadingMode::UNLIT;
        _groundMatB.shadingMode     = Renderer::ShadingMode::UNLIT;
        _obstacleMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
        _enemyMat.shadingMode       = Renderer::ShadingMode::GOURAUD;
        _kitMat.shadingMode         = Renderer::ShadingMode::UNLIT;
        _playerShellMat.shadingMode = Renderer::ShadingMode::UNLIT;
        _enemyShellMat.shadingMode  = Renderer::ShadingMode::UNLIT;

        _scene->setDirectionalLight(&_sun);
        _scene->setAmbientLight(&_amb);

        _ground = Primitives::createGrid(GROUND_SIZE, GROUND_SIZE,
                                         GROUND_CELLS, GROUND_CELLS,
                                         &_groundMatA, &_groundMatB);
        _scene->addObject(_ground);

        for (int i = 0; i < OBSTACLE_COUNT; ++i) {
            const ObstacleDef &o = OBSTACLES[i];
            // createPyramid puts its base at local y=0, but createCube centres
            // on its origin — so only the cube needs lifting by half its
            // height to sit on the ground instead of sunk through it.
            Renderer::Object* obj;
            int32_t baseY;
            if (o.pyramid) {
                obj = Primitives::createPyramid(o.size, (o.size * 5) / 4, &_obstacleMat);
                baseY = 0;
            } else {
                obj = Primitives::createCube(o.size, (o.size * 3) / 4, o.size, &_obstacleMat);
                baseY = (o.size * 3) / 8;
            }
            obj->setPosition(o.x, baseY, o.z);
            _scene->addObject(obj);
            _obstacleObjs[i] = obj;
        }

        for (int i = 0; i < REPAIR_COUNT; ++i) {
            _kits[i].obj = Primitives::createCube(130, 130, 130, &_kitMat);
            _kits[i].obj->setPosition(REPAIRS[i].x, 90, REPAIRS[i].z);
            _scene->addObject(_kits[i].obj);
        }

        // Enemy tanks: a low hull with a smaller turret box on top, so they
        // read as vehicles rather than floating crates even at fog distance.
        for (int i = 0; i < MAX_ENEMIES; ++i) {
            _enemies[i].hull = Primitives::createCube(280, 110, 380, &_enemyMat);
            _enemies[i].turret = Primitives::createCube(150, 90, 150, &_enemyMat);
            _enemies[i].hull->enabled = false;
            _enemies[i].turret->enabled = false;
            _scene->addObject(_enemies[i].hull);
            _scene->addObject(_enemies[i].turret);
        }

        _playerShell.obj = Primitives::createCube(46, 46, 46, &_playerShellMat);
        _playerShell.obj->enabled = false;
        _scene->addObject(_playerShell.obj);

        for (int i = 0; i < MAX_ENEMY_SHELLS; ++i) {
            _enemyShells[i].obj = Primitives::createCube(46, 46, 46, &_enemyShellMat);
            _enemyShells[i].obj->enabled = false;
            _scene->addObject(_enemyShells[i].obj);
        }
    }

    // Shortest signed difference between two headings, in degrees.
    static float angleDiff(float target, float current) {
        float d = target - current;
        while (d >  180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        return d;
    }

    static float wrapAngle(float a) {
        while (a >= 360.0f) a -= 360.0f;
        while (a <    0.0f) a += 360.0f;
        return a;
    }

    // Heading that points from (fromX,fromZ) at (toX,toZ), matching this
    // game's forward convention of (sin(h), 0, cos(h)).
    static float bearingTo(float fromX, float fromZ, float toX, float toZ) {
        return degrees(atan2f(toX - fromX, toZ - fromZ));
    }

    bool blockedFor(float x, float z, int32_t radius) const {
        for (int i = 0; i < OBSTACLE_COUNT; ++i) {
            float dx = x - (float)OBSTACLES[i].x;
            float dz = z - (float)OBSTACLES[i].z;
            float r  = (float)(radius + obstacleRadius(OBSTACLES[i]));
            if (dx * dx + dz * dz < r * r) return true;
        }
        return false;
    }

    bool blocked(float x, float z) const { return blockedFor(x, z, TANK_RADIUS); }

    static bool within(float ax, float az, float bx, float bz, int32_t radius) {
        float dx = ax - bx, dz = az - bz;
        return dx * dx + dz * dz < (float)radius * (float)radius;
    }

    void fireShell(Shell &s, float x, float z, float headingDeg, float speed) {
        float hr = radians(headingDeg);
        s.x = x;
        s.z = z;
        s.vx = sinf(hr) * speed;
        s.vz = cosf(hr) * speed;
        s.travelled = 0.0f;
        s.active = true;
        s.obj->enabled = true;
        s.obj->setPosition((int32_t)s.x, SHELL_Y, (int32_t)s.z);
    }

    void killShell(Shell &s) {
        s.active = false;
        s.obj->enabled = false;
    }

    // Advances a shell and returns true while it's still in flight. Shells
    // die on obstacles, which is what turns cover into actual cover — no
    // separate line-of-sight test is needed anywhere else.
    bool advanceShell(Shell &s, int32_t range) {
        s.x += s.vx;
        s.z += s.vz;
        s.travelled += sqrtf(s.vx * s.vx + s.vz * s.vz);
        s.obj->setPosition((int32_t)s.x, SHELL_Y, (int32_t)s.z);

        if (s.travelled > (float)range ||
            fabsf(s.x) > (float)ARENA_HALF || fabsf(s.z) > (float)ARENA_HALF) {
            killShell(s);
            return false;
        }
        if (blockedFor(s.x, s.z, 20)) {
            _particles.emitSparks(Renderer::Vec3f{ s.x, (float)SHELL_Y, s.z },
                                  Renderer::Vec3f{ 0, 1, 0 }, 260.0f, 8);
            killShell(s);
            return false;
        }
        return true;
    }

    void updateDriving(const InputState &input) {
        _headingDeg += TURN_SIGN * input.joyY * TURN_RATE;
        while (_headingDeg >= 360.0f) _headingDeg -= 360.0f;
        while (_headingDeg <    0.0f) _headingDeg += 360.0f;

        float drive  = DRIVE_SIGN * input.joyX;
        float target = drive * (drive >= 0.0f ? FWD_SPEED : REV_SPEED);
        _speed += (target - _speed) * SPEED_SMOOTH;

        float headRad = radians(_headingDeg);
        float fx = sinf(headRad);
        float fz = cosf(headRad);

        // Axis-separated so a glancing hit slides along an obstacle instead of
        // stopping the tank dead against it.
        float nx = _x + fx * _speed;
        float nz = _z + fz * _speed;
        if (!blocked(nx, _z)) _x = nx; else _speed *= 0.4f;
        if (!blocked(_x, nz)) _z = nz; else _speed *= 0.4f;

        const float limit = (float)(ARENA_HALF - TANK_RADIUS);
        _x = constrain(_x, -limit, limit);
        _z = constrain(_z, -limit, limit);

        _camera.setPosition((int32_t)_x, EYE_HEIGHT, (int32_t)_z);
        _camera.setRotation(0, (int32_t)_headingDeg, 0);

        // Keep the ground mesh centred on the tank, snapped to the checker
        // period. The pattern repeats exactly over that period, so the snap
        // is invisible and the ground reads as infinite without paying for a
        // mesh big enough to cover the whole arena at this cell density.
        // Worst case this leaves ~2625 units of ground ahead of the tank,
        // which is why the fog in JetConfig.hpp is set to finish inside that.
        _ground->setPosition(snapTo(_x, GROUND_SNAP) + GROUND_BIAS, 0,
                             snapTo(_z, GROUND_SNAP) + GROUND_BIAS);
    }

    void spawnEnemy(Enemy &e) {
        // Spawn out on the perimeter, and not right on top of the player.
        for (int attempt = 0; attempt < 12; ++attempt) {
            float ang = radians((float)random(0, 360));
            float r   = (float)(ARENA_HALF - 400);
            float ex  = sinf(ang) * r;
            float ez  = cosf(ang) * r;
            if (blockedFor(ex, ez, ENEMY_RADIUS)) continue;
            if (within(ex, ez, _x, _z, 1600)) continue;
            e.x = ex;
            e.z = ez;
            e.headingDeg = bearingTo(ex, ez, _x, _z);
            e.alive = true;
            e.hull->enabled = true;
            e.turret->enabled = true;
            e.nextFireAt = millis() + random((long)ENEMY_FIRE_MIN_MS, (long)ENEMY_FIRE_MAX_MS);
            return;
        }
        // Every candidate was blocked or too close; try again next frame.
        e.respawnAt = millis() + 400;
    }

    void destroyEnemy(Enemy &e, AudioEngine &audio) {
        _particles.emitSparks(Renderer::Vec3f{ e.x, 120.0f, e.z },
                              Renderer::Vec3f{ 0, 1, 0 }, 520.0f, 26);
        e.alive = false;
        e.hull->enabled = false;
        e.turret->enabled = false;
        e.respawnAt = millis() + ENEMY_RESPAWN_MS;
        _score += SCORE_PER_KILL;
        audio.playTone(1500, 90);
    }

    void updateEnemies(AudioEngine &audio) {
        for (auto &e : _enemies) {
            if (!e.alive) {
                if ((long)(millis() - e.respawnAt) >= 0) spawnEnemy(e);
                continue;
            }

            float want = bearingTo(e.x, e.z, _x, _z);
            float err  = angleDiff(want, e.headingDeg);
            e.headingDeg = wrapAngle(e.headingDeg +
                                     constrain(err, -ENEMY_TURN_RATE, ENEMY_TURN_RATE));

            float dx = _x - e.x, dz = _z - e.z;
            float dist = sqrtf(dx * dx + dz * dz);

            if (dist > (float)ENEMY_STANDOFF) {
                float hr = radians(e.headingDeg);
                float nx = e.x + sinf(hr) * ENEMY_SPEED;
                float nz = e.z + cosf(hr) * ENEMY_SPEED;
                if (!blockedFor(nx, nz, ENEMY_RADIUS)) {
                    e.x = nx;
                    e.z = nz;
                } else {
                    // Scrape around whatever it walked into rather than
                    // grinding against it forever.
                    e.headingDeg = wrapAngle(e.headingDeg + 9.0f);
                }
                const float limit = (float)(ARENA_HALF - ENEMY_RADIUS);
                e.x = constrain(e.x, -limit, limit);
                e.z = constrain(e.z, -limit, limit);
            }

            e.hull->setPosition((int32_t)e.x, 55, (int32_t)e.z);
            e.hull->setRotation(0, (int32_t)e.headingDeg, 0);
            e.turret->setPosition((int32_t)e.x, 155, (int32_t)e.z);
            e.turret->setRotation(0, (int32_t)e.headingDeg, 0);

            if (fabsf(err) < ENEMY_AIM_TOLERANCE && dist < (float)ENEMY_FIRE_RANGE &&
                (long)(millis() - e.nextFireAt) >= 0) {
                for (auto &s : _enemyShells) {
                    if (s.active) continue;
                    float hr = radians(e.headingDeg);
                    fireShell(s, e.x + sinf(hr) * 220.0f, e.z + cosf(hr) * 220.0f,
                              e.headingDeg, ENEMY_SHELL_SPEED);
                    audio.playTone(420, 45);
                    break;
                }
                e.nextFireAt = millis() + random((long)ENEMY_FIRE_MIN_MS, (long)ENEMY_FIRE_MAX_MS);
            }
        }
    }

    void updateShells(AudioEngine &audio) {
        if (_playerShell.active && advanceShell(_playerShell, PLAYER_SHELL_RANGE)) {
            for (auto &e : _enemies) {
                if (!e.alive) continue;
                if (within(_playerShell.x, _playerShell.z, e.x, e.z, KILL_RADIUS)) {
                    destroyEnemy(e, audio);
                    killShell(_playerShell);
                    break;
                }
            }
        }

        for (auto &s : _enemyShells) {
            if (!s.active) continue;
            if (!advanceShell(s, ENEMY_SHELL_RANGE)) continue;
            if (within(s.x, s.z, _x, _z, HIT_RADIUS)) {
                _health -= HIT_DAMAGE;
                _damageFlashUntil = millis() + 160;
                killShell(s);
                audio.playTone(180, 220);
            }
        }
    }

    void tryFire(const InputState &input, AudioEngine &audio) {
        if (!input.btnAPressed) return;
        if (_playerShell.active) return;              // one shell in flight
        if ((long)(millis() - _reloadAt) < 0) return;

        float hr = radians(_headingDeg);
        fireShell(_playerShell,
                  _x + sinf(hr) * 200.0f, _z + cosf(hr) * 200.0f,
                  _headingDeg, PLAYER_SHELL_SPEED);
        _reloadAt = millis() + PLAYER_RELOAD_MS;
        _muzzleFlashUntil = millis() + 70;
        audio.playTone(950, 55);
    }

    void updateKits(AudioEngine &audio) {
        for (int i = 0; i < REPAIR_COUNT; ++i) {
            RepairKit &k = _kits[i];
            if (!k.active) {
                if ((long)(millis() - k.respawnAt) >= 0) {
                    k.active = true;
                    k.obj->enabled = true;
                }
                continue;
            }
            k.obj->rotate(0, 3, 0);   // slow spin, so pickups read as pickups

            // Picked up whenever you drive through, even at full health —
            // a kit that silently refuses to collect reads as a bug.
            float dx = _x - (float)REPAIRS[i].x;
            float dz = _z - (float)REPAIRS[i].z;
            if (dx * dx + dz * dz < (float)REPAIR_PICKUP_RADIUS * REPAIR_PICKUP_RADIUS) {
                _health = min(HEALTH_MAX, _health + REPAIR_AMOUNT);
                k.active = false;
                k.obj->enabled = false;
                k.respawnAt = millis() + REPAIR_RESPAWN_MS;
                audio.playTone(1200, 70);
            }
        }
    }

    void startNewGame(AudioEngine &audio) {
        _x = 0.0f;
        _z = 0.0f;
        _headingDeg = 0.0f;
        _speed = 0.0f;
        _health = HEALTH_MAX;
        _score = 0;
        _reloadAt = 0;
        _muzzleFlashUntil = 0;
        _damageFlashUntil = 0;
        for (int i = 0; i < REPAIR_COUNT; ++i) {
            _kits[i].active = true;
            if (_kits[i].obj) _kits[i].obj->enabled = true;
        }
        killShell(_playerShell);
        for (auto &s : _enemyShells) killShell(s);
        for (int i = 0; i < MAX_ENEMIES; ++i) {
            _enemies[i].alive = false;
            _enemies[i].hull->enabled = false;
            _enemies[i].turret->enabled = false;
            // Staggered so they don't all arrive at once on the first wave.
            _enemies[i].respawnAt = millis() + 800 + (unsigned long)i * 1200;
        }
        for (auto &p : _particles.pool) p.active = false;
        _phase = PHASE_PLAYING;
        audio.playTone(900, 80);
    }

    void drawHUD(GFXcanvas16 &canvas) {
        canvas.fillRect(0, 0, ArcadeConfig::LANDSCAPE_WIDTH, 10, ArcadeConfig::COLOR_BLACK);
        canvas.drawFastHLine(0, 10, ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::COLOR_GREEN);

        canvas.setFont();
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(4, 1);
        canvas.print("SCORE:"); canvas.print(_score);

        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 60, 1);
        canvas.print("HI:"); canvas.print(_highScore);

        // Health bar sits bottom-left, stopping short of centre so it doesn't
        // run across the gun barrel.
        const int barX = 3, barY = ArcadeConfig::LANDSCAPE_HEIGHT - 7;
        const int barW = 58, barH = 5;
        canvas.drawRect(barX, barY, barW, barH, ArcadeConfig::COLOR_GREY);
        int fillW = ((barW - 2) * _health) / HEALTH_MAX;
        if (fillW > 0) {
            uint16_t c = (_health > 60) ? ArcadeConfig::COLOR_GREEN
                       : (_health > 30) ? ArcadeConfig::COLOR_AMBER
                                        : ArcadeConfig::COLOR_RED;
            canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, c);
        }
    }

    void drawGunsight(GFXcanvas16 &canvas, int cx, int cy) {
        canvas.drawFastHLine(cx - 8, cy, 6, ArcadeConfig::COLOR_GREEN);
        canvas.drawFastHLine(cx + 3, cy, 6, ArcadeConfig::COLOR_GREEN);
        canvas.drawFastVLine(cx, cy - 8, 6, ArcadeConfig::COLOR_GREEN);
        canvas.drawFastVLine(cx, cy + 3, 6, ArcadeConfig::COLOR_GREEN);
    }

    // The barrel is drawn in 2D rather than as a 3D object because it's
    // rigidly bolted to the vehicle the camera sits inside — it should never
    // move relative to the screen, so a screen-space shape is both simpler
    // and strictly more correct than positioning a mesh in front of the
    // camera every frame.
    void drawBarrel(GFXcanvas16 &canvas) {
        const int cx = canvas.width() / 2;
        const int base = canvas.height() - 1;
        const int tipY = canvas.height() - 34;
        const uint16_t body = rgb565(9, 18, 10);
        const uint16_t edge = rgb565(16, 32, 18);

        canvas.fillTriangle(cx - 13, base, cx + 13, base, cx + 6, tipY, body);
        canvas.fillTriangle(cx - 13, base, cx + 6, tipY, cx - 6, tipY, body);
        canvas.drawLine(cx - 13, base, cx - 6, tipY, edge);
        canvas.drawLine(cx + 13, base, cx + 6, tipY, edge);
        canvas.drawFastHLine(cx - 6, tipY, 13, edge);

        if ((long)(_muzzleFlashUntil - millis()) > 0) {
            canvas.fillCircle(cx, tipY - 2, 5, ArcadeConfig::COLOR_YELLOW);
            canvas.fillCircle(cx, tipY - 2, 2, ArcadeConfig::COLOR_WHITE);
        }
    }

    // A red border rather than a full-screen tint: at 160x128 a full flash
    // hides the very thing you need to see after being hit.
    void drawDamageFlash(GFXcanvas16 &canvas) {
        if ((long)(_damageFlashUntil - millis()) <= 0) return;
        const int w = canvas.width(), h = canvas.height();
        for (int i = 0; i < 3; ++i) {
            canvas.drawRect(i, 11 + i, w - i * 2, h - 11 - i * 2, ArcadeConfig::COLOR_RED);
        }
    }

public:
    TankFluxGame() {}

    void init(AudioEngine &audio) override {
        loadHighScore();
        _phase = PHASE_ATTRACT;
        _btnBWasHeld = true;
    }

    bool update(GFXcanvas16 &canvas,
                const InputState &input,
                AudioEngine &audio) override {
        ensureSceneReady(canvas);

        // --- Button B: require release first, then hold 2s to exit ---
        static unsigned long btnBHoldStart = 0;
        if (_btnBWasHeld) {
            if (!input.btnB) _btnBWasHeld = false;
        } else if (input.btnB) {
            if (btnBHoldStart == 0) btnBHoldStart = millis();
            if (millis() - btnBHoldStart > 2000) {
                btnBHoldStart = 0;
                audio.mute();
                return false;
            }
        } else {
            btnBHoldStart = 0;
        }

        // ---- PHASE: ATTRACT ----
        if (_phase == PHASE_ATTRACT) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            canvas.setFont();
            canvas.setTextSize(1);
            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(34, 34);
            canvas.print("TANK FLUX");
            canvas.setTextColor(ArcadeConfig::COLOR_GREY);
            canvas.setCursor(14, 54);
            canvas.print("[JOY] DRIVE / TURN");
            canvas.setCursor(32, 66);
            canvas.print("BEST: "); canvas.print(_highScore);
            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(18, 92);
            canvas.print("[BTN A] TO START");

            if (input.btnBPressed) {
                audio.mute();
                return false;
            }
            if (input.btnAPressed) startNewGame(audio);
            return true;
        }

        // ---- PHASE: GAME OVER ----
        if (_phase == PHASE_GAMEOVER) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            canvas.setTextColor(ArcadeConfig::COLOR_RED);
            canvas.setTextSize(2);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4 - 12, 15);
            canvas.print("DESTROYED");

            canvas.setTextSize(1);
            canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 45);
            canvas.print("SCORE: "); canvas.print(_score);

            if (_score >= _highScore && _score > 0) {
                canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
                canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
                canvas.print("NEW HIGH SCORE!!");
            } else {
                canvas.setTextColor(ArcadeConfig::COLOR_GREY);
                canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
                canvas.print("BEST: "); canvas.print(_highScore);
            }

            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(20, 90);
            canvas.print("[BTN A] PLAY AGAIN");
            canvas.setCursor(20, 103);
            canvas.print("[BTN B] QUIT");

            unsigned long elapsed = millis() - _gameOverEnteredMs;
            if (input.btnAPressed) { startNewGame(audio); return true; }
            if (input.btnBPressed) { audio.mute(); return false; }
            if (elapsed > GAMEOVER_TIMEOUT_MS) {
                _phase = PHASE_ATTRACT;
                _btnBWasHeld = false;
            }
            return true;
        }

        // ---- PHASE: PLAYING ----
        updateDriving(input);
        updateKits(audio);
        tryFire(input, audio);
        updateEnemies(audio);
        updateShells(audio);

        _scene->render();
        _particles.update(1.0f / 60.0f);
        _particles.render(_scene, &_camera, canvas.width(), canvas.height());

        drawBarrel(canvas);
        drawGunsight(canvas, canvas.width() / 2, 11 + (canvas.height() - 11) / 2);
        drawDamageFlash(canvas);
        drawHUD(canvas);

        if (_health <= 0) {
            if (_score > _highScore) { _highScore = _score; saveHighScore(); }
            _phase = PHASE_GAMEOVER;
            _gameOverEnteredMs = millis();
            audio.playTone(150, 400);
        }

        return true;
    }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Tank Flux"; }
};

#endif // TANK_FLUX_GAME_H
