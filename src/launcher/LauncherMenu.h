#ifndef LAUNCHER_MENU_H
#define LAUNCHER_MENU_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Preferences.h>
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

    // Settings, persisted in NVS ("cabinet": volume, music, fx) and edited
    // on the setup page, which B opens.
    float         _volume  = 0.8f;
    bool          _musicOn = true;
    bool          _fxOn    = true;
    Preferences   _prefs;

    bool _inSetup  = false;
    int  _setupSel = 0;
    // B opens/closes setup on release, but only after a press seen here:
    // a game exited by holding B would otherwise open setup on arrival.
    bool _bArmed   = false;
    bool _aArmed   = false;

    enum { SET_VOLUME, SET_MUSIC, SET_FX, SET_BACK, SET_COUNT };

    static const int VOL_STEPS = 10;
    static constexpr float VOL_STEP_SIZE = 1.0f / VOL_STEPS;

    void loadSettings() {
        _prefs.begin("cabinet", true);
        _volume  = _prefs.getFloat("volume", 0.8f);
        _musicOn = _prefs.getBool("music", true);
        _fxOn    = _prefs.getBool("fx", true);
        _prefs.end();
    }

    void saveSettings() {
        _prefs.begin("cabinet", false);
        _prefs.putFloat("volume", _volume);
        _prefs.putBool("music", _musicOn);
        _prefs.putBool("fx", _fxOn);
        _prefs.end();
    }

    void applySettings(AudioEngine &audio) {
        audio.setVolume(_volume);
        audio.setMusicEnabled(_musicOn);
        audio.setFxEnabled(_fxOn);
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

    // Nav hint, below the background art's menu box: its bottom border is
    // row 123 and the art's INSERT COIN starts at row 146, so three lines
    // fit only at a 7px pitch (124/131/138, the last ending on row 145).
    void drawHint(GFXcanvas16 &canvas, const char* a, const char* b, const char* c) {
        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        const char* lines[3] = { a, b, c };
        for (int i = 0; i < 3; i++) {
            if (!lines[i]) continue;
            canvas.setCursor(36, 124 + i * 7);
            canvas.print(lines[i]);
        }
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

    void renderSetup(GFXcanvas16 &canvas) {
        drawBackground(canvas);

        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(49, 38);
        canvas.print("SETUP");

        // VOLUME, with its bar on the line below.
        drawRow(canvas, 52, _setupSel == SET_VOLUME);
        canvas.setCursor(38, 52);
        canvas.print("VOLUME");
        canvas.drawRect(30, 63, 68, 5, ArcadeConfig::COLOR_AMBER);
        int fillW = (int)(66.0f * _volume + 0.5f);
        if (fillW > 0) {
            uint16_t fillColor = (_volume > 0.6f) ? ArcadeConfig::COLOR_GREEN
                               : (_volume > 0.3f) ? ArcadeConfig::COLOR_AMBER
                               :                    ArcadeConfig::COLOR_RED;
            canvas.fillRect(31, 64, fillW, 3, fillColor);
        }
        for (int s = 1; s < VOL_STEPS; s++) {
            canvas.drawFastVLine(31 + 66 * s / VOL_STEPS, 64, 3, ArcadeConfig::COLOR_BLACK);
        }

        drawRow(canvas, 74, _setupSel == SET_MUSIC);
        canvas.setCursor(38, 74);
        canvas.print(_musicOn ? "MUSIC  ON" : "MUSIC  OFF");

        drawRow(canvas, 86, _setupSel == SET_FX);
        canvas.setCursor(38, 86);
        canvas.print(_fxOn ? "FX     ON" : "FX     OFF");

        drawRow(canvas, 98, _setupSel == SET_BACK);
        canvas.setCursor(38, 98);
        canvas.print("BACK");

        drawHint(canvas, "[JOY] SELECT", "[BTN A] CHANGE", "[BTN B] BACK");
    }

    // One setup page frame. Left/right and A change the selected setting;
    // every change is saved at once, so nothing is lost at power-off.
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

        switch (_setupSel) {
        case SET_VOLUME:
            if (dir != 0) {
                float v = constrain(_volume + dir * VOL_STEP_SIZE, 0.0f, 1.0f);
                if (v != _volume) {
                    _volume = v;
                    audio.setVolume(_volume);
                    saveSettings();
                    audio.playTone(880, 150);
                }
            }
            break;
        case SET_MUSIC:
            if (dir != 0 || press) {
                _musicOn = !_musicOn;
                audio.setMusicEnabled(_musicOn);
                saveSettings();
                audio.playTone(_musicOn ? 880 : 440, 80);
            }
            break;
        case SET_FX:
            if (dir != 0 || press) {
                _fxOn = !_fxOn;
                audio.setFxEnabled(_fxOn);
                saveSettings();
                audio.playTone(880, 80);   // silent when just switched off
            }
            break;
        case SET_BACK:
            if (press) closeSetup(audio);
            break;
        }
    }

    void openSetup(AudioEngine &audio) {
        _inSetup = true;
        _setupSel = SET_VOLUME;
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