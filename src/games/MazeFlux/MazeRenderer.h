#ifndef MAZE_RENDERER_H
#define MAZE_RENDERER_H

#include <Adafruit_GFX.h>
#include "MazeConfig.h"
#include "MazeGenerator.h"
#include "cabinet/ArcadeConfig.h"

// The maze's walls, for the view whose top-left is at (camX, camY) maze
// pixels; the view starts under the HUD.
class MazeRenderer {
public:
    void draw(GFXcanvas16 &canvas, const MazeGenerator &maze, int camX, int camY) {
        using namespace mazecfg;
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        const uint16_t col = ArcadeConfig::COLOR_ION_BLUE;
        const int x0 = camX / CELL, y0 = camY / CELL;
        for (int my = y0; my <= (camY + VIEW_H) / CELL && my < maze.height; my++) {
            for (int mx = x0; mx <= (camX + VIEW_W) / CELL && mx < maze.width; mx++) {
                if (mx < 0 || my < 0) continue;
                const int px = mx * CELL - camX, py = my * CELL - camY + HUD_H;
                if (maze.hasWall(mx, my, WALL_N)) hline(canvas, px, py, CELL, col);
                if (maze.hasWall(mx, my, WALL_W)) vline(canvas, px, py, CELL, col);
                if (my == maze.height - 1 && maze.hasWall(mx, my, WALL_S)) hline(canvas, px, py + CELL, CELL, col);
                if (mx == maze.width - 1 && maze.hasWall(mx, my, WALL_E)) vline(canvas, px + CELL, py, CELL, col);
            }
        }
    }

private:
    // Lines kept below the HUD.
    static void hline(GFXcanvas16 &c, int x, int y, int w, uint16_t col) {
        if (y >= mazecfg::HUD_H) c.drawFastHLine(x, y, w, col);
    }
    static void vline(GFXcanvas16 &c, int x, int y, int h, uint16_t col) {
        if (y < mazecfg::HUD_H) { h -= mazecfg::HUD_H - y; y = mazecfg::HUD_H; }
        if (h > 0) c.drawFastVLine(x, y, h, col);
    }
};

#endif // MAZE_RENDERER_H
