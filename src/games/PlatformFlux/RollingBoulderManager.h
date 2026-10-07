#ifndef ROLLING_BOULDER_MANAGER_H
#define ROLLING_BOULDER_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
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
        float cy;       // last centre height, for the points popup
        bool  active;
        bool  scored;   // jumped: points given
    };

    static const int MAX_BOULDERS = ArcadeConfig::BOULDER_MAX_ACTIVE;
    Boulder _boulders[MAX_BOULDERS];
    unsigned long _nextSpawnAt;

    void spawnBoulder(int index) {
        _boulders[index].x         = ArcadeConfig::LANDSCAPE_WIDTH + random(10, 40);
        _boulders[index].radius    = 4.5f; // one size smaller than the original 6.0
        _boulders[index].angle     = random(0, 360);
        _boulders[index].spinSpeed = random(6, 12);
        _boulders[index].active    = true;
        _boulders[index].scored    = false;
        _boulders[index].cy        = 0.0f;
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
    // True if a boulder hit the runner this frame (the game plays the sound).
    // `hold`: none sent now (a Night obstacle ahead wants a duck, a
    // boulder a jump; never both at once).
    bool update(int tier, float scrollSpeed, const PlatformManager &platforms,
                PlayerRunner &player, ParticleManager &particles, bool &playerHit, bool hold = false) {
        bool hit = false;
        if (tier >= ArcadeConfig::RUNNER_BOULDER_TIER && millis() >= _nextSpawnAt && !hold) {
            for (int i = 0; i < MAX_BOULDERS; i++) {
                if (_boulders[i].active) continue;
                spawnBoulder(i);
                _nextSpawnAt = millis() + random(ArcadeConfig::BOULDER_SPAWN_MIN_MS, ArcadeConfig::BOULDER_SPAWN_MAX_MS);
                break;
            }
        }

        float rollSpeed = scrollSpeed * ArcadeConfig::BOULDER_SPEED_BONUS;
        float px = player.getX(), py = player.hitTop();
        float pRight = px + RUNNER_WIDTH, pBottom = player.getY() + RUNNER_HEIGHT;

        for (int i = 0; i < MAX_BOULDERS; i++) {
            if (!_boulders[i].active) continue;

            _boulders[i].x     -= rollSpeed;
            _boulders[i].angle += _boulders[i].spinSpeed;

            int groundY = platforms.surfaceYNear(_boulders[i].x - _boulders[i].radius,
                                                  _boulders[i].x + _boulders[i].radius);
            float cy = groundY - _boulders[i].radius;
            _boulders[i].cy = cy;

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
                _boulders[i].active = false;
                playerHit = true;
                hit = true;
            }
        }
        return hit;
    }

    // A boulder the runner has got past (all of it behind playerX) and
    // hasn't been scored: marks it, gives where to show the points, returns
    // them; 0 when there's none.
    int takeCleared(float playerX, float &popX, float &popY) {
        for (int i = 0; i < MAX_BOULDERS; i++) {
            Boulder &b = _boulders[i];
            if (!b.active || b.scored || b.x + b.radius >= playerX) continue;
            b.scored = true;
            popX = b.x;
            popY = b.cy - b.radius - 8.0f;
            return ArcadeConfig::RUNNER_BOULDER_POINTS;
        }
        return 0;
    }

    // Would a boulder touch this player box `t` frames from now? The box is
    // in screen space; the ground has scrolled t * scrollSpeed by then. For
    // the attract demo's autopilot, with a pixel of margin.
    bool wouldHit(int t, float scrollSpeed, float px, float pRight, float py, float pBottom,
                  const PlatformManager &platforms) const {
        const float roll = scrollSpeed * ArcadeConfig::BOULDER_SPEED_BONUS;
        const float shift = scrollSpeed * (float)t;
        for (int i = 0; i < MAX_BOULDERS; i++) {
            if (!_boulders[i].active) continue;
            float bx = _boulders[i].x - roll * (float)t;
            float r = _boulders[i].radius + 1.0f;
            int groundY = platforms.surfaceYNear(bx - r + shift, bx + r + shift);
            float cy = groundY - _boulders[i].radius;
            float cx = max(px, min(bx, pRight)), cyy = max(py, min(cy, pBottom));
            float dx = bx - cx, dy = cy - cyy;
            if (dx * dx + dy * dy < r * r) return true;
        }
        return false;
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
