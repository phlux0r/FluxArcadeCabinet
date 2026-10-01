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
//
// Portals aren't solid: the ball goes into them (see portalAt()). Every
// other kind turns the ball, broken or not.
// =============================================================================

namespace brickflux {

class BrickBoard {
public:
    enum Kind : uint8_t { EMPTY, NEUTRAL, HARD, STEEL, GUN, MAGNET, PORTAL, SPARK };
    // `pol` is a brick's colour (NEUTRAL and HARD), or a gun's eye; `pair`
    // which portal pair a portal is in; `timer` a gun's ms to its next shot.
    struct Cell { uint8_t kind, hits, maxHits, pol, pair; int16_t timer; };

    // What hit() did, for the scoring and the effects.
    struct HitResult {
        bool hit = false, broken = false, mismatch = false, coloured = false;
        uint8_t kind = EMPTY;
        int points = 0;
        uint16_t colour = 0;
    };

    void load(int layout) {
        layout = ((layout % LAYOUT_COUNT) + LAYOUT_COUNT) % LAYOUT_COUNT;
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c) {
                Cell &k = _cells[r][c];
                k = Cell{ EMPTY, 0, 0, POL_NONE, 0, 0 };
                switch ((char)pgm_read_byte(&LAYOUTS[layout][r][c])) {
                    case 'n': k = Cell{ NEUTRAL, 1, 1, POL_NONE, 0, 0 }; break;
                    case 'h': k = Cell{ HARD, 2, 2, POL_NONE, 0, 0 }; break;
                    case 'H': k = Cell{ HARD, 3, 3, POL_NONE, 0, 0 }; break;
                    case 'c': k = Cell{ NEUTRAL, 1, 1, POL_CYAN, 0, 0 }; break;
                    case 'm': k = Cell{ NEUTRAL, 1, 1, POL_MAGENTA, 0, 0 }; break;
                    case 'C': k = Cell{ HARD, 2, 2, POL_CYAN, 0, 0 }; break;
                    case 'M': k = Cell{ HARD, 2, 2, POL_MAGENTA, 0, 0 }; break;
                    case 's': k = Cell{ STEEL, 1, 1, POL_NONE, 0, 0 }; break;
                    case 'g': k = Cell{ GUN, 2, 2, POL_CYAN, 0, (int16_t)random(1500, 4000) }; break;
                    case 'G': k = Cell{ GUN, 2, 2, POL_MAGENTA, 0, (int16_t)random(1500, 4000) }; break;
                    case 'o': k = Cell{ MAGNET, 2, 2, POL_NONE, 0, 0 }; break;
                    case 'p': k = Cell{ PORTAL, 1, 1, POL_NONE, 1, 0 }; break;
                    case 'q': k = Cell{ PORTAL, 1, 1, POL_NONE, 2, 0 }; break;
                    case '*': k = Cell{ SPARK, 1, 1, POL_NONE, 0, 0 }; break;
                    default: break;
                }
            }
        _top = GRID_Y0;
    }

    float top() const { return _top; }
    const Cell& cell(int r, int c) const { return _cells[r][c]; }
    Cell& at(int r, int c) { return _cells[r][c]; }

    static float cellX(int c) { return (float)(GRID_X + c * CELL_W); }
    float cellY(int r) const { return _top + r * CELL_H; }

    bool occupied(int r, int c) const {
        return r >= 0 && r < ROWS && c >= 0 && c < COLS && _cells[r][c].kind != EMPTY;
    }
    // What turns a ball: anything but empty space and portals.
    bool solid(int r, int c) const { return occupied(r, c) && _cells[r][c].kind != PORTAL; }
    static bool breakable(const Cell &k) {
        return k.kind == NEUTRAL || k.kind == HARD || k.kind == GUN || k.kind == MAGNET || k.kind == SPARK;
    }
    // Bricks only a ball of their colour breaks.
    static bool coloured(const Cell &k) { return (k.kind == NEUTRAL || k.kind == HARD) && k.pol != POL_NONE; }

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

    // The portal the point (x, y) is inside, if any.
    bool portalAt(float x, float y, int &row, int &col) const {
        const int c = (int)floorf((x - GRID_X) / CELL_W), r = (int)floorf((y - _top) / CELL_H);
        if (!occupied(r, c) || _cells[r][c].kind != PORTAL) return false;
        row = r; col = c;
        return true;
    }
    // A portal's partner: the other cell of its pair.
    bool partnerOf(int r, int c, int &pr, int &pc) const {
        const uint8_t pair = _cells[r][c].pair;
        for (int rr = 0; rr < ROWS; ++rr)
            for (int cc = 0; cc < COLS; ++cc)
                if ((rr != r || cc != c) && _cells[rr][cc].kind == PORTAL && _cells[rr][cc].pair == pair) {
                    pr = rr; pc = cc;
                    return true;
                }
        return false;
    }

    // One hit on a cell by a ball of colour `ballPol` (POL_ANY for a laser).
    // `smash` breaks anything outright, steel and portals included. A
    // coloured brick hit by the other colour just turns the ball.
    HitResult hit(int r, int c, bool smash, uint8_t ballPol) {
        HitResult res;
        if (!occupied(r, c)) return res;
        Cell &k = _cells[r][c];
        res.kind = k.kind;
        res.colour = colourOf(r, k);
        if (k.kind == PORTAL) {
            if (!smash) return res;
            res.hit = res.broken = true;
            res.points = PTS_PORTAL;
            removePortal(r, c);
            return res;
        }
        res.hit = true;
        if (k.kind == STEEL && !smash) return res;
        if (coloured(k) && !smash && ballPol != POL_ANY && ballPol != k.pol) {
            res.mismatch = true;
            return res;
        }
        res.coloured = coloured(k);
        const bool breaks = smash || k.hits <= 1;
        switch (k.kind) {
            case HARD:   res.points = PTS_HARD * (smash ? k.hits : 1); break;
            case STEEL:  res.points = PTS_STEEL; break;
            case GUN:    res.points = breaks ? PTS_GUN : PTS_CRACK; break;
            case MAGNET: res.points = breaks ? PTS_MAGNET : PTS_CRACK; break;
            case SPARK:  res.points = PTS_SPARK; break;
            default:     res.points = res.coloured ? PTS_COLOURED : PTS_NEUTRAL; break;
        }
        if (breaks) {
            res.broken = true;
            k = Cell{ EMPTY, 0, 0, POL_NONE, 0, 0 };
        } else {
            --k.hits;
        }
        return res;
    }

    // Bricks the level needs gone (steel and portals don't count).
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

    // Is anything solid below this cell in its column? A gun only fires
    // with a clear line down.
    bool clearBelow(int r, int c) const {
        for (int rr = r + 1; rr < ROWS; ++rr) if (solid(rr, c)) return false;
        return true;
    }

    // The wall's step: the formation one row lower.
    void stepDown() { _top += CELL_H; }

    // A breakable brick at the danger line costs a life: everything that
    // crossed it goes (a portal's partner with it), and the formation is
    // pushed back up. Returns how many went (their centres in xs/ys, up to
    // `max`, for the explosions).
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
                if (_cells[r][c].kind == PORTAL) removePortal(r, c);
                else _cells[r][c] = Cell{ EMPTY, 0, 0, POL_NONE, 0, 0 };
            }
        }
        return n;
    }
    bool crossedDanger() const {
        float b = lowestBottom();
        return b > DANGER_Y;
    }
    // Steel and portals crossing the line shatter, costing nothing.
    int shatterUnbreakable(float *xs, float *ys, int max) {
        int n = 0;
        for (int r = 0; r < ROWS; ++r) {
            if (cellY(r) + CELL_H <= DANGER_Y) continue;
            for (int c = 0; c < COLS; ++c) {
                const uint8_t kind = _cells[r][c].kind;
                if (kind != STEEL && kind != PORTAL) continue;
                if (n < max) { xs[n] = cellX(c) + CELL_W * 0.5f; ys[n] = cellY(r) + CELL_H * 0.5f; }
                ++n;
                if (kind == PORTAL) removePortal(r, c);
                else _cells[r][c] = Cell{ EMPTY, 0, 0, POL_NONE, 0, 0 };
            }
        }
        return n;
    }
    void pushBack(int rows) {
        _top -= rows * CELL_H;
        if (_top < GRID_Y0) _top = GRID_Y0;
    }

    // Neutral bricks take their colour from the row, so a wall reads as
    // bands (never cyan or magenta, which are polarity's); hard bricks
    // darken as they crack.
    static uint16_t colourOf(int r, const Cell &k) {
        static const uint16_t ROW_COLS[6] = { 0xFFFF, 0xFFE0, 0xFD20, 0x07E0, 0x041F, 0xFBE0 };
        switch (k.kind) {
            case NEUTRAL:
                return k.pol == POL_CYAN ? COL_CYAN : k.pol == POL_MAGENTA ? COL_MAGENTA : ROW_COLS[r % 6];
            case HARD:
                if (k.pol == POL_CYAN) return k.hits >= 2 ? COL_CYAN_HARD : COL_CYAN;
                if (k.pol == POL_MAGENTA) return k.hits >= 2 ? COL_MAGENTA_HARD : COL_MAGENTA;
                return k.hits >= 3 ? 0xC618 : k.hits == 2 ? 0x9CD3 : 0x6B4D;
            case STEEL:  return COL_STEEL;
            case GUN:    return 0x8000;
            case MAGNET: return 0x2156;
            case PORTAL: return 0x780F;
            case SPARK:  return 0xFFE0;
            default:     return 0;
        }
    }

    // `rowsShown` is for the level intro (rows drop in one by one); `ox`,
    // `oy` shake it. `now` animates the living bricks.
    void draw(GFXcanvas16 &cv, int rowsShown, int ox, int oy, unsigned long now) const {
        for (int r = 0; r < ROWS && r < rowsShown; ++r) {
            const int y = (int)cellY(r) + oy;
            if (y + CELL_H < FIELD_T || y >= H) continue;
            for (int c = 0; c < COLS; ++c) {
                const Cell &k = _cells[r][c];
                if (k.kind == EMPTY) continue;
                const int x = (int)cellX(c) + ox;
                const uint16_t col = colourOf(r, k);
                const int w = CELL_W - 1, h = CELL_H - 1;
                switch (k.kind) {
                    case PORTAL: {
                        // A ring, its pixels chasing round.
                        cv.drawRect(x, y, w, h, col);
                        const int ph = (int)((now / 90 + r + c) % 4);
                        cv.drawPixel(x + 1 + ph, y, 0xFFFF);
                        cv.drawPixel(x + w - 2 - ph, y + h - 1, 0xFFFF);
                        break;
                    }
                    case GUN:
                        cv.drawRect(x, y, w, h, 0xF800);
                        cv.fillRect(x + 1, y + 1, w - 2, h - 2, col);
                        cv.fillRect(x + 2, y + 1, 3, 2, polColour(k.pol));
                        if (k.hits < k.maxHits) cv.drawPixel(x + 5, y + 2, 0x0000);
                        break;
                    case MAGNET:
                        cv.fillRect(x, y, w, h, col);
                        cv.drawRect(x, y, w, h, (now / 200) % 2 ? 0x041F : 0xFFFF);
                        if (k.hits < k.maxHits) cv.drawPixel(x + 3, y + 2, 0x0000);
                        break;
                    case SPARK:
                        cv.fillRect(x, y, w, h, col);
                        cv.drawPixel(x + (int)((now / 120 + c * 3) % 6), y + 1 + (int)((now / 170 + r) % 2), 0xFFFF);
                        break;
                    default:
                        cv.fillRect(x, y, w, h, col);
                        if (k.kind == HARD) {
                            // A light top edge for the bevel, and cracks once hit.
                            cv.drawFastHLine(x, y, w, 0xFFFF);
                            if (k.hits < k.maxHits) cv.drawPixel(x + 2 + k.hits, y + 2, 0x0000);
                            if (k.hits + 1 < k.maxHits) cv.drawPixel(x + 5, y + 1, 0x0000);
                        } else if (k.kind == STEEL) {
                            cv.drawFastHLine(x, y, w, COL_STEEL_HI);
                            cv.drawPixel(x + 1, y + 2, 0x4208);
                            cv.drawPixel(x + 5, y + 2, 0x4208);
                        } else if (k.pol != POL_NONE) {
                            // A dark centre dot marks a coloured brick.
                            cv.drawPixel(x + 3, y + 1, 0x0000);
                            cv.drawPixel(x + 3, y + 2, 0x0000);
                        }
                        break;
                }
            }
        }
    }

private:
    void removePortal(int r, int c) {
        int pr, pc;
        const bool partner = partnerOf(r, c, pr, pc);
        _cells[r][c] = Cell{ EMPTY, 0, 0, POL_NONE, 0, 0 };
        if (partner) _cells[pr][pc] = Cell{ EMPTY, 0, 0, POL_NONE, 0, 0 };
    }

    Cell _cells[ROWS][COLS];
    float _top = GRID_Y0;
};

}  // namespace brickflux

#endif  // BRICK_BOARD_H
