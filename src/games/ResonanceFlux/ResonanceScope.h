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
inline void ResonanceFluxGame::drawFigure(uint8_t *a, uint8_t *b, const Ratio &r, float phase,
                                          float cx, float cy, float rad, bool big) {
    const int n = segmentsFor(r, big);
    int px = 0, py = 0;
    for (int i = 0; i <= n; ++i) {
        float fx, fy;
        figurePoint(r, phase, TWO_PI_F * i / n, fx, fy);
        const int x = (int)lrintf(cx + fx * rad), y = (int)lrintf(cy - fy * rad);
        if (i > 0) {
            line(a, px, py, x, y, 255);
            if (b) line(b, px, py, x, y, 255);
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

    const bool playing = _phaseState == PHASE_PLAYING;
    for (int i = 0; i < MAX_SIGNALS; ++i) {
        const Signal &s = _signals[i];
        if (!s.alive) continue;
        drawFigure(amber, i == _matched && playing ? green : nullptr, RATIOS[s.ratio], s.phase, s.x, s.y, SIG_R, false);
    }
    if (playing) {
        const float d = dialEff();
        drawFigure(green, _matched >= 0 ? amber : nullptr, RATIOS[nearestRatio(d)], phaseEff(),
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
    // The core's ring, red for a moment when a signal hits it.
    const uint16_t ringC = _now < _hitFlashUntil ? ArcadeConfig::COLOR_RED : ring;
    for (int i = 0; i < 64; ++i) {
        const float a = TWO_PI_F * i / 64;
        dim(cx + (int)lrintf(cosf(a) * (CORE_R + SIG_R * 0.5f)), cy + (int)lrintf(sinf(a) * (CORE_R + SIG_R * 0.5f)), ringC);
    }
    // The focus: corner marks round the signal you're tuned nearest.
    if (_phaseState == PHASE_PLAYING && _focus >= 0) {
        const Signal &s = _signals[_focus];
        const int x0 = (int)s.x - 11, y0 = (int)s.y - 11, x1 = (int)s.x + 11, y1 = (int)s.y + 11;
        const uint16_t c = _matched >= 0 ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_GREY;
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
    snprintf(buf, sizeof(buf), "%ld", _score);
    cv.setTextColor(ArcadeConfig::COLOR_GREEN);
    cv.setCursor(1, 1);
    cv.print(buf);
    snprintf(buf, sizeof(buf), "W%d", _wave);
    hiscore::printCentred(cv, buf, 1, ArcadeConfig::COLOR_GREEN);
    for (int i = 0; i < _dampens; ++i) cv.fillRect(W - 6 - i * 6, 2, 4, 5, ArcadeConfig::COLOR_CYAN);
    if (_now < _dampUntil) cv.drawRect(W - 6 - DAMPEN_PER_WAVE * 6, 1, DAMPEN_PER_WAVE * 6 + 2, 7, ArcadeConfig::COLOR_CYAN);

    const int len = (int)(_static * W / 100.0f);
    const uint16_t c = _static < 50 ? ArcadeConfig::COLOR_GREEN : _static < 80 ? ArcadeConfig::COLOR_AMBER
                                                                                 : ArcadeConfig::COLOR_RED;
    cv.fillRect(0, METER_Y, len, 2, c);
    cv.fillRect(len, METER_Y, W - len, 2, 0x2104);

    if (_phaseState != PHASE_PLAYING) return;
    if (_round == ROUND_INTRO) {
        snprintf(buf, sizeof(buf), "WAVE %d", _wave);
        hiscore::printCentred(cv, buf, SCOPE_Y + 6, ArcadeConfig::COLOR_WHITE);
    } else if (_round == ROUND_CLEAR) {
        hiscore::printCentred(cv, "CLEAR", SCOPE_Y + 6, ArcadeConfig::COLOR_WHITE);
        snprintf(buf, sizeof(buf), "+%ld", _clearBonus);
        hiscore::printCentred(cv, buf, SCOPE_Y + SCOPE_H - 12, ArcadeConfig::COLOR_WHITE);
    }
}

// The tuning strip: the dial with a tick at each clean ratio (bright once
// this wave can send it), your needle, and the ratio you're on.
inline void ResonanceFluxGame::drawStrip(GFXcanvas16 &cv) {
    cv.fillRect(0, STRIP_Y, W, H - STRIP_Y, ArcadeConfig::COLOR_BLACK);
    const int x0 = 34, x1 = W - 4;
    auto dialX = [&](float d) { return x0 + (int)lrintf((d - DIAL_MIN) / (DIAL_MAX - DIAL_MIN) * (x1 - x0)); };
    const int base = STRIP_Y + 7;
    cv.drawFastHLine(x0, base, x1 - x0 + 1, 0x2104);
    for (int i = 0; i < RATIO_COUNT; ++i)
        cv.drawFastVLine(dialX(RATIOS[i].d), base - 3, 3, RATIOS[i].firstWave <= _wave ? ArcadeConfig::COLOR_GREEN : 0x2104);
    const float d = dialEff();
    const int nx = dialX(d);
    cv.drawFastVLine(nx, STRIP_Y + 1, 8, ArcadeConfig::COLOR_WHITE);

    const Ratio &r = RATIOS[nearestRatio(d)];
    char buf[24];
    const bool on = fabsf(d - r.d) < RATIO_TOL;
    snprintf(buf, sizeof(buf), "%s%d:%d", on ? "" : "~", r.a, r.b);
    cv.setFont();
    cv.setTextSize(1);
    cv.setTextColor(on ? ArcadeConfig::COLOR_GREEN : ArcadeConfig::COLOR_GREY);
    cv.setCursor(1, STRIP_Y + 1);
    cv.print(buf);

    // Tuning aid for the board, in the scope's bottom corner: the focus's
    // dial and phase gaps, and the beat they make.
    if (DEBUG_LINE && _focus >= 0 && _phaseState == PHASE_PLAYING) {
        snprintf(buf, sizeof(buf), "%.3f %.2f %.0fHz", _focusRatioGap, _focusPhaseGap, _focusRatioGap * HUM_F_SPAN);
        cv.setTextColor(0x8410);
        cv.setCursor(1, SCOPE_Y + SCOPE_H - 8);
        cv.print(buf);
    }
}

// The title: a figure morphing slowly through the ratios behind the name.
inline void ResonanceFluxGame::renderTitle(GFXcanvas16 &cv) {
    uint8_t *green = _glow, *amber = _glow + PLANE;
    fadeGlow();
    _titleDial += 0.03f * _dt;
    if (_titleDial > DIAL_MAX) _titleDial = DIAL_MIN;
    const Ratio &r = RATIOS[nearestRatio(_titleDial)];
    _titlePhase += (0.6f + TWO_PI_F * (_titleDial - r.d) * ROLL_HZ) * _dt;
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
    if ((_now / 500) % 2) hiscore::printCentred(cv, "PRESS A", 104, ArcadeConfig::COLOR_WHITE);
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
