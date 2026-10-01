#ifndef BRICK_RENDER_H
#define BRICK_RENDER_H

// =============================================================================
// BRICK FLUX — drawing: the field, bat, HUD, overlays and the attract
// screens. Included from BrickFluxGame.h. Everything is flat rectangles and
// lines into the canvas; nothing per-pixel beyond the backdrop's dots.
// =============================================================================

namespace brickflux {

inline void BrickFluxGame::panel(GFXcanvas16 &cv, int x, int y, int w, int h) {
    cv.fillRect(x, y, w, h, COL_PANEL);
    cv.drawRect(x, y, w, h, ArcadeConfig::COLOR_ION_BLUE);
}

inline void BrickFluxGame::renderPlay(GFXcanvas16 &cv) {
    drawField(cv);
    if (_round != ROUND_CLEAR && !(_round == ROUND_LOST && _lives <= 0)) drawBat(cv);
    drawHud(cv);
    drawRoundOverlay(cv);
}

// Backdrop, walls, danger line, bricks, capsule, shots, balls, particles,
// popups. A Perfect (or the wall breaking through) shakes it for a moment.
inline void BrickFluxGame::drawField(GFXcanvas16 &cv) {
    int ox = 0, oy = 0;
    if (millis() < _shakeUntil) { ox = (int)random(-2, 3); oy = (int)random(-2, 3); }
    cv.fillScreen(ArcadeConfig::COLOR_BLACK);

    // A faint dot grid that moves down with the formation, so the creep shows.
    const int phase = ((int)_board.top() - GRID_Y0) % 8;
    for (int y = FIELD_T + phase; y < H; y += 8)
        for (int x = FIELD_L + 4; x < FIELD_R; x += 8) cv.drawPixel(x, y, COL_DOTS);

    cv.fillRect(0, HUD_H + 2, FIELD_L, H - HUD_H - 2, COL_WALL);
    cv.fillRect(FIELD_R, HUD_H + 2, W - FIELD_R, H - HUD_H - 2, COL_WALL);

    // The danger line: brighter as the formation nears it, flashing as a
    // step comes.
    const float lb = _board.lowestBottom();
    const bool near = lb > 0 && DANGER_Y - lb <= 2 * CELL_H;
    const bool flash = _warned && (millis() / 80) % 2 == 0;
    const uint16_t dc = (flash || near) ? COL_DANGER_HOT : COL_DANGER;
    for (int x = FIELD_L; x < FIELD_R; x += 3) cv.drawFastHLine(x, DANGER_Y, 2, dc);

    int rows = ROWS;
    if (_phase != PHASE_ATTRACT || _slide == SLIDE_DEMO) {
        if (_round == ROUND_INTRO) rows = 1 + (int)((millis() - _roundAt) * ROWS * 3 / (INTRO_MS * 2));
    }
    _board.draw(cv, rows, ox, oy);

    if (_capsule.active) {
        static const uint16_t CAP_COLS[CAP_COUNT] = { ArcadeConfig::COLOR_ION_BLUE, ArcadeConfig::COLOR_ORANGE,
            ArcadeConfig::COLOR_GREEN, ArcadeConfig::COLOR_RED, ArcadeConfig::COLOR_YELLOW, ArcadeConfig::COLOR_WHITE };
        static const char CAP_LETTERS[CAP_COUNT + 1] = "WMCLSF";
        const int x = (int)_capsule.x, y = (int)_capsule.y;
        cv.fillRoundRect(x, y, CAPSULE_W, CAPSULE_H, 2, CAP_COLS[_capsule.kind]);
        cv.setTextSize(1);
        cv.setTextColor(ArcadeConfig::COLOR_BLACK);
        cv.setCursor(x + 3, y + 1);
        cv.print(CAP_LETTERS[_capsule.kind]);
    }
    for (const auto &s : _shots)
        if (s.active) cv.drawFastVLine((int)s.x, (int)s.y, 3, ArcadeConfig::COLOR_ORANGE);

    for (const auto &b : _balls) {
        if (!b.active) continue;
        const int x = (int)roundf(b.x) + ox, y = (int)roundf(b.y) + oy;
        if (b.pierce) {
            // A white-hot streak back along its path.
            const uint16_t sc = b.perfect ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_YELLOW;
            const int tx = x - (int)(b.dx * 10), ty = y - (int)(b.dy * 10);
            for (int w = (b.perfect ? -1 : 0); w <= (b.perfect ? 1 : 0); ++w)
                cv.drawLine(x + w, y, tx + w, ty, sc);
            cv.fillRect(x - 2, y - 2, 5, 5, ArcadeConfig::COLOR_WHITE);
        } else {
            cv.fillRect(x - 1, y - 1, 3, 3, ArcadeConfig::COLOR_WHITE);
        }
    }

    _particles.render(cv, FIELD_T);

    cv.setTextSize(1);
    for (const auto &p : _popups) {
        if (!p.active) continue;
        const int rise = (int)((millis() - p.at) / 40);
        char buf[24];
        if (p.mult > 1) snprintf(buf, sizeof(buf), "+%ld x%d", (long)p.pts, p.mult);
        else snprintf(buf, sizeof(buf), "+%ld", (long)p.pts);
        const int x = constrain((int)p.x - (int)strlen(buf) * 3, FIELD_L, FIELD_R - (int)strlen(buf) * 6);
        cv.setTextColor(ArcadeConfig::COLOR_YELLOW);
        cv.setCursor(x, p.y - 4 - rise);
        cv.print(buf);
    }
}

// The bat: a thick line at its tilt, ends marked. Charging, it glows
// white; with the meter full, its ends pulse.
inline void BrickFluxGame::drawBat(GFXcanvas16 &cv) {
    const float half = batW() * 0.5f, t = degToRad(_tilt);
    const float cx = _batX, cy = BAT_Y + 1.0f;
    const int x0 = (int)roundf(cx - half * cosf(t)), y0 = (int)roundf(cy + half * sinf(t));
    const int x1 = (int)roundf(cx + half * cosf(t)), y1 = (int)roundf(cy - half * sinf(t));
    const unsigned long now = millis();
    uint16_t body = _effect == CAP_WIDE ? 0x9DFF : 0xBDF7;
    if (_charging) body = (now / 60) % 2 ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_YELLOW;
    for (int d = -1; d <= 1; ++d) cv.drawLine(x0, y0 + d, x1, y1 + d, body);
    uint16_t ends = ArcadeConfig::COLOR_ION_BLUE;
    if (_meter >= METER_FULL && !_charging) ends = (now / 150) % 2 ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_WHITE;
    if (_effect == CAP_LASER) ends = ArcadeConfig::COLOR_RED;
    cv.fillRect(x0 - 1, y0 - 1, 3, 3, ends);
    cv.fillRect(x1 - 1, y1 - 1, 3, 3, ends);
}

inline void BrickFluxGame::drawHud(GFXcanvas16 &cv) {
    cv.fillRect(0, 0, W, HUD_H + 2, ArcadeConfig::COLOR_BLACK);
    char buf[16];
    cv.setTextSize(1);
    cv.setTextColor(ArcadeConfig::COLOR_WHITE);
    cv.setCursor(1, 1);
    snprintf(buf, sizeof(buf), "%ld", _score);
    cv.print(buf);
    snprintf(buf, sizeof(buf), "L%d", _level);
    cv.setTextColor(ArcadeConfig::COLOR_CYAN);
    cv.setCursor(W / 2 - (int)strlen(buf) * 3, 1);
    cv.print(buf);
    // Lives as small bats, right to left.
    for (int i = 0; i < _lives && i < MAX_LIVES; ++i)
        cv.fillRect(W - 8 - i * 8, 4, 6, 2, 0xBDF7);

    // The Flux meter across the top of the field.
    const int mw = FIELD_R - FIELD_L;
    cv.fillRect(FIELD_L, METER_Y, mw, 2, 0x2104);
    const unsigned long now = millis();
    uint16_t mc = ArcadeConfig::COLOR_AMBER;
    if (_meter >= METER_FULL) mc = _charging ? ArcadeConfig::COLOR_WHITE : ((now / 150) % 2 ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_WHITE);
    cv.fillRect(FIELD_L, METER_Y, mw * min(_meter, METER_FULL) / METER_FULL, 2, mc);

    // The timed capsule, as its letter and a draining bar under the bat.
    if (_effect != CAP_COUNT && _now < _effectUntil) {
        const unsigned long total = _effect == CAP_WIDE ? WIDE_MS : _effect == CAP_CATCH ? CATCH_MS
                                  : _effect == CAP_LASER ? LASER_MS : SLOW_MS;
        static const char LETTERS[CAP_COUNT + 1] = "WMCLSF";
        cv.setTextColor(ArcadeConfig::COLOR_GREY);
        cv.setCursor(FIELD_L + 1, H - 8);
        cv.print(LETTERS[_effect]);
        const int bw = (int)((FIELD_R - FIELD_L - 12) * (_effectUntil - _now) / total);
        cv.fillRect(FIELD_L + 10, H - 4, bw, 1, ArcadeConfig::COLOR_GREY);
    }
}

inline void BrickFluxGame::drawRoundOverlay(GFXcanvas16 &cv) {
    char buf[24];
    const unsigned long now = millis();
    switch (_round) {
        case ROUND_INTRO:
            panel(cv, 24, 80, 80, 20);
            snprintf(buf, sizeof(buf), "LEVEL %d", _level);
            hiscore::printCentred(cv, buf, 86, ArcadeConfig::COLOR_CYAN);
            break;
        case ROUND_PLAY:
            if (_serving && !_demo && (now / 400) % 2 == 0)
                hiscore::printCentred(cv, "A: SERVE", 118, ArcadeConfig::COLOR_WHITE);
            break;
        case ROUND_LOST:
            panel(cv, 18, 80, 92, 20);
            hiscore::printCentred(cv, _lives > 0 ? "LIFE LOST" : "GAME OVER", 86, ArcadeConfig::COLOR_RED);
            break;
        case ROUND_CLEAR:
            panel(cv, 10, 56, 108, 58);
            hiscore::printCentred(cv, "LEVEL CLEAR", 61, ArcadeConfig::COLOR_GREEN);
            snprintf(buf, sizeof(buf), "CLEAR %d", CLEAR_BONUS);
            hiscore::printCentred(cv, buf, 74, ArcadeConfig::COLOR_WHITE);
            snprintf(buf, sizeof(buf), "HEADROOM %dx%d", _headroomRows, HEADROOM_PER_ROW);
            hiscore::printCentred(cv, buf, 84, ArcadeConfig::COLOR_WHITE);
            if (!_lostThisLevel) hiscore::printCentred(cv, "NO LIFE LOST 2000", 94, ArcadeConfig::COLOR_YELLOW);
            snprintf(buf, sizeof(buf), "+%ld", _clearBonus);
            hiscore::printCentred(cv, buf, 104, ArcadeConfig::COLOR_CYAN);
            break;
    }
    if (now < _perfectFlashUntil) hiscore::printCentred(cv, "PERFECT!", 104, ArcadeConfig::COLOR_WHITE, 2);
}

inline void BrickFluxGame::drawQuitHint(GFXcanvas16 &cv) {
    if (_btnBHoldStart == 0) return;
    const unsigned long held = millis() - _btnBHoldStart;
    if (held < QUIT_HINT_DELAY_MS) return;
    cv.fillRect(14, 20, 100, 13, COL_PANEL);
    hiscore::printCentred(cv, "HOLD B TO QUIT", 21, ArcadeConfig::COLOR_WHITE);
    const int fill = (int)(96UL * (held > EXIT_HOLD_MS ? EXIT_HOLD_MS : held) / EXIT_HOLD_MS);
    cv.fillRect(16, 30, fill, 2, ArcadeConfig::COLOR_AMBER);
}

// The title, drawn rather than a bitmap: a band of bricks, the name, and a
// ball bouncing between a bat and the bricks.
inline void BrickFluxGame::renderTitle(GFXcanvas16 &cv) {
    cv.fillScreen(ArcadeConfig::COLOR_BLACK);
    const unsigned long now = millis();
    static const uint16_t BAND[5] = { 0xFFFF, 0xFFE0, 0xFD20, 0x07E0, 0x041F };
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < COLS; ++c)
            if (!((r == 4) && (c % 4 == 1)))       // a few gaps, as if mid-game
                cv.fillRect(GRID_X + c * CELL_W, 14 + r * CELL_H, CELL_W - 1, CELL_H - 1, BAND[r]);
    hiscore::printCentred(cv, "BRICK", 48, ArcadeConfig::COLOR_CYAN, 3);
    hiscore::printCentred(cv, "FLUX", 74, ArcadeConfig::COLOR_MAGENTA, 3);

    // The ball rises from the bat to the bricks and back, the bat tracking it.
    const float t = (now % 1600) / 1600.0f;
    const float h = t < 0.5f ? t * 2 : (1 - t) * 2;
    const float bx = 20 + 88 * (0.5f + 0.5f * sinf(now / 700.0f));
    const float by = 128 - h * 88;
    if (by > 100 || by < 44) cv.fillRect((int)bx - 1, (int)by - 1, 3, 3, ArcadeConfig::COLOR_WHITE);
    cv.fillRect((int)bx - 12, 131, 24, 3, 0xBDF7);

    if (now % 1000 < 600) hiscore::printCentred(cv, "PRESS A TO START", 140, ArcadeConfig::COLOR_WHITE);
    char buf[24];
    hiscore::printCentred(cv, _scores.bestLine(buf, sizeof(buf)), 151, ArcadeConfig::COLOR_YELLOW);
}

// Two how-to-play slides: the bat, then the smash and the wall. The first
// has a small bat tilting to and fro, the ball leaving it at its aim.
inline void BrickFluxGame::renderInfo(GFXcanvas16 &cv, bool second) {
    cv.fillScreen(ArcadeConfig::COLOR_BLACK);
    const uint16_t WH = ArcadeConfig::COLOR_WHITE, GR = ArcadeConfig::COLOR_GREY;
    if (!second) {
        hiscore::printCentred(cv, "HOW TO PLAY", 8, ArcadeConfig::COLOR_CYAN);
        hiscore::printCentred(cv, "STICK L/R: MOVE BAT", 24, WH);
        hiscore::printCentred(cv, "STICK U/D: TILT BAT", 34, WH);
        hiscore::printCentred(cv, "THE TILT AIMS", 46, GR);
        hiscore::printCentred(cv, "THE REBOUND", 56, GR);
        const float tilt = TILT_MAX_DEG * sinf(millis() / 600.0f);
        const float t = degToRad(tilt), cx = 64, cy = 112, half = 14;
        for (int d = -1; d <= 1; ++d)
            cv.drawLine((int)(cx - half * cosf(t)), (int)(cy + half * sinf(t)) + d,
                        (int)(cx + half * cosf(t)), (int)(cy - half * sinf(t)) + d, 0xBDF7);
        float dx, dy;
        dirFromAngle(batAimDeg(tilt), dx, dy);
        for (int k = 1; k <= 5; ++k)
            cv.fillRect((int)(cx + dx * k * 7) - 1, (int)(cy - 3 + dy * k * 7) - 1, 2, 2, k == 5 ? WH : GR);
        hiscore::printCentred(cv, "A: SERVE / LASERS", 128, WH);
        hiscore::printCentred(cv, "HOLD B: QUIT", 140, GR);
    } else {
        hiscore::printCentred(cv, "FLUX SMASH", 8, ArcadeConfig::COLOR_YELLOW);
        hiscore::printCentred(cv, "BREAK BRICKS TO FILL", 22, WH);
        hiscore::printCentred(cv, "THE METER. HOLD A,", 32, WH);
        hiscore::printCentred(cv, "LET GO AS THE BALL", 42, WH);
        hiscore::printCentred(cv, "MEETS THE BAT", 52, WH);
        hiscore::printCentred(cv, "IT SMASHES THROUGH!", 64, GR);
        hiscore::printCentred(cv, "THE WALL", 84, ArcadeConfig::COLOR_RED);
        hiscore::printCentred(cv, "THE BRICKS CREEP", 98, WH);
        hiscore::printCentred(cv, "DOWN. ONE ON THE", 108, WH);
        hiscore::printCentred(cv, "RED LINE COSTS", 118, WH);
        hiscore::printCentred(cv, "A LIFE", 128, WH);
        for (int x = FIELD_L; x < FIELD_R; x += 3) cv.drawFastHLine(x, 140, 2, COL_DANGER_HOT);
    }
    if (millis() % 1000 < 600) hiscore::printCentred(cv, "PRESS A TO START", 150, ArcadeConfig::COLOR_CYAN);
}

inline void BrickFluxGame::renderScores(GFXcanvas16 &cv) {
    cv.fillScreen(ArcadeConfig::COLOR_BLACK);
    hiscore::drawTable(cv, _scores.table(), "HIGH SCORES", 30);
    if (millis() % 1000 < 600) hiscore::printCentred(cv, "PRESS A TO START", 140, ArcadeConfig::COLOR_WHITE);
}

inline void BrickFluxGame::renderGameOver(GFXcanvas16 &cv) {
    panel(cv, 8, 44, 112, 72);
    hiscore::printCentred(cv, "GAME OVER", 50, ArcadeConfig::COLOR_RED, 2);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE %ld", _score);
    hiscore::printCentred(cv, buf, 72, ArcadeConfig::COLOR_WHITE);
    snprintf(buf, sizeof(buf), "LEVEL %d", _level);
    hiscore::printCentred(cv, buf, 82, ArcadeConfig::COLOR_GREY);
    const int rank = _scores.lastRank();
    if (rank >= 0) {
        snprintf(buf, sizeof(buf), rank == 0 ? "NEW HIGH SCORE!" : "HIGH SCORE #%d", rank + 1);
        hiscore::printCentred(cv, buf, 92, ArcadeConfig::COLOR_YELLOW);
    }
    if (millis() - _phaseAt > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS && millis() % 1000 < 600)
        hiscore::printCentred(cv, "A: PLAY AGAIN", 104, ArcadeConfig::COLOR_CYAN);
}

inline void BrickFluxGame::drawDemoOverlay(GFXcanvas16 &cv) {
    cv.setTextSize(1);
    cv.setTextColor(ArcadeConfig::COLOR_WHITE);
    cv.setCursor(FIELD_L + 2, 120);
    cv.print("DEMO");
    if ((millis() / 500) % 2 == 0) hiscore::printCentred(cv, "PRESS A TO PLAY", 132, ArcadeConfig::COLOR_CYAN);
}

}  // namespace brickflux

#endif  // BRICK_RENDER_H
