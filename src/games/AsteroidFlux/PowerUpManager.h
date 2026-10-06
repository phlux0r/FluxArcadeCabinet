#ifndef POWERUP_MANAGER_H
#define POWERUP_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "PlayerShip.h"
#include "../../cabinet/AudioEngine.h"
#include "AsteroidManager.h" // Linked to access speed controls

enum PowerUpType { NONE, EXTRA_LIFE, SHIELD, SLOW_SPEED, FIRE };

class PowerUpManager {
private:
    struct PowerUpData {
        float x;
        float y;
        float vx;
        float vy; // Added vertical drift element
        float radius;
        PowerUpType type;
        bool active;
        uint16_t color;
    };

    PowerUpData _data;
    unsigned long _nextSpawnTime;
    // Track the moving target for the next extra life unlock
    unsigned long _nextExtraLifeScore;
    PowerUpType _lastType = NONE;
    static constexpr float DODGE_SPEED = 2.0f;   // px/frame up or down, clearing an asteroid

    // One draw, weighted over the power-ups available at this score.
    PowerUpType draw(int score) const {
        const bool fire = score >= ArcadeConfig::FIRE_START_SCORE;
        const bool life = (unsigned long)score >= _nextExtraLifeScore;
        const int total = ArcadeConfig::WEIGHT_SHIELD + ArcadeConfig::WEIGHT_SLOW +
                          (fire ? ArcadeConfig::WEIGHT_FIRE : 0) + (life ? ArcadeConfig::WEIGHT_EXTRA_LIFE : 0);
        int r = random(0, total);
        if ((r -= ArcadeConfig::WEIGHT_SHIELD) < 0) return SHIELD;
        if ((r -= ArcadeConfig::WEIGHT_SLOW) < 0)   return SLOW_SPEED;
        if (fire && (r -= ArcadeConfig::WEIGHT_FIRE) < 0) return FIRE;
        return EXTRA_LIFE;
    }

    static uint16_t colourOf(PowerUpType t) {
        switch (t) {
            case EXTRA_LIFE: return ArcadeConfig::COLOR_HEALTH;
            case SLOW_SPEED: return ArcadeConfig::COLOR_SLOW;
            case FIRE:       return ArcadeConfig::COLOR_FIRE;
            default:         return ArcadeConfig::COLOR_SHIELD;
        }
    }

public:
    PowerUpManager() {
        _data = {0, 0, 0, 0, 6.0f, NONE, false, 0};
        _nextExtraLifeScore = ArcadeConfig::MIN_SCORE_FOR_EXTRA_LIFE;
        resetTimeline();
    }

    // A new game: the extra life's threshold back to its first, and the
    // first power-up some way off.
    void newGame() {
        _nextExtraLifeScore = ArcadeConfig::MIN_SCORE_FOR_EXTRA_LIFE;
        _lastType = NONE;
        resetTimeline();
    }

    void resetTimeline() {
        _data.active = false;
        _nextSpawnTime = millis() + random(ArcadeConfig::POWERUP_SPAWN_LOW_MS, ArcadeConfig::POWERUP_SPAWN_HIGH_MS);
    }

    void update(int score, PlayerShip &ship, int &lives, bool &uiUpdate, AudioEngine &audio, AsteroidManager &asteroids) {
        if (!_data.active && score >= ArcadeConfig::POWERUP_START_SCORE && millis() >= _nextSpawnTime) {
            PowerUpType rolledType = draw(score);
            if (rolledType == _lastType) rolledType = draw(score);   // once more on a repeat
            _lastType = rolledType;

            _data.active = true;
            _data.x = ArcadeConfig::SCREEN_WIDTH + 20;
            _data.vx = -1.2f;
            // Matches asteroid motion physics with random vertical drift vectors
            _data.vy = (random(-30, 31) / 100.0f);
            // In a clear lane: the first of a few tries with nothing coming
            // through it, else the least crowded.
            float bestY = 0, bestPush = 1e9f;
            for (int k = 0; k < 8; k++) {
                const float y = random(20, ArcadeConfig::SCREEN_HEIGHT - 20);
                float px, py;
                asteroids.dodge(_data.x, y, _data.vx, _data.radius, px, py);
                const float push = fabsf(px) + fabsf(py);
                if (push < bestPush) { bestPush = push; bestY = y; }
                if (push == 0.0f) break;
            }
            _data.y = bestY;

            _data.type = rolledType;
            _data.color = colourOf(rolledType);
        }

        if (!_data.active) return;

        // Apply 2D Physics Vector Movements
        _data.x += _data.vx;
        _data.y += _data.vy;
        // Steer up or down out of the way of any asteroid about to cross it,
        // so it's never drawn over one.
        float pushX, pushY;
        if (asteroids.dodge(_data.x, _data.y, _data.vx, _data.radius, pushX, pushY)) {
            // Up against the top or bottom, it can't go further that way:
            // it slips forwards or back instead.
            const bool atTop    = _data.y - _data.radius <= ArcadeConfig::UI_MARGIN_TOP + 1;
            const bool atBottom = _data.y + _data.radius >= ArcadeConfig::SCREEN_HEIGHT - 1;
            if ((atTop && pushY < 0) || (atBottom && pushY > 0)) {
                pushX = pushX >= 0 ? 1.0f : -1.0f;
                pushY = 0.0f;
            }
            _data.x += constrain(pushX, -1.0f, 1.0f) * DODGE_SPEED;
            _data.y += constrain(pushY, -1.0f, 1.0f) * DODGE_SPEED;
            if (pushY != 0.0f && (pushY > 0) != (_data.vy > 0)) _data.vy = -_data.vy;
        }

        // Screen boundary bounce mechanics (Top UI margin boundary at Y=11)
        if (_data.y - _data.radius < ArcadeConfig::UI_MARGIN_TOP) {
            _data.y = ArcadeConfig::UI_MARGIN_TOP + _data.radius;
            _data.vy = -_data.vy; // Invert movement vector on vertical impact
        } 
        else if (_data.y + _data.radius > ArcadeConfig::SCREEN_HEIGHT) {
            _data.y = ArcadeConfig::SCREEN_HEIGHT - _data.radius;
            _data.vy = -_data.vy; // Invert movement vector on vertical impact
        }

        // Garbage collection if scrolled past player boundaries
        if (_data.x + _data.radius < 0) {
            resetTimeline();
            return;
        }

        // Bounding Box Collision Calculations
        float shipLeft   = ship.getX();
        float shipRight  = ship.getX() + ArcadeConfig::SHIP_WIDTH;
        float shipTop    = (float)ship.getY();
        float shipBottom = (float)(ship.getY() + ArcadeConfig::SHIP_HEIGHT);
        
        float distSqX = max(shipLeft, min(_data.x, shipRight));
        float distSqY = max(shipTop,  min(_data.y, shipBottom));
        
        float deltaX = _data.x - distSqX;
        float deltaY = _data.y - distSqY;
        float distSq = (deltaX * deltaX) + (deltaY * deltaY);
        
        if (distSq < (_data.radius * _data.radius)) {
            if (_data.type == EXTRA_LIFE) {
                lives++;
                _nextExtraLifeScore *= 2;
                uiUpdate = true;
                audio.playWAV("/audio/powerup.wav");
            } 
            else if (_data.type == SHIELD) {
                ship.activateShield();
                audio.playWAV("/audio/powerup.wav");
            }
            else if (_data.type == SLOW_SPEED) {
                asteroids.reduceGameSpeed(); // Dial back the hazard scroll speeds safely
                audio.playWAV("/audio/powerup.wav");
            }
            else if (_data.type == FIRE) {
                ship.activateFire(ArcadeConfig::FIRE_DURATION_MS);
                uiUpdate = true;
                audio.playWAV("/audio/powerup.wav");
            }
            resetTimeline();
        }
    }

    void render(GFXcanvas16 &canvas) {
        if (!_data.active) return;

        int cx = (int)_data.x;
        int cy = (int)_data.y;
        int r = (int)_data.radius;

        if (cx < -20 || cx > ArcadeConfig::SCREEN_WIDTH + 20) return;

        if (_data.type == EXTRA_LIFE) {
            canvas.fillCircle(cx - r/2, cy - r/4, r/2 + 1, _data.color);
            canvas.fillCircle(cx + r/2, cy - r/4, r/2 + 1, _data.color);
            canvas.fillTriangle(cx - r, cy, cx + r, cy, cx, cy + r + 2, _data.color);
        } 
        else if (_data.type == SHIELD) {
            int w = r * 2;
            int h = r * 2;
            canvas.fillRoundRect(cx - r, cy - r, w, h, 3, _data.color);
            canvas.drawFastHLine(cx - r + 3, cy, w - 6, ST7735_BLACK);
            canvas.drawFastVLine(cx, cy - r + 3, h - 6, ST7735_BLACK);
        }
        else if (_data.type == SLOW_SPEED) {
            // Draw a Clock/Timer icon for the Slow Speed effect
            canvas.fillCircle(cx, cy, r, _data.color);
            canvas.drawCircle(cx, cy, r, ST7735_WHITE);
            // Hands of the clock
            canvas.drawLine(cx, cy, cx, cy - r + 3, ST7735_BLACK);
            canvas.drawLine(cx, cy, cx + r - 3, cy, ST7735_BLACK);
        }
        else if (_data.type == FIRE) {
            // An orange disc with a bolt across it, pointing the way it fires
            canvas.fillCircle(cx, cy, r, _data.color);
            canvas.drawCircle(cx, cy, r, ST7735_YELLOW);
            canvas.drawFastHLine(cx - r + 2, cy, 2 * r - 4, ST7735_WHITE);
            canvas.drawLine(cx + r - 5, cy - 2, cx + r - 3, cy, ST7735_WHITE);
            canvas.drawLine(cx + r - 5, cy + 2, cx + r - 3, cy, ST7735_WHITE);
        }
    }
};

#endif