#ifndef CAVERN_OBSTACLES_H
#define CAVERN_OBSTACLES_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

class CavernObstacles {
private:
    static const int NUM_POINTS  = 6;
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

    // Returns true if rock at index i overlaps any previously placed rock
    bool overlapsExisting(int i) {
        for (int j = 0; j < i; j++) {
            float dx = _rocks[i].x - _rocks[j].x;
            float dy = _rocks[i].y - _rocks[j].y;
            float distSq = dx * dx + dy * dy;
            // Minimum separation: sum of radii plus a 6px gap
            int minSep = _rocks[i].maxRadius + _rocks[j].maxRadius + 6;
            if (distSq < (float)(minSep * minSep)) return true;
        }
        return false;
    }

    void generatePoints(int i) {
        float angleStep = (2.0f * PI) / NUM_POINTS;
        for (int p = 0; p < NUM_POINTS; p++) {
            float angle = p * angleStep;
            int r = _rocks[i].maxRadius - random(0, 3);
            _rocks[i].pointX[p] = _rocks[i].x + (int)(cos(angle) * r);
            _rocks[i].pointY[p] = _rocks[i].y + (int)(sin(angle) * r);
        }
    }

public:
    void generateNewMap(int currentLevel) {
        _activeHazardsCount = 2 + (currentLevel / 2);
        if (_activeHazardsCount > MAX_HAZARDS) _activeHazardsCount = MAX_HAZARDS;

        int safeCeilingY = 45;
        int spawnFloorY  = ArcadeConfig::PORTRAIT_HEIGHT - 35;
        int playSpace    = spawnFloorY - safeCeilingY;
        int interval     = playSpace / _activeHazardsCount;
        int shipSpawnX   = ArcadeConfig::PORTRAIT_WIDTH / 2;

        // Rock size scaled with level, capped so they stay reasonable
        int minSize = min(5 + currentLevel, 10);
        int maxSize = min(8 + currentLevel, 14);

        for (int i = 0; i < _activeHazardsCount; i++) {
            _rocks[i].maxRadius = random(minSize, maxSize + 1);
            _rocks[i].color     = ROCK_COLORS[random(0, 4)];

            // Each rock gets up to 20 placement attempts to avoid overlap
            int attempts = 0;
            do {
                _rocks[i].x = random(20, ArcadeConfig::PORTRAIT_WIDTH - 20);
                // Keep rocks in their vertical band (prevents all stacking top/bottom)
                _rocks[i].y = safeCeilingY + (i * interval) + random(-4, 5);
                attempts++;
            } while (overlapsExisting(i) && attempts < 20);

            // Spawn shield: push rock i=0 away from ship spawn X if too close
            if (i == 0 && _rocks[i].y < 60) {
                int buffer = _rocks[i].maxRadius + 12;
                if (abs(_rocks[i].x - shipSpawnX) < buffer) {
                    _rocks[i].x = (_rocks[i].x < shipSpawnX)
                                  ? shipSpawnX - buffer - random(2, 6)
                                  : shipSpawnX + buffer + random(2, 6);
                    _rocks[i].x = constrain(_rocks[i].x, 20,
                                            ArcadeConfig::PORTRAIT_WIDTH - 20);
                }
            }

            generatePoints(i);
        }
    }

    bool checkCollision(float shipX, float shipY, int shipRadius) {
        for (int i = 0; i < _activeHazardsCount; i++) {
            float dx = shipX - _rocks[i].x;
            float dy = shipY - _rocks[i].y;
            int safetyR = shipRadius + _rocks[i].maxRadius - 2;
            if ((dx * dx + dy * dy) < (float)(safetyR * safetyR)) return true;
        }
        return false;
    }

    void render(GFXcanvas16 &canvas) {
        for (int i = 0; i < _activeHazardsCount; i++) {
            for (int p = 0; p < NUM_POINTS; p++) {
                int next = (p + 1) % NUM_POINTS;
                canvas.drawLine(
                    _rocks[i].pointX[p], _rocks[i].pointY[p],
                    _rocks[i].pointX[next], _rocks[i].pointY[next],
                    _rocks[i].color);
            }
        }
    }
};

#endif // CAVERN_OBSTACLES_H