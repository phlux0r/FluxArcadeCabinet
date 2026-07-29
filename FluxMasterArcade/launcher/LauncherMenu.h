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
    bool _joyXWasNeutral = true;   // For volume X-axis debounce

    const GameEntry* _games     = nullptr;
    int              _gameCount = 0;

    unsigned long _blinkTimer = 0;
    bool          _blinkState = false;

    // Volume — persisted in NVS
    float         _volume     = 0.8f;
    Preferences   _prefs;

    // Volume bar layout — plain values to avoid static const init issues
    // Portrait screen is 128x160. Bar sits in bottom 18px.
    static const int VOL_BAR_X  = 8;
    static const int VOL_BAR_Y  = 142;   // 160 - 18
    static const int VOL_BAR_W  = 112;   // 128 - 16
    static const int VOL_BAR_H  = 6;
    static const int VOL_STEPS  = 10;
    static constexpr float VOL_STEP_SIZE = 1.0f / VOL_STEPS;

    void loadVolume() {
        _prefs.begin("cabinet", true);
        _volume = _prefs.getFloat("volume", 0.8f);
        _prefs.end();
    }

    void saveVolume() {
        _prefs.begin("cabinet", false);
        _prefs.putFloat("volume", _volume);
        _prefs.end();
    }

    void renderMenu(GFXcanvas16 &canvas) {
        // Background image
        for (int i = 0; i < (ArcadeConfig::PORTRAIT_WIDTH * ArcadeConfig::PORTRAIT_HEIGHT); i++) {
            uint16_t px = pgm_read_word(&flux_arcade_128x160_data[i]);
            canvas.drawPixel(i % ArcadeConfig::PORTRAIT_WIDTH,
                            i / ArcadeConfig::PORTRAIT_WIDTH, px);
        }

        // Game list
        for (int i = 0; i < _gameCount; i++) {
            int yPos = 36 + (i * 15);
            if (i == _selection) {
                uint16_t rowColor = _blinkState ? ArcadeConfig::COLOR_GREEN : 0x03E0;
                canvas.fillRect(26, yPos - 2, 76, 12, rowColor);
                canvas.setTextColor(ArcadeConfig::COLOR_BLACK);
            } else {
                canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            }
            canvas.setCursor(38, yPos);
            canvas.print(_games[i].name);
        }

        // Nav hint
        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(36, 100);
        canvas.print("[JOY] MOVE");
        canvas.setCursor(36, 110);
        canvas.print("[BTN A] GO");

        // Volume strip (moved up 20px)
        //canvas.fillRect(0, 120, 148, 20, 0x1082);
        //canvas.drawFastHLine(0, 120, 128, ArcadeConfig::COLOR_AMBER);

        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(2, 150);
        canvas.print("VOL");

        canvas.drawRect(24, 153, 100, 4, ArcadeConfig::COLOR_AMBER);

        int fillW = (int)(98.0f * _volume);
        if (fillW > 0) {
            uint16_t fillColor = (_volume > 0.6f) ? ArcadeConfig::COLOR_GREEN
                            : (_volume > 0.3f) ? ArcadeConfig::COLOR_AMBER
                            :                    ArcadeConfig::COLOR_RED;
            canvas.fillRect(25, 154, fillW, 2, fillColor);
        }

        for (int s = 1; s < 10; s++) {
            int tx = 25 + (int)(98.0f * s / 10.0f);
            canvas.drawFastVLine(tx, 154, 2, ArcadeConfig::COLOR_BLACK);
        }
    }

public:
    LauncherMenu() {}

    void setGames(const GameEntry* games, int count) {
        _games     = games;
        _gameCount = count;
        _selection = 0;
        loadVolume();
    }

    void onEnter(AudioEngine &audio) {
        _selection      = 0;
        _joyWasNeutral  = true;
        _joyXWasNeutral = true;
        _blinkTimer     = millis();
        _blinkState     = false;
        audio.setVolume(_volume);
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

        // --- X axis: volume adjustment ---
        bool joyXActive = (input.joyLeft || input.joyRight);
        if (!joyXActive) {
            _joyXWasNeutral = true;
        } else if (_joyXWasNeutral) {
            _joyXWasNeutral = false;
            float newVol = _volume;
            if (input.joyLeft)  newVol -= VOL_STEP_SIZE;
            if (input.joyRight) newVol += VOL_STEP_SIZE;
            newVol = constrain(newVol, 0.0f, 1.0f);
            if (newVol != _volume) {
                _volume = newVol;
                audio.setVolume(_volume);
                saveVolume();
                audio.mute();  // Stop anything playing so tone can fire
                audio.playTone(880, 150);
            }
        }

        // --- Launch ---
        if (input.btnAReleased && _gameCount > 0) {
            Serial.printf("[LAUNCHER] Launching: %s\n", _games[_selection].name);
            audio.playLaunchMelody();
            return _games[_selection].state;
        }

        renderMenu(canvas);
        return STATE_LAUNCHER_MENU;
    }
};

#endif // LAUNCHER_MENU_H