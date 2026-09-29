// Host-side regression harness for Star Flux. Same approach as the Tank
// and Tube Flux ones (see README.md): real game logic and real Jet, fake
// clock, seeded RNG, scripted bot, hashed trace.

#include "harness_common.h"
#include <chrono>

#define private public
#include "games/StarFlux/StarFluxGame.h"
#undef private

using namespace starflux;

namespace {

void dump(const char* name, GFXcanvas16 &canvas) {
    FrameDumper d(name);
    d.n = 1; d.at[0] = 0;
    d.maybeDump(0, canvas);
}

// Renders fixed set-ups and dumps them, to check conventions by eye: which
// way Jet rolls the camera and turns objects relative to the game's
// angles, that rocks aren't inside out, and how the boss reads.
void poses(StarFluxGame &g, GFXcanvas16 &canvas, AudioEngine &audio) {
    InputState in{};
    g.update(canvas, in, audio);   // builds the scene
    g.startNewGame(audio);
    g._stage = StarFluxGame::STAGE_RUN;
    g._shipX = 0; g._shipY = 0; g._bank = 0;
    g._invulnUntil = 0;

    auto frame = [&](const char* name) {
        g.updateShipSprite();
        g.renderWorld(canvas);
        g.drawHUD(canvas);
        dump(name, canvas);
    };
    auto fighter = [&](int i, float x, float y, float z, int pitch, int yaw, int roll) {
        auto &f = g._fighters[i];
        f.active = true; f.startAt = 0; f.x = x; f.y = y; f.z = z;
        f.obj->enabled = true;
        f.obj->setPosition((int32_t)x, (int32_t)y, (int32_t)z);
        f.obj->setRotation(pitch, yaw, roll);
    };

    // Fighters: nose away (tail on), yawed +45, pitched +30, rolled +30.
    fighter(0, -450, 150, 1800, 0, 0, 0);
    fighter(1, -150, 150, 1800, 0, 45, 0);
    fighter(2, 150, 150, 1800, 30, 0, 0);
    fighter(3, 450, 150, 1800, 0, 0, 30);
    fighter(4, 0, -150, 1400, 0, 180, 0);   // nose towards you
    frame("pose_fighters");
    // Close up: yaw +60 (should face right), pitch +40, roll +40.
    for (auto &f : g._fighters) { f.active = false; f.obj->enabled = false; }
    fighter(0, -260, 200, 1000, 0, 60, 0);
    fighter(1, 0, 200, 1000, 40, 0, 0);
    fighter(2, 260, 200, 1000, 0, 0, 40);
    frame("pose_fighters_close");
    for (auto &f : g._fighters) { f.active = false; f.obj->enabled = false; }

    // Rocks: one of each size, and a shot and enemy shot in flight.
    auto &r0 = g._rocks[0];
    r0.active = true; r0.x = -250; r0.y = 100; r0.z = 1500; r0.obj->enabled = true;
    auto &r1 = g._rocks[5];
    r1.active = true; r1.x = 250; r1.y = 100; r1.z = 1200; r1.obj->enabled = true;
    for (auto* r : { &r0, &r1 }) {
        r->obj->setPosition((int32_t)r->x, (int32_t)r->y, (int32_t)r->z);
        r->obj->setRotation(20, 30, 0);
    }
    g._shots[0] = { true, 0, 0, 1400, 1400 };
    g._eshots[0] = { true, 100, -50, 1000, 0, 0, -30 };
    g._rings[0] = { true, false, -150, 0, 2400 };
    frame("pose_rocks");

    // Banked right, then left: the world should tilt with the ship.
    g._bank = 1.0f; g._shipX = 200;
    g.updateShipSprite();
    g.renderWorld(canvas);
    // project() must agree with Jet under roll: a cross on each rock's centre.
    for (auto* r : { &r0, &r1 }) {
        float sx, sy;
        g.project(r->x, r->y, r->z, sx, sy);
        canvas.drawFastHLine((int)sx - 3, (int)sy, 7, 0xF81F);
        canvas.drawFastVLine((int)sx, (int)sy - 3, 7, 0xF81F);
    }
    dump("pose_bank_right", canvas);
    g._bank = -0.5f; g._shipX = -200;
    frame("pose_bank_left");
    for (auto &r : g._rocks) { r.active = false; r.obj->enabled = false; }
    g._shots[0].active = false; g._eshots[0].active = false; g._rings[0].active = false;

    // The boss, whole, then with a cannon gone and the core open.
    g._bank = 0; g._shipX = 0;
    g._segSpawned = 0;
    g.startBoss();
    g._bossAt = g_fakeMillis - BOSS_ENTER_MS - 10;
    g._bossZ = BOSS_Z; g._bossX = 0; g._bossY = BOSS_BASE_Y;
    g.placeBoss();
    frame("pose_boss");
    g._cannonHp[0] = 0; g._cannonHp[1] = 0;
    g.placeBoss();
    g.addBlast(0, 0, 1500, 300, ArcadeConfig::COLOR_ORANGE);
    frame("pose_boss_open");
    g.hideBoss();

    // Titles and menus.
    g.enterAttract();
    g.renderAttractTitle(canvas);
    dump("pose_title", canvas);
}

}  // namespace

// Scenarios:
//   play     the autopilot plays; now and then it misjudges, so deaths and game over are covered
//   god      shield pinned: every segment and the boss, loop after loop
//   menus    attract exit, in-game hold-B quit (a tap is a bomb), game-over timeout
//   profile  god, plus per-frame render cost by segment
//   pose     renders fixed set-ups to pose_*.ppm (see poses())
//   idle     no input at all: the attract cycle (title, how-to-play, demo)
//   demoexit press A mid-demo: the real game must start clean (prints PASS/FAIL)
int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "play";
    const bool profile = strcmp(mode, "profile") == 0;
    const bool god   = strcmp(mode, "god") == 0 || profile;
    const bool menus = strcmp(mode, "menus") == 0;
    const bool idle  = strcmp(mode, "idle") == 0;
    const long frames = argc > 2 ? atol(argv[2]) : 20000;
    const unsigned long stepMs = argc > 3 ? (unsigned long)atol(argv[3]) : 33;

    GFXcanvas16 canvas(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);
    FrameDumper dumper("star");
    AudioEngine audio;
    StarFluxGame g;
    g.init(audio);

    if (strcmp(mode, "pose") == 0) {
        poses(g, canvas, audio);
        g.onExit();
        return 0;
    }

    if (strcmp(mode, "demoexit") == 0) {
        InputState none{};
        long f = 0;
        for (; f < 20000 && !g.inDemo(); ++f) { g.update(canvas, none, audio); g_fakeMillis += 33; }
        for (int i = 0; i < 300; ++i) { g.update(canvas, none, audio); g_fakeMillis += 33; }
        printf("demo at segment %d, score %ld, lives %d, boss %d\n", g._seg, g._score, g._lives, (int)g._bossActive);
        InputState press{}; press.btnA = true; press.btnAPressed = true;
        g.update(canvas, press, audio);
        g_fakeMillis += 33;
        int visible = 0, live = 0;
        for (auto &fi : g._fighters) visible += fi.obj->enabled, live += fi.active;
        for (auto &r : g._rocks) visible += r.obj->enabled, live += r.active;
        for (auto &e : g._eshots) live += e.active;
        for (auto &s : g._shots) live += s.active;
        for (auto &r : g._rings) live += r.active;
        visible += g._bossHull->enabled + g._coreObj->enabled + g._shieldObj->enabled;
        bool ok = g._phase == StarFluxGame::PHASE_PLAYING && g._stage == StarFluxGame::STAGE_INTRO &&
                  g._seg == 0 && g._score == 0 && g._lives == LIVES && g._shield == SHIELD_MAX &&
                  g._loop == 1 && !g._silent && !g._bossActive && !g._bombActive && visible == 0 && live == 0;
        printf("after A: phase %d stage %d seg %d score %ld lives %d shield %d silent %d leftovers %d/%d -> %s\n",
               (int)g._phase, (int)g._stage, g._seg, g._score, g._lives, g._shield, (int)g._silent,
               visible, live, ok ? "PASS" : "FAIL");
        g.onExit();
        return ok ? 0 : 1;
    }

    struct SegCost { long frames = 0; double us = 0, drawn = 0; int objs = 0, tris = 0; };
    SegCost cost[12];

    bool prevB = false;
    int quits = 0, gameOvers = 0, stagesCleared = 0, lastPhase = -1, lastLives = LIVES, livesLost = 0;
    int bombsUsed = 0, lastBombs = BOMBS_START, maxLoop = 1, bossesSeen = 0, demosStarted = 0;
    bool wasBoss = false, wasDemo = false;
    int hits = 0, lastShield = SHIELD_MAX, eshotsFired = 0;
    bool eshotWas[ESHOT_POOL] = {};
    long totalDowned = 0, totalSeen = 0;
    uint32_t traceHash = 2166136261u;

    for (long f = 0; f < frames; ++f) {
        InputState in{};
        bool b = false;

        if (g._phase == StarFluxGame::PHASE_PLAYING) {
            if (god) g._shield = SHIELD_MAX;
            // The play bot reacts slowly and has no bombs, so it gets hit
            // and loses lives; god mode's reacts every frame.
            in = god ? g.pilot(true, 0) : g.pilot(false, 500);
            b = in.btnB;
        } else {
            in.btnA = (f % 40) == 0;
        }
        if (menus) {
            if (f < 300)                  { in.btnA = false; b = (f >= 20 && f < 200); }   // attract: hold B to exit
            else if (f == 300)            { in.btnA = true; }                              // start
            else if (f >= 400 && f < 403) { b = true; }                                    // playing: tap B, a bomb
            else if (f >= 600 && f < 700) { b = true; }                                    // playing: hold B to quit
            else if (f == 900 && g._phase == StarFluxGame::PHASE_PLAYING) {        // last life lost: game over
                g._lives = 1; g._invulnUntil = 0; g._stage = StarFluxGame::STAGE_RUN;
                g.damageShip(SHIELD_MAX, audio);
            }
        }
        if (idle) { in = InputState{}; b = false; }
        in.btnB = b;
        in.btnAPressed = in.btnA;
        in.btnBPressed = b && !prevB;
        prevB = b;

        auto t0 = std::chrono::steady_clock::now();
        bool keepRunning = g.update(canvas, in, audio);
        auto t1 = std::chrono::steady_clock::now();
        dumper.maybeDump(f, canvas);

        if (profile && g._phase == StarFluxGame::PHASE_PLAYING && g._scene) {
            SegCost &c = cost[g._seg];
            int objs = 0, tris = 0, verts = 0;
            g._scene->getStatistics(objs, tris, verts);
            ++c.frames;
            c.us += std::chrono::duration<double, std::micro>(t1 - t0).count();
            c.drawn += g._scene->lastFrameDrawnTriangles;
            c.objs = objs; c.tris = tris;
        }

        if (!keepRunning) {
            ++quits;
            printf("quit at f=%ld phase=%d\n", f, (int)g._phase);
            g.onExit();
            g.init(audio);
        }
        if (g._phase == StarFluxGame::PHASE_PLAYING) {
            if (g._lives < lastLives) ++livesLost;
            if (g._bombs < lastBombs) ++bombsUsed;
        }
        if (g._phase == StarFluxGame::PHASE_PLAYING && g._shield < lastShield) ++hits;
        lastShield = g._shield;
        for (int i = 0; i < ESHOT_POOL; ++i) {
            if (g._eshots[i].active && !eshotWas[i]) ++eshotsFired;
            eshotWas[i] = g._eshots[i].active;
        }
        lastLives = g._lives;
        lastBombs = g._bombs;
        if (g._bossActive && !wasBoss) ++bossesSeen;
        wasBoss = g._bossActive;
        if (g.inDemo() && !wasDemo) ++demosStarted;
        wasDemo = g.inDemo();
        if (g._phase == StarFluxGame::PHASE_GAMEOVER && lastPhase != StarFluxGame::PHASE_GAMEOVER) ++gameOvers;
        if (g._phase == StarFluxGame::PHASE_RESULTS && lastPhase != StarFluxGame::PHASE_RESULTS) {
            ++stagesCleared;
            totalDowned += g._fightersDowned; totalSeen += g._fightersSeen;
            printf("stage clear f=%ld loop=%d downed %d/%d rocks %d rings %d shield %d score %ld\n",
                   f, g._loop, g._fightersDowned, g._fightersSeen, g._rocksDowned, g._ringsCaught, g._shield, g._score);
        }
        lastPhase = g._phase;
        if (g._loop > maxLoop) maxLoop = g._loop;

        int32_t st[10] = { (int32_t)g._phase, (int32_t)g._score, g._shield, g._lives, g._seg,
                           (int32_t)g._stage, (int32_t)g._shipX, (int32_t)g._shipY, g._bombs, g.bossHp() };
        traceHash = fnv(st, sizeof(st), traceHash);
        if (f % 60 == 0) {
            traceHash = fnv(canvas.getBuffer(),
                            ArcadeConfig::LANDSCAPE_WIDTH * ArcadeConfig::LANDSCAPE_HEIGHT * 2, traceHash);
        }
        if (f % 2000 == 0) {
            printf("f=%6ld ph=%d st=%d seg=%d sc=%7ld sh=%3d lives=%d bombs=%d loop=%d hash=%08x\n",
                   f, (int)g._phase, (int)g._stage, g._seg, g._score, g._shield, g._lives, g._bombs, g._loop, traceHash);
        }
        g_fakeMillis += stepMs;
    }

    printf("DONE frames=%ld stages=%d maxLoop=%d livesLost=%d gameovers=%d quits=%d bombs=%d bosses=%d "
           "demos=%d high=%ld final=%08x\n",
           frames, stagesCleared, maxLoop, livesLost, gameOvers, quits, bombsUsed, bossesSeen,
           demosStarted, g._highScore, traceHash);
    printf("hits taken=%d, enemy shots fired=%d\n", hits, eshotsFired);
    if (totalSeen) printf("fighters downed %ld/%ld (%.0f%%)\n", totalDowned, totalSeen, 100.0 * totalDowned / totalSeen);

    if (profile) {
        // Relative only: host microseconds are not ESP32 microseconds.
        printf("\nseg  frames  drawnTris  update_us   sceneObjs/sceneTris\n");
        for (int s = 0; s < 12; ++s) {
            const SegCost &c = cost[s];
            if (!c.frames) continue;
            printf("%3d %7ld %10.0f %10.1f   %d/%d\n", s, c.frames, c.drawn / c.frames, c.us / c.frames, c.objs, c.tris);
        }
    }
    return 0;
}
