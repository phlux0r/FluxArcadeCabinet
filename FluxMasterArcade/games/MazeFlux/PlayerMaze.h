#ifndef PLAYER_MAZE_H
#define PLAYER_MAZE_H

#include <Arduino.h>
#include "MazeGenerator.h"

class PlayerMaze {
public:
    int x = 0;
    int y = 0;
    bool interact = false;

    int  lives        = 3;
    bool alive        = true;
    bool speedBoost   = false;

private:
    unsigned long _lastMoveMs    = 0;
    unsigned long _speedBoostEnd = 0;

    static const int MOVE_DELAY_MS       = 200;
    static const int MOVE_DELAY_BOOST_MS = 100;

public:
    void reset(int startX, int startY) {
        x = startX; y = startY;
        alive = true;
        speedBoost = false;
        interact = false;
        _lastMoveMs = 0;
        _speedBoostEnd = 0;
    }

    void applySpeedBoost() {
        speedBoost = true;
        _speedBoostEnd = millis() + 5000;
    }

    void update(bool joyUp, bool joyDown, bool joyLeft, bool joyRight,
                bool btnA, const MazeGenerator &maze) {
        interact = false;

        if (btnA) interact = true;

        // Speed boost expiry
        if (speedBoost && millis() > _speedBoostEnd) speedBoost = false;

        unsigned long now = millis();
        int delay = speedBoost ? MOVE_DELAY_BOOST_MS : MOVE_DELAY_MS;
        if (now - _lastMoveMs < (unsigned long)delay) return;

        int nx = x, ny = y;
        uint8_t wall = 0;

        if      (joyUp)    { ny++; wall = WALL_S; }
        else if (joyDown)  { ny--; wall = WALL_N; }
        else if (joyLeft)  { nx--; wall = WALL_W; }
        else if (joyRight) { nx++; wall = WALL_E; }
        else return;

        if (nx < 0 || nx >= maze.width || ny < 0 || ny >= maze.height) return;
        if (maze.hasWall(x, y, wall)) return;

        x = nx; y = ny;
        _lastMoveMs = now;
    }
};

#endif // PLAYER_MAZE_H