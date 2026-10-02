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

// Top strip: score on the left, gems and spare balls on the right, and the
// clock big in the middle (red, flashing, in the last ten seconds). A
// banner line under it for news.
void RollFluxGame::drawHUD(GFXcanvas16 &canvas) {
    canvas.setFont();
    canvas.fillRect(0, 0, W, 10, ArcadeConfig::COLOR_BLACK);
    canvas.setTextSize(1);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    canvas.setCursor(2, 1);
    canvas.print(_score);

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

void RollFluxGame::renderGameOver(GFXcanvas16 &canvas) {
    canvas.setFont();
    canvas.fillRect(14, 22, W - 28, 84, PANEL);
    drawCentred(canvas, "GAME OVER", 28, ArcadeConfig::COLOR_RED, 2);
    char buf[28];
    snprintf(buf, sizeof(buf), "SCORE %ld", _score);
    drawCentred(canvas, buf, 52, ArcadeConfig::COLOR_YELLOW);
    snprintf(buf, sizeof(buf), "COURSE %d  GEMS %ld", _course + 1, _gemsTotal);
    drawCentred(canvas, buf, 66, ArcadeConfig::COLOR_CYAN);
    if (millis() - _phaseAt > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS)
        drawCentred(canvas, "A: AGAIN", 86, ArcadeConfig::COLOR_WHITE);
    drawCentred(canvas, "HOLD BACK TO EXIT", 116, ArcadeConfig::COLOR_GREY);
}

}  // namespace rollflux
