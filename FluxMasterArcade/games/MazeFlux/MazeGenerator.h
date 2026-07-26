#ifndef MAZE_GENERATOR_H
#define MAZE_GENERATOR_H

#include <Arduino.h>

#define WALL_N 0x01
#define WALL_S 0x02
#define WALL_E 0x04
#define WALL_W 0x08
#define CELL_VISITED 0x10

class MazeGenerator {
public:
    static const int MAX_W = 50;
    static const int MAX_H = 50;

    uint8_t grid[MAX_W * MAX_H];
    int width  = 16;
    int height = 20;

    void generate(int w, int h) {
        width  = constrain(w, 16, MAX_W);
        height = constrain(h, 20, MAX_H);
        memset(grid, WALL_N | WALL_S | WALL_E | WALL_W, sizeof(grid));

        // Iterative backtracker using explicit stack
        static int stackX[MAX_W * MAX_H];
        static int stackY[MAX_W * MAX_H];
        int stackTop = 0;

        stackX[stackTop] = 0;
        stackY[stackTop] = 0;
        stackTop++;
        grid[0] |= CELL_VISITED;

        while (stackTop > 0) {
            int x = stackX[stackTop - 1];
            int y = stackY[stackTop - 1];

            // Build list of unvisited neighbours
            uint8_t dirs[4]  = { WALL_N, WALL_S, WALL_E, WALL_W };
            int     nx[4], ny[4];
            int     count = 0;

            for (int i = 0; i < 4; i++) {
                int cx = x, cy = y;
                if      (dirs[i] == WALL_N) cy--;
                else if (dirs[i] == WALL_S) cy++;
                else if (dirs[i] == WALL_E) cx++;
                else if (dirs[i] == WALL_W) cx--;

                if (cx < 0 || cx >= width || cy < 0 || cy >= height) continue;
                if (grid[cy * width + cx] & CELL_VISITED) continue;

                dirs[count] = dirs[i];
                nx[count]   = cx;
                ny[count]   = cy;
                count++;
            }

            if (count == 0) {
                stackTop--;  // backtrack
                continue;
            }

            // Pick random unvisited neighbour
            int chosen = random(count);
            uint8_t dir = dirs[chosen];
            int cnx = nx[chosen];
            int cny = ny[chosen];

            // Carve wall
            uint8_t opposite = (dir == WALL_N) ? WALL_S :
                               (dir == WALL_S) ? WALL_N :
                               (dir == WALL_E) ? WALL_W : WALL_E;
            grid[y   * width + x  ] &= ~dir;
            grid[cny * width + cnx] &= ~opposite;
            grid[cny * width + cnx] |= CELL_VISITED;

            stackX[stackTop] = cnx;
            stackY[stackTop] = cny;
            stackTop++;
        }
    }

    bool hasWall(int x, int y, uint8_t dir) const {
        return grid[y * width + x] & dir;
    }
};

#endif // MAZE_GENERATOR_H
   