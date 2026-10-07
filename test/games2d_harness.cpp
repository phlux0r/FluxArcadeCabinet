// Host harness for the 2D games' attract demos (Runner, Asteroid, and
// Lander once it has one). The 3D games have their own harnesses.
//
// For each game it idles through the attract cycle and reports how the
// demos went (and fails if any made a sound or touched the high score),
// then checks A pressed mid-demo starts a clean real game.
//
// These games include the real cabinet/AudioEngine.h (by relative path),
// which is inert here: begin() is never called, so it drops everything,
// and its silence flag is still checked.

#include <Adafruit_ST7735.h>   // main.cpp includes it before the games
#include <cstdio>
#include <cstdint>

// The same fake clock and seeded RNG as the other harnesses
// (harness_common.h pulls in the audio stub, and these games need the real,
// inert engine).
unsigned long g_fakeMillis = 1000;
static uint32_t g_rng = 12345;
static uint32_t lcg() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
long random(long hi) { return hi <= 0 ? 0 : (long)(lcg() % (uint32_t)hi); }
long random(long lo, long hi) { return hi <= lo ? lo : lo + (long)(lcg() % (uint32_t)(hi - lo)); }
void randomSeed(unsigned long s) { g_rng = (uint32_t)s; }

#define private public
#include "games/PlatformFlux/PlatformFluxGame.h"
#include "games/AsteroidFlux/AsteroidFluxGame.h"
#include "games/LanderFlux/LanderFluxGame.h"
#undef private

static const unsigned long STEP_MS = 17;

static GFXcanvas16* g_lastCanvas = nullptr;
static GFXcanvas16 &g_canvasOf(PlatformFluxGame &) { return *g_lastCanvas; }
static void writePPM(const char* name, GFXcanvas16 &c) {
    FILE* f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
    const uint16_t* b = c.getBuffer();
    for (int i = 0; i < c.width() * c.height(); ++i) {
        uint16_t p = b[i];
        unsigned char rgb[3] = { (unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static int highScoreOf(PlatformFluxGame &g) { return g._highScore; }
static int highScoreOf(AsteroidFluxGame &g) { return g._highScore; }
static int highScoreOf(LanderFluxGame &g)   { return g._engine._highScore; }

// DUMP_AT=500,4000: write those idle frames as <game>_<frame>.ppm.
static bool dumpAt(long f) {
    const char* at = getenv("DUMP_AT");
    for (const char* p = at; p && *p; ) {
        if (atol(p) == f) return true;
        p = strchr(p, ',');
        if (p) ++p;
    }
    return false;
}

template <typename Game, typename InDemo, typename Report>
static bool idle(const char* name, Game &g, long frames, InDemo inDemo, Report report) {
    AudioEngine audio;
    GFXcanvas16 canvas(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);
    g_lastCanvas = &canvas;
    g.init(audio);
    const int hs0 = highScoreOf(g);
    int demos = 0, ended = 0;
    long demoFrames = 0;
    bool was = false, leftSilenced = false;
    for (long f = 0; f < frames; ++f) {
        InputState none{};
        g.update(canvas, none, audio);
        if (dumpAt(f)) {
            char file[48];
            snprintf(file, sizeof(file), "%s_%05ld.ppm", name, f);
            writePPM(file, canvas);
        }
        leftSilenced |= audio._silenced;
        const bool now = inDemo(g);
        if (now) { ++demoFrames; report(g, false); }
        if (!was && now) ++demos;
        if (was && !now) ++ended;
        was = now;
        g_fakeMillis += STEP_MS;
    }
    report(g, true);
    const bool ok = !leftSilenced && highScoreOf(g) == hs0 && demos > 0;
    printf("DONE %s idle frames=%ld demos=%d ended=%d avgDemoS=%.1f leftSilenced=%d highScoreTouched=%d -> %s\n",
           name, frames, demos, ended, demos ? demoFrames * STEP_MS / 1000.0 / demos : 0.0,
           (int)leftSilenced, (int)(highScoreOf(g) != hs0), ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "all";
    const long frames = argc > 2 ? atol(argv[2]) : 60000;
    bool ok = true;
    GFXcanvas16 canvas(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);

    if (!strcmp(which, "all") || !strcmp(which, "runner")) {
        static PlatformFluxGame g;
        int died = 0, stages = 0, lastStage = 0, cleared = 0, dumped = 0;
        int prevPhase = -1;
        const char* dump = getenv("RUNNER_DUMP");   // write a demo frame every this many
        const long dumpEvery = dump ? atol(dump) : 0;
        long demoFrame = 0;
        ok &= idle("runner", g, frames, [](PlatformFluxGame &g) { return g._demo; },
            [&](PlatformFluxGame &g, bool final) {
                if (final) {
                    printf("runner demos: %d ended by a death, %d stages cleared, %d hazards got past (popups)\n",
                           died, stages, cleared);
                    return;
                }
                for (auto &p : g._popups) cleared += p.active && p.at == g_fakeMillis;
                if (dumpEvery > 0 && ++demoFrame % dumpEvery == 0 && dumped < 12) {
                    char name[40];
                    snprintf(name, sizeof(name), "runner_%02d.ppm", dumped++);
                    writePPM(name, g_canvasOf(g));
                }
                if (g._stage > lastStage && lastStage) ++stages;
                lastStage = g._stage;
                if (g._phase == PlatformFluxGame::PHASE_DEATH && prevPhase == PlatformFluxGame::PHASE_PLAYING) ++died;
                prevPhase = g._phase;
            });
        printf("runner hazard points: %s\n", cleared > 0 ? "PASS" : "FAIL (none got past)");
        ok &= cleared > 0;
        // A mid-demo: a real game from stage 1, lives and score fresh.
        static PlatformFluxGame h;
        AudioEngine audio;
        h.init(audio);
        for (long f = 0; f < 100000 && !h._demo; ++f) { InputState n{}; h.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
        for (int f = 0; f < 600; ++f) { InputState n{}; h.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
        InputState a{}; a.btnA = a.btnAPressed = true;
        h.update(canvas, a, audio);
        bool pass = !h._demo && h._phase == PlatformFluxGame::PHASE_PLAYING && h._stage == 1 &&
                    h._lives == ArcadeConfig::RUNNER_LIVES && h._score == 0 && !audio._silenced;
        printf("runner demoexit: phase %d stage %d lives %d score %d -> %s\n",
               (int)h._phase, h._stage, h._lives, h._score, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerpits")) {
        // Runner's fire pits: on the first loop (stages 1-2) a plain jump,
        // stick left alone, clears every one with a few frames to spare;
        // from the second loop they're wider, and the forward push through
        // a jump (back on the ground, forward in the air) still clears them.
        // Each case: the first pit after `stage` starts, every jump frame
        // tried on a copy of the game.
        AudioEngine audio;
        auto tryPit = [&](int seed, int stage, bool push, int &gap, int &window) {
            g_rng = (uint32_t)seed;
            static PlatformFluxGame g;
            g = PlatformFluxGame();
            g.init(audio);
            g.startNewGame(audio);
            if (stage > 1) g.startStage(stage);
            g._player.activateInvincibility(0);
            auto stick = [&](const PlatformFluxGame &s) { return push ? (s._player.isOnGround() ? -1.0f : 1.0f) : 0.0f; };
            int pit = -1;
            for (int f = 0; f < 3000 && pit < 0 && g._phase == PlatformFluxGame::PHASE_PLAYING; ++f) {
                for (int i = 0; i < 6; ++i) {
                    const auto &p = g._platforms._pool[i];
                    if (!p.active || !p.firePitBefore || p.pitScored) continue;
                    const float l = p.x - p.firePitGapWidth;
                    if (l > g._player.getX() + RUNNER_WIDTH + 2 && l < g._player.getX() + RUNNER_WIDTH + 40) pit = i;
                }
                if (pit >= 0) break;
                InputState in{}; in.joyY = push ? -1.0f : 0.0f;
                in.btnB = g._platforms.duckWanted(g._player.getX(), g._player.getX() + RUNNER_WIDTH);   // Night's beams
                g.update(canvas, in, audio); g_fakeMillis += STEP_MS;
            }
            if (pit < 0) return false;
            gap = (int)g._platforms._pool[pit].firePitGapWidth;
            window = 0;
            static PlatformFluxGame h;
            for (int k = 0; k < 60; ++k) {
                h = g;
                const uint32_t r = g_rng; const unsigned long m = g_fakeMillis;
                bool alive = true;
                for (int f = 0; f < 150 && !h._platforms._pool[pit].pitScored; ++f) {
                    InputState in{}; in.joyY = stick(h); in.btnAPressed = f == k;
                    in.btnB = h._platforms.duckWanted(h._player.getX(), h._player.getX() + RUNNER_WIDTH);
                    h.update(canvas, in, audio); g_fakeMillis += STEP_MS;
                    if (h._phase != PlatformFluxGame::PHASE_PLAYING) { alive = false; break; }
                }
                // Over and landed: a few frames on, still alive.
                for (int f = 0; f < 15 && alive; ++f) {
                    InputState in{}; in.joyY = stick(h);
                    in.btnB = h._platforms.duckWanted(h._player.getX(), h._player.getX() + RUNNER_WIDTH);
                    h.update(canvas, in, audio); g_fakeMillis += STEP_MS;
                    alive = h._phase == PlatformFluxGame::PHASE_PLAYING;
                }
                window += alive && h._platforms._pool[pit].pitScored;
                g_rng = r; g_fakeMillis = m;
            }
            return true;
        };
        struct Case { int stage; bool push; int minWindow; const char* what; };
        const Case cases[] = {
            { 1,  false, 4, "stage 1, plain jump" },
            { 2,  false, 4, "stage 2, plain jump" },
            { 9,  true,  2, "stage 9, pushed jump" },
            { 17, true,  2, "stage 17, pushed jump" },
            { 25, true,  2, "stage 25, pushed jump" },
        };
        int firstLoopMax = 0;
        for (const Case &c : cases) {
            int n = 0, good = 0, gapLo = 999, gapHi = 0, winLo = 999;
            for (int seed = 1; seed <= 15; ++seed) {
                int gap, window;
                if (!tryPit(seed, c.stage, c.push, gap, window)) continue;
                ++n; good += window >= c.minWindow;
                gapLo = min(gapLo, gap); gapHi = max(gapHi, gap); winLo = min(winLo, window);
            }
            if (c.stage <= 2) firstLoopMax = max(firstLoopMax, gapHi);
            bool pass = n >= 10 && good == n && (c.stage <= 8 || gapHi > firstLoopMax);
            printf("runnerpits %s: %d/%d clear with %d+ frames to jump in (fewest %d), gaps %d-%dpx -> %s\n",
                   c.what, good, n, c.minWindow, winLo, gapLo, gapHi, pass ? "PASS" : "FAIL");
            ok &= pass;
        }
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerwall")) {
        // Where the floating platforms give way to ground again (stage 5
        // into 6), the first ground block mustn't stand more than a stair
        // step (8px, groundYAt's tolerance) above the slab before it: it
        // can't be walked up, and the runner sank through it.
        int runs = 0, walls = 0, worst = 0;
        for (int seed = 1; seed <= 200; ++seed) {
            g_rng = (uint32_t)seed;
            PlatformManager pm;
            pm.initGame(5);
            bool wall = false;
            for (int f = 0; f < 1400; ++f) {
                pm.update(); pm.advanceDifficulty();
                for (const auto &p : pm._pool) {
                    if (!p.active || !p.isGroundSegment || p.firePitBefore) continue;
                    for (const auto &q : pm._pool) {
                        if (!q.active || &q == &p || fabsf(q.x + q.width - p.x) > 0.5f) continue;
                        if (q.y - p.y > 8) { wall = true; worst = max(worst, q.y - p.y); }
                    }
                }
            }
            ++runs; walls += wall;
        }
        bool pass = walls == 0;
        printf("runnerwall: %d of %d runs through stage 5-6 had a step up over 8px (worst %dpx) -> %s\n",
               walls, runs, worst, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerover")) {
        // Game over left alone times out to the title, whichever attract
        // screen the game was started from (from the scores screen it went
        // straight into the demo).
        static PlatformFluxGame g;
        AudioEngine audio;
        g_rng = 7;
        g.init(audio);
        g._attractSlide = PlatformFluxGame::SLIDE_SCORES;
        InputState a{}; a.btnA = a.btnAPressed = true;
        g.update(canvas, a, audio); g_fakeMillis += STEP_MS;
        bool started = g._phase == PlatformFluxGame::PHASE_PLAYING;
        g._lives = 1;
        g._player.reset(30, 200);   // off the bottom: the last life goes
        for (int f = 0; f < 2000 && g._phase != PlatformFluxGame::PHASE_GAMEOVER; ++f) {
            if (g._phase == PlatformFluxGame::PHASE_NAME) g._scores.finishNow();
            InputState n{}; g.update(canvas, n, audio); g_fakeMillis += STEP_MS;
            if (g._phase == PlatformFluxGame::PHASE_NAME) { g._scores.finishNow(); g._phase = PlatformFluxGame::PHASE_GAMEOVER; g._gameOverEnteredMs = millis(); }
        }
        for (long f = 0; f < 3000 && g._phase == PlatformFluxGame::PHASE_GAMEOVER; ++f) {
            InputState n{}; g.update(canvas, n, audio); g_fakeMillis += STEP_MS;
        }
        for (int f = 0; f < 5; ++f) { InputState n{}; g.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
        bool pass = started && g._phase == PlatformFluxGame::PHASE_ATTRACT &&
                    g._attractSlide == PlatformFluxGame::SLIDE_SPLASH && !g._demo;
        printf("runnerover: after the game-over timeout phase %d slide %d demo %d -> %s\n",
               (int)g._phase, (int)g._attractSlide, (int)g._demo, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerlevitate")) {
        // Levitating with the stick held down: the runner can't sink into
        // the ground (it went down to the screen's edge, inside the stone,
        // and fell to its death when the flight ran out). Shielded, so only
        // a fall can end it.
        int sunk = 0, deaths = 0;
        for (int seed = 1; seed <= 10; ++seed) {
            g_rng = (uint32_t)seed;
            static PlatformFluxGame g;
            g = PlatformFluxGame();
            AudioEngine audio;
            g.init(audio);
            g.startNewGame(audio);
            g.startStage(6);
            g._player.activateInvincibility(60000);
            g._player.activateLevitation(ArcadeConfig::RUNNER_LEVITATE_MS);
            const unsigned long until = millis() + ArcadeConfig::RUNNER_LEVITATE_MS + 1500;
            while (millis() < until && g._phase == PlatformFluxGame::PHASE_PLAYING) {
                InputState in{}; in.joyX = 1.0f;
                g._player._invincibleEndTime = millis() + 60000;   // a star would cut it short
                g.update(canvas, in, audio); g_fakeMillis += STEP_MS;
                const float px = g._player.getX();
                if (g._player.isLevitating() &&
                    g._player.getY() + RUNNER_HEIGHT > g._platforms.surfaceYNear(px, px + RUNNER_WIDTH) + 0.5f) ++sunk;
            }
            deaths += g._phase != PlatformFluxGame::PHASE_PLAYING;
        }
        bool pass = sunk == 0 && deaths == 0;
        printf("runnerlevitate: 10 flights held down, %d frames inside the ground, %d deaths -> %s\n",
               sunk, deaths, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerdiamond")) {
        // The diamond (levitation) turns up just before a fire pit: from
        // the second Outpost loop (stages 17-18), where pits are wide, not
        // on the first loop's stages 1-2, whose pits a plain jump clears,
        // and never at Night (stages 9-16: a flying runner can't duck
        // under the gantries). (It was only ever allowed from stage 3,
        // after the pits, so the placement never ran.)
        auto run = [&](int stage, int &placed, int &any) {
            placed = any = 0;
            for (int seed = 1; seed <= 12; ++seed) {
                g_rng = (uint32_t)seed;
                static PlatformFluxGame g;
                g = PlatformFluxGame();
                AudioEngine audio;
                g.init(audio);
                g.startNewGame(audio);
                g.startStage(stage);
                g._player.activateInvincibility(600000);
                auto &lv = g._levitationPowerUp;
                for (int f = 0; f < 800 && g._phase == PlatformFluxGame::PHASE_PLAYING; ++f) {
                    const bool pending = lv._pendingFirePitSpawn, active = lv._active;
                    InputState in = g.demoPilot();
                    g.update(canvas, in, audio); g_fakeMillis += STEP_MS;
                    if (!active && lv._active) ++any;
                    if (pending && !lv._pendingFirePitSpawn && lv._active) ++placed;
                    if (g._player.isLevitating()) g._player._levitationEndTime = 0;   // keep the cooldown off
                    lv._cooldownUntil = 0;
                }
            }
        };
        int placed1, any1, placed9, any9, placed17, any17;
        run(1, placed1, any1);
        run(9, placed9, any9);
        run(17, placed17, any17);
        bool pass = any1 == 0 && any9 == 0 && placed17 >= 6;
        printf("runnerdiamond: stages 1-2 %d diamonds; Night's 9-10 %d; stages 17-18 %d before a pit (%d in all), 12 runs each -> %s\n",
               any1, any9, placed17, any17, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerspikes")) {
        // Spike traps: a retracted one still shows (it was invisible until
        // its warning), and getting past one only scores if it was rising
        // or up while the runner was over it.
        PlatformManager pm;
        auto &p = pm._pool[0];
        p = {};
        p.active = true; p.isGroundSegment = true; p.width = 60; p.x = 40;
        p.y = ArcadeConfig::LANDSCAPE_HEIGHT - 8; p.baseY = (float)p.y;
        p.hasSpike = true; p.spikeOffsetX = 30;
        p.spikePhaseEnd = ~0UL;
        const int sx = (int)(p.x + p.spikeOffsetX);
        p.spikePhase = PlatformManager::SPIKE_SAFE;
        canvas.fillScreen(0);
        pm.render(canvas);
        int shown = 0;
        for (int x = sx - 6; x <= sx + 7; ++x) shown += canvas.getBuffer()[(p.y - 1) * canvas.width() + x] != 0;
        auto pass1 = [&](PlatformManager::SpikePhase ph) {
            p.spikePhase = ph; p.spikeScored = false;
            p.spikeLive = false;
            int pts = 0; float x, y;
            for (float px = 0; px < 120; px += 1.0f) pts += pm.takeCleared(px, x, y);
            return pts;
        };
        const int safe = pass1(PlatformManager::SPIKE_SAFE), warn = pass1(PlatformManager::SPIKE_WARN),
                  up = pass1(PlatformManager::SPIKE_DANGER);
        bool pass = shown > 0 && safe == 0 && warn == ArcadeConfig::RUNNER_SPIKE_POINTS &&
                    up == ArcadeConfig::RUNNER_SPIKE_POINTS;
        printf("runnerspikes: retracted trap %d px showing; passed retracted +%d, rising +%d, up +%d -> %s\n",
               shown, safe, warn, up, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnersounds")) {
        // Runner's optional sounds: with no card, each event's tone or
        // melody; a shared file if that's all there is; its own file once
        // that's on the card. Then each event in play asks for its sound,
        // and a demo asks for none.
        using G = PlatformFluxGame;
        static G g;
        g = G();
        AudioEngine audio;
        g.init(audio);
        auto plays = [&](G::Sfx s, const char* want) { g.sfx(s); return !strcmp(g._lastSfx, want); };
        bool noCard = plays(G::SFX_TITLE, "melody") && plays(G::SFX_JUMP, "melody") && plays(G::SFX_POINTS, "tone") &&
                      plays(G::SFX_STAR, "tone") && plays(G::SFX_FLY, "tone") && plays(G::SFX_ROCK, "tone") &&
                      plays(G::SFX_BOULDER, "tone") && plays(G::SFX_STAGE, "melody") && plays(G::SFX_LIFE, "melody") &&
                      plays(G::SFX_DEATH, "melody") && plays(G::SFX_OVER, "melody") &&
                      plays(G::SFX_DUCK, "none") && plays(G::SFX_DART, "tone") && plays(G::SFX_JET, "tone");
        g._fallbackOnCard[G::SFX_STAR] = g._fallbackOnCard[G::SFX_TITLE] = true;
        bool shared = plays(G::SFX_STAR, "/audio/powerup.wav") && plays(G::SFX_TITLE, "/audio/lander_start.wav");
        for (int i = 0; i < G::SFX_COUNT; ++i) g._sfxOnCard[i] = true;
        bool own = plays(G::SFX_STAR, "/audio/runner_star.wav") && plays(G::SFX_JUMP, "/audio/jump.wav") &&
                   plays(G::SFX_DEATH, "/audio/death.wav");
        printf("runnersounds files: no card %d, shared fallback %d, own file %d\n", (int)noCard, (int)shared, (int)own);

        // In play: each event, set up on the spot, asks for its sound.
        auto step = [&](InputState in = InputState{}) { g.update(canvas, in, audio); g_fakeMillis += STEP_MS; };
        auto heard = [&](G::Sfx s, auto setUp, int frames) {
            g._sfxPlayed = 0;
            setUp();
            for (int f = 0; f < frames && !(g._sfxPlayed & (1u << s)); ++f) step();
            return (g._sfxPlayed & (1u << s)) != 0;
        };
        g.startNewGame(audio);
        for (int f = 0; f < 30; ++f) step();
        const float cx = g._player.getX() + RUNNER_WIDTH / 2.0f, cy = g._player.getY() + RUNNER_HEIGHT / 2.0f;
        g._sfxPlayed = 0;
        InputState a{}; a.btnA = a.btnAPressed = true;
        step(a);
        bool jump = g._sfxPlayed & (1u << G::SFX_JUMP);
        for (int f = 0; f < 60; ++f) step();
        bool star = heard(G::SFX_STAR, [&] {
            g._powerUp._active = true; g._powerUp._x = cx; g._powerUp._y = cy;
            g._player._y = cy - RUNNER_HEIGHT / 2.0f; }, 3);
        bool fly = heard(G::SFX_FLY, [&] {
            g._levitationPowerUp._active = true; g._levitationPowerUp._x = g._player.getX() + RUNNER_WIDTH / 2.0f;
            g._levitationPowerUp._y = g._player.getY() + RUNNER_HEIGHT / 2.0f; }, 3);
        g._player._levitating = false;
        bool stage = heard(G::SFX_STAGE, [&] { g._stage = g._platforms.stageNumber() - 1; }, 2);
        bool life = heard(G::SFX_LIFE, [&] {
            g._platforms.initGame(9); g._stage = 8; g._loopsSeen = 0; g._lives = 3; }, 2);
        g._player._invincible = false;
        bool points = heard(G::SFX_POINTS, [&] {
            auto &p = g._platforms._pool[1];
            p.firePitBefore = true; p.pitScored = false; p.firePitGapWidth = 10; p.x = g._player.getX() - 1; }, 2);
        g.startStage(1);
        g._player._invincible = false;
        bool rock = heard(G::SFX_ROCK, [&] {
            auto &r = g._enemies._rocks[0];
            r = {}; r.active = true; r.radius = 4; r.vy = 0;
            r.x = g._player.getX() + RUNNER_WIDTH / 2.0f; r.y = g._player.getY() + RUNNER_HEIGHT / 2.0f; }, 2);
        bool death = g._sfxPlayed & (1u << G::SFX_DEATH);
        for (int f = 0; f < 200 && g._phase != G::PHASE_PLAYING; ++f) step();
        g._player._invincible = false;
        bool boulder = heard(G::SFX_BOULDER, [&] {
            auto &b = g._boulders._boulders[0];
            b = {}; b.active = true; b.radius = 4.5f; b.x = g._player.getX() + RUNNER_WIDTH / 2.0f;
            g._player._invincible = false; }, 2);
        for (int f = 0; f < 200 && g._phase != G::PHASE_PLAYING; ++f) step();
        bool over = heard(G::SFX_OVER, [&] { g._lives = 1; g._player._y = 200; }, 200);
        printf("runnersounds in play: jump %d star %d fly %d stage %d life %d points %d rock %d death %d boulder %d over %d\n",
               (int)jump, (int)star, (int)fly, (int)stage, (int)life, (int)points, (int)rock, (int)death, (int)boulder, (int)over);

        // A demo: nothing asked for.
        static G d;
        d = G();
        d.init(audio);
        d._sfxPlayed = 0;
        int demoFrames = 0;
        for (long f = 0; f < 20000 && demoFrames < 3000; ++f) {
            InputState n{}; d.update(canvas, n, audio); g_fakeMillis += STEP_MS;
            demoFrames += d._demo;
        }
        printf("runnersounds demo: %d frames of demo, sounds asked for %x\n", demoFrames, d._sfxPlayed);
        bool pass = noCard && shared && own && jump && star && fly && stage && life && points && rock && death &&
                    boulder && over && demoFrames >= 3000 && d._sfxPlayed == 0;
        printf("runnersounds -> %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerplay")) {
        // Real games played by the autopilot (as a player's input, not a
        // demo): how far it gets, and the rules on the way. A lost life
        // restarts the stage it was lost in, score kept; a cleared stage
        // scores 100 if no life went in it, else 50; a whole loop gives a
        // life, up to 5; the last life lost ends in game over.
        using G = PlatformFluxGame;
        bool rules = true;
        int bestStage = 0, loops = 0, overs = 0, diedAt[64] = {};
        for (int seed = 1; seed <= 5; ++seed) {
            g_rng = (uint32_t)seed;
            static G g;
            g = G();
            AudioEngine audio;
            g.init(audio);
            g.startNewGame(audio);
            int deaths = 0, clears = 0, extra = 0;
            long f = 0;
            for (; f < 40000 && g._phase != G::PHASE_NAME && g._phase != G::PHASE_GAMEOVER; ++f) {
                const G::GamePhase phase = g._phase;
                const int stage = g._stage, score = g._score, lives = g._lives;
                const bool died = g._diedThisStage;
                const int loopsDone = g._platforms.loopsCompleted();
                InputState in = phase == G::PHASE_PLAYING ? g.demoPilot() : InputState{};
                g.update(canvas, in, audio); g_fakeMillis += STEP_MS;
                if (phase == G::PHASE_PLAYING && g._phase == G::PHASE_DEATH) {
                    ++deaths;
                    if (stage < 64) ++diedAt[stage];
                    rules &= g._lives == lives - 1;
                }
                if (phase == G::PHASE_DEATH && g._phase == G::PHASE_PLAYING) {
                    const bool good = g._stage == stage && g._score == score && g._diedThisStage &&
                                      g._player.isInvincible() && g._platforms.stageNumber() == stage;
                    if (!good) printf("  seed %d: respawn at stage %d (was %d), score %d (was %d)\n", seed, g._stage, stage, g._score, score);
                    rules &= good;
                }
                if (phase == G::PHASE_PLAYING && g._phase == G::PHASE_PLAYING && g._stage == stage + 1) {
                    ++clears;
                    const int bonus = ArcadeConfig::RUNNER_STAGE_BONUS * (died ? 1 : 2);
                    const int gained = g._score - score;
                    bool good = gained >= bonus && gained < bonus + 2 * ArcadeConfig::RUNNER_BOULDER_POINTS;
                    if (g._platforms.loopsCompleted() > loopsDone) {
                        ++extra;
                        good &= g._lives == min(lives + 1, (int)ArcadeConfig::RUNNER_MAX_LIVES);
                    } else {
                        good &= g._lives == lives;
                    }
                    if (!good) printf("  seed %d: stage %d cleared, +%d (bonus %d), lives %d -> %d\n", seed, stage, gained, bonus, lives, g._lives);
                    rules &= good;
                }
            }
            const bool ended = g._phase == G::PHASE_NAME || g._phase == G::PHASE_GAMEOVER;
            overs += ended;
            if (ended) rules &= g._lives == 0;
            bestStage = max(bestStage, g._stage);
            loops += extra;
            printf("runnerplay seed %d: stage %d, score %d, %d stages cleared, %d lives lost, %d loops, %s after %ld frames\n",
                   seed, g._stage, g._score, clears, deaths, extra, ended ? "game over" : "still going", f);
        }
        printf("runnerplay lives lost by stage:");
        for (int st = 1; st < 64; ++st) if (diedAt[st]) printf(" %d:%d", st, diedAt[st]);
        printf("\n");
        // It has to get through the Outpost and through Night.
        bool pass = rules && bestStage >= 17 && loops > 0;
        printf("runnerplay: rules held %d, furthest stage %d, %d loops finished, %d games over -> %s\n",
               (int)rules, bestStage, loops, overs, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerstages")) {
        // Starting at a stage (after a lost life) puts the world where a
        // run from stage 1 would have it when that stage begins: its
        // tier, its loop and its speed, the progress rule empty.
        int bad = 0;
        for (int stage = 1; stage <= 25; ++stage) {
            g_rng = 99;
            PlatformManager run;
            run.initGame(1);
            for (long f = 0; f < 200000 && run.stageNumber() < stage; ++f) { run.update(); run.advanceDifficulty(); }
            PlatformManager at;
            at.initGame(stage);
            const bool good = at.stageNumber() == stage && run.stageNumber() == stage &&
                              at.getTier() == (stage - 1) % PlatformManager::TIERS_PER_LOOP &&
                              at.getLoop() == (stage - 1) / PlatformManager::TIERS_PER_LOOP &&
                              fabsf(at.getScrollSpeed() - run.getScrollSpeed()) < 0.005f &&
                              at.stageProgress() < 0.01f;
            if (!good) {
                ++bad;
                printf("  stage %d: started at tier %d loop %d speed %.2f progress %.2f; a run got there at speed %.2f\n",
                       stage, at.getTier(), at.getLoop(), at.getScrollSpeed(), at.stageProgress(), run.getScrollSpeed());
            }
        }
        // The extra life for a loop stops at 5.
        static PlatformFluxGame g;
        g = PlatformFluxGame();
        AudioEngine audio;
        g.init(audio);
        g.startNewGame(audio);
        auto loopDone = [&](int lives) {
            g._platforms.initGame(9); g._stage = 8; g._loopsSeen = 0; g._lives = lives;
            g.checkStageProgress();
            return g._lives;
        };
        const int from3 = loopDone(3), from5 = loopDone((int)ArcadeConfig::RUNNER_MAX_LIVES);
        bool pass = bad == 0 && from3 == 4 && from5 == ArcadeConfig::RUNNER_MAX_LIVES;
        printf("runnerstages: %d of 25 stage starts off; a loop's life from 3 -> %d, from 5 -> %d -> %s\n",
               bad, from3, from5, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnername")) {
        // The last life lost with a score for the table: name entry, the
        // stick changing the letter (landscape: up is the stick's left),
        // A through the three letters, then game over with the score on
        // the table and on the HUD's best. Back mid-game records the score;
        // in a demo it doesn't.
        using G = PlatformFluxGame;
        static G g;
        g = G();
        AudioEngine audio;
        g.init(audio);
        g.startNewGame(audio);
        auto step = [&](InputState in = InputState{}) { g.update(canvas, in, audio); g_fakeMillis += STEP_MS; };
        for (int f = 0; f < 10; ++f) step();
        const long big = 900000 + g_fakeMillis % 1000;
        g._score = (int)big;
        g._lives = 1;
        g._player._invincible = false;
        g._player._y = 200;
        for (int f = 0; f < 200 && g._phase != G::PHASE_NAME; ++f) step();
        const bool naming = g._phase == G::PHASE_NAME;
        const char before = g._scores._name[0];
        InputState up{}; up.joyX = -1.0f; up.joyLeft = true;
        step(up); step();
        const char after = g._scores._name[0];
        for (int k = 0; k < 3; ++k) { InputState a{}; a.btnA = a.btnAPressed = true; step(a); step(); }
        const auto &t = g._scores.table();
        const bool onTable = t.e[0].score == big && t.e[0].name[0] == after;
        const bool over = g._phase == G::PHASE_GAMEOVER && g._highScore == (int)big && g._scores.lastRank() == 0;

        // Back mid-game: the score goes on the table; in a demo, nothing.
        g.startNewGame(audio);
        for (int f = 0; f < 10; ++f) step();
        g._score = (int)big + 1;
        g.onQuit(audio);
        const bool quitRecorded = g._scores.table().e[0].score == big + 1;
        static G d;
        d = G();
        d.init(audio);
        d.startDemo();
        d._score = (int)big + 2;
        d.onQuit(audio);
        const bool demoKept = d._scores.table().e[0].score != big + 2;
        bool pass = naming && after != before && onTable && over && quitRecorded && demoKept;
        printf("runnername: entry %d, letter %c -> %c, on the table %d, game over %d, quit recorded %d, demo left out %d -> %s\n",
               (int)naming, before, after, (int)onTable, (int)over, (int)quitRecorded, (int)demoKept, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerpick")) {
        // The stage select: B held and A on an attract screen opens it;
        // the stick steps the stage (left/right by one, up/down by a loop,
        // round from end to end); A starts a test run there (nothing goes
        // on the table, at game over or on a Back quit; A at game over runs
        // it again); B, or leaving it alone, goes back to the title. A on
        // its own still starts a real game from stage 1.
        using G = PlatformFluxGame;
        static G g;
        g = G();
        AudioEngine audio;
        g.init(audio);
        auto step = [&](InputState in = InputState{}) { g.update(canvas, in, audio); g_fakeMillis += STEP_MS; };
        auto push = [&](int dx, int dy) {   // screen directions; landscape: up is the stick's left
            InputState in{};
            in.joyLeft = dy < 0; in.joyRight = dy > 0; in.joyUp = dx < 0; in.joyDown = dx > 0;
            step(in); step();
            return g._pick;
        };
        bool pass = true;
        auto check = [&](const char* what, bool cond) {
            printf("  %-58s %s\n", what, cond ? "ok" : "BAD");
            pass &= cond;
        };
        printf("runnerpick:\n");
        for (int f = 0; f < 10; ++f) step();
        InputState ba{}; ba.btnB = true; ba.btnA = ba.btnAPressed = true;
        step(ba);
        check("B held and A on the title opens the stage select", g._phase == G::PHASE_PICK && g._pick == 1);
        const int r = push(1, 0), u = push(0, -1), l = push(-1, 0), d = push(0, 1), wrap = push(-1, 0);
        check("right +1, up +8, left -1, down -8, left from 1 wraps",
              r == 2 && u == 10 && l == 9 && d == 1 && wrap == G::PICK_STAGES);
        while (g._pick != 12) push(1, 0);
        InputState a{}; a.btnA = a.btnAPressed = true;
        step(a);
        check("A starts a test run at stage 12", g._phase == G::PHASE_PLAYING && g._test && g._stage == 12 &&
              g._score == 0 && g._lives == ArcadeConfig::RUNNER_LIVES && g._platforms.stageNumber() == 12);
        check("no extra life for the loop it starts in", g._lives == ArcadeConfig::RUNNER_LIVES);
        const auto best = g._scores.table().e[0].score;
        g._score = 950000;
        g.onQuit(audio);
        check("Back in a test run puts nothing on the table", g._scores.table().e[0].score == best);
        g._score = 950000;
        g._lives = 1;
        g._player._invincible = false;
        g._player._y = 200;
        for (int f = 0; f < 200 && g._phase == G::PHASE_PLAYING; ++f) step();
        for (int f = 0; f < 200 && g._phase == G::PHASE_DEATH; ++f) step();
        check("its game over skips name entry, table untouched", g._phase == G::PHASE_GAMEOVER &&
              g._scores.table().e[0].score == best);
        for (int f = 0; f < 120; ++f) step();
        step(a);
        check("A at game over runs stage 12 again, still a test", g._phase == G::PHASE_PLAYING && g._test &&
              g._stage == 12 && g._score == 0);
        // Back out of it: game over left alone goes to the title, no longer a test.
        g._lives = 1; g._player._invincible = false; g._player._y = 200;
        for (int f = 0; f < 3000 && g._phase != G::PHASE_ATTRACT; ++f) step();
        check("game over timing out leaves the test behind", g._phase == G::PHASE_ATTRACT && !g._test);
        step(ba);
        step(); step();
        InputState b{}; b.btnB = b.btnBPressed = true;
        step(b);
        check("B in the stage select goes back to the title", g._phase == G::PHASE_ATTRACT &&
              g._attractSlide == G::SLIDE_SPLASH);
        step(); step(ba);
        for (int f = 0; f < 2000 && g._phase == G::PHASE_PICK; ++f) step();
        check("left alone, it goes back to the title", g._phase == G::PHASE_ATTRACT);
        step(); step(a);
        check("A on its own: a real game from stage 1", g._phase == G::PHASE_PLAYING && !g._test && g._stage == 1);
        printf("runnerpick -> %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerpitcount")) {
        // Fire pits per stage, counted as the runner passes them: every
        // run gets the same number in each fire-pit stage (2 in a loop's
        // first stage, 3 in its second; stage 1 often had none), and none
        // two in a row closer than a stage's share apart.
        const int want[4] = { ArcadeConfig::RUNNER_PITS_FIRST, ArcadeConfig::RUNNER_PITS_SECOND,
                              ArcadeConfig::RUNNER_PITS_FIRST, ArcadeConfig::RUNNER_PITS_SECOND };
        const int stages[4] = { 1, 2, 9, 10 };
        int off[4] = {}, lo[4] = { 99, 99, 99, 99 }, hi[4] = {}, others = 0, close = 0;
        for (int seed = 1; seed <= 200; ++seed) {
            g_rng = (uint32_t)seed;
            PlatformManager pm;
            pm.initGame(1);
            int cnt[24] = {};
            long lastAt = -100000;
            for (long f = 0; f < (long)pm.cycleLength() * 2 + 100; ++f) {
                pm.update(); pm.advanceDifficulty();
                for (auto &p : pm._pool) {
                    if (!p.active || !p.firePitBefore || p.pitScored || p.x >= ArcadeConfig::RUNNER_BASE_X) continue;
                    p.pitScored = true;
                    const int s = pm.stageNumber();
                    if (s < 24) ++cnt[s];
                    close += f - lastAt < 60;
                    lastAt = f;
                }
            }
            for (int k = 0; k < 4; ++k) {
                const int c = cnt[stages[k]];
                off[k] += c != want[k];
                lo[k] = min(lo[k], c); hi[k] = max(hi[k], c);
            }
            for (int s = 1; s < 18; ++s)
                if (s != 1 && s != 2 && s != 9 && s != 10) others += cnt[s];
        }
        bool pass = others == 0 && close == 0;
        for (int k = 0; k < 4; ++k) {
            printf("runnerpitcount stage %d: %d-%d pits (want %d), %d of 200 runs off\n",
                   stages[k], lo[k], hi[k], want[k], off[k]);
            pass &= off[k] == 0;
        }
        printf("runnerpitcount: %d pits in other stages, %d closer than 60 frames -> %s\n",
               others, close, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerstars")) {
        // The star: one every 30-45s at most, the wait starting once the
        // last one's been picked up or gone by; never a new one while the
        // runner is invincible. Real games from stage 6, the autopilot
        // playing, ~30fps.
        using G = PlatformFluxGame;
        const unsigned long step33 = 33;
        int stars = 0, whileShield = 0, early = 0;
        unsigned long shortest = ~0UL;
        for (int seed = 1; seed <= 10; ++seed) {
            g_rng = (uint32_t)seed;
            static G g;
            g = G();
            AudioEngine audio;
            g.init(audio);
            g.startNewGame(audio, 6, true);
            // The wait starts when a star's picked up or gone by, and with
            // each life (a stage started again).
            unsigned long waitFrom = millis();
            for (int f = 0; f < 6000 && g._phase != G::PHASE_GAMEOVER && g._phase != G::PHASE_NAME; ++f) {
                const bool was = g._powerUp._active;
                const G::GamePhase phase = g._phase;
                if (g._phase == G::PHASE_PLAYING && g._stage > 8) { g.startStage(6); waitFrom = millis(); }   // stay in the star's stages
                InputState in = g._phase == G::PHASE_PLAYING ? g.demoPilot() : InputState{};
                const bool shielded = g._player.isInvincible();
                g.update(canvas, in, audio); g_fakeMillis += step33;
                if (phase == G::PHASE_DEATH && g._phase == G::PHASE_PLAYING) waitFrom = millis();
                if (!was && g._powerUp._active) {
                    ++stars;
                    whileShield += shielded;
                    const unsigned long gap = millis() - waitFrom;
                    shortest = min(shortest, gap);
                    early += gap < (unsigned long)ArcadeConfig::RUNNER_STAR_GAP_MIN_MS;
                }
                if (was && !g._powerUp._active) waitFrom = millis();
            }
        }
        // Shielded with the wait over: nothing; the shield gone: the star.
        static G h;
        h = G();
        AudioEngine audio;
        h.init(audio);
        h.startNewGame(audio, 6, true);
        h._powerUp._nextSpawnAt = 0;
        h._player.activateInvincibility(60000);
        int during = 0;
        for (int f = 0; f < 600; ++f) {
            h._player._invincibleEndTime = millis() + 60000;
            InputState in = h.demoPilot(); h.update(canvas, in, audio); g_fakeMillis += step33;
            during += h._powerUp._active;
            h._powerUp._active = false;
        }
        h._player._invincible = false;
        bool after = false;
        for (int f = 0; f < 5 && !after; ++f) {
            InputState in = h.demoPilot(); h.update(canvas, in, audio); g_fakeMillis += step33;
            after = h._powerUp._active;
        }
        bool pass = stars > 0 && whileShield == 0 && early == 0 && during == 0 && after;
        printf("runnerstars: %d stars, %d while invincible, shortest wait %lus (%d under 30s); shielded %d frames with one, after %d -> %s\n",
               stars, whileShield, shortest == ~0UL ? 0 : shortest / 1000, early, during, (int)after, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerdeath")) {
        // A lost life: the world stays drawn, stopped, under the burst
        // (only the backdrop and the burst were); the burst and the flight
        // exhaust have trails.
        using G = PlatformFluxGame;
        static G g;
        g = G();
        AudioEngine audio;
        g.init(audio);
        g.startNewGame(audio);
        auto step = [&](InputState in = InputState{}) { g.update(canvas, in, audio); g_fakeMillis += STEP_MS; };
        for (int f = 0; f < 40; ++f) step();
        auto groundLit = [&]() {   // the ground's rows, below the hills
            int n = 0;
            for (int y = ArcadeConfig::LANDSCAPE_HEIGHT - 6; y < ArcadeConfig::LANDSCAPE_HEIGHT; ++y)
                for (int x = 0; x < ArcadeConfig::LANDSCAPE_WIDTH; ++x) n += canvas.getBuffer()[y * canvas.width() + x] != 0;
            return n;
        };
        const int playing = groundLit();
        g._player._invincible = false;
        auto &r = g._enemies._rocks[0];
        r = {}; r.active = true; r.radius = 4;
        r.x = g._player.getX() + RUNNER_WIDTH / 2.0f; r.y = g._player.getY() + RUNNER_HEIGHT / 2.0f;
        step();
        const bool died = g._phase == G::PHASE_DEATH;
        const int atDeath = groundLit();
        int trailed = 0, sparks = 0;
        for (const auto &p : g._particles._pool)   // the burst, not the rock's grey hit sparks
            if (p.active && p.color != ArcadeConfig::COLOR_GREY) { ++sparks; trailed += p.trail >= 3; }
        const float x0 = g._platforms._pool[0].x;
        for (int f = 0; f < 20; ++f) step();
        const int during = groundLit();
        const bool still = g._platforms._pool[0].x == x0;
        // Flight: the exhaust's sparks have trails.
        for (int f = 0; f < 200 && g._phase != G::PHASE_PLAYING; ++f) step();
        g._particles.clearAll();
        g._player.activateLevitation(5000);
        int exhaust = 0, exhaustTrailed = 0;
        for (int f = 0; f < 30; ++f) {
            step();
            for (const auto &p : g._particles._pool)
                if (p.active && p.color == ArcadeConfig::COLOR_ION_BLUE) { ++exhaust; exhaustTrailed += p.trail >= 2; }
        }
        bool pass = died && atDeath >= playing * 3 / 4 && during >= playing * 3 / 4 && still &&
                    sparks > 0 && trailed == sparks && exhaust > 0 && exhaustTrailed == exhaust;
        printf("runnerdeath: ground pixels playing %d, at the death %d, during %d, stopped %d; burst %d/%d trailed; exhaust %d/%d trailed -> %s\n",
               playing, atDeath, during, (int)still, trailed, sparks, exhaustTrailed, exhaust, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerscore")) {
        // The score ticks with distance, a point every 3 frames of it,
        // whatever the clock does (it ticked every 100ms of real time),
        // and a game doesn't share its tick with the last one.
        using G = PlatformFluxGame;
        int got[3] = {};
        const unsigned long steps[3] = { 17, 50, 17 };
        for (int k = 0; k < 3; ++k) {
            static G g;
            g = G();
            AudioEngine audio;
            g_rng = 5;
            g.init(audio);
            g.startNewGame(audio);
            for (int f = 0; f < 150; ++f) { InputState n{}; g.update(canvas, n, audio); g_fakeMillis += steps[k]; }
            got[k] = g._score;
        }
        bool pass = got[0] == 150 / ArcadeConfig::RUNNER_SCORE_FRAMES && got[1] == got[0] && got[2] == got[0];
        printf("runnerscore: 150 frames at 17ms %d, at 50ms %d, a second game %d (want %d) -> %s\n",
               got[0], got[1], got[2], 150 / ArcadeConfig::RUNNER_SCORE_FRAMES, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerduck")) {
        // The duck: B held on the ground crouches (the box 12 tall, feet
        // where they were); let go, it stands; in the air or flying B does
        // nothing; A from a duck jumps, standing up.
        using G = PlatformFluxGame;
        static G g;
        g = G();
        AudioEngine audio;
        g.init(audio);
        g.startNewGame(audio);
        auto step = [&](InputState in = InputState{}) { g.update(canvas, in, audio); g_fakeMillis += STEP_MS; };
        for (int f = 0; f < 20; ++f) step();
        InputState b{}; b.btnB = true;
        step(b);
        const float feet = g._player.getY() + RUNNER_HEIGHT;
        const bool ducked = g._player.isDucking() && g._player.hitTop() == feet - RUNNER_DUCK_HEIGHT;
        step();
        const bool stood = !g._player.isDucking() && g._player.hitTop() == g._player.getY();
        step(b);
        InputState ab{}; ab.btnB = true; ab.btnA = ab.btnAPressed = true;
        step(ab);
        const bool jumped = !g._player.isOnGround() && !g._player.isDucking();
        step(b); step(b);
        const bool airNo = !g._player.isOnGround() && !g._player.isDucking();
        for (int f = 0; f < 60; ++f) step();
        g._player.activateLevitation(3000);
        step(b);
        const bool flyNo = !g._player.isDucking();
        bool pass = ducked && stood && jumped && airNo && flyNo;
        printf("runnerduck: ducks %d, stands %d, jumps from a duck %d, none in the air %d or flying %d -> %s\n",
               (int)ducked, (int)stood, (int)jumped, (int)airNo, (int)flyNo, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerobstacles")) {
        // Night's obstacles, one at a time on flat ground ahead: a beam
        // kills a runner standing or jumping, a ducked one passes (+10);
        // a jet lit kills one standing, unlit lets it pass standing, lit
        // passes it ducked (+10); a dart kills one standing, passes one
        // ducked (+20) or jumping.
        using G = PlatformFluxGame;
        using PM = PlatformManager;
        AudioEngine audio;
        enum How { STAND, DUCK, JUMP };
        auto trial = [&](PM::NightKind kind, PM::SpikePhase jet, How how, int &points) {
            static G g;
            g = G();
            g_rng = 3;
            g.init(audio);
            g.startNewGame(audio, 9, true);
            g._player._invincible = false;
            auto &pool = g._platforms._pool;
            for (int i = 0; i < 6; ++i) {   // flat ground, nothing on it
                auto &p = pool[i];
                p.active = true; p.isGroundSegment = true; p.isMoving = false; p.firePitBefore = false;
                p.hasSpike = false; p.night = PM::NIGHT_NONE; p.x = i * 60.0f; p.width = 60;
                p.y = ArcadeConfig::LANDSCAPE_HEIGHT - 8; p.baseY = (float)p.y;
            }
            auto &p = pool[2];
            p.night = kind; p.nightScored = p.fired = p.jetLive = false; p.glintAt = 0;
            p.nightX = kind == PM::NIGHT_LAUNCHER ? 52.0f : 25.0f;
            if (kind == PM::NIGHT_LAUNCHER) { p.x = 140.0f; }
            p.jetPhase = jet; p.jetPhaseEnd = ~0UL;
            const int score0 = g._score;
            int pts = 0;
            for (int f = 0; f < 200 && g._phase == G::PHASE_PLAYING; ++f) {
                InputState in{};
                in.btnB = how == DUCK;
                // Jump: as it nears the obstacle (or as the dart nears it).
                float target = kind == PM::NIGHT_LAUNCHER ? 9999.0f : p.x + p.nightX;
                for (auto &d : g._platforms._darts) if (d.active) target = d.x;
                if (how == JUMP && g._player.isOnGround() && target - (g._player.getX() + RUNNER_WIDTH) < 14) in.btnAPressed = true;
                const int before = g._score;
                g.update(canvas, in, audio); g_fakeMillis += STEP_MS;
                const int gained = g._score - before;
                pts += gained - gained % 10;   // hazard points, not the distance tick
            }
            (void)score0;
            points = pts;
            return g._phase == G::PHASE_PLAYING;
        };
        struct Case { PM::NightKind kind; PM::SpikePhase jet; How how; bool lives; int points; const char* what; };
        const Case cases[] = {
            { PM::NIGHT_BEAM, PM::SPIKE_SAFE, STAND, false, 0, "beam, standing" },
            { PM::NIGHT_BEAM, PM::SPIKE_SAFE, JUMP,  false, 0, "beam, jumping" },
            { PM::NIGHT_BEAM, PM::SPIKE_SAFE, DUCK,  true, ArcadeConfig::RUNNER_BEAM_POINTS, "beam, ducked" },
            { PM::NIGHT_JET, PM::SPIKE_DANGER, STAND, false, 0, "jet lit, standing" },
            { PM::NIGHT_JET, PM::SPIKE_SAFE,   STAND, true, 0, "jet off, standing" },
            { PM::NIGHT_JET, PM::SPIKE_DANGER, DUCK,  true, ArcadeConfig::RUNNER_JET_POINTS, "jet lit, ducked" },
            { PM::NIGHT_LAUNCHER, PM::SPIKE_SAFE, STAND, false, 0, "dart, standing" },
            { PM::NIGHT_LAUNCHER, PM::SPIKE_SAFE, DUCK,  true, ArcadeConfig::RUNNER_DART_POINTS, "dart, ducked" },
            { PM::NIGHT_LAUNCHER, PM::SPIKE_SAFE, JUMP,  true, ArcadeConfig::RUNNER_DART_POINTS, "dart, jumped" },
        };
        bool pass = true;
        printf("runnerobstacles:\n");
        for (const Case &c : cases) {
            int pts = 0;
            const bool lives = trial(c.kind, c.jet, c.how, pts);
            const bool good = lives == c.lives && (!c.lives || pts == c.points);
            printf("  %-20s %s, +%d -> %s\n", c.what, lives ? "lives" : "dies", pts, good ? "ok" : "BAD");
            pass &= good;
        }
        printf("runnerobstacles -> %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnernight")) {
        // Night's generation, 200 runs through loop 2 and into loop 3:
        // beams in stages 9-10, jets in 14, darts in 15, all three in 16,
        // none in 11-13 nor in the Outpost's loops; each column has the
        // runner's width of the same block either side of it (no step
        // under a duck), each launcher level ground behind it, and none
        // on a pit's block. Pits still exact.
        using PM = PlatformManager;
        int seen[3][26] = {};   // beams, jets, launchers by stage
        int badGround = 0, onPit = 0, pitsOff = 0, lacking = 0, missing[5] = {};
        for (int seed = 1; seed <= 200; ++seed) {
            g_rng = (uint32_t)seed;
            PM pm;
            pm.initGame(9);
            int pits[26] = {};
            int mine[3][26] = {};
            const long frames = (long)(pm.stageStartDistance(18) - pm.stageStartDistance(9)) + 100;
            for (long f = 0; f < frames; ++f) {
                pm.update(); pm.advanceDifficulty();
                pm.updateNight((float)ArcadeConfig::RUNNER_BASE_X);
                const int st = pm.stageNumber();
                for (auto &p : pm._pool) {
                    if (!p.active) continue;
                    if (p.firePitBefore && !p.pitScored && p.x < ArcadeConfig::RUNNER_BASE_X) { p.pitScored = true; if (st < 26) ++pits[st]; }
                    if (p.night == PM::NIGHT_NONE || p.nightScored) continue;
                    const float ox = p.x + p.nightX;
                    if (ox >= ArcadeConfig::RUNNER_BASE_X) continue;
                    p.nightScored = true;   // counted as the runner passes it
                    if (st < 26) { ++seen[p.night - 1][st]; ++mine[p.night - 1][st]; }
                    if (p.firePitBefore) ++onPit;
                    if (p.night != PM::NIGHT_LAUNCHER) {
                        badGround += p.nightX < RUNNER_WIDTH ||
                                     p.nightX + ArcadeConfig::NIGHT_COLUMN_W + RUNNER_WIDTH > p.width;
                    }
                }
            }
            pitsOff += pits[9] != ArcadeConfig::RUNNER_PITS_FIRST || pits[10] != ArcadeConfig::RUNNER_PITS_SECOND;
            // Every run meets each stage's obstacle at least once.
            const bool miss[5] = { !mine[0][9], !mine[0][10], !mine[1][14], !mine[2][15],
                                   !(mine[0][16] + mine[1][16] + mine[2][16]) };
            for (int k = 0; k < 5; ++k) { lacking += miss[k]; missing[k] += miss[k]; }
        }
        bool pass = badGround == 0 && onPit == 0 && pitsOff == 0;
        const char* names[3] = { "beams", "jets", "darts" };
        for (int k = 0; k < 3; ++k) {
            printf("runnernight %s by stage:", names[k]);
            for (int st = 9; st <= 17; ++st) printf(" %d:%d", st, seen[k][st]);
            printf("\n");
        }
        // Where each must and mustn't be (stage 17 is the Outpost again).
        auto none = [&](int k, int st) { return seen[k][st] == 0; };
        pass &= lacking == 0 && seen[0][16] > 0 && seen[1][16] > 0 && seen[2][16] > 0;
        for (int st : { 11, 12, 13, 17 }) for (int k = 0; k < 3; ++k) pass &= none(k, st);
        pass &= none(1, 9) && none(2, 9) && none(0, 14) && none(2, 14) && none(0, 15) && none(1, 15);
        printf("runnernight runs without: beams 9 %d, beams 10 %d, jets 14 %d, darts 15 %d, any 16 %d\n",
               missing[0], missing[1], missing[2], missing[3], missing[4]);
        printf("runnernight: %d runs missing a stage's obstacle, %d columns without room either side, %d on a pit's block, %d runs with pits off -> %s\n",
               lacking, badGround, onPit, pitsOff, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "runnerdark")) {
        // Night's lantern: the ground past its reach is drawn at half, then
        // a quarter, brightness; flames stay bright. The Outpost: all lit.
        using PM = PlatformManager;
        PM pm;
        pm.initGame(9);
        for (auto &p : pm._pool) p.active = false;
        auto &p = pm._pool[0];
        p = {};
        p.active = true; p.isGroundSegment = true; p.x = 0; p.width = 160; p.y = 110; p.baseY = 110;
        p.night = PM::NIGHT_JET; p.nightX = 130; p.jetPhase = PM::SPIKE_DANGER; p.jetPhaseEnd = ~0UL;
        canvas.fillScreen(0);
        pm.render(canvas, 60);
        auto at = [&](int x, int y) { return canvas.getBuffer()[y * canvas.width() + x]; };
        const uint16_t g = PM::NIGHT_STONE, near = at(30, 120), mid = at(75, 120), far = at(120, 120);
        const uint16_t flame = at(135, p.y - ArcadeConfig::NIGHT_BEAM_CLEAR - 2);
        const bool lanternOk = near == g && mid == PM::darken(g) && far == PM::darken(PM::darken(g));
        const bool flameOk = flame == ArcadeConfig::COLOR_ORANGE || flame == ArcadeConfig::COLOR_RED || flame == ArcadeConfig::COLOR_YELLOW;
        PM out;
        out.initGame(1);
        for (auto &q : out._pool) q.active = false;
        out._pool[0] = p;
        out._pool[0].night = PM::NIGHT_NONE;
        canvas.fillScreen(0);
        out.render(canvas);
        const bool outpostLit = at(30, 120) == at(120, 120);
        bool pass = lanternOk && flameOk && outpostLit;
        printf("runnerdark: ground near %04x, past %04x, far %04x (stone %04x); far flame %04x; outpost even %d -> %s\n",
               near, mid, far, g, flame, (int)outpostLit, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "asteroid")) {
        static AsteroidFluxGame g;
        int hits = 0, prevPhase = -1, maxField = 0;
        ok &= idle("asteroid", g, frames, [](AsteroidFluxGame &g) { return g._demo; },
            [&](AsteroidFluxGame &g, bool final) {
                if (final) { printf("asteroid demos: %d ended by a hit, fields up to %d asteroids\n", hits, maxField); return; }
                if (g._asteroids.activeCount() > maxField) maxField = g._asteroids.activeCount();
                if (g._phase == AsteroidFluxGame::PHASE_HIT && prevPhase == AsteroidFluxGame::PHASE_PLAYING) ++hits;
                prevPhase = g._phase;
            });
        static AsteroidFluxGame h;
        AudioEngine audio;
        h.init(audio);
        for (long f = 0; f < 100000 && !h._demo; ++f) { InputState n{}; h.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
        for (int f = 0; f < 600; ++f) { InputState n{}; h.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
        InputState a{}; a.btnA = a.btnAPressed = true;
        h.update(canvas, a, audio);
        bool pass = !h._demo && h._phase == AsteroidFluxGame::PHASE_PLAYING && h._lives == 3 && h._score == 0 &&
                    h._asteroids.activeCount() == 1 && !audio._silenced;
        printf("asteroid demoexit: phase %d lives %d score %d asteroids %d -> %s\n",
               (int)h._phase, h._lives, h._score, h._asteroids.activeCount(), pass ? "PASS" : "FAIL");
        ok &= pass;
    }
    if (!strcmp(which, "all") || !strcmp(which, "fire")) {
        // The Fire power-up: A shoots only while it lasts (20s), a bolt
        // breaks an asteroid in its path and scores as passing it would,
        // the line under the HUD is its timer, and it only turns up from
        // 500 points.
        static AsteroidFluxGame g;
        AudioEngine audio;
        g.init(audio);
        g.startNewGame(audio);
        auto frame = [&](bool a) {
            InputState in{}; in.btnA = a; in.btnAPressed = false;
            g.update(canvas, in, audio);
            g_fakeMillis += STEP_MS;
        };
        auto &rock = g._asteroids._pool[0];
        auto park = [&]() {             // one asteroid, dead ahead of the nose
            rock.active = true; rock.isComet = false;
            rock.x = 150; rock.y = g._ship.getY() + 5; rock.vy = 0;
        };
        auto away = [&]() { rock.x = 200; };   // out of the way while waiting
        for (int f = 0; f < 150; ++f) { frame(false); away(); }   // past the spawn shield
        park();
        int bolts = 0;
        for (int f = 0; f < 20; ++f) { frame(true); bolts += g._bolts.activeCount(); park(); }
        bool pass = bolts == 0;
        printf("fire none: A without the power-up fires %d bolts -> %s\n", bolts, pass ? "PASS" : "FAIL");
        ok &= pass;

        g._ship.activateFire(ArcadeConfig::FIRE_DURATION_MS);
        const unsigned long firedAt = millis();
        park();
        const int score0 = g._score, size = rock.sizeClass, passed0 = g._asteroidsPassed;
        bool hit = false;
        for (int f = 0; f < 40 && !hit; ++f) { frame(true); bolts += g._bolts.activeCount(); hit = rock.x > 150; }
        pass = hit && g._score == score0 + size && g._asteroidsPassed == passed0 + 1 && g._particles.activeCount() > 0;
        printf("fire hit: %d bolt-frames, asteroid broken %d, score +%d (size %d), passed +%d -> %s\n",
               bolts, (int)hit, g._score - score0, size, g._asteroidsPassed - passed0, pass ? "PASS" : "FAIL");
        ok &= pass;

        // The timer: the line under the HUD is orange for the time left,
        // from the left, green beyond; green all along once it's over.
        while (millis() < firedAt + ArcadeConfig::FIRE_DURATION_MS / 2) { frame(false); away(); }
        const uint16_t* line = canvas.getBuffer() + 10 * canvas.width();
        pass = line[40] == ArcadeConfig::COLOR_ORANGE && line[120] == ArcadeConfig::COLOR_GREEN;
        printf("fire bar: half way, x40 %04x x120 %04x -> %s\n", line[40], line[120], pass ? "PASS" : "FAIL");
        ok &= pass;
        while (millis() < firedAt + ArcadeConfig::FIRE_DURATION_MS + 100) { frame(false); away(); }
        park();
        bolts = 0;
        for (int f = 0; f < 20; ++f) { frame(true); bolts += g._bolts.activeCount(); park(); }
        pass = !g._ship.isFireActive() && bolts == 0 && line[40] == ArcadeConfig::COLOR_GREEN;
        printf("fire over: after 20s active %d, bolts %d, line x40 %04x -> %s\n",
               (int)g._ship.isFireActive(), bolts, line[40], pass ? "PASS" : "FAIL");
        ok &= pass;

        g._ship.deactivateFire();
    }

    if (!strcmp(which, "all") || !strcmp(which, "powerups")) {
        // Asteroid's power-ups: one weighted draw over those available
        // (shield 30, slow 20, Fire 35 from 500 points, extra life 15 when
        // due), the same one twice running only if drawn twice, every
        // 12-25s; and none drawn over an asteroid.
        static AsteroidFluxGame g;
        AudioEngine audio;
        g.init(audio);
        g.startNewGame(audio);
        auto &pu = g._powerUps;
        bool pass = true;
        struct Mix { int score; int lo[5], hi[5]; };   // % by type: NONE, LIFE, SHIELD, SLOW, FIRE
        const Mix mixes[] = {
            { 300, { 0, 0, 50, 35, 0 },  { 0, 0, 65, 50, 0 } },
            { 550, { 0, 0, 25, 17, 35 }, { 0, 0, 38, 30, 48 } },
            { 700, { 0, 9, 22, 14, 29 }, { 0, 20, 35, 26, 42 } },
        };
        for (const Mix &m : mixes) {
            int n[5] = {}, repeats = 0, last = -1;
            const int N = 4000;
            for (int k = 0; k < N; ++k) {
                pu._data.active = false;
                pu._nextSpawnTime = 0;
                int lives = 3; bool ui = false;
                pu.update(m.score, g._ship, lives, ui, audio, g._asteroids);
                const int t = (int)pu._data.type;
                ++n[t];
                repeats += t == last;
                last = t;
            }
            bool good = repeats * 100 < N * 30;   // one redraw: below 500 there are only two
            printf("powerups at %d:", m.score);
            for (int t = 1; t < 5; ++t) {
                const int pc = n[t] * 100 / N;
                good &= pc >= m.lo[t] && pc <= m.hi[t];
                printf(" %s %d%%", t == 1 ? "life" : t == 2 ? "shield" : t == 3 ? "slow" : "fire", pc);
            }
            printf(", %d%% repeats -> %s\n", repeats * 100 / N, good ? "PASS" : "FAIL");
            pass &= good;
        }
        unsigned long lo = ~0UL, hi = 0;
        for (int k = 0; k < 500; ++k) {
            pu.resetTimeline();
            const unsigned long d = pu._nextSpawnTime - millis();
            lo = min(lo, d); hi = max(hi, d);
        }
        bool good = lo >= 12000 && hi <= 25000;
        printf("powerups interval: %lu-%lums -> %s\n", lo, hi, good ? "PASS" : "FAIL");
        pass &= good;

        // A busy demo field with a power-up always on its way: count the
        // frames one is on screen over an asteroid.
        static AsteroidFluxGame h;
        h.init(audio);
        h.startDemo();
        h._asteroids.setDemoField(6, ArcadeConfig::BASE_SPEED + ArcadeConfig::SPEED_STEP * 6);
        int shown = 0, over = 0;
        for (int f = 0; f < 30000; ++f) {
            if (!h._demo) { h.startDemo(); h._asteroids.setDemoField(6, ArcadeConfig::BASE_SPEED + ArcadeConfig::SPEED_STEP * 6); }
            if (!h._powerUps._data.active) h._powerUps._nextSpawnTime = 0;
            h._score = 600;
            h._demoUntil = millis() + 60000;
            InputState in{}; h.update(canvas, in, audio); g_fakeMillis += STEP_MS;
            const auto &d = h._powerUps._data;
            if (!d.active || d.x - d.radius > ArcadeConfig::SCREEN_WIDTH || d.x + d.radius < 0) continue;
            ++shown;
            for (const auto &a : h._asteroids._pool) {
                if (!a.active) continue;
                const float dx = a.x - d.x, dy = a.y - d.y, r = a.radius + d.radius;
                if (dx * dx + dy * dy < r * r) { ++over; break; }
            }
        }
        good = shown > 5000 && over * 1000 < shown;
        printf("powerups clear: %d of %d frames on screen over an asteroid -> %s\n", over, shown, good ? "PASS" : "FAIL");
        pass &= good;
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "lander")) {
        static LanderFluxGame g;
        int landings = 0, crashes = 0, fastApproaches = 0, lastLandings = 0;
        bool wasDis = false;
        ok &= idle("lander", g, frames, [](LanderFluxGame &g) { return g._engine._demo; },
            [&](LanderFluxGame &g, bool final) {
                auto &e = g._engine;
                if (final) {
                    printf("lander demos: %d landings, %d crashes (%d on a deliberately fast approach)\n",
                           landings, crashes, fastApproaches);
                    return;
                }
                if (e._demoLandings > lastLandings) ++landings;
                lastLandings = e._demoLandings;
                if (e._lander.isDisintegrating && !wasDis) { ++crashes; fastApproaches += e._demoDescent > 1.0f; }
                wasDis = e._lander.isDisintegrating;
            });
        static LanderFluxGame h;
        AudioEngine audio;
        h.init(audio);
        for (long f = 0; f < 100000 && !h._engine._demo; ++f) { InputState n{}; h.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
        for (int f = 0; f < 600; ++f) { InputState n{}; h.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
        InputState a{}; a.btnA = a.btnAPressed = true;
        h.update(canvas, a, audio);
        auto &e = h._engine;
        bool pass = !e._demo && !e._isTitleScreen && !e._isGameOver && e._level == 1 && e._score == 0 &&
                    e._lander.lives == 3 && !audio._silenced;
        printf("lander demoexit: title %d level %d score %d lives %d -> %s\n",
               (int)e._isTitleScreen, e._level, e._score, e._lander.lives, pass ? "PASS" : "FAIL");
        ok &= pass;

        // Crash debris moves one step a frame, like everything else (it
        // was updated twice a frame while the ship broke up).
        e._particles.clearAll();
        e._lander.kill(e._particles);
        int k = 0;
        while (k < 120 && !e._particles._pool[k].active) ++k;
        const float x0 = e._particles._pool[k].x, vx = e._particles._pool[k].vx;
        g_fakeMillis += 20;
        InputState n{};
        h.update(canvas, n, audio);
        const float moved = e._particles._pool[k].x - x0;
        pass = fabsf(moved - vx) < 0.001f;
        printf("lander debris: moved %.3f in a frame at vx %.3f -> %s\n", moved, vx, pass ? "PASS" : "FAIL");
        ok &= pass;

        // Gravity: gentle on level 1 (about half the old 0.025), rising a
        // little every level, the old start by level 6, no more after level 20.
        float gv[31] = {};
        bool rising = true;
        for (int lv = 1; lv <= 30; ++lv) {
            e._level = lv;
            e.initLevel();
            gv[lv] = e._currentGravity;
            if (lv > 1 && lv <= 20) rising &= gv[lv] > gv[lv - 1];
        }
        pass = fabsf(gv[1] - 0.012f) < 1e-4f && fabsf(gv[6] - 0.0245f) < 1e-4f && rising &&
               fabsf(gv[20] - 0.0595f) < 1e-4f && gv[30] == gv[20];
        printf("lander gravity: L1 %.4f L6 %.4f L10 %.4f L20 %.4f L30 %.4f, rising each level %d -> %s\n",
               gv[1], gv[6], gv[10], gv[20], gv[30], (int)rising, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "landerphys")) {
        // Lander's flight: the engine a fixed 2.8x gravity on every level,
        // spooling up and down rather than all or nothing; the ship turning
        // towards the stick at a limited rate (the normalised stick, with
        // its deadzone, not the raw reading); and the landing rule: down
        // under 1.0, sideways under 0.5, tilt under 15 degrees, fuel or not.
        static LanderFluxGame l;
        AudioEngine audio;
        auto &e = l._engine;
        l.init(audio);
        e.startGame(audio);
        auto &s = e._lander;
        ParticleManager &pm = e._particles;
        bool pass = true;
        for (int lv : { 1, 20 }) {
            e._level = lv; e.initLevel();
            s.y = 60; s.vx = s.vy = 0;
            for (int k = 0; k < 20; ++k) s.updatePhysics(true, 0, e._currentGravity, e.thrustPower(), pm);
            const float v0 = s.vy;
            s.updatePhysics(true, 0, e._currentGravity, e.thrustPower(), pm);
            const float ratio = 1.0f - (s.vy - v0) / e._currentGravity;   // thrust / gravity
            const bool good = fabsf(ratio - 2.8f) < 0.02f;
            printf("landerphys ratio: level %d, thrust %.2fx gravity -> %s\n", lv, ratio, good ? "PASS" : "FAIL");
            pass &= good;
        }
        e._level = 1; e.initLevel();
        s.y = 60; s.vx = s.vy = 0;
        float v0 = s.vy;
        s.updatePhysics(true, 0, e._currentGravity, e.thrustPower(), pm);
        const float first = 1.0f - (s.vy - v0) / e._currentGravity;
        int up = 1;
        while (s.engine < 1.0f && up < 30) { s.updatePhysics(true, 0, e._currentGravity, e.thrustPower(), pm); ++up; }
        int down = 0;
        while (s.engine > 0.0f && down < 30) { s.updatePhysics(false, 0, e._currentGravity, e.thrustPower(), pm); ++down; }
        bool good = first < 0.6f && up >= 6 && up <= 9 && down >= 4 && down <= 6;
        printf("landerphys spool: first step %.2fx gravity, full after %d steps, off after %d -> %s\n",
               first, up, down, good ? "PASS" : "FAIL");
        pass &= good;

        auto frame = [&](float stick, int raw) {
            InputState in{}; in.joyX = stick; in.rawJoyX = raw; in.rawJoyY = 2048;
            l.update(canvas, in, audio);
            g_fakeMillis += 20;
        };
        e._level = 1; e.initLevel();
        for (int f = 0; f < 3; ++f) frame(0, 2048);
        for (int f = 0; f < 10; ++f) { frame(0, 2350); s.y = 40; s.vy = 0; }   // stick at rest, off centre
        const float rest = s.thrustAngle;
        frame(1.0f, 4095); s.y = 40; s.vy = 0;
        const float oneStep = s.thrustAngle;
        int turn = 1;
        while (s.thrustAngle < PI / 4 - 0.001f && turn < 40) { frame(1.0f, 4095); s.y = 40; s.vy = 0; ++turn; }
        good = rest == 0.0f && oneStep > 0.0f && oneStep < 0.07f && turn >= 11 && turn <= 14;
        printf("landerphys steer: at rest %.3f, one step %.3f rad, 45 degrees after %d steps -> %s\n",
               rest, oneStep, turn, good ? "PASS" : "FAIL");
        pass &= good;

        // By level: the pad 24px, a pixel narrower every two levels, 16px
        // from level 17; a full tank to level 10, then 4% less a level, 60%
        // from level 20.
        {
            const int lv[] = { 1, 2, 3, 10, 11, 16, 17, 20, 30 };
            const int pad[] = { 24, 24, 23, 20, 19, 17, 16, 16, 16 };
            const int fuel[] = { 100, 100, 100, 100, 96, 76, 72, 60, 60 };
            bool byLevel = true;
            for (int i = 0; i < 9; ++i) {
                e._level = lv[i]; e.initLevel();
                const bool good = e._padWidth == pad[i] && (int)lroundf(s.fuel) == fuel[i] &&
                                  e._padX >= 15 && e._padX + e._padWidth <= ArcadeConfig::PORTRAIT_WIDTH - 15;
                if (!good) printf("  level %d: pad %d (want %d), fuel %.0f (want %d)\n", lv[i], e._padWidth, pad[i], s.fuel, fuel[i]);
                byLevel &= good;
            }
            printf("landerphys levels: pad 24 to 16, fuel 100 to 60 -> %s\n", byLevel ? "PASS" : "FAIL");
            pass &= byLevel;
        }

        // Landing: the ship just above the pad's middle, one step to touch down.
        struct Case { const char* what; float vx, vy, deg, fuel; bool lands; };
        const Case cases[] = {
            { "gentle",          0.0f, 0.8f,  0, 50, true  },
            { "down too fast",   0.0f, 1.1f,  0, 50, false },
            { "sideways drift",  0.3f, 0.6f,  0, 50, true  },
            { "sideways fast",   0.7f, 0.6f,  0, 50, false },
            { "tilted a little", 0.0f, 0.6f, 10, 50, true  },
            { "tilted too far",  0.0f, 0.6f, 22, 50, false },
            { "out of fuel",     0.0f, 0.6f,  0,  0, true  },
        };
        for (const Case &c : cases) {
            e._level = 3; e.initLevel();
            e._isGameOver = false; s.lives = 3;
            frame(0, 2048);
            const float px = e._padX + e._padWidth / 2.0f;
            int seg = (int)px / e._groundStepX;
            const float floorY = e._groundY[seg] + (px - seg * e._groundStepX) / e._groundStepX * (e._groundY[seg + 1] - e._groundY[seg]);
            s.x = px; s.y = floorY - 4 - c.vy * 0.5f; s.vx = c.vx; s.vy = c.vy - e._currentGravity;
            s.thrustAngle = c.deg * PI / 180.0f + 0.0628f * (c.deg > 0);   // turns back a step first
            s.fuel = c.fuel;
            for (int f = 0; f < 3 && !e._isGameOver && !s.isDisintegrating; ++f) frame(0, 2048);
            const bool landed = e._isGameOver && !s.isDisintegrating;
            good = landed == c.lands && (landed || s.isDisintegrating);
            printf("landerphys land %-15s -> %s, %s\n", c.what, landed ? "landed" : s.isDisintegrating ? "crashed" : "neither",
                   good ? "PASS" : "FAIL");
            pass &= good;
        }
        ok &= pass;
    }

    if (!strcmp(which, "all") || !strcmp(which, "trails")) {
        // A spark with a trail leaves dimmer pixels behind it, in a line
        // back along its path; one without leaves just the spark.
        auto lit = [](GFXcanvas16 &c, int &dim) {
            int n = 0; dim = 0;
            const uint16_t* b = c.getBuffer();
            for (int i = 0; i < c.width() * c.height(); ++i)
                if (b[i]) { ++n; dim += b[i] != ArcadeConfig::COLOR_WHITE; }
            return n;
        };
        bool pass = true;
        for (int trail : { 0, 4 }) {
            static ParticleManager pm;
            pm.clearAll();
            pm.spawnFire(40, 60, 2.0f, 0.0f, ArcadeConfig::COLOR_WHITE, trail);
            pm._pool[0].expireMs = g_fakeMillis + 1000;   // well before its fade
            pm._pool[0].totalLifeMs = 1000;
            for (int f = 0; f < 4; ++f) { pm.update(); g_fakeMillis += STEP_MS; }
            canvas.fillScreen(0);
            pm.render(canvas);
            int dim, n = lit(canvas, dim);
            // Along the row, dimmer and dimmer back from the spark.
            bool fades = true;
            const uint16_t* row = canvas.getBuffer() + 60 * canvas.width();
            for (int x = 41; x <= 47; ++x) fades &= (row[x] & 0x1F) >= (row[x - 1] & 0x1F);
            const bool good = trail ? n >= 6 && dim == n - 1 && fades : n == 1;
            printf("trails %d: %d pixels lit, %d dimmed -> %s\n", trail, n, dim, good ? "PASS" : "FAIL");
            pass &= good;
        }
        // Clipped by the HUD line like the sparks themselves.
        static ParticleManager pm;
        pm.clearAll();
        pm.spawnFire(40, 5, 0.0f, 2.0f, ArcadeConfig::COLOR_WHITE, 4);   // comes down through it
        for (int f = 0; f < 4; ++f) { pm.update(); g_fakeMillis += STEP_MS; }
        canvas.fillScreen(0);
        pm.render(canvas, 11);
        int above = 0;
        for (int y = 0; y < 11; ++y) for (int x = 0; x < canvas.width(); ++x) above += canvas.getBuffer()[y * canvas.width() + x] != 0;
        printf("trails clip: %d pixels above the HUD line -> %s\n", above, above == 0 ? "PASS" : "FAIL");
        ok &= pass && above == 0;

        // Fading like Resonance's afterglow: a spark with a trail dims out
        // at the end of its life rather than flashing white, and its trail
        // lingers a few frames after it, dimming, then goes. One without
        // still ends in white.
        auto brightest = [&](int &n) {
            int top = 0; n = 0;
            const uint16_t* b = canvas.getBuffer();
            for (int i = 0; i < canvas.width() * canvas.height(); ++i)
                if (b[i]) { ++n; if ((b[i] & 0x1F) > top) top = b[i] & 0x1F; }
            return top;
        };
        pm.clearAll();
        pm.spawnFire(40, 60, 2.0f, 0.0f, ArcadeConfig::COLOR_WHITE, 4);
        auto &sp = pm._pool[0];
        sp.expireMs = g_fakeMillis + 150;   // the shortest fire life
        sp.totalLifeMs = 150;
        int frames = 0, white = 0, n = 0, after = 0, lingered = 0, top = 31;
        bool dimming = true;
        while (sp.active && frames < 100) {
            const bool expired = g_fakeMillis >= sp.expireMs;
            pm.update();
            g_fakeMillis += STEP_MS;
            ++frames;
            canvas.fillScreen(0);
            pm.render(canvas);
            const int b = brightest(n);
            for (int i = 0; i < canvas.width() * canvas.height(); ++i) white += canvas.getBuffer()[i] == ArcadeConfig::COLOR_WHITE && g_fakeMillis + 40 > sp.expireMs;
            if (expired && sp.active) { ++after; lingered += n > 0; dimming &= b <= top; }
            top = b;
        }
        bool good = !sp.active && white == 0 && after >= 2 && lingered >= 2 && dimming;
        printf("trails fade: %d frames after expiry, %d of them lit, dimming %d, white flashes %d -> %s\n",
               after, lingered, (int)dimming, white, good ? "PASS" : "FAIL");
        ok &= good;
        pm.clearAll();
        pm.spawnFire(40, 60, 0.0f, 0.0f, ArcadeConfig::COLOR_AMBER, 0);
        g_fakeMillis = pm._pool[0].expireMs - 40;
        canvas.fillScreen(0);
        pm.render(canvas);
        good = canvas.getBuffer()[60 * canvas.width() + 40] == ArcadeConfig::COLOR_WHITE;
        printf("trails none: a spark without one still ends white -> %s\n", good ? "PASS" : "FAIL");
        ok &= good;
    }
    return ok ? 0 : 1;
}
