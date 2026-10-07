#ifndef STORM_LAYER_H
#define STORM_LAYER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

// =============================================================================
// STORM LAYER
// Storm's gusts (every fourth loop, docs/design/RunnerFlux.md): one at a
// time, each warned of by streaks blowing its way and arrows under the HUD
// for STORM_TELL frames, then blowing the runner forward or back for its
// length. PlatformManager says how long gusts may go on for (to the end
// of the stages that have them), so a gust is never cut off or carried
// into a stage without them; the Storm boss calls one up itself.
// (Conveyor ground is PlatformManager's, on its blocks.)
//
// All in frames (update() calls), and each gust is chosen as the last one
// ends, at least STORM_GAP_MIN frames ahead: the demo's autopilot knows the
// wind over its whole lookahead (windAt). The streaks are drawn from a
// hash of the clock, not random(), so drawing leaves the game's sequence
// alone.
// =============================================================================
class StormLayer {
public:
    static const long NONE = -1;

    void reset() {
        _clock = 0;
        _tellAt = NONE;
        _told = NONE;
        _quietUntil = 0;
        _dir = 1;
        _blow = ArcadeConfig::STORM_BLOW;
    }

    // Once a frame. `framesLeft`: how many frames from now gusts may still
    // blow (0 where there are none). A gust's chosen as the last one ends,
    // if the whole of it fits; if not, none till that stretch is over.
    void update(long framesLeft) {
        ++_clock;
        if (_tellAt != NONE && _clock >= endAt()) _tellAt = NONE;
        if (_tellAt != NONE || framesLeft <= 0 || _clock < _quietUntil) return;
        const long gap = random(ArcadeConfig::STORM_GAP_MIN, ArcadeConfig::STORM_GAP_MAX + 1);
        const int dir = random(0, 2) ? 1 : -1;
        // (Its last frame blows gap + TELL + BLOW - 1 frames on, a frame
        // before the distance that frame reaches: it must be short of the end.)
        if (gap + ArcadeConfig::STORM_TELL + ArcadeConfig::STORM_BLOW >= framesLeft) {
            _quietUntil = _clock + framesLeft;
            return;
        }
        _tellAt = _clock + gap;
        _dir = dir;
        _blow = ArcadeConfig::STORM_BLOW;
    }

    // A gust now (the Storm boss): its warning starts this frame, and it
    // blows `blow` frames, towards `dir` (1 forward, -1 back).
    void forceGust(int dir, int blow) {
        _tellAt = _clock;
        _dir = dir;
        _blow = blow;
    }

    // The wind's push on the runner (px a frame, + forward) `ahead` frames
    // from now; wind() is this frame's.
    float windAt(int ahead) const {
        const long c = _clock + ahead;
        if (_tellAt == NONE || c < blowAt() || c >= endAt()) return 0.0f;
        return (float)_dir * ArcadeConfig::STORM_GUST;
    }
    float wind() const { return windAt(0); }

    bool telling() const { return _tellAt != NONE && _clock >= _tellAt && _clock < blowAt(); }
    bool blowing() const { return wind() != 0.0f; }
    // A gust being warned of or blowing (the boss waits it out).
    bool gusting() const { return _tellAt != NONE && _clock >= _tellAt; }
    int  dir() const { return _dir; }

    // True once as each gust's warning starts (for its sound).
    bool takeTell() {
        if (_tellAt == NONE || _clock < _tellAt || _told == _tellAt) return false;
        _told = _tellAt;
        return true;
    }

    // Streaks across the play area, blowing the gust's way: a few while
    // it's warned of, more and faster while it blows; and arrows under the
    // HUD pointing its way, flashing during the warning.
    void render(GFXcanvas16 &canvas) const {
        if (!gusting()) return;
        const bool blow = blowing();
        const int W = ArcadeConfig::LANDSCAPE_WIDTH, span = W + 40;
        const int n = blow ? 18 : 7, speed = blow ? 6 : 3;
        const uint16_t c = blow ? 0xBDF7 : 0x7BCF;   // light, then mid grey
        for (int i = 0; i < n; i++) {
            const uint32_t h = (uint32_t)(i + 1) * 2654435761u;
            const int len = 6 + (int)((h >> 8) % 7);
            int x = (int)((h >> 12) % (uint32_t)span) + (int)(_clock * speed) * _dir;
            x = ((x % span) + span) % span - 20;
            const int y = ArcadeConfig::UI_MARGIN_TOP + 4 + (int)((h >> 20) % 96);
            canvas.drawFastHLine(x, y, len, c);
        }
        if (!blow && ((_clock / 4) & 1)) return;
        const int y = ArcadeConfig::UI_MARGIN_TOP + 4, cx = W / 2;
        for (int k = -1; k <= 1; k++) {
            const int ax = cx + k * 7;
            for (int d = 0; d < 3; d++) {   // a chevron, its point the gust's way
                canvas.drawPixel(ax + _dir * d, y + d, ArcadeConfig::COLOR_WHITE);
                canvas.drawPixel(ax + _dir * d, y + 4 - d, ArcadeConfig::COLOR_WHITE);
            }
        }
    }

private:
    long _clock = 0;
    long _tellAt = NONE;     // the frame the next (or current) gust's warning starts
    long _told = NONE;       // the gust whose sound has played
    long _quietUntil = 0;    // no gust chosen before this (one wouldn't fit)
    int  _dir = 1;
    int  _blow = ArcadeConfig::STORM_BLOW;

    long blowAt() const { return _tellAt + ArcadeConfig::STORM_TELL; }
    long endAt() const  { return blowAt() + _blow; }
};

#endif // STORM_LAYER_H
