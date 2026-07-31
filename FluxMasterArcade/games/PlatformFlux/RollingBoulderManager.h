#ifndef ROLLING_BOULDER_MANAGER_H
#define ROLLING_BOULDER_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"
#include "PlatformManager.h"
#include "PlayerRunner.h"

// =============================================================================
// ROLLING BOULDER MANAGER
// Ground-hazard counterpart to FlyingEnemyManager's falling rocks — same
// jagged-octagon visual language, but rolling along the ground toward the
// player instead of falling from the sky. Unlocks at RUNNER_BOULDER_TIER.
// =============================================================================
class RollingBoulderManager {
private:
    struct Boulder {
        float x;
        float radius;
        float angle;
        float spinSpeed;
        float xOffsets[8];
        float yOffsets[8];
        bool  active;
    };

    static const int MAX_BOULDERS = ArcadeConfig::BOULDER_MAX_ACTIVE;
    Boulder _boulders[MAX_BOULDERS];
    unsigned long _nextSpawnAt;

    void spawnBoulder(int index) {
        _boulders[index].x         = ArcadeConfig::LANDSCAPE_WIDTH + random(10, 40);
        _boulders[index].radius    = 6.0f;
        _boulders[index].angle     = random(0, 360);
        _boulders[index].spinSpeed = random(6, 12);
        _boulders[index].active    = true;
        for (int j = 0; j < 8; j++) {
            float a = j * (PI / 4.0f);
            float r = _boulders[index].radius * (random(70, 131) / 100.0f);
            _boulders[index].xOffsets[j] = cos(a) * r;
            _boulders[index].yOffsets[j] = sin(a) * r;
        }
    }

    void drawJaggedBoulder(GFXcanvas16 &canvas, Boulder &b, int cy, uint16_t color) {
        float rad = b.angle * (PI / 180.0f);
        float cosA = cos(rad), sinA = sin(rad);
        int rx[8], ry[8];
        for (int i = 0; i < 8; i++) {
            rx[i] = (int)(b.x + (b.xOffsets[i] * cosA - b.yOffsets[i] * sinA));
            ry[i] = (int)(cy  + (b.xOffsets[i] * sinA + b.yOffsets[i] * cosA));
        }
        for (int i = 0; i < 8; i++) {
            int n = (i + 1) % 8;
            canvas.drawLine(rx[i], ry[i], rx[n], ry[n], color);
        }
    }

public:
    RollingBoulderManager() : _nextSpawnAt(0) {
        for (int i = 0; i < MAX_BOULDERS; i++) _boulders[i].active = false;
    }

    void initGame() {
        for (int i = 0; i < MAX_BOULDERS; i++) _boulders[i].active = false;
        _nextSpawnAt = millis() + random(ArcadeConfig::BOULDER_SPAWN_MIN_MS, ArcadeConfig::BOULDER_SPAWN_MAX_MS);
    }

    // groundYAt supplies the current ground surface under the boulder's X so
    // it rolls along stairs at the correct height instead of a fixed line.
    void update(int tier, float scrollSpeed, const PlatformManager &platforms,
                PlayerRunner &player, ParticleManager &particles, AudioEngine &audio,
                bool &playerHit) {
        if (tier >= ArcadeConfig::RUNNER_BOULDER_TIER && millis() >= _nextSpawnAt) {
            for (int i = 0; i < MAX_BOULDERS; i++) {
                if (_boulders[i].active) continue;
                spawnBoulder(i);
                _nextSpawnAt = millis() + random(ArcadeConfig::BOULDER_SPAWN_MIN_MS, ArcadeConfig::BOULDER_SPAWN_MAX_MS);
                break;
            }
        }

        float rollSpeed = scrollSpeed * ArcadeConfig::BOULDER_SPEED_BONUS;
        float px = player.getX(), py = player.getY();
        float pRight = px + RUNNER_WIDTH, pBottom = py + RUNNER_HEIGHT;

        for (int i = 0; i < MAX_BOULDERS; i++) {
            if (!_boulders[i].active) continue;

            _boulders[i].x     -= rollSpeed;
            _boulders[i].angle += _boulders[i].spinSpeed;

            int groundY = platforms.surfaceYNear(_boulders[i].x - _boulders[i].radius,
                                                  _boulders[i].x + _boulders[i].radius);
            float cy = groundY - _boulders[i].radius;

            if (_boulders[i].x + _boulders[i].radius < 0) {
                _boulders[i].active = false;
                continue;
            }

            float closestX = max(px, min(_boulders[i].x, pRight));
            float closestY = max(py, min(cy, pBottom));
            float dx = _boulders[i].x - closestX, dy = cy - closestY;
            float distSq = dx * dx + dy * dy;

            if (distSq < (_boulders[i].radius * _boulders[i].radius) && !player.isInvincible()) {
                particles.spawnExplosion(_boulders[i].x, cy, ArcadeConfig::COLOR_AMBER, 8);
                audio.playSound(220, 80);
                _boulders[i].active = false;
                playerHit = true;
            }
        }
    }

    // loopIndex rotates the boulder's color each time the tier cycle wraps
    // (see PlatformManager::getLoop).
    void render(GFXcanvas16 &canvas, const PlatformManager &platforms, int loopIndex = 0) {
        static const uint16_t palette[4] = {
            ArcadeConfig::COLOR_AMBER, ArcadeConfig::COLOR_GREY,
            ArcadeConfig::COLOR_MAGENTA, ArcadeConfig::COLOR_GREEN
        };
        uint16_t color = palette[loopIndex % 4];

        for (int i = 0; i < MAX_BOULDERS; i++) {
            if (!_boulders[i].active) continue;
            int groundY = platforms.surfaceYNear(_boulders[i].x - _boulders[i].radius,
                                                  _boulders[i].x + _boulders[i].radius);
            int cy = (int)(groundY - _boulders[i].radius);
            drawJaggedBoulder(canvas, _boulders[i], cy, color);
        }
    }
};

#endif // ROLLING_BOULDER_MANAGER_H
