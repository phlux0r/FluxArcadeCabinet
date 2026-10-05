// Host harness for Resonance Flux (the prototype): the real game logic
// against a fake clock and seeded RNG, like the other harnesses. Scenarios:
//
//   figure      the figure maths: phases ResonanceFigure.h calls the same
//               draw the same figure, a phase gap bounds how far apart two
//               figures are, and phases it calls different look different
//   match       resonance needs both the dial and the phase inside their
//               tolerances; the snap pulls the needle onto a clean ratio;
//               a misfire costs static and a cooldown, a shot in resonance
//               shatters and scores; a signal reaching the core adds static;
//               static's wander and the overload at 100
//   play [N]    the autopilot plays real games for N frames (restarting
//               through the name entry and game over): static stays in
//               0-100, the score never drops within a game, and it clears
//               waves with shots, not by letting signals through
//   quit        Back mid-game (onQuit, onExit, init, as main.cpp does) and
//               a clean game after it
//   all         everything (the default)
//
// DUMP_AT=frame,frame writes those frames of `play` as resonance_<frame>.ppm.
// The game includes the real (inert) AudioEngine, so src/ comes ahead of
// the stubs when building (see build.sh).

#include <Adafruit_ST7735.h>
#include "harness_common.h"

#define private public
#include "games/ResonanceFlux/ResonanceFluxGame.h"
#undef private

using namespace resonance;

static const unsigned long STEP_MS = 33;   // ~30fps, the game's target

static bool check(const char* what, bool cond, bool &ok) {
    printf("  %-60s %s\n", what, cond ? "ok" : "BAD");
    ok &= cond;
    return cond;
}

// --- The figure maths --------------------------------------------------------

// The largest distance from any point of one figure to the other's curve
// (both ways), sampled densely: how different they look, unit size. The
// sampling alone leaves up to ~0.01 on the longest curves (4:5).
static float figureGap(const Ratio &r, float p1, float p2) {
    const int N = 1500;
    static float ax[N], ay[N], bx[N], by[N];
    for (int i = 0; i < N; ++i) {
        figurePoint(r, p1, TWO_PI_F * i / N, ax[i], ay[i]);
        figurePoint(r, p2, TWO_PI_F * i / N, bx[i], by[i]);
    }
    float worst = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const float *px = pass ? bx : ax, *py = pass ? by : ay, *qx = pass ? ax : bx, *qy = pass ? ay : by;
        for (int i = 0; i < N; i += 3) {
            float best = 1e9f;
            for (int j = 0; j < N; ++j) {
                const float dx = px[i] - qx[j], dy = py[i] - qy[j], d = dx * dx + dy * dy;
                if (d < best) best = d;
            }
            if (best > worst) worst = best;
        }
    }
    return sqrtf(worst);
}

static bool scenarioFigure() {
    bool ok = true;
    printf("figure:\n");
    bool same = true, bound = true, differ = true;
    float worstSame = 0, worstBound = 0, leastDiffer = 1e9f;
    for (int i = 0; i < RATIO_COUNT; ++i) {
        const Ratio &r = RATIOS[i];
        for (int k = 0; k < 6; ++k) {
            const float p = (float)random(0, 6284) / 1000.0f;
            const float shifted = p + TWO_PI_F * (1 + k % 3) / r.b;
            const float mirrored = PI_F - PI_F * r.a / r.b - p;
            const float g1 = figureGap(r, p, shifted), g2 = figureGap(r, p, mirrored);
            worstSame = fmaxf(worstSame, fmaxf(g1, g2));
            same &= g1 < 0.02f && g2 < 0.02f && phaseGap(r, p, shifted) < 1e-3f && phaseGap(r, p, mirrored) < 1e-3f;
            // A phase gap at the tolerance: the figures no further apart.
            const float near = p + PHASE_TOL;
            const float gb = figureGap(r, p, near);
            worstBound = fmaxf(worstBound, gb - phaseGap(r, p, near));
            bound &= gb <= phaseGap(r, p, near) + 0.01f;
            // Well apart in phase: the figures visibly apart too.
            const float q = (float)random(0, 6284) / 1000.0f;
            if (phaseGap(r, p, q) > 3 * PHASE_TOL) {
                const float gd = figureGap(r, p, q);
                leastDiffer = fminf(leastDiffer, gd);
                differ &= gd > PHASE_TOL / 2;
            }
        }
    }
    char what[96];
    snprintf(what, sizeof(what), "symmetric phases draw the same figure (worst gap %.4f)", worstSame);
    check(what, same, ok);
    snprintf(what, sizeof(what), "a phase gap bounds the figures' gap (worst excess %.4f)", worstBound);
    check(what, bound, ok);
    snprintf(what, sizeof(what), "phases 3x the tolerance apart look apart (least gap %.3f)", leastDiffer);
    check(what, differ, ok);
    bool ordered = true;
    for (int i = 1; i < RATIO_COUNT; ++i)
        ordered &= RATIOS[i].a * RATIOS[i - 1].b > RATIOS[i - 1].a * RATIOS[i].b;
    check("the ratios are in dial order (a/b rising)", ordered, ok);
    check("a stop's snap zone and tolerance stay clear of the next stop", SNAP_ZONE + RATIO_TOL < 0.5f, ok);
    printf("figure -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// --- Scripted play -----------------------------------------------------------

static void startPlaying(ResonanceFluxGame &g, AudioEngine &audio, GFXcanvas16 &canvas) {
    g.init(audio);
    InputState a{}; a.btnA = a.btnAPressed = true;
    g.update(canvas, a, audio);
    g._round = ResonanceFluxGame::ROUND_PLAY;
    g._toSpawn = 5;
    g._spawnAt = g_fakeMillis + 1000000;    // nothing arrives unless placed
}

static void step(ResonanceFluxGame &g, AudioEngine &audio, GFXcanvas16 &canvas, const InputState &in = InputState{}) {
    g_fakeMillis += STEP_MS;
    g.update(canvas, in, audio);
}

static ResonanceFluxGame::Signal &place(ResonanceFluxGame &g, int ratio, float phase, float x, float y) {
    for (auto &s : g._signals) s.alive = false;
    auto &s = g._signals[0];
    s.alive = true; s.ratio = (uint8_t)ratio; s.phase = phase;
    s.x = x; s.y = y; s.speed = 0; s.sway = 0;
    return s;
}

static bool scenarioMatch() {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("match:\n");
    startPlaying(g, audio, canvas);
    check("wave 1's dial: 1:2 and 1:1, one stop apart",
          g._stopCount == 2 && g._stops[0] == 2 && g._stops[1] == 7, ok);
    g.startWave(3);                          // 1:3, 1:2, 2:3, 1:1
    g._round = ResonanceFluxGame::ROUND_PLAY;
    g._spawnAt = g_fakeMillis + 1000000;
    const int r23 = 4;                       // 2:3
    const float on23 = g._stopOf[r23];
    check("wave 3's dial: four stops, 2:3 the third", g._stopCount == 4 && on23 == 2, ok);
    place(g, r23, 1.0f, 20, 30);

    g._dial = on23; g._phase = 1.0f;
    step(g, audio, canvas);
    check("dial and phase on it: in resonance", g._matched == 0, ok);

    g._dial = on23; g._phase = 1.0f + 2 * PHASE_TOL;
    step(g, audio, canvas);
    check("phase off by twice the tolerance: not", g._matched == -1 && g._focus == 0, ok);

    g._dial = on23; g._phase = 1.0f + TWO_PI_F / 3;
    step(g, audio, canvas);
    check("phase a third of a turn on (2:3's symmetry): in resonance", g._matched == 0, ok);

    // The dial off by more than the tolerance, the stick pushing (no snap).
    InputState push{}; push.joyY = 0.2f;
    g._dial = on23 + 2 * RATIO_TOL; g._phase = 1.0f;
    step(g, audio, canvas, push);
    check("dial off by twice the tolerance: not", g._matched == -1, ok);

    // Let go just off a clean ratio: the snap brings it home.
    g._dial = on23 + SNAP_ZONE * 0.8f;
    for (int i = 0; i < 30; ++i) step(g, audio, canvas);
    check("let go inside the snap zone: the needle lands on 2:3", g._dial == on23, ok);
    g._dial = on23 + SNAP_ZONE * 1.5f;
    const float before = g._dial;
    for (int i = 0; i < 30; ++i) step(g, audio, canvas);
    check("outside it: the needle stays put", g._dial == before, ok);

    // Off a clean ratio the figure rolls; on it, it holds still.
    g._dial = on23 + 0.3f;
    float p0 = g._phase;
    step(g, audio, canvas);
    check("off a clean ratio, the phase rolls", fabsf(g._phase - p0) > 0.05f, ok);
    g._dial = on23;
    p0 = g._phase;
    step(g, audio, canvas);
    check("on it, the phase holds", fabsf(g._phase - p0) < 1e-5f, ok);

    // A misfire: static and a cooldown.
    g._phase = 1.0f + 2 * PHASE_TOL;
    g._static = 10;
    InputState a{}; a.btnA = a.btnAPressed = true;
    step(g, audio, canvas, a);
    const float afterMiss = g._static;
    check("a misfire adds static", afterMiss > 10 + STATIC_MISFIRE - 0.1f && g._statMisfires == 1, ok);
    g._phase = 1.0f;
    step(g, audio, canvas, a);
    check("and a shot straight after it is held back", g._signals[0].alive && g._statShatters == 0, ok);
    for (int i = 0; i < 15; ++i) step(g, audio, canvas);
    g._phase = 1.0f;
    const long score0 = g._score;
    step(g, audio, canvas, a);
    check("in resonance after the cooldown: shattered and scored",
          !g._signals[0].alive && g._statShatters == 1 && g._score > score0, ok);
    check("and it's no longer the focus", g._focus == -1 && g._matched == -1, ok);

    // A signal reaching the core.
    auto &s = place(g, 2, 0.5f, CORE_X + CORE_R + SIG_R, CORE_Y);
    s.speed = 20;
    g._static = 0;
    for (int i = 0; i < 20 && g._signals[0].alive; ++i) step(g, audio, canvas);
    check("a signal reaching the core adds its static", !g._signals[0].alive && g._static > STATIC_HIT - 1, ok);

    // Static's wander on your figure.
    g._static = 90;
    g._dial = 1;
    float most = 0;
    for (int i = 0; i < 60; ++i) { step(g, audio, canvas); most = fmaxf(most, fabsf(g._jitD)); }
    check("high static makes your figure wander", most > JITTER_DIAL * 0.3f, ok);
    g._static = 0;
    g._jitTD = g._jitTP = 0;
    for (int i = 0; i < 60; ++i) step(g, audio, canvas);
    check("and settles once it's gone", fabsf(g._jitD) < 1e-3f, ok);

    // Signals meander: one swaying hard wanders well off the straight line
    // in, and takes longer to arrive than one coming straight.
    auto timeIn = [&](float sway, float &widest) {
        auto &m = place(g, 2, 0.5f, 10, CORE_Y);
        m.speed = 10; m.sway = sway; m.swayRate = 1.0f; m.swayAt = 0;
        widest = 0;
        int f = 0;
        for (; f < 2000 && g._signals[0].alive; ++f) {
            widest = fmaxf(widest, fabsf(g._signals[0].y - CORE_Y));
            g._static = 0;
            step(g, audio, canvas);
        }
        return f;
    };
    float wide0, wide1;
    const int straight = timeIn(0, wide0), weaving = timeIn(1.0f, wide1);
    char what[96];
    snprintf(what, sizeof(what), "a weaving signal strays %.0fpx and takes %d frames, not %d", wide1, weaving, straight);
    check(what, wide0 < 0.5f && wide1 > 8 && weaving > straight * 1.15f, ok);

    // A new wave's stops: the needle stays on its ratio, as far off it.
    g._dial = g._stopOf[2] + 0.03f;          // just off 1:2
    g.startWave(5);                          // 1:3, 2:5, 1:2, 2:3, 3:4, 1:1
    check("new stops: the needle keeps its ratio (1:2, now the third)",
          g._stopCount == 6 && fabsf(g._dial - (2 + 0.03f)) < 1e-4f, ok);
    g._round = ResonanceFluxGame::ROUND_PLAY;
    g._spawnAt = g_fakeMillis + 1000000;

    // Overload.
    s = place(g, 2, 0.5f, CORE_X + CORE_R + SIG_R, CORE_Y);
    s.speed = 20;
    g._static = 95;
    for (int i = 0; i < 20 && g._phaseState == ResonanceFluxGame::PHASE_PLAYING; ++i) step(g, audio, canvas);
    check("static at 100 ends the game", g._phaseState != ResonanceFluxGame::PHASE_PLAYING, ok);
    printf("match -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// The autopilot playing for real, A to get through the name entry and
// game over.
static bool scenarioPlay(long frames) {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    FrameDumper dump("resonance");
    g.init(audio);
    long games = 0, bestWave = 1, bestScore = 0, prevScore = 0;
    const char* why = nullptr;
    bool ok = true, tick = false;
    for (long f = 0; f < frames && ok; ++f) {
        InputState in{};
        if (g._phaseState == ResonanceFluxGame::PHASE_PLAYING) in = g.autopilot();
        else { tick = !tick; in.btnA = in.btnAPressed = tick && (f % 40 == 0); }
        const bool wasPlaying = g._phaseState == ResonanceFluxGame::PHASE_PLAYING;
        g.update(canvas, in, audio);
        dump.maybeDump(f, canvas);
        if (g._phaseState == ResonanceFluxGame::PHASE_PLAYING) {
            if (!wasPlaying) { ++games; prevScore = 0; }
            if (g._score < prevScore) { why = "score went down"; ok = false; }
            prevScore = g._score;
            if (g._static < 0 || g._static > 100) { why = "static out of range"; ok = false; }
            if (g._wave > bestWave) bestWave = g._wave;
            if (g._score > bestScore) bestScore = g._score;
        }
        g_fakeMillis += STEP_MS;
    }
    ok = ok && g._statWaves >= 3 && g._statShatters > 3 * g._statHits;
    printf("play: %ld frames, %ld games, reached wave %ld, best %ld, waves cleared %ld, shattered %ld, "
           "got through %ld, misfires %ld, dampens %ld%s%s -> %s\n",
           frames, games, bestWave, bestScore, g._statWaves, g._statShatters, g._statHits,
           g._statMisfires, g._statDampens, why ? ": " : "", why ? why : "", ok ? "PASS" : "FAIL");
    return ok;
}

static bool scenarioQuit() {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("quit:\n");
    g.init(audio);
    for (int i = 0; i < 20; ++i) step(g, audio, canvas);
    check("the title waits for A", g._phaseState == ResonanceFluxGame::PHASE_TITLE, ok);
    InputState a{}; a.btnA = a.btnAPressed = true;
    step(g, audio, canvas, a);
    check("A starts a game", g._phaseState == ResonanceFluxGame::PHASE_PLAYING && g._wave == 1, ok);
    for (int i = 0; i < 300; ++i) step(g, audio, canvas, g.autopilot());
    g.onQuit(audio);
    g.onExit();
    check("onExit frees the scope", g._glow == nullptr, ok);
    g.init(audio);
    step(g, audio, canvas);
    check("init again: the title, the scope back", g._phaseState == ResonanceFluxGame::PHASE_TITLE && g._glow, ok);
    step(g, audio, canvas, a);
    check("a clean game: wave 1, no score, no static, no signals",
          g._wave == 1 && g._score == 0 && g._static == 0 && g.aliveCount() == 0, ok);
    printf("quit -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "all";
    const long frames = argc > 2 ? atol(argv[2]) : 0;
    const bool all = !strcmp(which, "all");
    bool ok = true;
    if (all || !strcmp(which, "figure")) ok &= scenarioFigure();
    if (all || !strcmp(which, "match"))  ok &= scenarioMatch();
    if (all || !strcmp(which, "play"))   ok &= scenarioPlay(frames ? frames : 20000);
    if (all || !strcmp(which, "quit"))   ok &= scenarioQuit();
    return ok ? 0 : 1;
}
