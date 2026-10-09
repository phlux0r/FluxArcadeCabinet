#ifndef CAVERN_OBSTACLES_H
#define CAVERN_OBSTACLES_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

class CavernObstacles {
private:
    static const int NUM_POINTS  = 6;
    static const int MAX_HAZARDS = 5;

    struct Hazard {
        int x, y;
        int maxRadius;
        uint8_t variant;             // 0-3: which way round its craters sit
        int pointX[NUM_POINTS];
        int pointY[NUM_POINTS];
    };

    Hazard _rocks[MAX_HAZARDS];
    int _activeHazardsCount = 0;

    // Space the ship needs between two rocks' surfaces to fly through: a
    // hit is the ship's centre within (rock radius + 2), so 4px is the bare
    // minimum; this leaves room to steer a heavy lander through.
    static const int SHIP_GAP = 18;

    // Returns true if rock at index i is closer to any previously placed
    // rock than a ship-sized gap.
    bool overlapsExisting(int i) {
        for (int j = 0; j < i; j++) {
            float dx = _rocks[i].x - _rocks[j].x;
            float dy = _rocks[i].y - _rocks[j].y;
            float distSq = dx * dx + dy * dy;
            int minSep = _rocks[i].maxRadius + _rocks[j].maxRadius + SHIP_GAP;
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
    // Up to 5 rocks (2, plus one every other level), each in its own
    // height band, growing slowly with level. Every pair leaves a ship-sized
    // gap: a rock that won't fit shrinks, and one that still won't is left
    // out, rather than walling the cavern off. The lowest rock keeps clear
    // of the column above the landing pad, so the final approach is open.
    void generateNewMap(int currentLevel, int padX, int padWidth) {
        int wanted = 2 + (currentLevel / 2);
        if (wanted > MAX_HAZARDS) wanted = MAX_HAZARDS;

        int safeCeilingY = 45;
        int spawnFloorY  = ArcadeConfig::PORTRAIT_HEIGHT - 35;
        int playSpace    = spawnFloorY - safeCeilingY;
        int interval     = playSpace / wanted;
        int shipSpawnX   = ArcadeConfig::PORTRAIT_WIDTH / 2;
        int padCentre    = padX + padWidth / 2;

        // Rock size grows a step every other level, capped.
        int minSize = min(5 + currentLevel / 2, 8);
        int maxSize = min(8 + currentLevel / 2, 11);

        _activeHazardsCount = 0;
        for (int band = 0; band < wanted; band++) {
            const int i = _activeHazardsCount;
            _rocks[i].maxRadius = random(minSize, maxSize + 1);
            _rocks[i].variant   = (uint8_t)random(0, 4);
            const bool lowest = band == wanted - 1;

            bool placed = false;
            while (!placed && _rocks[i].maxRadius >= 4) {
                for (int attempts = 0; attempts < 30 && !placed; attempts++) {
                    _rocks[i].x = random(20, ArcadeConfig::PORTRAIT_WIDTH - 20);
                    // Keep rocks in their vertical band (prevents all stacking top/bottom)
                    _rocks[i].y = safeCeilingY + (band * interval) + random(-4, 5);

                    // Spawn shield: the top rock stays off the ship's spawn column.
                    if (band == 0 && _rocks[i].y < 60 &&
                        abs(_rocks[i].x - shipSpawnX) < _rocks[i].maxRadius + 12) continue;
                    // The lowest rock stays off the column above the pad.
                    if (lowest && abs(_rocks[i].x - padCentre) < _rocks[i].maxRadius + padWidth / 2 + 8) continue;
                    placed = !overlapsExisting(i);
                }
                if (!placed) _rocks[i].maxRadius--;
            }
            if (!placed) continue;          // no room left for this one
            generatePoints(i);
            _activeHazardsCount++;
        }
    }

    // Is (x, y) at least `margin` clear of every rock's hit circle for a
    // ship of hit radius `shipRadius`? For the attract demo's route.
    bool clearOf(float x, float y, int shipRadius, int margin) const {
        for (int i = 0; i < _activeHazardsCount; i++) {
            float dx = x - _rocks[i].x, dy = y - _rocks[i].y;
            int r = shipRadius + _rocks[i].maxRadius - 2 + margin;
            if (dx * dx + dy * dy < (float)(r * r)) return false;
        }
        return true;
    }

    // The top rock's x (the first placed), or -1 with none.
    int topRockX() const { return _activeHazardsCount > 0 ? _rocks[0].x : -1; }

    bool checkCollision(float shipX, float shipY, int shipRadius) {
        for (int i = 0; i < _activeHazardsCount; i++) {
            float dx = shipX - _rocks[i].x;
            float dy = shipY - _rocks[i].y;
            int safetyR = shipRadius + _rocks[i].maxRadius - 2;
            if ((dx * dx + dy * dy) < (float)(safetyR * safetyR)) return true;
        }
        return false;
    }

    // Solid rocks in the world's colours: filled, edges facing the upper
    // left lit and the rest shaded, and a crater or two.
    void render(GFXcanvas16 &canvas, uint16_t fill, uint16_t lit, uint16_t shade) {
        for (int i = 0; i < _activeHazardsCount; i++) {
            const Hazard &h = _rocks[i];
            for (int p = 0; p < NUM_POINTS; p++) {
                const int next = (p + 1) % NUM_POINTS;
                canvas.fillTriangle(h.x, h.y, h.pointX[p], h.pointY[p],
                                    h.pointX[next], h.pointY[next], fill);
            }
            const int r = h.maxRadius;
            const int sx = (h.variant & 1) ? -1 : 1, sy = (h.variant & 2) ? -1 : 1;
            canvas.fillCircle(h.x + sx * r / 3, h.y + sy * r / 4, r >= 7 ? 2 : 1, shade);
            canvas.drawPixel(h.x - sx * r / 3, h.y - sy * r / 3, shade);
            for (int p = 0; p < NUM_POINTS; p++) {
                const int next = (p + 1) % NUM_POINTS;
                const int mx = h.pointX[p] + h.pointX[next] - 2 * h.x;
                const int my = h.pointY[p] + h.pointY[next] - 2 * h.y;
                canvas.drawLine(h.pointX[p], h.pointY[p], h.pointX[next], h.pointY[next],
                                (mx + my < 0) ? lit : shade);
            }
        }
    }
};

#endif // CAVERN_OBSTACLES_H