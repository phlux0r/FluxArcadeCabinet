#ifndef OBSTACLES_H
#define OBSTACLES_H

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Bullet
// -----------------------------------------------------------------------------
struct Bullet {
    float x, y;
    int   dx, dy;   // direction: one of {-1,0,1}
    bool  active = false;

    void spawn(int startX, int startY, int dirX, int dirY) {
        x = startX; y = startY;
        dx = dirX;  dy = dirY;
        active = true;
    }

    void update() {
        if (!active) return;
        x += dx;
        y += dy;
    }
};

// -----------------------------------------------------------------------------
// ProximityBomb
// -----------------------------------------------------------------------------
class ProximityBomb {
public:
    int  x, y;
    bool active    = true;
    bool exploded  = false;

    // Fuse state
    int           fuseCount   = 0;   // 3,2,1 then boom
    bool          fuseActive  = false;
    unsigned long _lastTickMs = 0;

    static const int PROXIMITY_RADIUS = 2;  // tiles
    static const int FUSE_TICK_MS     = 800;

    void init(int tx, int ty) {
        x = tx; y = ty;
        active = true; exploded = false;
        fuseActive = false; fuseCount = 0;
    }

    // Returns true if bomb just exploded this update
    bool update(int playerX, int playerY) {
        if (!active || exploded) return false;

        int dx = abs(playerX - x);
        int dy = abs(playerY - y);
        bool inRange = (dx <= PROXIMITY_RADIUS && dy <= PROXIMITY_RADIUS);

        if (!inRange) {
            // Reset fuse if player backs off
            fuseActive = false;
            fuseCount  = 0;
            return false;
        }

        if (!fuseActive) {
            fuseActive  = true;
            fuseCount   = 3;
            _lastTickMs = millis();
            return false;
        }

        unsigned long now = millis();
        if (now - _lastTickMs >= (unsigned long)FUSE_TICK_MS) {
            _lastTickMs = now;
            fuseCount--;
            if (fuseCount <= 0) {
                exploded = true;
                active   = false;
                return true;
            }
        }
        return false;
    }
};

// -----------------------------------------------------------------------------
// TrapEmitter
// -----------------------------------------------------------------------------
class TrapEmitter {
public:
    static const int MAX_BULLETS = 4;

    enum Type { TYPE_A, TYPE_B };

    int   x, y;
    int   dirX, dirY;   // bullet travel direction
    Type  type;
    bool  active       = true;
    bool  switchPaused = false;   // Type B only

    Bullet bullets[MAX_BULLETS];

private:
    unsigned long _lastFireMs  = 0;
    unsigned long _pauseEndMs  = 0;

    // Type A: generous timing — fire interval long enough to traverse corridor
    // Type B: tight timing — requires switch activation to pause
    int _fireIntervalMs  = 0;
    int _bulletRangeMs   = 0;   // how long bullet lives before despawning

public:
    static const int PAUSE_DURATION_MS   = 3000;  // Type B switch pause
    static const int BULLET_SPEED_TILES  = 1;     // tiles per tick

    void init(int tx, int ty, int dx, int dy, Type t, int corridorLength) {
        x = tx; y = ty; dirX = dx; dirY = dy; type = t;
        active = true; switchPaused = false;
        for (auto &b : bullets) b.active = false;

        // Type A: interval = travel time across corridor + safe gap
        // Type B: tight interval, barely passable without switch
        int travelMs = corridorLength * 200;  // ~200ms per tile (player move speed)
        if (type == TYPE_A) {
            _fireIntervalMs = travelMs * 2;   // generous: full corridor + equal gap
        } else {
            _fireIntervalMs = travelMs / 2;   // tight: impossible to traverse
        }
        _bulletRangeMs = travelMs;
        _lastFireMs    = millis();
    }

    void activateSwitch() {
        if (type == TYPE_B) {
            switchPaused = true;
            _pauseEndMs  = millis() + PAUSE_DURATION_MS;
        }
    }

    void update() {
        if (!active) return;

        unsigned long now = millis();

        // Handle Type B pause
        if (switchPaused && now > _pauseEndMs) switchPaused = false;

        // Update existing bullets
        for (auto &b : bullets) {
            if (!b.active) continue;
            b.update();
            // Despawn after range exceeded (simple time-based)
            if (now - _lastFireMs > (unsigned long)_bulletRangeMs) b.active = false;
        }

        if (switchPaused) return;

        // Fire new bullet
        if (now - _lastFireMs >= (unsigned long)_fireIntervalMs) {
            _lastFireMs = now;
            for (auto &b : bullets) {
                if (!b.active) {
                    b.spawn(x, y, dirX, dirY);
                    break;
                }
            }
        }
    }

    // Returns true if any bullet occupies tile (tx, ty)
    bool checkBulletHit(int tx, int ty) const {
        for (const auto &b : bullets) {
            if (!b.active) continue;
            if ((int)b.x == tx && (int)b.y == ty) return true;
        }
        return false;
    }
};

#endif // OBSTACLES_H
