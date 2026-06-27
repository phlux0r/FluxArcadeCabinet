#ifndef ASTEROID_MANAGER_H
#define ASTEROID_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "PlayerShip.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/ParticleManager.h"

class AsteroidManager {
private:
    struct Asteroid {
        float x, y;
        int sizeClass; 
        float radius;
        uint16_t color;
        bool active;
        float xOffsets[8];
        float yOffsets[8];
        bool isComet;
        float speedMultiplier;
        float vx, vy;
        float angle;       
        float spinSpeed;
    };

    Asteroid _pool[ArcadeConfig::MAX_ASTEROIDS];
    int _currentMaxActive; 
    float _currentSpeed;
    bool _cometOnScreen;

    void resetAsteroid(int index, bool startOffScreen) {
        if (_pool[index].active && _pool[index].isComet) {
            _cometOnScreen = false;
        }
        _pool[index].isComet = false;
        _pool[index].speedMultiplier = 1.0f;

        if (_currentMaxActive >= 3 && !_cometOnScreen && random(0, 20) == 0) { 
            _pool[index].isComet = true;
            _cometOnScreen = true;           
            _pool[index].sizeClass = 1;
            _pool[index].radius = 2.5f;       
            _pool[index].color = ST7735_MAGENTA; 
            _pool[index].speedMultiplier = 1.0f; 
        }
        else {
            _pool[index].sizeClass = random(1, 6);
            switch (_pool[index].sizeClass) {
                case 1: _pool[index].radius = 3.0f; _pool[index].color = ST7735_YELLOW; break;
                case 2: _pool[index].radius = 5.0f; _pool[index].color = ST7735_CYAN;  break;
                case 3: _pool[index].radius = 7.0f; _pool[index].color = ST7735_ORANGE; break;
                case 4: _pool[index].radius = 9.0f; _pool[index].color = ST7735_RED;   break;
                case 5: _pool[index].radius = 10.0f; _pool[index].color = ST7735_BLUE;  break;
            }

            for (int i = 0; i < 8; i++) {
                float angle = i * (PI / 4.0f);
                float irregularRadius = _pool[index].radius * (random(70, 131) / 100.0f);
                _pool[index].xOffsets[i] = cos(angle) * irregularRadius;
                _pool[index].yOffsets[i] = sin(angle) * irregularRadius;
            }

            float calculatedSpeed = _currentSpeed;
            if (_pool[index].isComet) {
                calculatedSpeed = _currentSpeed * 1.5f;
                if (calculatedSpeed > ArcadeConfig::COMET_SPEED_CAP) {
                    calculatedSpeed = ArcadeConfig::COMET_SPEED_CAP + (_currentSpeed / 10.0f);
                }
            }
            _pool[index].vx = -abs(calculatedSpeed);
            _pool[index].vy = (random(-30, 31) / 100.0f);
            _pool[index].angle = random(0, 360);
            _pool[index].spinSpeed = random(-3, 4);
        }

        if (startOffScreen) {
            _pool[index].x = ArcadeConfig::SCREEN_WIDTH + random(15, 80) + (index * 35);
        } else {
            _pool[index].x = ArcadeConfig::SCREEN_WIDTH + (_pool[index].isComet ? random(40, 90) : random(15, 45));
        }
        _pool[index].y = random(15 + _pool[index].radius, ArcadeConfig::SCREEN_HEIGHT - _pool[index].radius - 6);
    }

    void drawJagged(GFXcanvas16 &canvas, Asteroid& ast) {
        float rad = ast.angle * (PI / 180.0f);
        float cosA = cos(rad);
        float sinA = sin(rad);
        int rotatedX[8], rotatedY[8];

        for (int i = 0; i < 8; i++) {
            float rawX = ast.xOffsets[i];
            float rawY = ast.yOffsets[i];
            rotatedX[i] = (int)(ast.x + (rawX * cosA - rawY * sinA));
            rotatedY[i] = (int)(ast.y + (rawX * sinA + rawY * cosA));
        }
        for (int i = 0; i < 8; i++) {
            int nextIndex = (i + 1) % 8;
            canvas.drawLine(rotatedX[i], rotatedY[i], rotatedX[nextIndex], rotatedY[nextIndex], ast.color);
        }
    }

    void drawComet(GFXcanvas16 &canvas, Asteroid& ast) {
        int cx = (int)ast.x; int cy = (int)ast.y; int r = (int)ast.radius;
        canvas.drawCircle(cx, cy, r, ast.color);
        bool alternateFrame = (millis() / 60) % 2 == 0;
        if (alternateFrame) {
            canvas.drawLine(cx + r, cy, cx + r + 11, cy, ST7735_WHITE);
            canvas.drawLine(cx, cy - r, cx + r + 8, cy - 1, ArcadeConfig::COLOR_ION_BLUE);
            canvas.drawLine(cx, cy + r, cx + r + 8, cy + 1, ArcadeConfig::COLOR_ION_BLUE);
        } else {
            canvas.drawLine(cx + r, cy, cx + r + 7, cy, ST7735_WHITE);
            canvas.drawLine(cx + r, cy - 1, cx + r + 6, cy, ST7735_WHITE);
            canvas.drawLine(cx + r, cy + 1, cx + r + 6, cy, ST7735_WHITE);
            canvas.drawLine(cx, cy - r, cx + r + 9, cy - 3, ArcadeConfig::COLOR_ION_BLUE);
            canvas.drawLine(cx, cy + r, cx + r + 9, cy + 3, ArcadeConfig::COLOR_ION_BLUE);
        }
    }

public:
    AsteroidManager() : _currentMaxActive(1), _currentSpeed(ArcadeConfig::BASE_SPEED), _cometOnScreen(false) {
        for (int i = 0; i < ArcadeConfig::MAX_ASTEROIDS; i++) _pool[i].active = false;
    }

    void initGame() {
        _currentMaxActive = 1;
        _currentSpeed = ArcadeConfig::BASE_SPEED;
        _cometOnScreen = false;
        for (int i = 0; i < ArcadeConfig::MAX_ASTEROIDS; i++) {
            _pool[i].active = (i < _currentMaxActive);
            resetAsteroid(i, true);
        }
    }

    void forceBoardWipe() {
        _cometOnScreen = false;
        for (int j = 0; j < _currentMaxActive; j++) {
            _pool[j].active = (j < _currentMaxActive);
            resetAsteroid(j, true);
        }
    }

    void reduceGameSpeed() {
        _currentSpeed -= (ArcadeConfig::SPEED_STEP * ArcadeConfig::SPEED_STEPS_TO_REDUCE);
        if (_currentSpeed < ArcadeConfig::BASE_SPEED) {
            _currentSpeed = ArcadeConfig::BASE_SPEED; // Clamp to baseline minimum
        }
    }

    void update(PlayerShip &ship, int &score, int &asteroidsPassed, int &nextTargetScore, 
                bool &uiNeedsUpdate, bool &playerHit, AudioEngine &audio, ParticleManager &particles) {
        for (int i = 0; i < ArcadeConfig::MAX_ASTEROIDS; i++) {
            if (!_pool[i].active) continue;

            if (_pool[i].isComet) {
                float cometSpeed = _currentSpeed * 1.5f;
                if (cometSpeed > ArcadeConfig::COMET_SPEED_CAP) cometSpeed = ArcadeConfig::COMET_SPEED_CAP + (_currentSpeed / 10.0f);
                _pool[i].vx = -cometSpeed;
            } else {
                _pool[i].vx = -_currentSpeed;
            }

            _pool[i].x += _pool[i].vx; 
            _pool[i].y += _pool[i].vy;
            _pool[i].angle += _pool[i].spinSpeed;

            if (_pool[i].angle >= 360.0f) _pool[i].angle -= 360.0f;
            if (_pool[i].angle < 0.0f) _pool[i].angle += 360.0f;

            if (_pool[i].y - _pool[i].radius < ArcadeConfig::UI_MARGIN_TOP) {
                _pool[i].y = ArcadeConfig::UI_MARGIN_TOP + _pool[i].radius;
                _pool[i].vy = -_pool[i].vy;        
            } 
            else if (_pool[i].y + _pool[i].radius > ArcadeConfig::SCREEN_HEIGHT) {
                _pool[i].y = ArcadeConfig::SCREEN_HEIGHT - _pool[i].radius;
                _pool[i].vy = -_pool[i].vy;        
            }

           if (_pool[i].x + _pool[i].radius + 12 < 0) {
                if (_pool[i].isComet) {
                    score += ArcadeConfig::COMET_BONUS_SCORE;
                    audio.playSound(1200, 100); 
                    // Explode bright Magenta fragments on Comet pass!
                    particles.spawnExplosion(0, _pool[i].y, ST7735_MAGENTA, 12);
                } else {
                    score += _pool[i].sizeClass;
                    audio.playSound(800, 30);
                    // Spawns smaller sparks matching the exact color of the asteroid size destroyed
                    particles.spawnExplosion(0, _pool[i].y, _pool[i].color, 6);
                }
                
                asteroidsPassed++;
                uiNeedsUpdate = true;


                if (asteroidsPassed >= nextTargetScore) {
                    if (_currentMaxActive < ArcadeConfig::MAX_ASTEROIDS) {
                        _currentMaxActive++;
                        _pool[_currentMaxActive - 1].active = true;
                        resetAsteroid(_currentMaxActive - 1, true);
                    } else {
                        _currentSpeed += ArcadeConfig::SPEED_STEP;
                    }
                    nextTargetScore += ArcadeConfig::SCORE_TO_SPAWN + (_currentMaxActive * 4);
                }
                resetAsteroid(i, false);
                continue;
            }

            // Ship collision vectors check
            float closestX = max(ship.getX(), min(_pool[i].x, ship.getX() + ArcadeConfig::SHIP_WIDTH));
            float closestY = max((float)ship.getY(), min(_pool[i].y, (float)ship.getY() + ArcadeConfig::SHIP_HEIGHT));
            float distanceSquared = ((_pool[i].x - closestX) * (_pool[i].x - closestX)) + ((_pool[i].y - closestY) * (_pool[i].y - closestY));

            if (distanceSquared < ((_pool[i].radius * 0.9f) * (_pool[i].radius * 0.9f))) {
                if (ship.isShieldActive()) {
                    // DYNAMIC PLASMA BURST: Shatter the asteroid into glowing cyan particles
                    // We use the asteroid's current coordinates and match the density to its size
                    float burstX = _pool[i].x;
                    float burstY = _pool[i].y;
                    particles.spawnExplosion(burstX, burstY, ST7735_CYAN, 20);
                    // decativate shield and play sound
                    ship.deactivateShield();
                    audio.playSound(300, 200);
                    resetAsteroid(i, true);
                } else {
                    playerHit = true;
                    return; 
                }
            }
        }
    }

    void render(GFXcanvas16 &canvas) {
        for (int i = 0; i < ArcadeConfig::MAX_ASTEROIDS; i++) {
            if (!_pool[i].active) continue;
            if (_pool[i].isComet) drawComet(canvas, _pool[i]);
            else drawJagged(canvas, _pool[i]);
        }
    }
};

#endif