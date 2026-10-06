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
// TRAIL_MAX). A particle with one remembers where it was, and how bright,
// on its last few updates, and render() draws lines back through them,
// each step dimmer, so it leaves a fading streak like Resonance's shards
// (which get theirs from the scope's afterglow; these games draw straight
// to the screen). To look like the afterglow it also dims out at the end
// of its life instead of flashing white, and its trail lingers for a few
// updates after it, still fading.
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
        uint8_t draining;           // Updates left for the trail after it dies
        int16_t tx[TRAIL_MAX], ty[TRAIL_MAX];  // Past positions, newest first
        uint8_t tq[TRAIL_MAX];      // and how bright it was at each (0-255)
    };

    static const int POOL_SIZE = 120;  // Matches LanderFlux's larger pool
    Particle _pool[POOL_SIZE];

    // Drag coefficient applied each frame to slow particles naturally
    static constexpr float DRAG = 0.96f;

    void startTrail(Particle &p, int trail) {
        p.trail    = (uint8_t)constrain(trail, 0, TRAIL_MAX);
        p.kept     = 0;
        p.draining = 0;
    }

    // How bright a trailed particle is now (0-256, as scaled() takes it):
    // full, then down to a quarter over the last 40% of its life, when it
    // goes, leaving its trail to drain from there (dimming to nothing would
    // leave a trail too dark to see linger).
    static int brightness(const Particle &p, unsigned long now) {
        if (now >= p.expireMs) return 0;
        const unsigned long remaining = p.expireMs - now;
        const unsigned long fadeMs = p.totalLifeMs * 2 / 5;
        if (fadeMs == 0 || remaining >= fadeMs) return 256;
        return (int)(64 + 192 * remaining / fadeMs);
    }

    // An RGB565 colour scaled by q/256, channel by channel.
    static uint16_t scaled(uint16_t c, int q) {
        const int r = ((c >> 11) * q) >> 8, g = (((c >> 5) & 63) * q) >> 8, b = ((c & 31) * q) >> 8;
        return (uint16_t)((r << 11) | (g << 5) | b);
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

            Particle &p = _pool[i];
            const bool dead = now >= p.expireMs;
            if (dead && p.kept == 0) {
                p.active = false;
                continue;
            }
            // A dead particle's trail drains: it stops where it is and
            // dark positions are pushed in behind it, one an update, until
            // the last lit one has gone off the end.
            if (dead && p.draining == 0) p.draining = p.trail + 1;
            if (dead && --p.draining == 0) {
                p.active = false;
                continue;
            }

            if (p.trail) {
                // Where it is now becomes the newest past position.
                for (int k = p.trail - 1; k > 0; k--) {
                    p.tx[k] = p.tx[k - 1]; p.ty[k] = p.ty[k - 1]; p.tq[k] = p.tq[k - 1];
                }
                p.tx[0] = (int16_t)p.x;
                p.ty[0] = (int16_t)p.y;
                p.tq[0] = (uint8_t)min(brightness(p, now), 255);
                if (p.kept < p.trail) p.kept++;
            }
            if (dead) continue;

            _pool[i].x  += _pool[i].vx;
            _pool[i].y  += _pool[i].vy;
            _pool[i].vx *= DRAG;
            _pool[i].vy *= DRAG;
        }
    }

    // -------------------------------------------------------------------------
    // RENDER — call after update(), before canvas flush. offX/offY: a
    // scrolling game's camera, subtracted from every position (particles
    // spawned where things are in its world).
    // -------------------------------------------------------------------------
    void render(GFXcanvas16 &canvas, int clipTop = 0, int offX = 0, int offY = 0) {
        unsigned long now = millis();
        for (int i = 0; i < POOL_SIZE; i++) {
            if (!_pool[i].active) continue;

            int cx = (int)_pool[i].x - offX;
            int cy = (int)_pool[i].y - offY;
            unsigned long remaining = _pool[i].expireMs - now;

            // The trail first, oldest and dimmest segment first, so newer
            // ones and the spark draw over it. Each position keeps the
            // brightness the spark had there, times a falloff back along
            // the trail (roughly the afterglow's, a step an update).
            const Particle &p = _pool[i];
            if (p.kept) {
                static const uint8_t FALLOFF[TRAIL_MAX] = { 180, 115, 64, 31 };
                for (int k = p.kept - 1; k >= 0; k--) {
                    const int toX = k ? p.tx[k - 1] - offX : cx, toY = k ? p.ty[k - 1] - offY : cy;
                    const int q = (p.tq[k] * FALLOFF[k]) >> 8;
                    if (q) segment(canvas, p.tx[k] - offX, p.ty[k] - offY, toX, toY, scaled(p.color, q), clipTop);
                }
            }
            if (now >= p.expireMs) continue;   // just its trail, draining

            // Clip to visible area (respects UI margin if clipTop > 0)
            if (cx < 0 || cx >= canvas.width()) continue;
            if (cy < clipTop || cy >= canvas.height()) continue;

            // A trailed spark dims out; others fade to white in the last
            // 80ms of life (AsteroidFlux style)
            uint16_t color = _pool[i].color;
            if (p.trail) color = scaled(color, brightness(p, now));
            else if (remaining < 80) color = ArcadeConfig::COLOR_WHITE;

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
