#ifndef FLYING_ENEMY_MANAGER_H
#define FLYING_ENEMY_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"
#include "PlayerRunner.h"

// =============================================================================
// FLYING ENEMY MANAGER
// Patrolling flying enemies that periodically drop rocks. The rocks reuse
// AsteroidManager's jagged-octagon look (irregular radius offsets rotated
// each frame) so falling hazards read visually consistent with AsteroidFlux,
// but fall under gravity instead of drifting horizontally.
// =============================================================================
class FlyingEnemyManager {
private:
    struct Enemy {
        float x, y;
        float bobPhase;
        bool  active;
        unsigned long nextDropTime;
    };

    struct Rock {
        float x, y;
        float vy;
        float radius;
        float angle;
        float spinSpeed;
        float xOffsets[6];
        float yOffsets[6];
        bool  active;
    };

    // Capped at 1 — enemies must never be on-screen two at a time.
    static const int MAX_ENEMIES = 1;
    static const int MAX_ROCKS   = 4;

    Enemy _enemies[MAX_ENEMIES];
    Rock  _rocks[MAX_ROCKS];
    int   _activeEnemies;
    bool  _enabled;

    void spawnEnemy(int index, float startX) {
        _enemies[index].x            = startX;
        _enemies[index].y            = random(ArcadeConfig::UI_MARGIN_TOP + 6, 40);
        _enemies[index].bobPhase     = random(0, 628) / 100.0f;
        _enemies[index].active       = true;
        _enemies[index].nextDropTime = millis() + random(1200, 2600);
    }

    void spawnRock(float x, float y) {
        for (int i = 0; i < MAX_ROCKS; i++) {
            if (_rocks[i].active) continue;
            _rocks[i].x         = x;
            _rocks[i].y         = y;
            _rocks[i].vy        = 0.4f;
            _rocks[i].radius    = 4.0f;
            _rocks[i].angle     = random(0, 360);
            _rocks[i].spinSpeed = random(-4, 5);
            _rocks[i].active    = true;
            for (int j = 0; j < 6; j++) {
                float a = j * (PI / 3.0f);
                float r = _rocks[i].radius * (random(70, 131) / 100.0f);
                _rocks[i].xOffsets[j] = cos(a) * r;
                _rocks[i].yOffsets[j] = sin(a) * r;
            }
            return;
        }
    }

    void drawJaggedRock(GFXcanvas16 &canvas, Rock &r) {
        float rad = r.angle * (PI / 180.0f);
        float cosA = cos(rad), sinA = sin(rad);
        int rx[6], ry[6];
        for (int i = 0; i < 6; i++) {
            rx[i] = (int)(r.x + (r.xOffsets[i] * cosA - r.yOffsets[i] * sinA));
            ry[i] = (int)(r.y + (r.xOffsets[i] * sinA + r.yOffsets[i] * cosA));
        }
        for (int i = 0; i < 6; i++) {
            int n = (i + 1) % 6;
            canvas.drawLine(rx[i], ry[i], rx[n], ry[n], ArcadeConfig::COLOR_GREY);
        }
    }

public:
    FlyingEnemyManager() : _activeEnemies(0), _enabled(false) {
        for (int i = 0; i < MAX_ENEMIES; i++) _enemies[i].active = false;
        for (int i = 0; i < MAX_ROCKS; i++)   _rocks[i].active   = false;
    }

    // No enemies at run start — they turn on later as the difficulty tier
    // requires, via setActive(), so the opening stretch stays hazard-free.
    void initGame() {
        _activeEnemies = 0;
        _enabled       = false;
        for (int i = 0; i < MAX_ENEMIES; i++) _enemies[i].active = false;
        for (int i = 0; i < MAX_ROCKS; i++)   _rocks[i].active   = false;
    }

    // Called from PlatformFluxGame every frame with whether the flying
    // enemy should be present at the current tier — unlike a one-shot
    // unlock, this can turn back off (the enemy makes an early appearance
    // at RUNNER_EARLY_SHIP_TIER, then steps aside for the ground-hazard
    // tiers before returning for good at RUNNER_ENEMY_TIER). Turning off
    // does NOT touch any enemy or rocks currently in flight — it only
    // stops new spawns/respawns/rock-drops (see update()), so the last
    // ship finishes its patrol off-screen and any rock it already dropped
    // finishes falling naturally instead of both vanishing mid-air the
    // instant the tier changes.
    void setActive(bool active) {
        _enabled = active;
        if (active && !_enemies[0].active) {
            spawnEnemy(0, ArcadeConfig::LANDSCAPE_WIDTH + 20);
        }
        _activeEnemies = 1;
    }

    void update(float scrollSpeed, PlayerRunner &player, ParticleManager &particles,
                AudioEngine &audio, bool &playerHit) {
        for (int i = 0; i < _activeEnemies; i++) {
            if (!_enemies[i].active) continue;

            _enemies[i].x -= scrollSpeed * 0.6f;   // enemies drift slower than ground
            _enemies[i].bobPhase += 0.05f;

            if (_enemies[i].x < -20) {
                // Only respawn while still enabled — once disabled, let it
                // retire for good instead of looping back in.
                if (_enabled) spawnEnemy(i, ArcadeConfig::LANDSCAPE_WIDTH + random(10, 60));
                else          _enemies[i].active = false;
                continue;
            }

            // Stop dropping new rocks once disabled; existing ones (below)
            // keep falling/colliding until they resolve on their own.
            if (_enabled && millis() >= _enemies[i].nextDropTime) {
                spawnRock(_enemies[i].x, _enemies[i].y + 6);
                _enemies[i].nextDropTime = millis() + random(1400, 3000);
            }
        }

        float px = player.getX(), py = player.getY();
        float pRight = px + RUNNER_WIDTH, pBottom = py + RUNNER_HEIGHT;

        for (int i = 0; i < MAX_ROCKS; i++) {
            if (!_rocks[i].active) continue;

            _rocks[i].vy += 0.06f;   // gravity accel, gentler than the runner's own gravity
            _rocks[i].y  += _rocks[i].vy;
            _rocks[i].angle += _rocks[i].spinSpeed;

            if (_rocks[i].y - _rocks[i].radius > ArcadeConfig::LANDSCAPE_HEIGHT) {
                _rocks[i].active = false;
                continue;
            }

            float closestX = max(px, min(_rocks[i].x, pRight));
            float closestY = max(py, min(_rocks[i].y, pBottom));
            float dx = _rocks[i].x - closestX, dy = _rocks[i].y - closestY;
            float distSq = dx * dx + dy * dy;

            if (distSq < (_rocks[i].radius * _rocks[i].radius) && !player.isInvincible()) {
                particles.spawnExplosion(_rocks[i].x, _rocks[i].y, ArcadeConfig::COLOR_GREY, 8);
                audio.playSound(300, 60);
                _rocks[i].active = false;
                playerHit = true;
            }
        }
    }

    void render(GFXcanvas16 &canvas) {
        for (int i = 0; i < _activeEnemies; i++) {
            if (!_enemies[i].active) continue;
            int ex = (int)_enemies[i].x;
            int ey = (int)(_enemies[i].y + sinf(_enemies[i].bobPhase) * 3.0f);
            // Simple bat-like silhouette: body + two wing strokes
            canvas.fillCircle(ex, ey, 3, ArcadeConfig::COLOR_MAGENTA);
            canvas.drawLine(ex - 6, ey - 2, ex - 2, ey, ArcadeConfig::COLOR_MAGENTA);
            canvas.drawLine(ex + 6, ey - 2, ex + 2, ey, ArcadeConfig::COLOR_MAGENTA);
        }
        for (int i = 0; i < MAX_ROCKS; i++) {
            if (!_rocks[i].active) continue;
            drawJaggedRock(canvas, _rocks[i]);
        }
    }
};

#endif // FLYING_ENEMY_MANAGER_H
