// Host harness for Brick Flux: the real game logic against a fake clock and
// seeded RNG, like the other harnesses. Scenarios:
//
//   play N      the autopilot plays real games for N frames (restarting at
//               game over); checks no ball ever sits inside a brick or
//               leaves the field, the score never drops within a game, and
//               levels get cleared and smashes made
//   wall        the advancing wall: a breakable brick past the line costs
//               exactly one life and pushes the formation back; steel there
//               just shatters
//   smash       Flux Smash timing: releases 20, 100 and 200ms before the
//               ball meets the bat, and 30ms after: Perfect, Good, none
//               (meter kept), Good
//   tunnel      balls at top speed and the longest frame, at angles from
//               20 to 160 degrees, at a single row of steel: none gets past
//   idle N      the attract cycle into its demo: silent, high score
//               untouched, and the demo ends back at the title
//   demoexit    A mid-demo starts a clean real game
//   all         everything (the default)
//
// DUMP_AT=frame,frame writes those frames of `play` as brick_<frame>.ppm.
// The game includes the real (inert) AudioEngine, so src/ comes ahead of
// the stubs when building (see build.sh).

#include <Adafruit_ST7735.h>
#include "harness_common.h"

#define private public
#include "games/BrickFlux/BrickFluxGame.h"
#undef private

using namespace brickflux;

static const unsigned long STEP_MS = 33;   // ~30fps, the game's target

static bool ballsSound(const BrickFluxGame &g, const char* &why) {
    for (const auto &b : g._balls) {
        if (!b.active || b.held) continue;
        int r, c;
        if (!b.pierce && g._board.solidAt(b.x, b.y, BALL_HALF, r, c)) { why = "ball inside a brick"; return false; }
        if (b.x < FIELD_L || b.x > FIELD_R || b.y < FIELD_T) { why = "ball outside the field"; return false; }
    }
    return true;
}

// The autopilot playing for real: A on the title, its input in play, and A
// now and then to get through the name entry and game over.
static bool scenarioPlay(long frames) {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    FrameDumper dump("brick");
    g.init(audio);
    InputState a{}; a.btnA = a.btnAPressed = true;
    g.update(canvas, a, audio);
    g_fakeMillis += STEP_MS;
    long games = 0, worstLevel = 1, bestScore = 0, prevScore = 0;
    const char* why = nullptr;
    bool ok = g._phase == BrickFluxGame::PHASE_PLAYING, tick = false;
    for (long f = 0; f < frames && ok; ++f) {
        InputState in{};
        if (g._phase == BrickFluxGame::PHASE_PLAYING) in = g.autopilot();
        else { tick = !tick; in.btnA = in.btnAPressed = tick && (f % 40 == 0); }
        const bool wasPlaying = g._phase == BrickFluxGame::PHASE_PLAYING;
        g.update(canvas, in, audio);
        dump.maybeDump(f, canvas);
        if (g._phase == BrickFluxGame::PHASE_PLAYING) {
            if (!wasPlaying) { ++games; prevScore = 0; }
            if (g._score < prevScore) { why = "score went down"; ok = false; }
            prevScore = g._score;
            if (g._level > worstLevel) worstLevel = g._level;
            if (g._score > bestScore) bestScore = g._score;
            if (!ballsSound(g, why)) ok = false;
        }
        g_fakeMillis += STEP_MS;
    }
    ok = ok && g._statCleared > 0 && g._statSmashes > 0;
    printf("play: %ld frames, %ld restarts, reached level %ld, best %ld, cleared %ld, smashes %ld (%ld perfect), "
           "capsules %ld, wall steps %ld, lives to the wall %ld%s%s -> %s\n",
           frames, games, worstLevel, bestScore, g._statCleared, g._statSmashes, g._statPerfects,
           g._statCapsules, g._statSteps, g._statWallLives, why ? ": " : "", why ? why : "", ok ? "PASS" : "FAIL");
    return ok;
}

// A game in play with the ball served, for the scripted checks.
static void startPlaying(BrickFluxGame &g, AudioEngine &audio, GFXcanvas16 &canvas) {
    g.init(audio);
    InputState a{}; a.btnA = a.btnAPressed = true;
    g.update(canvas, a, audio);
    g._round = BrickFluxGame::ROUND_PLAY;
    g._serving = false;
    g._aHeld = false;
}

static bool scenarioWall() {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    startPlaying(g, audio, canvas);
    bool ok = true;

    // Level 1's layout, lowered until the next step takes its bottom row past
    // the line. The ball's kept well clear, up by the ceiling going sideways.
    g._board.load(0);
    const float bottom0 = g._board.lowestBottom();
    while (g._board.lowestBottom() + CELL_H <= DANGER_Y) g._board.stepDown();
    const float top0 = g._board.top();
    const int bricks0 = g._board.remaining(), lives0 = g._lives;
    g._balls[0] = Ball{};
    g._balls[0].active = true; g._balls[0].x = 64; g._balls[0].y = 140; g._balls[0].dx = 0; g._balls[0].dy = 1;
    g._wallElapsed = g.wallStepMs() - 5;
    g._now = millis();
    g.updateWall();
    const int crossedRows = 1;
    const bool lifeOk = g._lives == lives0 - 1 && g._round == BrickFluxGame::ROUND_LOST;
    const bool pushedOk = fabsf(g._board.top() - (top0 + CELL_H - WALL_PUSHBACK_ROWS * CELL_H)) < 0.01f;
    const bool goneOk = g._board.remaining() == bricks0 - 11 * crossedRows;   // the bottom row: 11 bricks
    printf("wall: a brick past the line: lives %d->%d (%s), pushed back %s, its row gone %s (%d->%d bricks)\n",
           lives0, g._lives, lifeOk ? "ok" : "BAD", pushedOk ? "ok" : "BAD", goneOk ? "ok" : "BAD",
           bricks0, g._board.remaining());
    ok &= lifeOk && pushedOk && goneOk;
    (void)bottom0;

    // Steel only at the bottom: it shatters, and nothing's lost.
    startPlaying(g, audio, canvas);
    g._board.load(0);
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            if (g._board._cells[r][c].kind != BrickBoard::EMPTY && r == 5)
                g._board._cells[r][c] = BrickBoard::Cell{ BrickBoard::STEEL, 1, 1 };
    while (g._board.cellY(5) + CELL_H + CELL_H <= DANGER_Y) g._board.stepDown();
    const int lives1 = g._lives, bricks1 = g._board.remaining();
    g._balls[0] = Ball{};
    g._balls[0].active = true; g._balls[0].x = 64; g._balls[0].y = 140; g._balls[0].dx = 0; g._balls[0].dy = 1;
    g._wallElapsed = g.wallStepMs() - 5;
    g._now = millis();
    g.updateWall();
    int steelLeft = 0;
    for (int c = 0; c < COLS; ++c) steelLeft += g._board._cells[5][c].kind == BrickBoard::STEEL;
    const bool steelOk = g._lives == lives1 && g._round == BrickFluxGame::ROUND_PLAY && steelLeft == 0 &&
                         g._board.remaining() == bricks1;
    printf("wall: steel past the line shatters, no life lost -> %s\n", steelOk ? "ok" : "BAD");
    ok &= steelOk;

    // The timing: level 1 steps every 14s, level 20 every 7s, loops quicker.
    g._level = 1;  const unsigned long s1 = g.wallStepMs();
    g._level = 20; const unsigned long s20 = g.wallStepMs();
    g._level = 21; const unsigned long s21 = g.wallStepMs();
    g._level = 100; const unsigned long s100 = g.wallStepMs();
    const bool timingOk = s1 == 14000 && s20 == 7000 && s21 == 11900 && s100 >= WALL_STEP_FLOOR;
    printf("wall: step every %lums (level 1), %lums (20), %lums (21), %lums (100) -> %s\n",
           s1, s20, s21, s100, timingOk ? "ok" : "BAD");
    ok &= timingOk;
    printf("wall -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// One smash attempt: a ball dropping straight onto the bat's centre, A held
// long enough to charge, released `lead` ms before it arrives (negative:
// after). What came of it, by the stats and the meter.
static void tryRelease(long lead, bool &smashed, bool &perfect, int &meterAfter) {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    startPlaying(g, audio, canvas);
    g._meter = METER_FULL;
    g._batX = 64;
    g._tilt = 0;
    // 150px/s-ish fall from high up, so the timing is easy to reckon.
    Ball &b = g._balls[0];
    b = Ball{};
    b.active = true; b.x = 64; b.y = 60; b.dx = 0; b.dy = 1;
    const float speed = g.ballSpeed();
    const long arriveMs = (long)((BAT_Y - BALL_HALF - b.y) / speed * 1000.0f);
    const long releaseAt = arriveMs - lead;
    const long smashes0 = g._statSmashes, perfects0 = g._statPerfects;
    const unsigned long t0 = g_fakeMillis;
    g._lastFrameMs = t0;
    for (long t = 10; t <= arriveMs + 300; t += 10) {
        g_fakeMillis = t0 + t;
        InputState in{};
        in.btnA = t < releaseAt;
        g.update(canvas, in, audio);
    }
    smashed = g._statSmashes > smashes0;
    perfect = g._statPerfects > perfects0;
    meterAfter = g._meter;
}

static bool scenarioSmash() {
    struct Case { long lead; bool smash, perfect; const char* what; };
    const Case cases[] = {
        {  20, true,  true,  "20ms before: Perfect" },
        { 100, true,  false, "100ms before: Good" },
        { 200, false, false, "200ms before: missed, meter kept" },
        { -30, true,  false, "30ms after: Good (late)" },
    };
    bool ok = true;
    for (const auto &c : cases) {
        bool s, p; int m;
        tryRelease(c.lead, s, p, m);
        const bool meterOk = c.smash ? m < METER_FULL : m == METER_FULL;
        const bool pass = s == c.smash && p == c.perfect && meterOk;
        printf("smash: %-34s smashed %d perfect %d meter %d -> %s\n", c.what, s, p, m, pass ? "ok" : "BAD");
        ok &= pass;
    }
    printf("smash -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// A row of steel with the field above it empty: a ball fired up at it at
// the top speed, frames at the 50ms cap, must always come back down.
static bool scenarioTunnel() {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    int through = 0, tries = 0;
    for (int deg = 20; deg <= 160; deg += 7) {
        for (int x0 = 20; x0 <= 108; x0 += 11) {
            startPlaying(g, audio, canvas);
            for (int r = 0; r < ROWS; ++r)
                for (int c = 0; c < COLS; ++c)
                    g._board._cells[r][c] = BrickBoard::Cell{ r == 6 ? BrickBoard::STEEL : BrickBoard::EMPTY, 1, 1 };
            g._board._cells[0][0] = BrickBoard::Cell{ BrickBoard::NEUTRAL, 1, 1 };   // so it isn't clear
            const float rowTop = g._board.cellY(6);
            g._level = 19;                                   // near the top speed
            Ball &b = g._balls[0];
            b = Ball{};
            b.active = true; b.x = (float)x0; b.y = 120;
            dirFromAngle((float)deg, b.dx, b.dy);
            g._lastFrameMs = g_fakeMillis;
            bool past = false;
            for (int f = 0; f < 40 && b.active; ++f) {
                g_fakeMillis += 50;
                InputState none{};
                g.update(canvas, none, audio);
                if (b.active && b.y + BALL_HALF <= rowTop) past = true;
            }
            ++tries;
            through += past;
        }
    }
    const bool ok = through == 0;
    printf("tunnel: %d shots at %.0fpx/s, 50ms frames: %d got through the steel -> %s\n",
           tries, g.ballSpeed(), through, ok ? "PASS" : "FAIL");
    return ok;
}

static bool scenarioIdle(long frames) {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    g.init(audio);
    const long hs0 = g._scores.best();
    int demos = 0, ended = 0;
    long demoFrames = 0, cleared0 = g._statCleared, smashes0 = g._statSmashes;
    bool was = false, leftSilenced = false;
    for (long f = 0; f < frames; ++f) {
        InputState none{};
        g.update(canvas, none, audio);
        leftSilenced |= audio._silenced;
        const bool now = g._demo;
        if (now) ++demoFrames;
        if (!was && now) ++demos;
        if (was && !now) ++ended;
        was = now;
        g_fakeMillis += STEP_MS;
    }
    const bool ok = !leftSilenced && g._scores.best() == hs0 && demos > 0 && ended > 0 &&
                    g._phase == BrickFluxGame::PHASE_ATTRACT;
    printf("idle: %ld frames, demos %d, ended %d, avg demo %.1fs, levels cleared in demos %ld, smashes %ld, "
           "left silenced %d, high score touched %d -> %s\n",
           frames, demos, ended, demos ? demoFrames * STEP_MS / 1000.0 / demos : 0.0,
           g._statCleared - cleared0, g._statSmashes - smashes0, (int)leftSilenced,
           (int)(g._scores.best() != hs0), ok ? "PASS" : "FAIL");
    return ok;
}

static bool scenarioDemoExit() {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    g.init(audio);
    for (long f = 0; f < 100000 && !g._demo; ++f) { InputState n{}; g.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
    for (int f = 0; f < 300; ++f) { InputState n{}; g.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
    const long demoScore = g._score;
    InputState a{}; a.btnA = a.btnAPressed = true;
    g.update(canvas, a, audio);
    g_fakeMillis += STEP_MS;
    // The A that started it mustn't serve, and the game is fresh.
    InputState n{}; n.btnA = true;
    for (int f = 0; f < 60; ++f) { g.update(canvas, n, audio); g_fakeMillis += STEP_MS; }
    const bool ok = !g._demo && g._phase == BrickFluxGame::PHASE_PLAYING && g._level == 1 && g._score == 0 &&
                    g._lives == LIVES && g._serving && !audio._silenced;
    printf("demoexit: demo score was %ld; now phase %d level %d score %ld lives %d serving %d -> %s\n",
           demoScore, (int)g._phase, g._level, g._score, g._lives, (int)g._serving, ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "all";
    const long frames = argc > 2 ? atol(argv[2]) : 0;
    const bool all = !strcmp(which, "all");
    bool ok = true;
    if (all || !strcmp(which, "play"))     ok &= scenarioPlay(frames ? frames : 40000);
    if (all || !strcmp(which, "wall"))     ok &= scenarioWall();
    if (all || !strcmp(which, "smash"))    ok &= scenarioSmash();
    if (all || !strcmp(which, "tunnel"))   ok &= scenarioTunnel();
    if (all || !strcmp(which, "idle"))     ok &= scenarioIdle(frames ? frames : 6000);
    if (all || !strcmp(which, "demoexit")) ok &= scenarioDemoExit();
    return ok ? 0 : 1;
}
