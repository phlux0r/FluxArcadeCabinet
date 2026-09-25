#include "TankFluxGame.h"
#include "assets/TitleScreen.h"

namespace tankflux {

namespace {

constexpr int16_t W = ArcadeConfig::LANDSCAPE_WIDTH;
constexpr int16_t H = ArcadeConfig::LANDSCAPE_HEIGHT;
constexpr int STRIP_Y = 13;   // message strip under the HUD bar (divider at y=10)

// Prints `msg` horizontally centred at row y, in the current text size/colour.
void centredText(GFXcanvas16 &canvas, const char* msg, int16_t y) {
    int16_t tbx, tby; uint16_t tbw, tbh;
    canvas.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);
    canvas.setCursor((W - (int16_t)tbw) / 2, y);
    canvas.print(msg);
}

uint16_t textHeight(GFXcanvas16 &canvas, const char* msg) {
    int16_t tbx, tby; uint16_t tbw, tbh;
    canvas.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);
    return tbh;
}

// Concentric rectangles round the play area: a border rather than a full
// tint, which at 160x128 would hide what you need to see.
void flashBorder(GFXcanvas16 &canvas, int rings, uint16_t colour) {
    const int w = canvas.width(), h = canvas.height();
    for (int i = 0; i < rings; ++i) {
        canvas.drawRect(i, 11 + i, w - i * 2, h - 11 - i * 2, colour);
    }
}

}  // namespace

void TankFluxGame::drawHUD(GFXcanvas16 &canvas) {
    canvas.fillRect(0, 0, W, 10, ArcadeConfig::COLOR_BLACK);
    canvas.drawFastHLine(0, 10, W, ArcadeConfig::COLOR_GREEN);

    canvas.setFont();
    canvas.setTextSize(1);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    canvas.setCursor(4, 1);
    canvas.print("SCORE:"); canvas.print(_score);

    canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
    canvas.setCursor(W / 2 - 6, 1);
    canvas.print("LV"); canvas.print(_level);

    canvas.setTextColor(ArcadeConfig::COLOR_GREY);
    canvas.setCursor(W - 54, 1);
    canvas.print("HI:"); canvas.print(_highScore);

    // Boss health: a red bar under the HUD strip during a boss fight.
    if (_bossActive) {
        const int bx = 3, by = STRIP_Y, bw = W - 6, bh = 6;
        canvas.drawRect(bx, by, bw, bh, ArcadeConfig::COLOR_GREY);
        int bossFillW = ((bw - 2) * _boss.hp) / max(1, _boss.maxHp);
        if (bossFillW > 0) {
            canvas.fillRect(bx + 1, by + 1, bossFillW, bh - 2, ArcadeConfig::COLOR_RED);
        }
    }

    // Player health, bottom-left, short of the centre so it clears the barrel.
    const int barX = 3, barY = H - 7;
    const int barW = 58, barH = 5;
    canvas.drawRect(barX, barY, barW, barH, ArcadeConfig::COLOR_GREY);
    int fillW = ((barW - 2) * _health) / HEALTH_MAX;
    if (fillW > 0) {
        uint16_t c = (_health > 60) ? ArcadeConfig::COLOR_GREEN
                   : (_health > 30) ? ArcadeConfig::COLOR_AMBER
                                    : ArcadeConfig::COLOR_RED;
        canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, c);
    }
}

// A sun disc in the direction of _sunVisual, drawn over the sky after the 3D
// pass. Plain circles rather than Jet's LensFlare, which needs texture
// mapping and pick queries this project doesn't enable. Kept above the
// horizon so it can't paint over ground it should be behind.
void TankFluxGame::drawSun(GFXcanvas16 &canvas) {
    Vector3 viewDir = _camera.transformDirection(_sunVisual.worldLightDir);
    if (viewDir.z <= 0) return;   // behind the camera
    const float invZ = _camera.fovFactor / (float)viewDir.z;
    const int sx = canvas.width()  / 2 + (int)(viewDir.x * invZ);
    const int sy = canvas.height() / 2 - (int)(viewDir.y * invZ);
    if (sy >= canvas.height() / 2 - 6) return;   // at/below the horizon
    if (sy < 12) return;                          // under the HUD bar anyway
    if (sx < -20 || sx > canvas.width() + 20) return;
    canvas.fillCircle(sx, sy, 9, rgb565(31, 55, 24));   // pale halo
    canvas.fillCircle(sx, sy, 5, rgb565(31, 50, 10));   // warm core
    canvas.fillCircle(sx, sy, 2, ArcadeConfig::COLOR_WHITE);
}

void TankFluxGame::drawGunsight(GFXcanvas16 &canvas, int cx, int cy) {
    canvas.drawFastHLine(cx - 8, cy, 6, ArcadeConfig::COLOR_GREEN);
    canvas.drawFastHLine(cx + 3, cy, 6, ArcadeConfig::COLOR_GREEN);
    canvas.drawFastVLine(cx, cy - 8, 6, ArcadeConfig::COLOR_GREEN);
    canvas.drawFastVLine(cx, cy + 3, 6, ArcadeConfig::COLOR_GREEN);
}

// The player's barrel is 2D: it's fixed to the vehicle the camera is in, so
// it never moves on screen.
void TankFluxGame::drawBarrel(GFXcanvas16 &canvas) {
    const int cx = canvas.width() / 2;
    const int base = canvas.height() - 1;
    const int tipY = canvas.height() - 34;
    const uint16_t body = rgb565(9, 18, 10);
    const uint16_t edge = rgb565(16, 32, 18);

    canvas.fillTriangle(cx - 13, base, cx + 13, base, cx + 6, tipY, body);
    canvas.fillTriangle(cx - 13, base, cx + 6, tipY, cx - 6, tipY, body);
    canvas.drawLine(cx - 13, base, cx - 6, tipY, edge);
    canvas.drawLine(cx + 13, base, cx + 6, tipY, edge);
    canvas.drawFastHLine(cx - 6, tipY, 13, edge);

    if (!reached(_muzzleFlashUntil)) {
        canvas.fillCircle(cx, tipY - 2, 5, ArcadeConfig::COLOR_YELLOW);
        canvas.fillCircle(cx, tipY - 2, 2, ArcadeConfig::COLOR_WHITE);
    }
}

// Rotating radar, forward always up, so a blip's position is the direction
// to turn. Covers the whole arena, beyond visual range, so shells arriving
// out of the haze still have a visible source.
void TankFluxGame::drawRadar(GFXcanvas16 &canvas) {
    const int r  = 20;
    const int cx = canvas.width() - r - 5;
    const int cy = canvas.height() - r - 5;
    const float scale = (float)r / (float)RADAR_RANGE;

    canvas.fillCircle(cx, cy, r, rgb565(2, 6, 4));
    canvas.drawCircle(cx, cy, r, ArcadeConfig::COLOR_GREY);
    canvas.drawFastVLine(cx, cy - r, 4, ArcadeConfig::COLOR_GREY);   // bow marker

    const float hr = radians(_headingDeg);
    const float sh = sinf(hr), ch = cosf(hr);

    auto plot = [&](float wx, float wz, uint16_t colour, bool big) {
        float dx = wx - _x, dz = wz - _z;
        // Tank-local: forward = (sin h, cos h), right = (cos h, -sin h).
        float right   = dx * ch - dz * sh;
        float forward = dx * sh + dz * ch;
        float px = right * scale;
        float py = -forward * scale;
        // Out-of-range blips sit on the rim, still showing their direction.
        float len = sqrtf(px * px + py * py);
        if (len > (float)(r - 2)) {
            float k = (float)(r - 2) / len;
            px *= k;
            py *= k;
        }
        int sx = cx + (int)px, sy = cy + (int)py;
        if (big) canvas.fillCircle(sx, sy, 2, colour);
        else     canvas.drawPixel(sx, sy, colour);
    };

    for (int i = 0; i < REPAIR_COUNT; ++i) {
        if (_kits[i].active) plot((float)_arena.kits[i].x, (float)_arena.kits[i].z,
                                  ArcadeConfig::COLOR_GREEN, false);
    }
    for (const auto &e : _enemies) {
        if (e.alive) plot(e.x, e.z, ArcadeConfig::COLOR_RED, true);
    }
    if (_bossActive) {
        // Blinks magenta/red so it reads as a different kind of threat.
        uint16_t bossColour = (millis() % 300 < 150) ? ArcadeConfig::COLOR_MAGENTA
                                                      : ArcadeConfig::COLOR_RED;
        plot(_boss.x, _boss.z, bossColour, true);
    }

    canvas.drawPixel(cx, cy, ArcadeConfig::COLOR_WHITE);
}

// Red for damage; green, one ring wider, for the arena shift.
void TankFluxGame::drawFlashes(GFXcanvas16 &canvas) {
    if (!reached(_damageFlashUntil))     flashBorder(canvas, 3, ArcadeConfig::COLOR_RED);
    if (!reached(_arenaShiftFlashUntil)) flashBorder(canvas, 4, ArcadeConfig::COLOR_GREEN);
}

// Under the HUD rather than screen-centre, so it doesn't cover the gunsight
// while regular enemies are still around. The whole bar flashes (text stays
// readable in both phases); small blinking text alone was easy to miss.
void TankFluxGame::drawBossAlert(GFXcanvas16 &canvas) {
    if (!_bossPending || reached(_bossAlertUntil)) return;
    const char* msg = "BOSS ALERT";
    const int y = 14;
    canvas.setFont();
    canvas.setTextSize(2);
    bool on = millis() % 400 < 200;
    canvas.fillRect(0, y - 3, W, (int)textHeight(canvas, msg) + 5,
                    on ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_BLACK);
    canvas.setTextColor(on ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_RED);
    centredText(canvas, msg, y);
    canvas.setTextSize(1);
}

// In the strip the boss health bar used, for BOSS_BONUS_SHOW_MS after a kill.
void TankFluxGame::drawBossBonus(GFXcanvas16 &canvas) {
    if (reached(_bossBonusUntil)) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "TIME BONUS +%ld", _bossBonus);
    canvas.setFont();
    canvas.setTextSize(1);
    canvas.fillRect(0, STRIP_Y - 2, W, (int)textHeight(canvas, buf) + 5, ArcadeConfig::COLOR_BLACK);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    centredText(canvas, buf, STRIP_Y);
}

// Shown once A+B have been held past QUIT_HINT_DELAY_MS; the bar fills over
// the rest of the hold.
void TankFluxGame::drawQuitHint(GFXcanvas16 &canvas) {
    if (_quitHoldStart == 0) return;
    const unsigned long delay = QUIT_HINT_DELAY_MS;
    const unsigned long total = QUIT_HOLD_MS;
    unsigned long held = millis() - _quitHoldStart;
    if (held < delay) return;
    if (held > total) held = total;
    int fillW = (int)((unsigned long)(W - 2) * (held - delay) / (total - delay));
    canvas.fillRect(0, STRIP_Y - 2, W, 12, ArcadeConfig::COLOR_BLACK);
    canvas.fillRect(1, STRIP_Y + 8, fillW, 2, ArcadeConfig::COLOR_AMBER);
    canvas.setFont();
    canvas.setTextSize(1);
    canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
    centredText(canvas, "HOLD TO QUIT", STRIP_Y - 1);
}

// Title art with a blinking start prompt and the high score in the image's
// black bottom strip.
void TankFluxGame::renderAttractGame(GFXcanvas16 &canvas) {
    for (int i = 0; i < (W * H); i++) {
        uint16_t px = pgm_read_word(&tank_flux_160x128_data[i]);
        canvas.drawPixel(i % W, i / W, px);
    }

    canvas.setFont();
    canvas.setTextSize(1);
    if (millis() % 1000 < 600) {
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        centredText(canvas, "[BTN A] TO START", 111);
    }
    char hiBuf[20];
    snprintf(hiBuf, sizeof(hiBuf), "HI: %d", _highScore);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    centredText(canvas, hiBuf, 120);
}

// 10px line pitch at text size 1 (the smallest built-in font) fits every line.
void TankFluxGame::renderAttractInfo(GFXcanvas16 &canvas) {
    canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
    canvas.setFont();
    canvas.setTextSize(1);

    canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
    canvas.setCursor(30, 4);
    canvas.print("HOW TO PLAY");

    canvas.setTextColor(ArcadeConfig::COLOR_GREY);
    canvas.setCursor(4, 18);
    canvas.print("[JOY]   DRIVE / TURN");
    canvas.setCursor(4, 28);
    canvas.print("[BTN A] FIRE (1 SHELL)");
    canvas.setCursor(4, 38);
    canvas.print("[HOLD B]+JOY STRAFE");
    canvas.setCursor(4, 48);
    canvas.print("[HOLD A+B] QUIT");

    canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
    canvas.setCursor(4, 60);
    canvas.print("GREEN CROSS = REPAIR");
    canvas.setTextColor(ArcadeConfig::COLOR_RED);
    canvas.setCursor(4, 70);
    canvas.print("RED DOTS ON RADAR = FOES");

    canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
    canvas.setCursor(4, 82);
    canvas.print("MORE TANKS EVERY FEW");
    canvas.setCursor(4, 92);
    canvas.print("KILLS -- SURVIVE!");
    canvas.setCursor(4, 102);
    canvas.print("BOSS TANK EVERY 15 KILLS");

    canvas.setTextColor(ArcadeConfig::COLOR_GREY);
    canvas.setCursor(28, 118);
    canvas.print("BEST: "); canvas.print(_highScore);
}

void TankFluxGame::renderGameOver(GFXcanvas16 &canvas) {
    canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
    canvas.setTextColor(ArcadeConfig::COLOR_RED);
    canvas.setTextSize(2);
    canvas.setCursor(W / 4 - 12, 15);
    canvas.print("DESTROYED");

    canvas.setTextSize(1);
    canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
    canvas.setCursor(W / 4, 45);
    canvas.print("SCORE: "); canvas.print(_score);

    if (_score >= _highScore && _score > 0) {
        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        canvas.setCursor(W / 4, 65);
        canvas.print("NEW HIGH SCORE!!");
    } else {
        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(W / 4, 65);
        canvas.print("BEST: "); canvas.print(_highScore);
    }

    canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
    canvas.setCursor(20, 90);
    canvas.print("[BTN A] PLAY AGAIN");
    canvas.setCursor(20, 103);
    canvas.print("[BTN B] QUIT");
}

}  // namespace tankflux
