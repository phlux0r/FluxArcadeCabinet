#ifndef LANDER_CAVERN_OBSTACLES_H
#define LANDER_CAVERN_OBSTACLES_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "ArcadeConfig.h"

class Lander_CavernObstacles {
private:
    static const int NUM_POINTS = 6;
    static const int MAX_HAZARDS = 6; 

    struct Hazard {
        int x, y;
        int maxRadius;
        uint16_t color;
        int pointX[NUM_POINTS];
        int pointY[NUM_POINTS];
    };

    Hazard _rocks[MAX_HAZARDS];
    int _activeHazardsCount = 0;
    const uint16_t ROCK_COLORS[4] = {0xCE59, 0x7BF3, 0x93A6, 0xBDF7};

public:
    void generateNewMap(int currentLevel) {
        _activeHazardsCount = 2 + (currentLevel / 2);
        if (_activeHazardsCount > MAX_HAZARDS) _activeHazardsCount = MAX_HAZARDS;

        int safeCeilingY = 45; 
        int spawnFloorY = ArcadeConfig::SCREEN_HEIGHT - 35; 
        int verticalPlayableSpace = spawnFloorY - safeCeilingY;
        int spacingInterval = verticalPlayableSpace / _activeHazardsCount;

        for (int i = 0; i < _activeHazardsCount; i++) {
            _rocks[i].maxRadius = random(6, 12);
            _rocks[i].color = ROCK_COLORS[random(0, 4)];
            
            if (i % 2 == 0) {
                _rocks[i].x = random(15, ArcadeConfig::SCREEN_WIDTH / 2 - 15);
            } else {
                _rocks[i].x = random(ArcadeConfig::SCREEN_WIDTH / 2 + 15, ArcadeConfig::SCREEN_WIDTH - 15);
            }
            
            _rocks[i].y = safeCeilingY + (i * spacingInterval) + random(-5, 5);

            float angleStep = (2.0f * PI) / NUM_POINTS;
            for (int p = 0; p < NUM_POINTS; p++) {
                float currentAngle = p * angleStep;
                int irregularRadius = _rocks[i].maxRadius - random(0, 3);
                
                _rocks[i].pointX[p] = _rocks[i].x + (cos(currentAngle) * irregularRadius);
                _rocks[i].pointY[p] = _rocks[i].y + (sin(currentAngle) * irregularRadius);
            }
        }
    }

    bool checkCollision(float shipX, float shipY, int shipRadius) {
        for (int i = 0; i < _activeHazardsCount; i++) {
            float dx = shipX - _rocks[i].x;
            float dy = shipY - _rocks[i].y;
            float distanceSq = (dx * dx) + (dy * dy);
            
            int safetyRadius = shipRadius + _rocks[i].maxRadius - 2;
            if (distanceSq < (safetyRadius * safetyRadius)) {
                return true; 
            }
        }
        return false;
    }

    void render(GFXcanvas16 &canvas) {
        for (int i = 0; i < _activeHazardsCount; i++) {
            for (int p = 0; p < NUM_POINTS; p++) {
                int nextPoint = (p + 1) % NUM_POINTS;
                canvas.drawLine(
                    _rocks[i].pointX[p], _rocks[i].pointY[p],
                    _rocks[i].pointX[nextPoint], _rocks[i].pointY[nextPoint],
                    _rocks[i].color
                );
            }
        }
    }
};

#endif