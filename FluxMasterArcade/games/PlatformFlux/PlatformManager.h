#ifndef PLATFORM_MANAGER_H
#define PLATFORM_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "PlayerRunner.h"

// =============================================================================
// PLATFORM MANAGER
// Pool of scrolling platform segments, modeled on AsteroidManager's
// pool-and-recycle pattern. Segments spawn off the right edge and recycle
// once they scroll past the left edge, with gap/width/height chosen from
// the current difficulty tier.
// =============================================================================
class PlatformManager {
private:
    struct Platform {
        float x;
        int   y;       // top surface Y
        int   width;
        bool  active;
        bool  isMoving;
        float baseY;
        float bobPhase;
    };

    static const int POOL_SIZE = 6;
    Platform _pool[POOL_SIZE];
    float    _scrollSpeed;
    int      _tier;
    unsigned long _distance;

    int groundLevel() const {
        return ArcadeConfig::LANDSCAPE_HEIGHT - 8;
    }

    void spawnPlatform(int index, float startX) {
        int minGap = 18 + _tier * 2;
        int maxGap = 30 + _tier * 4;
        int width  = random(24, 45) - _tier;
        if (width < 16) width = 16;

        _pool[index].x       = startX + random(minGap, maxGap);
        _pool[index].width   = width;
        _pool[index].active  = true;

        // Height varies more as tiers progress; stays reachable by jump.
        int maxRise = min(34, 14 + _tier * 4);
        _pool[index].baseY   = groundLevel() - random(0, maxRise);
        _pool[index].y       = (int)_pool[index].baseY;

        // Moving platforms unlock from tier 2 onward.
        _pool[index].isMoving = (_tier >= 2) && (random(0, 4) == 0);
        _pool[index].bobPhase = random(0, 628) / 100.0f; // 0..2pi
    }

public:
    PlatformManager() : _scrollSpeed(ArcadeConfig::RUNNER_BASE_SCROLL_SPEED),
                         _tier(0), _distance(0) {
        for (int i = 0; i < POOL_SIZE; i++) _pool[i].active = false;
    }

    void initGame() {
        _scrollSpeed = ArcadeConfig::RUNNER_BASE_SCROLL_SPEED;
        _tier        = 0;
        _distance    = 0;

        // First platform is always a safe, wide starting ledge under the player.
        _pool[0].x        = 0;
        _pool[0].width    = 60;
        _pool[0].baseY    = groundLevel();
        _pool[0].y        = groundLevel();
        _pool[0].active   = true;
        _pool[0].isMoving = false;

        float cursor = (float)_pool[0].width;
        for (int i = 1; i < POOL_SIZE; i++) {
            spawnPlatform(i, cursor);
            cursor = _pool[i].x + _pool[i].width;
        }
    }

    // Advances difficulty tier based on distance travelled.
    void advanceDifficulty() {
        _distance++;
        int newTier = _distance / ArcadeConfig::RUNNER_TIER_DISTANCE;
        if (newTier != _tier) {
            _tier = newTier;
            _scrollSpeed += ArcadeConfig::RUNNER_SPEED_STEP;
            if (_scrollSpeed > ArcadeConfig::RUNNER_MAX_SCROLL_SPEED) {
                _scrollSpeed = ArcadeConfig::RUNNER_MAX_SCROLL_SPEED;
            }
        }
    }

    void update() {
        float rightmostEdge = 0;
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;
            _pool[i].x -= _scrollSpeed;

            if (_pool[i].isMoving) {
                _pool[i].bobPhase += 0.04f;
                _pool[i].y = (int)(_pool[i].baseY + sinf(_pool[i].bobPhase) * 10.0f);
            }

            float edge = _pool[i].x + _pool[i].width;
            if (edge > rightmostEdge) rightmostEdge = edge;

            if (edge < 0) {
                spawnPlatform(i, rightmostEdge);
                rightmostEdge = _pool[i].x + _pool[i].width;
            }
        }
    }

    // Returns the ground-level Y the player should collide with given their
    // current footprint, or -1 if the player is over a gap (falling).
    int groundYAt(float playerX, float playerRight, float playerY, float playerBottom) const {
        int best = -1;
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;
            if (playerRight <= _pool[i].x || playerX >= _pool[i].x + _pool[i].width) continue;
            // Only count platforms the player is at/above (landing from a fall,
            // not clipping through from below).
            if (playerBottom <= _pool[i].y + 6) {
                if (best == -1 || _pool[i].y < best) best = _pool[i].y;
            }
        }
        return best;
    }

    bool isOverPit(float playerX, float playerRight) const {
        return groundYAt(playerX, playerRight, 0, 0) == -1;
    }

    float getScrollSpeed() const { return _scrollSpeed; }
    int   getTier() const { return _tier; }
    unsigned long getDistance() const { return _distance; }

    void render(GFXcanvas16 &canvas) {
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;
            uint16_t color = _pool[i].isMoving ? ArcadeConfig::COLOR_CYAN
                                                : ArcadeConfig::COLOR_GREEN;
            canvas.fillRect((int)_pool[i].x, _pool[i].y,
                            _pool[i].width, ArcadeConfig::LANDSCAPE_HEIGHT - _pool[i].y,
                            color);
        }
    }
};

#endif // PLATFORM_MANAGER_H
