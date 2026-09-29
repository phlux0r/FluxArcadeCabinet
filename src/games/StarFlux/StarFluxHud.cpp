#include "StarFluxGame.h"
#include "assets/StarTitleScreen.h"
#include <string.h>

namespace starflux {

namespace {
constexpr int16_t W = ArcadeConfig::LANDSCAPE_WIDTH;
constexpr uint16_t PANEL = 0x0843;   // near-black blue, behind menu text
constexpr uint16_t STRIP = 0x0822;   // the HUD strip

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
}  // namespace

void StarFluxGame::drawCentred(GFXcanvas16 &canvas, const char* text, int y, uint16_t colour, uint8_t size) {
    canvas.setTextSize(size);
    canvas.setTextColor(colour);
    int16_t bx, by; uint16_t bw, bh;
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas.setCursor((W - (int16_t)bw) / 2, y);
    canvas.print(text);
    canvas.setTextSize(1);
}

// Top strip: score, shield bar, bombs, lives. The boss's health goes
// under it while the boss is up.
void StarFluxGame::drawHUD(GFXcanvas16 &canvas) {
    canvas.fillRect(0, 0, W, 9, STRIP);
    canvas.setTextSize(1);
    canvas.setCursor(2, 1);
    if (inDemo()) {
        canvas.setTextColor(ArcadeConfig::COLOR_MAGENTA);
        canvas.print("DEMO");
    } else {
        char buf[12];
        snprintf(buf, sizeof(buf), "%07ld", _score);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.print(buf);
    }

    // Shield: green, amber below half, red below a quarter.
    const int16_t bx = 48, bw = 52;
    canvas.drawRect(bx, 1, bw, 7, ArcadeConfig::COLOR_GREY);
    int16_t fill = (int16_t)((long)(bw - 2) * _shield / SHIELD_MAX);
    uint16_t col = _shield * 4 < SHIELD_MAX ? ArcadeConfig::COLOR_RED
                 : _shield * 2 < SHIELD_MAX ? ArcadeConfig::COLOR_AMBER : ArcadeConfig::COLOR_GREEN;
    if (_shield * 4 < SHIELD_MAX && ((millis() / 250) & 1)) col = rgb(90, 0, 0);
    canvas.fillRect(bx + 1, 2, fill, 5, col);

    // Bombs: blue pips.
    for (int i = 0; i < _bombs && i < BOMBS_MAX; ++i) {
        canvas.fillCircle(106 + i * 6, 4, 2, rgb(120, 170, 255));
    }
    // Lives: a small ship and a count.
    int16_t lx = 140;
    canvas.fillTriangle(lx, 7, lx + 4, 1, lx + 8, 7, rgb(236, 232, 220));
    canvas.drawPixel(lx + 4, 5, rgb(255, 196, 64));
    canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
    canvas.setCursor(lx + 11, 1);
    canvas.print(_lives > 0 ? _lives : 0);

    if (_bossActive && _stage != STAGE_BOSS_DEATH) {
        int16_t hw = (int16_t)(100L * bossHp() / (_bossMaxHp > 0 ? _bossMaxHp : 1));
        canvas.fillRect(30, 11, 100, 2, rgb(70, 0, 50));
        canvas.fillRect(30, 11, hw, 2, ArcadeConfig::COLOR_MAGENTA);
    }
}

void StarFluxGame::drawOverlays(GFXcanvas16 &canvas) {
    if (inDemo() && ((millis() / 500) & 1)) {
        drawCentred(canvas, "PRESS A TO PLAY", 116, ArcadeConfig::COLOR_WHITE);
    }
    if (before(_hitFlashUntil)) {
        canvas.drawRect(0, 9, W, canvas.height() - 9, ArcadeConfig::COLOR_RED);
        canvas.drawRect(1, 10, W - 2, canvas.height() - 11, ArcadeConfig::COLOR_RED);
    }
    // The fly-in: the stage's name.
    if (_stage == STAGE_INTRO && _phase == PHASE_PLAYING) {
        unsigned long t = millis() - _stageAt;
        if (t > 300) {
            char buf[24];
            if (_loop > 1) snprintf(buf, sizeof(buf), "STAGE 1-%d", _loop);
            else snprintf(buf, sizeof(buf), "STAGE 1");
            drawCentred(canvas, buf, 34, ArcadeConfig::COLOR_WHITE, 2);
        }
        if (t > 900) drawCentred(canvas, "AURORA BELT", 56, ArcadeConfig::COLOR_CYAN);
        if (t > 1800 && ((millis() / 200) & 1)) drawCentred(canvas, "ALL WINGS, ENGAGE!", 68, ArcadeConfig::COLOR_YELLOW);
    }
    if (before(_bannerUntil) && ((millis() / 200) & 1)) {
        drawCentred(canvas, _banner, 15, _bannerColour);
    }
}

// Only once B has been held past QUIT_HINT_DELAY_MS, so a tap (a bomb)
// never flashes it.
void StarFluxGame::drawQuitHint(GFXcanvas16 &canvas) {
    if (_btnBHoldStart == 0) return;
    unsigned long held = millis() - _btnBHoldStart;
    if (held < QUIT_HINT_DELAY_MS) return;
    canvas.fillRect(30, 12, 100, 13, PANEL);
    drawCentred(canvas, "HOLD B TO QUIT", 13, ArcadeConfig::COLOR_WHITE);
    int16_t fill = (int16_t)(96UL * (held > EXIT_HOLD_MS ? EXIT_HOLD_MS : held) / EXIT_HOLD_MS);
    canvas.fillRect(32, 22, fill, 2, ArcadeConfig::COLOR_AMBER);
}

void StarFluxGame::enterAttract() {
    _phase = PHASE_ATTRACT;
    _shipSprite.enabled = false;
    _phaseEnteredMs = millis();
    _attractSlide = SLIDE_TITLE;
    _attractSlideAt = millis();
    _stage = STAGE_RUN;
}

// The pre-rendered title (tools/star_title_screen.py) with the start
// prompt and high score in its black bottom strip.
void StarFluxGame::renderAttractTitle(GFXcanvas16 &canvas) {
    if (canvas.width() == TITLE_W && canvas.height() == TITLE_H) {
        memcpy(canvas.getBuffer(), TITLE_SCREEN, sizeof(TITLE_SCREEN));
    }
    if (millis() % 1000 < 600) drawCentred(canvas, "[BTN A] TO PLAY", TITLE_STRIP_Y + 5, ArcadeConfig::COLOR_WHITE);
    char buf[20];
    snprintf(buf, sizeof(buf), "HI: %ld", _highScore);
    drawCentred(canvas, buf, TITLE_STRIP_Y + 14, ArcadeConfig::COLOR_YELLOW);
    drawQuitHint(canvas);
}

void StarFluxGame::renderAttractInfo(GFXcanvas16 &canvas) {
    canvas.fillRect(14, 18, W - 28, 92, PANEL);
    drawCentred(canvas, "HOW TO PLAY", 22, ArcadeConfig::COLOR_CYAN);
    drawCentred(canvas, "STICK: FLY", 33, ArcadeConfig::COLOR_WHITE);
    drawCentred(canvas, "A: LASERS (HOLD: STEADY)", 42, ArcadeConfig::COLOR_GREEN);
    drawCentred(canvas, "TAP B: SMART BOMB", 51, rgb(120, 170, 255));
    drawCentred(canvas, "FLY THROUGH SILVER", 61, ArcadeConfig::COLOR_GREY);
    drawCentred(canvas, "RINGS FOR SHIELD", 69, ArcadeConfig::COLOR_GREY);
    drawCentred(canvas, "BOSS: HIT THE GLOWS", 79, ArcadeConfig::COLOR_ORANGE);
    if ((millis() / 500) & 1) drawCentred(canvas, "PRESS A TO START", 91, ArcadeConfig::COLOR_WHITE);
    char buf[20];
    snprintf(buf, sizeof(buf), "HI %ld", _highScore);
    drawCentred(canvas, buf, 100, ArcadeConfig::COLOR_YELLOW);
    drawCentred(canvas, "HOLD B TO EXIT", 116, ArcadeConfig::COLOR_GREY);
    drawQuitHint(canvas);
}

// The stage tally: lines appear one by one.
void StarFluxGame::renderResults(GFXcanvas16 &canvas) {
    const unsigned long t = millis() - _phaseEnteredMs;
    canvas.fillRect(10, 16, W - 20, 96, PANEL);
    canvas.drawRect(10, 16, W - 20, 96, rgb(60, 70, 140));
    drawCentred(canvas, "STAGE CLEAR", 21, ArcadeConfig::COLOR_CYAN, 2);
    char buf[28];
    if (t > 400) {
        snprintf(buf, sizeof(buf), "ENEMIES DOWNED %d/%d", _fightersDowned, _fightersSeen);
        drawCentred(canvas, buf, 44, ArcadeConfig::COLOR_WHITE);
    }
    if (t > 800) {
        snprintf(buf, sizeof(buf), "ROCKS %d  RINGS %d", _rocksDowned, _ringsCaught);
        drawCentred(canvas, buf, 54, ArcadeConfig::COLOR_GREY);
    }
    if (t > 1200) {
        snprintf(buf, sizeof(buf), "SHIELD BONUS %ld", _shieldBonus);
        drawCentred(canvas, buf, 64, ArcadeConfig::COLOR_GREEN);
    }
    if (t > 1600) {
        snprintf(buf, sizeof(buf), "SCORE %ld", _score);
        drawCentred(canvas, buf, 76, ArcadeConfig::COLOR_YELLOW);
        if (_newHighScore && ((millis() / 300) & 1)) drawCentred(canvas, "NEW HIGH SCORE!", 86, ArcadeConfig::COLOR_MAGENTA);
    }
    if (t > RESULTS_MIN_MS && ((millis() / 400) & 1)) drawCentred(canvas, "A: NEXT LOOP", 99, ArcadeConfig::COLOR_WHITE);
    drawQuitHint(canvas);
}

void StarFluxGame::renderGameOver(GFXcanvas16 &canvas) {
    canvas.fillRect(14, 22, W - 28, 84, PANEL);
    drawCentred(canvas, "GAME OVER", 28, ArcadeConfig::COLOR_RED, 2);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE %ld", _score);
    drawCentred(canvas, buf, 52, ArcadeConfig::COLOR_YELLOW);
    snprintf(buf, sizeof(buf), "LOOP %d", _loop);
    drawCentred(canvas, buf, 64, ArcadeConfig::COLOR_CYAN);
    if (_newHighScore) {
        if ((millis() / 300) & 1) drawCentred(canvas, "NEW HIGH SCORE!", 78, ArcadeConfig::COLOR_GREEN);
    } else {
        snprintf(buf, sizeof(buf), "HI %ld", _highScore);
        drawCentred(canvas, buf, 78, ArcadeConfig::COLOR_GREEN);
    }
    if (millis() - _phaseEnteredMs > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS) {
        drawCentred(canvas, "A: AGAIN", 94, ArcadeConfig::COLOR_WHITE);
    }
    drawCentred(canvas, "HOLD B TO EXIT", 116, ArcadeConfig::COLOR_GREY);
    drawQuitHint(canvas);
}

}  // namespace starflux
