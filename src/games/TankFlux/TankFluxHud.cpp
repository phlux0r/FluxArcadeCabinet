#include "TankFluxGame.h"
#include "assets/TitleScreen.h"

namespace tankflux {

void TankFluxGame::drawHUD(GFXcanvas16 &canvas) {
    canvas.fillRect(0, 0, ArcadeConfig::LANDSCAPE_WIDTH, 10, ArcadeConfig::COLOR_BLACK);
    canvas.drawFastHLine(0, 10, ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::COLOR_GREEN);

    canvas.setFont();
    canvas.setTextSize(1);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    canvas.setCursor(4, 1);
    canvas.print("SCORE:"); canvas.print(_score);

    canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
    canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 2 - 6, 1);
    canvas.print("LV"); canvas.print(_level);

    canvas.setTextColor(ArcadeConfig::COLOR_GREY);
    canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 54, 1);
    canvas.print("HI:"); canvas.print(_highScore);

    // Boss health bar: a second, wider bar directly under the main HUD
    // strip, only while a boss fight is on. Red so it reads as "the
    // threat," distinct from the player's own green/amber/red bar.
    if (_bossActive) {
        const int bx = 3, by = 13, bw = ArcadeConfig::LANDSCAPE_WIDTH - 6, bh = 6;
        canvas.drawRect(bx, by, bw, bh, ArcadeConfig::COLOR_GREY);
        int bossFillW = ((bw - 2) * _boss.hp) / max(1, _boss.maxHp);
        if (bossFillW > 0) {
            canvas.fillRect(bx + 1, by + 1, bossFillW, bh - 2, ArcadeConfig::COLOR_RED);
        }
    }

    // Health bar sits bottom-left, stopping short of centre so it doesn't
    // run across the gun barrel.
    const int barX = 3, barY = ArcadeConfig::LANDSCAPE_HEIGHT - 7;
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

// A visible sun disc, projected from the same _sun direction that
// actually lights the world — same idea as the LensFlare in Jet's
// Tropical Island example, but drawn directly with plain 2D circles
// (matching the muzzle-flash pattern just below) rather than pulling in
// Jet's Sprite2D/LensFlare/pick-query machinery: this project doesn't
// enable TEXTURE_MAPPING or MAX_PICK_QUERIES, and a single fixed light
// never needs occlusion fading or a multi-element flare chain. Drawn
// straight onto the canvas after the 3D scene, so it paints over the
// sky gradient — clipped to stay above the horizon so it can't paint
// over ground geometry it should logically be behind.
void TankFluxGame::drawSun(GFXcanvas16 &canvas) {
    Vector3 viewDir = _camera.transformDirection(_sunVisual.worldLightDir);
    if (viewDir.z <= 0) return;   // behind the camera
    const float invZ = _camera.fovFactor / (float)viewDir.z;
    const int sx = canvas.width()  / 2 + (int)(viewDir.x * invZ);
    const int sy = canvas.height() / 2 - (int)(viewDir.y * invZ);
    if (sy >= canvas.height() / 2 - 6) return;   // at/below the horizon
    if (sy < 12) return;   // drawHUD's top bar (rows 0-9) paints over this anyway
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

// The barrel is drawn in 2D rather than as a 3D object because it's
// rigidly bolted to the vehicle the camera sits inside — it should never
// move relative to the screen, so a screen-space shape is both simpler
// and strictly more correct than positioning a mesh in front of the
// camera every frame.
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

    if ((long)(_muzzleFlashUntil - millis()) > 0) {
        canvas.fillCircle(cx, tipY - 2, 5, ArcadeConfig::COLOR_YELLOW);
        canvas.fillCircle(cx, tipY - 2, 2, ArcadeConfig::COLOR_WHITE);
    }
}

// Rotated radar: forward is always up, so a blip's position on the dial
// is the direction you need to turn. Without this, an attacker that
// slides out of frame is simply lost — which is the single hardest
// thing to deal with in a first-person game on a 160x128 screen.
// Shows the whole arena, deliberately beyond visual range, so shells
// arriving out of the haze still have a visible source.
void TankFluxGame::drawRadar(GFXcanvas16 &canvas) {
    const int r  = 20;
    const int cx = canvas.width() - r - 5;
    const int cy = canvas.height() - r - 5;
    const float scale = (float)r / (float)RADAR_RANGE;

    canvas.fillCircle(cx, cy, r, rgb565(2, 6, 4));
    canvas.drawCircle(cx, cy, r, ArcadeConfig::COLOR_GREY);
    // Bow marker, so "up" is unambiguous.
    canvas.drawFastVLine(cx, cy - r, 4, ArcadeConfig::COLOR_GREY);

    const float hr = radians(_headingDeg);
    const float sh = sinf(hr), ch = cosf(hr);

    auto plot = [&](float wx, float wz, uint16_t colour, bool big) {
        float dx = wx - _x, dz = wz - _z;
        // Into tank-local space: forward = (sin h, cos h), right = (cos h, -sin h).
        float right   = dx * ch - dz * sh;
        float forward = dx * sh + dz * ch;
        float px = right * scale;
        float py = -forward * scale;
        // Clamp to the rim rather than dropping it, so something out of
        // range still tells you which way it is.
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
        if (_kits[i].active) plot((float)REPAIRS[i].x, (float)REPAIRS[i].z,
                                  ArcadeConfig::COLOR_GREEN, false);
    }
    for (const auto &e : _enemies) {
        if (e.alive) plot(e.x, e.z, ArcadeConfig::COLOR_RED, true);
    }
    if (_bossActive) {
        // Distinct from the regular red dots and blinking, so the boss
        // reads as a different kind of threat on the dial, not just
        // another enemy — same 300ms on/off cadence as drawBossAlert().
        uint16_t bossColour = (millis() % 300 < 150) ? ArcadeConfig::COLOR_MAGENTA
                                                      : ArcadeConfig::COLOR_RED;
        plot(_boss.x, _boss.z, bossColour, true);
    }

    canvas.drawPixel(cx, cy, ArcadeConfig::COLOR_WHITE);
}

// A red border rather than a full-screen tint: at 160x128 a full flash
// hides the very thing you need to see after being hit.
void TankFluxGame::drawDamageFlash(GFXcanvas16 &canvas) {
    if ((long)(_damageFlashUntil - millis()) <= 0) return;
    const int w = canvas.width(), h = canvas.height();
    for (int i = 0; i < 3; ++i) {
        canvas.drawRect(i, 11 + i, w - i * 2, h - 11 - i * 2, ArcadeConfig::COLOR_RED);
    }
}

// regenerateArena()'s visual half of the transition cue — same
// concentric-rect trick as drawDamageFlash(), but green (a positive
// event, not a threat) and one ring wider so it doesn't read as a copy
// of the damage flash.
void TankFluxGame::drawArenaShiftFlash(GFXcanvas16 &canvas) {
    if ((long)(_arenaShiftFlashUntil - millis()) <= 0) return;
    const int w = canvas.width(), h = canvas.height();
    for (int i = 0; i < 4; ++i) {
        canvas.drawRect(i, 11 + i, w - i * 2, h - 11 - i * 2, ArcadeConfig::COLOR_GREEN);
    }
}

// Shown between the boss trigger and its spawn. Directly under the HUD
// rather than screen-centre, so it doesn't cover the gunsight while
// regular enemies are still around. The bar itself flashes red/black
// (text stays visible in both phases) because small blinking text on
// its own was easy to miss mid-fight.
void TankFluxGame::drawBossAlert(GFXcanvas16 &canvas) {
    if (!_bossPending || (long)(millis() - _bossAlertUntil) >= 0) return;
    const char* msg = "BOSS ALERT";
    canvas.setFont();
    canvas.setTextSize(2);
    int16_t tbx, tby; uint16_t tbw, tbh;
    canvas.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);
    const int y = 14;   // HUD divider is at y=10
    bool on = millis() % 400 < 200;
    canvas.fillRect(0, y - 3, ArcadeConfig::LANDSCAPE_WIDTH, (int)tbh + 5,
                    on ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_BLACK);
    canvas.setTextColor(on ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_RED);
    canvas.setCursor((ArcadeConfig::LANDSCAPE_WIDTH - (int16_t)tbw) / 2, y);
    canvas.print(msg);
    canvas.setTextSize(1);
}

// Shown for BOSS_BONUS_SHOW_MS after a boss kill, in the strip the
// boss health bar used during the fight.
void TankFluxGame::drawBossBonus(GFXcanvas16 &canvas) {
    if ((long)(millis() - _bossBonusUntil) >= 0) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "TIME BONUS +%ld", _bossBonus);
    canvas.setFont();
    canvas.setTextSize(1);
    int16_t tbx, tby; uint16_t tbw, tbh;
    canvas.getTextBounds(buf, 0, 0, &tbx, &tby, &tbw, &tbh);
    const int y = 13;
    canvas.fillRect(0, y - 2, ArcadeConfig::LANDSCAPE_WIDTH, (int)tbh + 5, ArcadeConfig::COLOR_BLACK);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    canvas.setCursor((ArcadeConfig::LANDSCAPE_WIDTH - (int16_t)tbw) / 2, y);
    canvas.print(buf);
}

// Feedback while A+B is held, so a quit doesn't come as a surprise and
// an accidental press is obvious. Same strip as drawBossAlert().
void TankFluxGame::drawQuitHint(GFXcanvas16 &canvas) {
    if (_quitHoldStart == 0) return;
    const unsigned long delay = QUIT_HINT_DELAY_MS;
    const unsigned long total = QUIT_HOLD_MS;
    unsigned long held = millis() - _quitHoldStart;
    if (held < delay) return;
    if (held > total) held = total;
    const int y = 13;
    const int w = ArcadeConfig::LANDSCAPE_WIDTH;
    // Bar fills over the visible part of the hold, not the whole 2s.
    int fillW = (int)((unsigned long)(w - 2) * (held - delay) / (total - delay));
    canvas.fillRect(0, y - 2, w, 12, ArcadeConfig::COLOR_BLACK);
    canvas.fillRect(1, y + 8, fillW, 2, ArcadeConfig::COLOR_AMBER);
    canvas.setFont();
    canvas.setTextSize(1);
    canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
    const char* msg = "HOLD TO QUIT";
    int16_t tbx, tby; uint16_t tbw, tbh;
    canvas.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);
    canvas.setCursor((w - (int16_t)tbw) / 2, y - 1);
    canvas.print(msg);
}

// Real title art (assets/TitleScreen.h) replaces the old placeholder
// (a code-drawn tank icon + "TANK FLUX" text) now that it exists —
// same full-screen PROGMEM blit every other game's splash uses. The
// source image's own bottom 20px are solid black, left there
// deliberately for this HUD text: start prompt and high score,
// centred, blinking prompt at the same 600ms-on/400ms-off cadence
// Maze/Lander's own splash screens use.
void TankFluxGame::renderAttractGame(GFXcanvas16 &canvas) {
    for (int i = 0; i < (ArcadeConfig::LANDSCAPE_WIDTH * ArcadeConfig::LANDSCAPE_HEIGHT); i++) {
        uint16_t px = pgm_read_word(&tank_flux_160x128_data[i]);
        canvas.drawPixel(i % ArcadeConfig::LANDSCAPE_WIDTH,
                         i / ArcadeConfig::LANDSCAPE_WIDTH, px);
    }

    canvas.setFont();
    canvas.setTextSize(1);
    int16_t tbx, tby; uint16_t tbw, tbh;

    if (millis() % 1000 < 600) {
        const char* prompt = "[BTN A] TO START";
        canvas.getTextBounds(prompt, 0, 0, &tbx, &tby, &tbw, &tbh);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor((ArcadeConfig::LANDSCAPE_WIDTH - (int16_t)tbw) / 2, 111);
        canvas.print(prompt);
    }

    char hiBuf[20];
    snprintf(hiBuf, sizeof(hiBuf), "HI: %d", _highScore);
    canvas.getTextBounds(hiBuf, 0, 0, &tbx, &tby, &tbw, &tbh);
    canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
    canvas.setCursor((ArcadeConfig::LANDSCAPE_WIDTH - (int16_t)tbw) / 2, 120);
    canvas.print(hiBuf);
}

// 10px line pitch at setTextSize(1) (the smallest built-in font) so
// every line fits in 128px without dropping any.
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

}  // namespace tankflux
