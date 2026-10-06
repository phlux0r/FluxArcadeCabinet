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
//
// Trails: each spawn call takes an optional trail length (0 = none, up to
// TRAIL_MAX). A particle with one remembers where it was on its last few
// updates and render() draws lines back through them, each step dimmer, so
// it leaves a fading streak like Resonance's shards (which get theirs from
// the scope's afterglow; these games draw straight to the screen).
// =============================================================================

class ParticleManager {
public:
    static const int TRAIL_MAX = 4;

private:
    // Use time-based lifespan for all particles (more accurate than frame counts).
    struct Particle {
        float x, y;
        float vx, vy;
        uint16_t color;
        unsigned long expireMs;
        unsigned long totalLifeMs;  // Used to calculate fade timing
        bool active;
        uint8_t trail;              // Positions kept (0 = no trail)
        uint8_t kept;               // How many of them are filled so far
        int16_t tx[TRAIL_MAX], ty[TRAIL_MAX];  // Past positions, newest first
    };

    static const int POOL_SIZE = 120;  // Matches LanderFlux's larger pool
    Particle _pool[POOL_SIZE];

    // Drag coefficient applied each frame to slow particles naturally
    static constexpr float DRAG = 0.96f;

    void startTrail(Particle &p, int trail) {
        p.trail = (uint8_t)constrain(trail, 0, TRAIL_MAX);
        p.kept  = 0;
    }

    // An RGB565 colour at 1/2, 1/4 or 1/8 brightness, or black past that (each channel shifted,
    // the masks keeping one channel's bits out of the next).
    static uint16_t dimmed(uint16_t c, int shift) {
        static const uint16_t MASK[4] = { 0xFFFF, 0x7BEF, 0x39E7, 0x18E3 };
        if (shift <= 0) return c;
        if (shift > 3) return 0;
        return (c >> shift) & MASK[shift];
    }

    // A line from (x0, y0) to just short of (x1, y1), which the next newer
    // segment or the spark itself draws. Segments are a few pixels long, so
    // a simple stepped walk with a clip test per pixel is plenty.
    static void segment(GFXcanvas16 &canvas, int x0, int y0, int x1, int y1,
                        uint16_t color, int clipTop) {
        const int dx = x1 - x0, dy = y1 - y0;
        const int n = max(abs(dx), abs(dy));
        for (int i = 0; i < n; i++) {
            const int x = x0 + (dx * i + (dx >= 0 ? n / 2 : -n / 2)) / n;
            const int y = y0 + (dy * i + (dy >= 0 ? n / 2 : -n / 2)) / n;
            if (x < 0 || x >= canvas.width() || y < clipTop || y >= canvas.height()) continue;
            canvas.drawPixel(x, y, color);
        }
    }

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
                        int count, int lifespanMs = 600, int trail = 0) {
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
            startTrail(_pool[i], trail);
            spawned++;
        }
    }

    // -------------------------------------------------------------------------
    // TRIGGER EXPLOSION — LanderFlux API (kept for minimal port changes)
    // Spawns a large mixed-colour burst suitable for ship disintegration.
    // -------------------------------------------------------------------------
    void triggerExplosion(float centerX, float centerY, int count = 60, int trail = 0) {
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
            startTrail(_pool[i], trail);
            spawned++;
        }
    }

    // -------------------------------------------------------------------------
    // SPAWN FIRE — LanderFlux thrust trail style
    // Single directed particle with explicit velocity.
    // -------------------------------------------------------------------------
    void spawnFire(float x, float y, float vx, float vy,
                   uint16_t color = 0, int trail = 0) {
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
        startTrail(*p, trail);
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

            Particle &p = _pool[i];
            if (p.trail) {
                // Where it is now becomes the newest past position.
                for (int k = p.trail - 1; k > 0; k--) { p.tx[k] = p.tx[k - 1]; p.ty[k] = p.ty[k - 1]; }
                p.tx[0] = (int16_t)p.x;
                p.ty[0] = (int16_t)p.y;
                if (p.kept < p.trail) p.kept++;
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
            unsigned long remaining = _pool[i].expireMs - now;

            // The trail first, oldest and dimmest segment first, so newer
            // ones and the spark draw over it: half, a quarter, then an
            // eighth of the colour, a step dimmer in the last third of the
            // particle's life.
            const Particle &p = _pool[i];
            if (p.kept) {
                const int fade = remaining * 3 < p.totalLifeMs ? 1 : 0;
                for (int k = p.kept - 1; k >= 0; k--) {
                    const int toX = k ? p.tx[k - 1] : cx, toY = k ? p.ty[k - 1] : cy;
                    const uint16_t c = dimmed(p.color, min(k + 1, 3) + fade);
                    if (c) segment(canvas, p.tx[k], p.ty[k], toX, toY, c, clipTop);
                }
            }

            // Clip to visible area (respects UI margin if clipTop > 0)
            if (cx < 0 || cx >= canvas.width()) continue;
            if (cy < clipTop || cy >= canvas.height()) continue;

            // Fade to white in the last 80ms of life (AsteroidFlux style)
            uint16_t color = _pool[i].color;
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
