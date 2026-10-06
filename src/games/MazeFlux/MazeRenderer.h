#ifndef MAZE_RENDERER_H
#define MAZE_RENDERER_H

#include <Adafruit_GFX.h>
#include "MazeConfig.h"
#include "MazeGenerator.h"
#include "cabinet/ArcadeConfig.h"

// =============================================================================
// The maze's floor and walls, for the view whose top-left is at (camX,
// camY) maze pixels (the view starts under the HUD). Walls are WALL_T
// thick: each cell draws a WALL_T / 2 band along each side it has a wall
// on, its neighbour the other half, with a post at every corner. Wall
// faces towards the floor are shaded as if lit from above (light on top,
// dark beneath). Each level has a theme: colours and a texture.
// Written straight into the canvas's buffer (portrait, unrotated).
// =============================================================================

struct MazeTheme {
    uint16_t floor;
    uint16_t wall, light, dark;
    uint8_t  pattern;      // MazeRenderer::PAT_*
};

class MazeRenderer {
public:
    enum { PAT_BRICK, PAT_STONE, PAT_CRYSTAL, PAT_CIRCUIT, PAT_DIAGONAL, PAT_TILES, PAT_COUNT };

    static constexpr uint16_t rgb(int r, int g, int b) {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    // A theme a level, round again after the last.
    static const MazeTheme &themeFor(int level) {
        static const MazeTheme THEMES[] = {
            { rgb(10, 14, 34),  rgb(40, 90, 200),  rgb(120, 170, 255), rgb(15, 35, 90),  PAT_BRICK },     // ion blue
            { rgb(12, 22, 12),  rgb(60, 130, 60),  rgb(140, 210, 120), rgb(25, 55, 25),  PAT_STONE },     // moss
            { rgb(30, 10, 6),   rgb(190, 80, 30),  rgb(255, 170, 90),  rgb(80, 25, 10),  PAT_BRICK },     // ember
            { rgb(20, 8, 30),   rgb(130, 60, 190), rgb(210, 150, 255), rgb(50, 20, 80),  PAT_CRYSTAL },   // amethyst
            { rgb(14, 16, 20),  rgb(105, 110, 125), rgb(190, 195, 210), rgb(45, 48, 58), PAT_CIRCUIT },   // steel
            { rgb(26, 20, 6),   rgb(180, 140, 40), rgb(255, 225, 120), rgb(85, 60, 15),  PAT_DIAGONAL },  // gold
            { rgb(4, 22, 24),   rgb(30, 150, 150), rgb(130, 240, 230), rgb(10, 60, 65),  PAT_TILES },     // teal
            { rgb(28, 8, 16),   rgb(190, 40, 90),  rgb(255, 140, 180), rgb(80, 12, 35),  PAT_STONE },     // rose
        };
        const int n = sizeof(THEMES) / sizeof(THEMES[0]);
        return THEMES[(level - 1) % n];
    }

    void draw(GFXcanvas16 &canvas, const MazeGenerator &maze, int camX, int camY, const MazeTheme &theme) {
        using namespace mazecfg;
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        _buf = canvas.getBuffer();
        _w = canvas.width();
        _h = canvas.height();
        _camX = camX;
        _camY = camY - HUD_H;   // maze y to screen y: minus this
        _t = &theme;

        // Floor over the maze's part of the view.
        const int fx0 = max(0, -camX), fy0 = max(HUD_H, HUD_H - camY);
        const int fx1 = min(_w, maze.width * CELL - camX), fy1 = min(_h, maze.height * CELL - camY + HUD_H);
        if (fx1 > fx0 && fy1 > fy0) canvas.fillRect(fx0, fy0, fx1 - fx0, fy1 - fy0, theme.floor);

        const int H = WALL_T / 2;
        const int x0 = max(0, camX / CELL - 1), y0 = max(0, camY / CELL - 1);
        const int x1 = min(maze.width - 1, (camX + VIEW_W) / CELL), y1 = min(maze.height - 1, (camY + VIEW_H) / CELL);
        for (int my = y0; my <= y1; my++) {
            for (int mx = x0; mx <= x1; mx++) {
                const int wx = mx * CELL, wy = my * CELL;
                // Bands (between the corner posts), shaded on their floor side.
                if (maze.hasWall(mx, my, WALL_N)) band(wx + H, wy, CELL - 2 * H, H, 0, H - 1);
                if (maze.hasWall(mx, my, WALL_S)) band(wx + H, wy + CELL - H, CELL - 2 * H, H, 1, 0);
                if (maze.hasWall(mx, my, WALL_W)) band(wx, wy + H, H, CELL - 2 * H, 2, H - 1);
                if (maze.hasWall(mx, my, WALL_E)) band(wx + CELL - H, wy + H, H, CELL - 2 * H, 3, 0);
                // The outside edge as thick as the rest, beyond the maze.
                if (mx == 0)               band(wx - H, wy - H, H, CELL + 2 * H, -1, 0);
                if (mx == maze.width - 1)  band(wx + CELL, wy - H, H, CELL + 2 * H, -1, 0);
                if (my == 0)               band(wx, wy - H, CELL, H, -1, 0);
                if (my == maze.height - 1) band(wx, wy + CELL, CELL, H, -1, 0);
                // Corner posts.
                band(wx, wy, H, H, -1, 0);
                band(wx + CELL - H, wy, H, H, -1, 0);
                band(wx, wy + CELL - H, H, H, -1, 0);
                band(wx + CELL - H, wy + CELL - H, H, H, -1, 0);
            }
        }
    }

private:
    uint16_t* _buf = nullptr;
    int _w = 0, _h = 0, _camX = 0, _camY = 0;
    const MazeTheme* _t = nullptr;

    // The texture at maze pixel (x, y).
    uint16_t texel(int x, int y) const {
        const MazeTheme &t = *_t;
        switch (t.pattern) {
            case PAT_BRICK: {
                const int row = y >> 2;
                if ((y & 3) == 0 || ((x + (row & 1) * 4) & 7) == 0) return t.dark;
                return t.wall;
            }
            case PAT_STONE: {
                uint32_t h = (uint32_t)(x >> 1) * 73856093u ^ (uint32_t)(y >> 1) * 19349663u;
                h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                const int v = h & 7;
                return v == 0 ? t.dark : v == 7 ? t.light : t.wall;
            }
            case PAT_CRYSTAL: return ((x + y) % 6) < 2 ? t.light : t.wall;
            case PAT_CIRCUIT: return ((x & 3) == 1 && (y & 3) == 1) ? t.light : ((x & 7) == 0 ? t.dark : t.wall);
            case PAT_DIAGONAL: return ((x - y) & 7) < 2 ? t.dark : t.wall;
            default: return ((x & 3) == 0 || (y & 3) == 0) ? t.dark : t.wall;   // PAT_TILES
        }
    }

    // A w x h block of wall at maze pixel (x, y). face: which side faces
    // the floor (0 the bottom row, 1 the top row, 2 the right column, 3
    // the left column; -1 none), shaded at index edge along it.
    void band(int x, int y, int w, int h, int face, int edge) {
        for (int j = 0; j < h; j++) {
            const int sy = y + j - _camY;
            if (sy < mazecfg::HUD_H || sy >= _h) continue;
            uint16_t* row = _buf + sy * _w;
            for (int i = 0; i < w; i++) {
                const int sx = x + i - _camX;
                if (sx < 0 || sx >= _w) continue;
                uint16_t c;
                if      (face == 0 && j == edge) c = _t->dark;    // underside
                else if (face == 1 && j == edge) c = _t->light;   // top
                else if (face == 2 && i == edge) c = _t->dark;
                else if (face == 3 && i == edge) c = _t->light;
                else c = texel(x + i, y + j);
                row[sx] = c;
            }
        }
    }
};

#endif // MAZE_RENDERER_H
