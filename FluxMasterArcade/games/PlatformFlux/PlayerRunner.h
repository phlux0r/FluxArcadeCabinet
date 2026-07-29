#ifndef PLAYER_RUNNER_H
#define PLAYER_RUNNER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

// --- 12x14 SIDE-VIEW RUNNER BITMAP (16-bit RGB565 Colors) ---
// Same sprite-sheet approach as AsteroidFlux's PlayerShip: fixed-size
// PROGMEM-style const arrays swapped on a timer to fake motion.
#define W ST7735_WHITE
#define B ST7735_BLACK
#define S 0xC618   // skin tone (light grey-tan, placeholder)
#define C ST7735_CYAN
#define O ST7735_ORANGE

static const int RUNNER_WIDTH      = 12;
static const int RUNNER_HEIGHT     = 14;
static const int RUNNER_ANIM_SPEED = 90;   // ms per animation frame while grounded

class PlayerRunner {
private:
    float _x;
    float _y;          // top-left Y, in canvas space
    float _vy;
    bool  _onGround;
    int   _currentFrame;   // 0/1 = run cycle, 2 = jump/airborne
    unsigned long _nextFrameTime;
    bool  _invincible;
    unsigned long _invincibleEndTime;

    // Frame 0: legs forward — Frame 1: legs back — Frame 2: airborne (tucked)
    const uint16_t _frames[3][168] = {
        {
            B,B,B,B,W,W,W,W,B,B,B,B,
            B,B,B,W,W,W,W,W,W,B,B,B,
            B,B,B,W,S,S,S,S,W,B,B,B,
            B,B,B,W,S,S,S,S,W,B,B,B,
            B,B,B,B,W,W,W,W,B,B,B,B,
            B,B,O,O,C,C,C,C,O,O,B,B,
            B,O,O,O,C,C,C,C,O,O,O,B,
            B,O,O,B,C,C,C,C,B,O,O,B,
            B,B,O,B,C,C,C,C,B,O,B,B,
            B,B,B,B,C,C,C,C,B,B,B,B,
            B,B,B,S,S,B,B,S,S,B,B,B,
            B,B,S,S,B,B,B,B,S,S,B,B,
            B,S,S,B,B,B,B,B,B,S,S,B,
            W,W,B,B,B,B,B,B,B,B,W,W
        },
        {
            B,B,B,B,W,W,W,W,B,B,B,B,
            B,B,B,W,W,W,W,W,W,B,B,B,
            B,B,B,W,S,S,S,S,W,B,B,B,
            B,B,B,W,S,S,S,S,W,B,B,B,
            B,B,B,B,W,W,W,W,B,B,B,B,
            B,B,O,O,C,C,C,C,O,O,B,B,
            B,O,O,O,C,C,C,C,O,O,O,B,
            B,O,O,B,C,C,C,C,B,O,O,B,
            B,B,O,B,C,C,C,C,B,O,B,B,
            B,B,B,B,C,C,C,C,B,B,B,B,
            B,B,S,S,B,B,B,B,B,S,S,B,
            B,S,S,B,B,B,B,B,B,B,S,S,
            S,S,B,B,B,B,B,B,B,B,B,S,
            B,B,B,B,B,B,B,B,B,W,W,B
        },
        {
            B,B,B,B,W,W,W,W,B,B,B,B,
            B,B,B,W,W,W,W,W,W,B,B,B,
            B,B,B,W,S,S,S,S,W,B,B,B,
            B,B,B,W,S,S,S,S,W,B,B,B,
            B,B,B,B,W,W,W,W,B,B,B,B,
            B,B,O,O,C,C,C,C,O,O,B,B,
            B,O,O,O,C,C,C,C,O,O,O,B,
            B,B,O,B,C,C,C,C,B,O,B,B,
            B,B,B,S,C,C,C,C,S,B,B,B,
            B,B,S,S,B,B,B,B,S,S,B,B,
            B,S,S,B,B,B,B,B,B,S,S,B,
            B,B,B,B,B,B,B,B,B,B,B,B,
            B,B,B,B,B,B,B,B,B,B,B,B,
            B,B,B,B,B,B,B,B,B,B,B,B
        }
    };

public:
    PlayerRunner() : _x(30.0f), _y(0.0f), _vy(0.0f), _onGround(true),
                     _currentFrame(0), _nextFrameTime(0),
                     _invincible(false), _invincibleEndTime(0) {}

    void reset(float x, float groundY) {
        _x        = x;
        _y        = groundY - RUNNER_HEIGHT;
        _vy       = 0.0f;
        _onGround = true;
        _currentFrame = 0;
        _invincible = false;
    }

    void jump() {
        if (_onGround) {
            _vy = -ArcadeConfig::RUNNER_JUMP_VELOCITY;
            _onGround = false;
        }
    }

    // groundY is the top surface Y of whatever platform is currently beneath
    // the player (or SCREEN_HEIGHT if there is none — i.e. falling to death).
    void updatePhysics(float groundY) {
        _vy += ArcadeConfig::RUNNER_GRAVITY;
        _y  += _vy;

        if (_y + RUNNER_HEIGHT >= groundY && _vy >= 0.0f) {
            _y = groundY - RUNNER_HEIGHT;
            _vy = 0.0f;
            _onGround = true;
        } else {
            _onGround = false;
        }
    }

    void updateAnimation() {
        if (!_onGround) {
            _currentFrame = 2;
            return;
        }
        if (millis() >= _nextFrameTime) {
            _currentFrame  = (_currentFrame == 0) ? 1 : 0;
            _nextFrameTime = millis() + RUNNER_ANIM_SPEED;
        }
    }

    void activateInvincibility(unsigned long durationMs) {
        _invincible        = true;
        _invincibleEndTime = millis() + durationMs;
    }

    void updateInvincibility() {
        if (_invincible && millis() >= _invincibleEndTime) _invincible = false;
    }

    void setX(float x) { _x = x; }

    float getX() const { return _x; }
    float getY() const { return _y; }
    bool  isOnGround() const { return _onGround; }
    bool  isInvincible() const { return _invincible; }
    bool  isFalling() const { return _vy > 0.0f; }

    void render(GFXcanvas16 &canvas) {
        // Blink while invincible, same visual language as AsteroidFlux's shield
        if (_invincible && (millis() / 80) % 2 == 0) return;
        canvas.drawRGBBitmap((int)_x, (int)_y, _frames[_currentFrame],
                             RUNNER_WIDTH, RUNNER_HEIGHT);
    }
};

#undef W
#undef B
#undef S
#undef C
#undef O

#endif // PLAYER_RUNNER_H
