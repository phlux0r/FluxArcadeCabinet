// Host harness for Maze Flux: the real game against a fake clock and seeded
// RNG, like the other harnesses. Scenarios:
//
//   layout      levels 1-20, a few mazes each: the size for the level, loops
//               (more ways than a tree), every key in its stretch and
//               reachable in order, doors really needed (closed, the exit
//               can't be reached), nothing stacked, outside the maze or by
//               the start, no trap firing through the start, teleport pairs
//               in one stretch and apart, the clock sized to the maze
//   doors       a closed door stops you; its key opens it
//   bullets     bullets stop at walls, whatever the frame time, and move
//               at the same speed at 17ms and 50ms frames
//
// MAZE_TRACE=1 prints the bot's progress and each life lost (MAZE_TRAPS=1
// adds the traps' state at each).
//   death       a bullet, a bomb and the clock each cost one life after the
//               pause; the respawn is at the start and safe for a while;
//               the clock starts again at the level's time
//   teleport    walking onto a pad puts you on its partner, and not back
//   complete    the level-complete screen waits for A to be let go
//   buffer      a push between steps is taken at the next cell
//   passing     walking straight on with the stick held, at uneven frame
//               times: every cell passed is marked, keys and pickups on
//               the way collected
//   wayfinding  cells walked are marked (breadcrumbs); the compass aims at
//               the next key, then the exit, with an arrow on the screen's
//               edge pointing its way while it's off screen
//   play [N]    a bot that walks to each key and the exit, waiting at a
//               trap's line of fire for a gap (or using a type B's switch),
//               lives pinned: over N frames (default 60000) it must reach
//               level 15 losing at most 5 lives; the score never drops
//   quit        Back mid-game puts the score on the table
//   idle        the attract cycle (title, how-to, scores) and A to start
//   all         everything (the default)
//
// DUMP_AT=frame,frame writes those frames of `play` as maze_<frame>.ppm.

#include <Adafruit_ST7735.h>
#include "harness_common.h"
#include <chrono>

#define private public
#include "games/MazeFlux/MazeFluxGame.h"
#undef private

using namespace mazecfg;

static const unsigned long STEP_MS = 33;
static GFXcanvas16 g_canvas(ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
static AudioEngine g_audio;

// Screen directions to the stick, as rotation 2 reads it.
static InputState stick(int dir, bool a = false) {
    InputState in{};
    in.joyDown  = dir == WALL_N;
    in.joyUp    = dir == WALL_S;
    in.joyLeft  = dir == WALL_W;
    in.joyRight = dir == WALL_E;
    in.btnA = a;
    return in;
}

static void frame(MazeFluxGame &g, const InputState &in, unsigned long ms = STEP_MS) {
    g.update(g_canvas, in, g_audio);
    g_fakeMillis += ms;
}

static GameEngineMaze &startAt(MazeFluxGame &g, int level) {
    g.init(g_audio);
    GameEngineMaze &e = g._engine;
    e.startGame(g_audio);
    e._level = level;
    e.initLevel();
    return e;
}

// Cells reachable from (sx, sy) through open ways.
static int reachable(const GameEngineMaze &e, int sx, int sy, bool* seen) {
    const int W = e._maze.width, n = W * e._maze.height;
    for (int i = 0; i < n; i++) seen[i] = false;
    static int q[MazeGenerator::MAX_W * MazeGenerator::MAX_H];
    int head = 0, tail = 0, count = 0;
    q[tail++] = sy * W + sx; seen[sy * W + sx] = true;
    while (head < tail) {
        const int c = q[head++], x = c % W, y = c / W;
        count++;
        for (uint8_t d : MazeGenerator::DIRS) {
            if (!e.canPass(x, y, d)) continue;
            const int nc = (y + MazeGenerator::dy(d)) * W + x + MazeGenerator::dx(d);
            if (!seen[nc]) { seen[nc] = true; q[tail++] = nc; }
        }
    }
    return count;
}

static bool layout() {
    bool ok = true;
    static bool seen[MazeGenerator::MAX_W * MazeGenerator::MAX_H];
    int doorsSeen = 0, trapsSeen = 0, padsSeen = 0, bombsSeen = 0, loops = 0;
    for (int level = 1; level <= 20; level++) {
        for (int rep = 0; rep < 4; rep++) {
            static MazeFluxGame g;
            GameEngineMaze &e = startAt(g, level);
            const int W = e._maze.width;
            const char* why = nullptr;
            doorsSeen += e._activeDoors; trapsSeen += e._activeTraps; padsSeen += e._activePads; bombsSeen += e._activeBombs;

            if (e._activeKeys != e._activeDoors + 1) why = "a key per door plus the exit's";
            if (e._activeDoors != min(doorsFor(level), 3) && level < 12) why = "doors for the level";
            // Keys in order: each reachable once the doors before it are open,
            // and (with doors) the exit not reachable while they're shut.
            reachable(e, 0, 0, seen);
            if (e._activeDoors > 0 && seen[e._exit.y * W + e._exit.x]) why = "exit reachable past a shut door";
            for (int k = 0; k < e._activeKeys && !why; k++) {
                const Key &key = e._keys[k];
                if (e._region[key.y * W + key.x] != k) why = "key outside its stretch";
                reachable(e, 0, 0, seen);
                if (!seen[key.y * W + key.x]) why = "key not reachable in order";
                if (k < e._activeDoors) e._doors[k].open = true;
            }
            reachable(e, 0, 0, seen);
            if (!why && !seen[e._exit.y * W + e._exit.x]) why = "exit not reachable with every door open";
            for (int k = 0; k < e._activeDoors; k++) e._doors[k].open = false;

            // Nothing stacked, outside, or by the start.
            struct P { int x, y; } items[64];
            int n = 0;
            for (int i = 0; i < e._activeKeys; i++)  items[n++] = { e._keys[i].x, e._keys[i].y };
            for (int i = 0; i < e._activeBombs; i++) items[n++] = { e._bombs[i].x, e._bombs[i].y };
            for (int i = 0; i < e._activeTraps; i++) items[n++] = { e._traps[i].x, e._traps[i].y };
            for (int i = 0; i < e._activePads; i++)  items[n++] = { e._pads[i].x, e._pads[i].y };
            for (auto &b : e._boosts)  if (b.active) items[n++] = { b.x, b.y };
            for (auto &b : e._bonuses) if (b.active) items[n++] = { b.x, b.y };
            for (int i = 0; i < n && !why; i++) {
                if (!e._maze.inside(items[i].x, items[i].y)) why = "outside the maze";
                else if (e._dist[items[i].y * W + items[i].x] < START_CLEAR) why = "by the start";
                else if (items[i].x == e._exit.x && items[i].y == e._exit.y) why = "on the exit";
                for (int j = i + 1; j < n; j++) if (items[i].x == items[j].x && items[i].y == items[j].y) why = "two on one cell";
            }
            for (int i = 0; i < e._activeBombs && !why; i++)
                if (e._dist[e._bombs[i].y * W + e._bombs[i].x] < START_CLEAR + BOMB_RANGE) why = "bomb reaches the start";
            for (int i = 0; i < e._activeTraps && !why; i++) {
                const TrapEmitter &t = e._traps[i];
                const uint8_t d = t.dirX > 0 ? WALL_E : t.dirX < 0 ? WALL_W : t.dirY > 0 ? WALL_S : WALL_N;
                if (e.laneFrom(t.x, t.y, d) < TRAP_MIN_LANE) why = "trap lane short or through the start";
            }
            for (int i = 0; i < e._activePads && !why; i++) {
                const Teleport &a = e._pads[i], &b = e._pads[a.partner];
                if (b.partner != i) why = "pads not paired";
                else if (e._region[a.y * W + a.x] != e._region[b.y * W + b.x]) why = "pads skip a door";
                else if (abs(a.x - b.x) + abs(a.y - b.y) < PAD_MIN_APART) why = "pads too close";
                else if (e.exits(a.x, a.y) != 1) why = "pad on a way through";
            }
            if (!why && e._levelTime != (int)(TIME_BASE_S + TIME_PER_CELL_S * W * e._maze.height)) why = "clock not sized to the maze";
            if (!why && (W != min(mazeWidthFor(level), 24) || e._maze.height != min(mazeHeightFor(level), 30))) why = "maze size";
            // Loops: more open ways than a tree's cells - 1.
            int ways = 0;
            for (int y = 0; y < e._maze.height; y++)
                for (int x = 0; x < W; x++) ways += !e._maze.hasWall(x, y, WALL_E) + !e._maze.hasWall(x, y, WALL_S);
            loops += ways - (W * e._maze.height - 1);
            if (!why && ways < W * e._maze.height - 1) why = "maze not connected";
            if (why) { printf("  level %d maze %d: %s\n", level, rep, why); ok = false; }
        }
    }
    ok &= loops > 80 * 5;
    printf("layout: 80 mazes, %d doors, %d bombs, %d traps, %d pads, %d loops -> %s\n",
           doorsSeen, bombsSeen, trapsSeen, padsSeen, loops, ok ? "PASS" : "FAIL");
    return ok;
}

// The first open way out of (x, y), and the door index (or -1) across it.
static bool doors() {
    static MazeFluxGame g;
    GameEngineMaze &e = startAt(g, 5);
    if (e._activeDoors == 0) { printf("doors: no door on level 5 -> FAIL\n"); return false; }
    Door &d = e._doors[0];
    e._safeUntil = millis() + 100000;           // nothing else gets in the way
    e._player.reset(d.x, d.y);
    for (int f = 0; f < 20; f++) frame(g, stick(d.dir));
    const bool stopped = e._player.x == d.x && e._player.y == d.y;
    // Its key collected (as if walked onto), then through.
    e.arriveAt(e._keys[0].x, e._keys[0].y, g_audio);
    e._player.reset(d.x, d.y);
    for (int f = 0; f < 4; f++) frame(g, stick(d.dir));
    const bool through = e._player.x == d.x + MazeGenerator::dx(d.dir) && e._player.y == d.y + MazeGenerator::dy(d.dir);
    const bool ok = stopped && d.open && through;
    printf("doors: shut door stops you %d, key opens it %d, through %d -> %s\n", stopped, d.open, through, ok ? "PASS" : "FAIL");
    return ok;
}

static bool bullets() {
    bool ok = true;
    int fired = 0, crossings = 0;
    for (int level : { 9, 11, 13, 15 }) {
        static MazeFluxGame g;
        GameEngineMaze &e = startAt(g, level);
        e._safeUntil = millis() + 10000000;
        for (int f = 0; f < 3000; f++) {
            int prevX[MAX_TRAPS][MAX_BULLETS], prevY[MAX_TRAPS][MAX_BULLETS];
            bool was[MAX_TRAPS][MAX_BULLETS];
            for (int i = 0; i < e._activeTraps; i++)
                for (int b = 0; b < MAX_BULLETS; b++) {
                    was[i][b] = e._traps[i].bullets[b].active;
                    prevX[i][b] = e._traps[i].bullets[b].cellX; prevY[i][b] = e._traps[i].bullets[b].cellY;
                }
            frame(g, InputState{}, 17 + (f * 7) % 40);
            for (int i = 0; i < e._activeTraps; i++)
                for (int b = 0; b < MAX_BULLETS; b++) {
                    const auto &bl = e._traps[i].bullets[b];
                    if (!bl.active) continue;
                    if (!was[i][b]) { fired++; continue; }
                    // Every cell it entered, through an open way.
                    int x = prevX[i][b], y = prevY[i][b];
                    while (x != bl.cellX || y != bl.cellY) {
                        const uint8_t d = e._traps[i].dirX > 0 ? WALL_E : e._traps[i].dirX < 0 ? WALL_W : e._traps[i].dirY > 0 ? WALL_S : WALL_N;
                        if (!e.canPass(x, y, d)) { printf("  level %d: a bullet went through a wall\n", level); ok = false; break; }
                        x += e._traps[i].dirX; y += e._traps[i].dirY;
                        crossings++;
                    }
                    if ((int)floorf(bl.x / CELL) != bl.cellX || (int)floorf(bl.y / CELL) != bl.cellY) {
                        printf("  level %d: a bullet's cell and position disagree\n", level); ok = false;
                    }
                }
        }
    }
    // Speed: one bullet 300ms on, at 17ms and at 50ms frames.
    float dist[2];
    for (int k = 0; k < 2; k++) {
        TrapEmitter t;
        t.init(0, 0, 1, 0, TrapEmitter::TYPE_A, 20);
        t.bullets[0] = { CELL * 0.5f, CELL * 0.5f, 0, 0, true };
        t._lastFireMs = millis();
        const unsigned long step = k ? 50 : 17;
        for (unsigned long ms = 0; ms + step <= 300; ms += step) t.update(step, [](int, int, uint8_t) { return true; });
        dist[k] = t.bullets[0].x - CELL * 0.5f;
        // Remaining time, so both cover 300ms exactly.
        const unsigned long done = (300 / step) * step;
        if (done < 300) { t.update(300 - done, [](int, int, uint8_t) { return true; }); dist[k] = t.bullets[0].x - CELL * 0.5f; }
    }
    const float want = BULLET_CELLS_PER_S * CELL * 0.3f;
    const bool speed = fabsf(dist[0] - want) < 0.5f && fabsf(dist[1] - want) < 0.5f;
    ok &= fired > 20 && crossings > 50 && speed;
    printf("bullets: %d fired, %d cell crossings all through open ways; 300ms covers %.1f/%.1fpx (want %.1f) -> %s\n",
           fired, crossings, dist[0], dist[1], want, ok ? "PASS" : "FAIL");
    return ok;
}

static bool death() {
    bool ok = true;
    static MazeFluxGame g;
    // A bullet.
    GameEngineMaze &e = startAt(g, 9);
    for (int f = 0; f < 60; f++) frame(g, InputState{});          // past nothing: no safe time at a level start
    int lives = e._player.lives;
    e._player.reset(4, 4);
    TrapEmitter &t = e._traps[0];
    t.bullets[0] = { e._player.px(), e._player.py(), 4, 4, true };
    frame(g, InputState{});
    const bool dying = e._dyingUntil != 0 && e._player.lives == lives;
    for (int f = 0; f < (int)(DEATH_MS / STEP_MS) + 2; f++) frame(g, InputState{});
    const bool respawned = !e._dyingUntil && e._player.lives == lives - 1 && e._player.x == 0 && e._player.y == 0 && e.safe();
    // Safe for a while: a bullet on you does nothing.
    t.bullets[0] = { e._player.px(), e._player.py(), 0, 0, true };
    frame(g, InputState{});
    const bool spared = !e._dyingUntil;
    for (int f = 0; f < (int)(RESPAWN_SAFE_MS / STEP_MS) + 2; f++) frame(g, InputState{});
    t.bullets[0] = { e._player.px(), e._player.py(), 0, 0, true };
    t._pauseEndMs = millis() + 100000;           // no fresh bullets in the way
    frame(g, InputState{});
    const bool hitAgain = e._dyingUntil != 0;
    printf("death bullet: pause %d, then a life gone and back at the start %d, safe %d, then hit %d -> %s\n",
           dying, respawned, spared, hitAgain, dying && respawned && spared && hitAgain ? "PASS" : "FAIL");
    ok &= dying && respawned && spared && hitAgain;
    for (int f = 0; f < (int)(DEATH_MS / STEP_MS) + 2; f++) frame(g, InputState{});

    // The clock.
    e._player.lives = lives = 3;
    e._timeLeft = 1;
    e._safeUntil = 0;
    for (int f = 0; f < 40 && !e._dyingUntil; f++) frame(g, InputState{});
    for (int f = 0; f < (int)(DEATH_MS / STEP_MS) + 2; f++) frame(g, InputState{});
    const bool timeOk = e._player.lives == lives - 1 && e._timeLeft == e._levelTime;
    printf("death clock: a life gone %d, clock back to %ds (%d) -> %s\n", e._player.lives == lives - 1, e._levelTime, e._timeLeft,
           timeOk ? "PASS" : "FAIL");
    ok &= timeOk;

    // A bomb: stand next to one until it goes.
    GameEngineMaze &b = startAt(g, 7);
    if (b._activeBombs == 0) { printf("death bomb: none on level 7 -> FAIL\n"); return false; }
    ProximityBomb &bomb = b._bombs[0];
    int nx = bomb.x, ny = bomb.y;
    for (uint8_t d : MazeGenerator::DIRS) if (b.canPass(bomb.x, bomb.y, d)) { nx += MazeGenerator::dx(d); ny += MazeGenerator::dy(d); break; }
    for (int f = 0; f < 80; f++) frame(g, InputState{});
    for (int i = 0; i < b._activeTraps; i++) b._traps[i].active = false;
    lives = b._player.lives;
    b._player.reset(nx, ny);
    int f = 0;
    for (; f < 200 && !b._dyingUntil; f++) frame(g, InputState{});
    const unsigned long fuse = f * STEP_MS;
    for (int k = 0; k < (int)(DEATH_MS / STEP_MS) + 2; k++) frame(g, InputState{});
    const bool bombOk = !bomb.active && b._player.lives == lives - 1 && b._deathCause == GameEngineMaze::DEATH_BOMB &&
                        fuse >= FUSE_TICKS * FUSE_TICK_MS && fuse < (FUSE_TICKS + 1) * FUSE_TICK_MS + 200;
    printf("death bomb: went off after %lums, a life gone %d -> %s\n", fuse, b._player.lives == lives - 1, bombOk ? "PASS" : "FAIL");
    ok &= bombOk;
    return ok;
}

static bool teleport() {
    static MazeFluxGame g;
    GameEngineMaze &e = startAt(g, 6);
    if (e._activePads < 2) { printf("teleport: no pads on level 6 -> FAIL\n"); return false; }
    e._safeUntil = millis() + 100000;
    const Teleport &a = e._pads[0], &b = e._pads[a.partner];
    // Step onto pad a from a neighbour.
    uint8_t in = 0;
    int sx = a.x, sy = a.y;
    for (uint8_t d : MazeGenerator::DIRS) if (e.canPass(a.x, a.y, d)) { sx = a.x + MazeGenerator::dx(d); sy = a.y + MazeGenerator::dy(d); in = MazeGenerator::opposite(d); break; }
    e._player.reset(sx, sy);
    for (int f = 0; f < 10 && !(e._player.x == b.x && e._player.y == b.y); f++) frame(g, stick(in));
    const bool there = e._player.x == b.x && e._player.y == b.y;
    for (int f = 0; f < 10; f++) frame(g, InputState{});
    const bool stays = e._player.x == b.x && e._player.y == b.y;
    printf("teleport: onto a pad puts you on its partner %d, and you stay %d -> %s\n", there, stays, there && stays ? "PASS" : "FAIL");
    return there && stays;
}

static bool complete() {
    static MazeFluxGame g;
    GameEngineMaze &e = startAt(g, 2);
    const int score0 = e._score, t = e._timeLeft;
    for (int k = 0; k < e._activeKeys; k++) e.arriveAt(e._keys[k].x, e._keys[k].y, g_audio);
    e.arriveAt(e._exit.x, e._exit.y, g_audio);
    const bool done = e._state == GameEngineMaze::STATE_LEVEL_COMPLETE && e._level == 3 &&
                      e._score == score0 + PTS_KEY * e._activeKeys + t * PTS_PER_SECOND + 2 * PTS_LEVEL;
    for (int f = 0; f < 100; f++) frame(g, stick(0, true));              // A held throughout
    const bool held = e._state == GameEngineMaze::STATE_LEVEL_COMPLETE;
    frame(g, InputState{});
    frame(g, stick(0, true));
    const bool next = e._state == GameEngineMaze::STATE_PLAYING && e._level == 3;
    printf("complete: scored %d, held A waits %d, a fresh press goes on %d -> %s\n", done, held, next,
           done && held && next ? "PASS" : "FAIL");
    return done && held && next;
}

static bool buffer() {
    static MazeFluxGame g;
    GameEngineMaze &e = startAt(g, 1);
    e._safeUntil = millis() + 100000;
    // Find a cell with a straight way on and a side turning from the next cell.
    for (int y = 0; y < e._maze.height; y++)
        for (int x = 0; x < e._maze.width; x++)
            for (uint8_t d : MazeGenerator::DIRS) {
                if (!e.canPass(x, y, d)) continue;
                const int nx = x + MazeGenerator::dx(d), ny = y + MazeGenerator::dy(d);
                for (uint8_t side : MazeGenerator::DIRS) {
                    if (side == d || side == MazeGenerator::opposite(d) || !e.canPass(nx, ny, side)) continue;
                    if (e.canPass(x, y, side)) continue;            // the side way only from the next cell
                    e._player.reset(x, y);
                    frame(g, stick(d));                            // start the step
                    frame(g, stick(side));                         // a short push mid-step
                    for (int f = 0; f < 3; f++) frame(g, InputState{});
                    for (int f = 0; f < 12; f++) frame(g, InputState{});
                    const int wx = nx + MazeGenerator::dx(side), wy = ny + MazeGenerator::dy(side);
                    const bool ok = e._player.x == wx && e._player.y == wy;
                    printf("buffer: a push mid-step taken at the next cell %d -> %s\n", ok, ok ? "PASS" : "FAIL");
                    return ok;
                }
            }
    printf("buffer: no turning found -> FAIL\n");
    return false;
}

// Walking straight on with the stick held: every cell passed is marked and
// whatever's on it collected (a key mid-corridor, a pickup).
static bool passing() {
    static MazeFluxGame g;
    int runs = 0, missedCrumbs = 0, missedKeys = 0, missedBoosts = 0, early = 0;
    for (int level = 1; level <= 12; level++) {
        GameEngineMaze &e = startAt(g, level);
        e._safeUntil = millis() + 10000000;
        for (int i = 0; i < e._activeTraps; i++) e._traps[i].active = false;
        for (int i = 0; i < e._activeBombs; i++) e._bombs[i].active = false;
        e._activePads = 0;
        // A straight run of at least four open cells.
        for (int y = 0; y < e._maze.height && runs < level * 3; y++)
            for (int x = 0; x < e._maze.width && runs < level * 3; x++)
                for (uint8_t d : MazeGenerator::DIRS) {
                    int n = 0, cx = x, cy = y;
                    while (n < 6 && e.canPass(cx, cy, d)) { cx += MazeGenerator::dx(d); cy += MazeGenerator::dy(d); n++; }
                    if (n < 4) continue;
                    // A key in the second cell, a boost in the third.
                    const int kx = x + MazeGenerator::dx(d) * 2, ky = y + MazeGenerator::dy(d) * 2;
                    const int bx = x + MazeGenerator::dx(d) * 3, by = y + MazeGenerator::dy(d) * 3;
                    e._keys[0] = { kx, ky, 0, false };
                    e._boosts[0] = { bx, by, true };
                    memset(e._visited, 0, sizeof(e._visited));
                    e._player.reset(x, y);
                    for (int f = 0; f < 40 && !(e._player.x == cx && e._player.y == cy && !e._player.moving); f++) {
                        const bool had = e._keys[0].collected;
                        frame(g, stick(d), 17 + (f * 13) % 30);
                        // Collected only on reaching it, not a step before.
                        if (!had && e._keys[0].collected &&
                            fabsf(e._player.px() - (kx + 0.5f) * CELL) + fabsf(e._player.py() - (ky + 0.5f) * CELL) > CELL / 2)
                            early++;
                    }
                    for (int k = 1; k <= n; k++)
                        missedCrumbs += !e._visited[(y + MazeGenerator::dy(d) * k) * e._maze.width + x + MazeGenerator::dx(d) * k];
                    missedKeys += !e._keys[0].collected;
                    missedBoosts += e._boosts[0].active;
                    runs++;
                    break;
                }
    }
    const bool ok = runs > 20 && !missedCrumbs && !missedKeys && !missedBoosts && !early;
    printf("passing: %d straight runs with the stick held, %d cells unmarked, %d keys and %d boosts left behind, %d keys taken a step early -> %s\n",
           runs, missedCrumbs, missedKeys, missedBoosts, early, ok ? "PASS" : "FAIL");
    return ok;
}

// Breadcrumbs and the compass.
static bool wayfinding() {
    static MazeFluxGame g;
    GameEngineMaze &e = startAt(g, 8);
    e._safeUntil = millis() + 10000000;
    for (int i = 0; i < e._activeTraps; i++) e._traps[i].active = false;
    for (int i = 0; i < e._activeBombs; i++) e._bombs[i].active = false;
    // Walk a few cells: each marked, nothing else.
    int walked = 1;
    for (int f = 0; f < 60; f++) {
        uint8_t d = 0;
        for (uint8_t k : MazeGenerator::DIRS) if (e.canPass(e._player.x, e._player.y, k)) { d = k; break; }
        frame(g, stick(d));
        if (e._player.arrived) walked++;
    }
    int marked = 0;
    for (int i = 0; i < e._maze.width * e._maze.height; i++) marked += e._visited[i] != 0;
    const bool here = e._visited[e._player.y * e._maze.width + e._player.x] != 0;
    const bool crumbs = here && marked >= 2 && marked <= walked;

    // The compass: towards the lowest key not collected; off screen an
    // arrow on the edge, pointing its way; none once it's in view.
    int tx, ty; uint16_t col;
    e.compassTarget(tx, ty, col);
    bool aims = tx == e._keys[0].x && ty == e._keys[0].y && col == GameEngineMaze::keyColour(0);
    e._keys[0].collected = true;
    e.compassTarget(tx, ty, col);
    aims &= e._activeKeys < 2 || (tx == e._keys[1].x && ty == e._keys[1].y);
    e._keys[0].collected = false;
    int checked = 0, wrong = 0;
    for (int y = 0; y < e._maze.height; y += 3)
        for (int x = 0; x < e._maze.width; x += 3) {
            e._player.reset(x, y);
            e.updateCamera(0, true);
            int ax, ay; float dx, dy;
            const bool arrow = e.compassArrow(ax, ay, dx, dy, col);
            const int qx = e.cx(e._keys[0].x), qy = e.cy(e._keys[0].y);
            const bool inView = qx >= 0 && qx < 128 && qy >= HUD_H && qy < 160;
            if (inView == arrow) { wrong++; continue; }
            if (!arrow) continue;
            checked++;
            const bool onEdge = ax <= GameEngineMaze::COMPASS_INSET + 1 || ax >= 127 - GameEngineMaze::COMPASS_INSET - 1 ||
                                ay <= HUD_H + GameEngineMaze::COMPASS_INSET + 1 || ay >= 159 - GameEngineMaze::COMPASS_INSET - 1;
            const float px = e.sx(e._player.px()), py = e.sy(e._player.py());
            const float dot = (qx - px) * (ax - px) + (qy - py) * (ay - py);
            if (!onEdge || dot <= 0) wrong++;
        }
    const bool compass = aims && checked > 5 && wrong == 0;
    printf("wayfinding: %d cells walked, %d crumbs, here %d; compass aims at the next key %d, %d arrows on the edge its way, %d wrong -> %s\n",
           walked, marked, here, aims, checked, wrong, crumbs && compass ? "PASS" : "FAIL");
    return crumbs && compass;
}

// The bot: the way to the next key (lowest first) or the exit, through open
// ways; A when beside a paused-able trap whose lane it's about to enter.
static int botDir(GameEngineMaze &e) {
    const int W = e._maze.width, H = e._maze.height;
    int tx = e._exit.x, ty = e._exit.y;
    for (int k = 0; k < e._activeKeys; k++) if (!e._keys[k].collected) { tx = e._keys[k].x; ty = e._keys[k].y; break; }
    static int16_t prev[MazeGenerator::MAX_W * MazeGenerator::MAX_H];
    static int16_t q[MazeGenerator::MAX_W * MazeGenerator::MAX_H];
    for (int i = 0; i < W * H; i++) prev[i] = -2;
    const int start = e._player.y * W + e._player.x, goal = ty * W + tx;
    int head = 0, tail = 0;
    q[tail++] = start; prev[start] = -1;
    while (head < tail && prev[goal] == -2) {
        const int c = q[head++], x = c % W, y = c / W;
        for (uint8_t d : MazeGenerator::DIRS) {
            if (!e.canPass(x, y, d)) continue;
            const int nc = (y + MazeGenerator::dy(d)) * W + x + MazeGenerator::dx(d);
            bool pad = false;
            for (int i = 0; i < e._activePads; i++) pad |= e._pads[i].y * W + e._pads[i].x == nc;
            if (pad && nc != goal) continue;
            if (prev[nc] == -2) { prev[nc] = c; q[tail++] = nc; }
        }
    }
    if (prev[goal] == -2 || goal == start) return 0;
    int c = goal;
    while (prev[c] != start) c = prev[c];
    const int nx = c % W, ny = c / W;
    // Into a type A trap's lane only just after a bullet's gone, with time
    // to cross it; into type B's only while paused. (Mid-step too: the
    // held stick would carry it on.)
    for (int i = 0; i < e._activeTraps; i++) {
        const TrapEmitter &t = e._traps[i];
        if (!t.active || !t.inLane(nx, ny) || t.inLane(e._player.x, e._player.y)) continue;
        if (t.type == TrapEmitter::TYPE_B ? !t.paused()
                                          : t.bulletsInFlight() || t.msToNextShot() < (t.laneLen + 2) * MOVE_MS)
            return -1;
    }
    return nx > e._player.x ? WALL_E : nx < e._player.x ? WALL_W : ny > e._player.y ? WALL_S : WALL_N;
}

static bool play(long frames) {
    static MazeFluxGame g;
    GameEngineMaze &e = startAt(g, 1);
    FrameDumper dump("maze");
    int deaths = 0, best = 1, lastScore = 0;
    double usTotal = 0; long usFrames = 0;
    bool ok = true, wasDying = false;
    for (long f = 0; f < frames; f++) {
        e._player.lives = 3;
        InputState in{};
        if (e._state == GameEngineMaze::STATE_PLAYING) {
            const int dir = botDir(e);
            in = stick(dir > 0 ? dir : 0);
            // Waiting at a type B trap's lane: its switch.
            if (dir < 0)
                for (int i = 0; i < e._activeTraps; i++)
                    if (e._traps[i].type == TrapEmitter::TYPE_B && !e._traps[i].paused() &&
                        e._traps[i].switchReach(e._player.x, e._player.y))
                        in.btnA = (f & 1);
        } else if (e._state == GameEngineMaze::STATE_LEVEL_COMPLETE) {
            in.btnA = (f / 10) & 1;
        }
        const auto t0 = std::chrono::steady_clock::now();
        frame(g, in);
        if (e._state == GameEngineMaze::STATE_PLAYING) {
            usTotal += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            usFrames++;
        }
        dump.maybeDump(f, g_canvas);
        if (e._dyingUntil && !wasDying) {
            deaths++;
            if (getenv("MAZE_TRACE")) printf("  f%ld L%d died: %s at %d,%d\n", f, e._level,
                e._deathCause == GameEngineMaze::DEATH_BOMB ? "bomb" : e._deathCause == GameEngineMaze::DEATH_TIME ? "clock" : "bullet",
                e._player.x, e._player.y);
            if (getenv("MAZE_TRACE") && getenv("MAZE_TRAPS"))
                for (int i = 0; i < e._activeTraps; i++) {
                    const TrapEmitter &t = e._traps[i];
                    printf("    trap %c at %d,%d dir %d,%d lane %d paused %d inLane %d from %d,%d moving %d\n", t.type ? 'B' : 'A', t.x, t.y, t.dirX, t.dirY,
                           t.laneLen, t.paused(), t.inLane(e._player.nearX(), e._player.nearY()), e._player.fromX, e._player.fromY, e._player.moving);
                }
        }
        wasDying = e._dyingUntil != 0;
        if (e._score < lastScore) { printf("  frame %ld: score dropped\n", f); ok = false; }
        if (getenv("MAZE_TRACE") && f % 300 == 0)
            printf("  f%ld L%d state %d at %d,%d time %d keys %d/%d\n", f, e._level, (int)e._state, e._player.x, e._player.y,
                   e._timeLeft, [&] { int n = 0; for (int k = 0; k < e._activeKeys; k++) n += e._keys[k].collected; return n; }(), e._activeKeys);
        lastScore = e._score;
        best = max(best, e._level);
    }
    ok &= best >= 15 && deaths <= 5;   // it waits out traps and walks past bombs: no level is unfair
    printf("play: %ld frames, reached level %d, %d lives lost, host %.0fus a frame in play -> %s\n", frames, best, deaths,
           usFrames ? usTotal / usFrames : 0.0, ok ? "PASS" : "FAIL");
    return ok;
}

static bool quit() {
    static MazeFluxGame g;
    GameEngineMaze &e = startAt(g, 3);
    e._score = 777;
    g.onQuit(g_audio);
    const bool ok = e._scores.table().e[0].score == 777 || e._scores.lastRank() >= 0;
    printf("quit: mid-game score on the table %d -> %s\n", ok, ok ? "PASS" : "FAIL");
    return ok;
}

static bool idle() {
    static MazeFluxGame g;
    g.init(g_audio);
    GameEngineMaze &e = g._engine;
    int pages = 0, last = -1;
    for (int f = 0; f < (int)(3 * 8200 / STEP_MS); f++) {
        frame(g, stick(0, f < 5));                 // A held from the menu at first: no start
        if (e._attractPage != last) { pages++; last = e._attractPage; }
    }
    const bool stayed = e._state == GameEngineMaze::STATE_TITLE;
    frame(g, InputState{});
    frame(g, stick(0, true));
    const bool started = e._state == GameEngineMaze::STATE_PLAYING && e._level == 1 && e._player.lives == 3;
    const bool ok = pages >= 3 && stayed && started;
    printf("idle: %d attract pages, held A ignored %d, A starts %d -> %s\n", pages, stayed, started, ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "all";
    const long frames = argc > 2 ? atol(argv[2]) : 60000;
    const bool all = !strcmp(which, "all");
    bool ok = true;
    if (all || !strcmp(which, "layout"))   ok &= layout();
    if (all || !strcmp(which, "doors"))    ok &= doors();
    if (all || !strcmp(which, "bullets"))  ok &= bullets();
    if (all || !strcmp(which, "death"))    ok &= death();
    if (all || !strcmp(which, "teleport")) ok &= teleport();
    if (all || !strcmp(which, "complete")) ok &= complete();
    if (all || !strcmp(which, "buffer"))   ok &= buffer();
    if (all || !strcmp(which, "wayfinding")) ok &= wayfinding();
    if (all || !strcmp(which, "passing"))  ok &= passing();
    if (all || !strcmp(which, "play"))     ok &= play(frames);
    if (all || !strcmp(which, "quit"))     ok &= quit();
    if (all || !strcmp(which, "idle"))     ok &= idle();
    return ok ? 0 : 1;
}
