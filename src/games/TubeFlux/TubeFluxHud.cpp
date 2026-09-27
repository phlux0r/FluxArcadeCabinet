#include "TubeFluxGame.h"

namespace tubeflux {

namespace {
constexpr int16_t W = ArcadeConfig::LANDSCAPE_WIDTH;
constexpr uint16_t PANEL = 0x0843;   // near-black blue, behind menu text

inline bool before(unsigned long deadline) { return (long)(millis() - deadline) < 0; }
}  // namespace

void TubeFluxGame::drawCentred(GFXcanvas16 &canvas, const char* text, int y, uint16_t colour, uint8_t size) {
    canvas.setTextSize(size);
    canvas.setTextColor(colour);
    int16_t bx, by; uint16_t bw, bh;
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas.setCursor((W - (int16_t)bw) / 2, y);
    canvas.print(text);
    canvas.setTextSize(1);
}

// Top strip: score, tier, shield pips. The ship owns the bottom of the
// screen, so nothing else goes there.
void TubeFluxGame::drawHUD(GFXcanvas16 &canvas) {
    canvas.fillRect(0, 0, W, 10, ArcadeConfig::COLOR_BLACK);
    canvas.setTextSize(1);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    canvas.setCursor(3, 1);
    canvas.print("SC ");
    canvas.print(_score);

    canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
    canvas.setCursor(W / 2 + 6, 1);
    canvas.print("T");
    canvas.print(_tier);

    // Shield pips, right-aligned: filled for each hit you can still take.
    for (int i = 0; i < SHIELD_MAX; ++i) {
        int16_t x = W - 3 - (SHIELD_MAX - i) * 9;
        if (i < _shield) canvas.fillRect(x, 2, 7, 6, ArcadeConfig::COLOR_GREEN);
        else             canvas.drawRect(x, 2, 7, 6, ArcadeConfig::COLOR_GREY);
    }
}

void TubeFluxGame::drawOverlays(GFXcanvas16 &canvas) {
    if (before(_hitFlashUntil)) {
        canvas.drawRect(0, 10, W, canvas.height() - 10, ArcadeConfig::COLOR_RED);
        canvas.drawRect(1, 11, W - 2, canvas.height() - 12, ArcadeConfig::COLOR_RED);
    }
    if (before(_tierBannerUntil)) {
        char buf[12];
        snprintf(buf, sizeof(buf), "TIER %d", _tier);
        drawCentred(canvas, buf, 36, ArcadeConfig::COLOR_CYAN, 2);
        drawCentred(canvas, "FASTER", 54, ArcadeConfig::COLOR_WHITE);
    }
    if (before(_nearMissUntil)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "CLOSE +%d", NEAR_MISS_POINTS);
        drawCentred(canvas, buf, 24, ArcadeConfig::COLOR_MAGENTA);
    }
}

// Only once B has been held past QUIT_HINT_DELAY_MS, so a tap never flashes it.
void TubeFluxGame::drawQuitHint(GFXcanvas16 &canvas) {
    if (_btnBHoldStart == 0) return;
    unsigned long held = millis() - _btnBHoldStart;
    if (held < QUIT_HINT_DELAY_MS) return;
    canvas.fillRect(30, 12, 100, 13, PANEL);
    drawCentred(canvas, "HOLD B TO QUIT", 13, ArcadeConfig::COLOR_WHITE);
    int16_t fill = (int16_t)(96UL * (held > EXIT_HOLD_MS ? EXIT_HOLD_MS : held) / EXIT_HOLD_MS);
    canvas.fillRect(32, 22, fill, 2, ArcadeConfig::COLOR_AMBER);
}

void TubeFluxGame::renderAttract(GFXcanvas16 &canvas) {
    canvas.fillRect(14, 22, W - 28, 84, PANEL);
    drawCentred(canvas, "TUBE FLUX", 28, ArcadeConfig::COLOR_CYAN, 2);
    drawCentred(canvas, "ROLL: JOYSTICK L/R", 52, ArcadeConfig::COLOR_WHITE);
    drawCentred(canvas, "BOOST: UP  BRAKE: DOWN", 62, ArcadeConfig::COLOR_GREY);
    if ((millis() / 500) & 1) drawCentred(canvas, "PRESS A TO START", 78, ArcadeConfig::COLOR_YELLOW);
    char buf[20];
    snprintf(buf, sizeof(buf), "HI %ld", _highScore);
    drawCentred(canvas, buf, 92, ArcadeConfig::COLOR_GREEN);
    drawCentred(canvas, "HOLD B TO EXIT", 116, ArcadeConfig::COLOR_GREY);
    drawQuitHint(canvas);
}

void TubeFluxGame::renderGameOver(GFXcanvas16 &canvas) {
    canvas.fillRect(14, 22, W - 28, 84, PANEL);
    drawCentred(canvas, "GAME OVER", 28, ArcadeConfig::COLOR_RED, 2);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE %ld", _score);
    drawCentred(canvas, buf, 52, ArcadeConfig::COLOR_YELLOW);
    snprintf(buf, sizeof(buf), "REACHED TIER %d", _tier);
    drawCentred(canvas, buf, 64, ArcadeConfig::COLOR_CYAN);
    if (_newHighScore) {
        if ((millis() / 300) & 1) drawCentred(canvas, "NEW HIGH SCORE!", 78, ArcadeConfig::COLOR_GREEN);
    } else {
        snprintf(buf, sizeof(buf), "HI %ld", _highScore);
        drawCentred(canvas, buf, 78, ArcadeConfig::COLOR_GREEN);
    }
    if (millis() - _phaseEnteredMs > GAMEOVER_INPUT_DELAY_MS) {
        drawCentred(canvas, "A: AGAIN", 94, ArcadeConfig::COLOR_WHITE);
    }
    drawCentred(canvas, "HOLD B TO EXIT", 116, ArcadeConfig::COLOR_GREY);
    drawQuitHint(canvas);
}

}  // namespace tubeflux
