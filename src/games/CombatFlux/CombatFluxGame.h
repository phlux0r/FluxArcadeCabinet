#ifndef COMBAT_FLUX_GAME_H
#define COMBAT_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Preferences.h>
#include <math.h>

#include <Jet.hpp>

// =============================================================================
// 3D COMBAT FLUX — a third-person space shooter rendered with Jet
// (https://github.com/CubeCoders/Jet), the cabinet's first 3D game.
//
// The player's ship sits at a fixed position (it's a turret, not a flying
// ship — there's no forward travel); aiming with the joystick swings both
// the ship's facing and a chase camera pulled back behind and above it.
// Low-poly enemies of mixed shapes spawn far down +Z and close in. A fixed
// reticle sits at screen centre and BTN A fires a hitscan through Jet's own
// screen-space picking (Scene::setPickQueries/getPickResults).
//
// Because the ship never moves, dodging isn't a mechanic — whether an
// enemy is ever on a collision course is decided by its spawn offset, and
// the only counterplay is shooting it down before it arrives. An enemy
// that closes in without coming near the ship just passes by harmlessly;
// one that does collide ends the run.
//
// Jet's per-frontend render config lives in include/JetConfig.hpp at the
// project root (see that file for why the numbers here are what they are).
// =============================================================================

class CombatFluxGame : public IGame {
private:
    static const int   MAX_ENEMIES   = 3;
    static const int32_t SPAWN_Z     = 3600;   // enemies appear here...
    static const int32_t KILL_Z      = 260;    // ...and are checked for a hit once they reach here
    static const int32_t ENEMY_SPEED = 9;      // world units/frame, closing
    static const int32_t ENEMY_BASE  = 260;
    static const int32_t ENEMY_HEIGHT = 340;
    static const int32_t SPAWN_X_RANGE = 1300;
    static const int32_t SPAWN_Y_RANGE = 750;

    // Squared collision radius (ship + enemy combined) tested against the
    // ship's fixed position when an enemy reaches KILL_Z. Playtest-tunable —
    // this trades off how often an enemy is actually a threat vs. just a
    // target of opportunity.
    static const int32_t COLLISION_RADIUS = 450;

    static constexpr float AIM_SPEED    = 1.6f;   // deg/frame at full joystick deflection
    static constexpr float YAW_LIMIT    = 34.0f;
    static constexpr float PITCH_LIMIT  = 22.0f;

    static const unsigned long RESPAWN_MIN_MS = 500;
    static const unsigned long RESPAWN_MAX_MS = 1500;
    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    // Ship + chase camera. The ship sits at a fixed world position; the
    // camera is re-derived from it every frame using the current aim
    // (pitch/yaw), pulled back along the aim's own forward vector so it
    // tracks smoothly instead of swinging around a screen-fixed point.
    static const int32_t SHIP_Y      = -60;
    static const int32_t CHASE_DIST  = 420;
    static const int32_t CHASE_HEIGHT = 140;

    // Floor grid — purely a spatial reference ("a world" to fly over) so the
    // aim has a horizon to read against; it doesn't interact with gameplay.
    static const int32_t FLOOR_Y      = -900;
    static const int32_t FLOOR_Z_CTR  = 3000;
    static const int32_t FLOOR_WIDTH  = 5000;
    static const int32_t FLOOR_DEPTH  = 7000;
    static const int32_t FLOOR_ROWS   = 6;
    static const int32_t FLOOR_COLS   = 6;

    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_GAMEOVER };
    GamePhase _phase = PHASE_ATTRACT;

    struct Enemy {
        Renderer::Object* obj = nullptr;
        bool alive = false;
        unsigned long respawnAt = 0;
    };
    Enemy _enemies[MAX_ENEMIES];

    // Jet scene state. The Scene itself needs a framebuffer pointer, which
    // only exists once update() hands us the launcher's canvas, so it (and
    // the enemy/ship/floor meshes, which Scene::addObject borrows a pointer
    // to) are built lazily on the first update() call rather than in init().
    Renderer::Scene*  _scene = nullptr;
    Renderer::Camera  _camera;
    Renderer::Object* _shipHull  = nullptr;
    Renderer::Object* _shipWings = nullptr;
    Renderer::Object* _floor     = nullptr;
    // Vector3 is declared at global scope in Jet (Shader.hpp), unlike Color.
    Renderer::DirectionalLight _sun{ Vector3{40, 55, 0}, Renderer::Color{255, 235, 210}, 230 };
    Renderer::AmbientLight     _amb{ Renderer::Color{55, 60, 85} };
    Renderer::Material         _enemyMat{ ArcadeConfig::COLOR_ORANGE, nullptr, nullptr, false, 255, 255, 60 };
    Renderer::Material         _shipMat{ ArcadeConfig::COLOR_CYAN };
    Renderer::Material         _floorMat{ ArcadeConfig::COLOR_ION_BLUE };
    Renderer::ParticleSystem   _particles{ (float)JET32_WORLD_SCALE };

    float _yawDeg   = 0.0f;
    float _pitchDeg = 0.0f;

    int  _score     = 0;
    int  _highScore = 0;
    bool _uiDirty   = true;

    bool _btnBWasHeld = false;
    unsigned long _gameOverEnteredMs = 0;

    Preferences _prefs;

    void loadHighScore() {
        _prefs.begin("cf_data", true);
        _highScore = _prefs.getInt("highscore", 0);
        _prefs.end();
    }

    void saveHighScore() {
        _prefs.begin("cf_data", false);
        _prefs.putInt("highscore", _highScore);
        _prefs.end();
    }

    void ensureSceneReady(GFXcanvas16 &canvas) {
        if (_scene) return;

        _scene = new Renderer::Scene(canvas.getBuffer(), nullptr,
                                     canvas.width(), canvas.height());
        // A faint dark blue rather than pure black — cheap "sky" so the void
        // doesn't read as broken rendering.
        _scene->setBackcolor(0x0011);
        _scene->setClearBuffer(true);

        _camera.setFOV(72, canvas.width());
        _camera.nearPlane = 64;
        _camera.farPlane  = SPAWN_Z + CHASE_DIST + 400;
        _scene->setCamera(&_camera);

        // UNLIT: raw material colour, fully bright regardless of face angle —
        // easier to spot than lit shading on a screen this small.
        _enemyMat.shadingMode = Renderer::ShadingMode::UNLIT;
        _shipMat.shadingMode  = Renderer::ShadingMode::UNLIT;
        // WIREFRAME reads far more clearly than a filled checker did at this
        // resolution once distance fog is involved — a filled floor washed
        // out to a flat colour; the grid lines stay legible.
        _floorMat.shadingMode = Renderer::ShadingMode::WIREFRAME;

        _scene->setDirectionalLight(&_sun);
        _scene->setAmbientLight(&_amb);

        // Enemies cycle through a fixed shape per slot rather than swapping
        // meshes at spawn time, so silhouette variety costs nothing at
        // runtime — each slot just always looks like what it looks like.
        for (int i = 0; i < MAX_ENEMIES; ++i) {
            Renderer::Object* obj;
            switch (i % 3) {
                case 0:  obj = Primitives::createPyramid(ENEMY_BASE, ENEMY_HEIGHT, &_enemyMat); break;
                case 1:  obj = Primitives::createCube(ENEMY_BASE, ENEMY_BASE, ENEMY_BASE, &_enemyMat); break;
                default: obj = Primitives::createCapsule(ENEMY_BASE / 2, ENEMY_HEIGHT, 6, &_enemyMat); break;
            }
            obj->enabled = false;
            _scene->addObject(obj);
            _enemies[i].obj = obj;
        }

        // Player ship: a simple fuselage + wings, fixed at the world origin
        // (the "turret" position everything else — spawn offsets, collision
        // — is measured against).
        _shipHull = Primitives::createCube(120, 70, 280, &_shipMat);
        _shipHull->setPosition(0, SHIP_Y, 0);
        _scene->addObject(_shipHull);

        _shipWings = Primitives::createCube(320, 22, 90, &_shipMat);
        _shipWings->setPosition(0, SHIP_Y, -40);
        _scene->addObject(_shipWings);

        _floor = Primitives::createGrid(FLOOR_WIDTH, FLOOR_DEPTH, FLOOR_ROWS, FLOOR_COLS,
                                        &_floorMat, &_floorMat);
        _floor->setPosition(0, FLOOR_Y, FLOOR_Z_CTR);
        _scene->addObject(_floor);
    }

    // Aim-derived forward vector, used only to place the chase camera — a
    // simple, self-consistent approximation rather than a bit-exact match
    // to Jet's own fixed-point rotation matrix (not needed here: nothing
    // hit-tests against it, it just has to move smoothly with the aim).
    void updateCamera() {
        float yawRad   = radians(_yawDeg);
        float pitchRad = radians(_pitchDeg);
        float fx = sinf(yawRad) * cosf(pitchRad);
        float fy = sinf(pitchRad);
        float fz = cosf(yawRad) * cosf(pitchRad);

        int32_t camX = (int32_t)(0        - fx * CHASE_DIST);
        int32_t camY = (int32_t)(SHIP_Y   - fy * CHASE_DIST + CHASE_HEIGHT);
        int32_t camZ = (int32_t)(0        - fz * CHASE_DIST);

        _camera.setPosition(camX, camY, camZ);
        _camera.setRotation((int32_t)_pitchDeg, (int32_t)_yawDeg, 0);
    }

    void resetEnemies() {
        for (auto &e : _enemies) {
            e.alive = false;
            e.respawnAt = millis() + random((long)RESPAWN_MIN_MS, (long)RESPAWN_MAX_MS);
            if (e.obj) e.obj->enabled = false;
        }
    }

    void spawnEnemy(Enemy &e) {
        int32_t x = random(-SPAWN_X_RANGE, SPAWN_X_RANGE + 1);
        int32_t y = random(-SPAWN_Y_RANGE, SPAWN_Y_RANGE + 1);
        e.obj->setPosition(x, y, SPAWN_Z);
        e.obj->setRotation(0, 0, 0);
        e.obj->enabled = true;
        e.alive = true;
    }

    void retireEnemy(Enemy &e, AudioEngine &audio, bool killedByPlayer) {
        Renderer::Vec3f pos{ (float)e.obj->position.x,
                             (float)e.obj->position.y,
                             (float)e.obj->position.z };
        if (killedByPlayer) _particles.emitSparks(pos, Renderer::Vec3f{0, 1, 0}, 420.0f, 20);

        e.alive = false;
        e.obj->enabled = false;
        e.respawnAt = millis() + random((long)RESPAWN_MIN_MS, (long)RESPAWN_MAX_MS);

        if (killedByPlayer) {
            _score += 10;
            if (_score > _highScore) { _highScore = _score; saveHighScore(); }
            audio.playTone(1500, 60);
            _uiDirty = true;
        }
    }

    bool collidesWithShip(const Enemy &e) const {
        int64_t dx = e.obj->position.x - 0;
        int64_t dy = e.obj->position.y - SHIP_Y;
        int64_t dz = e.obj->position.z - 0;
        int64_t distSq = dx * dx + dy * dy + dz * dz;
        return distSq <= (int64_t)COLLISION_RADIUS * COLLISION_RADIUS;
    }

    void triggerGameOver(AudioEngine &audio) {
        Renderer::Vec3f shipPos{ 0, (float)SHIP_Y, 0 };
        _particles.emitSparks(shipPos, Renderer::Vec3f{0, 1, 0}, 500.0f, 32);
        _phase = PHASE_GAMEOVER;
        _gameOverEnteredMs = millis();
        audio.playTone(150, 400);
    }

    void startNewGame(AudioEngine &audio) {
        _score    = 0;
        _yawDeg   = 0.0f;
        _pitchDeg = 0.0f;
        _uiDirty  = true;
        resetEnemies();
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
    }

    void drawReticle(GFXcanvas16 &canvas, int cx, int cy) {
        canvas.drawFastHLine(cx - 5, cy, 11, ArcadeConfig::COLOR_GREEN);
        canvas.drawFastVLine(cx, cy - 5, 11, ArcadeConfig::COLOR_GREEN);
        canvas.drawPixel(cx, cy, ArcadeConfig::COLOR_WHITE);
    }

public:
    CombatFluxGame() {}

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
        // (same convention as the other rotation-1 games — see AsteroidFluxGame)
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
            canvas.setCursor(16, 40);
            canvas.print("3D COMBAT FLUX");
            canvas.setTextColor(ArcadeConfig::COLOR_GREY);
            canvas.setCursor(10, 60);
            canvas.print("BEST: "); canvas.print(_highScore);
            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(18, 90);
            canvas.print("[BTN A] TO START");

            if (input.btnBPressed) {
                audio.mute();
                return false;
            }
            if (input.btnAPressed) {
                startNewGame(audio);
            }
            return true;
        }

        // ---- PHASE: GAME OVER ----
        if (_phase == PHASE_GAMEOVER) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            canvas.setTextColor(ArcadeConfig::COLOR_RED);
            canvas.setTextSize(2);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4 - 12, 15);
            canvas.print("GAME OVER");

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
            if (elapsed > (GAMEOVER_TIMEOUT_MS - 10000UL)) {
                int secsLeft = (int)((GAMEOVER_TIMEOUT_MS - elapsed) / 1000UL) + 1;
                canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
                canvas.setCursor(20, 116);
                canvas.print("AUTO: "); canvas.print(secsLeft); canvas.print("s");
            }

            if (input.btnAPressed) {
                startNewGame(audio);
                return true;
            }
            if (input.btnBPressed) {
                audio.mute();
                return false;
            }
            if (elapsed > GAMEOVER_TIMEOUT_MS) {
                _phase = PHASE_ATTRACT;
                _btnBWasHeld = false;
            }
            return true;
        }

        // ---- PHASE: PLAYING ----

        // Aim. The joystick is physically mounted rotated relative to this
        // landscape orientation, so X/Y are swapped here the same way
        // AsteroidFluxGame swaps them for its ship movement. Pitch sign was
        // flipped after playtesting felt backwards on the vertical axis.
        _pitchDeg += input.joyX * AIM_SPEED;
        _yawDeg   += input.joyY * AIM_SPEED;
        _pitchDeg = constrain(_pitchDeg, -PITCH_LIMIT, PITCH_LIMIT);
        _yawDeg   = constrain(_yawDeg,   -YAW_LIMIT,   YAW_LIMIT);
        updateCamera();

        // Advance enemies: spawn, close in, and either collide with the
        // fixed-position ship (game over) or pass by harmlessly.
        for (auto &e : _enemies) {
            if (!e.alive) {
                if ((long)(millis() - e.respawnAt) >= 0) spawnEnemy(e);
                continue;
            }
            e.obj->translate(0, 0, -ENEMY_SPEED);
            e.obj->rotate(0, 3, 0);  // slow tumble, purely cosmetic

            if (e.obj->position.z <= KILL_Z) {
                if (collidesWithShip(e)) {
                    triggerGameOver(audio);
                    break;
                }
                retireEnemy(e, audio, /*killedByPlayer=*/false);
            }
        }

        if (_phase != PHASE_PLAYING) return true;  // triggerGameOver fired this frame

        const int pickX = canvas.width() / 2;
        const int pickY = 11 + (canvas.height() - 11) / 2;
        Renderer::PickQuery q[1] = { { (int16_t)pickX, (int16_t)pickY } };
        _scene->setPickQueries(q, 1);

        _scene->render();
        _particles.update(1.0f / 60.0f);
        _particles.render(_scene, &_camera, canvas.width(), canvas.height());

        if (input.btnAPressed) {
            const Renderer::PickResult *r = _scene->getPickResults();
            bool hitEnemy = false;
            if (r[0].hit) {
                for (auto &e : _enemies) {
                    if (e.alive && e.obj == r[0].object) {
                        retireEnemy(e, audio, /*killedByPlayer=*/true);
                        hitEnemy = true;
                        break;
                    }
                }
            }
            if (!hitEnemy) audio.playTone(300, 30);
        }

        drawHUD(canvas);
        drawReticle(canvas, pickX, pickY);

        return true;
    }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "3D Combat Flux"; }
};

#endif // COMBAT_FLUX_GAME_H
