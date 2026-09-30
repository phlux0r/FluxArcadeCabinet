#ifndef HIGH_SCORES_H
#define HIGH_SCORES_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Preferences.h>
#include <string.h>
#include "ArcadeConfig.h"
#include "InputManager.h"

// =============================================================================
// HIGH SCORES — the cabinet's top-5 table for every game, with names.
//
// Each game's table lives in NVS namespace "hiscores" under its key, as a
// blob of ENTRIES (name, score) pairs, best first. The first time a table
// is read, the game's old single high score (kept in its own namespace
// before tables existed) becomes its first entry, named "---".
//
// A game holds a ScoreBoard: begin() loads its table; at game over,
// offer(score) says whether it made the table, and if so the game shows
// the name entry (update() and draw() each frame) until it's done, which
// saves it. A score the player didn't stay for (they quit mid-game) goes
// in with record(), under the last name entered. drawTable() draws the
// table, for attract screens and the launcher.
//
// Name entry: up/down changes the letter under the cursor, left/right
// moves it, A confirms a letter (the third ends it), B steps back. It
// starts on the last name entered on this cabinet ("AAA" the first time),
// and if nothing's touched for ENTRY_TIMEOUT_MS, that's what's saved.
// =============================================================================

namespace hiscore {

constexpr int ENTRIES = 5;
constexpr unsigned long ENTRY_TIMEOUT_MS = 20000;
constexpr unsigned long REPEAT_DELAY_MS = 380;   // stick held: letters start to run...
constexpr unsigned long REPEAT_MS = 110;         // ...this fast
constexpr const char* NS = "hiscores";
constexpr const char* CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.";

struct Entry { char name[4]; int32_t score; };
struct Table { Entry e[ENTRIES]; };

// Every game with a table: its key, the name on the table, and where its
// high score lived before (for carrying it over).
struct GameInfo { const char* key; const char* title; const char* legacyNs; const char* legacyKey; };
inline const GameInfo GAMES[] = {
    { "asteroids", "ASTEROIDS", "af_data",     "highscore"  },
    { "lander",    "LANDER",    "lander_flux", "high_score" },
    { "maze",      "MAZE",      "maze_flux",   "high_score" },
    { "runner",    "RUNNER",    "pf_data",     "highscore"  },
    { "star",      "STAR FLUX", "sf_data",     "highscore"  },
    { "tank",      "TANK FLUX", "tf_data",     "highscore"  },
    { "tube",      "TUBE FLUX", "tb_data",     "highscore"  },
};
constexpr int GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);

inline const GameInfo* info(const char* key) {
    for (const auto &g : GAMES) if (strcmp(g.key, key) == 0) return &g;
    return nullptr;
}

inline void clear(Table &t) {
    for (auto &e : t.e) { strcpy(e.name, "---"); e.score = 0; }
}

inline void save(const char* key, const Table &t) {
    Preferences p;
    p.begin(NS, false);
    p.putBytes(key, &t, sizeof(t));
    p.end();
}

inline void load(const char* key, Table &t) {
    clear(t);
    Preferences p;
    p.begin(NS, true);
    const bool have = p.getBytesLength(key) == sizeof(t);
    if (have) p.getBytes(key, &t, sizeof(t));
    p.end();
    if (have) {
        for (auto &e : t.e) e.name[3] = '\0';   // whatever's stored, names stay strings
        return;
    }
    // No table yet: carry over the old single high score, if there was one.
    if (const GameInfo* g = info(key)) {
        Preferences old;
        old.begin(g->legacyNs, true);
        int32_t best = old.getInt(g->legacyKey, 0);
        old.end();
        if (best > 0) t.e[0].score = best;
    }
    save(key, t);
}

// Where `score` would go (0 is the top), or -1 if it doesn't make it. It
// has to beat an entry outright: a tie stays below the score it ties.
inline int rankFor(const Table &t, long score) {
    if (score <= 0) return -1;
    for (int i = 0; i < ENTRIES; ++i) if (score > t.e[i].score) return i;
    return -1;
}

inline int insert(Table &t, const char* name, long score) {
    int r = rankFor(t, score);
    if (r < 0) return -1;
    for (int i = ENTRIES - 1; i > r; --i) t.e[i] = t.e[i - 1];
    strncpy(t.e[r].name, name, 3);
    t.e[r].name[3] = '\0';
    t.e[r].score = (int32_t)score;
    return r;
}

inline void loadLastName(char out[4]) {
    Preferences p;
    p.begin(NS, true);
    size_t n = p.getBytesLength("last") == 3 ? p.getBytes("last", out, 3) : 0;
    p.end();
    if (n != 3) memcpy(out, "AAA", 3);
    out[3] = '\0';
    for (int i = 0; i < 3; ++i) if (!strchr(CHARS, out[i]) || out[i] == '\0') out[i] = 'A';
}

inline void saveLastName(const char* name) {
    Preferences p;
    p.begin(NS, false);
    p.putBytes("last", name, 3);
    p.end();
}

// Screen directions from the stick, for a game drawn at `rotation` (see
// IGame::getRotation(): 1 landscape, else portrait), as the games read it.
inline void screenDirs(const InputState &in, uint8_t rotation, bool &up, bool &down, bool &left, bool &right) {
    if (rotation == 1) { up = in.joyLeft; down = in.joyRight; left = in.joyUp; right = in.joyDown; }
    else               { up = in.joyDown; down = in.joyUp;    left = in.joyLeft; right = in.joyRight; }
}

inline void printCentred(GFXcanvas16 &c, const char* s, int y, uint16_t colour, uint8_t size = 1) {
    c.setTextSize(size);
    c.setTextColor(colour);
    int16_t bx, by; uint16_t bw, bh;
    c.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
    c.setCursor((c.width() - (int)bw) / 2, y);
    c.print(s);
    c.setTextSize(1);
}

// The table, `title` above it, from row y: rank, name and score per line,
// one highlighted (a just-entered score), centred on the canvas.
inline void drawTable(GFXcanvas16 &c, const Table &t, const char* title, int y, int highlight = -1) {
    printCentred(c, title, y, ArcadeConfig::COLOR_CYAN);
    for (int i = 0; i < ENTRIES; ++i) {
        char line[32];
        snprintf(line, sizeof(line), "%d %s %7ld", i + 1, t.e[i].name, (long)t.e[i].score);
        uint16_t col = i == highlight ? ((millis() / 250) & 1 ? ArcadeConfig::COLOR_WHITE : ArcadeConfig::COLOR_YELLOW)
                     : i == 0 ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_WHITE;
        printCentred(c, line, y + 14 + i * 11, col);
    }
}

class ScoreBoard {
public:
    void begin(const char* key) {
        _key = key;
        load(key, _table);
        _entering = false;
        _rank = -1;
    }

    long best() const { return _table.e[0].score; }
    const char* bestName() const { return _table.e[0].name; }
    const Table& table() const { return _table; }
    const char* key() const { return _key; }
    bool entering() const { return _entering; }
    int lastRank() const { return _rank; }   // where the last score went, -1 if nowhere
    void forget() { _rank = -1; }            // a new game: nothing placed yet

    // "HI ABC 12345", for a title screen or HUD.
    const char* bestLine(char* buf, size_t n, const char* prefix = "HI ") const {
        snprintf(buf, n, "%s%s %ld", prefix, _table.e[0].name, (long)_table.e[0].score);
        return buf;
    }

    // A finished game's score: true if it makes the table, and the name
    // entry has begun.
    bool offer(long score) {
        _rank = rankFor(_table, score);
        if (_rank < 0) return false;
        _entering = true;
        _score = score;
        loadLastName(_name);
        _slot = 0;
        _startAt = _lastInputAt = millis();
        _heldDir = 0;
        _prevA = _prevB = true;      // the buttons that ended the game don't count
        return true;
    }

    // A score the player didn't stay to name (quit mid-game): in under the
    // last name used.
    void record(long score) {
        if (rankFor(_table, score) < 0) return;
        char name[4];
        loadLastName(name);
        _rank = insert(_table, name, score);
        save(_key, _table);
    }

    // One frame of name entry; true once it's finished (and saved).
    bool update(const InputState &in, uint8_t rotation) {
        if (!_entering) return true;
        const unsigned long now = millis();
        bool up, down, left, right;
        screenDirs(in, rotation, up, down, left, right);
        const int dir = up ? 1 : down ? 2 : left ? 3 : right ? 4 : 0;
        bool step = false;
        if (dir != _heldDir) {
            _heldDir = dir;
            _heldAt = now;
            _repeatAt = now + REPEAT_DELAY_MS;
            step = dir != 0;
        } else if (dir != 0 && (long)(now - _repeatAt) >= 0) {
            _repeatAt = now + REPEAT_MS;
            step = dir == 1 || dir == 2;   // only letters run; the cursor steps once per push
        }
        if (step) {
            _lastInputAt = now;
            const int n = (int)strlen(CHARS);
            const char* at = strchr(CHARS, _name[_slot]);
            int i = at ? (int)(at - CHARS) : 0;
            if (dir == 1) _name[_slot] = CHARS[(i + 1) % n];
            if (dir == 2) _name[_slot] = CHARS[(i + n - 1) % n];
            if (dir == 3 && _slot > 0) --_slot;
            if (dir == 4 && _slot < 2) ++_slot;
        }
        const bool aPress = in.btnA && !_prevA, bPress = in.btnB && !_prevB;
        _prevA = in.btnA;
        _prevB = in.btnB;
        if (aPress) {
            _lastInputAt = now;
            if (_slot < 2) ++_slot;
            else return finish();
        }
        if (bPress) {
            _lastInputAt = now;
            if (_slot > 0) --_slot;
        }
        if (now - _lastInputAt >= ENTRY_TIMEOUT_MS) return finish();
        return false;
    }

    // The entry screen, over whatever's behind it: a panel centred on the
    // canvas (160x128 or 128x160).
    void draw(GFXcanvas16 &c) const {
        const int w = c.width(), h = c.height();
        const int pw = w - 16, ph = 112, px = 8, py = (h - ph) / 2;
        c.fillRect(px, py, pw, ph, 0x0843);
        c.drawRect(px, py, pw, ph, ArcadeConfig::COLOR_CYAN);
        const unsigned long now = millis();
        printCentred(c, "NEW HIGH SCORE!", py + 6, (now / 300) & 1 ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_WHITE);
        char buf[24];
        snprintf(buf, sizeof(buf), "#%d  %ld", _rank + 1, _score);
        printCentred(c, buf, py + 18, ArcadeConfig::COLOR_WHITE);
        printCentred(c, "ENTER YOUR NAME", py + 30, ArcadeConfig::COLOR_GREEN);

        // Three big letters; the one being set flashes, with arrows over and under it.
        const int cw = 18, gap = 8, total = 3 * cw + 2 * gap, x0 = (w - total) / 2, ly = py + 50;
        for (int i = 0; i < 3; ++i) {
            const int x = x0 + i * (cw + gap);
            const bool sel = i == _slot;
            c.setTextSize(3);
            c.setTextColor(sel && ((now / 200) & 1) ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_WHITE);
            c.setCursor(x, ly);
            c.print(_name[i]);
            if (sel) {
                const int cx = x + 8;
                c.fillTriangle(cx - 4, ly - 4, cx + 4, ly - 4, cx, ly - 9, ArcadeConfig::COLOR_AMBER);
                c.fillTriangle(cx - 4, ly + 26, cx + 4, ly + 26, cx, ly + 31, ArcadeConfig::COLOR_AMBER);
            }
            c.drawFastHLine(x, ly + 23, 16, sel ? ArcadeConfig::COLOR_AMBER : ArcadeConfig::COLOR_GREY);
        }
        c.setTextSize(1);
        printCentred(c, "STICK:LETTER A:OK", py + ph - 20, ArcadeConfig::COLOR_GREY);
        // Time left, as a draining bar.
        unsigned long idle = now - _lastInputAt;
        if (idle > ENTRY_TIMEOUT_MS) idle = ENTRY_TIMEOUT_MS;
        const int bw = pw - 20;
        c.fillRect(px + 10, py + ph - 8, bw * (int)(ENTRY_TIMEOUT_MS - idle) / (int)ENTRY_TIMEOUT_MS, 3, ArcadeConfig::COLOR_AMBER);
    }

    const char* name() const { return _name; }

private:
    const char* _key = "";
    Table _table{};
    bool  _entering = false;
    int   _rank = -1;
    long  _score = 0;
    char  _name[4] = "AAA";
    int   _slot = 0;
    unsigned long _startAt = 0, _lastInputAt = 0, _heldAt = 0, _repeatAt = 0;
    int   _heldDir = 0;
    bool  _prevA = true, _prevB = true;

    bool finish() {
        _entering = false;
        _rank = insert(_table, _name, _score);
        save(_key, _table);
        saveLastName(_name);
        return true;
    }
};

}  // namespace hiscore

#endif  // HIGH_SCORES_H
