// Host harness for Roll Flux's stage 0 (the renderer prototype): the real
// game and the real Jet against a fake clock, like the other 3D harnesses.
//
//   profile [N]  a scripted driver rolls the ball round the course for N
//                frames (default 3000) with each floor renderer and camera
//                height; prints the host render cost and Jet's triangles
//                for each, and whether the driver reached the goal. Host
//                microseconds aren't ESP32 ones: compare the rows with each
//                other, not with a frame budget
//   pose         writes frames of both renderers at fixed points on the
//                course (roll_<renderer>_<camera>_<spot>.ppm), to compare
//                them by eye: they should match, the ball on the floor
//   physics      the ball rolls up a ramp, can't climb a step, falls off an
//                edge and back to the start, and never ends up inside the
//                floor at top speed and the longest frame
//   all          everything (the default)
//
// The driver steers for waypoints along the course, as a stand-in for the
// demo's planned route (which comes with stage 1).

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
// -joyX, right +joyY).
struct Driver {
    int next = 1;
    InputState input(const RollFluxGame &g) {
        float wx, wz;
        routePoint(g, next, wx, wz);
        float dx = wx - g._bx, dz = wz - g._bz;
        const float d = sqrtf(dx * dx + dz * dz);
        if (d < 70.0f && next < ROUTE_N - 1) { ++next; routePoint(g, next, wx, wz); dx = wx - g._bx; dz = wz - g._bz; }
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

static bool scenarioProfile(long frames) {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    g.init(audio);
    bool ok = true;
    printf("profile: %ld frames each, host microseconds per frame (update + render)\n", frames);
    printf("  %-7s %-5s %8s %8s %8s %6s %6s\n", "floor", "cam", "mean", "p95", "max", "tris", "goals");
    for (int direct = 1; direct >= 0; --direct)
        for (int preset = 0; preset < 3; ++preset) {
            g.init(audio);
            g._direct = direct;
            g._preset = preset;
            g.respawn();
            Driver drv;
            std::vector<double> us;
            long tris = 0;
            for (long f = 0; f < frames; ++f) {
                InputState in = drv.input(g);
                auto t0 = std::chrono::steady_clock::now();
                g.update(canvas, in, audio);
                auto t1 = std::chrono::steady_clock::now();
                us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
                tris += g._jetTris;
                g_fakeMillis += STEP_MS;
                if (g._goals > 0) break;
            }
            std::sort(us.begin(), us.end());
            double sum = 0;
            for (double u : us) sum += u;
            const bool reached = g._goals > 0;
            printf("  %-7s %-5s %8.0f %8.0f %8.0f %6ld %6ld%s\n", direct ? "DIRECT" : "JET",
                   CAMERA_PRESETS[preset].name, sum / us.size(), us[us.size() * 95 / 100], us.back(),
                   tris / (long)us.size(), g._goals, reached ? "" : "  (didn't reach the goal)");
            ok &= reached;
        }
    printf("profile -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static void scenarioPose() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    g.init(audio);
    InputState none{};
    g.update(canvas, none, audio);
    struct Spot { const char* name; float c, r, yawDeg; };
    const Spot spots[] = {
        { "start",   5.0f, 33, 0 },
        { "ramp",    5.5f, 28, 0 },
        { "gap",     5.5f, 24, 0 },
        { "block",   4.5f, 10.5f, 0 },
        { "sideways", 3.5f, 6, 45 },
    };
    for (const Spot &s : spots)
        for (int direct = 1; direct >= 0; --direct)
            for (int preset = 0; preset < 3; ++preset) {
                g._direct = direct;
                g._preset = preset;
                g._bx = (s.c + 0.5f) * CELL;
                g._bz = (g._h - 1 - s.r + 0.5f) * CELL;
                float y = 0;
                g.floorAt(g._bx, g._bz, y);
                g._by = y;
                g._vx = g._vz = 0;
                g._falling = false;
                g._yaw = s.yawDeg * (float)PI / 180.0f;
                g.updateCamera(true);
                g.renderFrame(canvas);
                char name[64];
                snprintf(name, sizeof(name), "roll_%s_%s_%s", direct ? "direct" : "jet",
                         CAMERA_PRESETS[preset].name, s.name);
                FrameDumper d(name);
                d.n = 1; d.at[0] = 0;
                d.maybeDump(0, canvas);
            }
}

static bool check(const char* what, bool cond, bool &ok) {
    printf("  %-60s %s\n", what, cond ? "ok" : "BAD");
    ok &= cond;
    return cond;
}

static bool scenarioPhysics() {
    static RollFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(160, 128);
    g.init(audio);
    bool ok = true;
    printf("physics:\n");
    auto at = [&](float c, float r) {
        g._bx = (c + 0.5f) * CELL; g._bz = (g._h - 1 - r + 0.5f) * CELL;
        float y = 0; g.floorAt(g._bx, g._bz, y); g._by = y;
        g._vx = g._vz = g._vy = 0; g._falling = false; g._yaw = 0;
    };
    auto run = [&](float joyX, float joyY, int frames, unsigned long ms = STEP_MS) {
        for (int f = 0; f < frames; ++f) {
            InputState in{}; in.joyX = joyX; in.joyY = joyY;
            g._lastFrameMs = g_fakeMillis; g_fakeMillis += ms;
            g.updateFrameScale();
            g.stepBall(in);
        }
    };
    // Up the ramp (rows 27 to 25, rising north): stick up, camera facing north.
    at(5.5f, 28);
    run(-1.0f, 0, 40);
    check("rolls up the ramp onto the plateau", g._by >= HEIGHT_STEP - 1 && !g._falling, ok);
    // The raised block (height 2, rows 7-8, cols 5-6) from the east: a wall.
    at(7.5f, 7.5f);
    g._yaw = -(float)PI / 2;                       // facing west
    run(-1.0f, 0, 40);
    check("can't climb a two-step block: bounced off it", g._bx > 7.0f * CELL && g._by < 1.0f, ok);
    // Off the plateau's edge into the gap (rows 19-21 void between cols 2 and 9).
    const long falls0 = g._falls;
    at(5.5f, 22);
    run(-1.0f, 0, 120);
    check("rolls off into the gap, falls, and is back at the start",
          g._falls == falls0 + 1 && fabsf(g._bx - g._startX) < CELL && !g._falling, ok);
    // Never inside the floor: fling it about at top speed with 100ms frames.
    int inside = 0;
    for (int trial = 0; trial < 40; ++trial) {
        at(5.5f, 30 - (trial % 6));
        g._vx = BALL_MAX_SPEED * cosf(trial * 0.7f);
        g._vz = BALL_MAX_SPEED * sinf(trial * 0.7f);
        for (int f = 0; f < 20; ++f) {
            run(cosf(trial + f * 0.3f), sinf(trial * 1.3f + f * 0.2f), 1, 100);
            float y;
            if (!g._falling && g.floorAt(g._bx, g._bz, y) && g._by < y - 1.0f) ++inside;
        }
    }
    check("never below the floor it's rolling on (100ms frames, full speed)", inside == 0, ok);
    printf("physics -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    (void)fnv;                                   // harness_common's trace hash: not used here
    const char* which = argc > 1 ? argv[1] : "all";
    const long frames = argc > 2 ? atol(argv[2]) : 3000;
    const bool all = !strcmp(which, "all");
    bool ok = true;
    if (all || !strcmp(which, "physics")) ok &= scenarioPhysics();
    if (all || !strcmp(which, "profile")) ok &= scenarioProfile(frames);
    if (all || !strcmp(which, "pose")) scenarioPose();
    return ok ? 0 : 1;
}
