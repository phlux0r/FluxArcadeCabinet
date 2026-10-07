#ifndef PLATFORM_MANAGER_H
#define PLATFORM_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "PlayerRunner.h"
#include "RuinsLayer.h"

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
//   tier 5-7 - solid ground again, now with spike traps (tier 5) and
//              rolling boulders (handled by a separate manager, tier 6);
//              the flying enemy is off here and returns at tier 7
//
// After tier 7 finishes, the whole tier 1-7 cycle repeats (see
// advanceDifficulty/cycleLength) rather than sitting at tier 7 forever —
// each new loop gets a fresh fire-pit guarantee, a slightly higher scroll
// speed floor/ceiling, and a rotated ground/spike/ship/boulder color
// palette (see getLoop, groundColor, spikeDangerColor) so a repeat trip
// reads as a new stage rather than an identical rerun.
//
// The run opens with a stretch of flat, contiguous ground (no gaps, no
// height change, no hazards) so the player has time to get used to the
// controls before anything is asked of them.
// =============================================================================
class PlatformManager {
private:
    enum SpikePhase { SPIKE_SAFE, SPIKE_WARN, SPIKE_DANGER };
    // Night's obstacles (every second loop): see ArcadeConfig's NIGHT_ block.
    enum NightKind { NIGHT_NONE, NIGHT_BEAM, NIGHT_JET, NIGHT_LAUNCHER };

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
        bool       pitScored, spikeScored;  // cleared by the runner: points given
        bool       spikeLive;    // rising or up while the runner was over it
        float      spikeOffsetX; // offset from x, scrolls with the segment
        SpikePhase spikePhase;
        unsigned long spikePhaseEnd;
        // Night's obstacle on this segment, if any: a beam or a flame jet
        // (a column, nightX its left edge from x), or a dart launcher (a
        // post at nightX).
        NightKind  night;
        float      nightX;
        SpikePhase jetPhase;      // the jet's flame, timed like a spike: off, sputtering, lit
        unsigned long jetPhaseEnd;
        bool       jetLive;       // lit while the runner was under it
        unsigned long glintAt;    // the launcher: when it began to glint (0: not yet)
        bool       fired, nightScored;
        // Ruins: a slab that crumbles RUINS_CRUMBLE_FRAMES after the runner
        // lands on it (crumbleAt: the frame it did, -1 not yet), then falls.
        bool       crumbly, falling;
        long       crumbleAt;
        float      fallVy;
    };

    RuinsLayer _ruins;
    int   _ruinsEvents = 0;       // RuinsLayer's sounds due, till the game takes them
    long  _frames = 0;            // update() calls: the crumbling slabs' clock
    float _runnerRight = (float)(ArcadeConfig::RUNNER_BASE_X + RUNNER_WIDTH);   // for pillars cracking
    bool  _prevCrumbly = false;   // the slab before the next gap crumbles: a shorter gap
    int   _springStage = 0;       // the Ruins pit stage that's had its spring
    float _sincePillar = 9999.0f; // px built since the last pillar
    int   _slabsSinceCrumbly = 0;  // still slabs built since the last crumbling one

    // Darts in flight: screen space, flying left DART_SPEED faster than
    // the scroll, at a fixed height.
    struct Dart { float x; int y; bool active, scored; };
    static const int DARTS = 3;
    Dart _darts[DARTS];
    bool _jetLitNear = false;   // a jet near the runner lit this update (for its sound)

    static const int POOL_SIZE = 6;
    Platform _pool[POOL_SIZE];
    float    _scrollSpeed;
    int      _tier;
    unsigned long _distance;
    int      _introPlatformsLeft;
    int      _lastGroundY;   // running elevation for stepped ground generation
    int      _pitStage;          // the stage _pitsDone counts for
    int      _pitsDone;          // fire pits placed in it so far
    int      _beamsDone;         // Night: the gaps round the pits filled with a beam so far
    unsigned long _lastPitAt;    // distance at which the runner reaches the last pit
    static const unsigned long PIT_SPACING = 75;      // frames, at least, from one pit to the next
    static const unsigned long PIT_END_MARGIN = 6;    // and from a pit's far side to its stage's end
    static const unsigned long NIGHT_END_MARGIN = 40; // and a Night obstacle, so it's met in its own stage
    float    _distanceSinceLastSpike; // px generated since the last spike, enforces min spacing
    int      _loop;              // how many full tier 1-7 cycles have completed
    unsigned long _cycleDistance; // _distance wrapped to the current loop
    float    _scrollSpeedCap;    // per-loop scroll speed ceiling, rises each loop
    bool     _afterSlabs;        // floating platforms last: the next ground block starts level
    float    _sinceNight;        // px generated since the last Night obstacle
    bool     _prevNight, _prevPit;   // the block before this one carried one / a pit before it
    int      _prevNightRoom;     // px left on that block after its column
    int      _flatRunPx;         // px of level ground (no step, no pit) up to the last block

    // Ground/spike palettes rotate each loop so a repeat trip through the
    // tiers reads as visually distinct, not just "the same run again."
    // Index 0 matches the original colors.
    // Night and the Ruins have their own: slate, sandstone. The Outpost's
    // loops (every third) rotate through the rest.
    uint16_t groundColor() const {
        static const uint16_t palette[3] = {
            ArcadeConfig::COLOR_GREY, ArcadeConfig::COLOR_AMBER, ArcadeConfig::COLOR_MAGENTA
        };
        return isNight(_loop) ? NIGHT_STONE : isRuins(_loop) ? SANDSTONE : palette[(_loop / 3) % 3];
    }
    uint16_t spikeDangerColor() const {
        static const uint16_t palette[3] = {
            ArcadeConfig::COLOR_WHITE, ArcadeConfig::COLOR_YELLOW, ArcadeConfig::COLOR_ORANGE
        };
        return isNight(_loop) ? ArcadeConfig::COLOR_CYAN : isRuins(_loop) ? ArcadeConfig::COLOR_WHITE : palette[(_loop / 3) % 3];
    }

    // A colour at a lantern's reach: full up to litTo, half for 30px past
    // it, a quarter beyond (Night only; litTo is off the screen otherwise).
    static uint16_t lit(uint16_t c, float x, int litTo) {
        if (x <= litTo) return c;
        return x <= litTo + 30 ? darken(c) : darken(darken(c));
    }

    int groundLevel() const {
        return ArcadeConfig::LANDSCAPE_HEIGHT - 8;
    }

    // tier 0/1 and tier 5+ are solid-ground terrain; tier 2-4 are floating
    // platforms with gaps. See class comment for the full progression.
    bool isGroundTier(int tier) const {
        return tier <= 1 || tier >= ArcadeConfig::RUNNER_GROUND2_TIER_START;
    }

    // Tier boundaries, in cycle-relative distance frames. Tier 4 (the early
    // ship preview) runs twice as long as the others — it was only getting
    // ~2 ships' worth of screen time before handing off to tier 5.
    // Where tiers 1-8 start; tier 8, the boss stretch, runs
    // RUNNER_BOSS_DISTANCE to the loop's end.
    void computeThresholds(unsigned long thresholds[8]) const {
        unsigned long base = ArcadeConfig::RUNNER_TIER_DISTANCE;
        thresholds[0] = base * 1; // tier 1
        thresholds[1] = base * 2; // tier 2
        thresholds[2] = base * 3; // tier 3
        thresholds[3] = base * 4; // tier 4
        thresholds[4] = thresholds[3] + base * 2; // tier 5 (tier 4 doubled)
        thresholds[5] = thresholds[4] + base;     // tier 6
        thresholds[6] = thresholds[5] + base;     // tier 7
        thresholds[7] = thresholds[6] + base;     // tier 8: the boss
    }

    int tierForDistance(unsigned long distance) const {
        unsigned long thresholds[8];
        computeThresholds(thresholds);
        int tier = 0;
        for (int i = 0; i < 8; i++) {
            if (distance >= thresholds[i]) tier = i + 1;
            else break;
        }
        return tier;
    }

    // Total length of one full tier 1-7 cycle, plus tier 7's own run —
    // after this many frames the run wraps back to tier 1 (see
    // advanceDifficulty), rather than sitting at tier 7 forever.
    unsigned long cycleLength() const {
        unsigned long thresholds[8];
        computeThresholds(thresholds);
        return thresholds[7] + ArcadeConfig::RUNNER_BOSS_DISTANCE;
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
    void drawBrickPattern(GFXcanvas16 &canvas, int x, int y, int width, uint16_t baseColor, int originX) {
        uint16_t mortar = darken(baseColor);
        static const int BRICK_W = 10;
        static const int BRICK_H = ArcadeConfig::PLATFORM_THICKNESS / 2;

        int midY = y + BRICK_H;
        canvas.drawFastHLine(x, midY, width, mortar);

        for (int row = 0; row < 2; row++) {
            int rowY = y + row * BRICK_H;
            int offset = (row % 2 == 0) ? 0 : BRICK_W / 2;
            for (int bx = originX - offset; bx < x + width; bx += BRICK_W) {
                if (bx <= x || bx <= originX) continue;
                canvas.drawFastVLine(bx, rowY, BRICK_H, mortar);
            }
        }
    }

    // The distance (advanceDifficulty's frames) at which the runner will
    // reach screen x `x`: at today's speed to the end of this stage, then
    // the next stage's starting speed (the ground's built less than a
    // stage ahead, so one boundary at most).
    unsigned long arrivalAt(float x) const {
        const float dx = max(0.0f, x - (float)ArcadeConfig::RUNNER_BASE_X);
        const int stage = stageNumber();
        const unsigned long end = stageStartDistance(stage + 1);
        const float toEnd = (float)(end > _distance ? end - _distance : 0);
        if (dx <= toEnd * _scrollSpeed) return _distance + (unsigned long)(dx / _scrollSpeed);
        return end + (unsigned long)((dx - toEnd * _scrollSpeed) / speedAtStageStart(stage + 1));
    }

    // Frames a jump keeps the runner's feet clear of a pit's flames (more
    // than firePitHitsPlayer's 4px up), on the runner's own physics.
    static int jumpFramesClear() {
        float y = 0.0f, vy = -ArcadeConfig::RUNNER_JUMP_VELOCITY;
        int n = 0;
        while (true) {
            vy += ArcadeConfig::RUNNER_GRAVITY;
            y  += vy;
            if (y > -4.0f) return n;
            ++n;
        }
    }

    // A new fire pit's width at `speed`, on loop `loop` (see ArcadeConfig's
    // RUNNER_PIT_* block): what a plain jump clears with a comfortable
    // window on the first loop; wider each loop after, up to what a jump
    // pushed forward clears (the stick adds RUNNER_X_MOVE_SPEED a frame
    // in the air, from as far back as the runner goes).
    void pitGapRange(float speed, int loop, float &lo, float &hi) const {
        const int air = jumpFramesClear();
        const float minGap = (float)ArcadeConfig::RUNNER_PIT_MIN_GAP;
        hi = speed * (float)(air - ArcadeConfig::RUNNER_PIT_EASY_WINDOW);
        lo = minGap;
        if (loop > 0) {
            const int pushFrames = air - ArcadeConfig::RUNNER_PIT_HARD_WINDOW;
            const float push = min((float)pushFrames * ArcadeConfig::RUNNER_X_MOVE_SPEED,
                                   (float)(ArcadeConfig::RUNNER_X_MAX_OFFSET - ArcadeConfig::RUNNER_X_MIN_OFFSET));
            const float pushed = speed * (float)pushFrames + push;
            const float widen = (float)(ArcadeConfig::RUNNER_PIT_WIDEN_PX * loop);
            hi = min(hi + widen, pushed);
            lo = min(minGap + widen, hi);
        }
        if (hi < minGap) hi = minGap;
    }
    float pitGapWidth(float speed, int loop) const {
        float lo, hi;
        pitGapRange(speed, loop, lo, hi);
        return (float)random((long)lo, (long)hi + 1);
    }

    // Solid-ground segment: contiguous with the previous one (no gap) unless
    // this segment is chosen to carry a fire pit, in which case the pit's
    // gap is inserted before it (sized by pitGapWidth; the flames kill
    // under the runner's middle, and a pit wider than the runner can be
    // fallen into as well). Elevation steps by a small amount each
    // segment — always within the player's ground tolerance, so stairs are
    // walkable without ever requiring a jump.
    void spawnGroundSegment(int index, float startX) {
        int width = random(35, 60);

        int stepDir = random(0, 3) - 1;      // -1, 0, or 1
        int stepAmt = random(4, 9);          // stays under groundYAt's +10 snap tolerance
        const int prevY = _lastGroundY;
        int newY = _lastGroundY + stepDir * stepAmt;
        int minY = groundLevel() - 50;
        int maxY = groundLevel();
        if (newY < minY) newY = minY;
        if (newY > maxY) newY = maxY;

        // Fire pits go by the stage the runner will be in when it gets
        // here (the ground's built a screen or so ahead): a loop's first
        // stage has RUNNER_PITS_FIRST, its second RUNNER_PITS_SECOND. The
        // k-th of n (from 0) is due 2k+1 (2n+1)ths of the way through (one
        // in the middle of each equal share, the last share's end left
        // over for a late one), and goes on
        // the first block the runner reaches from then, not too close to
        // the last pit nor the stage's end. Any stair height will do: both
        // sides of a pit are level.
        const unsigned long passAt = arrivalAt(startX);
        const int passStage = stageForDistance(passAt);
        const int passTier = (passStage - 1) % TIERS_PER_LOOP;
        bool wantFirePit = false;
        bool beamTurn = false;   // Night's pit stages: a beam's turn, between the pits
        if (passTier <= 1) {
            if (passStage != _pitStage) { _pitStage = passStage; _pitsDone = 0; _beamsDone = 0; }
            const int n = passTier == 0 ? ArcadeConfig::RUNNER_PITS_FIRST : ArcadeConfig::RUNNER_PITS_SECOND;
            const unsigned long start = stageStartDistance(passStage);
            const unsigned long len = stageStartDistance(passStage + 1) - start;
            const unsigned long due = start + len * (unsigned long)(2 * _pitsDone + 1) / (unsigned long)(2 * n + 1);
            // Its far side reached in the stage too, at its widest.
            const float speed = passStage == stageNumber() ? _scrollSpeed : speedAtStageStart(passStage);
            float gapLo, gapHi;
            pitGapRange(speed, (passStage - 1) / TIERS_PER_LOOP, gapLo, gapHi);
            const unsigned long farAt = passAt + (unsigned long)(gapHi / speed) + PIT_END_MARGIN;
            wantFirePit = _pitsDone < n && passAt >= due && passAt >= _lastPitAt + PIT_SPACING &&
                          farAt <= start + len && (!_prevNight || _prevNightRoom >= 30);
            // Night's beams go in the gaps around the pits, one each: before
            // the first, then halfway between each pit's mark and the next's
            // (2g (2n+1)ths), giving way once the next pit's is due.
            // _beamsDone is the gaps filled (the current gap is _pitsDone).
            const int gap = _pitsDone;
            const unsigned long beamDue = gap == 0 ? start
                                        : start + len * (unsigned long)(2 * gap) / (unsigned long)(2 * n + 1);
            const unsigned long nextPit = start + len * (unsigned long)(2 * gap + 1) / (unsigned long)(2 * n + 1);
            beamTurn = !wantFirePit && _beamsDone <= gap && passAt >= beamDue &&
                       (gap >= n || passAt < nextPit);
        }

        // Night (by the stage it's reached in, like the pits): beams in its
        // first two stages, flame jets in the sixth, dart launchers in the
        // seventh, all three in the eighth; the platform stages have only
        // the dark. A dart stage keeps its ground level, so a dart flies
        // at head height all the way. In the pit stages a beam
        // follows each pit, on its own mark (above); never on a pit's
        // landing block (the one after is fine: there's that whole block to
        // land and duck in), and a pit after one only with 30px or more of
        // its block left past the column (a duck, then a jump); kept apart
        // from each other and from spikes, and clear of the stage's end.
        const int passLoop = (passStage - 1) / TIERS_PER_LOOP;
        const bool night = isNight(passLoop);
        if (night && (passTier == 6 || passTier == 7)) newY = _lastGroundY;

        // Spikes need at least 4 player-sprite-widths of clearance since
        // the last one — otherwise back-to-back rolls on adjacent segments
        // could place two spikes close enough together to be unfair.
        _distanceSinceLastSpike += width;
        bool spikeSpacingOK = _distanceSinceLastSpike >= RUNNER_WIDTH * 4;
        // (Night's dart stage has none: they'd stand in the darts' way.)
        const bool nightDarts = isNight((passStage - 1) / TIERS_PER_LOOP) && passTier == 6;
        // The boss stretch's ground is plain and level at the bottom (the
        // rocks are the test, and a stair left high gave them less room to
        // fall): a step down onto it, never up. (Blocks built during it
        // but reached after stay level too; what's on them goes by where
        // they're reached.)
        const bool boss = passTier == BOSS_TIER;
        if (boss) newY = groundLevel();
        else if (_tier == BOSS_TIER) newY = prevY;
        NightKind kind = NIGHT_NONE;
        // Night's obstacle comes first, a spike only where there's none.
        if (night && !boss && !wantFirePit && (passTier > 1 || beamTurn) &&
            _sinceNight >= (passTier <= 1 ? 40 : ArcadeConfig::NIGHT_SPACING) &&   // pits part the beams; a duck can stay down for two
            _distanceSinceLastSpike >= ArcadeConfig::NIGHT_CLEAR &&
            passAt + NIGHT_END_MARGIN <= stageStartDistance(passStage + 1)) {
            if (passTier <= 1) { kind = NIGHT_BEAM; _beamsDone = _pitsDone + 1; }
            else if (passTier == 5) kind = NIGHT_JET;
            else if (passTier == 6) kind = NIGHT_LAUNCHER;
            else if (passTier == 7) kind = (NightKind)(NIGHT_BEAM + random(0, 3));
            // A launcher needs level ground back to the runner; a column a
            // block wide enough that the runner is wholly on it while
            // under it (a stair step up beside it would lift it into it).
            // (and no spike in a dart's way back to it).
            if (kind == NIGHT_LAUNCHER && (_flatRunPx < 130 || newY != prevY || _distanceSinceLastSpike < 130))
                kind = NIGHT_NONE;
            // Level with the block before, too: off a step down the runner
            // stays on the higher block till it's wholly past it, then
            // falls, and it can't duck in the air.
            if (kind == NIGHT_BEAM || kind == NIGHT_JET) {
                width = max(width, 2 * RUNNER_WIDTH + ArcadeConfig::NIGHT_COLUMN_W + 8);
                if (passTier <= 1) width = max(width, RUNNER_WIDTH + 2 + ArcadeConfig::NIGHT_COLUMN_W + 30);
                newY = prevY;
                if (_afterSlabs) kind = NIGHT_NONE;   // the block steps level after slabs: not here
            }
        }

        // The Ruins' pillars (stages 6-8 of a loop): on a block long enough
        // for the log to lie on, its foot near the block's end; apart from
        // each other and from spikes.
        const bool ruinsGround = isRuins((passStage - 1) / TIERS_PER_LOOP) && !boss &&
                                 passTier >= ArcadeConfig::RUNNER_GROUND2_TIER_START;
        _sincePillar += (float)width;
        bool wantPillar = ruinsGround && !wantFirePit && _sincePillar >= ArcadeConfig::RUINS_PILLAR_SPACING &&
                          _distanceSinceLastSpike >= ArcadeConfig::NIGHT_CLEAR &&
                          (_sincePillar >= 2 * ArcadeConfig::RUINS_PILLAR_SPACING || random(0, 2) == 0);

        bool wantSpike    = !wantFirePit && !nightDarts && !boss && kind == NIGHT_NONE && passTier > 1 && !wantPillar &&
                           (_tier >= ArcadeConfig::RUNNER_SPIKE_TIER) &&
                           spikeSpacingOK && _sinceNight >= ArcadeConfig::NIGHT_CLEAR &&
                           _sincePillar >= ArcadeConfig::NIGHT_CLEAR &&
                           (random(0, 4) == 0);
        if (wantSpike) _distanceSinceLastSpike = 0.0f;
        if (wantPillar) {
            width = max(width, RuinsLayer::PILLAR_LEN + 16);
            _sincePillar = 0.0f;
        }

        // Back down from the floating platforms: level with the ground, so
        // this block never stands taller than a stair step above the last
        // slab (a slab sits at most a bob below ground level).
        if (_afterSlabs) {
            newY = groundLevel();
            _afterSlabs = false;
        }

        if (wantFirePit) {
            ++_pitsDone;
            _lastPitAt = passAt;
            // Sized for the speed and loop of the stage it's in: a pit built
            // at the end of one loop is crossed at the next one's slower start.
            const float speed = passStage == stageNumber() ? _scrollSpeed : speedAtStageStart(passStage);
            float gapWidth = pitGapWidth(speed, (passStage - 1) / TIERS_PER_LOOP);

            _pool[index].x               = startX + gapWidth;
            _pool[index].firePitBefore   = true;
            _pool[index].firePitGapWidth = gapWidth;

            // The Ruins: each pit stage's first pit has a spring just before
            // it, on the block it starts after, throwing the runner onto a
            // high slab across it (jump the spring to stay low).
            if (isRuins((passStage - 1) / TIERS_PER_LOOP) && _springStage != passStage) {
                for (int k = 0; k < POOL_SIZE; k++) {
                    const Platform &q = _pool[k];
                    if (k == index || !q.active || !q.isGroundSegment || fabsf(q.x + q.width - startX) > 0.5f) continue;
                    if (q.width < 30 || q.y != _lastGroundY) break;
                    _ruins.addSpringRoute(startX - 12.0f, q.y, startX + gapWidth + 34.0f, ArcadeConfig::RUINS_ROUTE_RISE);
                    _springStage = passStage;
                    break;
                }
            }
            // Level with the approach, so the pit's no step up or down.
            newY = _lastGroundY;
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

        if (wantPillar) _ruins.addPillar(startX + width - 6.0f, newY);

        _pool[index].night = kind;
        _pool[index].nightScored = _pool[index].fired = _pool[index].jetLive = false;
        _pool[index].glintAt = 0;
        if (kind == NIGHT_BEAM || kind == NIGHT_JET) {
            // In the pit stages at the front of its block, so a pit can come
            // next with 30px or more to stand up and jump in.
            _pool[index].nightX = passTier <= 1 ? (float)(RUNNER_WIDTH + 2)
                                : (float)random(RUNNER_WIDTH + 2, width - RUNNER_WIDTH - 2 - ArcadeConfig::NIGHT_COLUMN_W + 1);
            _pool[index].jetPhase = SPIKE_SAFE;
            _pool[index].jetPhaseEnd = millis() + random(0, ArcadeConfig::NIGHT_JET_OFF_MS);
        } else if (kind == NIGHT_LAUNCHER) {
            _pool[index].nightX = (float)(width - 8);
        }
        _sinceNight = kind != NIGHT_NONE ? 0.0f : _sinceNight + width;
        _prevNight  = kind != NIGHT_NONE;
        _prevNightRoom = (kind == NIGHT_BEAM || kind == NIGHT_JET)
                       ? (int)(width - _pool[index].nightX - ArcadeConfig::NIGHT_COLUMN_W) : 0;
        _prevPit    = wantFirePit;
        _flatRunPx  = (newY == prevY && !wantFirePit) ? _flatRunPx + width : width;

        _pool[index].hasSpike = wantSpike;
        if (wantSpike) {
            _pool[index].spikeOffsetX  = width * 0.5f;
            _pool[index].spikePhase    = SPIKE_SAFE;
            // Stagger start phase so traps aren't all synchronized.
            _pool[index].spikePhaseEnd = millis() + random(0, ArcadeConfig::SPIKE_SAFE_MS);
        }
    }

    void spawnPlatform(int index, float startX) {
        _pool[index].night         = NIGHT_NONE;
        _pool[index].crumbly = _pool[index].falling = false;
        _pool[index].crumbleAt     = -1;
        _pool[index].fallVy        = 0.0f;
        _pool[index].firePitBefore = false;
        _pool[index].hasSpike      = false;
        _pool[index].pitScored     = false;
        _pool[index].spikeScored   = false;
        _pool[index].spikeLive     = false;

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
        // (Off a crumbling slab the jump may come early: a shorter gap.)
        float safetyFactor  = (landingIsMoving || _prevCrumbly) ? 0.65f : 0.85f;
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
        // The Ruins: a third of the still slabs crumble (half from the
        // moving-platform stage on), and those are short.
        const bool crumbly = isRuins(_loop) && !landingIsMoving &&
                             (_slabsSinceCrumbly >= 2 || random(0, _tier >= ArcadeConfig::RUNNER_MOVING_TIER ? 2 : 3) == 0);
        _slabsSinceCrumbly = crumbly ? 0 : _slabsSinceCrumbly + 1;   // never more than two plain in a row
        _pool[index].crumbly = crumbly;
        if (crumbly) _pool[index].width = min(width, (int)ArcadeConfig::RUINS_SLAB_MAX_W);
        _prevCrumbly = crumbly;
        _afterSlabs = true;
        _prevNight = _prevPit = false;
        _flatRunPx = 0;
        _sinceNight += (float)width;
    }

public:
    // The worlds' own colours (the game's how-to screen and explosions use them).
    static const uint16_t SANDSTONE      = 0xC56E;   // rgb(196, 172, 112): the Ruins' stone
    static const uint16_t SANDSTONE_DARK = 0x7B47;   // rgb(120, 104, 56)
    static const uint16_t NIGHT_STONE = 0x3A90;   // rgb(60, 80, 130)
    static const uint16_t STEEL       = 0x4A8C;   // rgb(72, 80, 100): Night's gantries
    static const uint16_t STEEL_LIGHT = 0x9D17;   // rgb(150, 160, 184)

    PlatformManager() : _scrollSpeed(ArcadeConfig::RUNNER_BASE_SCROLL_SPEED),
                         _tier(0), _distance(0), _introPlatformsLeft(0),
                         _lastGroundY(0), _pitStage(0), _pitsDone(0), _beamsDone(0), _lastPitAt(0),
                         _distanceSinceLastSpike(9999.0f), _loop(0),
                         _cycleDistance(0),
                         _scrollSpeedCap(ArcadeConfig::RUNNER_MAX_SCROLL_SPEED),
                         _afterSlabs(false), _sinceNight(9999.0f), _prevNight(false), _prevPit(false), _prevNightRoom(0),
                         _flatRunPx(0) {
        for (int i = 0; i < DARTS; i++) _darts[i].active = false;
        for (int i = 0; i < POOL_SIZE; i++) _pool[i].active = false;
    }

    // Stages: each tier (0-8) of each loop is one, numbered from 1, so a
    // loop is TIERS_PER_LOOP stages, the last of them the boss stretch
    // (RunnerBoss.h). A death restarts the stage it happened in (see
    // PlatformFluxGame).
    static const int TIERS_PER_LOOP = 9;   // eight stages and the boss
    static const int BOSS_TIER = 8;
    static bool isBossStage(int stage) { return (stage - 1) % TIERS_PER_LOOP == BOSS_TIER; }

    int stageNumber() const { return _loop * TIERS_PER_LOOP + _tier + 1; }
    int loopsCompleted() const { return _loop; }

    // Where a stage starts, in the same distance frames advanceDifficulty() counts.
    unsigned long stageStartDistance(int stage) const {
        int s = stage - 1;
        int loop = s / TIERS_PER_LOOP, tier = s % TIERS_PER_LOOP;
        unsigned long t[8];
        computeThresholds(t);
        return (unsigned long)loop * cycleLength() + (tier == 0 ? 0 : t[tier - 1]);
    }

    // The stage the runner is in at `distance` (advanceDifficulty's frames).
    int stageForDistance(unsigned long distance) const {
        const unsigned long cycleLen = cycleLength();
        return (int)(distance / cycleLen) * TIERS_PER_LOOP + tierForDistance(distance % cycleLen) + 1;
    }

    // The speed advanceDifficulty() has reached as `stage` begins: each loop
    // starts 0.25 faster, and every tier change adds a step (from loop 1
    // on, the wrap into tier 0 is one too), up to the loop's ceiling.
    static float speedAtStageStart(int stage) {
        const int loop = (stage - 1) / TIERS_PER_LOOP, tier = (stage - 1) % TIERS_PER_LOOP;
        const float speed = ArcadeConfig::RUNNER_BASE_SCROLL_SPEED + ArcadeConfig::RUNNER_LOOP_SPEED_STEP * loop +
                            ArcadeConfig::RUNNER_SPEED_STEP * (float)(tier + (loop > 0 ? 1 : 0));
        return min(speed, ArcadeConfig::RUNNER_MAX_SCROLL_SPEED + ArcadeConfig::RUNNER_LOOP_SPEED_STEP * loop);
    }

    // 0..1 through the current stage, for the HUD's progress line.
    float stageProgress() const {
        unsigned long t[8];
        computeThresholds(t);
        unsigned long start = _tier == 0 ? 0 : t[_tier - 1];
        unsigned long end   = _tier < BOSS_TIER ? t[_tier] : cycleLength();
        if (_cycleDistance <= start) return 0.0f;
        float p = (float)(_cycleDistance - start) / (float)(end - start);
        return p > 1.0f ? 1.0f : p;
    }

    // A fresh run from `stage` (1 = the very start): its hazards, its speed,
    // a safe starting ledge, and a short flat run-in (the full intro only
    // for stage 1).
    void initGame(int stage = 1) {
        _distance           = stageStartDistance(stage);
        const unsigned long cycleLen = cycleLength();
        _loop               = (int)(_distance / cycleLen);
        _cycleDistance      = _distance % cycleLen;
        _tier               = tierForDistance(_cycleDistance);
        _scrollSpeedCap     = ArcadeConfig::RUNNER_MAX_SCROLL_SPEED + ArcadeConfig::RUNNER_LOOP_SPEED_STEP * _loop;
        _scrollSpeed        = speedAtStageStart(stage);
        _introPlatformsLeft = stage <= 1 ? ArcadeConfig::PLATFORM_INTRO_COUNT : 1;
        _lastGroundY        = groundLevel();
        _pitStage           = 0;
        _pitsDone           = 0;
        _beamsDone          = 0;
        _lastPitAt          = 0;
        _distanceSinceLastSpike = 9999.0f;
        _afterSlabs         = false;
        _sinceNight         = 9999.0f;
        _prevNight = _prevPit = false;
        _prevNightRoom      = 0;
        _flatRunPx          = 0;
        for (int i = 0; i < DARTS; i++) _darts[i].active = false;
        _ruins.reset();
        _frames = 0;
        _prevCrumbly = false;
        _springStage = 0;
        _sincePillar = 9999.0f;
        _slabsSinceCrumbly = 0;
        for (int i = 0; i < POOL_SIZE; i++) { _pool[i].crumbly = _pool[i].falling = false; _pool[i].crumbleAt = -1; }

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
        _pool[0].night = NIGHT_NONE;

        float cursor = (float)_pool[0].width;
        for (int i = 1; i < POOL_SIZE; i++) {
            spawnPlatform(i, cursor);
            cursor = _pool[i].x + _pool[i].width;
        }
    }

    // Advances difficulty tier based on distance travelled. Tiers (and the
    // speed ramp that comes with them) only begin counting once the intro
    // run has been fully placed, so the opening stretch stays at base speed.
    // Once a full tier 1-7 cycle finishes, wraps back to tier 1 instead of
    // sitting at tier 7 forever — each new loop gets its own fire-pit
    // guarantee, a slightly higher scroll-speed ceiling, and a rotated
    // ground/spike color palette (see groundColor/spikeDangerColor) so a
    // repeat trip through the tiers reads as a new stage, not a rerun.
    void advanceDifficulty() {
        _distance++;
        if (_introPlatformsLeft > 0) return;

        unsigned long cycleLen = cycleLength();
        int newLoop = _distance / cycleLen;
        _cycleDistance = _distance % cycleLen;

        if (newLoop != _loop) {
            _loop           = newLoop;
            // Both floor and ceiling shift up together so each new loop
            // still opens gently and ramps up the same way — just a
            // little faster start-to-finish than the loop before it,
            // rather than only getting faster near the end.
            _scrollSpeed    = ArcadeConfig::RUNNER_BASE_SCROLL_SPEED + ArcadeConfig::RUNNER_LOOP_SPEED_STEP * _loop;
            _scrollSpeedCap = ArcadeConfig::RUNNER_MAX_SCROLL_SPEED  + ArcadeConfig::RUNNER_LOOP_SPEED_STEP * _loop;
        }

        int newTier = tierForDistance(_cycleDistance);
        if (newTier != _tier) {
            _tier = newTier;
            _scrollSpeed += ArcadeConfig::RUNNER_SPEED_STEP;
            if (_scrollSpeed > _scrollSpeedCap) {
                _scrollSpeed = _scrollSpeedCap;
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
        ++_frames;
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;
            _pool[i].x -= _scrollSpeed;

            // A crumbling slab, its time up: falls away.
            if (_pool[i].crumbly && _pool[i].crumbleAt >= 0 &&
                _frames >= _pool[i].crumbleAt + ArcadeConfig::RUINS_CRUMBLE_FRAMES) {
                _pool[i].falling = true;
                _pool[i].fallVy += 0.25f;
                _pool[i].baseY  += _pool[i].fallVy;
                _pool[i].y = (int)_pool[i].baseY;
            }

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

            // A flame jet: off, sputtering, lit, off again, like a spike.
            Platform &p = _pool[i];
            if (p.night == NIGHT_JET && millis() >= p.jetPhaseEnd) {
                if (p.jetPhase == SPIKE_SAFE) {
                    p.jetPhase = SPIKE_WARN;   p.jetPhaseEnd = millis() + ArcadeConfig::NIGHT_JET_WARN_MS;
                } else if (p.jetPhase == SPIKE_WARN) {
                    p.jetPhase = SPIKE_DANGER; p.jetPhaseEnd = millis() + ArcadeConfig::NIGHT_JET_LIT_MS;
                    const float jx = p.x + p.nightX;
                    if (jx > 0 && jx < ArcadeConfig::LANDSCAPE_WIDTH) _jetLitNear = true;
                } else {
                    p.jetPhase = SPIKE_SAFE;   p.jetPhaseEnd = millis() + ArcadeConfig::NIGHT_JET_OFF_MS;
                }
            }

            float edge = _pool[i].x + _pool[i].width;
            if (edge > rightmostEdge) rightmostEdge = edge;
        }

        for (int i = 0; i < DARTS; i++) {
            if (!_darts[i].active) continue;
            _darts[i].x -= _scrollSpeed + ArcadeConfig::NIGHT_DART_SPEED;
            if (_darts[i].x < -10) _darts[i].active = false;
        }
        _ruinsEvents |= _ruins.update(_scrollSpeed, _runnerRight);

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
    // `ahead` > 0 asks where moving platforms will have bobbed to that many
    // frames from now (for the attract demo's prediction); positions are
    // the caller's to shift.
    // `touched` (the demo's prediction): for each slab, the frame ahead the
    // predicted runner landed on it, -1 if it hasn't; a crumbling slab is
    // gone RUINS_CRUMBLE_FRAMES after. `idx` gets the platform stood on
    // (-1: none, or a Ruins high slab).
    int groundYAt(float playerX, float playerRight, float playerY, float playerBottom, int ahead = 0,
                  const short* touched = nullptr, int* idx = nullptr) const {
        int best = -1;
        if (idx) *idx = -1;
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active || _pool[i].falling) continue;
            if (playerRight <= _pool[i].x || playerX >= _pool[i].x + _pool[i].width) continue;
            if (_pool[i].crumbly) {
                const long C = ArcadeConfig::RUINS_CRUMBLE_FRAMES;
                if (_pool[i].crumbleAt >= 0 ? _frames + ahead >= _pool[i].crumbleAt + C
                                            : (touched && touched[i] >= 0 && ahead >= touched[i] + C)) continue;
            }
            int y = _pool[i].y;
            if (ahead > 0 && _pool[i].isMoving) {
                y = (int)(_pool[i].baseY + sinf(_pool[i].bobPhase + 0.04f * ahead) * ArcadeConfig::PLATFORM_BOB_AMPLITUDE);
            }

            // Tolerance for "is this close enough to count as ground yet."
            // Contiguous stairs steps (no gap before them) get a generous
            // window so a step-up doesn't dip the player through its own
            // top. Anything on the far side of a real gap — a platform-mode
            // gap, or a fire pit landing — gets a tight one instead: gravity
            // only needs a handful of frames to fall a few px, and when the
            // far side is the SAME height as the takeoff (fire pits always
            // are), a generous tolerance would count that tiny fall as
            // "close enough" and snap the player straight back onto solid
            // ground almost immediately, well before they'd actually fallen
            // far enough to die — silently bridging what should have been a
            // lethal gap regardless of jumping.
            int tolerance = (_pool[i].isGroundSegment && !_pool[i].firePitBefore) ? 8 : 2;

            if (playerBottom <= y + tolerance) {
                if (best == -1 || y < best) { best = y; if (idx) *idx = i; }
            }
        }
        const int slab = _ruins.slabYAt(playerX, playerRight, playerBottom);
        if (slab >= 0 && (best == -1 || slab < best)) { best = slab; if (idx) *idx = -1; }
        return best;
    }
    bool isCrumbly(int i) const { return i >= 0 && i < POOL_SIZE && _pool[i].crumbly && _pool[i].crumbleAt < 0; }

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

    // A spike under the player's feet, whatever its phase: the attract
    // demo's autopilot treats every trap as live rather than timing them.
    bool spikeNear(float playerX, float playerRight, float playerBottom) const {
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active || !_pool[i].hasSpike) continue;
            float sx = _pool[i].x + _pool[i].spikeOffsetX;
            if (playerRight <= sx - 6 || playerX >= sx + 6) continue;
            if (playerBottom >= _pool[i].y - 8) return true;
        }
        return false;
    }

    // Direct "touching the flame" hit test — independent of the fall-through
    // mechanic. Previously a fire pit was only lethal via groundYAt() never
    // finding valid ground under the player's whole footprint; a jump that
    // landed straddling the edge (half on solid ground, half over the pit)
    // could still register as grounded on the solid half and survive. This
    // kills when the runner's middle is over the pit and its feet are down
    // near ground level, however it got there — walking in, or a mistimed
    // jump landing on the edge. A pit shorter than the runner can't be
    // fallen into at all, so for those it's the only test.
    bool firePitHitsPlayer(float playerX, float playerRight, float playerBottom) const {
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active || !_pool[i].firePitBefore) continue;
            // In the flames when the runner's middle is over them: its feet
            // are the sprite's middle ten pixels, so that's most of them.
            // (The whole box, as it was, made even a short pit need the
            // stick pushed forward through the jump.)
            float pitLeft  = _pool[i].x - _pool[i].firePitGapWidth;
            float pitRight = _pool[i].x;
            float mid      = (playerX + playerRight) * 0.5f;
            if (mid < pitLeft || mid >= pitRight) continue;
            if (playerBottom >= _pool[i].y - 4) return true;   // the far side's level, as is the near
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

    // A fire pit or spike trap the runner has got past (its right edge
    // behind playerX) and hasn't been scored yet: marks it, gives where to
    // show the points, and returns them; 0 when there's none. Call until 0,
    // every frame: a spike only scores if it was rising or up at some
    // point while the runner was over it, which this watches for.
    int takeCleared(float playerX, float &popX, float &popY) {
        for (int i = 0; i < POOL_SIZE; i++) {
            Platform &p = _pool[i];
            if (!p.active) continue;
            if (p.firePitBefore && !p.pitScored && p.x < playerX) {
                p.pitScored = true;
                popX = p.x - p.firePitGapWidth * 0.5f;
                popY = (float)(p.y - 10);
                return ArcadeConfig::RUNNER_PIT_POINTS;
            }
            if ((p.night == NIGHT_BEAM || p.night == NIGHT_JET) && !p.nightScored) {
                const float cl = p.x + p.nightX, cr = cl + ArcadeConfig::NIGHT_COLUMN_W;
                if (p.night == NIGHT_JET && p.jetPhase == SPIKE_DANGER &&
                    playerX + RUNNER_WIDTH > cl && playerX < cr) p.jetLive = true;
                if (cr < playerX) {
                    p.nightScored = true;
                    if (p.night == NIGHT_JET && !p.jetLive) continue;   // never lit over it
                    popX = cl + ArcadeConfig::NIGHT_COLUMN_W * 0.5f;
                    popY = (float)(p.y - 30);
                    return p.night == NIGHT_BEAM ? ArcadeConfig::RUNNER_BEAM_POINTS : ArcadeConfig::RUNNER_JET_POINTS;
                }
            }
            if (p.hasSpike && !p.spikeScored) {
                const float sx = p.x + p.spikeOffsetX;
                if (p.spikePhase != SPIKE_SAFE && playerX + RUNNER_WIDTH > sx - 6 && playerX < sx + 6) p.spikeLive = true;
                if (sx + 7.0f < playerX) {
                    p.spikeScored = true;
                    if (!p.spikeLive) continue;   // never fired at the runner: nothing dodged
                    popX = sx;
                    popY = (float)(p.y - 16);
                    return ArcadeConfig::RUNNER_SPIKE_POINTS;
                }
            }
        }
        if (const int r = _ruins.takeCleared(playerX, popX, popY)) return r;
        for (int i = 0; i < DARTS; i++) {
            Dart &d = _darts[i];
            if (!d.active || d.scored || d.x + 6.0f >= playerX) continue;
            d.scored = true;   // gone past: ducked or jumped
            popX = d.x + 3.0f;
            popY = (float)(d.y - 8);
            return ArcadeConfig::RUNNER_DART_POINTS;
        }
        return 0;
    }

    // ---- Night ------------------------------------------------------------

    // The worlds come round in turn, a loop each (docs/design/RunnerFlux.md):
    // the Outpost, Night, the Ruins (Storm will make it four).
    static const int WORLDS = 3;
    static int  worldOf(int loop) { return loop % WORLDS; }
    static bool isNight(int loop) { return worldOf(loop) == 1; }
    static bool isRuins(int loop) { return worldOf(loop) == 2; }
    static const char* worldName(int loop) {
        static const char* const names[WORLDS] = { "OUTPOST", "NIGHT", "RUINS" };
        return names[worldOf(loop)];
    }

    RuinsLayer &ruins() { return _ruins; }
    int takeRuinsEvents() { const int e = _ruinsEvents; _ruinsEvents = 0; return e; }
    const RuinsLayer &ruins() const { return _ruins; }
    void setRunnerRight(float x) { _runnerRight = x; }

    // The runner has landed (feet at `bottom`): a crumbling slab under it
    // starts to go. True the frame one does (for its sound).
    bool standOn(float px, float pr, float bottom) {
        for (int i = 0; i < POOL_SIZE; i++) {
            Platform &p = _pool[i];
            if (!p.active || !p.crumbly || p.falling || p.crumbleAt >= 0) continue;
            if (pr <= p.x || px >= p.x + p.width || fabsf(bottom - (float)p.y) > 1.0f) continue;
            p.crumbleAt = _frames;
            return true;
        }
        return false;
    }

    // Once a frame, with the runner's x: launchers glint as the runner
    // comes within range, then fire. Returns a bit for each sound due:
    // 1 a dart fired, 2 a jet lit on screen.
    int updateNight(float runnerX) {
        int events = _jetLitNear ? 2 : 0;
        _jetLitNear = false;
        for (int i = 0; i < POOL_SIZE; i++) {
            Platform &p = _pool[i];
            if (!p.active || p.night != NIGHT_LAUNCHER || p.fired) continue;
            const float lx = p.x + p.nightX, ahead = lx - runnerX;
            if (!p.glintAt && ahead > ArcadeConfig::NIGHT_LAUNCH_NEAR && ahead < ArcadeConfig::NIGHT_LAUNCH_FAR)
                p.glintAt = millis() | 1;
            if (p.glintAt && millis() - p.glintAt >= ArcadeConfig::NIGHT_GLINT_MS) {
                p.fired = true;
                for (int k = 0; k < DARTS; k++) {
                    if (_darts[k].active) continue;
                    _darts[k] = Dart{ lx - 2.0f, p.y - ArcadeConfig::NIGHT_DART_HEIGHT, true, false };
                    events |= 1;
                    break;
                }
            }
        }
        return events;
    }

    // Does a Night obstacle touch the runner's box (top is lower while it
    // ducks)? `ahead` > 0: where things will be that many frames on, the
    // box being where the runner will be on screen then; jets are taken
    // as lit, as the demo's autopilot treats spikes as up.
    bool nightHits(float px, float pr, float top, float bottom, int ahead = 0) const {
        const float shift = _scrollSpeed * (float)ahead;
        for (int i = 0; i < POOL_SIZE; i++) {
            const Platform &p = _pool[i];
            if (!p.active || (p.night != NIGHT_BEAM && p.night != NIGHT_JET)) continue;
            const float cl = p.x + p.nightX - shift, cr = cl + ArcadeConfig::NIGHT_COLUMN_W;
            if (pr - 3.0f <= cl || px + 3.0f >= cr) continue;
            const bool flame = p.night == NIGHT_BEAM || ahead > 0 || p.jetPhase == SPIKE_DANGER;
            const int under = p.y - (flame ? ArcadeConfig::NIGHT_BEAM_CLEAR : ArcadeConfig::NIGHT_JET_HOUSING);
            if (top < (float)under) return true;
        }
        const float dartShift = (_scrollSpeed + ArcadeConfig::NIGHT_DART_SPEED) * (float)ahead;
        for (int i = 0; i < DARTS; i++) {
            const Dart &d = _darts[i];
            if (!d.active) continue;
            const float dx = d.x - dartShift;
            if (pr - 2.0f > dx && px + 2.0f < dx + 6.0f && bottom > (float)d.y && top < (float)(d.y + 2)) return true;
        }
        return false;
    }

    // Should the autopilot duck, `ahead` frames on? Under a column or just
    // short of one, or with a dart on its way.
    bool duckWanted(float px, float pr, int ahead = 0) const {
        const float shift = _scrollSpeed * (float)ahead;
        for (int i = 0; i < POOL_SIZE; i++) {
            const Platform &p = _pool[i];
            if (!p.active || (p.night != NIGHT_BEAM && p.night != NIGHT_JET)) continue;
            const float cl = p.x + p.nightX - shift, cr = cl + ArcadeConfig::NIGHT_COLUMN_W;
            if (cl - 14.0f < pr && cr + 2.0f > px) return true;
        }
        const float dartShift = (_scrollSpeed + ArcadeConfig::NIGHT_DART_SPEED) * (float)ahead;
        for (int i = 0; i < DARTS; i++) {
            if (!_darts[i].active) continue;
            const float dx = _darts[i].x - dartShift;
            if (dx < pr + 50.0f && dx + 6.0f > px - 2.0f) return true;
        }
        return false;
    }

    // A dart at screen x `x`, at head height over the ground there (the
    // Night boss fires them).
    void spawnDart(float x) {
        const int ground = surfaceYNear((float)ArcadeConfig::RUNNER_BASE_X, (float)ArcadeConfig::RUNNER_BASE_X + RUNNER_WIDTH);
        for (int k = 0; k < DARTS; k++) {
            if (_darts[k].active) continue;
            _darts[k] = Dart{ x, ground - ArcadeConfig::NIGHT_DART_HEIGHT, true, false };
            return;
        }
    }

    bool dartsFlying() const {
        for (int i = 0; i < DARTS; i++) if (_darts[i].active) return true;
        return false;
    }

    // Frames to the end of the stage the runner is in.
    long framesLeftInStage() const {
        return (long)stageStartDistance(stageNumber() + 1) - (long)_distance;
    }

    // A Night obstacle on screen ahead of the runner, or a dart in flight:
    // no boulder is sent then (one needs a jump, these a duck).
    bool nightAhead(float runnerX) const {
        for (int i = 0; i < DARTS; i++) if (_darts[i].active) return true;
        for (int i = 0; i < POOL_SIZE; i++) {
            const Platform &p = _pool[i];
            if (!p.active || p.night == NIGHT_NONE || (p.night == NIGHT_LAUNCHER && p.fired)) continue;
            const float x = p.x + p.nightX;
            if (x > runnerX - 20.0f && x < ArcadeConfig::LANDSCAPE_WIDTH + 100) return true;
        }
        return false;
    }

    float getScrollSpeed() const { return _scrollSpeed; }
    int   getTier() const { return _tier; }
    int   getLoop() const { return _loop; }
    unsigned long getDistance() const { return _distance; }

    // litTo: at Night, how far the runner's lantern reaches (screen x);
    // the ground and gantries past it are drawn dimmer. Flames, lamps and
    // darts stay bright, so they read first.
    void render(GFXcanvas16 &canvas, int litTo = 9999) {
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;

            uint16_t color;
            int fillHeight;
            if (_pool[i].isGroundSegment) {
                color      = groundColor();   // stone/earth, distinct from floating platforms; rotates per loop
                fillHeight = ArcadeConfig::LANDSCAPE_HEIGHT - _pool[i].y;
            } else if (isRuins(_loop) && !_pool[i].isMoving) {
                color      = _pool[i].crumbly ? SANDSTONE_DARK : SANDSTONE;   // crumbling: darker, cracked
                fillHeight = ArcadeConfig::PLATFORM_THICKNESS;
            } else {
                color      = _pool[i].isMoving ? ArcadeConfig::COLOR_CYAN : ArcadeConfig::COLOR_GREEN;
                // Fixed-thickness slab, not a pillar down to the screen bottom —
                // keeps moving platforms a constant visual size as they bob,
                // instead of appearing to grow/shrink and swallow the player.
                fillHeight = ArcadeConfig::PLATFORM_THICKNESS;
            }

            // In up to three parts: lit, half, a quarter. A crumbling slab
            // shakes once it's going.
            const bool shaking = _pool[i].crumbly && _pool[i].crumbleAt >= 0 && !_pool[i].falling;
            const int x0 = (int)_pool[i].x + (shaking && ((_frames / 2) & 1) ? 1 : 0), x1 = x0 + _pool[i].width;
            const int cuts[4] = { x0, constrain(litTo, x0, x1), constrain(litTo + 30, x0, x1), x1 };
            for (int k = 0; k < 3; k++) {
                if (cuts[k + 1] <= cuts[k]) continue;
                const uint16_t c = lit(color, (float)cuts[k] + 0.5f, litTo);
                canvas.fillRect(cuts[k], _pool[i].y, cuts[k + 1] - cuts[k], fillHeight, c);
                drawBrickPattern(canvas, cuts[k], _pool[i].y, cuts[k + 1] - cuts[k], c, x0);
            }

            if (_pool[i].firePitBefore) {
                int fireX = (int)(_pool[i].x - _pool[i].firePitGapWidth);
                int fireW = (int)_pool[i].firePitGapWidth;
                int fireY = _pool[i].y; // both sides are level (see spawnGroundSegment)
                bool flicker = (millis() / 100) % 2 == 0;
                uint16_t fireColor = flicker ? ArcadeConfig::COLOR_ORANGE : ArcadeConfig::COLOR_RED;
                // Fill the whole pit, floor to bottom of screen — no thin
                // ember-bar-only strip that could read as a solid, walkable
                // ledge inside what is actually a full lethal drop.
                canvas.fillRect(fireX, fireY, fireW, ArcadeConfig::LANDSCAPE_HEIGHT - fireY, fireColor);
                for (int fx = fireX; fx < fireX + fireW; fx += 3) {
                    canvas.drawPixel(fx + (flicker ? 1 : 0), fireY - 1, ArcadeConfig::COLOR_YELLOW);
                }
            }

            if (_pool[i].hasSpike) {
                int sx = (int)(_pool[i].x + _pool[i].spikeOffsetX);
                int baseY = _pool[i].y;
                if (_pool[i].spikePhase != SPIKE_DANGER) {
                    // Retracted (or rising): just the tips showing, dim, so
                    // the trap can be seen coming.
                    uint16_t dim = lit(darken(spikeDangerColor()), (float)sx, litTo);
                    canvas.fillTriangle(sx - 6, baseY, sx + 2, baseY, sx - 2, baseY - 2, dim);
                    canvas.fillTriangle(sx - 1, baseY, sx + 7, baseY, sx + 3, baseY - 2, dim);
                }
                if (_pool[i].spikePhase == SPIKE_WARN) {
                    // Telegraph: a thin rising nub, not yet dangerous.
                    canvas.drawFastVLine(sx, baseY - 3, 3, ArcadeConfig::COLOR_YELLOW);
                    canvas.drawFastVLine(sx + 5, baseY - 2, 2, ArcadeConfig::COLOR_YELLOW);
                } else if (_pool[i].spikePhase == SPIKE_DANGER) {
                    uint16_t spikeColor = lit(spikeDangerColor(), (float)sx, litTo);
                    canvas.fillTriangle(sx - 6, baseY, sx + 2, baseY, sx - 2, baseY - 11, spikeColor);
                    canvas.fillTriangle(sx - 1, baseY, sx + 7, baseY, sx + 3, baseY - 11, spikeColor);
                }
            }

            if (_pool[i].night != NIGHT_NONE) renderNight(canvas, _pool[i], litTo);
            if (_pool[i].crumbly) {   // its cracks
                const int cx = x0 + _pool[i].width / 3, cy = _pool[i].y;
                canvas.drawLine(cx, cy, cx + 2, cy + 4, ArcadeConfig::COLOR_BLACK);
                canvas.drawLine(cx + _pool[i].width / 3, cy + 1, cx + _pool[i].width / 3 - 2, cy + 6, ArcadeConfig::COLOR_BLACK);
            }
        }
        _ruins.render(canvas, SANDSTONE, SANDSTONE_DARK);
        for (int i = 0; i < DARTS; i++) {
            if (!_darts[i].active) continue;
            const int dx = (int)_darts[i].x, dy = _darts[i].y;
            canvas.drawFastHLine(dx + 1, dy, 5, ArcadeConfig::COLOR_WHITE);
            canvas.drawPixel(dx, dy, ArcadeConfig::COLOR_RED);              // the point
            canvas.drawFastVLine(dx + 6, dy - 1, 3, ArcadeConfig::COLOR_RED); // the flights
        }
    }

    // A beam: a lattice gantry hanging from under the HUD to its girder,
    // a lamp under it. A jet: a shorter housing whose nozzles sputter, then
    // fill the gap below with flame. A launcher: a post with an eye that
    // flashes before it fires.
    void renderNight(GFXcanvas16 &canvas, const Platform &p, int litTo) {
        const int top = ArcadeConfig::UI_MARGIN_TOP + 1;
        if (p.night == NIGHT_LAUNCHER) {
            const int lx = (int)(p.x + p.nightX), ly = p.y - 10;
            canvas.fillRect(lx, ly, 4, 10, lit(STEEL, (float)lx, litTo));
            const bool glint = p.glintAt && !p.fired && ((millis() / 60) & 1);
            if (glint) canvas.fillRect(lx - 1, ly - 1, 3, 3, ArcadeConfig::COLOR_WHITE);
            else canvas.drawPixel(lx, ly + 1, p.fired ? darken(ArcadeConfig::COLOR_RED) : ArcadeConfig::COLOR_RED);
            return;
        }
        const int cl = (int)(p.x + p.nightX), w = ArcadeConfig::NIGHT_COLUMN_W;
        const int under = p.y - (p.night == NIGHT_BEAM ? ArcadeConfig::NIGHT_BEAM_CLEAR : ArcadeConfig::NIGHT_JET_HOUSING);
        const uint16_t steel = lit(STEEL, (float)cl, litTo), light = lit(STEEL_LIGHT, (float)cl, litTo);
        canvas.fillRect(cl, top, w, under - top, steel);
        canvas.drawFastVLine(cl, top, under - top, light);
        canvas.drawFastVLine(cl + w - 1, top, under - top, light);
        for (int y = top + 2; y < under - 4; y += 8) {   // the lattice
            canvas.drawLine(cl + 1, y, cl + w - 2, y + 6, light);
            canvas.drawLine(cl + w - 2, y, cl + 1, y + 6, light);
        }
        canvas.fillRect(cl, under - 3, w, 3, light);     // the girder / housing foot
        if (p.night == NIGHT_BEAM) {
            canvas.drawPixel(cl + w / 2, under, ArcadeConfig::COLOR_YELLOW);   // the lamp, always lit
            canvas.drawPixel(cl + w / 2 - 1, under, darken(ArcadeConfig::COLOR_YELLOW));
            canvas.drawPixel(cl + w / 2 + 1, under, darken(ArcadeConfig::COLOR_YELLOW));
            return;
        }
        const bool flick = (millis() / 70) & 1;
        if (p.jetPhase == SPIKE_WARN) {
            for (int k = 0; k < 3; k++)
                canvas.drawPixel(cl + 2 + k * 3 + (flick ? 1 : 0), under + (k & 1), ArcadeConfig::COLOR_ORANGE);
        } else if (p.jetPhase == SPIKE_DANGER) {
            const int bottom = p.y - ArcadeConfig::NIGHT_BEAM_CLEAR;
            canvas.fillRect(cl, under, w, bottom - under, flick ? ArcadeConfig::COLOR_ORANGE : ArcadeConfig::COLOR_RED);
            for (int x = cl + 1; x < cl + w - 1; x += 2)
                canvas.drawPixel(x, under + (flick ? 1 : 2), ArcadeConfig::COLOR_YELLOW);
        } else {
            canvas.drawFastHLine(cl + 1, under, w - 2, darken(ArcadeConfig::COLOR_RED));   // nozzles, cold
        }
    }
};

#endif // PLATFORM_MANAGER_H
