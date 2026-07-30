#ifndef PLAYER_RUNNER_H
#define PLAYER_RUNNER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "assets/runner_sprites.h"

// --- 18x20 SIDE-VIEW RUNNER SPRITE (16-bit RGB565, PROGMEM) ---
// Generated from assets/frame1.png, frame2.png, frame3.png. Frame 1/2 are
// the run cycle (legs alternating), frame 3 is the jump/airborne pose.
// Drawn pixel-by-pixel (not drawRGBBitmap) so transparent source pixels —
// encoded as RUNNER_TRANSPARENT_KEY — are skipped instead of boxing the
// sprite in a solid rectangle.
static const int RUNNER_WIDTH      = 18;
static const int RUNNER_HEIGHT     = 20;
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

    const uint16_t* frameData(int frame) const {
        switch (frame) {
            case 0:  return runner_frame_run1;
            case 1:  return runner_frame_run2;
            default: return runner_frame_jump;
        }
    }

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

        const uint16_t* data = frameData(_currentFrame);
        int baseX = (int)_x, baseY = (int)_y;
        for (int row = 0; row < RUNNER_HEIGHT; row++) {
            for (int col = 0; col < RUNNER_WIDTH; col++) {
                uint16_t px = pgm_read_word(&data[row * RUNNER_WIDTH + col]);
                if (px == RUNNER_TRANSPARENT_KEY) continue;
                canvas.drawPixel(baseX + col, baseY + row, px);
            }
        }
    }
};

#endif // PLAYER_RUNNER_H
