#ifndef RUNNER_BOSS_H
#define RUNNER_BOSS_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "PlayerRunner.h"
#include "PlatformManager.h"

// =============================================================================
// RUNNER BOSS
// The boss stretch ending every loop (its ninth stage; docs/design/
// RunnerFlux.md): a big ship flies in over plain ground and drops rocks in
// patterns for RUNNER_BOSS_DISTANCE frames, then leaves. Surviving it ends
// the loop. Patterns, one at a time, the next chosen so it's never the
// same twice running:
//   LINE   the bay flashes, then a spread of rocks falls across the
//          runner's reach with one gap in it: step into the gap.
//   TRACK  rocks one after another, each aimed where the runner is: keep
//          moving.
//   ROLL   a rock lands ahead and rolls at the runner: jump it.
//   DART   (Night's boss) darts fired at head height: duck.
//   CRACK  (the Ruins' boss) two rocks land ahead and crack the ground:
//          holes coming at the runner, to jump.
//   GUST   (Storm's boss) a gust, warned of, then three aimed rocks while
//          it blows: fight the wind to dodge them.
// Everything runs in frames, like the scroll, so the demo's autopilot can
// predict it exactly (wouldHit).
// =============================================================================
class RunnerBoss {
public:
    enum Phase { IDLE, ENTER, FIGHT, LEAVE };
    enum Pattern { P_LINE, P_TRACK, P_ROLL, P_DART, P_CRACK, P_GUST, P_COUNT };

    // Rocks fall from just under the ship at ROCK_TOP with this pull,
    // slow (about 70 frames to the ground) so a spread can be read and
    // dodged: the owner found 47 frames too quick.
    static constexpr float ROCK_TOP = 32.0f;
    static constexpr float ROCK_VY0 = 0.3f;
    static constexpr float ROCK_G   = 0.024f;
    static const int ROCK_R = 5;
    static const int ENTER_FRAMES = 75;     // flying in, nothing dropped
    static const int TELEGRAPH    = 24;     // the bay flashes before a line drops
    static const int LEAVE_BEFORE = 100;    // no new pattern this close to the end
    static const int GUST_BLOW    = 90;     // GUST: frames its gust blows

private:
    struct Rock {
        float x, y, vy;
        float landY;       // the ground's top under it
        bool  active, roller, rolling, scored;
        bool  cracks;      // lands cracking the ground (the Ruins' boss)
    };
    static const int ROCKS = 10;
    Rock _rocks[ROCKS];

    Phase _phase = IDLE;
    bool  _night = false;
    int   _world = 0;          // 0 Outpost, 1 Night, 2 Ruins, 3 Storm
    int   _loop = 0;
    long  _frame = 0;          // frames since it began
    long  _nextAt = 0;         // frame the next pattern starts
    Pattern _pattern = P_LINE, _last = P_COUNT;
    int   _step = 0;           // within a pattern: rocks or darts sent so far
    long  _stepAt = 0;
    float _gapX = 0;           // LINE: the gap's centre
    float _shipX = 200, _shipY = 22;
    long  _flashUntil = -1;
    bool  _dropped = false;    // a pattern released something this frame (its sound)

    // The pause between one pattern's last rock (or dart) going and the
    // next pattern starting: patterns never overlap.
    int breather() const { return max(8, 16 - 3 * _loop); }

    bool patternDone() const {
        switch (_pattern) {
            case P_LINE:  return _step >= 1;
            case P_TRACK: return _step >= 3 + min(_loop, 2);
            case P_ROLL:  return _step >= 1;
            case P_DART:  return _step >= 2;
            case P_CRACK: return _step >= 1;
            case P_GUST:  return _step >= 3;
            default:      return true;
        }
    }
    bool rocksFalling() const {
        for (int i = 0; i < ROCKS; i++) if (_rocks[i].active) return true;
        return false;
    }

    // A rock at x under the ship; the rock, or nullptr if all are falling.
    Rock* spawnRock(float x, const PlatformManager &platforms, bool roller) {
        for (int i = 0; i < ROCKS; i++) {
            if (_rocks[i].active) continue;
            const int ground = platforms.surfaceYNear(x - ROCK_R, x + ROCK_R);
            _rocks[i] = Rock{ x, ROCK_TOP, ROCK_VY0, (float)ground, true, roller, false, false, false };
            return &_rocks[i];
        }
        return nullptr;
    }

    // The runner's centre can be anywhere from here to there on screen.
    static float reachLo() { return ArcadeConfig::RUNNER_BASE_X + ArcadeConfig::RUNNER_X_MIN_OFFSET + RUNNER_WIDTH / 2.0f; }
    static float reachHi() { return ArcadeConfig::RUNNER_BASE_X + ArcadeConfig::RUNNER_BOSS_MAX_OFFSET + RUNNER_WIDTH / 2.0f; }

    // LINE's gap is never further than this from the runner: it can get
    // there between the flash and the rocks landing (about 90 frames).
    static const int GAP_REACH = 40;

    void choosePattern(float runnerCentre) {
        // The world's patterns: the three, and Night's darts, the Ruins'
        // cracks or Storm's gusts.
        const int kinds = _world == 0 ? 3 : 4;
        Pattern p;
        do {
            p = (Pattern)random(0, kinds);
            if (p == P_DART && _world == 2) p = P_CRACK;
            if (p == P_DART && _world == 3) p = P_GUST;
        } while (p == _last);
        _pattern = _last = p;
        _step = 0;
        _stepAt = _frame;
        if (p == P_LINE) {
            const float lo = max(reachLo() + 4.0f, runnerCentre - GAP_REACH);
            const float hi = min(reachHi() - 4.0f, runnerCentre + GAP_REACH);
            _gapX = (float)random((long)lo, (long)hi + 1);
            _flashUntil = _frame + TELEGRAPH;
        }
    }

    // One frame of the current pattern.
    void runPattern(float runnerCentre, PlatformManager &platforms) {
        const long since = _frame - _stepAt;
        switch (_pattern) {
            case P_LINE:
                // After the flash: rocks every 13px from the left edge to
                // past the runner's reach, none within 18px of the gap's
                // centre (a 26px clear gap for an 18px runner).
                if (_step == 0 && since >= TELEGRAPH) {
                    for (float x = 4.0f; x < reachHi() + 30.0f; x += 13.0f)
                        if (fabsf(x - _gapX) >= 18.0f) spawnRock(x, platforms, false);
                    _step = 1;
                    _dropped = true;
                }
                break;
            case P_TRACK: {
                const int n = 3 + min(_loop, 2);
                if (_step < n && since >= (long)_step * 24) {
                    // Aimed at the runner, but never within 16px of either
                    // end of its reach: pinned at an end, it's always got
                    // room to stand clear.
                    spawnRock(constrain(runnerCentre, reachLo() + 16.0f, reachHi() - 16.0f), platforms, false);
                    ++_step;
                    _dropped = true;
                }
                break;
            }
            case P_ROLL:
                if (_step == 0) {
                    spawnRock(ArcadeConfig::LANDSCAPE_WIDTH - 20.0f, platforms, true);
                    _step = 1;
                    _dropped = true;
                }
                break;
            case P_CRACK:
                // Two rocks well ahead, 50px apart: where they land the ground
                // cracks, then opens.
                if (_step == 0) {
                    for (int k = 0; k < 2; k++)
                        if (Rock* r = spawnRock(runnerCentre + 60.0f + 50.0f * k, platforms, false)) r->cracks = true;
                    _step = 1;
                    _dropped = true;
                }
                break;
            case P_GUST:
                // The gust's warning starts with the pattern; three rocks,
                // aimed as TRACK's, 20 frames apart from 10 in, land while
                // it blows (about 80 frames to fall).
                if (since == 0) platforms.storm().forceGust(random(0, 2) ? 1 : -1, GUST_BLOW);
                if (_step < 3 && since >= 10 + (long)_step * 20) {
                    spawnRock(constrain(runnerCentre, reachLo() + 16.0f, reachHi() - 16.0f), platforms, false);
                    ++_step;
                    _dropped = true;
                }
                break;
            case P_DART:
                if (_step < 2 && since >= (long)_step * 40) {
                    platforms.spawnDart(ArcadeConfig::LANDSCAPE_WIDTH + 4.0f);
                    ++_step;
                    _dropped = true;
                }
                break;
            default: break;
        }
    }

public:
    RunnerBoss() { reset(); }

    void reset() {
        _phase = IDLE;
        for (int i = 0; i < ROCKS; i++) _rocks[i].active = false;
        _shipX = 200;
        _flashUntil = -1;
    }

    // The boss stage has begun (or begun again after a lost life).
    void start(int loop) {
        reset();
        _phase = ENTER;
        _loop = loop;
        _night = PlatformManager::isNight(loop);
        _world = PlatformManager::worldOf(loop);
        _frame = 0;
        _nextAt = ENTER_FRAMES;
        _last = P_COUNT;
        _pattern = P_LINE;
        _step = 1;   // nothing pending
    }

    bool active() const { return _phase != IDLE; }
    Phase phase() const { return _phase; }

    // Once a playing frame. `framesLeft`: frames to the stretch's end.
    // Returns true when it dropped something (for its sound); sets
    // playerHit when a rock touches the runner.
    bool update(float scrollSpeed, long framesLeft, PlayerRunner &player, PlatformManager &platforms,
                ParticleManager &particles, bool &playerHit) {
        _dropped = false;
        if (_phase == IDLE) return false;
        ++_frame;
        const float centre = player.getX() + RUNNER_WIDTH / 2.0f;

        // The ship: in from the right, then sways over the runner's half of
        // the screen; at the end, off to the right again.
        const float sway = 70.0f + 30.0f * sinf((float)_frame * 0.03f);
        if (_phase == ENTER) {
            _shipX += (sway - _shipX) * 0.05f;
            if (_frame >= ENTER_FRAMES) _phase = FIGHT;
        } else if (_phase == FIGHT) {
            _shipX += (sway - _shipX) * 0.1f;
            if (!patternDone() || rocksFalling() || platforms.dartsFlying() || platforms.storm().gusting())
                _nextAt = _frame + breather();
            if (framesLeft <= LEAVE_BEFORE) _phase = LEAVE;
            else if (_frame >= _nextAt) choosePattern(centre);
            if (_phase == FIGHT) runPattern(centre, platforms);
        } else if (_phase == LEAVE) {
            _shipX += 1.5f;
        }

        // Rocks: fall, then shatter on the ground, or (a roller) roll at
        // the runner faster than the scroll. They move after the runner
        // has, like every hazard.
        const float px = player.getX(), pr = px + RUNNER_WIDTH;
        const float top = player.hitTop(), bottom = player.getY() + RUNNER_HEIGHT;
        for (int i = 0; i < ROCKS; i++) {
            Rock &r = _rocks[i];
            if (!r.active) continue;
            if (r.rolling) {
                r.x -= scrollSpeed + ROLL_EXTRA;
                if (r.x < -10) { r.active = false; continue; }
            } else {
                r.vy += ROCK_G;
                r.y  += r.vy;
                if (r.y + ROCK_R >= r.landY) {
                    if (r.roller) { r.rolling = true; r.y = r.landY - ROCK_R; }
                    else {
                        if (r.cracks) platforms.ruins().addHole(r.x, (int)r.landY, RuinsLayer::HOLE_CRACK);
                        particles.spawnExplosion(r.x, r.landY - 2, ArcadeConfig::COLOR_GREY, 5, 350);
                        r.active = false;
                        continue;
                    }
                }
            }
            const float cx = max(px, min(r.x, pr)), cy = max(top, min(r.y, bottom));
            const float dx = r.x - cx, dy = r.y - cy;
            if (dx * dx + dy * dy < (float)(ROCK_R * ROCK_R) && !player.isInvincible()) {
                particles.spawnExplosion(r.x, r.y, ArcadeConfig::COLOR_GREY, 8);
                r.active = false;
                playerHit = true;
            }
        }
        if (_phase == LEAVE && _shipX > ArcadeConfig::LANDSCAPE_WIDTH + 40) _phase = IDLE;
        return _dropped;
    }

    static constexpr float ROLL_EXTRA = 0.6f;   // a roller's speed over the scroll

    // Would a rock touch this box `t` frames from now? Exact, from each
    // rock's fall and roll; for the demo's autopilot, with a pixel to spare.
    // (Rocks not dropped yet can't be seen coming.)
    bool wouldHit(int t, float scrollSpeed, float px, float pr, float top, float bottom) const {
        const float r2 = (float)(ROCK_R + 1) * (float)(ROCK_R + 1);
        for (int i = 0; i < ROCKS; i++) {
            const Rock &r = _rocks[i];
            if (!r.active) continue;
            float x = r.x, y;
            if (r.rolling) {
                x -= (scrollSpeed + ROLL_EXTRA) * (float)t;
                y = r.y;
            } else {
                // Frames until it lands: the first k with y + vy*k + g*k(k+1)/2 + R >= landY.
                const float need = r.landY - ROCK_R - r.y;
                const float g = ROCK_G, b = r.vy + g * 0.5f;
                const float landK = (-b + sqrtf(b * b + 2.0f * g * need)) / g;
                const float ft = (float)t;
                if (ft >= landK) {
                    if (!r.roller) continue;
                    x -= (scrollSpeed + ROLL_EXTRA) * (ft - ceilf(landK));
                    y = r.landY - ROCK_R;
                } else {
                    y = r.y + r.vy * ft + g * ft * (ft + 1.0f) * 0.5f;
                }
            }
            const float cx = max(px, min(x, pr)), cy = max(top, min(y, bottom));
            const float dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy < r2) return true;
        }
        return false;
    }

    // A roller the runner has got past: points, like a boulder.
    int takeCleared(float playerX, float &popX, float &popY) {
        for (int i = 0; i < ROCKS; i++) {
            Rock &r = _rocks[i];
            if (!r.active || !r.rolling || r.scored || r.x + ROCK_R >= playerX) continue;
            r.scored = true;
            popX = r.x;
            popY = r.y - 14.0f;
            return ArcadeConfig::RUNNER_BOULDER_POINTS;
        }
        return 0;
    }

    // A gunship: a long hull, a canopy, engine glows, and the bay under it
    // that flashes before a spread drops. Rocks as rough octagons.
    void render(GFXcanvas16 &canvas) const {
        if (_phase != IDLE) {
            const int x = (int)_shipX, y = (int)_shipY + (int)(2.0f * sinf((float)_frame * 0.08f));
            const uint16_t hull = _night ? 0x4A8C : ArcadeConfig::COLOR_MAGENTA;
            const uint16_t dark = _night ? 0x2945 : 0x780F;
            canvas.fillRoundRect(x - 22, y - 4, 44, 9, 4, hull);
            canvas.fillRect(x - 18, y + 4, 36, 3, dark);
            canvas.fillRoundRect(x - 6, y - 8, 14, 6, 3, ArcadeConfig::COLOR_CYAN);   // canopy
            canvas.drawFastHLine(x - 4, y - 7, 8, ArcadeConfig::COLOR_WHITE);
            const bool glow = (_frame / 4) & 1;
            canvas.fillRect(x + 22, y - 2, 3, 4, glow ? ArcadeConfig::COLOR_ORANGE : ArcadeConfig::COLOR_YELLOW);
            canvas.fillRect(x - 25, y - 2, 3, 4, glow ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_ORANGE);
            const bool flash = _frame < _flashUntil && ((_frame / 2) & 1);
            canvas.fillRect(x - 10, y + 6, 20, 3, flash ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_RED);
            for (int k = -16; k <= 16; k += 8) canvas.drawPixel(x + k, y, ArcadeConfig::COLOR_YELLOW);   // portholes
        }
        for (int i = 0; i < ROCKS; i++) {
            const Rock &r = _rocks[i];
            if (!r.active) continue;
            const int cx = (int)r.x, cy = (int)r.y;
            const uint16_t c = r.roller ? ArcadeConfig::COLOR_AMBER : ArcadeConfig::COLOR_GREY;
            canvas.drawLine(cx - 2, cy - ROCK_R, cx + 3, cy - ROCK_R + 1, c);
            canvas.drawLine(cx + 3, cy - ROCK_R + 1, cx + ROCK_R, cy + 2, c);
            canvas.drawLine(cx + ROCK_R, cy + 2, cx + 1, cy + ROCK_R, c);
            canvas.drawLine(cx + 1, cy + ROCK_R, cx - 4, cy + 3, c);
            canvas.drawLine(cx - 4, cy + 3, cx - ROCK_R, cy - 1, c);
            canvas.drawLine(cx - ROCK_R, cy - 1, cx - 2, cy - ROCK_R, c);
        }
    }

    // For the harness.
    float gapX() const { return _gapX; }
    Pattern pattern() const { return _pattern; }
};

#endif // RUNNER_BOSS_H
