#ifndef RUNNER_POWERUP_MANAGER_H
#define RUNNER_POWERUP_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "PlayerRunner.h"
#include "PlatformManager.h"

// =============================================================================
// RUNNER POWER-UP MANAGER
// A single star pickup that grants brief invincibility, one every
// RUNNER_STAR_GAP_MIN..MAX_MS at most and never while the runner's already
// invincible. Renders a small
// halo each frame via ParticleManager::spawnExplosion at low count/lifespan
// so it reads as a continuous shimmer rather than a one-shot burst.
// =============================================================================
class RunnerPowerUpManager {
private:
    float _x, _y;
    bool  _active;
    unsigned long _nextHaloTick;
    unsigned long _nextSpawnAt;   // the earliest the next star may come

    // The wait from the last star picked up or gone by (or a life's start).
    void startWait() {
        _nextSpawnAt = millis() + (unsigned long)random((long)ArcadeConfig::RUNNER_STAR_GAP_MIN_MS,
                                                        (long)ArcadeConfig::RUNNER_STAR_GAP_MAX_MS + 1);
    }

public:
    RunnerPowerUpManager() : _x(0), _y(0), _active(false), _nextHaloTick(0), _nextSpawnAt(0) {}

    void reset() { _active = false; startWait(); }

    // Gated to start once contact hazards actually exist — spikes/boulders
    // arrive well before the flying enemy now, so this keys off the second
    // ground-hazard phase rather than the (now much later) enemy tier.
    // `shielded`: the runner's invincible already, so no star comes (the
    // wait, once over, holds until the shield's gone).
    void maybeSpawn(int tier, float rightEdgeX, const PlatformManager &platforms, bool shielded) {
        if (_active || shielded || millis() < _nextSpawnAt) return;
        if (tier < ArcadeConfig::RUNNER_GROUND2_TIER_START) return;
        {
            _x = rightEdgeX + random(20, 60);
            // Clear whatever platform (if any) ends up under this X so the
            // pickup never spawns inside a slab — floats above its surface.
            // Bounded to what a jump can actually reach (not an arbitrary
            // band up toward the top of the screen), so it's never placed
            // higher than the player can get to.
            int surfaceY = platforms.surfaceYNear(_x - 6, _x + 6);
            int maxY = surfaceY - 14;
            int minY = surfaceY - ArcadeConfig::RUNNER_MAX_REACHABLE_RISE;
            if (minY < ArcadeConfig::UI_MARGIN_TOP + 12) minY = ArcadeConfig::UI_MARGIN_TOP + 12;
            if (maxY < minY) maxY = minY;
            _y      = random(minY, maxY + 1);
            _active = true;
        }
    }

    // True the frame the runner picks it up (the game plays the sound).
    bool update(float scrollSpeed, PlayerRunner &player, ParticleManager &particles, bool &uiNeedsUpdate) {
        if (!_active) return false;

        _x -= scrollSpeed;
        if (_x < -10) { _active = false; startWait(); return false; }

        if (millis() >= _nextHaloTick) {
            particles.spawnExplosion(_x, _y, ArcadeConfig::COLOR_YELLOW, 3, 250);
            _nextHaloTick = millis() + 90;
        }

        float px = player.getX(), py = player.getY();
        float dx = _x - (px + RUNNER_WIDTH / 2.0f);
        float dy = _y - (py + RUNNER_HEIGHT / 2.0f);
        if ((dx * dx + dy * dy) < 100.0f) {
            player.activateInvincibility(ArcadeConfig::RUNNER_INVINCIBLE_MS);
            uiNeedsUpdate = true;
            _active = false;
            startWait();
            return true;
        }
        return false;
    }

    void render(GFXcanvas16 &canvas) {
        if (!_active) return;
        canvas.fillCircle((int)_x, (int)_y, 3, ArcadeConfig::COLOR_YELLOW);
    }
};

#endif // RUNNER_POWERUP_MANAGER_H
