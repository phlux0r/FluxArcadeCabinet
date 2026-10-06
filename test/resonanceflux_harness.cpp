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
//   quit        Back mid-game (onQuit, onExit, init, as main.cpp does): the
//               score on the table, and a clean game after it
//   idle [N]    no input: title, how-to slides, scores, then the demo:
//               silent, the high score untouched, back to the title
//   demoexit    A mid-demo starts a clean real game
//   pick        the wave select: B held with A on the title opens it, the
//               stick steps the wave, A starts a test run that puts nothing
//               on the table; plain A still starts wave 1
//   sounds      the optional WAVs: each event's file if it's on the card,
//               else its fallback file, else its tone, melody or nothing
//   options     B on the title opens the options (B with A still the wave
//               select); each is saved and does what it says; the score
//               multiplier; the how-to slides clear the bands round the scope
//   chord       the boss: every fifth wave with bosses on; stripping, the
//               wrong order, escorts, drift and morph; the autopilot brings
//               down waves 5, 10, 15 and 20's
//   all         everything (the default)
//
// DUMP_AT=frame,frame writes those frames of `play` as resonance_<frame>.ppm,
// of `idle` as resonance_idle_<frame>.ppm and of `pick` as
// resonance_pick_<frame>.ppm.
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
    const int on23 = g._stopOf[r23];
    check("wave 3's dial: four stops, 2:3 the third", g._stopCount == 4 && on23 == 2, ok);
    place(g, r23, 1.0f, 20, 30);

    g._stop = on23; g._phase = 1.0f;
    step(g, audio, canvas);
    check("on its stop, phase on it: in resonance", g._matched == 0, ok);

    g._phase = 1.0f + 2 * PHASE_TOL;
    step(g, audio, canvas);
    check("phase off by twice the tolerance: not", g._matched == -1 && g._focus == 0, ok);

    g._phase = 1.0f + TWO_PI_F / 3;
    step(g, audio, canvas);
    check("phase a third of a turn on (2:3's symmetry): in resonance", g._matched == 0, ok);

    g._stop = on23 + 1; g._phase = 1.0f;
    step(g, audio, canvas);
    check("the next stop, phase right: not, and it isn't the focus", g._matched == -1 && g._focus == -1, ok);
    check("  but it's marked as the one nearest the core", g._threat == 0, ok);
    g._stop = on23;

    // Stepping: one stop a push, round the ring both ways, held repeating.
    InputState right{}; right.joyY = 0.8f;
    InputState left{};  left.joyY = -0.8f;
    InputState nudge{}; nudge.joyY = STEP_PUSH * 0.6f;
    g._stop = 0;
    step(g, audio, canvas, nudge);
    check("a small push doesn't step", g._stop == 0, ok);
    step(g, audio, canvas);
    step(g, audio, canvas, right);
    step(g, audio, canvas, right);
    check("a push steps one stop, however many frames it's held briefly", g._stop == 1, ok);
    step(g, audio, canvas);
    g._stop = g._stopCount - 1;
    step(g, audio, canvas, right); step(g, audio, canvas);
    check("right from the last stop goes round to the first", g._stop == 0, ok);
    step(g, audio, canvas, left); step(g, audio, canvas);
    check("left from the first goes round to the last", g._stop == g._stopCount - 1, ok);
    g._stop = 0;
    const unsigned long holdFrames = (STEP_REPEAT_DELAY_MS + 2 * STEP_REPEAT_MS) / STEP_MS + 1;
    for (unsigned long i = 0; i < holdFrames; ++i) step(g, audio, canvas, right);
    step(g, audio, canvas);
    char what[96];
    snprintf(what, sizeof(what), "held %lums: steps once, then repeats (on stop %d)", holdFrames * STEP_MS, g._stop);
    check(what, g._stop == 3, ok);

    // Up/down turns the phase at a rate; let go, it stays.
    g._stop = on23; g._phase = 1.0f;
    InputState up{}; up.joyX = -1.0f;
    step(g, audio, canvas, up);
    const float turned = g._phase;
    step(g, audio, canvas);
    check("up turns the phase; let go, it holds", turned > 1.05f && fabsf(g._phase - turned) < 1e-5f, ok);

    // The hum: your note, and the signal's (the same on its stop).
    check("on a signal's stop you hear the same note", g._focus == 0 && g._targetHz == g.pitchOf(r23), ok);

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

    // Static's wander on your phase.
    g._static = 90;
    float most = 0;
    for (int i = 0; i < 60; ++i) { step(g, audio, canvas); most = fmaxf(most, fabsf(g._jitP)); }
    check("high static makes your phase wander", most > JITTER_PHASE * 0.3f, ok);
    g._static = 0;
    g._jitTP = 0;
    for (int i = 0; i < 60; ++i) step(g, audio, canvas);
    check("and settles once it's gone", fabsf(g._jitP) < 1e-3f, ok);

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
    snprintf(what, sizeof(what), "a weaving signal strays %.0fpx and takes %d frames, not %d", wide1, weaving, straight);
    check(what, wide0 < 0.5f && wide1 > 8 && weaving > straight * 1.15f, ok);

    // Where signals start: the sides, and the top and bottom only near the
    // corners (the rest is too close to the core).
    float closest = 1e9f;
    bool edgesOk = true;
    for (int i = 0; i < 400; ++i) {
        for (auto &sg : g._signals) sg.alive = false;
        g._toSpawn = 1;
        g.spawnSignal();
        const auto &sg = g._signals[0];
        const float dx = sg.x - CORE_X, dy = sg.y - CORE_Y;
        closest = fminf(closest, sqrtf(dx * dx + dy * dy) - (CORE_R + SIG_R * 0.5f));
        const bool side = sg.x <= SIG_R + 1.5f || sg.x >= W - SIG_R - 1.5f;
        const bool outer = fabsf(sg.x - CORE_X) >= W / 2.0f - SIG_R - 1 - SPAWN_CORNER - 0.5f;
        edgesOk &= side || outer;
    }
    for (auto &sg : g._signals) sg.alive = false;
    snprintf(what, sizeof(what), "signals start at the sides or corners, %.0fpx out at the least", closest);
    check(what, edgesOk && closest >= 40, ok);

    // A new wave's stops: you stay on your ratio.
    g._stop = g._stopOf[2];                  // 1:2, the second of four
    g.startWave(5);                          // 1:3, 2:5, 1:2, 2:3, 3:4, 1:1
    check("new stops: you keep your ratio (1:2, now the third)",
          g._stopCount == 6 && g._stop == 2 && g._stops[g._stop] == 2, ok);
    g._round = ResonanceFluxGame::ROUND_PLAY;
    g._spawnAt = g_fakeMillis + 1000000;

    // The ratio hint: the nearest signal's stop lit amber on the dial,
    // wherever you are.
    place(g, r23, 1.0f, 20, 30);
    g._stop = 0;
    step(g, audio, canvas);
    const uint16_t *px = canvas.getBuffer();
    const int hx = g.dialX(g._stopOf[r23]), mine = g.dialX(g._stop);
    check("the nearest signal's stop is lit amber on the dial (and not yours)",
          px[(STRIP_Y + 8) * W + hx] == ArcadeConfig::COLOR_AMBER &&
          px[(STRIP_Y + 8) * W + mine] != ArcadeConfig::COLOR_AMBER, ok);

    // The pace through wave 20: capped speed, never more than four on the
    // scope, arrivals at least 2.5s apart, and even the nearest start
    // (40px) takes over 5s to reach you.
    bool paced = true;
    float slowest = 1e9f;
    for (int w = 1; w <= 20; ++w) {
        g._wave = w;
        const long gapMs = (long)SPAWN_FIRST_MS - (long)SPAWN_STEP_MS * (w - 1);
        const float secs = 40.0f / g.driftSpeed();
        slowest = fminf(slowest, secs);
        paced &= g.driftSpeed() <= DRIFT_MAX && g.maxOnScope() <= 4 &&
                 (gapMs > (long)SPAWN_MIN_MS ? gapMs : (long)SPAWN_MIN_MS) >= 2500 && secs > 5.0f;
    }
    g._wave = 20;
    snprintf(what, sizeof(what), "pace to wave 20: %.1fpx/s, %d at once, 40px in %.1fs at the least",
             g.driftSpeed(), g.maxOnScope(), slowest);
    check(what, paced, ok);
    g.startWave(5);
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
    hiscore::Table empty;
    hiscore::clear(empty);
    hiscore::save("resonance", empty);
    g.init(audio);
    for (int i = 0; i < 20; ++i) step(g, audio, canvas);
    check("the title waits for A", g._phaseState == ResonanceFluxGame::PHASE_ATTRACT, ok);
    InputState a{}; a.btnA = a.btnAPressed = true;
    step(g, audio, canvas, a);
    check("A starts a game", g._phaseState == ResonanceFluxGame::PHASE_PLAYING && g._wave == 1 && !g._test, ok);
    for (int i = 0; i < 300; ++i) step(g, audio, canvas, g.autopilot());
    g._score = 4321;
    g.onQuit(audio);
    g.onExit();
    check("onExit frees the scope", g._glow == nullptr, ok);
    g._scores.begin("resonance");
    check("Back mid-game: the score on the table", g._scores.best() == 4321, ok);
    g.init(audio);
    step(g, audio, canvas);
    check("init again: the title, the scope back", g._phaseState == ResonanceFluxGame::PHASE_ATTRACT && g._glow, ok);
    step(g, audio, canvas, a);
    check("a clean game: wave 1, no score, no static, no signals",
          g._wave == 1 && g._score == 0 && g._static == 0 && g.aliveCount() == 0, ok);
    printf("quit -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// No input: title, the how-to slides and the scores, then the demo:
// silent, the high score untouched, and back to the title.
static bool scenarioIdle(long frames) {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    FrameDumper dump("resonance_idle");
    g.init(audio);
    const long hs0 = g._scores.best();
    int demos = 0, ended = 0, slides = 0;
    long demoFrames = 0, shattered0 = g._statShatters;
    bool was = false, leftSilenced = false;
    int lastSlide = -1;
    for (long f = 0; f < frames; ++f) {
        InputState none{};
        g.update(canvas, none, audio);
        dump.maybeDump(f, canvas);
        leftSilenced |= audio._silenced;
        if (g._slide != lastSlide) { ++slides; lastSlide = g._slide; }
        const bool now = g._demo;
        if (now) ++demoFrames;
        if (!was && now) ++demos;
        if (was && !now) ++ended;
        was = now;
        g_fakeMillis += STEP_MS;
    }
    const bool ok = !leftSilenced && g._scores.best() == hs0 && demos > 0 && ended > 0 &&
                    slides >= 6 && g._phaseState == ResonanceFluxGame::PHASE_ATTRACT &&
                    g._statShatters > shattered0;
    printf("idle: %ld frames, slides seen %d, demos %d, ended %d, avg demo %.1fs, shattered in demos %ld, "
           "left silenced %d, high score touched %d -> %s\n",
           frames, slides, demos, ended, demos ? demoFrames * STEP_MS / 1000.0 / demos : 0.0,
           g._statShatters - shattered0, (int)leftSilenced, (int)(g._scores.best() != hs0), ok ? "PASS" : "FAIL");
    return ok;
}

// A mid-demo starts a clean real game.
static bool scenarioDemoExit() {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    g.init(audio);
    for (long f = 0; f < 100000 && !g._demo; ++f) step(g, audio, canvas);
    for (int f = 0; f < 300; ++f) step(g, audio, canvas);
    const bool demoGoing = g._demo;
    InputState a{}; a.btnA = a.btnAPressed = true;
    step(g, audio, canvas, a);
    const bool ok = demoGoing && !g._demo && g._phaseState == ResonanceFluxGame::PHASE_PLAYING && !g._test &&
                    g._wave == 1 && g._score == 0 && g._static == 0 && g.aliveCount() == 0 &&
                    g._round == ResonanceFluxGame::ROUND_INTRO && g._stopCount == 2 && !audio._silenced;
    printf("demoexit: a demo under way %d; A: wave 1, no score, no static, no signals, sound back -> %s\n",
           (int)demoGoing, ok ? "PASS" : "FAIL");
    return ok;
}

// The wave select: B held with A on the title opens it; the stick steps
// the wave; A starts a test run there that puts nothing on the table.
static bool scenarioPick() {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("pick:\n");
    FrameDumper dump("resonance_pick");
    long n = 0;
    auto frame = [&](InputState in = InputState{}) { step(g, audio, canvas, in); dump.maybeDump(n++, canvas); };
    // Landscape (rotation 1): screen right is joyDown, left joyUp, up joyLeft, down joyRight.
    auto push = [&](bool right, bool left, bool up, bool down) {
        InputState in{};
        in.joyDown = right; in.joyUp = left; in.joyLeft = up; in.joyRight = down;
        frame(in);
        frame();
    };
    hiscore::Table empty;
    hiscore::clear(empty);
    hiscore::save("resonance", empty);
    InputState ba{}; ba.btnB = true; ba.btnA = ba.btnAPressed = true;
    InputState a{}; a.btnA = a.btnAPressed = true;
    InputState b{}; b.btnB = b.btnBPressed = true;
    InputState stillB{}; stillB.btnB = true;
    g.init(audio);
    for (int f = 0; f < 10; ++f) frame();
    frame(ba);
    check("B+A on the title: the wave select, wave 1", g._phaseState == ResonanceFluxGame::PHASE_PICK && g._pick == 1, ok);
    frame(stillB);
    check("B still held from opening it doesn't close it", g._phaseState == ResonanceFluxGame::PHASE_PICK, ok);
    push(true, false, false, false);
    push(true, false, false, false);
    check("right twice: wave 3", g._pick == 3, ok);
    push(false, false, true, false);
    check("up: five on, wave 8", g._pick == 8, ok);
    push(false, false, false, true);
    push(false, false, false, true);
    check("down twice: round to wave 18", g._pick == 18, ok);
    push(false, true, false, false);
    check("left: wave 17", g._pick == 17, ok);
    frame(b);
    check("B: back to the title", g._phaseState == ResonanceFluxGame::PHASE_ATTRACT && !g._test, ok);
    frame(ba);
    frame();
    push(true, false, false, false);
    push(false, false, true, false);
    check("right, up: wave 7", g._pick == 7, ok);
    frame(a);
    check("A: a test run on 7, fresh, its six stops",
          g._phaseState == ResonanceFluxGame::PHASE_PLAYING && g._test && g._wave == 7 && g._score == 0 &&
          g._static == 0 && g._stopCount == 6, ok);
    g._score = 54320;
    g._static = 101;                        // over the top: the frame's decay leaves it at 100
    frame();
    check("test game over: no name entry", g._phaseState == ResonanceFluxGame::PHASE_GAMEOVER, ok);
    g._scores.begin("resonance");
    check("  and nothing on the table", g._scores.best() == 0, ok);
    for (unsigned long t = 0; t <= GAMEOVER_MIN_MS + STEP_MS; t += STEP_MS) frame();
    frame(a);
    check("A at its game over: wave 7 again, still a test",
          g._phaseState == ResonanceFluxGame::PHASE_PLAYING && g._test && g._wave == 7, ok);
    g._score = 43210;
    g.onQuit(audio);
    g._scores.begin("resonance");
    check("Back mid test run: nothing on the table", g._scores.best() == 0, ok);
    g.init(audio);
    frame();
    frame(a);
    check("plain A: a real game from wave 1", g._phaseState == ResonanceFluxGame::PHASE_PLAYING && !g._test &&
          g._wave == 1, ok);
    g.init(audio);
    frame();
    frame(ba);
    for (unsigned long t = 0; t <= PICK_TIMEOUT_MS + STEP_MS; t += STEP_MS) frame();
    check("left alone, the wave select goes back to the title", g._phaseState == ResonanceFluxGame::PHASE_ATTRACT, ok);
    printf("pick -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// The optional sounds: with no card, each event's tone, melody or nothing;
// with its file on the card, the file; with only the fallback file, that.
// And each event in play asks for the sound it should.
static bool scenarioSounds() {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("sounds:\n");
    startPlaying(g, audio, canvas);
    using G = ResonanceFluxGame;
    auto plays = [&](G::Sfx sfx, const char *want) {
        g.sfx(sfx);
        return !strcmp(g._lastSfx, want);
    };
    check("no card: shatter is a tone, wave clear a melody, the wave's start nothing",
          plays(G::SFX_SHATTER, "tone") && plays(G::SFX_CLEAR, "melody") && plays(G::SFX_WAVE, "none") &&
          plays(G::SFX_LOCK, "none") && plays(G::SFX_TITLE, "melody") && plays(G::SFX_OVER, "melody"), ok);
    g._fallbackOnCard[G::SFX_SHATTER] = true;
    check("explosion.wav on the card: the shatter uses it", plays(G::SFX_SHATTER, "/audio/explosion.wav"), ok);
    for (int i = 0; i < G::SFX_COUNT; ++i) g._sfxOnCard[i] = true;
    check("res_shatter.wav on the card: that instead", plays(G::SFX_SHATTER, "/audio/res_shatter.wav"), ok);

    // Each event, every file on the card.
    const int r12 = 2;
    place(g, r12, 1.0f, 20, 30);
    g._stop = g._stopOf[r12]; g._phase = 1.0f + 2 * PHASE_TOL;
    step(g, audio, canvas);
    InputState a{}; a.btnA = a.btnAPressed = true;
    step(g, audio, canvas, a);
    check("a misfire: res_miss.wav", !strcmp(g._lastSfx, "/audio/res_miss.wav"), ok);
    g._phase = 1.0f;
    step(g, audio, canvas);
    check("resonance begins: res_lock.wav", !strcmp(g._lastSfx, "/audio/res_lock.wav"), ok);
    for (int i = 0; i < 15; ++i) { g._phase = 1.0f; step(g, audio, canvas); }
    g._phase = 1.0f;
    step(g, audio, canvas, a);
    check("a shatter: res_shatter.wav", !strcmp(g._lastSfx, "/audio/res_shatter.wav"), ok);
    auto &s = place(g, r12, 0.5f, CORE_X + CORE_R + SIG_R, CORE_Y);
    s.speed = 20;
    for (int i = 0; i < 20 && g._signals[0].alive; ++i) step(g, audio, canvas);
    check("a signal reaching you: res_hit.wav", !strcmp(g._lastSfx, "/audio/res_hit.wav"), ok);
    InputState b{}; b.btnB = b.btnBPressed = true;
    step(g, audio, canvas, b);
    check("dampen: res_damp.wav", !strcmp(g._lastSfx, "/audio/res_damp.wav"), ok);
    g._toSpawn = 0;
    for (auto &sg : g._signals) sg.alive = false;
    step(g, audio, canvas);
    check("the wave cleared: res_clear.wav", !strcmp(g._lastSfx, "/audio/res_clear.wav"), ok);
    for (unsigned long t = 0; t <= WAVE_CLEAR_MS + WAVE_INTRO_MS + 2 * STEP_MS; t += STEP_MS) {
        g._spawnAt = g_fakeMillis + 1000000;
        step(g, audio, canvas);
    }
    check("the next wave starts: res_wave.wav", !strcmp(g._lastSfx, "/audio/res_wave.wav"), ok);
    g._static = 101;
    step(g, audio, canvas);
    check("overload: res_over.wav", !strcmp(g._lastSfx, "/audio/res_over.wav"), ok);
    printf("sounds -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// The options: B pressed and let go on the title opens them (B held with A
// is still the wave select); the stick picks and changes; each is saved
// and comes back after init; and each does what it says. And the how-to
// slides clear the bands above and below the scope.
static bool scenarioOptions() {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("options:\n");
    using G = ResonanceFluxGame;
    auto frame = [&](InputState in = InputState{}) { step(g, audio, canvas, in); };
    // Landscape: screen up is joyLeft, down joyRight, left joyUp, right joyDown.
    auto push = [&](bool up, bool down, bool left, bool right) {
        InputState in{};
        in.joyLeft = up; in.joyRight = down; in.joyUp = left; in.joyDown = right;
        frame(in);
        frame();
    };
    InputState bDown{}; bDown.btnB = bDown.btnBPressed = true;
    InputState bHeld{}; bHeld.btnB = true;
    InputState bUp{}; bUp.btnBReleased = true;
    InputState ba{}; ba.btnB = true; ba.btnA = ba.btnAPressed = true;

    // The slides' bands: a white screen, then each slide; the bands black.
    g.init(audio);
    bool bands = true;
    for (int page = 0; page < 3; ++page) {
        canvas.fillScreen(ArcadeConfig::COLOR_WHITE);
        g.renderInfo(canvas, page);
        const uint16_t *px = canvas.getBuffer();
        for (int y = 0; y < H; ++y) {
            if (!(y < METER_Y || y >= STRIP_Y)) continue;
            for (int x = 0; x < W; ++x) bands &= px[y * W + x] != ArcadeConfig::COLOR_WHITE;
        }
    }
    check("the how-to slides clear the bands above and below the scope", bands, ok);

    frame();
    frame(bDown); frame(bHeld); frame(bUp);
    check("B pressed and let go on the title: the options", g._phaseState == G::PHASE_OPTIONS, ok);
    frame(bDown);
    check("B there: back to the title", g._phaseState == G::PHASE_ATTRACT, ok);
    frame(bUp);
    check("  and letting it go doesn't open them again", g._phaseState == G::PHASE_ATTRACT, ok);
    frame(bDown); frame(ba);
    check("B held with A: still the wave select", g._phaseState == G::PHASE_PICK, ok);
    frame(bUp);
    check("  and letting B go there doesn't open the options", g._phaseState == G::PHASE_PICK, ok);
    InputState b{}; b.btnB = b.btnBPressed = true;
    frame(b); frame(bUp);

    frame(bDown); frame(bUp);
    push(false, false, false, true);
    check("right on RATIO HINT: off", !g._opt.hint && g._optRow == G::OPT_HINT, ok);
    push(false, true, false, false);
    push(false, false, false, true);
    check("down, right: NOTES off", !g._opt.notes && g._optRow == G::OPT_NOTES, ok);
    push(false, true, false, false);
    push(false, false, false, true);
    check("down, right: PACE fast", g._opt.pace == G::PACE_FAST, ok);
    push(false, false, false, true);
    push(false, false, true, false);
    check("right then left round: calm, then back to fast", g._opt.pace == G::PACE_FAST, ok);
    push(false, true, false, false);
    push(false, false, false, true);
    check("down, right: BOSSES off", !g._opt.bosses && g._optRow == G::OPT_BOSSES, ok);
    push(false, false, false, true);
    check("  and right again: on", g._opt.bosses, ok);
    push(false, true, false, false);
    InputState a{}; a.btnA = a.btnAPressed = true;
    frame(a); frame();
    check("A on DEBUG LINE: off", !g._opt.debug, ok);
    char what[96];
    snprintf(what, sizeof(what), "hint and notes off, fast: score x%.4f", g.scoreMult());
    check(what, fabsf(g.scoreMult() - MULT_NO_HINT * MULT_NO_NOTES * MULT_FAST) < 1e-4f, ok);
    g.init(audio);
    check("saved: after init, still off, off, fast, off",
          !g._opt.hint && !g._opt.notes && g._opt.pace == G::PACE_FAST && !g._opt.debug, ok);

    // What each does, in play.
    startPlaying(g, audio, canvas);
    const int r12 = 2;
    place(g, r12, 1.0f, 20, 30);
    g._stop = 0;
    step(g, audio, canvas);
    const uint16_t *px = canvas.getBuffer();
    check("hint off: no amber on the dial", px[(STRIP_Y + 8) * W + g.dialX(g._stopOf[r12])] != ArcadeConfig::COLOR_AMBER, ok);
    check("notes off: neither note sounds", g._youLevel == 0 && g._targetLevel == 0, ok);
    g._wave = 1;
    check("fast: wave 1 drifts 1.25x", fabsf(g.driftSpeed() - DRIFT_START * PACE_FAST_SPEED) < 1e-4f, ok);
    // A shatter just off the core's ring: 100, its small distance bonus,
    // then the multiplier.
    auto &sg = place(g, r12, 1.0f, CORE_X + CORE_R + SIG_R, CORE_Y);
    g._stop = g._stopOf[r12]; g._phase = 1.0f;
    g._score = 0;
    step(g, audio, canvas);
    g._phase = 1.0f;
    step(g, audio, canvas, a);
    const float far = (CORE_R + SIG_R - CORE_R) / (70.0f - CORE_R);
    const long want = (long)(PTS_TONE * (1.0f + far) * g.scoreMult());
    snprintf(what, sizeof(what), "a shatter scores %ld (100 x %.2f x %.4f = %ld)", g._score, 1 + far, g.scoreMult(), want);
    check(what, !sg.alive && labs(g._score - want) <= 1, ok);

    // Back to the defaults, for whatever runs next.
    g._opt = G::Options{};
    g.saveOptions();
    g.loadOptions();
    check("defaults back: hint, notes, normal, x1", g._opt.hint && g._opt.notes && g._opt.pace == G::PACE_NORMAL &&
          fabsf(g.scoreMult() - 1) < 1e-6f, ok);
    printf("options -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// The Chord: every fifth wave with bosses on, none with them off; strip
// the brightest layer and it jumps sides; fire at a dimmer one and the
// stripped ones come back; its escorts; drift and morph; and the
// autopilot brings down each kind (waves 5, 10, 15, 20), static pinned.
static bool scenarioChord() {
    static ResonanceFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("chord:\n");
    using G = ResonanceFluxGame;
    startPlaying(g, audio, canvas);
    g.startWave(4);
    check("wave 4: no Chord", !g._chord.active && g._toSpawn > 0, ok);
    g.startWave(5);
    check("wave 5: a Chord, three different ratios, no signals of its own wave",
          g._chord.active && g._toSpawn == 0 && g._chord.ratio[0] != g._chord.ratio[1] &&
          g._chord.ratio[1] != g._chord.ratio[2] && g._chord.ratio[0] != g._chord.ratio[2], ok);
    g._opt.bosses = false;
    g.startWave(5);
    check("bosses off: wave 5 is an ordinary wave", !g._chord.active && g._toSpawn > 0, ok);
    g._opt.bosses = true;

    // Strip the brightest; then fire at a dimmer one.
    g.startWave(5);
    g._round = G::ROUND_PLAY;
    for (auto &sg : g._signals) sg.alive = false;
    g._chord.sendAt = g_fakeMillis + 1000000;
    g._fireReadyAt = 0;
    auto tuneTo = [&](int layer) {
        g._stop = g._stopOf[g._chord.ratio[layer]];
        g._phase = g._chord.phase[layer];
        g._jitP = 0;
    };
    InputState a{}; a.btnA = a.btnAPressed = true;
    const bool left0 = g._chord.left;
    const long score0 = g._score;
    tuneTo(0);
    step(g, audio, canvas);
    tuneTo(0);
    step(g, audio, canvas, a);
    check("the brightest in resonance, fired: stripped, scored, to the other side",
          g._chord.stripped == 1 && g._score > score0 && g._chord.left != left0, ok);
    g._static = 0;
    tuneTo(2);
    step(g, audio, canvas);
    tuneTo(2);
    step(g, audio, canvas, a);
    check("a dimmer layer fired at: the stripped one back, and static",
          g._chord.stripped == 0 && g._static >= STATIC_MISFIRE - 0.1f, ok);

    // Escorts: sent while it's up, a couple at a time.
    g._chord.sendAt = g_fakeMillis;
    int most = 0;
    for (int f = 0; f < 900; ++f) {
        g._static = 0;
        step(g, audio, canvas);
        most = std::max(most, g.aliveCount());
    }
    char what[96];
    snprintf(what, sizeof(what), "escorts sent while it's up, %d at most", most);
    check(what, most >= 1 && most <= CHORD_ESCORTS_MAX, ok);

    // Drift from wave 10, morph from 15.
    g.startWave(10);
    g._round = G::ROUND_PLAY;
    float p0 = g._chord.phase[0];
    for (int f = 0; f < 30; ++f) step(g, audio, canvas);
    check("wave 10: its layers drift in phase", fabsf(g._chord.phase[0] - p0) > 0.01f && !g._chord.morph, ok);
    g.startWave(15);
    g._round = G::ROUND_PLAY;
    const int mid0 = g._chord.ratio[1];
    for (unsigned long t = 0; t <= CHORD_MORPH_MS + STEP_MS; t += STEP_MS) step(g, audio, canvas);
    check("wave 15: its middle layer swaps ratio", g._chord.morph && g._chord.ratio[1] != mid0, ok);

    // The autopilot against each kind.
    for (int w : { 5, 10, 15, 20 }) {
        g.startWave(w);
        g._round = G::ROUND_PLAY;
        g._chord.startedAt = g_fakeMillis;
        const long chords0 = g._statChords;
        long f = 0;
        for (; f < 6000 && g._chord.active; ++f) {
            g._static = 0;
            step(g, audio, canvas, g.autopilot());
        }
        for (long k = 0; k < 200 && g._round == G::ROUND_PLAY; ++k) step(g, audio, canvas);
        snprintf(what, sizeof(what), "wave %d's Chord brought down in %.1fs, the wave cleared", w, f * STEP_MS / 1000.0f);
        check(what, g._statChords == chords0 + 1 && g._round == G::ROUND_CLEAR, ok);
    }
    printf("chord -> %s\n", ok ? "PASS" : "FAIL");
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
    if (all || !strcmp(which, "idle"))     ok &= scenarioIdle(frames ? frames : 3000);
    if (all || !strcmp(which, "demoexit")) ok &= scenarioDemoExit();
    if (all || !strcmp(which, "pick"))     ok &= scenarioPick();
    if (all || !strcmp(which, "sounds"))   ok &= scenarioSounds();
    if (all || !strcmp(which, "options"))  ok &= scenarioOptions();
    if (all || !strcmp(which, "chord"))    ok &= scenarioChord();
    return ok ? 0 : 1;
}
