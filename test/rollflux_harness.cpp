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

static bool check(const char* what, bool cond, bool &ok) {
    printf("  %-66s %s\n", what, cond ? "ok" : "BAD");
    ok &= cond;
    return cond;
}

// A fresh game (past the attract screens), its scene ready, the respawn
// wait over.
static void fresh(RollFluxGame &g, AudioEngine &audio, GFXcanvas16 &canvas) {
    g.init(audio);
    g.startNewGame(audio);
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
    // The dash at full strength clears a two-cell gap (2-1, rows 7-8,
    // north from row 9) but not a three (2-4, rows 9-11, north from row
    // 12); rolling at the usual top speed, the two-cell gap is a fall.
    auto jump = [&](int course, float r, bool dash) {
        g.loadCourse(course);
        at(g, 5.5f, r);
        if (dash) g.startDash(0, 1, DASH_SPEED_MAX);
        else g._vz = BALL_MAX_SPEED;
        bool landed = false;
        for (int f = 0; f < 40 && !g._fellOut && !landed; ++f) {
            run(g, 0, 0, 1);
            landed = !g._falling && g.rowAt(g._bz) < (int)r - 1;
        }
        g.loadCourse(0);
        return landed;
    };
    check("a full dash clears a two-cell gap", jump(4, 9, true), ok);
    check("but not a three-cell one", !jump(7, 12, true), ok);
    check("and rolling at top speed doesn't clear the two", !jump(4, 9, false), ok);
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
    check("then the next course, 1-2", g._phase == RollFluxGame::PHASE_PLAYING && g._course == 1 &&
          g._loop == 0 && g._courseMs == COURSES[1].seconds * 1000L, ok);
    // After the last course, round again with 15% less time.
    g._course = COURSE_COUNT - 1;
    g.startCourse(audio);
    g._holdUntil = 0;
    g.reachGoal(audio);
    for (int f = 0; f < 200 && g._phase == RollFluxGame::PHASE_CLEAR; ++f) frame(g, audio, canvas);
    check("after the last, round again with 15% less time",
          g._course == COURSE_COUNT && g._loop == 1 && g._courseMs == 51000, ok);
    g._course = 0;
    g._loop = 0;
    g.startCourse(audio);

    // The dash meter: five gems a step. With none, A does nothing; with a
    // step, A charges (the ball held back), and letting go after a full
    // charge dashes, the step used; let go at once, nothing, the step kept.
    InputState holdA{}; holdA.btnA = true;
    at(g, 5.5f, 33);
    g._dashGems = 4;
    frame(g, audio, canvas, holdA);
    check("no dash step: A doesn't charge", !g._charging, ok);
    frame(g, audio, canvas);
    g._dashGems = 5;
    at(g, 5.5f, 33);
    frame(g, audio, canvas, holdA);
    frame(g, audio, canvas);
    check("a tap: no dash, the step kept", !g._charging && g._dashGems == 5 && g._dashes == 0, ok);
    at(g, 5.5f, 33);
    g._vz = 600;
    frame(g, audio, canvas, holdA);
    for (int f = 0; f < 14; ++f) frame(g, audio, canvas, holdA);
    check("held: charging, the ball held back", g._charging &&
          sqrtf(g._vx * g._vx + g._vz * g._vz) <= BALL_MAX_SPEED * DASH_CHARGE_HOLD + 1, ok);
    frame(g, audio, canvas);
    char line[96];
    snprintf(line, sizeof(line), "let go after a full charge: a dash (%.0f), the step used",
             sqrtf(g._vx * g._vx + g._vz * g._vz));
    check(line, g._dashes == 1 && g._dashGems == 0 && g._vz > BALL_MAX_SPEED * 1.8f, ok);
    // A gem fills the meter.
    const int meter = g._dashGems;
    at(g, 2, 21.2f);
    g._yaw = 0;
    const int taken = g._courseGemsTaken;
    for (int f = 0; f < 60 && g._courseGemsTaken == taken; ++f) frame(g, audio, canvas, InputState{ -1.0f, 0 });
    check("a gem goes on the dash meter", g._dashGems == meter + 1, ok);

    // The last ball gone: game over; A (after its delay) starts again.
    at(g, 5.5f, 22);
    for (int f = 0; f < 120 && g._phase == RollFluxGame::PHASE_PLAYING; ++f)
        frame(g, audio, canvas, InputState{ -1.0f, 0 });
    check("the last ball gone, with a score: name entry", g._phase == RollFluxGame::PHASE_NAME && g._lives == 0, ok);
    InputState a{}; a.btnA = a.btnAPressed = true;
    // (A held as the game ended doesn't count: a fresh press for each letter.)
    frame(g, audio, canvas);
    for (int i = 0; i < 3; ++i) { frame(g, audio, canvas, a); frame(g, audio, canvas); }
    g._scores.begin("roll");
    check("three letters in: game over, the score on the table", g._phase == RollFluxGame::PHASE_GAMEOVER &&
          g._scores.best() == g._score, ok);
    frame(g, audio, canvas, a);
    check("A straight away does nothing", g._phase == RollFluxGame::PHASE_GAMEOVER, ok);
    for (int f = 0; f < 40; ++f) frame(g, audio, canvas);
    frame(g, audio, canvas, a);
    check("A after a second: a new game", g._phase == RollFluxGame::PHASE_PLAYING &&
          g._lives == START_LIVES && g._score == 0 && g._loop == 0, ok);
    printf("rules -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// Every course: well formed (one start, a goal), and the autopilot's
// planner finds a way from the start to the goal and to every gem, and on
// from each gem to the goal.
static bool scenarioCourses() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    fresh(g, audio, canvas);
    bool ok = true;
    printf("courses:\n");
    for (int k = 0; k < COURSE_COUNT; ++k) {
        const CourseDef &def = COURSES[k];
        int starts = 0, goals = 0, badRows = 0;
        for (int r = 0; r < def.h; ++r) {
            if ((int)strlen(def.rows[r]) != def.w * 2) ++badRows;
            for (int c = 0; c < def.w; ++c) {
                starts += def.rows[r][c * 2 + 1] == 'S';
                goals += def.rows[r][c * 2 + 1] == 'G';
            }
        }
        g.loadCourse(k);
        g.planDistances(g.colAt(g._startX), g.rowAt(g._startZ));
        bool toGoal = false;
        int gems = 0, gemsReached = 0, gemsOut = 0;
        static int16_t fromStart[MAX_COURSE_W * MAX_COURSE_H];
        memcpy(fromStart, g._dist, sizeof(fromStart));
        for (int r = 0; r < g._h; ++r)
            for (int c = 0; c < g._w; ++c) {
                const int i = r * MAX_COURSE_W + c;
                if (g._cells[r][c].kind == RollFluxGame::K_GOAL && fromStart[i] < 32767) toGoal = true;
                if (!(g._cells[r][c].flags & RollFluxGame::F_GEM)) continue;
                ++gems;
                if (fromStart[i] == 32767) continue;
                ++gemsReached;
                // And from the gem on to the goal.
                g.planDistances(c, r);
                for (int r2 = 0; r2 < g._h; ++r2)
                    for (int c2 = 0; c2 < g._w; ++c2)
                        if (g._cells[r2][c2].kind == RollFluxGame::K_GOAL && g.planDist(c2, r2) < 32767) { ++gemsOut; r2 = g._h; break; }
            }
        char line[96];
        snprintf(line, sizeof(line), "%s %-12s %2dx%-2d %3ds: one start, a goal, a way through, %d/%d/%d gems",
                 def.code, def.name, def.w, def.h, def.seconds, gemsReached, gemsOut, gems);
        check(line, badRows == 0 && starts == 1 && goals >= 1 && toGoal && gemsReached == gems && gemsOut == gems, ok);
    }
    printf("courses -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// The autopilot on every course in turn, its balls topped up (so a fall
// costs only time): it must reach every goal before the clock runs out.
static bool scenarioGod(long frames) {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    fresh(g, audio, canvas);
    bool ok = true;
    printf("god: the autopilot on every course, balls topped up\n");
    for (int k = 0; k < COURSE_COUNT; ++k) {
        g._course = k;
        g._loop = 0;
        g._dashGems = 0;
        g.startCourse(audio);
        const long falls0 = g._falls, dashes0 = g._dashes;
        long f = 0;
        int timeUps = 0;
        for (; f < frames && g._phase == RollFluxGame::PHASE_PLAYING; ++f) {
            g._lives = START_LIVES;
            const long before = g._timeMs;
            frame(g, audio, canvas, g.pilot(true));
            if (g._timeMs > before + 30000) ++timeUps;   // the course started again
        }
        const bool cleared = g._phase == RollFluxGame::PHASE_CLEAR && timeUps == 0;
        char line[128];
        snprintf(line, sizeof(line), "%s %-12s %4.1fs of %2lds left, %ld falls, gems %d/%d, %ld dashes",
                 COURSES[k].code, COURSES[k].name, g._timeMs / 1000.0, COURSES[k].seconds,
                 g._falls - falls0, g._courseGemsTaken, g._courseGems, g._dashes - dashes0);
        check(line, cleared, ok);
        g._phase = RollFluxGame::PHASE_PLAYING;
    }
    printf("god -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// A real game from the start, the autopilot at the stick, until its balls
// run out (or N frames). Prints how far it got and the host cost of a
// frame; it must clear the first two courses, and draw the course over
// most of the view.
static bool scenarioPlay(long frames) {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    fresh(g, audio, canvas);
    FrameDumper dump("roll_play");
    std::vector<double> us;
    long f = 0;
    float floorShare = 0;                      // of the screen under the HUD, not sky
    for (; f < frames && g._phase != RollFluxGame::PHASE_NAME && g._phase != RollFluxGame::PHASE_GAMEOVER; ++f) {
        const InputState in = g.pilot(true);
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
    printf("play: %ld frames, %ld courses cleared (on %s), %ld falls, %ld gems, %ld dashes, score %ld, %d balls\n",
           f, g._goals, g.courseDef().code, g._falls, g._gemsTotal, g._dashes, g._score, g._lives);
    printf("  host us a frame (update + draw): mean %.0f, p95 %.0f, max %.0f\n",
           sum / us.size(), us[us.size() * 95 / 100], us.back());
    printf("  course drawn over %.0f%% of the view at frame 100\n", floorShare * 100);
    const bool ok = g._goals >= 2 && floorShare > 0.3f;
    printf("play -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// The attract cycle with no input at all: title, how to roll, how to
// dash, the scores, then the demo (silent, nothing kept), and back to the
// title.
static bool scenarioIdle() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    g.init(audio);
    bool ok = true;
    printf("idle:\n");
    const hiscore::Table before = g._scores.table();
    int seen[5] = {};
    long demoFrames = 0, demoMoved = 0;
    float lastX = 0;
    const int audible0 = audio.tones + audio.melodies + audio.wavs;
    int audibleInDemo = 0;
    for (long f = 0; f < 3000; ++f) {
        const bool demo = g._demo;
        const int heard = audio.tones + audio.melodies + audio.wavs;
        frame(g, audio, canvas);
        ++seen[g._slide];
        if (demo) {
            audibleInDemo += audio.tones + audio.melodies + audio.wavs - heard;
            ++demoFrames;
            if (fabsf(g._bx - lastX) > 0.5f) ++demoMoved;
        }
        lastX = g._bx;
        if (f > 100 && !g._demo && g._slide == RollFluxGame::SLIDE_TITLE && demoFrames > 0) break;
    }
    (void)audible0;
    check("title, how to roll, how to dash, scores each shown",
          seen[0] > 0 && seen[1] > 0 && seen[2] > 0 && seen[3] > 0, ok);
    char line[96];
    snprintf(line, sizeof(line), "then the demo, rolling (%ld of %ld frames moving)", demoMoved, demoFrames);
    check(line, demoFrames > 300 && demoMoved > demoFrames / 2, ok);
    snprintf(line, sizeof(line), "the demo makes no sound (%d heard, %d dropped)", audibleInDemo, audio.silencedCalls);
    check(line, audibleInDemo == 0, ok);
    check("and back to the title, the high scores untouched", !g._demo && g._slide == RollFluxGame::SLIDE_TITLE &&
          !memcmp(&before, &g._scores.table(), sizeof(before)), ok);
    printf("idle -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// A mid-demo: the real game must start clean (no score, all balls, course
// 1-1, an empty dash meter, sound back on).
static bool scenarioDemoExit() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    g.init(audio);
    bool ok = true;
    printf("demoexit:\n");
    for (long f = 0; f < 3000 && !(g._demo && g._goals + g._gemsTotal + g._falls > 0); ++f) frame(g, audio, canvas);
    printf("  demo on %s: score %ld, gems %ld, falls %ld, dashes %ld\n", g.courseDef().code, g._score,
           g._gemsTotal, g._falls, g._dashes);
    check("the demo got going", g._demo, ok);
    InputState a{}; a.btnA = a.btnAPressed = true;
    frame(g, audio, canvas, a);
    InputState held{}; held.btnA = true;
    frame(g, audio, canvas, held);
    check("A: a real game, clean", !g._demo && !g._silent && !audio.silenced &&
          g._phase == RollFluxGame::PHASE_PLAYING && g._course == 0 && g._loop == 0 && g._score == 0 &&
          g._lives == START_LIVES && g._dashGems == 0 && g._courseGemsTaken == 0 && !g._charging, ok);
    printf("demoexit -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// Quitting (main.cpp's Back, then onQuit()) from a game in progress keeps
// its score; a name being entered is kept; A starts a game from each
// attract screen.
static bool scenarioMenus() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    bool ok = true;
    printf("menus:\n");
    // An empty table (the scenarios before this one share the stub's NVS).
    hiscore::Table empty;
    hiscore::clear(empty);
    hiscore::save("roll", empty);
    InputState a{}; a.btnA = a.btnAPressed = true;
    for (int slide = 0; slide < 4; ++slide) {
        g.init(audio);
        while (g._slide != slide) frame(g, audio, canvas);
        frame(g, audio, canvas, a);
        char line[64];
        snprintf(line, sizeof(line), "A on attract screen %d starts a game", slide);
        check(line, g._phase == RollFluxGame::PHASE_PLAYING && !g._demo, ok);
    }
    // A game in progress, quit: its score goes on the table.
    g._score = 12340;
    g.onQuit(audio);
    g._scores.begin("roll");
    check("Back mid-game: the score's on the table", g._scores.best() == 12340, ok);
    // The last ball gone with a score for the table: name entry; Back keeps it.
    g.init(audio);
    g.startNewGame(audio);
    g._score = 23450;
    g._lives = 1;
    g.loseLife(audio, nullptr);
    check("game over with a high score: name entry", g._phase == RollFluxGame::PHASE_NAME, ok);
    g.onQuit(audio);
    g._scores.begin("roll");
    check("Back during it: the score's kept", g._scores.best() == 23450, ok);
    printf("menus -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static void scenarioPose() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    fresh(g, audio, canvas);
    struct Spot { const char* name; int course; float c, r, yawDeg, drop, joyX, joyY; bool glow; };
    const Spot spots[] = {
        { "start",    0, 5.0f, 33,    0,    0,  0, 0, false },
        { "rails",    0, 5.5f, 30.5f, 0,    0,  0, 0, false },
        { "ramp",     0, 5.5f, 26.5f, 0,    0,  0, 0, false },
        { "gap",      0, 5.5f, 23.5f, 0,    0,  0, 0, false },
        { "bridge",   0, 9.0f, 22.5f, 0,    0,  0, 0, false },
        { "ice",      0, 5.5f, 18.5f, 0,    0,  0, 0, false },
        { "block",    0, 4.5f, 10.5f, 0,    0,  0, 0, false },
        { "sideways", 0, 3.5f, 6,     45,   0,  0, 0, false },
        { "goal",     0, 7.5f, 4,     0,    0,  0, 0, false },
        { "lean",     0, 5.5f, 23.5f, 0,    0, -1, 1, false },
        // Fallen into the gap below the near edge: the plateau in front
        // must hide it (camera left where it was, as during a fall).
        { "behind",   0, 5.5f, 20.7f, 0,   60,  0, 0, false },
        // Other courses: the climb's levels, and the Ice Relay's palette with
        // the dash gap ahead and the ball glowing in a dash.
        { "climb",    3, 5.5f, 24.5f, 0,    0,  0, 0, false },
        { "icerelay", 4, 5.5f, 10.5f, 0,    0,  0, 0, true },
        { "frost",    7, 5.5f, 21.5f, 0,    0,  0, 0, false },
    };
    for (const Spot &s : spots) {
        g.loadCourse(s.course);
        g._course = s.course;
        at(g, s.c, s.r);
        g._dashUntil = s.glow ? g_fakeMillis + 1000 : 0;
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
    // The title, over the orbiting course 1.
    g.enterAttract();
    for (int f = 0; f < 60; ++f) frame(g, audio, canvas);
    FrameDumper d("roll_title");
    d.n = 1; d.at[0] = 0;
    d.maybeDump(0, canvas);
}

int main(int argc, char** argv) {
    (void)fnv;                                   // harness_common's trace hash: not used here
    const char* which = argc > 1 ? argv[1] : "all";
    const long frames = argc > 2 ? atol(argv[2]) : 0;
    const bool all = !strcmp(which, "all");
    bool ok = true;
    if (all || !strcmp(which, "physics")) ok &= scenarioPhysics();
    if (all || !strcmp(which, "rules")) ok &= scenarioRules();
    if (all || !strcmp(which, "courses")) ok &= scenarioCourses();
    if (all || !strcmp(which, "god")) ok &= scenarioGod(frames ? frames : 4000);
    if (all || !strcmp(which, "play")) ok &= scenarioPlay(frames ? frames : 20000);
    if (all || !strcmp(which, "idle")) ok &= scenarioIdle();
    if (all || !strcmp(which, "demoexit")) ok &= scenarioDemoExit();
    if (all || !strcmp(which, "menus")) ok &= scenarioMenus();
    if (!strcmp(which, "pose")) scenarioPose();
    return ok ? 0 : 1;
}
