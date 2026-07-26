#ifndef MAZE_RENDERER_H
#define MAZE_RENDERER_H

#include <Adafruit_GFX.h>
#include "MazeGenerator.h"
#include "SpriteManager.h"
#include "cabinet/ArcadeConfig.h"

class MazeRenderer {
public:
    static const int TILE = 8;
    static const int HUD_HEIGHT = 10;

    void draw(GFXcanvas16 &canvas, const MazeGenerator &maze, int camX, int camY) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);

        int tilesX = ArcadeConfig::PORTRAIT_WIDTH  / TILE;
        int tilesY = (ArcadeConfig::PORTRAIT_HEIGHT - HUD_HEIGHT) / TILE;

        for (int ty = 0; ty < tilesY; ty++) {
            for (int tx = 0; tx < tilesX; tx++) {
                int mx = camX + tx;
                int my = camY + ty;
                if (mx < 0 || mx >= maze.width) continue;
                if (my < 0 || my >= maze.height) continue;

                int px = tx * TILE;
                int py = ty * TILE + HUD_HEIGHT;

                // N wall
                if (maze.hasWall(mx, my, WALL_N))
                    canvas.drawFastHLine(px, py, TILE, ArcadeConfig::COLOR_ION_BLUE);
                // W wall
                if (maze.hasWall(mx, my, WALL_W))
                    canvas.drawFastVLine(px, py, TILE, ArcadeConfig::COLOR_ION_BLUE);
                // S wall (bottom row only, to avoid double-drawing)
                if (my == maze.height - 1 && maze.hasWall(mx, my, WALL_S))
                    canvas.drawFastHLine(px, py + TILE, TILE, ArcadeConfig::COLOR_ION_BLUE);
                // E wall (right col only)
                if (mx == maze.width - 1 && maze.hasWall(mx, my, WALL_E))
                    canvas.drawFastVLine(px + TILE, py, TILE, ArcadeConfig::COLOR_ION_BLUE);
            }
        }
    }
};

#endif // MAZE_RENDERER_H