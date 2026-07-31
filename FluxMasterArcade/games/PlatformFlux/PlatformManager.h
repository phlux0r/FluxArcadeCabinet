#ifndef PLATFORM_MANAGER_H
#define PLATFORM_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "PlayerRunner.h"

// =============================================================================
// PLATFORM MANAGER
// Pool of scrolling ground/platform segments, modeled on AsteroidManager's
// pool-and-recycle pattern. Segments spawn off the right edge and recycle
// once they scroll past the left edge.
//
// Alternates terrain MODE by tier rather than purely stacking hazards (see
// ArcadeConfig's tier constants for the authoritative list):
//   tier 0/1 - solid ground (contiguous, stepped "stairs" elevation),
//              fire pits unlock from tier 0 onward
//   tier 2-4 - floating platforms with gaps; moving platforms at tier 3;
//              the flying enemy makes an early appearance at tier 4
//   tier 5+  - solid ground again, now with spike traps (tier 5) and
//              rolling boulders (handled by a separate manager, tier 6);
//              the flying enemy is off here and returns for good at tier 7
//
// The run opens with a stretch of flat, contiguous ground (no gaps, no
// height change, no hazards) so the player has time to get used to the
// controls before anything is asked of them.
// =============================================================================
class PlatformManager {
private:
    enum SpikePhase { SPIKE_SAFE, SPIKE_WARN, SPIKE_DANGER };

    struct Platform {
        float x;
        int   y;       // top surface Y
        int   width;
        bool  active;
        bool  isMoving;
        bool  isGroundSegment; // solid-to-floor stone fill vs. thin floating slab
        float baseY;
        float bobPhase;
        bool  firePitBefore;   // fire pit rendered in the gap just before this platform
        float firePitGapWidth; // gap width — region [x - firePitGapWidth, x) scrolls with x
        bool       hasSpike;
        float      spikeOffsetX; // offset from x, scrolls with the segment
        SpikePhase spikePhase;
        unsigned long spikePhaseEnd;
    };

    static const int POOL_SIZE = 6;
    Platform _pool[POOL_SIZE];
    float    _scrollSpeed;
    int      _tier;
    unsigned long _distance;
    int      _introPlatformsLeft;
    int      _lastGroundY;   // running elevation for stepped ground generation

    int groundLevel() const {
        return ArcadeConfig::LANDSCAPE_HEIGHT - 8;
    }

    // tier 0/1 and tier 5+ are solid-ground terrain; tier 2-4 are floating
    // platforms with gaps. See class comment for the full progression.
    bool isGroundTier(int tier) const {
        return tier <= 1 || tier >= ArcadeConfig::RUNNER_GROUND2_TIER_START;
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

    // Two courses of offset bricks across the slab's top band — a repeating
    // pattern drawn over the existing fill color, not a different texture,
    // so static/moving/ground colors stay whatever the caller filled with.
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

    // Solid-ground segment: contiguous with the previous one (no gap) unless
    // this segment is chosen to carry a fire pit, in which case a real,
    // anti-bridge-safe gap is inserted before it (same fall-through death
    // as a platform-mode gap, just a ground-mode re-skin). Elevation steps
    // by a small amount each segment — always within the player's ground
    // tolerance, so stairs are walkable without ever requiring a jump.
    void spawnGroundSegment(int index, float startX) {
        int width = random(35, 60);

        int stepDir = random(0, 3) - 1;      // -1, 0, or 1
        int stepAmt = random(4, 9);          // stays under groundYAt's +10 snap tolerance
        int newY = _lastGroundY + stepDir * stepAmt;
        int minY = groundLevel() - 50;
        int maxY = groundLevel();
        if (newY < minY) newY = minY;
        if (newY > maxY) newY = maxY;

        // Eligible from tier 0 (right after the flat intro) through tier 1 —
        // earlier than before, and only ever rolled when the approach is at
        // (or very near) baseline ground height. Restricting to a flat
        // approach removes any ambiguity between "how high is the ground
        // I'm standing on" and "where exactly is the pit," which could read
        // as surviving a pit that was actually still comfortably jumpable
        // from an elevated stair step.
        bool wantFirePit = (_tier <= 1) &&
                           (_lastGroundY >= groundLevel() - 6) &&
                           (random(0, 4) == 0);
        bool wantSpike   = !wantFirePit &&
                           (_tier >= ArcadeConfig::RUNNER_SPIKE_TIER) &&
                           (random(0, 4) == 0);

        if (wantFirePit) {
            // Same jump-range-derived gap sizing as platform-mode gaps.
            float airtimeFrames = 2.0f * ArcadeConfig::RUNNER_JUMP_VELOCITY / ArcadeConfig::RUNNER_GRAVITY;
            int   safeReach      = (int)(_scrollSpeed * airtimeFrames * 0.85f);
            int   minGap = ArcadeConfig::PLATFORM_MIN_GAP;
            int   maxGap = minGap + max(3, safeReach - minGap);
            float gapWidth = (float)random(minGap, maxGap);

            _pool[index].x               = startX + gapWidth;
            _pool[index].firePitBefore   = true;
            _pool[index].firePitGapWidth = gapWidth;
            // Land back at baseline after a pit so its shape reads cleanly,
            // rather than mixing a stair step into the same spot.
            newY = groundLevel();
        } else {
            _pool[index].x             = startX; // contiguous — no gap
            _pool[index].firePitBefore = false;
        }
        _lastGroundY = newY;

        _pool[index].width          = width;
        _pool[index].active         = true;
        _pool[index].isMoving       = false;
        _pool[index].isGroundSegment = true;
        _pool[index].baseY          = (float)newY;
        _pool[index].y              = newY;
        _pool[index].bobPhase       = 0.0f;

        _pool[index].hasSpike = wantSpike;
        if (wantSpike) {
            _pool[index].spikeOffsetX  = width * 0.5f;
            _pool[index].spikePhase    = SPIKE_SAFE;
            // Stagger start phase so traps aren't all synchronized.
            _pool[index].spikePhaseEnd = millis() + random(0, ArcadeConfig::SPIKE_SAFE_MS);
        }
    }

    void spawnPlatform(int index, float startX) {
        _pool[index].firePitBefore = false;
        _pool[index].hasSpike      = false;

        if (_introPlatformsLeft > 0) {
            // Flat, contiguous run — no gap, no height change, no hazards.
            _introPlatformsLeft--;
            _pool[index].x        = startX;
            _pool[index].width    = random(40, 65);
            _pool[index].baseY    = groundLevel();
            _pool[index].y        = groundLevel();
            _pool[index].active   = true;
            _pool[index].isMoving = false;
            _pool[index].isGroundSegment = true;
            _lastGroundY = groundLevel();
            return;
        }

        if (isGroundTier(_tier)) {
            spawnGroundSegment(index, startX);
            return;
        }

        // ---- Floating-platform mode (tier 2/3) ----
        // Moving platforms unlock at RUNNER_MOVING_TIER. Decided before the
        // gap so the gap leading into a mover can be given extra safety
        // margin — its landing surface bobs, so the timing window is
        // tighter than a static platform at the same distance.
        bool landingIsMoving = (_tier >= ArcadeConfig::RUNNER_MOVING_TIER) && (random(0, 4) == 0);

        // Gap is derived from actual jump range, not a flat/tier-scaled
        // constant — a fixed max that grows with tier can end up wider than
        // the player can physically clear once scroll speed (and therefore
        // effective horizontal reach per jump) is factored in. Airtime is
        // the full up-and-back-down hang time at the takeoff height;
        // horizontal reach is airtime * scroll speed (platforms close the
        // gap while the player is airborne, not the other way around).
        float airtimeFrames = 2.0f * ArcadeConfig::RUNNER_JUMP_VELOCITY / ArcadeConfig::RUNNER_GRAVITY;
        float safetyFactor  = landingIsMoving ? 0.65f : 0.85f;
        int   safeReach      = (int)(_scrollSpeed * airtimeFrames * safetyFactor);

        int minGap = ArcadeConfig::PLATFORM_MIN_GAP;
        // At least a few px of variety even when reach barely clears the
        // sprite-width floor (e.g. right at base scroll speed).
        int maxGap = minGap + max(3, safeReach - minGap);
        int width  = random(24, 45) - _tier;
        if (width < 16) width = 16;

        _pool[index].x       = startX + random(minGap, maxGap);
        _pool[index].width   = width;
        _pool[index].active  = true;
        _pool[index].isGroundSegment = false;

        // Height varies more as tiers progress; stays reachable by jump.
        int maxRise = min(30, 10 + _tier * 3);
        _pool[index].baseY   = groundLevel() - random(0, maxRise);
        _pool[index].y       = (int)_pool[index].baseY;

        _pool[index].isMoving = landingIsMoving;
        _pool[index].bobPhase = random(0, 628) / 100.0f; // 0..2pi
    }

public:
    PlatformManager() : _scrollSpeed(ArcadeConfig::RUNNER_BASE_SCROLL_SPEED),
                         _tier(0), _distance(0), _introPlatformsLeft(0),
                         _lastGroundY(0) {
        for (int i = 0; i < POOL_SIZE; i++) _pool[i].active = false;
    }

    void initGame() {
        _scrollSpeed        = ArcadeConfig::RUNNER_BASE_SCROLL_SPEED;
        _tier               = 0;
        _distance           = 0;
        _introPlatformsLeft = ArcadeConfig::PLATFORM_INTRO_COUNT;
        _lastGroundY        = groundLevel();

        // First platform is always a safe, wide starting ledge under the player.
        _pool[0].x        = 0;
        _pool[0].width    = 70;
        _pool[0].baseY    = groundLevel();
        _pool[0].y        = groundLevel();
        _pool[0].active   = true;
        _pool[0].isMoving = false;
        _pool[0].isGroundSegment = true;
        _pool[0].firePitBefore = false;
        _pool[0].hasSpike = false;

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
        // First pass: scroll everything, advance bob/spike state, and find
        // the true rightmost edge across the whole pool. Recycling must
        // never use an edge computed from only part of the pool — doing so
        // let a recycled platform spawn using a stale (too-small) edge and
        // land mid-screen on top of a platform that hadn't been scanned
        // yet, which both looked like an extra block appearing out of
        // nowhere and could silently paper over what should have been a
        // real gap.
        float rightmostEdge = 0;
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;
            _pool[i].x -= _scrollSpeed;

            if (_pool[i].isMoving) {
                _pool[i].bobPhase += 0.04f;
                _pool[i].y = (int)(_pool[i].baseY + sinf(_pool[i].bobPhase) * ArcadeConfig::PLATFORM_BOB_AMPLITUDE);
            }

            if (_pool[i].hasSpike && millis() >= _pool[i].spikePhaseEnd) {
                switch (_pool[i].spikePhase) {
                    case SPIKE_SAFE:
                        _pool[i].spikePhase    = SPIKE_WARN;
                        _pool[i].spikePhaseEnd = millis() + ArcadeConfig::SPIKE_WARN_MS;
                        break;
                    case SPIKE_WARN:
                        _pool[i].spikePhase    = SPIKE_DANGER;
                        _pool[i].spikePhaseEnd = millis() + ArcadeConfig::SPIKE_DANGER_MS;
                        break;
                    case SPIKE_DANGER:
                        _pool[i].spikePhase    = SPIKE_SAFE;
                        _pool[i].spikePhaseEnd = millis() + ArcadeConfig::SPIKE_SAFE_MS;
                        break;
                }
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
            // bobbing platform — or a stair step up — doesn't dip the
            // player through its own top.
            if (playerBottom <= _pool[i].y + 10) {
                if (best == -1 || _pool[i].y < best) best = _pool[i].y;
            }
        }
        return best;
    }

    bool isOverPit(float playerX, float playerRight) const {
        return groundYAt(playerX, playerRight, 0, 0) == -1;
    }

    // True if the player's footprint overlaps a spike currently in its
    // erupted (dangerous) phase and their feet are low enough to touch it
    // (jumping clears it — see the height check).
    bool spikeHitsPlayer(float playerX, float playerRight, float playerBottom) const {
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active || !_pool[i].hasSpike || _pool[i].spikePhase != SPIKE_DANGER) continue;
            float sx = _pool[i].x + _pool[i].spikeOffsetX;
            if (playerRight <= sx - 6 || playerX >= sx + 6) continue;
            if (playerBottom >= _pool[i].y - 8) return true;
        }
        return false;
    }

    // Topmost (smallest-Y) platform surface whose X range overlaps
    // [xMin, xMax], or groundLevel() if nothing overlaps there (an empty
    // gap, where ground-level clearance is the safe assumption). Used to
    // keep power-ups from spawning inside a platform's slab — spawn X is
    // only known at roll time, so callers query this with a small window
    // around their chosen X rather than relying on a fixed ground Y.
    int surfaceYNear(float xMin, float xMax) const {
        int best = groundLevel();
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;
            if (xMax <= _pool[i].x || xMin >= _pool[i].x + _pool[i].width) continue;
            if (_pool[i].y < best) best = _pool[i].y;
        }
        return best;
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

            uint16_t color;
            int fillHeight;
            if (_pool[i].isGroundSegment) {
                color      = ArcadeConfig::COLOR_GREY;   // stone/earth, distinct from floating platforms
                fillHeight = ArcadeConfig::LANDSCAPE_HEIGHT - _pool[i].y;
            } else {
                color      = _pool[i].isMoving ? ArcadeConfig::COLOR_CYAN : ArcadeConfig::COLOR_GREEN;
                // Fixed-thickness slab, not a pillar down to the screen bottom —
                // keeps moving platforms a constant visual size as they bob,
                // instead of appearing to grow/shrink and swallow the player.
                fillHeight = ArcadeConfig::PLATFORM_THICKNESS;
            }

            canvas.fillRect((int)_pool[i].x, _pool[i].y, _pool[i].width, fillHeight, color);
            drawBrickPattern(canvas, (int)_pool[i].x, _pool[i].y, _pool[i].width, color);

            if (_pool[i].firePitBefore) {
                int fireX = (int)(_pool[i].x - _pool[i].firePitGapWidth);
                int fireW = (int)_pool[i].firePitGapWidth;
                int fireY = _pool[i].y; // == groundLevel() by construction (see spawnGroundSegment)
                bool flicker = (millis() / 100) % 2 == 0;
                uint16_t fireColor = flicker ? ArcadeConfig::COLOR_ORANGE : ArcadeConfig::COLOR_RED;
                canvas.fillRect(fireX, fireY + ArcadeConfig::PLATFORM_THICKNESS - 3,
                                fireW, 3, fireColor);
                for (int fx = fireX; fx < fireX + fireW; fx += 3) {
                    canvas.drawPixel(fx + (flicker ? 1 : 0), fireY - 1, ArcadeConfig::COLOR_YELLOW);
                }
            }

            if (_pool[i].hasSpike) {
                int sx = (int)(_pool[i].x + _pool[i].spikeOffsetX);
                int baseY = _pool[i].y;
                if (_pool[i].spikePhase == SPIKE_WARN) {
                    // Telegraph: a thin rising nub, not yet dangerous.
                    canvas.drawFastVLine(sx, baseY - 3, 3, ArcadeConfig::COLOR_YELLOW);
                    canvas.drawFastVLine(sx + 5, baseY - 2, 2, ArcadeConfig::COLOR_YELLOW);
                } else if (_pool[i].spikePhase == SPIKE_DANGER) {
                    canvas.fillTriangle(sx - 6, baseY, sx + 2, baseY, sx - 2, baseY - 11, ArcadeConfig::COLOR_WHITE);
                    canvas.fillTriangle(sx - 1, baseY, sx + 7, baseY, sx + 3, baseY - 11, ArcadeConfig::COLOR_WHITE);
                }
            }
        }
    }
};

#endif // PLATFORM_MANAGER_H
