#ifndef COMBAT_FLUX_GAME_H
#define COMBAT_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Preferences.h>

#include <Jet.hpp>

// =============================================================================
// 3D COMBAT FLUX — a first-person turret shooter rendered with Jet
// (https://github.com/CubeCoders/Jet), the cabinet's first 3D game.
//
// The camera sits fixed at the cockpit (world origin) and only rotates; low-
// poly fighters spawn far down +Z and close in along it. A fixed reticle
// sits at screen centre and BTN A fires a hitscan through Jet's own
// screen-space picking (Scene::setPickQueries/getPickResults) rather than
// re-deriving the camera's projection by hand.
//
// Endless score-attack: an enemy that reaches the cockpit unshot just
// despawns — there's no lives/game-over state, only BTN A (hold 2s) to quit.
//
// Jet's per-frontend render config lives in include/JetConfig.hpp at the
// project root (see that file for why the numbers here are what they are).
// =============================================================================

class CombatFluxGame : public IGame {
private:
    static const int   MAX_ENEMIES   = 3;
    static const int32_t SPAWN_Z     = 3600;   // enemies appear here...
    static const int32_t KILL_Z      = 260;    // ...and despawn once they reach here, unshot or not
    static const int32_t ENEMY_SPEED = 9;      // world units/frame, closing
    static const int32_t ENEMY_BASE  = 260;
    static const int32_t ENEMY_HEIGHT = 340;
    static const int32_t SPAWN_X_RANGE = 1300;
    static const int32_t SPAWN_Y_RANGE = 750;

    static constexpr float AIM_SPEED    = 1.6f;   // deg/frame at full joystick deflection
    static constexpr float YAW_LIMIT    = 34.0f;
    static constexpr float PITCH_LIMIT  = 22.0f;

    static const unsigned long RESPAWN_MIN_MS = 500;
    static const unsigned long RESPAWN_MAX_MS = 1500;

    // Floor grid — purely a spatial reference ("a world" to fly over) so the
    // aim has a horizon to read against; it doesn't interact with gameplay.
    static const int32_t FLOOR_Y      = -900;
    static const int32_t FLOOR_Z_CTR  = 3000;
    static const int32_t FLOOR_WIDTH  = 5000;
    static const int32_t FLOOR_DEPTH  = 7000;
    static const int32_t FLOOR_ROWS   = 6;
    static const int32_t FLOOR_COLS   = 6;

    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING };
    GamePhase _phase = PHASE_ATTRACT;

    struct Enemy {
        Renderer::Object* obj = nullptr;
        bool alive = false;
        unsigned long respawnAt = 0;
    };
    Enemy _enemies[MAX_ENEMIES];

    // Jet scene state. The Scene itself needs a framebuffer pointer, which
    // only exists once update() hands us the launcher's canvas, so it (and
    // the enemy/floor meshes, which Scene::addObject borrows a pointer to)
    // are built lazily on the first update() call rather than in init().
    Renderer::Scene*  _scene = nullptr;
    Renderer::Camera  _camera;
    Renderer::Object* _floor = nullptr;
    // Vector3 is declared at global scope in Jet (Shader.hpp), unlike Color.
    Renderer::DirectionalLight _sun{ Vector3{40, 55, 0}, Renderer::Color{255, 235, 210}, 230 };
    Renderer::AmbientLight     _amb{ Renderer::Color{55, 60, 85} };
    Renderer::Material         _enemyMat{ ArcadeConfig::COLOR_ORANGE, nullptr, nullptr, false, 255, 255, 60 };
    Renderer::Material         _floorMatA{ 0x2104 /* dark navy */ };
    Renderer::Material         _floorMatB{ 0x39C8 /* lighter blue-grey */ };
    Renderer::ParticleSystem   _particles{ (float)JET32_WORLD_SCALE };

    float _yawDeg   = 0.0f;
    float _pitchDeg = 0.0f;

    int  _score     = 0;
    int  _highScore = 0;
    bool _uiDirty   = true;

    bool _btnBWasHeld = false;

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

        _camera.setPosition(0, 0, 0);
        _camera.setFOV(72, canvas.width());
        _camera.nearPlane = 64;
        _camera.farPlane  = SPAWN_Z + 400;
        _scene->setCamera(&_camera);

        // UNLIT: raw material colour, fully bright regardless of face angle —
        // easier to spot than lit shading on a screen this small, at the
        // cost of the enemies reading a bit flatter.
        _enemyMat.shadingMode = Renderer::ShadingMode::UNLIT;
        _floorMatA.shadingMode = Renderer::ShadingMode::UNLIT;
        _floorMatB.shadingMode = Renderer::ShadingMode::UNLIT;

        _scene->setDirectionalLight(&_sun);
        _scene->setAmbientLight(&_amb);

        for (auto &e : _enemies) {
            e.obj = Primitives::createPyramid(ENEMY_BASE, ENEMY_HEIGHT, &_enemyMat);
            e.obj->enabled = false;
            _scene->addObject(e.obj);
        }

        _floor = Primitives::createGrid(FLOOR_WIDTH, FLOOR_DEPTH, FLOOR_ROWS, FLOOR_COLS,
                                        &_floorMatA, &_floorMatB);
        _floor->setPosition(0, FLOOR_Y, FLOOR_Z_CTR);
        _scene->addObject(_floor);
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

    // Enemy despawns either way once it reaches KILL_Z — shot down (sparks,
    // score, tone) or simply flown past (silent, no penalty).
    void retireEnemy(Enemy &e, AudioEngine &audio, bool killedByPlayer) {
        e.alive = false;
        e.obj->enabled = false;
        e.respawnAt = millis() + random((long)RESPAWN_MIN_MS, (long)RESPAWN_MAX_MS);

        if (killedByPlayer) {
            Renderer::Vec3f pos{ (float)e.obj->position.x,
                                 (float)e.obj->position.y,
                                 (float)e.obj->position.z };
            _particles.emitSparks(pos, Renderer::Vec3f{0, 1, 0}, 420.0f, 20);
            _score += 10;
            if (_score > _highScore) { _highScore = _score; saveHighScore(); }
            audio.playTone(1500, 60);
            _uiDirty = true;
        }
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

        // ---- PHASE: PLAYING ----

        // Aim. The joystick is physically mounted rotated relative to this
        // landscape orientation, so X/Y are swapped here the same way
        // AsteroidFluxGame swaps them for its ship movement. Pitch sign was
        // flipped after playtesting felt backwards on the vertical axis.
        _pitchDeg += input.joyX * AIM_SPEED;
        _yawDeg   += input.joyY * AIM_SPEED;
        _pitchDeg = constrain(_pitchDeg, -PITCH_LIMIT, PITCH_LIMIT);
        _yawDeg   = constrain(_yawDeg,   -YAW_LIMIT,   YAW_LIMIT);
        _camera.setRotation((int32_t)_pitchDeg, (int32_t)_yawDeg, 0);

        // Advance enemies: spawn, close in, or despawn once they pass the
        // player (shot or not — see retireEnemy).
        for (auto &e : _enemies) {
            if (!e.alive) {
                if ((long)(millis() - e.respawnAt) >= 0) spawnEnemy(e);
                continue;
            }
            e.obj->translate(0, 0, -ENEMY_SPEED);
            e.obj->rotate(0, 3, 0);  // slow tumble, purely cosmetic

            if (e.obj->position.z <= KILL_Z) {
                retireEnemy(e, audio, /*killedByPlayer=*/false);
            }
        }

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
