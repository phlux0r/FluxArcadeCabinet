#include "RollFluxGame.h"

namespace rollflux {

namespace {
constexpr int16_t W = ArcadeConfig::LANDSCAPE_WIDTH;
constexpr uint16_t PANEL = 0x0843;   // near-black blue, behind menu text
}  // namespace

void RollFluxGame::drawCentred(GFXcanvas16 &canvas, const char* text, int y, uint16_t colour, uint8_t size) {
    canvas.setTextSize(size);
    canvas.setTextColor(colour);
    int16_t bx, by; uint16_t bw, bh;
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas.setCursor((W - (int16_t)bw) / 2, y);
    canvas.print(text);
    canvas.setTextSize(1);
}

// Top strip: score on the left (DEMO in the demo), gems and spare balls
// on the right, and the clock big in the middle (red, flashing, in the
// last ten seconds). A banner line under it for news; the dash meter along
// the bottom.
void RollFluxGame::drawHUD(GFXcanvas16 &canvas) {
    canvas.setFont();
    canvas.fillRect(0, 0, W, 10, ArcadeConfig::COLOR_BLACK);
    canvas.setTextSize(1);
    canvas.setCursor(2, 1);
    if (_demo) {
        canvas.setTextColor(ArcadeConfig::COLOR_MAGENTA);
        canvas.print("DEMO");
    } else {
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.print(_score);
    }

    // Spare balls, right-aligned (a number past five).
    int x = W - 6;
    const int shown = _lives > 5 ? 1 : _lives;
    for (int i = 0; i < shown; ++i, x -= 8) canvas.fillCircle(x, 4, 3, ArcadeConfig::COLOR_CYAN);
    if (_lives > 5) {
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(x - 8, 1);
        canvas.print(_lives);
        x -= 14;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", _courseGemsTaken, _courseGems);
    const int gx = x - 4 - (int)strlen(buf) * 6;
    canvas.fillTriangle(gx - 7, 4, gx - 4, 0, gx - 1, 4, ArcadeConfig::COLOR_YELLOW);
    canvas.fillTriangle(gx - 7, 4, gx - 4, 8, gx - 1, 4, ArcadeConfig::COLOR_ORANGE);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    canvas.setCursor(gx + 1, 1);
    canvas.print(buf);

    // The clock: whole seconds, rounded up, so 0 means time's up.
    const long secs = (_timeMs + 999) / 1000;
    snprintf(buf, sizeof(buf), "%ld", secs);
    const bool warn = _timeMs < (long)TIME_WARN_MS;
    canvas.fillRect(W / 2 - 17, 0, 34, 17, ArcadeConfig::COLOR_BLACK);
    if (!warn || ((millis() / 250) & 1) || _phase != PHASE_PLAYING)
        drawCentred(canvas, buf, 2, warn ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_WHITE, 2);

    if ((long)(millis() - _bannerUntil) < 0) drawCentred(canvas, _banner, 21, _bannerColour);
    if (_demo && ((millis() / 500) & 1)) drawCentred(canvas, "PRESS A TO PLAY", 31, ArcadeConfig::COLOR_WHITE);

    // The dash meter along the bottom: three steps, each filling with gems;
    // full steps bright, flickering while it charges.
    const int H = canvas.height(), segW = (W - 8) / DASH_STEPS;
    for (int i = 0; i < DASH_STEPS; ++i) {
        const int x = 2 + i * (segW + 2);
        int fill = _dashGems - i * DASH_GEMS_PER_STEP;
        if (fill > DASH_GEMS_PER_STEP) fill = DASH_GEMS_PER_STEP;
        if (fill < 0) fill = 0;
        canvas.fillRect(x, H - 4, segW, 3, 0x2104);
        const bool full = fill == DASH_GEMS_PER_STEP;
        const uint16_t col = !full ? 0x0451
                           : (_charging && i == dashSteps() - 1 && ((millis() / 80) & 1)) ? ArcadeConfig::COLOR_WHITE
                           : ArcadeConfig::COLOR_CYAN;
        canvas.fillRect(x, H - 4, segW * fill / DASH_GEMS_PER_STEP, 3, col);
    }
}

// The tally over the goal: what each bonus added.
void RollFluxGame::renderClear(GFXcanvas16 &canvas) {
    canvas.fillRect(18, 26, W - 36, 74, PANEL);
    drawCentred(canvas, "COURSE CLEAR", 31, ArcadeConfig::COLOR_GREEN, 1);
    char buf[28];
    snprintf(buf, sizeof(buf), "TIME     %6ld", _clearTime);
    drawCentred(canvas, buf, 46, ArcadeConfig::COLOR_WHITE);
    snprintf(buf, sizeof(buf), "NO FALLS %6ld", _clearNoFall);
    drawCentred(canvas, buf, 56, _clearNoFall ? ArcadeConfig::COLOR_CYAN : ArcadeConfig::COLOR_GREY);
    snprintf(buf, sizeof(buf), "ALL GEMS %6ld", _clearAllGems);
    drawCentred(canvas, buf, 66, _clearAllGems ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_GREY);
    snprintf(buf, sizeof(buf), "SCORE %ld", _score);
    drawCentred(canvas, buf, 82, ArcadeConfig::COLOR_YELLOW);
}

// The title over the orbiting course: the name big, the start prompt and
// the best score.
void RollFluxGame::renderTitle(GFXcanvas16 &canvas) {
    canvas.setFont();
    canvas.fillRect(0, 18, W, 40, PANEL);
    drawCentred(canvas, "ROLL", 21, ArcadeConfig::COLOR_CYAN, 2);
    drawCentred(canvas, "FLUX", 38, ArcadeConfig::COLOR_YELLOW, 2);
    canvas.fillRect(0, 100, W, 28, ArcadeConfig::COLOR_BLACK);
    if (millis() % 1000 < 600) drawCentred(canvas, "[BTN A] TO PLAY", 105, ArcadeConfig::COLOR_WHITE);
    char buf[24];
    drawCentred(canvas, _scores.bestLine(buf, sizeof(buf), "HI: "), 116, ArcadeConfig::COLOR_YELLOW);
}

// How to play: rolling and the course first, then the dash and the pads.
void RollFluxGame::renderHowTo(GFXcanvas16 &canvas, bool dash) {
    canvas.setFont();
    canvas.fillRect(10, 14, W - 20, 96, PANEL);
    if (!dash) {
        drawCentred(canvas, "HOW TO PLAY", 18, ArcadeConfig::COLOR_CYAN);
        drawCentred(canvas, "STICK: TILT THE COURSE", 30, ArcadeConfig::COLOR_WHITE);
        drawCentred(canvas, "ROLL TO THE CHEQUERS", 40, ArcadeConfig::COLOR_WHITE);
        drawCentred(canvas, "BEFORE TIME RUNS OUT", 50, ArcadeConfig::COLOR_WHITE);
        drawCentred(canvas, "GEMS: +2 SECONDS", 62, ArcadeConfig::COLOR_YELLOW);
        drawCentred(canvas, "DARK ROW: CHECKPOINT", 72, ArcadeConfig::COLOR_CYAN);
        drawCentred(canvas, "DON'T FALL OFF!", 84, ArcadeConfig::COLOR_ORANGE);
    } else {
        drawCentred(canvas, "FLUX DASH", 18, ArcadeConfig::COLOR_CYAN);
        drawCentred(canvas, "5 GEMS FILL A STEP", 30, ArcadeConfig::COLOR_YELLOW);
        drawCentred(canvas, "HOLD A TO CHARGE", 40, ArcadeConfig::COLOR_WHITE);
        drawCentred(canvas, "LET GO TO DASH", 50, ArcadeConfig::COLOR_WHITE);
        drawCentred(canvas, "IT JUMPS 2-CELL GAPS", 60, ArcadeConfig::COLOR_WHITE);
        drawCentred(canvas, "ARROWS: BOOST", 72, ArcadeConfig::COLOR_ORANGE);
        drawCentred(canvas, "PALE BLUE: ICE", 82, ArcadeConfig::COLOR_CYAN);
    }
    if ((millis() / 500) & 1) drawCentred(canvas, "PRESS A TO START", 96, ArcadeConfig::COLOR_WHITE);
    drawCentred(canvas, "HOLD BACK TO EXIT", 116, ArcadeConfig::COLOR_GREY);
}

// The cabinet's table for this game, over the orbiting course.
void RollFluxGame::renderScores(GFXcanvas16 &canvas) {
    canvas.setFont();
    canvas.fillRect(22, 18, W - 44, 88, PANEL);
    hiscore::drawTable(canvas, _scores.table(), "HIGH SCORES", 24);
    if ((millis() / 500) & 1) drawCentred(canvas, "PRESS A TO START", 96, ArcadeConfig::COLOR_WHITE);
    drawCentred(canvas, "HOLD BACK TO EXIT", 116, ArcadeConfig::COLOR_GREY);
}

void RollFluxGame::renderGameOver(GFXcanvas16 &canvas) {
    canvas.setFont();
    canvas.fillRect(14, 22, W - 28, 84, PANEL);
    drawCentred(canvas, "GAME OVER", 28, ArcadeConfig::COLOR_RED, 2);
    char buf[28];
    snprintf(buf, sizeof(buf), "SCORE %ld", _score);
    drawCentred(canvas, buf, 52, ArcadeConfig::COLOR_YELLOW);
    snprintf(buf, sizeof(buf), "COURSE %s  GEMS %ld", courseDef().code, _gemsTotal);
    drawCentred(canvas, buf, 64, ArcadeConfig::COLOR_CYAN);
    const int rank = _scores.lastRank();
    if (rank == 0) {
        if ((millis() / 300) & 1) drawCentred(canvas, "NEW HIGH SCORE!", 76, ArcadeConfig::COLOR_GREEN);
    } else if (rank > 0) {
        snprintf(buf, sizeof(buf), "HIGH SCORE #%d", rank + 1);
        drawCentred(canvas, buf, 76, ArcadeConfig::COLOR_GREEN);
    } else {
        drawCentred(canvas, _scores.bestLine(buf, sizeof(buf)), 76, ArcadeConfig::COLOR_GREEN);
    }
    if (millis() - _phaseAt > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS)
        drawCentred(canvas, "A: AGAIN", 90, ArcadeConfig::COLOR_WHITE);
    drawCentred(canvas, "HOLD BACK TO EXIT", 116, ArcadeConfig::COLOR_GREY);
}

}  // namespace rollflux
