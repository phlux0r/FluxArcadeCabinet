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
