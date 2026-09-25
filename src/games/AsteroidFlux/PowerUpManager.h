#ifndef POWERUP_MANAGER_H
#define POWERUP_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "PlayerShip.h"
#include "../../cabinet/AudioEngine.h"
#include "AsteroidManager.h" // Linked to access speed controls

enum PowerUpType { NONE, EXTRA_LIFE, SHIELD, SLOW_SPEED };

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

public:
    PowerUpManager() {
        _data = {0, 0, 0, 0, 6.0f, NONE, false, 0};
        _nextExtraLifeScore = ArcadeConfig::MIN_SCORE_FOR_EXTRA_LIFE;
        resetTimeline();
    }

    void resetTimeline() {
        _data.active = false;
        _nextSpawnTime = millis() + random(ArcadeConfig::POWERUP_SPAWN_LOW_MS, ArcadeConfig::POWERUP_SPAWN_HIGH_MS);
    }

    void update(int score, PlayerShip &ship, int &lives, bool &uiUpdate, AudioEngine &audio, AsteroidManager &asteroids) {
        if (!_data.active && score >= ArcadeConfig::POWERUP_START_SCORE && millis() >= _nextSpawnTime) {
            PowerUpType rolledType = SHIELD;
            uint16_t rolledColor = ArcadeConfig::COLOR_SHIELD;

            int probabilityDice = random(0, 100);
            
            if (probabilityDice < ArcadeConfig::EXTRA_LIFE_CHANCE && score >= _nextExtraLifeScore) {
                rolledType = EXTRA_LIFE;
                rolledColor = ArcadeConfig::COLOR_HEALTH;
            }
            // Roll check for our brand new Slow Speed module
            else {
                // roll again
                probabilityDice = random(0, 100);
                if (probabilityDice < (ArcadeConfig::SLOW_SPEED_CHANCE)) {
                    rolledType = SLOW_SPEED;
                    rolledColor = ArcadeConfig::COLOR_SLOW;
                }
            }

            _data.active = true;
            _data.x = ArcadeConfig::SCREEN_WIDTH + 20;
            _data.y = random(20, ArcadeConfig::SCREEN_HEIGHT - 20);
            _data.vx = -1.2f;
            
            // Matches asteroid motion physics with random vertical drift vectors
            _data.vy = (random(-30, 31) / 100.0f); 
            
            _data.type = rolledType;
            _data.color = rolledColor;
        }

        if (!_data.active) return;

        // Apply 2D Physics Vector Movements
        _data.x += _data.vx;
        _data.y += _data.vy;

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
                audio.playSound(1500, 150); 
            } 
            else if (_data.type == SHIELD) {
                ship.activateShield();
                audio.playSound(1000, 250);
            }
            else if (_data.type == SLOW_SPEED) {
                asteroids.reduceGameSpeed(); // Dial back the hazard scroll speeds safely
                audio.playSound(600, 400);   // Deeper satisfying down-tempo chime
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
    }
};

#endif