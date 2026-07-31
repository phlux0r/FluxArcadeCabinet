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
//
// The run opens with a stretch of flat, contiguous platforms (no gaps, no
// height change, no bobbing) so the player has time to get used to the
// controls before any jump is required.
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
        bool  firePitBefore;   // fire pit rendered in the gap just before this platform
        float firePitGapWidth; // gap width — region [x - firePitGapWidth, x) scrolls with x
    };

    static const int POOL_SIZE = 6;
    Platform _pool[POOL_SIZE];
    float    _scrollSpeed;
    int      _tier;
    unsigned long _distance;
    int      _introPlatformsLeft;

    int groundLevel() const {
        return ArcadeConfig::LANDSCAPE_HEIGHT - 8;
    }

    // Darkens a RGB565 color for mortar lines — same base hue, roughly
    // half brightness, so it reads as grout rather than a different color.
    static uint16_t darken(uint16_t c) {
        uint16_t r = (c >> 11) & 0x1F;
        uint16_t g = (c >> 5)  & 0x3F;
        uint16_t b = c & 0x1F;
        r >>= 1; g >>= 1; b >>= 1;
        return (r << 11) | (g << 5) | b;
    }

    // Two courses of offset bricks within the slab's fixed thickness —
    // a repeating pattern drawn over the existing fill color, not a
    // different texture, so static/moving platform colors stay the same.
    void drawBrickPattern(GFXcanvas16 &canvas, int x, int y, int width, uint16_t baseColor) {
        uint16_t mortar = darken(baseColor);
        static const int BRICK_W = 10;
        static const int BRICK_H = ArcadeConfig::PLATFORM_THICKNESS / 2;

        int midY = y + BRICK_H;
        canvas.drawFastHLine(x, midY, width, mortar);

        for (int row = 0; row < 2; row++) {
            int rowY = y + row * BRICK_H;
            int offset = (row % 2 == 0) ? 0 : BRICK_W / 2;
            for (int bx = x - offset; bx < x + width; bx += BRICK_W) {
                if (bx <= x) continue;
                canvas.drawFastVLine(bx, rowY, BRICK_H, mortar);
            }
        }
    }

    void spawnPlatform(int index, float startX) {
        _pool[index].firePitBefore = false;

        if (_introPlatformsLeft > 0) {
            // Flat, contiguous run — no gap, no height change, no bobbing.
            _introPlatformsLeft--;
            _pool[index].x        = startX;
            _pool[index].width    = random(40, 65);
            _pool[index].baseY    = groundLevel();
            _pool[index].y        = groundLevel();
            _pool[index].active   = true;
            _pool[index].isMoving = false;
            return;
        }

        int minGap = ArcadeConfig::PLATFORM_MIN_GAP + _tier * 2;
        int maxGap = ArcadeConfig::PLATFORM_MAX_GAP + _tier * 3;
        int width  = random(24, 45) - _tier;
        if (width < 16) width = 16;

        _pool[index].x       = startX + random(minGap, maxGap);
        _pool[index].width   = width;
        _pool[index].active  = true;

        // Height varies more as tiers progress; stays reachable by jump.
        int maxRise = min(30, 10 + _tier * 3);
        _pool[index].baseY   = groundLevel() - random(0, maxRise);
        _pool[index].y       = (int)_pool[index].baseY;

        // Moving platforms unlock from tier 2 onward.
        _pool[index].isMoving = (_tier >= 2) && (random(0, 4) == 0);
        _pool[index].bobPhase = random(0, 628) / 100.0f; // 0..2pi

        // Fire pits unlock from tier 4 onward — purely a re-skin of an
        // ordinary gap (same fall-through mechanic) restricted to ground-level
        // stretches, so no new collision logic is needed, just a visual cue
        // that this particular gap is one to respect.
        if (_tier >= ArcadeConfig::RUNNER_FIREPIT_TIER &&
            _pool[index].baseY == groundLevel() &&
            random(0, 5) == 0) {
            _pool[index].firePitBefore   = true;
            _pool[index].firePitGapWidth = _pool[index].x - startX;
        }
    }

public:
    PlatformManager() : _scrollSpeed(ArcadeConfig::RUNNER_BASE_SCROLL_SPEED),
                         _tier(0), _distance(0), _introPlatformsLeft(0) {
        for (int i = 0; i < POOL_SIZE; i++) _pool[i].active = false;
    }

    void initGame() {
        _scrollSpeed        = ArcadeConfig::RUNNER_BASE_SCROLL_SPEED;
        _tier               = 0;
        _distance           = 0;
        _introPlatformsLeft = ArcadeConfig::PLATFORM_INTRO_COUNT;

        // First platform is always a safe, wide starting ledge under the player.
        _pool[0].x        = 0;
        _pool[0].width    = 70;
        _pool[0].baseY    = groundLevel();
        _pool[0].y        = groundLevel();
        _pool[0].active   = true;
        _pool[0].isMoving = false;
        _pool[0].firePitBefore = false;

        float cursor = (float)_pool[0].width;
        for (int i = 1; i < POOL_SIZE; i++) {
            spawnPlatform(i, cursor);
            cursor = _pool[i].x + _pool[i].width;
        }
    }

    // Advances difficulty tier based on distance travelled. Tiers (and the
    // speed ramp that comes with them) only begin counting once the intro
    // run has been fully placed, so the opening stretch stays at base speed.
    void advanceDifficulty() {
        _distance++;
        if (_introPlatformsLeft > 0) return;

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
        // First pass: scroll everything and find the true rightmost edge
        // across the whole pool. Recycling must never use an edge computed
        // from only part of the pool — doing so let a recycled platform
        // spawn using a stale (too-small) edge and land mid-screen on top
        // of a platform that hadn't been scanned yet, which both looked
        // like an extra block appearing out of nowhere and could silently
        // paper over what should have been a real gap.
        float rightmostEdge = 0;
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;
            _pool[i].x -= _scrollSpeed;

            if (_pool[i].isMoving) {
                _pool[i].bobPhase += 0.04f;
                _pool[i].y = (int)(_pool[i].baseY + sinf(_pool[i].bobPhase) * ArcadeConfig::PLATFORM_BOB_AMPLITUDE);
            }

            float edge = _pool[i].x + _pool[i].width;
            if (edge > rightmostEdge) rightmostEdge = edge;
        }

        // Second pass: recycle anything that has scrolled fully off-screen,
        // always building off the confirmed global rightmost edge.
        for (int i = 0; i < POOL_SIZE; i++) {
            if (_pool[i].active && _pool[i].x + _pool[i].width >= 0) continue;
            spawnPlatform(i, rightmostEdge);
            rightmostEdge = _pool[i].x + _pool[i].width;
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
            // not clipping through from below). Generous tolerance so a
            // bobbing platform doesn't dip the player through its own top.
            if (playerBottom <= _pool[i].y + 10) {
                if (best == -1 || _pool[i].y < best) best = _pool[i].y;
            }
        }
        return best;
    }

    bool isOverPit(float playerX, float playerRight) const {
        return groundYAt(playerX, playerRight, 0, 0) == -1;
    }

    // Looks ahead at the already-generated pool (not just what's visible) for
    // a fire pit about to scroll on-screen, so a hazard-aware power-up can be
    // placed just before it instead of spawning at a purely random moment.
    // Returns the pit's left edge X (in current scroll-space) via outX.
    bool upcomingFirePitX(float &outX) const {
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active || !_pool[i].firePitBefore) continue;
            float pitStart = _pool[i].x - _pool[i].firePitGapWidth;
            if (pitStart > ArcadeConfig::LANDSCAPE_WIDTH &&
                pitStart < ArcadeConfig::LANDSCAPE_WIDTH + 70) {
                outX = pitStart;
                return true;
            }
        }
        return false;
    }

    float getScrollSpeed() const { return _scrollSpeed; }
    int   getTier() const { return _tier; }
    unsigned long getDistance() const { return _distance; }

    void render(GFXcanvas16 &canvas) {
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;
            uint16_t color = _pool[i].isMoving ? ArcadeConfig::COLOR_CYAN
                                                : ArcadeConfig::COLOR_GREEN;
            // Fixed-thickness slab, not a pillar down to the screen bottom —
            // keeps moving platforms a constant visual size as they bob,
            // instead of appearing to grow/shrink and swallow the player.
            canvas.fillRect((int)_pool[i].x, _pool[i].y,
                            _pool[i].width, ArcadeConfig::PLATFORM_THICKNESS,
                            color);
            drawBrickPattern(canvas, (int)_pool[i].x, _pool[i].y, _pool[i].width, color);

            if (_pool[i].firePitBefore) {
                int fireX = (int)(_pool[i].x - _pool[i].firePitGapWidth);
                int fireW = (int)_pool[i].firePitGapWidth;
                int fireY = groundLevel();
                bool flicker = (millis() / 100) % 2 == 0;
                uint16_t fireColor = flicker ? ArcadeConfig::COLOR_ORANGE : ArcadeConfig::COLOR_RED;
                canvas.fillRect(fireX, fireY + ArcadeConfig::PLATFORM_THICKNESS - 3,
                                fireW, 3, fireColor);
                for (int fx = fireX; fx < fireX + fireW; fx += 3) {
                    canvas.drawPixel(fx + (flicker ? 1 : 0), fireY - 1, ArcadeConfig::COLOR_YELLOW);
                }
            }
        }
    }
};

#endif // PLATFORM_MANAGER_H
