// Host-side regression harness for Tank Flux.
//
// Runs the real game logic and the real Jet rasteriser on a desktop, against
// a fake clock, a seeded RNG and a scripted bot, then prints a trace of game
// state and framebuffer hashes. An identical trace before and after a change
// means behaviour was preserved; that is what this is for. Built with
// AddressSanitizer, it also catches memory errors and leaks.
//
// It does NOT verify anything about how the game looks or feels, and the
// hardware it stubs out (audio especially) is invisible to it.
//
// See test/README.md. Build and run with test/build.sh.

#include "harness_common.h"
#include <chrono>

// The bot reads game state (health, enemy positions, phase) that the game
// rightly keeps private. Nothing else in the project does this.
#define private public
#include "games/TankFlux/TankFluxGame.h"
#undef private

// What the profile mode groups frames by, so cost can be attributed to what
// was actually on screen.
struct Bucket {
    const char* name;
    long   frames = 0;
    double updateUs = 0;      // host time, only meaningful relative to other buckets
    double drawnTris = 0;     // triangles Jet submitted to the rasteriser
    double rastTris = 0;      // of those, the ones that produced work
    int    sceneObjsMin = 1 << 30, sceneObjsMax = 0;   // drift check
    int    sceneTrisMin = 1 << 30, sceneTrisMax = 0;

    void add(double us, const Renderer::Scene& s, int objs, int tris) {
        ++frames;
        updateUs  += us;
        drawnTris += s.lastFrameDrawnTriangles;
        rastTris  += s.lastFrameRasterizedTriangles;
        if (objs < sceneObjsMin) sceneObjsMin = objs;
        if (objs > sceneObjsMax) sceneObjsMax = objs;
        if (tris < sceneTrisMin) sceneTrisMin = tris;
        if (tris > sceneTrisMax) sceneTrisMax = tris;
    }
    void report() const {
        if (!frames) { printf("  %-12s (never happened)\n", name); return;  }
        printf("  %-12s %6ld %10.0f %10.0f %10.1f    %d-%d / %d-%d\n",
               name, frames, drawnTris / frames, rastTris / frames,
               updateUs / frames, sceneObjsMin, sceneObjsMax, sceneTrisMin, sceneTrisMax);
    }
};

// Scenarios:
//   play    normal run: the bot dies and restarts, so game-over is covered
//   god     health pinned, so a long run reaches many bosses and arena resets
//   menus   exercises a Back quit from the attract screen and mid-game (as
//           main.cpp does it: onQuit(), onExit(), init()) and the game-over
//           timeout
//   profile god, plus per-frame render cost grouped by what was on screen
//   idle     no input at all: the attract cycle (title, how-to-play, demo),
//            reporting how demos went and that they made no sound
//   demoexit press A mid-demo: the real game must start clean (PASS/FAIL)
int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "play";
    const bool profile = strcmp(mode, "profile") == 0;
    const bool god   = strcmp(mode, "god") == 0 || profile;
    const bool menus = strcmp(mode, "menus") == 0;
    const long frames = argc > 2 ? atol(argv[2]) : 20000;
    // Milliseconds of fake clock per frame. The game reads this as its own
    // frame time, so it also sets the frame rate being simulated: 16 is
    // ~60fps, 33 is ~30fps (what the hardware actually manages).
    const unsigned long stepMs = argc > 3 ? (unsigned long)atol(argv[3]) : 16;

    // 0-3 regular enemies, or a boss fight (the boss suppresses respawns).
    Bucket buckets[5] = { {"0 enemies"}, {"1 enemy"}, {"2 enemies"}, {"3 enemies"}, {"boss"} };

    GFXcanvas16 canvas(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);
    FrameDumper dumper("tank");
    AudioEngine audio;
    TankFluxGame g;
    g.init(audio);

    if (strcmp(mode, "idle") == 0) {
        // Only the attract cycle runs; count demos and how each ended, and
        // any sound a demo made that wasn't silenced.
        InputState none{};
        int demos = 0, destroyed = 0, timedOut = 0, maxKills = 0, bosses = 0;
        int audible = 0;
        bool was = false, bossWas = false;
        long demoFrames = 0;
        for (long f = 0; f < frames; ++f) {
            const int heard = audio.tones + audio.melodies + audio.wavs;
            const float health = g._health;
            g.update(canvas, none, audio);
            const bool now = g.inDemo();
            if (now) {
                ++demoFrames;
                audible += audio.tones + audio.melodies + audio.wavs - heard;
                if (g._kills > maxKills) maxKills = g._kills;
                if (g._bossActive && !bossWas) ++bosses;
            }
            bossWas = now && g._bossActive;
            if (!was && now) ++demos;
            if (was && !now) (health <= 0 || g._health <= 0 || health < 25 ? ++destroyed : ++timedOut);
            was = now;
            g_fakeMillis += stepMs;
        }
        printf("DONE idle frames=%ld demos=%d destroyed=%d timedout=%d avgDemoS=%.1f maxKills=%d bosses=%d "
               "audibleInDemo=%d silenced=%d\n",
               frames, demos, destroyed, timedOut, demos ? demoFrames * stepMs / 1000.0 / demos : 0.0,
               maxKills, bosses, audible, audio.silencedCalls);
        g.onExit();
        return audible == 0 ? 0 : 1;
    }

    if (strcmp(mode, "demoexit") == 0) {
        InputState none{};
        for (long f = 0; f < 20000 && !g.inDemo(); ++f) { g.update(canvas, none, audio); g_fakeMillis += stepMs; }
        for (int i = 0; i < 900; ++i) { g.update(canvas, none, audio); g_fakeMillis += stepMs; }
        printf("demo at level %d, kills %d, score %d, health %d\n", g._level, g._kills, g._score, (int)g._health);
        InputState press{}; press.btnA = true; press.btnAPressed = true;
        g.update(canvas, press, audio);
        g_fakeMillis += stepMs;
        int visible = 0;
        for (auto &e : g._enemies) visible += e.alive;
        visible += g._boss.alive + g._bossActive + g._bossPending;
        for (auto &s : g._enemyShells) visible += s.active;
        bool ok = g._phase == TankFluxGame::PHASE_PLAYING && g._level == 1 && g._kills == 0 &&
                  g._score == 0 && g._health == tankflux::HEALTH_MAX && visible == 0 && !g.inDemo();
        printf("after A: phase %d level %d kills %d score %d health %d leftovers %d -> %s\n",
               (int)g._phase, g._level, g._kills, g._score, (int)g._health, visible, ok ? "PASS" : "FAIL");
        g.onExit();
        return ok ? 0 : 1;
    }

    bool prevA = false, prevB = false, wasBoss = false;
    int bossesSeen = 0, quits = 0, gameOvers = 0, lastPhase = -1;
    // Boss difficulty: how long fights last and how much they hurt (god
    // mode pins health each frame, so the drop since the pin is this
    // frame's damage).
    long bossFrames = 0, bossDamage = 0;
    uint32_t traceHash = 2166136261u;

    for (long f = 0; f < frames; ++f) {
        InputState in{};
        bool a = false, b = false;

        if (g._phase != TankFluxGame::PHASE_PLAYING) {
            a = (f % 40) == 0;   // press A to start / restart
        } else {
            if (god) g._health = tankflux::HEALTH_MAX;

            // Turn towards the nearest target (the boss if there is one),
            // hold a mid-range distance, and fire when roughly lined up.
            float bx = 0, bz = 0, bd = 1e30f;
            bool have = false;
            for (auto &e : g._enemies) {
                if (!e.alive) continue;
                float d = (e.x - g._x) * (e.x - g._x) + (e.z - g._z) * (e.z - g._z);
                if (d < bd) { bd = d; bx = e.x; bz = e.z; have = true; }
            }
            if (g._bossActive) { bx = g._boss.x; bz = g._boss.z; have = true; }
            if (have) {
                float want = degrees(atan2f(bx - g._x, bz - g._z));
                float err = want - g._headingDeg;
                while (err >  180) err -= 360;
                while (err < -180) err += 360;
                in.joyY = constrain(err / 8.0f, -1.0f, 1.0f);
                float dist = sqrtf((bx - g._x) * (bx - g._x) + (bz - g._z) * (bz - g._z));
                in.joyX = dist > 1400 ? -1.0f : (dist < 700 ? 0.7f : 0.0f);
                if (fabsf(err) < 3.0f && (f % 6) == 0) a = true;
            }
            // Strafe now and then, so hold-B is covered.
            long cycle = f % 600;
            if (cycle >= 300 && cycle < 360) { b = true; in.joyX = (f / 600) % 2 ? 1.0f : -1.0f; }
        }

        // Back (main.cpp's): from the attract screen, then mid-game.
        const bool backQuit = menus && (f == 200 || f == 700);
        if (menus) {
            if (f < 300)                 { a = false; b = false; }
            else if (f > 2000 && g._phase == TankFluxGame::PHASE_GAMEOVER) { a = false; b = false; }
        }

        in.btnA = a; in.btnB = b;
        in.btnAPressed = a && !prevA;
        in.btnBPressed = b && !prevB;
        prevA = a; prevB = b;

        auto t0 = std::chrono::steady_clock::now();
        bool keepRunning = g.update(canvas, in, audio);
        auto t1 = std::chrono::steady_clock::now();
        dumper.maybeDump(f, canvas);

        if (profile && g._phase == TankFluxGame::PHASE_PLAYING && g._scene) {
            int objs = 0, tris = 0, verts = 0;
            g._scene->getStatistics(objs, tris, verts);
            int alive = 0;
            for (const auto &e : g._enemies) if (e.alive) ++alive;
            int b = g._bossActive ? 4 : alive;
            buckets[b].add(std::chrono::duration<double, std::micro>(t1 - t0).count(),
                           *g._scene, objs, tris);
        }

        if (!keepRunning || backQuit) {
            // What main.cpp's returnToLauncher() + launchGame() do.
            if (backQuit) g.onQuit(audio);
            ++quits;
            printf("quit at f=%ld phase=%d\n", f, (int)g._phase);
            g.onExit();
            g.init(audio);
        }

        if (g._bossActive || wasBoss) {
            ++bossFrames;
            if (god && g._phase == TankFluxGame::PHASE_PLAYING && g._health < tankflux::HEALTH_MAX)
                bossDamage += tankflux::HEALTH_MAX - g._health;
        }
        if (g._bossActive && !wasBoss) ++bossesSeen;
        wasBoss = g._bossActive;
        if (g._phase == TankFluxGame::PHASE_GAMEOVER && lastPhase != TankFluxGame::PHASE_GAMEOVER) ++gameOvers;
        lastPhase = g._phase;

        int32_t st[12] = { (int32_t)g._phase, g._score, g._health, g._kills, g._level,
                           (int32_t)g._x, (int32_t)g._z, (int32_t)g._headingDeg,
                           g._bossActive, g._bossActive ? g._boss.hp : -1,
                           (int32_t)g._bossPending, g._bossesDefeated };
        traceHash = fnv(st, sizeof(st), traceHash);
        // Hashing every frame's pixels would dominate the runtime; once a
        // second is enough to catch a rendering change.
        if (f % 60 == 0) {
            traceHash = fnv(canvas.getBuffer(),
                            ArcadeConfig::LANDSCAPE_WIDTH * ArcadeConfig::LANDSCAPE_HEIGHT * 2,
                            traceHash);
        }
        if (f % 2000 == 0) {
            printf("f=%6ld ph=%d sc=%6d hp=%3d k=%3d lv=%d pos=(%6d,%6d) boss=%d/%d hash=%08x\n",
                   f, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[8], st[9], traceHash);
        }

        g_fakeMillis += stepMs;
    }

    printf("DONE frames=%ld bosses=%d defeated=%d gameovers=%d quits=%d "
           "tones=%d melodies=%d wavs=%d final=%08x\n",
           frames, bossesSeen, g._bossesDefeated, gameOvers, quits,
           audio.tones, audio.melodies, audio.wavs, traceHash);
    if (bossesSeen)
        printf("boss fights: %ld frames each on average, %ld damage taken per fight%s\n",
               bossFrames / bossesSeen, bossDamage / bossesSeen, god ? "" : " (god mode only)");

    if (profile) {
        // Host microseconds are not ESP32 microseconds; compare buckets to
        // each other, not to the 16.6ms frame budget. The scene totals are
        // the drift check: they must not grow as bosses are defeated, since
        // an arena reset only moves existing objects.
        printf("\non screen      frames  drawnTris   rastTris   update_us"
               "    sceneObjs / sceneTris\n");
        for (const auto &b : buckets) b.report();
    }
    return 0;
}
