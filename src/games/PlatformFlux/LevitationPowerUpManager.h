#ifndef LEVITATION_POWERUP_MANAGER_H
#define LEVITATION_POWERUP_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "PlayerRunner.h"
#include "PlatformManager.h"

// =============================================================================
// LEVITATION POWER-UP MANAGER
// A diamond pickup granting 10s of free vertical flight (see
// PlayerRunner::activateLevitation). Rarer than the star, and biased to
// appear just ahead of an upcoming fire pit when one is queued in the
// platform pool (from the second loop, see maybeSpawn), rather than
// spawning at a purely random moment.
// =============================================================================
class LevitationPowerUpManager {
private:
    float _x, _y;
    bool  _active;
    unsigned long _nextHaloTick;
    unsigned long _cooldownUntil;
    bool  _pendingFirePitSpawn;
    float _pendingFirePitX;

public:
    LevitationPowerUpManager() : _x(0), _y(0), _active(false), _nextHaloTick(0),
                                  _cooldownUntil(0), _pendingFirePitSpawn(false),
                                  _pendingFirePitX(0) {}

    void reset() {
        _active = false;
        _cooldownUntil = 0;
        _pendingFirePitSpawn = false;
    }

    // hasFirePitAhead/firePitX come from PlatformManager::upcomingFirePitX —
    // when a fire pit is queued just off-screen, spawn a bit ahead of it
    // instead of rolling purely at random. Pits are only in the first two
    // stages of each loop: on the first loop they're short and the diamond
    // stays away (from stage 3); from the second, where they're wide, it
    // turns up in front of them too.
    void maybeSpawn(int tier, int loop, float rightEdgeX, const PlatformManager &platforms,
                    bool hasFirePitAhead, float firePitX) {
        if (_active || millis() < _cooldownUntil) return;
        if (tier < 2 && loop == 0) return;

        if (hasFirePitAhead) {
            if (!_pendingFirePitSpawn) {
                _pendingFirePitSpawn = true;
                _pendingFirePitX     = firePitX;
            }
            return; // actual spawn happens in update() once it scrolls into range
        }

        // Rare random roll — roughly half as frequent as the star.
        if (random(0, 900) == 0) {
            _x = rightEdgeX + random(20, 60);
            // Clear whatever platform (if any) ends up under this X so the
            // pickup never spawns inside a slab — floats above its surface.
            // Still has to be reached by a normal jump before levitation
            // activates, so bounded the same way the star is.
            int surfaceY = platforms.surfaceYNear(_x - 6, _x + 6);
            int maxY = surfaceY - 20;
            int minY = surfaceY - ArcadeConfig::RUNNER_MAX_REACHABLE_RISE;
            if (minY < ArcadeConfig::UI_MARGIN_TOP + 12) minY = ArcadeConfig::UI_MARGIN_TOP + 12;
            if (maxY < minY) maxY = minY;
            _y      = random(minY, maxY + 1);
            _active = true;
        }
    }

    // True the frame the runner picks it up (the game plays the sound).
    bool update(float scrollSpeed, PlayerRunner &player, ParticleManager &particles,
                bool &uiNeedsUpdate, const PlatformManager &platforms) {
        // Trigger the pending fire-pit-ahead spawn once it's close enough to
        // place comfortably in front of the pit rather than off-screen.
        if (_pendingFirePitSpawn && !_active) {
            _pendingFirePitX -= scrollSpeed;
            if (_pendingFirePitX < ArcadeConfig::LANDSCAPE_WIDTH - 20) {
                _x = _pendingFirePitX - 40;
                // Fire pits are ground-level gaps, but the platform just
                // before one could still be elevated — check its actual
                // surface instead of assuming ground height.
                int surfaceY = platforms.surfaceYNear(_x - 6, _x + 6);
                int minY = ArcadeConfig::UI_MARGIN_TOP + 12;
                int maxY = surfaceY - 20;
                if (maxY < minY) maxY = minY;
                _y = maxY;
                _active = true;
                _pendingFirePitSpawn = false;
            }
        }

        if (!_active) return false;

        _x -= scrollSpeed;
        if (_x < -10) { _active = false; return false; }

        if (millis() >= _nextHaloTick) {
            particles.spawnExplosion(_x, _y, ArcadeConfig::COLOR_ION_BLUE, 3, 250);
            _nextHaloTick = millis() + 90;
        }

        float px = player.getX(), py = player.getY();
        float dx = _x - (px + RUNNER_WIDTH / 2.0f);
        float dy = _y - (py + RUNNER_HEIGHT / 2.0f);
        if ((dx * dx + dy * dy) < 100.0f) {
            player.activateLevitation(ArcadeConfig::RUNNER_LEVITATE_MS);
            uiNeedsUpdate  = true;
            _active        = false;
            _cooldownUntil = millis() + 10000UL;
            return true;
        }
        return false;
    }

    void render(GFXcanvas16 &canvas) {
        if (!_active) return;
        int cx = (int)_x, cy = (int)_y;
        // Diamond: two mirrored triangles, distinct from the star's filled circle.
        canvas.fillTriangle(cx, cy - 4, cx - 4, cy, cx + 4, cy, ArcadeConfig::COLOR_ION_BLUE);
        canvas.fillTriangle(cx, cy + 4, cx - 4, cy, cx + 4, cy, ArcadeConfig::COLOR_ION_BLUE);
    }
};

#endif // LEVITATION_POWERUP_MANAGER_H
