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
// BUILD STATUS: step 1 of 4 — driving, world and pickups only. Enemy tanks,
// shells and damage come next; health can't drop yet, so kits will read as
// "already full" until then.
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

    // --- Tank ----------------------------------------------------------------
    static const int32_t EYE_HEIGHT  = 120;
    static const int32_t TANK_RADIUS = 150;

    // Both signs are here as named constants because this project has had to
    // flip joystick axes by trial more than once — the cabinet's stick is
    // physically mounted rotated relative to a landscape (rotation 1) game,
    // so X/Y read swapped, same as AsteroidFluxGame. If driving or turning
    // comes out backwards on hardware, flip the relevant sign here.
    static constexpr float DRIVE_SIGN = 1.0f;
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
    Renderer::Material _groundMat{ ArcadeConfig::COLOR_ION_BLUE };
    // Obstacles are the one lit (GOURAUD) surface: their faces shade
    // differently as you drive around them, which is a real depth cue in a
    // first-person game where the camera is constantly moving.
    Renderer::Material _obstacleMat{ 0x5B0C /* slate */, nullptr, nullptr, false, 255, 255, 30 };
    Renderer::Material _kitMat{ ArcadeConfig::COLOR_GREEN };
    Renderer::ParticleSystem _particles{ (float)JET32_WORLD_SCALE };

    // --- Tank state ----------------------------------------------------------
    float _x = 0.0f, _z = 0.0f;
    float _headingDeg = 0.0f;
    float _speed = 0.0f;

    int _health = HEALTH_MAX;
    int _score = 0;
    int _highScore = 0;

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

    static int32_t obstacleRadius(const ObstacleDef &o) {
        // Effective circular footprint for a square-ish block; a little under
        // its half-diagonal so you can just scrape past a corner.
        return (o.size * 3) / 5;
    }

    void ensureSceneReady(GFXcanvas16 &canvas) {
        if (_scene) return;

        _scene = new Renderer::Scene(canvas.getBuffer(), nullptr,
                                     canvas.width(), canvas.height());
        _scene->setBackcolor(0x0011);   // faint dark blue "sky"
        _scene->setClearBuffer(true);

        _camera.setFOV(70, canvas.width());
        _camera.nearPlane = 48;
        _camera.farPlane  = 5200;
        _scene->setCamera(&_camera);

        _groundMat.shadingMode   = Renderer::ShadingMode::WIREFRAME;
        _obstacleMat.shadingMode = Renderer::ShadingMode::GOURAUD;
        _kitMat.shadingMode      = Renderer::ShadingMode::UNLIT;

        _scene->setDirectionalLight(&_sun);
        _scene->setAmbientLight(&_amb);

        _ground = Primitives::createGrid(GROUND_SIZE, GROUND_SIZE,
                                         GROUND_CELLS, GROUND_CELLS,
                                         &_groundMat, &_groundMat);
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
    }

    bool blocked(float x, float z) const {
        for (int i = 0; i < OBSTACLE_COUNT; ++i) {
            float dx = x - (float)OBSTACLES[i].x;
            float dz = z - (float)OBSTACLES[i].z;
            float r  = (float)(TANK_RADIUS + obstacleRadius(OBSTACLES[i]));
            if (dx * dx + dz * dz < r * r) return true;
        }
        return false;
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

        // Keep the ground grid centred on the tank, snapped to whole cells.
        // The pattern repeats with exactly that period, so the snap is
        // invisible and the ground reads as infinite without paying for a
        // mesh big enough to cover the whole arena at this cell density.
        int32_t gx = ((int32_t)_x / GROUND_CELL) * GROUND_CELL;
        int32_t gz = ((int32_t)_z / GROUND_CELL) * GROUND_CELL;
        _ground->setPosition(gx, 0, gz);
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

            if (_health >= HEALTH_MAX) continue;

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
        for (int i = 0; i < REPAIR_COUNT; ++i) {
            _kits[i].active = true;
            if (_kits[i].obj) _kits[i].obj->enabled = true;
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

        // Health bar along the bottom edge.
        const int barX = 22, barY = ArcadeConfig::LANDSCAPE_HEIGHT - 8;
        const int barW = ArcadeConfig::LANDSCAPE_WIDTH - barX - 6, barH = 5;
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(2, barY - 1);
        canvas.print("HP");
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

        _scene->render();
        _particles.update(1.0f / 60.0f);
        _particles.render(_scene, &_camera, canvas.width(), canvas.height());

        drawHUD(canvas);
        drawGunsight(canvas, canvas.width() / 2, 11 + (canvas.height() - 11) / 2);

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
