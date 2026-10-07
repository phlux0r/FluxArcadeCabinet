#ifndef RUINS_LAYER_H
#define RUINS_LAYER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "PlayerRunner.h"

// =============================================================================
// RUINS LAYER
// What the Ruins (the third loop of every four, docs/design/RunnerFlux.md)
// add on top of PlatformManager's ground, all scrolling with it:
//   - springs, each just before a fire pit: running over one throws the
//     runner up onto a long high slab across the pit, with a chain of gems
//     along it (collect the whole chain and it counts twice);
//   - falling pillars: one cracks as the runner nears it, topples towards
//     it and lies across the path as a log to jump;
//   - holes the Ruins boss's rocks crack in the ground: a crack, then a
//     gap to jump.
// Timing is in frames (update() calls), so the demo's autopilot can
// predict it. Positions are screen x, as PlatformManager's; the prediction
// passes the runner shifted by the scroll instead of moving these.
// =============================================================================
class RuinsLayer {
public:
    // Pillar phases, in frames from its crack.
    static const int CRACK_FRAMES = 22;
    static const int FALL_FRAMES  = 12;
    // A broken column: short enough that its log, trimmed 3px each end, is
    // cleared by a plain jump at the Ruins' speed with about 7 frames to
    // choose from (36 long needed the push; 24, 4 frames).
    static const int PILLAR_LEN   = 20;
    static const int LOG_H        = 6;
    static const int HOLE_W       = 14;
    static const int HOLE_CRACK   = 20;   // a hole shows as a crack this long before it opens

    struct Slab   { float x; int y, w; bool active; };
    struct Gem    { float x; int y; int chain; bool active, taken; };
    struct Spring { float x; int y; long firedAt; bool active; };
    struct Pillar { float x; int y; long crackAt; bool active, scored; };
    struct Hole   { float x; int y; long openAt; bool active, scored; };

    static const int SLABS = 3, GEMS = 24, SPRINGS = 3, PILLARS = 3, HOLES = 4;
    Slab   _slabs[SLABS];
    Gem    _gems[GEMS];
    Spring _springs[SPRINGS];
    Pillar _pillars[PILLARS];
    Hole   _holes[HOLES];
    long   _clock = 0;
    int    _nextChain = 1;
    // Chains: gems in each, taken, and whether one's been missed.
    static const int CHAINS = 4;
    struct Chain { int id, total, taken; bool broken, done; };
    Chain  _chains[CHAINS];

    void reset() {
        for (auto &s : _slabs)   s.active = false;
        for (auto &g : _gems)    g.active = false;
        for (auto &s : _springs) s.active = false;
        for (auto &p : _pillars) p.active = false;
        for (auto &h : _holes)   h.active = false;
        for (auto &c : _chains)  c.id = 0;
        _clock = 0;
    }

    // ---- Placing (PlatformManager, as it builds the ground) -------------

    // A spring at x on ground at y, and the high slab it throws the runner
    // onto, from just past it to `toX`, `rise` px above the ground, with
    // a chain of gems along it.
    void addSpringRoute(float x, int y, float toX, int rise) {
        for (auto &s : _springs) {
            if (s.active) continue;
            s = Spring{ x, y, -1000, true };
            break;
        }
        const float from = x + 16.0f;
        for (auto &sl : _slabs) {
            if (sl.active) continue;
            sl = Slab{ from, y - rise, (int)(toX - from), true };
            break;
        }
        int chainSlot = -1;
        for (int c = 0; c < CHAINS; c++) if (_chains[c].id == 0) { chainSlot = c; break; }
        if (chainSlot < 0) return;
        const int id = _nextChain++;
        _chains[chainSlot] = Chain{ id, 0, 0, false, false };
        for (float gx = from + 8.0f; gx < toX - 4.0f; gx += 12.0f) {
            for (auto &g : _gems) {
                if (g.active) continue;
                g = Gem{ gx, y - rise - 9, id, true, false };
                ++_chains[chainSlot].total;
                break;
            }
        }
    }

    // A pillar standing with its foot at x on ground at y (it falls left,
    // lying over [x - PILLAR_LEN, x]).
    void addPillar(float x, int y) {
        for (auto &p : _pillars) {
            if (p.active) continue;
            p = Pillar{ x, y, -1, true, false };
            return;
        }
    }

    // A crack at x on ground at y, opening into a hole `delay` frames on.
    void addHole(float x, int y, int delay) {
        for (auto &h : _holes) {
            if (h.active) continue;
            h = Hole{ x - HOLE_W / 2.0f, y, _clock + delay, true, false };
            return;
        }
    }

    // ---- Each frame -----------------------------------------------------

    // Scroll, and start pillars cracking as the runner nears them (its
    // right edge `runnerRight` on screen). Returns a bit per sound due:
    // 1 a pillar toppled.
    int update(float scroll, float runnerRight) {
        ++_clock;
        int events = 0;
        for (auto &s : _slabs)   if (s.active) { s.x -= scroll; if (s.x + s.w < -4) s.active = false; }
        for (auto &s : _springs) if (s.active) { s.x -= scroll; if (s.x < -16) s.active = false; }
        for (auto &h : _holes)   if (h.active) { h.x -= scroll; if (h.x + HOLE_W < -4) h.active = false; }
        for (auto &p : _pillars) {
            if (!p.active) continue;
            p.x -= scroll;
            if (p.x < -10) { p.active = false; continue; }
            if (p.crackAt < 0 && p.x - runnerRight < ArcadeConfig::RUINS_PILLAR_RANGE) p.crackAt = _clock;
            if (p.crackAt >= 0 && _clock - p.crackAt == CRACK_FRAMES) events |= 1;
        }
        for (auto &g : _gems) {
            if (!g.active) continue;
            g.x -= scroll;
            if (g.x < -4) {
                g.active = false;
                if (!g.taken) breakChain(g.chain);   // gone past: the chain's broken
            }
        }
        // A chain with no gems left is finished with: its slot's free.
        for (auto &c : _chains) {
            if (c.id == 0) continue;
            bool any = false;
            for (const auto &g : _gems) any |= g.active && g.chain == c.id;
            if (!any) c.id = 0;
        }
        return events;
    }

    // ---- The runner -----------------------------------------------------

    // The high slabs, for PlatformManager::groundYAt: the topmost surface
    // under the runner's feet (within 2px), or -1.
    int slabYAt(float px, float pr, float bottom) const {
        int best = -1;
        for (const auto &s : _slabs) {
            if (!s.active || pr <= s.x || px >= s.x + s.w) continue;
            if (bottom <= s.y + 2 && (best == -1 || s.y < best)) best = s.y;
        }
        return best;
    }

    // A spring under the runner's middle, its feet on the ground there.
    bool springUnder(float px, float pr, float bottom) const {
        const float mid = (px + pr) * 0.5f;
        for (const auto &s : _springs) {
            if (!s.active || mid < s.x - 6.0f || mid > s.x + 6.0f) continue;
            if (fabsf(bottom - (float)s.y) < 3.0f) return true;
        }
        return false;
    }
    void springFired(float px, float pr) {
        const float mid = (px + pr) * 0.5f;
        for (auto &s : _springs)
            if (s.active && mid >= s.x - 6.0f && mid <= s.x + 6.0f) s.firedAt = _clock;
    }

    // Gems the runner's box touches: taken. Returns the points (5 each);
    // `chainBonus` gets a whole chain's worth if this completed one, and
    // where to show it.
    int collect(float px, float pr, float top, float bottom, int &chainBonus, float &popX, float &popY) {
        int pts = 0;
        chainBonus = 0;
        for (auto &g : _gems) {
            if (!g.active || g.taken) continue;
            if (pr < g.x - 2 || px > g.x + 2 || bottom < g.y - 2 || top > g.y + 2) continue;
            g.taken = true;
            g.active = false;
            pts += ArcadeConfig::RUINS_GEM_POINTS;
            for (auto &c : _chains) {
                if (c.id != g.chain) continue;
                if (++c.taken == c.total && !c.broken && !c.done) {
                    c.done = true;
                    chainBonus = c.total * ArcadeConfig::RUINS_GEM_POINTS;
                    popX = g.x;
                    popY = (float)g.y - 8.0f;
                }
            }
        }
        return pts;
    }

    // Does a pillar or a hole get the runner? Its box is px..pr, top to
    // bottom (shifted by the scroll for `ahead` frames on, as
    // PlatformManager's prediction passes it); runnerRightNow is its right
    // edge on screen now, for when an uncracked pillar will crack.
    bool hits(float px, float pr, float top, float bottom, int ahead, float runnerRightNow, float scroll) const {
        const long now = _clock + ahead;
        for (const auto &p : _pillars) {
            if (!p.active) continue;
            long crackAt = p.crackAt;
            if (crackAt < 0) {
                // It cracks when the runner's within range: then, at this scroll.
                const float gap = p.x - runnerRightNow - ArcadeConfig::RUINS_PILLAR_RANGE;
                crackAt = _clock + (gap <= 0 ? 0 : (long)ceilf(gap / max(scroll, 0.1f)));
            }
            const long since = now - crackAt - CRACK_FRAMES;
            if (since < 0) continue;   // standing, in the background
            if (since >= FALL_FRAMES) {
                // Down: a log lying left of its foot.
                if (pr - 3 > p.x - PILLAR_LEN && px + 3 < p.x + 2 && bottom > p.y - LOG_H && top < p.y) return true;
                continue;
            }
            // Falling: a line from its foot, swinging from upright to flat.
            const float f = (float)since / (float)FALL_FRAMES, a = f * f * (float)(PI / 2.0);
            const float dx = -sinf(a), dy = -cosf(a);
            for (int k = 1; k <= 8; k++) {
                const float x = p.x + dx * PILLAR_LEN * k / 8.0f, y = p.y + dy * PILLAR_LEN * k / 8.0f;
                if (x > px - 2 && x < pr + 2 && y > top - 2 && y < bottom + 2) return true;
            }
        }
        // An open hole: the runner's middle over it, its feet down.
        const float mid = (px + pr) * 0.5f;
        for (const auto &h : _holes) {
            if (!h.active || now < h.openAt) continue;
            if (mid >= h.x && mid < h.x + HOLE_W && bottom >= h.y - 4) return true;
        }
        return false;
    }

    // Logs and holes the runner has got past: points, like a boulder.
    int takeCleared(float playerX, float &popX, float &popY) {
        for (auto &p : _pillars) {
            if (!p.active || p.scored || p.crackAt < 0 || _clock - p.crackAt < CRACK_FRAMES + FALL_FRAMES) continue;
            if (p.x + 2 >= playerX) continue;
            p.scored = true;
            popX = p.x - PILLAR_LEN / 2.0f; popY = (float)p.y - 16;
            return ArcadeConfig::RUNNER_BOULDER_POINTS;
        }
        for (auto &h : _holes) {
            if (!h.active || h.scored || _clock < h.openAt || h.x + HOLE_W >= playerX) continue;
            h.scored = true;
            popX = h.x + HOLE_W / 2.0f; popY = (float)h.y - 12;
            return ArcadeConfig::RUNNER_PIT_POINTS;
        }
        return 0;
    }

    // A pillar or a high slab ahead (no boulder sent while one is).
    bool ahead(float runnerX) const {
        for (const auto &p : _pillars) if (p.active && p.x > runnerX - 10 && p.x < ArcadeConfig::LANDSCAPE_WIDTH + 80) return true;
        return false;
    }

    // ---- Drawing --------------------------------------------------------

    void render(GFXcanvas16 &canvas, uint16_t stone, uint16_t stoneDark) const {
        for (const auto &h : _holes) {
            if (!h.active) continue;
            const int x = (int)h.x;
            if (_clock < h.openAt) {   // the crack, widening
                const int w = (int)(HOLE_W * (1.0f - (float)(h.openAt - _clock) / HOLE_CRACK));
                for (int k = 0; k < 4; k++)
                    canvas.drawLine(x + HOLE_W / 2 - 3 + k * 2, h.y, x + HOLE_W / 2 - 2 + k, h.y + 4, ArcadeConfig::COLOR_BLACK);
                if (w > 0) canvas.fillRect(x + (HOLE_W - w) / 2, h.y, w, 2, ArcadeConfig::COLOR_BLACK);
            } else {
                canvas.fillRect(x, h.y, HOLE_W, ArcadeConfig::LANDSCAPE_HEIGHT - h.y, ArcadeConfig::COLOR_BLACK);
                canvas.drawFastVLine(x, h.y, 6, stoneDark);
                canvas.drawFastVLine(x + HOLE_W - 1, h.y, 6, stoneDark);
            }
        }
        for (const auto &s : _slabs) {
            if (!s.active) continue;
            canvas.fillRect((int)s.x, s.y, s.w, 5, stone);
            canvas.drawFastHLine((int)s.x, s.y + 5, s.w, stoneDark);
            for (int bx = (int)s.x + 8; bx < (int)s.x + s.w; bx += 10) canvas.drawFastVLine(bx, s.y, 5, stoneDark);
        }
        for (const auto &g : _gems) {
            if (!g.active) continue;
            const int x = (int)g.x, y = g.y;
            const bool glint = ((_clock + x) / 6) & 1;
            canvas.fillTriangle(x, y - 3, x - 2, y, x + 2, y, ArcadeConfig::COLOR_GREEN);
            canvas.fillTriangle(x, y + 3, x - 2, y, x + 2, y, glint ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_GREEN);
        }
        for (const auto &s : _springs) {
            if (!s.active) continue;
            const int x = (int)s.x;
            const bool sprung = _clock - s.firedAt < 8;
            const int h = sprung ? 7 : 4;
            for (int k = 0; k < h; k += 2) canvas.drawFastHLine(x - 3, s.y - 1 - k, 7, ArcadeConfig::COLOR_GREY);
            canvas.fillRect(x - 6, s.y - 2 - h, 13, 2, ArcadeConfig::COLOR_RED);   // the plate
        }
        for (const auto &p : _pillars) {
            if (!p.active) continue;
            const int x = (int)p.x, y = p.y;
            const long since = p.crackAt < 0 ? -1 : _clock - p.crackAt;
            if (since < CRACK_FRAMES) {
                // Standing (behind the path), shaking and cracked once it's going.
                const int jig = since >= 0 && ((_clock / 2) & 1) ? 1 : 0;
                canvas.fillRect(x - 4 + jig, y - PILLAR_LEN, 7, PILLAR_LEN, stoneDark);
                canvas.drawFastVLine(x - 2 + jig, y - PILLAR_LEN + 3, PILLAR_LEN - 5, stone);
                canvas.drawFastVLine(x + 1 + jig, y - PILLAR_LEN + 3, PILLAR_LEN - 5, stone);
                canvas.fillRect(x - 5 + jig, y - PILLAR_LEN, 9, 2, stone);   // its broken capital
                if (since >= 0) {
                    canvas.drawLine(x - 3 + jig, y - 12, x + 2 + jig, y - 8, ArcadeConfig::COLOR_BLACK);
                    canvas.drawLine(x + 2 + jig, y - 8, x - 1 + jig, y - 4, ArcadeConfig::COLOR_BLACK);
                }
            } else if (since < CRACK_FRAMES + FALL_FRAMES) {
                const float f = (float)(since - CRACK_FRAMES) / FALL_FRAMES, a = f * f * (float)(PI / 2.0);
                const int ex = x - (int)(sinf(a) * PILLAR_LEN), ey = y - (int)(cosf(a) * PILLAR_LEN);
                for (int d = -2; d <= 2; d++) canvas.drawLine(x + d, y, ex + d, ey, d == 0 ? stone : stoneDark);
            } else {
                canvas.fillRect(x - PILLAR_LEN, y - LOG_H, PILLAR_LEN + 2, LOG_H, stone);
                canvas.drawFastHLine(x - PILLAR_LEN, y - LOG_H, PILLAR_LEN + 2, stoneDark);
                canvas.drawFastHLine(x - PILLAR_LEN, y - 3, PILLAR_LEN + 2, stoneDark);
                canvas.drawFastVLine(x - PILLAR_LEN, y - LOG_H, LOG_H, stoneDark);
            }
        }
    }

private:
    void breakChain(int id) {
        for (auto &c : _chains) if (c.id == id) c.broken = true;
    }
};

#endif // RUINS_LAYER_H
