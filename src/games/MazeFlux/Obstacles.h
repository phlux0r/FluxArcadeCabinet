#ifndef OBSTACLES_H
#define OBSTACLES_H

#include <Arduino.h>
#include "MazeConfig.h"
#include "MazeGenerator.h"

// -----------------------------------------------------------------------------
// ProximityBomb: a fuse starts once you're within reach (BOMB_RANGE steps,
// as the game measures it, walls in the way), counts down while you stay,
// and resets if you back off. At zero it goes off, catching you if you're
// still within reach.
// -----------------------------------------------------------------------------
class ProximityBomb {
public:
    int  x, y;
    bool active     = false;
    int  fuseCount  = 0;      // FUSE_TICKS down to 1, then boom
    bool fuseActive = false;
    unsigned long _lastTickMs = 0;

    void init(int tx, int ty) {
        x = tx; y = ty;
        active = true;
        fuseActive = false; fuseCount = 0;
    }

    void disarm() { fuseActive = false; fuseCount = 0; }

    // inReach: whether you're within its reach now. True the update it
    // goes off.
    bool update(bool inReach) {
        if (!active) return false;
        if (!inReach) { disarm(); return false; }
        const unsigned long now = millis();
        if (!fuseActive) {
            fuseActive  = true;
            fuseCount   = mazecfg::FUSE_TICKS;
            _lastTickMs = now;
            return false;
        }
        if (now - _lastTickMs >= mazecfg::FUSE_TICK_MS) {
            _lastTickMs = now;
            if (--fuseCount <= 0) {
                active = false;
                return true;
            }
        }
        return false;
    }
};

// -----------------------------------------------------------------------------
// TrapEmitter: fires bullets from its cell along (dirX, dirY), down a
// straight corridor (its lane), every interval. Bullets move in pixels at
// BULLET_CELLS_PER_S whatever the frame rate, and stop at the first wall
// (or closed door) they meet. Type A leaves room to walk the whole lane,
// either way, between bullets. Type B fires too often to run through; A
// pressed in or beside its lane (its switch) clears its bullets and pauses
// it for SWITCH_PAUSE_MS.
// -----------------------------------------------------------------------------
class TrapEmitter {
public:
    enum Type { TYPE_A, TYPE_B };

    struct Bullet {
        float x, y;          // px, centre, in the maze (not the screen)
        int   cellX, cellY;  // the cell it's in
        bool  active = false;
    };

    int   x, y;
    int   dirX, dirY;
    int   laneLen = 0;   // cells of lane in front of it
    Type  type;
    bool  active = false;
    Bullet bullets[mazecfg::MAX_BULLETS];

    unsigned long _lastFireMs = 0;
    unsigned long _pauseEndMs = 0;
    unsigned long _intervalMs = 0;

    // lane: cells of straight corridor in front of it. Type A's interval
    // is a bullet's run down it, then a walk along all of it (and the
    // emitter's cell) at the unboosted pace, then TRAP_A_SPARE_MS.
    void init(int tx, int ty, int dx, int dy, Type t, int lane) {
        x = tx; y = ty; dirX = dx; dirY = dy; type = t;
        laneLen = lane;
        active = true;
        for (auto &b : bullets) b.active = false;
        const unsigned long runMs  = (unsigned long)(lane * 1000.0f / mazecfg::BULLET_CELLS_PER_S);
        const unsigned long walkMs = (lane + 1) * mazecfg::MOVE_MS;
        _intervalMs = t == TYPE_A ? runMs + walkMs + mazecfg::TRAP_A_SPARE_MS : mazecfg::TRAP_B_INTERVAL_MS;
        _lastFireMs = millis();
        _pauseEndMs = 0;
    }

    bool paused() const { return millis() < _pauseEndMs; }

    // Whether (cx, cy) is in its line of fire (its own cell included).
    bool inLane(int cx, int cy) const {
        for (int i = 0; i <= laneLen; i++) if (cx == x + dirX * i && cy == y + dirY * i) return true;
        return false;
    }
    // Whether its switch can be reached from (cx, cy): in the lane or
    // beside it.
    bool switchReach(int cx, int cy) const {
        for (int i = 0; i <= laneLen; i++)
            if (abs(cx - (x + dirX * i)) + abs(cy - (y + dirY * i)) <= 1) return true;
        return false;
    }
    unsigned long msToNextShot() const {
        const unsigned long since = millis() - _lastFireMs;
        return since >= _intervalMs ? 0 : _intervalMs - since;
    }
    bool bulletsInFlight() const {
        for (const auto &b : bullets) if (b.active) return true;
        return false;
    }

    // Type B's switch: no more bullets for a while, and those in the air
    // gone, so the lane is clear at once.
    void activateSwitch() {
        if (type != TYPE_B) return;
        _pauseEndMs = millis() + mazecfg::SWITCH_PAUSE_MS;
        clearBullets();
    }

    void clearBullets() { for (auto &b : bullets) b.active = false; }

    // dtMs since the last update. canPass(x, y, dir) says whether the way
    // out of cell (x, y) towards dir is open.
    template <typename CanPass>
    void update(unsigned long dtMs, CanPass canPass) {
        if (!active) return;
        const float step = mazecfg::BULLET_CELLS_PER_S * mazecfg::CELL * dtMs / 1000.0f;
        const uint8_t dir = dirX > 0 ? WALL_E : dirX < 0 ? WALL_W : dirY > 0 ? WALL_S : WALL_N;
        for (auto &b : bullets) {
            if (!b.active) continue;
            b.x += dirX * step;
            b.y += dirY * step;
            // Into the next cell only through an open way.
            const int cx = (int)floorf(b.x / mazecfg::CELL), cy = (int)floorf(b.y / mazecfg::CELL);
            while (b.active && (cx != b.cellX || cy != b.cellY)) {
                if (!canPass(b.cellX, b.cellY, dir)) { b.active = false; break; }
                b.cellX += dirX; b.cellY += dirY;
            }
        }
        const unsigned long now = millis();
        if (paused()) { _lastFireMs = now; return; }
        if (now - _lastFireMs >= _intervalMs) {
            _lastFireMs = now;
            for (auto &b : bullets) {
                if (b.active) continue;
                b.x = (x + 0.5f) * mazecfg::CELL;
                b.y = (y + 0.5f) * mazecfg::CELL;
                b.cellX = x; b.cellY = y;
                b.active = true;
                break;
            }
        }
    }
};

#endif // OBSTACLES_H
