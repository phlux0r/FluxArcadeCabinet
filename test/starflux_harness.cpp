// Host-side regression harness for Star Flux. Same approach as the Tank
// and Tube Flux ones (see README.md): real game logic and real Jet, fake
// clock, seeded RNG, scripted bot, hashed trace.

#include "harness_common.h"
#include <algorithm>
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

    // A loop-5 V of nine, far off and then closed up mid-dive.
    {
        g._loop = 5; g._seg = 0; g._segSpawned = 0; g._segAt = g_fakeMillis; g._waveLeft[0] = 0;
        g.spawnWaveFighters();
        for (unsigned long t : { 400UL, 3000UL }) {
            for (auto &f : g._fighters) {
                if (!f.active) continue;
                const long rel = (long)t - (long)(f.startAt - g._segAt - 400);
                g.pathPoint(f, (unsigned long)std::max(0L, rel), f.x, f.y, f.z);
                f.obj->enabled = true;
                f.obj->setPosition((int32_t)f.x, (int32_t)f.y, (int32_t)f.z);
                f.obj->setRotation(0, 180, 0);
            }
            frame(t < 1000 ? "pose_vdive_far" : "pose_vdive_close");
        }
        for (auto &f : g._fighters) { f.active = false; f.obj->enabled = false; }
        g._loop = 1;
    }

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
    g._eshots[0] = { true, false, false, 100, -50, 1000, 0, 0, -30 };
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

    // Stages 2-4: the ground with pillars, a tower and an arch; the trench
    // with a barrier, a gate and a tower; the canyon with icicles, an arch,
    // pillars and mines; then their bosses, whole and with the core open
    // (the reactor's fan showing, the walker's frost shards in flight).
    static const char* const scene[4] = { "pose_planet", "pose_trench", "pose_canyon", "pose_mother" };
    static const char* const boss[4] = { "pose_boss_crawler", "pose_boss_reactor", "pose_boss_walker", "pose_boss_mother" };
    static const char* const bossOpen[4] = { "pose_boss_crawler_open", "pose_boss_reactor_open", "pose_boss_walker_open",
                                             "pose_boss_mother_open" };
    for (int st = 1; st <= 4; ++st) {
        g._stageNum = st;
        g.applyStagePalette();
        g.hideWorld();
        g._shipX = 120; g._shipY = -60; g._bank = 0.4f;
        g_fakeMillis += 1000;
        if (st == 1) {
            g.spawnBox(-420, -240, GROUND_Y, 900, 2600, 180);
            g.spawnTower(200, 1900, true);
            g.spawnBox(-150, -30, GROUND_Y, 420, 4200, 150);
            g.spawnBox(330, 450, GROUND_Y, 420, 4200, 150);
            g.spawnBox(-150, 450, 170, 420, 4200, 150);
        } else if (st == 4) {
            g.spawnTower(-250, 1800, true);
            g.spawnDoors(60, 3000);
            for (auto &b : g._boxes) if (b.active && b.slide != 0) b.phase = DOOR_CYCLE_MS / 4;   // half shut
            g.spawnBox(250, 350, HULL_Y, 500, 4400, 100);
            auto* gate = g.spawnGate(-50, 150, 5600);
            gate->phase = 0;
            g_fakeMillis = (g_fakeMillis / (GATE_MS * 2)) * GATE_MS * 2 + 10;   // on
            g._shipX = 40; g._shipY = 0; g._bank = 0.1f;
        } else if (st == 3) {
            const float hw = g.canyonHalfW(BRIDGE_Y);
            g.spawnBox(-hw, hw, BRIDGE_Y, BRIDGE_Y + 150, 3200, 170);
            for (float x : { -300.0f, -80.0f, 280.0f }) g.spawnBox(x - 45, x + 45, BRIDGE_Y - 470, BRIDGE_Y, 3200, 90);
            g.spawnBox(-g.canyonHalfW(0), g.canyonHalfW(0), -60, 60, 5200, 170);
            g.spawnBox(-380, -200, CANYON_FLOOR, 600, 1900, 180);
            g.spawnBox(160, 380, CANYON_FLOOR, -80, 2200, 220);
            for (auto &r : g._rocks) { r.active = false; r.obj->enabled = false; }
            g.spawnMine(); g._rocks[ROCK_BIG_SLOTS].x = -120; g._rocks[ROCK_BIG_SLOTS].y = 60; g._rocks[ROCK_BIG_SLOTS].z = 1500;
            g.spawnMine(); g._rocks[ROCK_BIG_SLOTS + 1].x = 260; g._rocks[ROCK_BIG_SLOTS + 1].y = 180; g._rocks[ROCK_BIG_SLOTS + 1].z = 2600;
        } else {
            g.spawnBox(-TRENCH_HALF_W, -100, TRENCH_FLOOR, TRENCH_TOP, 3000, 160);
            g.spawnBox(200, TRENCH_HALF_W, TRENCH_FLOOR, TRENCH_TOP, 3000, 160);
            auto* gate = g.spawnGate(-50, 150, 1800);
            gate->phase = 0;
            g_fakeMillis = (g_fakeMillis / (GATE_MS * 2)) * GATE_MS * 2 + 10;   // on
            g.spawnTower(-200, 4500, true);
        }
        for (auto &t : g._turrets) if (t.active) { t.obj->setPosition((int32_t)t.x, (int32_t)t.y, (int32_t)t.z); }
        if (st == 1) {   // the rapid-fire pod, and its HUD mark
            g._rapid = true;
            g._pod.active = true; g._pod.x = -80; g._pod.y = 120; g._pod.z = 2200;
        }
        frame(scene[st - 1]);
        for (auto &r : g._rocks) r.active = false;
        g._rapid = false; g._pod.active = false;
        if (st == 2) {   // the barrier nearest: outlined, the marker red, then green in the gap
            for (auto &b : g._boxes) if (b.gate) b.active = false;
            g._shipX = 280;
            g_fakeMillis = (g_fakeMillis / 240) * 240;   // the red marker's blink: on
            frame("pose_trench_hit");
            g._shipX = 50; g._shipY = 0;
            frame("pose_trench_clear");
        }
        g.hideWorld();
        g._shipX = 0; g._shipY = 0; g._bank = 0;
        g.startBoss();
        g._bossAt = g_fakeMillis - BOSS_ENTER_MS - 10;
        g._bossZ = g.bossZ();
        g.placeBoss();
        frame(boss[st - 1]);
        g._cannonHp[0] = g._cannonHp[1] = 0;
        g._fanAngle = 30;
        g.placeBoss();
        if (st == 3) {   // a ring of frost shards on its way
            float cx, cy, cz;
            g.bossPartPos(2, cx, cy, cz);
            g.ringBurst(cx, cy, cz, true);
            for (auto &e : g._eshots) if (e.active) { e.x += e.vx * 20; e.y += e.vy * 20; e.z += e.vz * 20; }
        }
        frame(bossOpen[st - 1]);
        for (auto &e : g._eshots) e.active = false;
        g.hideBoss();
    }
    g._stageNum = 0;

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
//   profile  god, plus per-frame render cost by stage and segment
//   pose     renders fixed set-ups to pose_*.ppm (see poses())
//   idle     no input at all: the attract cycle (title, how-to-play, demo)
//   demoexit press A mid-demo: the real game must start clean (prints PASS/FAIL)
//   loops    extra lives, shield overcharge, difficulty by loop, reachable rings (PASS/FAIL)
//   rapid    the rapid-fire pod (loop 2 on) and the flight-aid marker (PASS/FAIL)
int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "play";
    const bool profile = strcmp(mode, "profile") == 0;
    // passive: shield pinned, and the bot never fires: nothing gets shot
    // down, so every wave has to leave on its own and the stage still move on.
    const bool passive = strcmp(mode, "passive") == 0;
    const bool god   = strcmp(mode, "god") == 0 || profile || passive;
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
        printf("demo at stage %d segment %d, score %ld, lives %d, boss %d\n", g._stageNum + 1, g._seg, g._score, g._lives,
               (int)g._bossActive);
        InputState press{}; press.btnA = true; press.btnAPressed = true;
        g.update(canvas, press, audio);
        g_fakeMillis += 33;
        int visible = 0, live = 0;
        for (auto &fi : g._fighters) visible += fi.obj->enabled, live += fi.active;
        for (auto &r : g._rocks) visible += r.obj->enabled, live += r.active;
        for (auto &e : g._eshots) live += e.active;
        for (auto &s : g._shots) live += s.active;
        for (auto &r : g._rings) live += r.active;
        for (auto* h : g._bossHulls) visible += h->enabled;
        visible += g._coreObj->enabled + g._shieldObj->enabled;
        for (auto &b : g._boxes) visible += b.obj->enabled, live += b.active;
        for (auto &t : g._turrets) visible += t.obj->enabled, live += t.active;
        bool ok = g._phase == StarFluxGame::PHASE_PLAYING && g._stage == StarFluxGame::STAGE_INTRO &&
                  g._seg == 0 && g._stageNum == 0 && g._score == 0 && g._lives == LIVES && g._shield == SHIELD_MAX &&
                  g._loop == 1 && !g._silent && !g._bossActive && !g._bombActive && visible == 0 && live == 0;
        printf("after A: phase %d stage %d seg %d score %ld lives %d shield %d silent %d leftovers %d/%d -> %s\n",
               (int)g._phase, (int)g._stage, g._seg, g._score, g._lives, g._shield, (int)g._silent,
               visible, live, ok ? "PASS" : "FAIL");
        g.onExit();
        return ok ? 0 : 1;
    }

    if (strcmp(mode, "loops") == 0) {
        // Extra lives, shield overcharge, difficulty by loop, and rings
        // that can be reached.
        bool ok = true;
        auto check = [&](bool c, const char* what) { printf("%-52s %s\n", what, c ? "PASS" : "FAIL"); ok = ok && c; };
        auto step = [&](const InputState &in) { g.update(canvas, in, audio); g_fakeMillis += 33; };
        InputState none{};
        for (long f = 0; f < 3000 && g._phase != StarFluxGame::PHASE_PLAYING; ++f) {
            InputState in{}; in.btnA = in.btnAPressed = (f & 1); step(in);
        }
        for (int i = 0; i < 200; ++i) step(none);
        // Extra lives at 75k, 125k, 175k...
        const int lives0 = g._lives;
        g._score = EXTRA_LIFE_FIRST - 10; step(none);
        check(g._lives == lives0, "no life before 75k");
        g._score = EXTRA_LIFE_FIRST; step(none);
        check(g._lives == lives0 + 1 && g._nextLifeAt == EXTRA_LIFE_FIRST + EXTRA_LIFE_EVERY, "a life at 75k, next at 125k");
        g._score = EXTRA_LIFE_FIRST + 2 * EXTRA_LIFE_EVERY; step(none); step(none);
        check(g._lives == lives0 + 3, "125k and 175k: two more");
        g._lives = LIVES_MAX; g._score = 1000000; step(none);
        check(g._lives == LIVES_MAX, "never more than LIVES_MAX");
        g._lives = 3;
        // Overcharge: rings past full, to 3x; carried into the next stage; a
        // lost life resets it.
        g._shield = SHIELD_MAX;
        int got = 0;
        for (int i = 0; i < 8; ++i) {
            auto &r = g._rings[0];
            r.active = true; r.resolved = false; r.x = g._shipX; r.y = g._shipY; r.z = SHIP_Z + 10.0f;
            g._invulnUntil = g_fakeMillis + 100000; step(none); ++got;
        }
        check(g._shield == SHIELD_CAP, "8 rings from full: capped at 3x");
        g._invulnUntil = 0; g._stage = StarFluxGame::STAGE_RUN;
        g.damageShip(30, audio);
        check(g._shield == SHIELD_CAP - 30, "damage comes off the overcharge first");
        g.startStage();
        check(g._shield == SHIELD_CAP - 30, "overcharge carried into the next stage");
        g._stage = StarFluxGame::STAGE_RUN; g._invulnUntil = 0;
        g.damageShip(SHIELD_CAP, audio);
        for (int i = 0; i < 80; ++i) step(none);
        check(g._shield == SHIELD_MAX, "a lost life: back to plain full");
        // Difficulty by loop, capped at LOOP_CAP.
        int waves[7], pace[7];
        for (int l = 1; l <= 6; ++l) {
            g._loop = l; waves[l] = g.waveCount(6); pace[l] = (int)g.bossMs(1000);
        }
        printf("wave of 6 by loop: %d %d %d %d %d %d; boss 1000ms: %d %d %d %d %d %d\n", waves[1], waves[2], waves[3],
               waves[4], waves[5], waves[6], pace[1], pace[2], pace[3], pace[4], pace[5], pace[6]);
        check(waves[1] == 6 && waves[2] == 7 && waves[4] == 9 && waves[6] == 9, "waves grow by one a loop, to 9");
        check(pace[1] == 1000 && pace[5] < pace[2] && pace[6] == pace[5], "boss quicker each loop, capped");
        g._loop = 2; const int b2 = g.burstPct(); g._loop = 3; const int b3 = g.burstPct();
        check(b2 == 0 && b3 == BURST_PCT, "pairs of shots from loop 3");
        g._loop = 1;
        // Rings on the planet and in the trench: none may sit inside an
        // obstacle, however dense the field (loop 5).
        int rings = 0, blocked = 0;
        for (int st = 1; st <= 2; ++st) {
            g._stageNum = st; g._loop = 5;
            g.startStage();
            for (int i = 0; i < 200; ++i) step(none);
            for (int seg = 0; seg < 9; ++seg) {
                if (g.segment().type == StarFluxGame::SEG_BOSS) continue;
                g.startSegment(seg);
                g._segRingDone = false;   // a ring in every segment
                bool was[RING_POOL] = {};
                for (int f = 0; f < 300; ++f) {
                    g._shield = SHIELD_MAX;
                    step(none);
                    for (int k = 0; k < RING_POOL; ++k) {
                        auto &r = g._rings[k];
                        if (r.active && !r.resolved && r.z - SHIP_Z < 60.0f && !was[k]) {
                            was[k] = true; ++rings;
                            for (auto &b : g._boxes) {
                                if (!b.active || fabsf(b.z - r.z) > b.depth * 0.5f + 60.0f) continue;
                                float nx = std::max(b.x0, std::min(r.x, b.x1)), ny = std::max(b.y0, std::min(r.y, b.y1));
                                float dx = r.x - nx, dy = r.y - ny, rr = SHIP_HIT_R * 0.7f;
                                if (dx * dx + dy * dy < rr * rr) { ++blocked; break; }
                            }
                        }
                        if (!r.active) was[k] = false;
                    }
                }
            }
        }
        // Directly: a wall with a narrow gap right where a ring appears; the
        // ring must land in the gap (or wait), and no new obstacle may be
        // put down on top of it.
        int placed = 0, inWall = 0;
        for (int i = 0; i < 200; ++i) {
            for (auto &b : g._boxes) { b.active = false; if (b.obj) b.obj->enabled = false; }
            for (auto &r : g._rings) r.active = false;
            g.spawnBox(-TRENCH_HALF_W, -60.0f, TRENCH_FLOOR, TRENCH_TOP, ROCK_SPAWN_Z, 160.0f);
            g.spawnBox(220.0f, TRENCH_HALF_W, TRENCH_FLOOR, TRENCH_TOP, ROCK_SPAWN_Z, 160.0f);
            if (!g.spawnRing()) continue;
            ++placed;
            const auto &r = g._rings[0];
            if (r.x < -60.0f + SHIP_HIT_R * 0.7f || r.x > 220.0f - SHIP_HIT_R * 0.7f) ++inWall;
        }
        g._rings[0].active = true; g._rings[0].resolved = false; g._rings[0].z = BOX_SPAWN_Z - 300.0f;
        const bool heldBack = g.pickupNear(BOX_SPAWN_Z);
        printf("ring by a gapped wall: %d placed of 200 tries, %d in the wall\n", placed, inWall);
        check(placed > 0 && inWall == 0 && heldBack, "rings go in the gap; fields hold back near one");
        printf("rings on stages 2-3 at loop 5: %d, inside an obstacle: %d\n", rings, blocked);
        // Every wave of a stage perfect: the bonus at the results; one short: none.
        g._stageNum = StarFluxGame::STAGE_BELT; g._loop = 1;
        g.startStage();
        const int nWaves = g.wavesInStage();
        g._wavesPerfect = nWaves; g._shield = 0;
        long before = g._score;
        g.enterResults(audio);
        const long allGot = g._score - before;
        g.startStage(); g._phase = StarFluxGame::PHASE_PLAYING;
        g._wavesPerfect = nWaves - 1; g._shield = 0;
        before = g._score;
        g.enterResults(audio);
        const long shortGot = g._score - before;
        g._phase = StarFluxGame::PHASE_PLAYING;
        printf("stage 1 waves %d: all perfect +%ld, one short +%ld\n", nWaves, allGot, shortGot);
        check(nWaves == 6 && allGot == ALL_PERFECT_POINTS && shortGot == 0, "all waves perfect: the stage bonus");
        // Two overlapping waves of nine still launch whole.
        g._loop = 5; g.startStage();
        for (auto &f : g._fighters) f.active = false;
        g._seg = 0; g._segSpawned = 0; g._segAt = g_fakeMillis; g._waveLeft[0] = 0; g.spawnWaveFighters();
        const int first = g._segSpawned;
        g._seg = 2; g._segSpawned = 0; g._segAt = g_fakeMillis; g._waveLeft[2] = 0; g.spawnWaveFighters();
        printf("loop 5, two waves at once: %d and %d launched of %d\n", first, g._segSpawned, g.waveCount(6));
        check(first == g.waveCount(7) && g._segSpawned == g.waveCount(6), "two waves of nine fit the fighter pool");
        for (auto &f : g._fighters) f.active = false;
        g._loop = 1;
        // Stage 4: a frost shard slows your steering; a mine steers at you.
        g._stageNum = StarFluxGame::STAGE_CANYON; g._loop = 1;
        g.startStage();
        for (int i = 0; i < 200; ++i) step(none);
        g._shipX = 0; g._shipY = 0; g._invulnUntil = 0; g._shield = SHIELD_MAX;
        for (auto &r : g._rocks) { r.active = false; r.obj->enabled = false; }
        for (auto &e : g._eshots) e.active = false;
        if (StarFluxGame::EShot* e = g.fireAt(0, 0, SHIP_Z + 400.0f, 0, 0)) e->frost = true;
        for (int i = 0; i < 20 && !((long)(g._frozenUntil - g_fakeMillis) > 0); ++i) step(none);
        const bool frozen = (long)(g._frozenUntil - g_fakeMillis) > 0;
        InputState push{}; push.joyY = 1.0f / STEER_X_SIGN;
        const float x0 = g._shipX;
        for (int i = 0; i < 10; ++i) step(push);
        const float slow = fabsf(g._shipX - x0);
        g._frozenUntil = 0; g._shipX = 0;
        for (int i = 0; i < 10; ++i) step(push);
        const float fast = fabsf(g._shipX);
        printf("frozen %d; steering over 10 frames: %.0f frozen, %.0f normal\n", (int)frozen, slow, fast);
        check(frozen && slow < fast * 0.6f, "a frost shard slows steering");
        g._shipX = 200; g._shipY = 100;
        g.spawnMine();
        StarFluxGame::Rock* mine = nullptr;
        for (auto &r : g._rocks) if (r.active && r.mine) mine = &r;
        const float far0 = mine ? hypotf(mine->x - g._shipX, mine->y - g._shipY) : 0;
        for (int i = 0; i < 60 && mine && mine->active; ++i) { g._shipX = 200; g._shipY = 100; step(none); }
        const float far1 = mine ? hypotf(mine->x - g._shipX, mine->y - g._shipY) : 0;
        printf("mine off your line: %.0f, then %.0f\n", far0, far1);
        check(mine && far1 < far0 * 0.7f, "a mine steers at you");
        // Stage 5's blast doors: the gap closes to DOOR_SHUT and opens to
        // DOOR_OPEN, and the marker judges them where they'll be.
        g._stageNum = StarFluxGame::STAGE_MOTHER;
        g.startStage();
        for (auto &b : g._boxes) { b.active = false; if (b.obj) b.obj->enabled = false; }
        g.spawnDoors(0, SHIP_Z + 2000.0f);
        StarFluxGame::Box* doors[2] = {};
        for (auto &b : g._boxes) if (b.active && b.slide != 0) doors[doors[0] ? 1 : 0] = &b;
        float minGap = 1e9f, maxGap = 0;
        for (unsigned long t = 0; t < DOOR_CYCLE_MS; t += 20) {
            float l0, l1, r0, r1;
            g.boxXAt(*doors[0], t, l0, l1); g.boxXAt(*doors[1], t, r0, r1);
            const float gap = std::max(r0, l0) - std::min(r1, l1);
            minGap = std::min(minGap, gap); maxGap = std::max(maxGap, gap);
        }
        printf("blast door gap: %.0f to %.0f\n", minGap, maxGap);
        check(fabsf(minGap - DOOR_SHUT) < 2 && fabsf(maxGap - DOOR_OPEN) < 2, "doors close to DOOR_SHUT, open to DOOR_OPEN");
        float front = 0;
        g.nextObstacle(front);
        g._shipX = 120; g._shipY = 0;
        const unsigned long arrive = g_fakeMillis + (unsigned long)((doors[0]->z - SHIP_Z) / FLY_SPEED * REFERENCE_FRAME_MS);
        unsigned long ph = 0;
        doors[0]->phase = doors[1]->phase = 0;
        auto shutAt = [&](unsigned long p) { StarFluxGame::Box b = *doors[0]; b.phase = p; return g.doorShut(b, arrive); };
        while (shutAt(ph) < 0.99f) ph += 10;   // shut when you get there
        doors[0]->phase = doors[1]->phase = ph;
        const bool shutHit = !g.passClear(front);
        while (shutAt(ph) > 0.01f) ph += 10;   // open when you get there
        doors[0]->phase = doors[1]->phase = ph;
        const bool openClear = g.passClear(front);
        check(shutHit && openClear, "marker: a door counts where it'll be when you get there");
        check(rings >= 10 && blocked == 0, "every ring reachable");
        g.onExit();
        printf("%s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    if (strcmp(mode, "reach") == 0) {
        // Every fighter of every wave, every stage, loops 1-5: how long is it
        // where your lasers can hit it (in front, in range, and within a
        // shot's reach of somewhere the ship can be)? A perfect wave needs
        // them all; under MIN_MS is too short to aim at.
        const unsigned long MIN_MS = 700;
        const float reachX = BOX_X + FIGHTER_R + SHOT_HIT_PAD - 30.0f;
        const float lowY = BOX_Y_MIN - FIGHTER_R - SHOT_HIT_PAD + 30.0f, highY = BOX_Y_MAX + FIGHTER_R + SHOT_HIT_PAD - 30.0f;
        int checked = 0, tooShort = 0;
        unsigned long worst = ~0UL;
        InputState none{};
        g.update(canvas, none, audio);
        for (int loop = 1; loop <= 5; ++loop) {
            for (int st = 0; st < StarFluxGame::STAGE_COUNT; ++st) {
                g._loop = loop; g._stageNum = st;
                for (int seg = 0; seg < g.segmentCount(); ++seg) {
                    g._seg = seg;
                    const auto &s = g.segment();
                    if (s.type != StarFluxGame::SEG_WAVE) continue;
                    for (auto &f : g._fighters) f.active = false;
                    g._segSpawned = 0; g._segAt = g_fakeMillis; g._waveLeft[seg] = 0;
                    g.spawnWaveFighters();
                    for (auto &f : g._fighters) {
                        if (!f.active) continue;
                        unsigned long inReach = 0;
                        for (unsigned long t = 0; t < g.patternLength(f.pattern); t += 20) {
                            float x, y, z;
                            g.pathPoint(f, t, x, y, z);
                            if (z > SHIP_Z + 400.0f && z < SHIP_Z + SHOT_RANGE * 0.7f && fabsf(x) < reachX && y > lowY && y < highY)
                                inReach += 20;
                        }
                        // Time on the clock: paths run quicker in later loops.
                        inReach = inReach * 100 / (100 + FIGHTER_PACE_PER_LOOP * g.steps());
                        ++checked;
                        if (inReach < worst) worst = inReach;
                        if (inReach < MIN_MS) {
                            ++tooShort;
                            if (tooShort <= 12)
                                printf("  loop %d stage %d seg %d pattern %d: fighter at (%+.0f,%+.0f) in reach %lums\n",
                                       loop, st + 1, seg, (int)f.pattern, f.ox * f.mirror, f.oy, inReach);
                        }
                    }
                }
            }
        }
        for (auto &f : g._fighters) f.active = false;
        printf("fighters checked %d, in reach under %lums: %d (worst %lums)\n", checked, MIN_MS, tooShort, worst);
        const bool ok = tooShort == 0;
        printf("%s\n", ok ? "PASS" : "FAIL");
        g.onExit();
        return ok ? 0 : 1;
    }

    if (strcmp(mode, "rapid") == 0) {
        // The rapid-fire pod and the flight aids.
        bool ok = true;
        auto check = [&](bool c, const char* what) { printf("%-52s %s\n", what, c ? "PASS" : "FAIL"); ok = ok && c; };
        auto step = [&](const InputState &in) { g.update(canvas, in, audio); g_fakeMillis += 33; };
        InputState none{};
        for (long f = 0; f < 3000 && g._phase != StarFluxGame::PHASE_PLAYING; ++f) {
            InputState in{}; in.btnA = in.btnAPressed = (f & 1); step(in);
        }
        // Loop 1: no pod, even through the segments that bring one.
        bool podSeen = false;
        for (long f = 0; f < 6000 && !(g._stage == StarFluxGame::STAGE_RUN && g._seg > POD_SEG_A); ++f) {
            g._shield = SHIELD_MAX; step(none); podSeen |= g._pod.active;
        }
        check(!podSeen && g._seg > POD_SEG_A, "loop 1: no pod");
        // Loop 2, from the pod's segment: it comes, and flying into it gives rapid fire.
        g._loop = 2;
        g.startSegment(POD_SEG_A);
        long f = 0;
        for (; f < 400 && !g._pod.active; ++f) { g._shield = SHIELD_MAX; step(none); }
        check(g._pod.active, "loop 2: pod flies in");
        const long score0 = g._score;
        for (f = 0; f < 400 && g._pod.active && !g._rapid; ++f) {
            g._shield = SHIELD_MAX; g._shipX = g._pod.x; g._shipY = g._pod.y; step(none);
        }
        check(g._rapid && g._score >= score0 + POD_POINTS, "caught: rapid fire and points");
        // Held fire for 1.5s, with and without: about twice the shots.
        auto shotsIn = [&](bool rapid) {
            g._rapid = rapid;
            for (auto &s : g._shots) s.active = false;
            int n = 0; bool was[SHOT_POOL] = {};
            for (int i = 0; i < 45; ++i) {
                g._shield = SHIELD_MAX;
                InputState in{}; in.btnA = true; in.btnAPressed = i == 0; step(in);
                for (int k = 0; k < SHOT_POOL; ++k) { if (g._shots[k].active && !was[k]) ++n; was[k] = g._shots[k].active; }
            }
            step(none); step(none);
            return n;
        };
        const int slow = shotsIn(false), fast = shotsIn(true);
        printf("held fire, 1.5s: %d shots normal, %d rapid\n", slow, fast);
        check(fast >= slow * 2 - 2 && fast <= slow * 2 + 2, "rapid fire doubles the rate");
        // Losing a life loses it.
        g._stage = StarFluxGame::STAGE_RUN; g._invulnUntil = 0;
        g.damageShip(SHIELD_MAX * 2, audio);
        check(!g._rapid, "a life lost: rapid fire gone");
        // Flight aids: a barrier with a gap at x=0, the ship in and out of it.
        for (int i = 0; i < 80; ++i) step(none);   // the retry
        for (auto &b : g._boxes) { b.active = false; if (b.obj) b.obj->enabled = false; }
        const float z = SHIP_Z + 2000.0f;
        g.spawnBox(-TRENCH_HALF_W, -150.0f, TRENCH_FLOOR, TRENCH_TOP, z, 160.0f);
        g.spawnBox(150.0f, TRENCH_HALF_W, TRENCH_FLOOR, TRENCH_TOP, z, 160.0f);
        float front = 0;
        check(g.nextObstacle(front) && fabsf(front - (z - 80.0f)) < 1.0f, "next obstacle found");
        g._shipX = 0; g._shipY = 0;
        const bool inGap = g.passClear(front);
        g._shipX = 200.0f;
        const bool onWall = g.passClear(front);
        g._shipX = 150.0f - SHIP_HIT_R * 0.7f - 2.0f;
        const bool edge = g.passClear(front);
        check(inGap && !onWall && edge, "marker: clear in the gap, a hit on the wall");
        // A gate that will be off when it gets here is clear; on, a hit.
        for (auto &b : g._boxes) b.active = false;
        StarFluxGame::Box* gate = g.spawnGate(-100.0f, 100.0f, z);
        g._shipX = 0; g._shipY = 0;
        g.nextObstacle(front);
        const unsigned long arrive = g_fakeMillis + (unsigned long)((z - SHIP_Z) / FLY_SPEED * REFERENCE_FRAME_MS);
        gate->phase = 0;
        while (!g.gateOn(*gate, arrive)) ++gate->phase;
        const bool gateOn = g.passClear(front);
        while (g.gateOn(*gate, arrive)) ++gate->phase;
        const bool gateOff = g.passClear(front);
        check(!gateOn && gateOff, "marker: a gate counts only if on when you get there");
        g.onExit();
        printf("%s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    struct SegCost { long frames = 0; double us = 0, drawn = 0; int objs = 0, tris = 0; };
    SegCost cost[StarFluxGame::STAGE_COUNT][12];

    bool prevB = false;
    int stallStage = -1, stallSeg = -1;
    unsigned long stallSince = 0, longestSeg = 0;
    bool stalled = false;
    int retrySeg = -1, retryLives = 0;
    bool retryFailed = false;
    int quits = 0, gameOvers = 0, stagesCleared = 0, lastPhase = -1, lastLives = LIVES, livesLost = 0;
    int bombsUsed = 0, lastBombs = BOMBS_START, maxLoop = 1, bossesSeen = 0, demosStarted = 0;
    bool wasBoss = false, wasDemo = false;
    int hits = 0, lastShield = SHIELD_MAX, eshotsFired = 0, namesEntered = 0;
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
            in = god ? g.pilot(!passive, 0) : g.pilot(false, 500);
            if (passive) in.btnA = in.btnB = false;
            b = in.btnB;
        } else {
            in.btnA = (f % 40) == 0;
        }
        if (menus) {
            if (f < 300)                  { in.btnA = false; b = (f >= 20 && f < 200); }   // attract: hold B to exit
            else if (f == 300)            { in.btnA = true; }                              // start
            else if (f >= 400 && f < 403) { b = true; }                                    // playing: tap B, a bomb
            else if (f >= 600 && f < 700) { b = true; }                                    // playing: hold B to quit
            else if (f == 800 && g._phase == StarFluxGame::PHASE_PLAYING) {        // a life lost: the segment again
                g._stage = StarFluxGame::STAGE_RUN; g._invulnUntil = 0; g._shield = 5;
                retrySeg = g._seg; retryLives = g._lives;
                g.damageShip(SHIELD_MAX, audio);
            }
            else if (f == 880 && retrySeg >= 0) {
                bool ok = g._stage == StarFluxGame::STAGE_RUN && g._seg == retrySeg && g._lives == retryLives - 1 &&
                          g._shield == SHIELD_MAX && g._phase == StarFluxGame::PHASE_PLAYING;
                printf("retry: stage %d seg %d lives %d shield %d -> %s\n", (int)g._stage, g._seg, g._lives, g._shield,
                       ok ? "PASS" : "FAIL");
                if (!ok) retryFailed = true;
            }
            else if (f == 900 && g._phase == StarFluxGame::PHASE_PLAYING) {        // last life lost: game over
                g._lives = 1; g._invulnUntil = 0; g._stage = StarFluxGame::STAGE_RUN;
                g._score = 1234;   // makes the table: the name entry must follow
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
            SegCost &c = cost[g._stageNum][g._seg];
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
        // A segment that never ends is a stall (the boss gets longer: it has
        // to be shot, which passive mode doesn't do).
        if (g._phase == StarFluxGame::PHASE_PLAYING && g._stage == StarFluxGame::STAGE_RUN) {
            if (g._stageNum != stallStage || g._seg != stallSeg) {
                stallStage = g._stageNum; stallSeg = g._seg; stallSince = g_fakeMillis;
            } else if (g.segment().type != StarFluxGame::SEG_BOSS) {
                unsigned long t = g_fakeMillis - stallSince;
                if (t > longestSeg) longestSeg = t;
                if (t > 40000 && !stalled) {
                    stalled = true;
                    printf("STALL f=%ld stage %d seg %d: %lus with no progress\n", f, g._stageNum + 1, g._seg, t / 1000);
                }
            }
        }
        lastBombs = g._bombs;
        if (g._bossActive && !wasBoss) ++bossesSeen;
        wasBoss = g._bossActive;
        if (g.inDemo() && !wasDemo) ++demosStarted;
        wasDemo = g.inDemo();
        if (g._phase == StarFluxGame::PHASE_GAMEOVER && lastPhase != StarFluxGame::PHASE_GAMEOVER) ++gameOvers;
        if (g._phase == StarFluxGame::PHASE_NAME && lastPhase != StarFluxGame::PHASE_NAME) ++namesEntered;
        if (g._phase == StarFluxGame::PHASE_RESULTS && lastPhase != StarFluxGame::PHASE_RESULTS) {
            ++stagesCleared;
            totalDowned += g._fightersDowned; totalSeen += g._fightersSeen;
            printf("stage %d clear f=%ld loop=%d downed %d/%d targets %d rings %d shield %d lives %d score %ld\n",
                   g._stageNum + 1, f, g._loop, g._fightersDowned, g._fightersSeen, g._targetsDowned, g._ringsCaught,
                   g._shield, g._lives, g._score);
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
        if (getenv("DEBUG_BOSS") && g._bossActive && f % 60 == 0) {
            float px[3], py[3], pz[3];
            for (int p = 0; p < 3; ++p) g.bossPartPos(p, px[p], py[p], pz[p]);
            printf("boss f=%ld hp=%d/%d/%d ship=(%.0f,%.0f) pods=(%.0f,%.0f)(%.0f,%.0f) core=(%.0f,%.0f,%.0f) online=%d shots=%d\n",
                   f, g._cannonHp[0], g._cannonHp[1], g._coreHp, g._shipX, g._shipY, px[0], py[0], px[1], py[1],
                   px[2], py[2], pz[2], (int)g.targetOnLine(g._shipX, g._shipY),
                   (int)std::count_if(std::begin(g._shots), std::end(g._shots), [](auto &s) { return s.active; }));
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
           demosStarted, g._scores.best(), traceHash);
    printf("hits taken=%d, enemy shots fired=%d\n", hits, eshotsFired);
    printf("table:");
    for (const auto &e : g._scores.table().e) printf(" %s %ld", e.name, (long)e.score);
    printf(" (names entered %d)\n", namesEntered);
    if (totalSeen) printf("fighters downed %ld/%ld (%.0f%%)\n", totalDowned, totalSeen, 100.0 * totalDowned / totalSeen);

    if (menus) {
        // The forced game over (score 1234) must have asked for a name and saved it.
        bool ok = namesEntered == 1 && g._scores.table().e[0].score == 1234;
        printf("name entry: %s\n", ok ? "PASS" : "FAIL");
        if (!ok) return 1;
    }
    printf("longest non-boss segment %.1fs%s\n", longestSeg / 1000.0, stalled ? " STALLED" : "");
    if (stalled) return 1;
    if (retryFailed) return 1;
    if (profile) {
        // Relative only: host microseconds are not ESP32 microseconds.
        printf("\nstage seg  frames  drawnTris  update_us   sceneObjs/sceneTris\n");
        for (int st = 0; st < StarFluxGame::STAGE_COUNT; ++st) {
            for (int s = 0; s < 12; ++s) {
                const SegCost &c = cost[st][s];
                if (!c.frames) continue;
                printf("%5d %3d %7ld %10.0f %10.1f   %d/%d\n", st + 1, s, c.frames, c.drawn / c.frames,
                       c.us / c.frames, c.objs, c.tris);
            }
        }
    }
    return 0;
}
