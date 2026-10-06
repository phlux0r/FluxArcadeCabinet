#ifndef PLAYER_MAZE_H
#define PLAYER_MAZE_H

#include <Arduino.h>
#include "MazeConfig.h"
#include "MazeGenerator.h"

// The player: steps a cell at a time, gliding between cells over MOVE_MS
// (MOVE_BOOST_MS boosted). Held, the stick carries on cell to cell without
// a pause; pushed between steps, the direction is remembered (for
// BUFFER_MS) and taken at the next cell, so turning into a side passage
// doesn't need the push timed to the step.
class PlayerMaze {
public:
    enum Facing { FACE_DOWN, FACE_UP, FACE_LEFT, FACE_RIGHT };

    int  x = 0, y = 0;          // the cell it's in, or moving into
    int  fromX = 0, fromY = 0;  // the cell it's moving from
    bool moving  = false;
    bool arrived = false;       // true the update it reaches a cell
    int  lives   = 3;
    Facing  facing    = FACE_DOWN;
    uint8_t walkFrame = 0;      // flips each step

    static constexpr unsigned long BUFFER_MS = 250;

    unsigned long _moveStart = 0, _moveMs = mazecfg::MOVE_MS;
    unsigned long _boostEnd  = 0;
    uint8_t       _heldDir   = 0;     // last update's stick, a WALL_ bit
    uint8_t       _bufDir    = 0;
    unsigned long _bufAt     = 0;

    void reset(int sx, int sy) {
        x = fromX = sx; y = fromY = sy;
        moving = arrived = false;
        facing = FACE_DOWN;
        _boostEnd = 0;
        _bufDir = 0;
    }

    void applySpeedBoost() { _boostEnd = millis() + mazecfg::BOOST_MS; }
    bool boosted() const   { return millis() < _boostEnd; }

    // How far into the current step, 0..1.
    float progress() const {
        if (!moving) return 1.0f;
        const float t = (float)(millis() - _moveStart) / (float)_moveMs;
        return t > 1.0f ? 1.0f : t;
    }
    // The centre in maze pixels.
    float px() const { const float t = progress(); return ((fromX + (x - fromX) * t) + 0.5f) * mazecfg::CELL; }
    float py() const { const float t = progress(); return ((fromY + (y - fromY) * t) + 0.5f) * mazecfg::CELL; }
    // The cell it's mostly in.
    int nearX() const { return progress() < 0.5f ? fromX : x; }
    int nearY() const { return progress() < 0.5f ? fromY : y; }

    // up/down/left/right as on the screen. canPass(x, y, dir): whether the
    // way out of cell (x, y) towards dir is open.
    template <typename CanPass>
    void update(bool up, bool down, bool left, bool right, CanPass canPass) {
        const unsigned long now = millis();
        arrived = false;
        unsigned long endedAt = 0;
        if (moving && now - _moveStart >= _moveMs) {
            moving  = false;
            arrived = true;
            endedAt = _moveStart + _moveMs;
            fromX = x; fromY = y;
        }

        const uint8_t held = up ? WALL_N : down ? WALL_S : left ? WALL_W : right ? WALL_E : 0;
        if (held && held != _heldDir) { _bufDir = held; _bufAt = now; }
        _heldDir = held;
        if (_bufDir && now - _bufAt > BUFFER_MS) _bufDir = 0;
        if (moving) return;

        // The remembered push first, if it leads anywhere, else the stick.
        uint8_t dir = 0;
        if (_bufDir && canPass(x, y, _bufDir)) dir = _bufDir;
        else if (held) dir = held;
        if (!dir) return;
        facing = dir == WALL_N ? FACE_UP : dir == WALL_S ? FACE_DOWN : dir == WALL_W ? FACE_LEFT : FACE_RIGHT;
        if (!canPass(x, y, dir)) return;

        fromX = x; fromY = y;
        x += MazeGenerator::dx(dir);
        y += MazeGenerator::dy(dir);
        moving = true;
        // Straight on from the step just finished, without losing the
        // time this frame ran over.
        _moveStart = endedAt && now - endedAt < 100 ? endedAt : now;
        _moveMs = boosted() ? mazecfg::MOVE_BOOST_MS : mazecfg::MOVE_MS;
        walkFrame ^= 1;
        if (dir == _bufDir) _bufDir = 0;
    }
};

#endif // PLAYER_MAZE_H
