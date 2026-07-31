#ifndef RUNNER_POWERUP_MANAGER_H
#define RUNNER_POWERUP_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"
#include "PlayerRunner.h"

// =============================================================================
// RUNNER POWER-UP MANAGER
// A single star pickup that grants brief invincibility. Renders a small
// halo each frame via ParticleManager::spawnExplosion at low count/lifespan
// so it reads as a continuous shimmer rather than a one-shot burst.
// =============================================================================
class RunnerPowerUpManager {
private:
    float _x, _y;
    bool  _active;
    unsigned long _nextHaloTick;

public:
    RunnerPowerUpManager() : _x(0), _y(0), _active(false), _nextHaloTick(0) {}

    void reset() { _active = false; }

    // Gated to start just ahead of the flying enemy tier — there's no point
    // handing out contact-immunity before anything can hit the player.
    void maybeSpawn(int tier, float rightEdgeX, int groundY) {
        if (_active) return;
        if (tier < ArcadeConfig::RUNNER_ENEMY_TIER - 1) return;
        if (random(0, 400) == 0) {
            _x      = rightEdgeX + random(20, 60);
            _y      = groundY - random(20, 40);
            _active = true;
        }
    }

    void update(float scrollSpeed, PlayerRunner &player, ParticleManager &particles,
                AudioEngine &audio, bool &uiNeedsUpdate) {
        if (!_active) return;

        _x -= scrollSpeed;
        if (_x < -10) { _active = false; return; }

        if (millis() >= _nextHaloTick) {
            particles.spawnExplosion(_x, _y, ArcadeConfig::COLOR_YELLOW, 3, 250);
            _nextHaloTick = millis() + 90;
        }

        float px = player.getX(), py = player.getY();
        float dx = _x - (px + RUNNER_WIDTH / 2.0f);
        float dy = _y - (py + RUNNER_HEIGHT / 2.0f);
        if ((dx * dx + dy * dy) < 100.0f) {
            player.activateInvincibility(ArcadeConfig::RUNNER_INVINCIBLE_MS);
            audio.playSound(1000, 80);
            uiNeedsUpdate = true;
            _active = false;
        }
    }

    void render(GFXcanvas16 &canvas) {
        if (!_active) return;
        canvas.fillCircle((int)_x, (int)_y, 3, ArcadeConfig::COLOR_YELLOW);
    }
};

#endif // RUNNER_POWERUP_MANAGER_H
