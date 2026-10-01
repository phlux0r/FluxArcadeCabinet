#ifndef BRICK_BOARD_H
#define BRICK_BOARD_H

#include <Adafruit_GFX.h>
#include "BrickConfig.h"
#include "BrickLevels.h"

// =============================================================================
// BRICK FLUX — the brick grid and the advancing wall.
//
// The grid is fixed in shape (COLS x ROWS cells); the whole formation moves
// by its top edge, _top, which steps down a row at a time. A cell's
// rectangle is the full 8x5 pitch, gap included, so the ball (3px) can
// never slip between two bricks; only the drawn brick is 7x4.
// =============================================================================

namespace brickflux {

class BrickBoard {
public:
    enum Kind : uint8_t { EMPTY, NEUTRAL, HARD, STEEL };
    struct Cell { uint8_t kind, hits, maxHits; };

    // What hit() did, for the scoring and the effects.
    struct HitResult { bool hit = false, broken = false; int points = 0; uint16_t colour = 0; };

    void load(int layout) {
        layout = ((layout % LAYOUT_COUNT) + LAYOUT_COUNT) % LAYOUT_COUNT;
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c) {
                Cell &k = _cells[r][c];
                k = Cell{ EMPTY, 0, 0 };
                switch ((char)pgm_read_byte(&LAYOUTS[layout][r][c])) {
                    case 'n': k = Cell{ NEUTRAL, 1, 1 }; break;
                    case 'h': k = Cell{ HARD, 2, 2 }; break;
                    case 'H': k = Cell{ HARD, 3, 3 }; break;
                    case 's': k = Cell{ STEEL, 1, 1 }; break;
                    default: break;
                }
            }
        _top = GRID_Y0;
    }

    float top() const { return _top; }
    const Cell& cell(int r, int c) const { return _cells[r][c]; }

    static float cellX(int c) { return (float)(GRID_X + c * CELL_W); }
    float cellY(int r) const { return _top + r * CELL_H; }

    bool solid(int r, int c) const {
        return r >= 0 && r < ROWS && c >= 0 && c < COLS && _cells[r][c].kind != EMPTY;
    }
    static bool breakable(const Cell &k) { return k.kind == NEUTRAL || k.kind == HARD; }

    // The cells a box (x0,y0)-(x1,y1) overlaps, as a row/column range;
    // false if it's clear of the grid entirely.
    bool span(float x0, float y0, float x1, float y1, int &r0, int &r1, int &c0, int &c1) const {
        c0 = (int)floorf((x0 - GRID_X) / CELL_W);
        c1 = (int)floorf((x1 - GRID_X) / CELL_W);
        r0 = (int)floorf((y0 - _top) / CELL_H);
        r1 = (int)floorf((y1 - _top) / CELL_H);
        if (c0 < 0) c0 = 0;
        if (c1 >= COLS) c1 = COLS - 1;
        if (r0 < 0) r0 = 0;
        if (r1 >= ROWS) r1 = ROWS - 1;
        return c0 <= c1 && r0 <= r1;
    }

    // The solid cell a ball-sized box at (x, y) overlaps that's nearest its
    // centre, or false.
    bool solidAt(float x, float y, float half, int &row, int &col) const {
        int r0, r1, c0, c1;
        if (!span(x - half, y - half, x + half - 0.001f, y + half - 0.001f, r0, r1, c0, c1)) return false;
        float best = 1e9f;
        bool found = false;
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c) {
                if (!solid(r, c)) continue;
                float dx = cellX(c) + CELL_W * 0.5f - x, dy = cellY(r) + CELL_H * 0.5f - y;
                float d = dx * dx + dy * dy;
                if (d < best) { best = d; row = r; col = c; found = true; }
            }
        return found;
    }

    // One hit on a cell. `smash` breaks anything, steel included, outright.
    HitResult hit(int r, int c, bool smash) {
        HitResult res;
        if (!solid(r, c)) return res;
        Cell &k = _cells[r][c];
        res.hit = true;
        res.colour = colourOf(r, k);
        if (k.kind == STEEL && !smash) return res;
        if (k.kind == HARD) res.points = PTS_HARD * (smash ? k.hits : 1);
        else if (k.kind == STEEL) res.points = PTS_STEEL;
        else res.points = PTS_NEUTRAL;
        if (smash || k.hits <= 1) {
            res.broken = true;
            k = Cell{ EMPTY, 0, 0 };
        } else {
            --k.hits;
        }
        return res;
    }

    // Bricks the level needs gone (steel doesn't count).
    int remaining() const {
        int n = 0;
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c) n += breakable(_cells[r][c]);
        return n;
    }

    // The bottom edge of the lowest breakable brick, or -1 with none left.
    float lowestBottom() const {
        for (int r = ROWS - 1; r >= 0; --r)
            for (int c = 0; c < COLS; ++c)
                if (breakable(_cells[r][c])) return cellY(r) + CELL_H;
        return -1.0f;
    }

    // The wall's step: the formation one row lower.
    void stepDown() { _top += CELL_H; }

    // A breakable brick at the danger line costs a life: the rows that
    // crossed it go, and the formation is pushed back up. Returns how many
    // bricks went (their centres in xs/ys, up to `max`, for the explosions).
    // Steel and portals there just shatter.
    int clearCrossed(float *xs, float *ys, uint16_t *cols, int max) {
        int n = 0;
        for (int r = 0; r < ROWS; ++r) {
            if (cellY(r) + CELL_H <= DANGER_Y) continue;
            for (int c = 0; c < COLS; ++c) {
                if (_cells[r][c].kind == EMPTY) continue;
                if (n < max) {
                    xs[n] = cellX(c) + CELL_W * 0.5f;
                    ys[n] = cellY(r) + CELL_H * 0.5f;
                    cols[n] = colourOf(r, _cells[r][c]);
                }
                ++n;
                _cells[r][c] = Cell{ EMPTY, 0, 0 };
            }
        }
        return n;
    }
    bool crossedDanger() const {
        float b = lowestBottom();
        return b > DANGER_Y;
    }
    // Steel crossing the line shatters, costing nothing.
    int shatterUnbreakable(float *xs, float *ys, int max) {
        int n = 0;
        for (int r = 0; r < ROWS; ++r) {
            if (cellY(r) + CELL_H <= DANGER_Y) continue;
            for (int c = 0; c < COLS; ++c) {
                if (_cells[r][c].kind != STEEL) continue;
                if (n < max) { xs[n] = cellX(c) + CELL_W * 0.5f; ys[n] = cellY(r) + CELL_H * 0.5f; }
                ++n;
                _cells[r][c] = Cell{ EMPTY, 0, 0 };
            }
        }
        return n;
    }
    void pushBack(int rows) {
        _top -= rows * CELL_H;
        if (_top < GRID_Y0) _top = GRID_Y0;
    }

    // Neutral bricks take their colour from the row, so a wall reads as
    // bands; hard bricks darken as they crack.
    static uint16_t colourOf(int r, const Cell &k) {
        static const uint16_t ROW_COLS[6] = { 0xFFFF, 0xFFE0, 0xFD20, 0x07E0, 0x041F, 0xFBE0 };
        switch (k.kind) {
            case NEUTRAL: return ROW_COLS[r % 6];
            case HARD:    return k.hits >= 3 ? 0xC618 : k.hits == 2 ? 0x9CD3 : 0x6B4D;
            case STEEL:   return COL_STEEL;
            default:      return 0;
        }
    }

    // `rowsShown` is for the level intro (rows drop in one by one); `warn`
    // shakes the formation a pixel as a step comes.
    void draw(GFXcanvas16 &cv, int rowsShown, int ox, int oy) const {
        for (int r = 0; r < ROWS && r < rowsShown; ++r) {
            const int y = (int)cellY(r) + oy;
            if (y + CELL_H < FIELD_T || y >= H) continue;
            for (int c = 0; c < COLS; ++c) {
                const Cell &k = _cells[r][c];
                if (k.kind == EMPTY) continue;
                const int x = (int)cellX(c) + ox;
                const uint16_t col = colourOf(r, k);
                cv.fillRect(x, y, CELL_W - 1, CELL_H - 1, col);
                if (k.kind == HARD) {
                    // A light top edge for the bevel, and cracks once hit.
                    cv.drawFastHLine(x, y, CELL_W - 1, 0xFFFF);
                    if (k.hits < k.maxHits) cv.drawPixel(x + 2 + k.hits, y + 2, 0x0000);
                    if (k.hits + 1 < k.maxHits) cv.drawPixel(x + 5, y + 1, 0x0000);
                } else if (k.kind == STEEL) {
                    cv.drawFastHLine(x, y, CELL_W - 1, COL_STEEL_HI);
                    cv.drawPixel(x + 1, y + 2, 0x4208);
                    cv.drawPixel(x + 5, y + 2, 0x4208);
                }
            }
        }
    }

private:
    Cell _cells[ROWS][COLS];
    float _top = GRID_Y0;
};

}  // namespace brickflux

#endif  // BRICK_BOARD_H
