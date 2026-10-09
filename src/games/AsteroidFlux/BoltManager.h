#ifndef BOLT_MANAGER_H
#define BOLT_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "AsteroidManager.h"
#include "AlienSaucer.h"

// The Fire power-up's bolts: straight ahead from the ship's nose, until
// they hit an asteroid or the saucer, or leave the screen.
class BoltManager {
private:
    static const int MAX_BOLTS = 8;
    static const int BOLT_LENGTH = 5;
    struct Bolt { float x, y; bool active; };
    Bolt _pool[MAX_BOLTS];

public:
    BoltManager() { clearAll(); }

    void clearAll() { for (auto &b : _pool) b.active = false; }

    void fire(float x, float y) {
        for (auto &b : _pool) {
            if (b.active) continue;
            b = { x, y, true };
            return;
        }
    }

    // Moves every bolt and breaks what it hits; returns how many hit an
    // asteroid. saucerHit: 0, or 1 if one hit the saucer, 2 if one
    // brought it down.
    int update(AsteroidManager &asteroids, AlienSaucer &saucer, int &saucerHit,
               int &score, int &asteroidsPassed,
               int &nextTargetScore, ParticleManager &particles) {
        int hits = 0;
        saucerHit = 0;
        for (auto &b : _pool) {
            if (!b.active) continue;
            const float x0 = b.x;
            b.x += ArcadeConfig::BOLT_SPEED;
            if (asteroids.shoot(x0, b.x, b.y, score, asteroidsPassed, nextTargetScore, particles)) {
                b.active = false;
                ++hits;
            } else if (const int s = saucer.shoot(x0, b.x, b.y, particles)) {
                b.active = false;
                saucerHit = max(saucerHit, s);
            } else if (b.x - BOLT_LENGTH > ArcadeConfig::SCREEN_WIDTH) {
                b.active = false;
            }
        }
        return hits;
    }

    void render(GFXcanvas16 &canvas) {
        for (const auto &b : _pool) {
            if (!b.active) continue;
            const int x = (int)b.x, y = (int)b.y;
            canvas.drawFastHLine(x - BOLT_LENGTH, y, BOLT_LENGTH - 1, ArcadeConfig::COLOR_ORANGE);
            canvas.drawPixel(x - 1, y, ST7735_YELLOW);
        }
    }

    int activeCount() const {
        int n = 0;
        for (const auto &b : _pool) n += b.active;
        return n;
    }
};

#endif
