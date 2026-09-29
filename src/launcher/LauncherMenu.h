#ifndef LAUNCHER_MENU_H
#define LAUNCHER_MENU_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Preferences.h>
#include <Fonts/TomThumb.h>
#include "cabinet/ArcadeConfig.h"
#include "cabinet/InputManager.h"
#include "cabinet/AudioEngine.h"
#include "../assets/shared/ArcadeScreen.h"

struct GameEntry {
    const char*  name;
    CabinetState state;
};

class LauncherMenu {
private:
    int  _selection     = 0;
    bool _joyWasNeutral = true;
    bool _joyXWasNeutral = true;

    const GameEntry* _games     = nullptr;
    int              _gameCount = 0;

    unsigned long _blinkTimer = 0;
    bool          _blinkState = false;

    // Settings, persisted in NVS ("cabinet": volume, music_vol, fx_vol) and
    // edited on the setup page, which B opens. A bus at 0 is off.
    float         _volume   = 0.8f;
    float         _musicVol = 1.0f;
    float         _fxVol    = 1.0f;
    Preferences   _prefs;

    bool _inSetup  = false;
    int  _setupSel = 0;
    // B opens/closes setup on release, but only after a press seen here:
    // a game exited by holding B would otherwise open setup on arrival.
    bool _bArmed   = false;
    bool _aArmed   = false;

    enum { SET_MASTER, SET_MUSIC, SET_FX, SET_BACK, SET_COUNT };

    static const int VOL_STEPS = 10;
    static constexpr float VOL_STEP_SIZE = 1.0f / VOL_STEPS;

    void loadSettings() {
        _prefs.begin("cabinet", true);
        _volume   = _prefs.getFloat("volume", 0.8f);
        // Before the volume bars these were on/off switches: carry them over.
        _musicVol = _prefs.getFloat("music_vol", _prefs.getBool("music", true) ? 1.0f : 0.0f);
        _fxVol    = _prefs.getFloat("fx_vol", _prefs.getBool("fx", true) ? 1.0f : 0.0f);
        _prefs.end();
    }

    void saveSettings() {
        _prefs.begin("cabinet", false);
        _prefs.putFloat("volume", _volume);
        _prefs.putFloat("music_vol", _musicVol);
        _prefs.putFloat("fx_vol", _fxVol);
        _prefs.end();
    }

    void applySettings(AudioEngine &audio) {
        audio.setVolume(_volume);
        audio.setMusicVolume(_musicVol);
        audio.setFxVolume(_fxVol);
    }

    void drawBackground(GFXcanvas16 &canvas) {
        for (int i = 0; i < (ArcadeConfig::PORTRAIT_WIDTH * ArcadeConfig::PORTRAIT_HEIGHT); i++) {
            uint16_t px = pgm_read_word(&flux_arcade_128x160_data[i]);
            canvas.drawPixel(i % ArcadeConfig::PORTRAIT_WIDTH,
                            i / ArcadeConfig::PORTRAIT_WIDTH, px);
        }
    }

    // Row highlight shared by both pages.
    void drawRow(GFXcanvas16 &canvas, int yPos, bool selected) {
        if (selected) {
            uint16_t rowColor = _blinkState ? ArcadeConfig::COLOR_GREEN : 0x03E0;
            canvas.fillRect(26, yPos - 2, 76, 10, rowColor);
            canvas.setTextColor(ArcadeConfig::COLOR_BLACK);
        } else {
            canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        }
    }

    // Nav hint, below the background art's menu box, in the 5px TomThumb
    // font (as Asteroid's info screen uses), centred. Its box border is row
    // 123 and the art's INSERT COIN starts at row 146: baselines 129/138/147
    // put the glyphs (5 rows above the baseline) at 124-128, 133-137 and
    // 142-146, the last just touching the art.
    void drawHint(GFXcanvas16 &canvas, const char* a, const char* b, const char* c) {
        canvas.setFont(&TomThumb);
        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        const char* lines[3] = { a, b, c };
        for (int i = 0; i < 3; i++) {
            if (!lines[i]) continue;
            int16_t x1, y1;
            uint16_t w, h;
            canvas.getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
            canvas.setCursor((ArcadeConfig::PORTRAIT_WIDTH - (int)w) / 2 - x1, 129 + i * 9);
            canvas.print(lines[i]);
        }
        canvas.setFont();   // back to the built-in font for everything else
    }

    void renderMenu(GFXcanvas16 &canvas) {
        drawBackground(canvas);

        // Game list — rows packed tighter (12px pitch, 10px tall highlight,
        // was 15/12) so a 5th entry still lands inside the background
        // art's baked-in menu rectangle instead of spilling past it.
        for (int i = 0; i < _gameCount; i++) {
            int yPos = 38 + (i * 12);
            drawRow(canvas, yPos, i == _selection);
            canvas.setCursor(38, yPos);
            canvas.print(_games[i].name);
        }

        drawHint(canvas, "[JOY] MOVE", "[BTN A] GO", "[BTN B] SETUP");
    }

    // A label and a 10-step bar on one row, inside the row highlight.
    void drawVolumeRow(GFXcanvas16 &canvas, int y, const char* label, float v, bool selected) {
        drawRow(canvas, y, selected);
        canvas.setCursor(30, y);
        canvas.print(label);
        const int bx = 67, bw = 33;                 // bar x 67..99, inside the highlight
        canvas.fillRect(bx, y, bw, 7, ArcadeConfig::COLOR_BLACK);
        canvas.drawRect(bx, y, bw, 7, ArcadeConfig::COLOR_AMBER);
        int fillW = (int)((bw - 2) * v + 0.5f);
        if (fillW > 0) {
            uint16_t fillColor = (v > 0.6f) ? ArcadeConfig::COLOR_GREEN
                               : (v > 0.3f) ? ArcadeConfig::COLOR_AMBER
                               :              ArcadeConfig::COLOR_RED;
            canvas.fillRect(bx + 1, y + 1, fillW, 5, fillColor);
        }
        for (int s = 1; s < VOL_STEPS; s++) {
            canvas.drawFastVLine(bx + 1 + (bw - 2) * s / VOL_STEPS, y + 1, 5, ArcadeConfig::COLOR_BLACK);
        }
    }

    void renderSetup(GFXcanvas16 &canvas) {
        drawBackground(canvas);

        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(49, 38);
        canvas.print("SETUP");

        drawVolumeRow(canvas, 54, "MASTER", _volume,   _setupSel == SET_MASTER);
        drawVolumeRow(canvas, 68, "MUSIC",  _musicVol, _setupSel == SET_MUSIC);
        drawVolumeRow(canvas, 82, "FX",     _fxVol,    _setupSel == SET_FX);

        drawRow(canvas, 98, _setupSel == SET_BACK);
        canvas.setCursor(30, 98);
        canvas.print("BACK");

        drawHint(canvas, "[JOY] SELECT", "[JOY] < > SET", "[BTN B] BACK");
    }

    // One setup page frame. Left/right changes the selected level (0 is
    // off); every change is saved at once, so nothing is lost at power-off.
    void updateSetup(const InputState &input, bool press, AudioEngine &audio) {
        bool joyYActive = (input.joyUp || input.joyDown);
        if (!joyYActive) {
            _joyWasNeutral = true;
        } else if (_joyWasNeutral) {
            _joyWasNeutral = false;
            // Same (inverted) mapping as the game list.
            _setupSel += input.joyDown ? -1 : 1;
            _setupSel = (_setupSel + SET_COUNT) % SET_COUNT;
            audio.playTone(660, 40);
        }

        int dir = 0;
        bool joyXActive = (input.joyLeft || input.joyRight);
        if (!joyXActive) {
            _joyXWasNeutral = true;
        } else if (_joyXWasNeutral) {
            _joyXWasNeutral = false;
            dir = input.joyLeft ? -1 : 1;
        }

        if (_setupSel == SET_BACK) {
            if (press) closeSetup(audio);
            return;
        }
        if (dir == 0) return;
        float &level = _setupSel == SET_MASTER ? _volume
                     : _setupSel == SET_MUSIC  ? _musicVol : _fxVol;
        // Snapped to whole steps, so 0 is exactly off, not a float crumb.
        float v = constrain(roundf((level + dir * VOL_STEP_SIZE) * VOL_STEPS) / VOL_STEPS, 0.0f, 1.0f);
        if (v == level) return;
        level = v;
        applySettings(audio);
        saveSettings();
        // A beep at the new level (effects bus, so silent with FX at 0;
        // no music plays here to preview the music level with).
        audio.playTone(880, 120);
    }

    void openSetup(AudioEngine &audio) {
        _inSetup = true;
        _setupSel = SET_MASTER;
        audio.playTone(784, 60);
    }

    void closeSetup(AudioEngine &audio) {
        _inSetup = false;
        audio.playTone(523, 60);
    }

public:
    LauncherMenu() {}

    void setGames(const GameEntry* games, int count) {
        _games     = games;
        _gameCount = count;
        _selection = 0;
        loadSettings();
    }

    void onEnter(AudioEngine &audio) {
        _selection      = 0;
        _joyWasNeutral  = true;
        _joyXWasNeutral = true;
        _blinkTimer     = millis();
        _blinkState     = false;
        _inSetup        = false;
        _aArmed         = false;
        _bArmed         = false;
        applySettings(audio);
        audio.playTone(523, 80);
    }

    CabinetState update(GFXcanvas16 &canvas,
                        const InputState &input,
                        AudioEngine &audio) {
        if (_games == nullptr || _gameCount == 0) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            canvas.setTextColor(ArcadeConfig::COLOR_RED);
            canvas.setCursor(10, 70);
            canvas.print("NO GAMES FOUND");
            return STATE_LAUNCHER_MENU;
        }

        // --- Blink ---
        if (millis() - _blinkTimer > 400) {
            _blinkState = !_blinkState;
            _blinkTimer = millis();
        }

        // Buttons act on release, and only after a press seen here (see
        // _bArmed), so a held button can't carry over from a game.
        if (input.btnAPressed) _aArmed = true;
        if (input.btnBPressed) _bArmed = true;
        const bool aTap = input.btnAReleased && _aArmed;
        const bool bTap = input.btnBReleased && _bArmed;
        if (input.btnAReleased) _aArmed = false;
        if (input.btnBReleased) _bArmed = false;

        if (_inSetup) {
            if (bTap) closeSetup(audio);
            else      updateSetup(input, aTap, audio);
            if (_inSetup) renderSetup(canvas);
            else          renderMenu(canvas);
            return STATE_LAUNCHER_MENU;
        }

        if (bTap) {
            openSetup(audio);
            renderSetup(canvas);
            return STATE_LAUNCHER_MENU;
        }

        // --- Y axis: menu navigation ---
        bool joyYActive = (input.joyUp || input.joyDown);
        if (!joyYActive) {
            _joyWasNeutral = true;
        } else if (_joyWasNeutral) {
            _joyWasNeutral = false;
            if (input.joyDown) {
                _selection--;
                if (_selection < 0) _selection = _gameCount - 1;
                audio.playTone(660, 40);
            } else if (input.joyUp) {
                _selection++;
                if (_selection >= _gameCount) _selection = 0;
                audio.playTone(660, 40);
            }
        }

        // --- Launch ---
        if (aTap && _gameCount > 0) {
            Serial.printf("[LAUNCHER] Launching: %s\n", _games[_selection].name);
            audio.playLaunchMelody();
            return _games[_selection].state;
        }

        renderMenu(canvas);
        return STATE_LAUNCHER_MENU;
    }
};

#endif // LAUNCHER_MENU_H