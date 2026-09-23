#ifndef PLAYER_SHIP_H
#define PLAYER_SHIP_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

// --- 16x10 SIDE-VIEW SHIP BITMAP (16-bit RGB565 Colors) ---
#define W ST7735_WHITE
#define B ST7735_BLACK
#define C ST7735_CYAN
#define L ST7735_BLUE
#define O ST7735_ORANGE
#define R ST7735_RED
#define M ST7735_MAGENTA

// Ship dimensions — landscape screen (160x128)
static const int SHIP_WIDTH        = 16;
static const int SHIP_HEIGHT       = 10;
static const int SHIP_UI_MARGIN    = 11;   // Top HUD bar height
static const int SHIP_ANIM_SPEED   = 80;   // ms per animation frame
static const int SHIELD_DURATION   = 10000; // ms

class PlayerShip {
private:
    float _x;
    int   _y;
    int   _currentFrame;
    unsigned long _nextFrameTime;
    bool  _shieldActive;
    unsigned long _shieldEndTime;

    // 3-frame thruster animation — unchanged from original
    const uint16_t _frames[3][160] = {
        {
            B,B,B,B,B,B,B,B,W,W,B,B,B,B,B,B,
            B,B,B,B,B,B,W,W,L,L,W,B,B,B,B,B,
            B,B,B,B,W,W,C,C,L,L,L,W,B,B,B,B,
            O,B,B,W,W,W,W,W,W,W,W,W,W,B,B,B,
            O,O,R,L,L,L,L,M,M,M,W,W,W,W,W,B,
            O,O,R,L,L,L,L,M,M,M,W,W,W,W,W,B,
            O,B,B,W,W,W,W,W,W,W,W,W,W,B,B,B,
            B,B,B,B,W,W,C,C,L,L,L,W,B,B,B,B,
            B,B,B,B,B,B,W,W,L,L,W,B,B,B,B,B,
            B,B,B,B,B,B,B,B,W,W,B,B,B,B,B,B
        },
        {
            B,B,B,B,B,B,B,B,W,W,B,B,B,B,B,B,
            B,B,B,B,B,B,W,W,L,L,W,B,B,B,B,B,
            B,B,B,B,W,W,C,C,L,L,L,W,B,B,B,B,
            B,B,B,W,W,W,W,W,W,W,W,W,W,B,B,B,
            R,O,O,L,L,L,L,M,M,M,W,W,W,W,W,B,
            R,O,O,L,L,L,L,M,M,M,W,W,W,W,W,B,
            B,B,B,W,W,W,W,W,W,W,W,W,W,B,B,B,
            B,B,B,B,W,W,C,C,L,L,L,W,B,B,B,B,
            B,B,B,B,B,B,W,W,L,L,W,B,B,B,B,B,
            B,B,B,B,B,B,B,B,W,W,B,B,B,B,B,B
        },
        {
            B,B,B,B,B,B,B,B,W,W,B,B,B,B,B,B,
            B,B,B,B,B,B,W,W,L,L,W,B,B,B,B,B,
            B,B,B,B,W,W,C,C,L,L,L,W,B,B,B,B,
            R,B,B,W,W,W,W,W,W,W,W,W,W,B,B,B,
            O,R,B,L,L,L,L,M,M,M,W,W,W,W,W,B,
            O,R,B,L,L,L,L,M,M,M,W,W,W,W,W,B,
            R,B,B,W,W,W,W,W,W,W,W,W,W,B,B,B,
            B,B,B,B,W,W,C,C,L,L,L,W,B,B,B,B,
            B,B,B,B,B,B,W,W,L,L,W,B,B,B,B,B,
            B,B,B,B,B,B,B,B,W,W,B,B,B,B,B,B
        }
    };

public:
    PlayerShip() : _x(15.0f), _y(64), _currentFrame(0),
                   _nextFrameTime(0), _shieldActive(false), _shieldEndTime(0) {}

    void reset() {
        _y            = ArcadeConfig::LANDSCAPE_HEIGHT / 2;
        _shieldActive = false;
    }

    // Y position driven by accumulated offset from AsteroidFluxGame
    // (same velocity-based approach as X axis)
    void updatePosition(float joyY) {
        // No-op: Y is now driven externally via setY() just like X via setX()
        // Kept for API compatibility
    }

    void setY(int y) {
        _y = constrain(y, SHIP_UI_MARGIN, ArcadeConfig::LANDSCAPE_HEIGHT - SHIP_HEIGHT - 1);
    }

    void updateAnimation() {
        if (millis() >= _nextFrameTime) {
            _currentFrame  = (_currentFrame + 1) % 3;
            _nextFrameTime = millis() + SHIP_ANIM_SPEED;
        }
    }

    void activateShield() {
        _shieldActive  = true;
        _shieldEndTime = millis() + SHIELD_DURATION;
    }

    void updateShield() {
        if (_shieldActive && millis() >= _shieldEndTime) {
            _shieldActive = false;
        }
    }

    void deactivateShield() { _shieldActive = false; }

    float getX()          const { return _x; }
    int   getY()          const { return _y; }
    void  setX(float x)         { _x = x; }
    bool  isShieldActive() const { return _shieldActive; }

    void render(GFXcanvas16 &canvas) {
        // Shield bubble — unchanged from original
        if (_shieldActive) {
            int cx = (int)_x + (SHIP_WIDTH  / 2);
            int cy = _y      + (SHIP_HEIGHT / 2);
            int r  = 14;

            uint16_t primaryColor   = ArcadeConfig::COLOR_CYAN;
            uint16_t secondaryColor = ArcadeConfig::COLOR_ION_BLUE;

            unsigned long timeLeft = (_shieldEndTime > millis())
                                     ? (_shieldEndTime - millis()) : 0;
            if (timeLeft < 2000 && (millis() / 50) % 2 == 0) {
                primaryColor   = ArcadeConfig::COLOR_RED;
                secondaryColor = ArcadeConfig::COLOR_ORANGE;
            }

            if ((millis() / 80) % 2 == 0) {
                canvas.drawCircle(cx, cy, r,     primaryColor);
                canvas.drawCircle(cx, cy, r - 1, secondaryColor);
            }
        }

        canvas.drawRGBBitmap((int)_x, _y, _frames[_currentFrame],
                             SHIP_WIDTH, SHIP_HEIGHT);
    }
};

// Clean up local colour macros so they don't leak into other headers
#undef W
#undef B
#undef C
#undef L
#undef O
#undef R
#undef M

#endif // PLAYER_SHIP_H