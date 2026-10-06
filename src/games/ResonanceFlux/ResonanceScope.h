#ifndef RESONANCE_SCOPE_H
#define RESONANCE_SCOPE_H

// Included from ResonanceFluxGame.h. The scope's phosphor afterglow and
// everything drawn on the screen.
//
// The afterglow is two 8-bit intensity planes over the scope (y 12-117),
// green for you and amber for the signals. Each frame every pixel fades
// (a half-life of GLOW_HALF_MS, whatever the frame rate), the figures are
// drawn in at full brightness, and the planes are mapped to colours
// through a 32x32 table: green, amber, and white where both are bright
// (resonance). Fast moves leave trails; still figures burn sharp. The HUD
// and tuning strip are drawn straight onto the canvas above and below.

namespace resonance {

constexpr int PLANE = W * SCOPE_H;

// Internal RAM if there's room (every byte is read and written each
// frame), PSRAM otherwise.
inline bool ResonanceFluxGame::allocGlow() {
    if (_glow) return true;
    _glow = (uint8_t *)heap_caps_malloc(2 * PLANE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_glow) _glow = (uint8_t *)heap_caps_malloc(2 * PLANE, MALLOC_CAP_8BIT);
    if (_glow) memset(_glow, 0, 2 * PLANE);
    return _glow != nullptr;
}

inline void ResonanceFluxGame::freeGlow() {
    if (_glow) heap_caps_free(_glow);
    _glow = nullptr;
}

inline void ResonanceFluxGame::buildLut() {
    for (int g = 0; g < 32; ++g)
        for (int a = 0; a < 32; ++a) {
            const int r5 = a;
            int g6 = 2 * g;
            const int ga = a * 2 * 65 / 100;
            if (ga > g6) g6 = ga;
            const int b5 = g < a ? g : a;
            _lut[(g << 5) | a] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
}

inline void ResonanceFluxGame::fadeGlow() {
    const int q = (int)(256.0f * powf(0.5f, _dt * 1000.0f / GLOW_HALF_MS));
    for (int i = 0; i < 2 * PLANE; ++i) _glow[i] = (uint8_t)((_glow[i] * q) >> 8);
}

// Screen coordinates; the brighter of what's there and v.
inline void ResonanceFluxGame::plot(uint8_t *plane, int x, int y, uint8_t v) {
    y -= SCOPE_Y;
    if (x < 0 || x >= W || y < 0 || y >= SCOPE_H) return;
    uint8_t &p = plane[y * W + x];
    if (v > p) p = v;
}

inline void ResonanceFluxGame::line(uint8_t *plane, int x0, int y0, int x1, int y1, uint8_t v) {
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(plane, x0, y0, v);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// A figure into one plane, or both (b, if given: white).
// v: how bright (the Chord's dimmer layers).
inline void ResonanceFluxGame::drawFigure(uint8_t *a, uint8_t *b, const Ratio &r, float phase,
                                          float cx, float cy, float rad, bool big, uint8_t v) {
    const int n = segmentsFor(r, big);
    int px = 0, py = 0;
    for (int i = 0; i <= n; ++i) {
        float fx, fy;
        figurePoint(r, phase, TWO_PI_F * i / n, fx, fy);
        const int x = (int)lrintf(cx + fx * rad), y = (int)lrintf(cy - fy * rad);
        if (i > 0) {
            line(a, px, py, x, y, v);
            if (b) line(b, px, py, x, y, v);
        }
        px = x;
        py = y;
    }
}

inline void ResonanceFluxGame::pushGlow(GFXcanvas16 &cv) {
    uint16_t *dst = cv.getBuffer() + SCOPE_Y * W;
    const uint8_t *g = _glow, *a = _glow + PLANE;
    for (int i = 0; i < PLANE; ++i) dst[i] = _lut[((g[i] >> 3) << 5) | (a[i] >> 3)];
}

// The scope in play: fade, draw in, push, then the dim parts that don't
// glow (graticule, the core's ring, the focus marker) where nothing's lit.
inline void ResonanceFluxGame::renderPlay(GFXcanvas16 &cv) {
    uint8_t *green = _glow, *amber = _glow + PLANE;
    fadeGlow();

    // Static: noise on the scope, more the worse it gets.
    const int noise = (int)(_static * 2.5f);
    for (int i = 0; i < noise; ++i)
        plot(green, (int)random(0, W), SCOPE_Y + (int)random(0, SCOPE_H), (uint8_t)random(60, 200));

    const bool playing = inPlay();
    for (int i = 0; i < MAX_SIGNALS; ++i) {
        const Signal &s = _signals[i];
        if (!s.alive) continue;
        drawFigure(amber, i == _matched && playing ? green : nullptr, RATIOS[s.ratio], s.phase, s.x, s.y, SIG_R, false);
    }
    if (_chord.active && playing) drawChord(green, amber);
    if (playing) {
        const bool glow = _matched >= 0 || (_chordMatched >= 0 && _chordMatched == _chord.stripped);
        drawFigure(green, glow ? amber : nullptr, yourRatio(), phaseEff(),
                   CORE_X, CORE_Y, CORE_R, true);
    }
    if (_now < _beamUntil) {
        line(green, (int)CORE_X, (int)CORE_Y, (int)_beamX, (int)_beamY, 255);
        line(amber, (int)CORE_X, (int)CORE_Y, (int)_beamX, (int)_beamY, 255);
    }
    for (const auto &sh : _shards) {
        if (sh.until <= _now) continue;
        plot(amber, (int)sh.x, (int)sh.y, 255);
        if (sh.white) plot(green, (int)sh.x, (int)sh.y, 255);
    }
    pushGlow(cv);
    drawOverlays(cv);
    drawHud(cv);
    drawStrip(cv);
}

// The Chord's layers still there, the brightest at full and the others
// faint, so the one to strip reads. In resonance, the brightest glows
// white; a dimmer one shows green instead (a warning: firing at it
// brings the stripped layers back).
inline void ResonanceFluxGame::drawChord(uint8_t *green, uint8_t *amber) {
    static const uint8_t bright[CHORD_LAYERS] = { 255, 56, 28 };
    for (int i = CHORD_LAYERS - 1; i >= _chord.stripped; --i) {
        const Ratio &r = RATIOS[_chord.ratio[i]];
        if (i == _chordMatched && i != _chord.stripped) {
            drawFigure(green, nullptr, r, _chord.phase[i], chordX(), CORE_Y, CHORD_R, true, 160);
            continue;
        }
        drawFigure(amber, i == _chordMatched ? green : nullptr, r, _chord.phase[i],
                   chordX(), CORE_Y, CHORD_R, true, bright[i - _chord.stripped]);
    }
}

inline void ResonanceFluxGame::drawOverlays(GFXcanvas16 &cv) {
    uint16_t *buf = cv.getBuffer();
    auto dim = [&](int x, int y, uint16_t c) {
        if (x < 0 || x >= W || y < SCOPE_Y || y >= SCOPE_Y + SCOPE_H) return;
        uint16_t &p = buf[y * W + x];
        if (p == 0) p = c;
    };
    const uint16_t grid = 0x0120, ring = 0x01E0;
    // Graticule: a dot every 4px along lines every 16px from the centre.
    const int cx = (int)CORE_X, cy = (int)CORE_Y;
    for (int y = cy % 16; y < SCOPE_Y + SCOPE_H; y += 16)
        for (int x = 0; x < W; x += 4) dim(x, y, grid);
    for (int x = cx % 16; x < W; x += 16)
        for (int y = SCOPE_Y; y < SCOPE_Y + SCOPE_H; y += 4) dim(x, y, grid);
    // The core's ring: red for a moment when a signal hits it, cyan while
    // dampen has things slowed.
    const uint16_t ringC = _now < _hitFlashUntil ? ArcadeConfig::COLOR_RED : _now < _dampUntil ? 0x0410 : ring;
    for (int i = 0; i < 64; ++i) {
        const float a = TWO_PI_F * i / 64;
        dim(cx + (int)lrintf(cosf(a) * (CORE_R + SIG_R * 0.5f)), cy + (int)lrintf(sinf(a) * (CORE_R + SIG_R * 0.5f)), ringC);
    }
    // Corner marks: grey round the signal on your stop nearest your phase
    // (white in resonance); dim round the one nearest the core when none
    // shares your stop (its tone is the one you hear).
    // The Chord: a red ring round it, a pip above for each layer left, and
    // the corner marks when a layer of it is on your stop and no signal is.
    if (inPlay() && _chord.active) {
        const int hx = (int)chordX(), hy = (int)CORE_Y, rr = (int)CHORD_R + 4;
        for (int i = 0; i < 72; ++i) {
            const float a = TWO_PI_F * i / 72;
            dim(hx + (int)lrintf(cosf(a) * rr), hy + (int)lrintf(sinf(a) * rr), 0x6000);
        }
        for (int i = 0; i < CHORD_LAYERS - _chord.stripped; ++i)
            for (int k = 0; k < 3; ++k) dim(hx - 5 + i * 5 + k, hy - rr - 3, ArcadeConfig::COLOR_RED);
        if (_focus < 0 && _chordFocus >= 0) {
            const uint16_t c = _chordMatched >= 0 && _chordMatched == _chord.stripped ? ArcadeConfig::COLOR_WHITE
                             : ArcadeConfig::COLOR_GREY;
            const int x0 = hx - rr, y0 = hy - rr, x1 = hx + rr, y1 = hy + rr;
            for (int k = 0; k < 5; ++k) {
                dim(x0 + k, y0, c); dim(x0, y0 + k, c);
                dim(x1 - k, y0, c); dim(x1, y0 + k, c);
                dim(x0 + k, y1, c); dim(x0, y1 - k, c);
                dim(x1 - k, y1, c); dim(x1, y1 - k, c);
            }
        }
    }
    const int mark = _focus >= 0 ? _focus : _threat;
    if (inPlay() && mark >= 0) {
        const Signal &s = _signals[mark];
        const int x0 = (int)s.x - 11, y0 = (int)s.y - 11, x1 = (int)s.x + 11, y1 = (int)s.y + 11;
        const uint16_t c = _matched >= 0 ? ArcadeConfig::COLOR_WHITE
                         : _focus >= 0  ? ArcadeConfig::COLOR_GREY : 0x4208;   // dim: not on your stop
        for (int k = 0; k < 4; ++k) {
            dim(x0 + k, y0, c); dim(x0, y0 + k, c);
            dim(x1 - k, y0, c); dim(x1, y0 + k, c);
            dim(x0 + k, y1, c); dim(x0, y1 - k, c);
            dim(x1 - k, y1, c); dim(x1, y1 - k, c);
        }
    }
}

// Score, wave and dampen charges across the top; the static meter under
// them; the wave's banner and tally over the scope.
inline void ResonanceFluxGame::drawHud(GFXcanvas16 &cv) {
    cv.fillRect(0, 0, W, SCOPE_Y, ArcadeConfig::COLOR_BLACK);
    cv.setFont();
    cv.setTextSize(1);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%ld", _test ? "T " : "", _score);   // T: a test run
    cv.setTextColor(ArcadeConfig::COLOR_GREEN);
    cv.setCursor(1, 1);
    cv.print(buf);
    snprintf(buf, sizeof(buf), "W%d", _wave);
    hiscore::printCentred(cv, buf, 1, ArcadeConfig::COLOR_GREEN);
    const float mult = scoreMult();
    if (fabsf(mult - 1.0f) > 0.001f) {       // the options' multiplier, when it isn't 1
        snprintf(buf, sizeof(buf), "x%.2f", mult);
        cv.setTextColor(ArcadeConfig::COLOR_YELLOW);
        cv.setCursor(104, 1);
        cv.print(buf);
    }
    // Dampen: a block for each charge left; the one just spent drains away
    // while its slowdown lasts.
    for (int i = 0; i < _dampens; ++i) cv.fillRect(W - 6 - i * 6, 2, 4, 5, ArcadeConfig::COLOR_CYAN);
    if (_now < _dampUntil) {
        const int h = (int)((_dampUntil - _now) * 5 + DAMPEN_MS - 1) / (int)DAMPEN_MS;   // 5..1, rounded up
        cv.fillRect(W - 6 - _dampens * 6, 7 - h, 4, h, ArcadeConfig::COLOR_CYAN);
    }

    const int len = (int)(_static * W / 100.0f);
    const uint16_t c = _static < 50 ? ArcadeConfig::COLOR_GREEN : _static < 80 ? ArcadeConfig::COLOR_AMBER
                                                                                 : ArcadeConfig::COLOR_RED;
    cv.fillRect(0, METER_Y, len, 2, c);
    cv.fillRect(len, METER_Y, W - len, 2, 0x2104);

    if (!inPlay()) return;
    if (_round == ROUND_INTRO) {
        snprintf(buf, sizeof(buf), _chord.active ? "WAVE %d: CHORD" : "WAVE %d", _wave);
        hiscore::printCentred(cv, buf, SCOPE_Y + 6, _chord.active ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_WHITE);
    } else if (_round == ROUND_CLEAR) {
        hiscore::printCentred(cv, "CLEAR", SCOPE_Y + 6, ArcadeConfig::COLOR_WHITE);
        snprintf(buf, sizeof(buf), "+%ld", _clearBonus);
        hiscore::printCentred(cv, buf, SCOPE_Y + SCOPE_H - 12, ArcadeConfig::COLOR_WHITE);
    }
}

// Where a stop sits on the tuning strip.
inline int ResonanceFluxGame::dialX(int stop) const {
    const int x0 = 34, x1 = W - 4;
    const int span = _stopCount > 1 ? _stopCount - 1 : 1;
    return x0 + stop * (x1 - x0) / span;
}

// The tuning strip: the dial with a tick at each stop (the ratios this
// game has reached, evenly spaced), your needle, and the ratio you're on.
// The stop of the signal nearest the core is lit amber, with a mark under
// it, so you can step straight there before working out its shape.
inline void ResonanceFluxGame::drawStrip(GFXcanvas16 &cv) {
    cv.fillRect(0, STRIP_Y, W, H - STRIP_Y, ArcadeConfig::COLOR_BLACK);
    const int base = STRIP_Y + 7;
    cv.drawFastHLine(dialX(0), base, dialX(_stopCount - 1) - dialX(0) + 1, 0x2104);
    for (int i = 0; i < _stopCount; ++i) cv.drawFastVLine(dialX(i), base - 3, 3, ArcadeConfig::COLOR_GREEN);
    if (_opt.hint && inPlay() && _threat >= 0) {
        const int x = dialX(_stopOf[_signals[_threat].ratio]);
        cv.drawFastVLine(x, base - 5, 5, ArcadeConfig::COLOR_AMBER);
        cv.fillRect(x - 1, base + 1, 3, 2, ArcadeConfig::COLOR_AMBER);
    }
    // The Chord's brightest layer's stop: a red mark above the line.
    if (_opt.hint && inPlay() && _chord.active) {
        const int x = dialX(_stopOf[_chord.ratio[_chord.stripped]]);
        cv.fillRect(x - 1, STRIP_Y, 3, 2, ArcadeConfig::COLOR_RED);
    }
    cv.drawFastVLine(dialX(_stop), STRIP_Y + 1, 8, ArcadeConfig::COLOR_WHITE);

    const Ratio &r = yourRatio();
    char buf[24];
    snprintf(buf, sizeof(buf), "%d:%d", r.a, r.b);
    cv.setFont();
    cv.setTextSize(1);
    cv.setTextColor(ArcadeConfig::COLOR_GREEN);
    cv.setCursor(1, STRIP_Y + 1);
    cv.print(buf);

    // Tuning aid for the board, in the scope's bottom corner: the phase gap
    // to the signal on your stop.
    if (_opt.debug && _focus >= 0 && _phaseState == PHASE_PLAYING && !_demo) {
        snprintf(buf, sizeof(buf), "phase %.2f / %.2f", _focusPhaseGap, PHASE_TOL);
        cv.setTextColor(0x8410);
        cv.setCursor(1, SCOPE_Y + SCOPE_H - 8);
        cv.print(buf);
    }
}

// The title: a figure turning, stepping through the ratios behind the name.
inline void ResonanceFluxGame::renderTitle(GFXcanvas16 &cv) {
    uint8_t *green = _glow, *amber = _glow + PLANE;
    fadeGlow();
    _titleAt += 0.25f * _dt;                 // a new ratio every 4s
    if (_titleAt >= RATIO_COUNT) _titleAt = 0;
    const Ratio &r = RATIOS[(int)_titleAt];
    _titlePhase += 0.6f * _dt;
    drawFigure(green, nullptr, r, _titlePhase, CORE_X, CORE_Y + 4, 30, true);
    drawFigure(amber, nullptr, r, _titlePhase + 1.3f, CORE_X - 52, CORE_Y + 22, 9, false);
    drawFigure(amber, nullptr, r, -_titlePhase, CORE_X + 52, CORE_Y + 22, 9, false);
    cv.fillRect(0, 0, W, SCOPE_Y, ArcadeConfig::COLOR_BLACK);
    cv.fillRect(0, STRIP_Y, W, H - STRIP_Y, ArcadeConfig::COLOR_BLACK);
    pushGlow(cv);
    cv.setFont();
    hiscore::printCentred(cv, "RESONANCE", 16, ArcadeConfig::COLOR_WHITE, 2);
    hiscore::printCentred(cv, "FLUX", 34, ArcadeConfig::COLOR_GREEN);
    char buf[24];
    hiscore::printCentred(cv, _scores.bestLine(buf, sizeof(buf)), 2, ArcadeConfig::COLOR_AMBER);
    hiscore::printCentred(cv, "STICK TUNE  A FIRE  B DAMP", STRIP_Y - 2, ArcadeConfig::COLOR_GREY);
    hiscore::printCentred(cv, (_now / 1500) % 2 ? "B: OPTIONS" : "PRESS A", 104,
                          (_now / 1500) % 2 ? ArcadeConfig::COLOR_CYAN : ArcadeConfig::COLOR_WHITE);
}

// A figure into the planes: green (yours) or amber (a signal's), or both
// (white: resonance).
inline void ResonanceFluxGame::figureFor(const Ratio &r, float phase, float cx, float cy, float rad,
                                         bool amber, bool white) {
    uint8_t *green = _glow, *amb = _glow + PLANE;
    uint8_t *a = amber ? amb : green, *b = white ? (amber ? green : amb) : nullptr;
    drawFigure(a, b, r, phase, cx, cy, rad, rad > 12);
}

// The how-to slides, each a small scene on the scope with a few lines:
// 0 tuning (a figure stepping round the ratios, the dial under it),
// 1 matching (a signal and your figure turning into resonance), 2 static
// (a signal reaching you, the scope filling with noise).
inline void ResonanceFluxGame::renderInfo(GFXcanvas16 &cv, int page) {
    // The scope is redrawn whole; the bands above and below it must be
    // cleared too, or the last screen's lines show under these.
    cv.fillRect(0, 0, W, SCOPE_Y, ArcadeConfig::COLOR_BLACK);
    cv.fillRect(0, STRIP_Y, W, H - STRIP_Y, ArcadeConfig::COLOR_BLACK);
    fadeGlow();
    const unsigned long t = _now - _slideAt;
    char line[32];
    if (page == 0) {
        const int k = (int)(t / 1200) % RATIO_COUNT;
        const Ratio &r = RATIOS[k];
        figureFor(r, 0.5f + t * 0.0006f, CORE_X, 48, 20, false, false);
        pushGlow(cv);
        const int x0 = 34, x1 = W - 34;
        cv.drawFastHLine(x0, 80, x1 - x0 + 1, 0x2104);
        for (int i = 0; i < RATIO_COUNT; ++i)
            cv.drawFastVLine(x0 + i * (x1 - x0) / (RATIO_COUNT - 1), 77, 3, ArcadeConfig::COLOR_GREEN);
        cv.drawFastVLine(x0 + k * (x1 - x0) / (RATIO_COUNT - 1), 74, 8, ArcadeConfig::COLOR_WHITE);
        snprintf(line, sizeof(line), "%d:%d", r.a, r.b);
        hiscore::printCentred(cv, line, 84, ArcadeConfig::COLOR_GREEN);
        hiscore::printCentred(cv, "TUNE YOUR FIGURE", 2, ArcadeConfig::COLOR_WHITE);
        hiscore::printCentred(cv, "LEFT/RIGHT: RATIO", 96, ArcadeConfig::COLOR_WHITE);
        hiscore::printCentred(cv, "UP/DOWN: PHASE", 106, ArcadeConfig::COLOR_WHITE);
        hiscore::printCentred(cv, "THE DIAL GOES ROUND", 119, ArcadeConfig::COLOR_GREY);
    } else if (page == 1) {
        // Every 3s: 2s turning towards the signal's phase, 1s in resonance.
        const Ratio &r = RATIOS[4];                     // 2:3
        const float u = (t % 3000) / 2000.0f;
        const bool on = u >= 1.0f;
        const float off = on ? 0 : 1.6f * (1.0f - u);
        figureFor(r, 1.0f, 36, 48, 10, true, on);
        figureFor(r, 1.0f + off, 104, 48, 18, false, on);
        pushGlow(cv);
        hiscore::printCentred(cv, "MATCH ITS SHAPE", 2, ArcadeConfig::COLOR_WHITE);
        hiscore::printCentred(cv, "SAME RATIO, SAME TWIST:", 80, ArcadeConfig::COLOR_WHITE);
        hiscore::printCentred(cv, "BOTH GLOW WHITE", 90, on ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_GREY);
        hiscore::printCentred(cv, "A FIRES", 102, ArcadeConfig::COLOR_GREEN);
        hiscore::printCentred(cv, "WRONG SHAPE ADDS STATIC", 119, ArcadeConfig::COLOR_GREY);
    } else {
        // A signal coming in every 2.5s, the ring flashing, noise rising.
        const float u = (t % 2500) / 2500.0f;
        const float level = (float)(t % 6000) / 6000.0f;
        for (int i = 0; i < (int)(level * 250); ++i)
            plot(_glow, (int)random(0, W), SCOPE_Y + (int)random(0, SCOPE_H), (uint8_t)random(60, 200));
        figureFor(RATIOS[2], 0.4f, 14 + u * (CORE_X - 14 - 22), 48, 10, true, false);
        figureFor(RATIOS[7], 1.2f, CORE_X, 48, 14, false, false);
        pushGlow(cv);
        const uint16_t ring = u > 0.9f ? ArcadeConfig::COLOR_RED : 0x01E0;
        cv.drawCircle((int)CORE_X, 48, 19, ring);
        hiscore::printCentred(cv, "KEEP THEM OFF", 2, ArcadeConfig::COLOR_WHITE);
        cv.fillRect(0, METER_Y, (int)(level * W), 2, level < 0.5f ? ArcadeConfig::COLOR_GREEN
                                                         : level < 0.8f ? ArcadeConfig::COLOR_AMBER : ArcadeConfig::COLOR_RED);
        hiscore::printCentred(cv, "SIGNALS THAT REACH YOU", 80, ArcadeConfig::COLOR_WHITE);
        hiscore::printCentred(cv, "ADD STATIC. 100: OVERLOAD", 90, ArcadeConfig::COLOR_WHITE);
        hiscore::printCentred(cv, "B SLOWS THEM, 2 A WAVE", 102, ArcadeConfig::COLOR_CYAN);
        hiscore::printCentred(cv, "A TO PLAY", 119, ArcadeConfig::COLOR_GREY);
    }
}

inline void ResonanceFluxGame::renderScores(GFXcanvas16 &cv) {
    cv.fillScreen(ArcadeConfig::COLOR_BLACK);
    hiscore::drawTable(cv, _scores.table(), "HIGH SCORES", 16);
    if (_now % 1000 < 600) hiscore::printCentred(cv, "PRESS A TO START", 108, ArcadeConfig::COLOR_WHITE);
}

// The wave select: the wave, its ratios as they'll come (one figure per
// stop), how many signals and how fast.
inline void ResonanceFluxGame::renderPicker(GFXcanvas16 &cv) {
    fadeGlow();
    int stops[RATIO_COUNT], n = 0;
    for (int i = 0; i < RATIO_COUNT; ++i) if (RATIOS[i].firstWave <= _pick) stops[n++] = i;
    const float gap = W / (float)n, rad = gap * 0.4f < 9 ? gap * 0.4f : 9;
    for (int i = 0; i < n; ++i)
        figureFor(RATIOS[stops[i]], 0.6f, gap * (i + 0.5f), 56, rad, true, false);
    pushGlow(cv);
    cv.fillRect(0, 0, W, SCOPE_Y, ArcadeConfig::COLOR_BLACK);
    cv.fillRect(0, STRIP_Y, W, H - STRIP_Y, ArcadeConfig::COLOR_BLACK);
    char line[32];
    hiscore::printCentred(cv, "WAVE SELECT (TEST)", 2, ArcadeConfig::COLOR_ORANGE);
    snprintf(line, sizeof(line), "WAVE %d", _pick);
    hiscore::printCentred(cv, line, 18, ArcadeConfig::COLOR_CYAN, 2);
    cv.setFont();
    cv.setTextSize(1);
    cv.setTextColor(ArcadeConfig::COLOR_GREEN);
    for (int i = 0; i < n; ++i) {
        snprintf(line, sizeof(line), "%d:%d", RATIOS[stops[i]].a, RATIOS[stops[i]].b);
        cv.setCursor((int)(gap * (i + 0.5f)) - 9, 70);
        cv.print(line);
    }
    const int count = 6 + 2 * _pick > 30 ? 30 : 6 + 2 * _pick;
    float speed = DRIFT_START * powf(DRIFT_GROWTH, (float)(_pick - 1));
    if (speed > DRIFT_MAX) speed = DRIFT_MAX;
    if (isChordWave(_pick)) {
        snprintf(line, sizeof(line), "CHORD%s%s", _pick >= CHORD_DRIFT_WAVE ? ", DRIFTING" : "",
                 _pick >= CHORD_MORPH_WAVE ? ", MORPHING" : "");
        hiscore::printCentred(cv, line, 84, ArcadeConfig::COLOR_RED);
    } else {
        snprintf(line, sizeof(line), "%d SIGNALS, %.0f PX/S", count, speed * paceSpeed());
        hiscore::printCentred(cv, line, 84, ArcadeConfig::COLOR_WHITE);
    }
    hiscore::printCentred(cv, "<> WAVE  ^v BY 5", 100, ArcadeConfig::COLOR_WHITE);
    hiscore::printCentred(cv, "A: TEST  B: BACK", 112, ArcadeConfig::COLOR_YELLOW);
}

// The options screen: one line each, the chosen one marked; bosses greyed
// until there are bosses; the score multiplier the settings give.
inline void ResonanceFluxGame::renderOptions(GFXcanvas16 &cv) {
    cv.fillScreen(ArcadeConfig::COLOR_BLACK);
    hiscore::printCentred(cv, "OPTIONS", 4, ArcadeConfig::COLOR_ORANGE, 2);
    static const char *const names[OPT_COUNT] = { "RATIO HINT", "NOTES", "PACE", "BOSSES", "DEBUG LINE" };
    static const char *const paces[] = { "CALM", "NORMAL", "FAST" };
    cv.setFont();
    cv.setTextSize(1);
    for (int i = 0; i < OPT_COUNT; ++i) {
        const int y = 28 + i * 13;
        const char *value = i == OPT_HINT ? (_opt.hint ? "ON" : "OFF")
                          : i == OPT_NOTES ? (_opt.notes ? "ON" : "OFF")
                          : i == OPT_PACE ? paces[_opt.pace]
                          : i == OPT_BOSSES ? (_opt.bosses ? "ON" : "OFF")
                          : (_opt.debug ? "ON" : "OFF");
        const bool grey = false;
        const uint16_t c = grey ? ArcadeConfig::COLOR_GREY : i == _optRow ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_GREEN;
        cv.setTextColor(i == _optRow ? ArcadeConfig::COLOR_CYAN : ArcadeConfig::COLOR_BLACK);
        cv.setCursor(8, y);
        cv.print(">");
        cv.setTextColor(c);
        cv.setCursor(18, y);
        cv.print(names[i]);
        cv.setCursor(W - 8 - 6 * (int)strlen(value), y);
        cv.print(value);
    }
    char line[24];
    snprintf(line, sizeof(line), "SCORE x%.2f", scoreMult());
    hiscore::printCentred(cv, line, 96, ArcadeConfig::COLOR_YELLOW);
    hiscore::printCentred(cv, "^v PICK  <> CHANGE", 108, ArcadeConfig::COLOR_WHITE);
    hiscore::printCentred(cv, "B: BACK", 118, ArcadeConfig::COLOR_GREY);
}

inline void ResonanceFluxGame::drawDemoOverlay(GFXcanvas16 &cv) {
    cv.setFont();
    cv.setTextSize(1);
    cv.setTextColor(ArcadeConfig::COLOR_WHITE);
    cv.setCursor(2, SCOPE_Y + 2);
    cv.print("DEMO");
    if ((_now / 500) % 2 == 0) hiscore::printCentred(cv, "PRESS A TO PLAY", SCOPE_Y + SCOPE_H - 10, ArcadeConfig::COLOR_CYAN);
}

inline void ResonanceFluxGame::renderGameOver(GFXcanvas16 &cv) {
    renderPlay(cv);
    cv.fillRect(20, 42, W - 40, 44, ArcadeConfig::COLOR_BLACK);
    cv.drawRect(20, 42, W - 40, 44, ArcadeConfig::COLOR_RED);
    hiscore::printCentred(cv, "OVERLOAD", 48, ArcadeConfig::COLOR_RED, 2);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE %ld  W%d", _score, _wave);
    hiscore::printCentred(cv, buf, 68, ArcadeConfig::COLOR_WHITE);
    if (_now - _phaseAt > GAMEOVER_MIN_MS) hiscore::printCentred(cv, "A AGAIN", 77, ArcadeConfig::COLOR_GREY);
}

}  // namespace resonance

#endif  // RESONANCE_SCOPE_H
