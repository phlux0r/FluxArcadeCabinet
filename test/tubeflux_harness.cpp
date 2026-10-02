// Host-side regression harness for Tube Flux. Same approach as the Tank
// Flux one (see tankflux_harness.cpp and README.md): real game logic and
// real Jet, fake clock, seeded RNG, scripted bot, hashed trace.

#include "harness_common.h"
#include <chrono>

#define private public
#include "games/TubeFlux/TubeFluxGame.h"
#undef private

using namespace tubeflux;

namespace {

// Lanes a block ahead (within `lookahead` of the ship) will cover.
// Renders fixed set-ups and dumps them, to check conventions by eye: which
// way Jet rolls the camera and rotates objects about Z relative to the
// game's angles. Writes pose_*.ppm.
void poses(TubeFluxGame &g, GFXcanvas16 &canvas, AudioEngine &audio) {
    InputState in{};
    g.update(canvas, in, audio);   // builds the scene
    g.startNewGame(audio);
    g._nextSpawnAt = 1e9f;         // no random spawns

    struct Pose { const char* name; float angle; int lane; int lanes; float z; };
    const Pose poses[] = {
        { "pose_a000_block_lane0",   0.0f, 0, 1, 1400 },   // block dead ahead, on the floor
        { "pose_a000_block_lane2",   0.0f, 2, 1, 1400 },   // +90deg: should be on the right wall
        { "pose_a090_block_lane2",  90.0f, 2, 1, 1400 },   // ship rolled to it: block on the floor
        { "pose_a000_block3_lane7",  0.0f, 7, 3, 1400 },   // lanes 7,0,1: across the floor
        { "pose_a000_block_ship",    0.0f, 0, 1, SHIP_Z }, // at the ship: should sit under it
        { "pose_a000_crystal_lane0", 0.0f, 0, 0, 1400 },   // lanes 0 = crystal
        { "pose_a000_crystal_lane1", 0.0f, 1, 0, 1000 },
    };
    for (const Pose &p : poses) {
        for (auto &o : g._obstacles) { o.active = false; o.obj->enabled = false; }
        for (auto &c : g._crystals) { c.active = false; c.obj->enabled = false; }
        if (p.lanes == 0) {
            auto &c = g._crystals[0];
            c.active = true; c.resolved = true; c.lane = p.lane; c.at = g._dist + p.z;
            c.obj->enabled = true;
            g.placeObstacle(c);
        }
        for (auto &o : g._obstacles) {
            if (o.lanes != p.lanes) continue;
            o.active = true; o.resolved = true;
            o.lane = p.lane; o.at = g._dist + p.z;
            o.obj->enabled = true;
            g.placeObstacle(o);
            break;
        }
        g._angle = p.angle;
        g._rollVel = 0;
        g.updateShipSprite();
        g.renderWorld(canvas);
        g.drawHUD(canvas);

        FrameDumper d(p.name);
        d.n = 1; d.at[0] = 0;
        d.maybeDump(0, canvas);
    }

    // Pickups ahead in lane 0, with shots in flight: the gun with single
    // shots, then the shield cross with twin-gun shots.
    struct PickupPose { const char* name; TubeFluxGame::PickupKind kind; int gunLevel; };
    const PickupPose pps[] = {
        { "pose_a000_gun_shots",    TubeFluxGame::PICKUP_GUN,    1 },
        { "pose_a000_shield_twin",  TubeFluxGame::PICKUP_SHIELD, 2 },
        { "pose_a000_upgrade_rapid", TubeFluxGame::PICKUP_UPGRADE, 3 },
    };
    for (const PickupPose &pp : pps) {
        for (auto &o : g._obstacles) { o.active = false; o.obj->enabled = false; }
        for (auto &c : g._crystals) { c.active = false; c.obj->enabled = false; }
        for (auto &sh : g._shots) { sh.active = false; sh.obj->enabled = false; }
        g._chevronObj->enabled = g._crossObj->enabled = false;
        g._angle = 0; g._rollVel = 0;
        g._nextSpawnAt = 1e9f;
        g._pickupKind = pp.kind;
        g._pickupActive = true; g._pickupLane = 0; g._pickupAt = g._dist + 1500;
        g.pickupObj()->enabled = true;
        g.updatePickup(audio);
        g._gunLevel = pp.gunLevel;
        g._shield = 2;
        int shots = 0;
        for (int i = 0; i < 2; ++i) {
            if (pp.gunLevel >= 2) {
                for (float off : { -TWIN_SPREAD_DEG, TWIN_SPREAD_DEG }) {
                    auto &sh = g._shots[shots++];
                    sh.active = true; sh.angle = off; sh.at = g._dist + 900 + i * 700; sh.obj->enabled = true;
                }
            } else {
                auto &sh = g._shots[shots++];
                sh.active = true; sh.angle = 0; sh.at = g._dist + 900 + i * 700; sh.obj->enabled = true;
            }
        }
        g.updateShots(audio);
        g.updateShipSprite();
        g.renderWorld(canvas);
        g.drawHUD(canvas);
        FrameDumper d(pp.name);
        d.n = 1; d.at[0] = 0;
        d.maybeDump(0, canvas);
    }

    // Drone chase: lane 0 and lane 3 warned (the flash is on for odd 90ms
    // periods), then the drone ahead in lane 1, hit-flashing.
    for (auto &sh : g._shots) { sh.active = false; sh.obj->enabled = false; }
    g._chevronObj->enabled = g._crossObj->enabled = false;
    g._pickupActive = false;
    g._angle = 0;
    g_fakeMillis = 90 * 101;
    g._warnLanes = (1u << 0) | (1u << 3);
    g.renderWorld(canvas);
    g.drawHUD(canvas);
    { FrameDumper d("pose_a000_chase_warn"); d.n = 1; d.at[0] = 0; d.maybeDump(0, canvas); }
    g._warnLanes = 0;
    g._chase = TubeFluxGame::CHASE_AHEAD;
    g._droneAngle = 45.0f; g._droneZ = DRONE_AHEAD_Z;
    g._droneHp = 5; g._droneMaxHp = 8;
    g._droneObj->enabled = true;
    g.placeDrone();
    g.renderWorld(canvas);
    g.drawHUD(canvas);
    { FrameDumper d("pose_a000_chase_drone"); d.n = 1; d.at[0] = 0; d.maybeDump(0, canvas); }
    g._droneFlashUntil = g_fakeMillis + 100;
    g._droneAngle = 0.0f;
    g.placeDrone();
    g.renderWorld(canvas);
    g.drawHUD(canvas);
    { FrameDumper d("pose_a000_chase_drone_hit"); d.n = 1; d.at[0] = 0; d.maybeDump(0, canvas); }
    g.hideChase();

    // Bonus: the portal (lane 2, just ahead), then a round with a ring and
    // a line of gems in front of you.
    g._bonusPhase = TubeFluxGame::BONUS_PORTAL;
    g._portalLane = 2; g._portalAt = g._dist + 1500;
    g_fakeMillis = 100 * 11;   // gold, not white, this frame
    g.renderWorld(canvas);
    g.drawHUD(canvas);
    { FrameDumper d("pose_a000_bonus_portal"); d.n = 1; d.at[0] = 0; d.maybeDump(0, canvas); }
    g._bonusPhase = TubeFluxGame::BONUS_ROUND;
    g.applyTierPalette();
    for (int i = 0; i < TUBE_SIDES; ++i) g.placeGem(i == 0 ? 1 : 0, i, g._dist + 2200);
    for (int i = 0; i < 3; ++i) g.placeGem(i == 2 ? 2 : 0, 0, g._dist + 1000 + i * 320);
    g.renderWorld(canvas);
    g.drawHUD(canvas);
    { FrameDumper d("pose_a000_bonus_gems"); d.n = 1; d.at[0] = 0; d.maybeDump(0, canvas); }
    // The three gem types side by side: green, cyan-white, gold.
    for (auto &gm : g._gems) { gm.active = false; gm.obj->enabled = false; }
    for (int t = 0; t < 3; ++t) g.placeGem(t, (TUBE_SIDES - 1 + t) % TUBE_SIDES, g._dist + 1100);
    g.renderWorld(canvas);
    g.drawHUD(canvas);
    { FrameDumper d("pose_a000_bonus_types"); d.n = 1; d.at[0] = 0; d.maybeDump(0, canvas); }
    g.hideBonus();
}

}  // namespace

// Scenarios:
//   play     normal run: the bot misjudges now and then, so game over is covered
//   god      shield pinned, so a long run climbs every tier
//   menus    a Back quit from the attract screen and mid-game (as main.cpp
//            does it: onQuit(), onExit(), init()), game-over timeout
//   profile  god, plus per-frame render cost by tier
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
    const unsigned long stepMs = argc > 3 ? (unsigned long)atol(argv[3]) : 16;

    GFXcanvas16 canvas(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);
    FrameDumper dumper("tube");
    AudioEngine audio;
    TubeFluxGame g;
    g.init(audio);

    if (strcmp(mode, "demoexit") == 0) {
        // Idle into a demo, let it play a while, then press A.
        InputState none{};
        long f = 0;
        for (; f < 20000 && !g.inDemo(); ++f) { g.update(canvas, none, audio); g_fakeMillis += 16; }
        for (int i = 0; i < 600; ++i) { g.update(canvas, none, audio); g_fakeMillis += 16; }
        printf("demo at tier %d, gun %d, dist %.0f, score %ld\n", g._tier, g._gunLevel, g._dist, g._score);
        InputState press{}; press.btnA = true; press.btnAPressed = true;
        g.update(canvas, press, audio);
        g_fakeMillis += 16;
        // Leftovers: anything from the demo still showing. The new run's
        // first frame legitimately spawns blocks at the fog, so a block or
        // crystal only counts if it isn't one of those.
        int visible = 0;
        auto stale = [&](const TubeFluxGame::Obstacle &o) {
            return o.obj->enabled && !(o.active && o.at > g._dist && o.at < g._dist + SPAWN_AHEAD + 1000.0f);
        };
        for (auto &o : g._obstacles) visible += stale(o);
        for (auto &c : g._crystals) visible += stale(c);
        for (auto &b : g._bolts) visible += b.obj->enabled;
        visible += g._droneObj->enabled + g._chevronObj->enabled + g._crossObj->enabled;
        bool ok = g._phase == TubeFluxGame::PHASE_PLAYING && g._tier == 1 && g._gunLevel == 0 &&
                  g._shield == SHIELD_MAX && g._score < 10 && g._chase == TubeFluxGame::CHASE_NONE &&
                  !g._silent && visible == 0 && g._dist < 500.0f;
        printf("after A: phase %d tier %d gun %d shield %d score %ld dist %.0f silent %d leftovers %d -> %s\n",
               (int)g._phase, g._tier, g._gunLevel, g._shield, g._score, g._dist, (int)g._silent, visible,
               ok ? "PASS" : "FAIL");
        g.onExit();
        return ok ? 0 : 1;
    }

    if (strcmp(mode, "pose") == 0) {
        poses(g, canvas, audio);
        g.onExit();
        return 0;
    }

    struct TierCost { long frames = 0; double us = 0, drawn = 0, rast = 0; int objs = 0, tris = 0; };
    TierCost cost[MAX_TIER + 1];

    bool prevA = false, prevB = false;
    int quits = 0, gameOvers = 0, lastPhase = -1, maxTier = 0, hits = 0, lastShield = SHIELD_MAX;
    int hitsByTier[MAX_TIER + 1] = {};
    long armedAtFrame = -1;
    int demosStarted = 0, demosDied = 0;
    bool wasDemo = false;
    int maxGunLevel = 0;
    uint32_t traceHash = 2166136261u;

    for (long f = 0; f < frames; ++f) {
        InputState in{};
        bool a = false, b = false;

        if (g._phase != TubeFluxGame::PHASE_PLAYING) {
            a = (f % 40) == 0;
        } else {
            if (god) g._shield = SHIELD_MAX;
            // Head for the nearest lane that's clear for a while. The play
            // bot looks less far ahead, so it gets caught by fast wide blocks.
            // The game's own autopilot (the attract demo's player), looking
            // further ahead in god mode. Every third drone it holds fire on,
            // so the escape path runs too.
            InputState p = g.pilot(god ? 2600.0f : 900.0f, g._chaseIndex % 3 != 2, 0);
            in.joyY = p.joyY;
            a = p.btnA;
            // Boost and brake now and then, so the throttle is covered.
            long cycle = f % 900;
            if (cycle < 120)                     in.joyX = THROTTLE_SIGN * 1.0f;
            else if (cycle >= 450 && cycle < 520) in.joyX = THROTTLE_SIGN * -1.0f;
        }

        // Back (main.cpp's): from the attract screen, then mid-game.
        const bool backQuit = menus && (f == 200 || f == 700);
        if (menus) {
            if (f < 300)                  { a = false; b = false; }
            else if (f > 2000 && g._phase == TubeFluxGame::PHASE_GAMEOVER) { a = false; b = false; }
        }

        if (idle) { in = InputState{}; a = b = false; }
        in.btnA = a; in.btnB = b;
        in.btnAPressed = a && !prevA;
        in.btnBPressed = b && !prevB;
        prevA = a; prevB = b;

        auto t0 = std::chrono::steady_clock::now();
        bool keepRunning = g.update(canvas, in, audio);
        auto t1 = std::chrono::steady_clock::now();
        dumper.maybeDump(f, canvas);
        if (getenv("DUMP_STATE")) {
            for (int i = 0; i < dumper.n; ++i)
                if (dumper.at[i] == f)
                    printf("  state f=%ld tier=%d angle=%.3f roll=%.3f dist=%.1f bend=(%.0f,%.0f) camRot=(%.3f,%.3f,%.3f)\n",
                           f, g._tier, g._angle, g._rollVel, g._dist, g._bendX, g._bendY,
                           g._camera.rotation.x, g._camera.rotation.y, g._camera.rotation.z);
        }

        if (profile && g._phase == TubeFluxGame::PHASE_PLAYING && g._scene) {
            // Bonus-round frames get their own row (0): many gems on screen.
            TierCost &c = cost[g._bonusPhase == TubeFluxGame::BONUS_ROUND ? 0 : g._tier];
            int objs = 0, tris = 0, verts = 0;
            g._scene->getStatistics(objs, tris, verts);
            ++c.frames;
            c.us += std::chrono::duration<double, std::micro>(t1 - t0).count();
            c.drawn += g._scene->lastFrameDrawnTriangles;
            c.rast  += g._scene->lastFrameRasterizedTriangles;
            c.objs = objs; c.tris = tris;
        }

        if (!keepRunning || backQuit) {
            if (backQuit) g.onQuit(audio);
            ++quits;
            printf("quit at f=%ld phase=%d\n", f, (int)g._phase);
            g.onExit();
            g.init(audio);
        }

        if (g._phase == TubeFluxGame::PHASE_PLAYING && g._shield < lastShield) {
            ++hits; ++hitsByTier[g._tier];
            if (getenv("DEBUG_HITS")) {
                printf("HIT f=%ld ang=%.1f vel=%.2f spd=%.1f safe=%d prev=%d blocks:",
                       f, g._angle, g._rollVel, g._speed, g._safeLane, g._prevSafeLane);
                for (const auto &o : g._obstacles)
                    if (o.active && o.at - g._dist < 2500)
                        printf(" [z=%.0f l=%d w=%d%s]", o.at - g._dist, o.lane, o.lanes, o.resolved ? " r" : "");
                printf("\n");
            }
        }
        lastShield = g._shield;
        if (g._phase == TubeFluxGame::PHASE_GAMEOVER && lastPhase != TubeFluxGame::PHASE_GAMEOVER) ++gameOvers;
        lastPhase = g._phase;
        if (g._tier > maxTier) maxTier = g._tier;
        if (g.inDemo() && !wasDemo) ++demosStarted;
        if (!g.inDemo() && wasDemo && g._shield <= 0) ++demosDied;
        wasDemo = g.inDemo();
        if (g.armed() && armedAtFrame < 0) armedAtFrame = f;
        if (g._gunLevel > maxGunLevel) maxGunLevel = g._gunLevel;

        int32_t st[14] = { (int32_t)g._phase, (int32_t)g._score, g._shield, g._tier,
                           (int32_t)g._dist, (int32_t)(g._angle * 10), (int32_t)(g._speed * 10),
                           (int32_t)g._safeLane, (int32_t)g._bendX, (int32_t)g._bendY,
                           g._gunLevel, g._crystalsDestroyed, (int32_t)g._chase, g._droneHp };
        traceHash = fnv(st, sizeof(st), traceHash);
        if (f % 60 == 0) {
            traceHash = fnv(canvas.getBuffer(),
                            ArcadeConfig::LANDSCAPE_WIDTH * ArcadeConfig::LANDSCAPE_HEIGHT * 2, traceHash);
        }
        if (f % 2000 == 0) {
            printf("f=%6ld ph=%d sc=%7d sh=%d tier=%d dist=%8d ang=%5.1f spd=%5.1f hash=%08x\n",
                   f, st[0], st[1], st[2], st[3], st[4], g._angle, g._speed, traceHash);
        }
        g_fakeMillis += stepMs;
    }

    printf("DONE frames=%ld maxTier=%d hits=%d gameovers=%d quits=%d high=%ld "
           "tones=%d melodies=%d final=%08x\n",
           frames, maxTier, hits, gameOvers, quits, g._scores.best(),
           audio.tones, audio.melodies, traceHash);

    printf("weapon: first armed at f=%ld, max gun level=%d, crystals destroyed=%d, "
           "shields collected=%d, wavs=%d\n",
           armedAtFrame, maxGunLevel, g._crystalsDestroyed, g._shieldsCollected, audio.wavs);
    printf("chase: started=%d destroyed=%d escaped=%d (last run)\n",
           g._chaseCount, g._dronesDestroyed, g._dronesEscaped);
    printf("demo: started=%d ended-by-crash=%d\n", demosStarted, demosDied);
    printf("bonus: portals entered=%d missed=%d perfect=%d, last round %d/%d gems (last run)\n",
           g._portalsEntered, g._portalsMissed, g._bonusPerfects, g._gemsHit, g._gemsTotal);
    printf("hits by tier:");
    for (int t = 1; t <= MAX_TIER; ++t) printf(" %d:%d", t, hitsByTier[t]);
    printf("\n");

    if (profile) {
        // Relative only: host microseconds are not ESP32 microseconds.
        printf("\ntier  frames  drawnTris  rastTris  update_us   sceneObjs/sceneTris\n");
        for (int t = 0; t <= MAX_TIER; ++t) {
            const TierCost &c = cost[t];
            if (!c.frames) continue;
            printf(t == 0 ? "bonus" : "%4d", t);
            printf(" %7ld %10.0f %9.0f %10.1f   %d/%d\n", c.frames, c.drawn / c.frames,
                   c.rast / c.frames, c.us / c.frames, c.objs, c.tris);
        }
    }
    return 0;
}
