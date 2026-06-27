#ifndef PARTICLE_MANAGER_H
#define PARTICLE_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "ArcadeConfig.h"

// =============================================================================
// SHARED PARTICLE MANAGER
// Merged from AsteroidFlux ParticleManager and LanderFlux ParticleEngine.
// Supports two spawn modes:
//
//   spawnExplosion() — radial burst from a point, time-based lifespan,
//                      fade-to-white effect on expiry. (AsteroidFlux style)
//
//   spawnFire()      — single directed particle with velocity, frame-count
//                      lifespan. Used for thrust trails. (LanderFlux style)
//
//   triggerExplosion() — large radial burst shorthand (LanderFlux API kept
//                        for minimal changes to existing callers).
//
// Pool size is shared across all active particles regardless of spawn mode.
// =============================================================================

class ParticleManager {
private:
    // Use time-based lifespan for all particles (more accurate than frame counts).
    struct Particle {
        float x, y;
        float vx, vy;
        uint16_t color;
        unsigned long expireMs;
        unsigned long totalLifeMs;  // Used to calculate fade timing
        bool active;
    };

    static const int POOL_SIZE = 120;  // Matches LanderFlux's larger pool
    Particle _pool[POOL_SIZE];

    // Drag coefficient applied each frame to slow particles naturally
    static constexpr float DRAG = 0.96f;

    Particle* allocate() {
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) return &_pool[i];
        }
        return nullptr;  // Pool full — caller should handle gracefully
    }

public:
    ParticleManager() {
        for (int i = 0; i < POOL_SIZE; i++) _pool[i].active = false;
    }

    // -------------------------------------------------------------------------
    // EXPLOSION BURST — AsteroidFlux style
    // Spawns 'count' particles radiating outward from (centerX, centerY).
    // lifespanMs controls how long they persist (default 600ms).
    // -------------------------------------------------------------------------
    void spawnExplosion(float centerX, float centerY, uint16_t color,
                        int count, int lifespanMs = 600) {
        int spawned = 0;
        for (int i = 0; i < POOL_SIZE && spawned < count; i++) {
            if (_pool[i].active) continue;

            float angle = random(0, 360) * (PI / 180.0f);
            float speed = random(50, 250) / 100.0f;  // 0.5–2.5 px/frame

            int life = random(lifespanMs / 2, lifespanMs);

            _pool[i].active      = true;
            _pool[i].x           = centerX;
            _pool[i].y           = centerY;
            _pool[i].vx          = cos(angle) * speed;
            _pool[i].vy          = sin(angle) * speed;
            _pool[i].color       = color;
            _pool[i].expireMs    = millis() + life;
            _pool[i].totalLifeMs = life;
            spawned++;
        }
    }

    // -------------------------------------------------------------------------
    // TRIGGER EXPLOSION — LanderFlux API (kept for minimal port changes)
    // Spawns a large mixed-colour burst suitable for ship disintegration.
    // -------------------------------------------------------------------------
    void triggerExplosion(float centerX, float centerY, int count = 60) {
        int spawned = 0;
        for (int i = 0; i < POOL_SIZE && spawned < count; i++) {
            if (_pool[i].active) continue;

            float angle = random(0, 360) * (PI / 180.0f);
            float speed = random(5, 25) * 0.1f;

            // Mix of white, amber/orange, and red — ship explosion colours
            uint16_t color;
            int roll = random(0, 3);
            if      (roll == 0) color = ArcadeConfig::COLOR_WHITE;
            else if (roll == 1) color = ArcadeConfig::COLOR_AMBER;
            else                color = ArcadeConfig::COLOR_RED;

            int life = random(400, 900);

            _pool[i].active      = true;
            _pool[i].x           = centerX;
            _pool[i].y           = centerY;
            _pool[i].vx          = cos(angle) * speed;
            _pool[i].vy          = sin(angle) * speed;
            _pool[i].color       = color;
            _pool[i].expireMs    = millis() + life;
            _pool[i].totalLifeMs = life;
            spawned++;
        }
    }

    // -------------------------------------------------------------------------
    // SPAWN FIRE — LanderFlux thrust trail style
    // Single directed particle with explicit velocity.
    // -------------------------------------------------------------------------
    void spawnFire(float x, float y, float vx, float vy,
                   uint16_t color = 0) {
        Particle* p = allocate();
        if (!p) return;

        // Default fire colour: alternates amber and red
        if (color == 0) {
            color = (random(0, 2) == 0) ? ArcadeConfig::COLOR_AMBER : ArcadeConfig::COLOR_RED;
        }

        int life = random(150, 400);  // Thrust trails fade quickly

        p->active      = true;
        p->x           = x;
        p->y           = y;
        p->vx          = vx;
        p->vy          = vy;
        p->color       = color;
        p->expireMs    = millis() + life;
        p->totalLifeMs = life;
    }

    // -------------------------------------------------------------------------
    // UPDATE — call once per frame
    // -------------------------------------------------------------------------
    void update() {
        unsigned long now = millis();
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;

            if (now >= _pool[i].expireMs) {
                _pool[i].active = false;
                continue;
            }

            _pool[i].x  += _pool[i].vx;
            _pool[i].y  += _pool[i].vy;
            _pool[i].vx *= DRAG;
            _pool[i].vy *= DRAG;
        }
    }

    // -------------------------------------------------------------------------
    // RENDER — call after update(), before canvas flush
    // -------------------------------------------------------------------------
    void render(GFXcanvas16 &canvas, int clipTop = 0) {
        unsigned long now = millis();
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;

            int cx = (int)_pool[i].x;
            int cy = (int)_pool[i].y;

            // Clip to visible area (respects UI margin if clipTop > 0)
            if (cx < 0 || cx >= canvas.width()) continue;
            if (cy < clipTop || cy >= canvas.height()) continue;

            // Fade to white in the last 80ms of life (AsteroidFlux style)
            uint16_t color = _pool[i].color;
            unsigned long remaining = _pool[i].expireMs - now;
            if (remaining < 80) color = ArcadeConfig::COLOR_WHITE;

            canvas.drawPixel(cx, cy, color);
        }
    }

    void clearAll() {
        for (int i = 0; i < POOL_SIZE; i++) _pool[i].active = false;
    }

    int activeCount() const {
        int n = 0;
        for (int i = 0; i < POOL_SIZE; i++) if (_pool[i].active) n++;
        return n;
    }
};

#endif // PARTICLE_MANAGER_H
