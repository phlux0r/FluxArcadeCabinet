// Host harness for Roll Flux: the real game against a fake clock, like the
// other 3D harnesses.
//
//   physics      the ball rolls up a ramp, can't climb a step (or sink into
//                it), stays between rails at top speed, coasts further on
//                ice, is thrown on by a boost pad, falls off an edge, turns
//                the way it rolls, and never ends up inside the floor at top
//                speed and the longest frame
//   rules        gems (points and time), checkpoints, a fall back to the
//                last checkpoint with its time, time running out, game over,
//                and the goal's tally on to the next course
//   play [N]     a scripted driver rolls the ball round the course through
//                the whole game loop (N frames at most, default 3000); it
//                must reach the goal in time. Prints the host cost of a
//                frame: compare runs with each other, not with a budget
//   pose         writes frames at fixed points on the course
//                (roll_<spot>.ppm), to look at by eye: the ball on the
//                floor, its stripes and shadow, rails, gems, a ball fallen
//                behind an edge hidden by it
//   all          everything but pose (the default)
//
// The driver steers for waypoints along course 1, as a stand-in for the
// demo's planned route (which comes with the attract cycle).

#include "harness_common.h"
#include <chrono>
#include <vector>
#include <algorithm>

#define private public
#include "games/RollFlux/RollFluxGame.h"
#undef private

using namespace rollflux;

static const unsigned long STEP_MS = 33;

// The route round course 1, in cell coordinates (column, row from the top;
// whole numbers are cell centres).
static const float ROUTE[][2] = {
    { 5.5f, 33 }, { 5.5f, 29 }, { 5.5f, 26 }, { 5.5f, 24 }, { 2, 23 }, { 2, 19 }, { 2, 16 },
    { 5.5f, 15.5f }, { 5.5f, 14 }, { 4.5f, 13 }, { 4.5f, 11 }, { 6.5f, 11 }, { 6.5f, 9.5f },
    { 3.5f, 9 }, { 3.5f, 6 }, { 3.5f, 5 }, { 5.5f, 4.5f }, { 7.5f, 3 }, { 7.5f, 1 },
};
static const int ROUTE_N = sizeof(ROUTE) / sizeof(ROUTE[0]);

static void routePoint(const RollFluxGame &g, int i, float &x, float &z) {
    x = (ROUTE[i][0] + 0.5f) * CELL;
    z = (g._h - 1 - ROUTE[i][1] + 0.5f) * CELL;
}

// Stick input that steers the ball for the next waypoint at a steady pace,
// braking against its own velocity: the wanted change of velocity, turned
// into the camera-relative stick the game expects (landscape: screen up is
// -joyX, right +joyY). After a fall it starts again from the waypoint
// nearest the ball.
struct Driver {
    int next = 1;
    InputState input(const RollFluxGame &g) {
        float wx, wz;
        routePoint(g, next, wx, wz);
        float dx = wx - g._bx, dz = wz - g._bz;
        if (dx * dx + dz * dz > 1500.0f * 1500.0f) {
            float best = 1e30f;
            for (int i = 1; i < ROUTE_N; ++i) {
                float x, z;
                routePoint(g, i, x, z);
                const float d = (x - g._bx) * (x - g._bx) + (z - g._bz) * (z - g._bz);
                if (d < best) { best = d; next = i; }
            }
            routePoint(g, next, wx, wz);
            dx = wx - g._bx; dz = wz - g._bz;
        }
        const float d = sqrtf(dx * dx + dz * dz);
        // Reached it, or gone past it (nearer the one after than it is).
        bool passed = false;
        if (next < ROUTE_N - 1) {
            float ax, az;
            routePoint(g, next + 1, ax, az);
            passed = (ax - g._bx) * (ax - g._bx) + (az - g._bz) * (az - g._bz) <
                     (ax - wx) * (ax - wx) + (az - wz) * (az - wz);
        }
        if ((d < 70.0f || passed) && next < ROUTE_N - 1) { ++next; routePoint(g, next, wx, wz); dx = wx - g._bx; dz = wz - g._bz; }
        const float dd = sqrtf(dx * dx + dz * dz) + 1e-3f;
        const float want = fminf(420.0f, dd * 2.5f);
        float ax = (dx / dd * want - g._vx) / 300.0f, az = (dz / dd * want - g._vz) / 300.0f;
        const float m = sqrtf(ax * ax + az * az);
        if (m > 1.0f) { ax /= m; az /= m; }
        const float fx = sinf(g._yaw), fz = cosf(g._yaw), rx = cosf(g._yaw), rz = -sinf(g._yaw);
        InputState in{};
        in.joyX = -(ax * fx + az * fz);
        in.joyY = ax * rx + az * rz;
        return in;
    }
};

static bool check(const char* what, bool cond, bool &ok) {
    printf("  %-66s %s\n", what, cond ? "ok" : "BAD");
    ok &= cond;
    return cond;
}

// A fresh game, its scene ready, the respawn wait over.
static void fresh(RollFluxGame &g, AudioEngine &audio, GFXcanvas16 &canvas) {
    g.init(audio);
    g.update(canvas, InputState{}, audio);
    g_fakeMillis += 1000;
    g._holdUntil = 0;
    g._lastFrameMs = g_fakeMillis;
}

// The ball still at cell (c, r) (fractions allowed), on the floor there.
static void at(RollFluxGame &g, float c, float r) {
    g._bx = (c + 0.5f) * CELL; g._bz = (g._h - 1 - r + 0.5f) * CELL;
    float y = 0; g.floorAt(g._bx, g._bz, y); g._by = y;
    g._vx = g._vz = g._vy = 0; g._falling = false; g._fellOut = false; g._yaw = 0;
    g._extraSpeed = 0; g._holdUntil = 0;
}

static void run(RollFluxGame &g, float joyX, float joyY, int frames, unsigned long ms = STEP_MS) {
    for (int f = 0; f < frames; ++f) {
        InputState in{}; in.joyX = joyX; in.joyY = joyY;
        g._lastFrameMs = g_fakeMillis; g_fakeMillis += ms;
        g.updateFrameScale();
        g.stepBall(in);
    }
}

static bool scenarioPhysics() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    fresh(g, audio, canvas);
    bool ok = true;
    printf("physics:\n");
    // Up the ramp (rows 27 to 25, rising north): stick up, camera facing north.
    at(g, 5.5f, 28);
    run(g, -1.0f, 0, 40);
    check("rolls up the ramp onto the plateau", g._by >= HEIGHT_STEP - 1 && !g._falling, ok);
    // The raised block (height 2, rows 7-8, cols 5-6) from the east: a wall,
    // and the ball stops short of it by about its radius.
    at(g, 7.5f, 7.5f);
    g._yaw = -(float)PI / 2;                       // facing west
    run(g, -1.0f, 0, 40);
    check("can't climb a two-step block: bounced off it", g._bx > 7.0f * CELL && g._by < 1.0f, ok);
    check("and doesn't sink into it (stops a radius short)", g._bx > 7.0f * CELL + WALL_PROBE - SUBSTEP, ok);
    // Off the plateau's edge into the gap (rows 19-21 void between cols 2 and 9).
    at(g, 5.5f, 22);
    run(g, -1.0f, 0, 120);
    check("rolls off into the gap and falls out", g._fellOut, ok);
    // Rails: the right-hand bridge (col 9, rows 19-21) is railed both sides.
    // Flung at it sideways at full speed, with 100ms frames, it stays on.
    bool stayed = true;
    for (int trial = 0; trial < 16; ++trial) {
        at(g, 9, 20);
        const float s = (trial & 1) ? 1.0f : -1.0f;
        g._vx = s * BALL_MAX_SPEED;
        g._vz = (trial % 3 - 1) * 200.0f;
        g._yaw = (float)PI / 2;                    // facing east: stick up pushes east
        for (int f = 0; f < 10; ++f) {
            run(g, -s, 0, 1, 100);
            const int c = g.colAt(g._bx);
            if (g._falling || c != 9) stayed = false;
        }
    }
    check("rails keep the ball on a bridge at full speed and 100ms frames", stayed, ok);
    // The unrailed bridge (col 2): the same throw goes over the edge.
    at(g, 2, 20);
    g._vx = BALL_MAX_SPEED;
    run(g, 0, 0, 10);
    check("without rails the same throw goes over", g._falling || g._fellOut, ok);
    // Ice (rows 15-16, cols 4-7) against plain floor (row 18): coasting
    // east from the same speed, the ball goes further on ice.
    at(g, 3.6f, 15);
    g._vx = 500;
    run(g, 0, 0, 30);
    const float iceDist = g._bx - 4.1f * CELL;
    at(g, 3.6f, 18);
    g._vx = 500;
    run(g, 0, 0, 30);
    const float floorDist = g._bx - 4.1f * CELL;
    char line[96];
    snprintf(line, sizeof(line), "coasts further on ice (%.0f) than on floor (%.0f)", iceDist, floorDist);
    check(line, iceDist > floorDist * 1.3f, ok);
    // And steers less: rolling north, full stick right for a third of a
    // second turns it much less on ice (rows 15-16) than on floor.
    auto turned = [&](float r) {
        at(g, 5.5f, r);
        g._vz = 500;
        run(g, 0, 1.0f, 10);
        return atan2f(g._vx, g._vz);
    };
    const float iceTurn = turned(16.4f), floorTurn = turned(24.5f);
    snprintf(line, sizeof(line), "steers less on ice (%.2f rad) than on floor (%.2f)", iceTurn, floorTurn);
    check(line, iceTurn < floorTurn * 0.4f, ok);
    // The boost pad (row 28, pushing north) speeds it up, past its usual top speed.
    auto peak = [&](float vz) {
        at(g, 5.5f, 29);
        g._vz = vz;
        float top = 0;
        for (int f = 0; f < 30; ++f) { run(g, 0, 0, 1); top = fmaxf(top, g._vz); }
        return top;
    };
    const float slow = peak(300), fast = peak(BALL_MAX_SPEED - 50);
    snprintf(line, sizeof(line), "a boost pad throws it on (300 -> %.0f)", slow);
    check(line, slow > 800, ok);
    snprintf(line, sizeof(line), "and past the usual top speed (%.0f -> %.0f)", BALL_MAX_SPEED - 50, fast);
    check(line, fast > BALL_MAX_SPEED + 100, ok);
    // Turning as it rolls: rolled east, the top of the ball has gone east;
    // rolled south, south; by the distance over the radius.
    at(g, 5.5f, 33);
    g._rot[0] = g._rot[4] = g._rot[8] = 1;
    g._rot[1] = g._rot[2] = g._rot[3] = g._rot[5] = g._rot[6] = g._rot[7] = 0;
    float x0 = g._bx;
    g._vx = 300;
    run(g, 0, 0, 4);
    float angle = acosf(fmaxf(-1.0f, fminf(1.0f, g._rot[4])));
    snprintf(line, sizeof(line), "rolled east, its top turns east (top x %.2f, %.2f rad for %.2f)",
             g._rot[1], angle, (g._bx - x0) / BALL_RADIUS);
    check(line, g._rot[1] > 0.3f && fabsf(angle - (g._bx - x0) / BALL_RADIUS) < 0.05f, ok);
    at(g, 5.5f, 33);
    g._rot[0] = g._rot[4] = g._rot[8] = 1;
    g._rot[1] = g._rot[2] = g._rot[3] = g._rot[5] = g._rot[6] = g._rot[7] = 0;
    g._vz = -300;
    run(g, 0, 0, 4);
    snprintf(line, sizeof(line), "rolled south, its top turns south (top z %.2f)", g._rot[7]);
    check(line, g._rot[7] < -0.3f && fabsf(g._rot[1]) < 0.01f, ok);
    // The camera turns to follow the ball rolling sideways, but holds its
    // heading while the ball rolls back towards it (down a ramp, say), so
    // the stick doesn't swap round.
    auto follow = [&](float vx, float vz) {
        at(g, 5.5f, 33);
        g._vx = vx; g._vz = vz;
        g._dt = 0.033f;
        for (int f = 0; f < 30; ++f) g.updateCamera(false);
        return g._yaw;
    };
    const float backYaw = follow(-60, -600), sideYaw = follow(600, 0);
    snprintf(line, sizeof(line), "camera holds while it rolls back (yaw %.2f), follows sideways (%.2f)",
             backYaw, sideYaw);
    check(line, fabsf(backYaw) < 0.05f && sideYaw > 1.0f, ok);
    // Every course: no ramp whose high edge drops to a lower floor beyond
    // it (a sawtooth: the ball tops it, falls, and meets a step back).
    int saw = 0;
    for (int k = 0; k < COURSE_COUNT; ++k) {
        g.loadCourse(k);
        for (int r = 0; r < g._h; ++r)
            for (int c = 0; c < g._w; ++c) {
                const uint8_t kind = g._cells[r][c].kind;
                if (!RollFluxGame::isRamp(kind)) continue;
                static const int DC[4] = { 0, 0, 1, -1 }, DR[4] = { -1, 1, 0, 0 };
                const int d = kind - RollFluxGame::K_RAMP_N;
                const int nc = c + DC[d], nr = r + DR[d];
                if (!g.solid(nc, nr)) continue;
                const float top = (g._cells[r][c].h + 1) * HEIGHT_STEP;
                // The neighbour's height just past the shared edge.
                const float ex = g.cellX0(c) + CELL * 0.5f + DC[d] * (CELL * 0.5f + 1.0f);
                const float ez = g.cellZ0(r) + CELL * 0.5f - DR[d] * (CELL * 0.5f + 1.0f);
                float ny = 0;
                g.floorAt(ex, ez, ny);
                if (ny < top - STEP_DOWN) {
                    printf("    course %d: ramp at col %d row %d drops to %.0f beyond its top (%.0f)\n", k + 1, c, r, ny, top);
                    ++saw;
                }
            }
    }
    g.loadCourse(0);
    check("no course has a ramp that drops away at its top", saw == 0, ok);
    // Never inside the floor: fling it about at top speed with 100ms frames.
    int inside = 0;
    for (int trial = 0; trial < 40; ++trial) {
        at(g, 5.5f, 30 - (trial % 6));
        g._vx = BALL_MAX_SPEED * cosf(trial * 0.7f);
        g._vz = BALL_MAX_SPEED * sinf(trial * 0.7f);
        for (int f = 0; f < 20; ++f) {
            run(g, cosf(trial + f * 0.3f), sinf(trial * 1.3f + f * 0.2f), 1, 100);
            float y;
            if (!g._falling && g.floorAt(g._bx, g._bz, y) && g._by < y - 1.0f) ++inside;
        }
    }
    check("never below the floor it's rolling on (100ms frames, full speed)", inside == 0, ok);
    printf("physics -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// One frame of the whole game.
static void frame(RollFluxGame &g, AudioEngine &audio, GFXcanvas16 &canvas, InputState in = InputState{}) {
    g_fakeMillis += STEP_MS;
    g.update(canvas, in, audio);
}

static bool scenarioRules() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    bool ok = true;
    printf("rules:\n");
    fresh(g, audio, canvas);
    check("starts with 4 balls, the course's time, gems to find",
          g._lives == START_LIVES && g._timeMs > 50000 && g._courseGems == 7, ok);

    // A gem: the one on the left bridge (col 2, row 20).
    at(g, 2, 21.2f);
    g._yaw = 0;
    const long score0 = g._score, time0 = g._timeMs;
    for (int f = 0; f < 60 && g._courseGemsTaken == 0; ++f) frame(g, audio, canvas, InputState{ -1.0f, 0 });
    check("a gem: 100 points, 2 more seconds",
          g._courseGemsTaken == 1 && g._score == score0 + GEM_POINTS && g._timeMs > time0 + 1000, ok);

    // A fall with no checkpoint yet: back to the start, a ball fewer.
    at(g, 5.5f, 22);
    for (int f = 0; f < 120 && g._lives == START_LIVES; ++f) frame(g, audio, canvas, InputState{ -1.0f, 0 });
    check("a fall: a ball fewer, back at the start",
          g._lives == START_LIVES - 1 && fabsf(g._bx - g._startX) < 1 && fabsf(g._bz - g._startZ) < 1, ok);
    check("and the ball waits a moment", (long)(g_fakeMillis - g._holdUntil) < 0, ok);

    // The checkpoint row (17): rolled onto, then a fall puts it back there
    // with the time it had.
    at(g, 5.5f, 18.4f);
    for (int f = 0; f < 60 && g._checkR < 0; ++f) frame(g, audio, canvas, InputState{ -1.0f, 0 });
    const long checkTime = g._respawnTimeMs;
    check("the checkpoint row is reached", g._checkR == 17, ok);
    at(g, 5.5f, 22);
    g._timeMs = 5000;
    for (int f = 0; f < 120 && g._lives == START_LIVES - 1; ++f) frame(g, audio, canvas, InputState{ -1.0f, 0 });
    check("a fall after it: back at the checkpoint, with its time",
          g._lives == START_LIVES - 2 && g.rowAt(g._bz) == 17 && g._timeMs == checkTime, ok);

    // Time running out: a ball fewer, the course from the top (gems back).
    g._timeMs = 40;
    g._holdUntil = 0;
    frame(g, audio, canvas);
    frame(g, audio, canvas);
    check("time up: a ball fewer, the course again with its gems and time",
          g._lives == START_LIVES - 3 && g._courseGemsTaken == 0 && g._timeMs == g._courseMs &&
          g._checkR < 0 && fabsf(g._bx - g._startX) < 1, ok);

    // The goal: the tally, then the next course (round again: 15% less time).
    g._holdUntil = 0;
    g._timeMs = 30500;
    const long before = g._score;
    at(g, 7, 2.2f);
    for (int f = 0; f < 60 && g._phase == RollFluxGame::PHASE_PLAYING; ++f)
        frame(g, audio, canvas, InputState{ -1.0f, 0 });
    check("the goal: the course is clear", g._phase == RollFluxGame::PHASE_CLEAR, ok);
    printf("  (tally: time %ld, no falls %ld, all gems %ld; score %ld -> %ld)\n", g._clearTime, g._clearNoFall,
           g._clearAllGems, before, g._score);
    // (No falls since time ran out and the course started again.)
    check("time bonus 100 a second left, and the no-falls bonus",
          g._clearTime >= 28 * TIME_POINTS && g._clearTime <= 30 * TIME_POINTS && g._clearNoFall == NO_FALL_BONUS &&
          g._clearAllGems == 0 && g._score == before + g._clearTime + g._clearNoFall, ok);
    for (int f = 0; f < 200 && g._phase == RollFluxGame::PHASE_CLEAR; ++f) frame(g, audio, canvas);
    check("then the next course (round again, less time)",
          g._phase == RollFluxGame::PHASE_PLAYING && g._loop == 1 && g._courseMs == 51000, ok);

    // The last ball gone: game over; A (after its delay) starts again.
    at(g, 5.5f, 22);
    for (int f = 0; f < 120 && g._phase == RollFluxGame::PHASE_PLAYING; ++f)
        frame(g, audio, canvas, InputState{ -1.0f, 0 });
    check("the last ball gone: game over", g._phase == RollFluxGame::PHASE_GAMEOVER && g._lives == 0, ok);
    InputState a{}; a.btnA = a.btnAPressed = true;
    frame(g, audio, canvas, a);
    check("A straight away does nothing", g._phase == RollFluxGame::PHASE_GAMEOVER, ok);
    for (int f = 0; f < 40; ++f) frame(g, audio, canvas);
    frame(g, audio, canvas, a);
    check("A after a second: a new game", g._phase == RollFluxGame::PHASE_PLAYING &&
          g._lives == START_LIVES && g._score == 0 && g._loop == 0, ok);
    printf("rules -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool scenarioPlay(long frames) {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    g.init(audio);
    FrameDumper dump("roll_play");
    Driver drv;
    std::vector<double> us;
    long f = 0;
    float floorShare = 0;                      // of the screen under the HUD, not sky
    for (; f < frames && g._phase == RollFluxGame::PHASE_PLAYING; ++f) {
        InputState in = drv.input(g);
        g_fakeMillis += STEP_MS;
        auto t0 = std::chrono::steady_clock::now();
        g.update(canvas, in, audio);
        auto t1 = std::chrono::steady_clock::now();
        us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        dump.maybeDump(f, canvas);
        if (f == 100) {
            int n = 0;
            for (int y = 20; y < 128; ++y)
                for (int x = 0; x < 160; ++x) n += canvas.getBuffer()[y * 160 + x] != g._sky[y];
            floorShare = n / (108.0f * 160.0f);
        }
    }
    std::sort(us.begin(), us.end());
    double sum = 0;
    for (double u : us) sum += u;
    const bool cleared = g._phase == RollFluxGame::PHASE_CLEAR;
    printf("play: %ld frames, %s, %ld falls, gems %d/%d, %.1fs left, score %ld\n", f,
           cleared ? "reached the goal" : "didn't reach the goal", g._falls, g._courseGemsTaken,
           g._courseGems, g._timeMs / 1000.0, g._score);
    printf("  host us a frame (update + draw): mean %.0f, p95 %.0f, max %.0f\n",
           sum / us.size(), us[us.size() * 95 / 100], us.back());
    printf("  course drawn over %.0f%% of the view at frame 100\n", floorShare * 100);
    const bool ok = cleared && g._lives == START_LIVES && floorShare > 0.3f;
    printf("play -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static void scenarioPose() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    fresh(g, audio, canvas);
    struct Spot { const char* name; float c, r, yawDeg, drop, joyX, joyY; };
    const Spot spots[] = {
        { "start",    5.0f, 33,    0,    0,  0, 0 },
        { "rails",    5.5f, 30.5f, 0,    0,  0, 0 },
        { "ramp",     5.5f, 26.5f, 0,    0,  0, 0 },
        { "gap",      5.5f, 23.5f, 0,    0,  0, 0 },
        { "bridge",   9.0f, 22.5f, 0,    0,  0, 0 },
        { "ice",      5.5f, 18.5f, 0,    0,  0, 0 },
        { "block",    4.5f, 10.5f, 0,    0,  0, 0 },
        { "sideways", 3.5f, 6,     45,   0,  0, 0 },
        { "goal",     7.5f, 4,     0,    0,  0, 0 },
        { "lean",     5.5f, 23.5f, 0,    0, -1, 1 },
        // Fallen into the gap below the near edge: the plateau in front
        // must hide it (camera left where it was, as during a fall).
        { "behind",   5.5f, 20.7f, 0,   60,  0, 0 },
    };
    for (const Spot &s : spots) {
        at(g, s.c, s.r);
        g._yaw = s.yawDeg * (float)PI / 180.0f;
        g._leanRoll = s.joyY * LEAN_ROLL;
        g._leanPitch = -s.joyX * LEAN_PITCH;
        // A little roll so the stripes sit at an angle.
        g.rollBall(37, 23);
        if (s.drop > 0) {
            g._bz -= 2.0f * CELL;              // the camera as it was, on the plateau
            g.updateCamera(true);
            g._bz += 2.0f * CELL;
            g._by = -s.drop;
            g._falling = true;
            g.updateCamera(false);
        } else {
            g.updateCamera(true);
        }
        g.renderFrame(canvas);
        char name[64];
        snprintf(name, sizeof(name), "roll_%s", s.name);
        FrameDumper d(name);
        d.n = 1; d.at[0] = 0;
        d.maybeDump(0, canvas);
    }
}

int main(int argc, char** argv) {
    (void)fnv;                                   // harness_common's trace hash: not used here
    const char* which = argc > 1 ? argv[1] : "all";
    const long frames = argc > 2 ? atol(argv[2]) : 3000;
    const bool all = !strcmp(which, "all");
    bool ok = true;
    if (all || !strcmp(which, "physics")) ok &= scenarioPhysics();
    if (all || !strcmp(which, "rules")) ok &= scenarioRules();
    if (all || !strcmp(which, "play")) ok &= scenarioPlay(frames);
    if (!strcmp(which, "pose")) scenarioPose();
    return ok ? 0 : 1;
}
