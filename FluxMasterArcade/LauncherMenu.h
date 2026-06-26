#ifndef LAUNCHER_MENU_H
#define LAUNCHER_MENU_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "ArcadeConfig.h"

class LauncherMenu {
private:
    int _currentSelection = 0;
    const int _totalGames = 2;
    const char* _gameTitles[2] = {"1. Asteroid Flux", "2. Lander Flux"};
    
    unsigned long _lastInputTime = 0;
    bool _buttonWasDown = false;

public:
    LauncherMenu() {}

    void init() {
        pinMode(ArcadeConfig::BUTTON_A, INPUT_PULLUP);
        pinMode(ArcadeConfig::BUTTON_B, INPUT_PULLUP);
    }

    // Returns the selected state if a game is launched, otherwise returns STATE_LAUNCHER_MENU
    GameState update(GFXcanvas16 &canvas) {
        unsigned long now = millis();
        
        // 1. Read Joystick Y-Axis (GPIO 17) for vertical menu scrolling
        int joyY = analogRead(ArcadeConfig::JOY_Y);
        
        // Debounce directional changes by forcing a 200ms delay between menu jumps
        if (now - _lastInputTime > 200) {
            if (joyY < 1000) { // Pushed Up
                _currentSelection--;
                if (_currentSelection < 0) _currentSelection = _totalGames - 1;
                _lastInputTime = now;
            } 
            else if (joyY > 3000) { // Pushed Down
                _currentSelection++;
                if (_currentSelection >= _totalGames) _currentSelection = 0;
                _lastInputTime = now;
            }
        }

        // 2. Read Button A (GPIO 4) on Release Edge to launch the game
        bool isButtonDown = (digitalRead(ArcadeConfig::BUTTON_A) == LOW);
        bool launchTriggered = false;

        if (isButtonDown) {
            _buttonWasDown = true;
        } else {
            if (_buttonWasDown) {
                _buttonWasDown = false; // Clear latch
                launchTriggered = true;
            }
        }

        // 3. Render the Arcade Selection Interface
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        
        // Draw Header Border
        canvas.drawRect(2, 2, ArcadeConfig::SCREEN_WIDTH - 4, ArcadeConfig::SCREEN_HEIGHT - 4, ArcadeConfig::COLOR_AMBER);
        canvas.drawFastHLine(2, 25, ArcadeConfig::SCREEN_WIDTH - 4, ArcadeConfig::COLOR_AMBER);
        
        // Title Text
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(20, 10);
        canvas.print("FLUX CABINET OS");
        
        // Render Game List
        for (int i = 0; i < _totalGames; i++) {
            int yPos = 50 + (i * 25);
            
            if (i == _currentSelection) {
                // Highlighted row
                canvas.fillRect(6, yPos - 4, ArcadeConfig::SCREEN_WIDTH - 12, 16, ArcadeConfig::COLOR_GREEN);
                canvas.setTextColor(ArcadeConfig::COLOR_BLACK);
            } else {
                canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            }
            
            canvas.setCursor(12, yPos);
            canvas.print(_gameTitles[i]);
        }

        // Footer instructions
        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(10, 140);
        canvas.print("[JOY] Scroll Menu");
        canvas.setCursor(10, 150);
        canvas.print("[BTN A] Launch Game");

        // 4. Handle State Switching
        if (launchTriggered) {
            Serial.print("[LAUNCHER] Launching: ");
            Serial.println(_gameTitles[_currentSelection]);
            if (_currentSelection == 0) return STATE_ASTEROID_FLUX;
            if (_currentSelection == 1) return STATE_LANDER_FLUX;
        }

        return STATE_LAUNCHER_MENU;
    }
};

#endif