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
