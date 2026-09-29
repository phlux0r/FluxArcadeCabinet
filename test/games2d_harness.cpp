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

static int highScoreOf(PlatformFluxGame &g) { return g._highScore; }
static int highScoreOf(AsteroidFluxGame &g) { return g._highScore; }
static int highScoreOf(LanderFluxGame &g)   { return g._engine._highScore; }

template <typename Game, typename InDemo, typename Report>
static bool idle(const char* name, Game &g, long frames, InDemo inDemo, Report report) {
    AudioEngine audio;
    GFXcanvas16 canvas(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);
    g.init(audio);
    const int hs0 = highScoreOf(g);
    int demos = 0, ended = 0;
    long demoFrames = 0;
    bool was = false, leftSilenced = false;
    for (long f = 0; f < frames; ++f) {
        InputState none{};
        g.update(canvas, none, audio);
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
        int died = 0, stages = 0, lastStage = 0;
        int prevPhase = -1;
        ok &= idle("runner", g, frames, [](PlatformFluxGame &g) { return g._demo; },
            [&](PlatformFluxGame &g, bool final) {
                if (final) { printf("runner demos: %d ended by a death, %d stages cleared\n", died, stages); return; }
                if (g._stage > lastStage && lastStage) ++stages;
                lastStage = g._stage;
                if (g._phase == PlatformFluxGame::PHASE_DEATH && prevPhase == PlatformFluxGame::PHASE_PLAYING) ++died;
                prevPhase = g._phase;
            });
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
    }
    return ok ? 0 : 1;
}
