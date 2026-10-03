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
//   polarity    the ball breaks its own colour and neutral bricks, bounces
//               off the other colour (breaking the chain), takes the bat's
//               colour at the bat; the chain multiplies; B's cooldown;
//               smashes and lasers break either colour
//   living      guns fire only with a clear line down; bolts absorbed in
//               your colour, stunning in the other; magnets bend the ball;
//               portals carry it to their partner; the third spark is a life
//   levels [N]  the autopilot, lives pinned, plays every regular level of
//               the first loop: each must be cleared within N frames
//   boss [N]    the same for each of the four bosses
//   idle N      the attract cycle into its demo: silent, high score
//               untouched, and the demo ends back at the title
//   demoexit    A mid-demo starts a clean real game
//   pick        the stage-select cheat: B held with A on the title opens
//               it, the stick steps the level, A starts a test run there
//               that puts nothing on the table; plain A still starts level 1
//   all         everything (the default)
//
// DUMP_AT=frame,frame writes those frames of `play` as brick_<frame>.ppm,
// and of each level in `levels` and `boss` as brick_L<level>_<frame>.ppm.
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

// An empty grid, for the scripted checks below.
static void clearGrid(BrickFluxGame &g) {
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) g._board.at(r, c) = BrickBoard::Cell{ BrickBoard::EMPTY, 0, 0, POL_NONE, 0, 0 };
    g._board.at(0, 0) = BrickBoard::Cell{ BrickBoard::STEEL, 1, 1, POL_NONE, 0, 0 };
}
static Ball &soloBall(BrickFluxGame &g, float x, float y, float deg, uint8_t pol) {
    for (auto &b : g._balls) b = Ball{};
    Ball &b = g._balls[0];
    b.active = true; b.x = x; b.y = y; b.pol = pol;
    dirFromAngle(deg, b.dx, b.dy);
    return b;
}
static bool check(const char* what, bool cond, bool &ok) {
    printf("  %-58s %s\n", what, cond ? "ok" : "BAD");
    ok &= cond;
    return cond;
}

static bool scenarioPolarity() {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("polarity:\n");
    startPlaying(g, audio, canvas);
    clearGrid(g);
    g._now = millis();
    const int r = 8, c = 7;
    const float cx = BrickBoard::cellX(c) + CELL_W * 0.5f, below = g._board.cellY(r) + CELL_H + 4;

    // A cyan ball up into a magenta brick: it bounces, the brick stays.
    g._board.at(r, c) = BrickBoard::Cell{ BrickBoard::NEUTRAL, 1, 1, POL_MAGENTA, 0, 0 };
    g._chain = 4;
    long mm0 = g._statMismatches;
    Ball &b = soloBall(g, cx, below, 90, POL_CYAN);
    g.moveBall(b, 8);
    check("cyan ball off a magenta brick: bounces, brick intact", b.dy > 0 && g._board.occupied(r, c), ok);
    check("...and the chain's broken", g._chain == 1 && g._statMismatches == mm0 + 1, ok);

    // Cyan bricks broken in a row: x1, x2, x3.
    long gains[3];
    for (int i = 0; i < 3; ++i) {
        g._board.at(r, c) = BrickBoard::Cell{ BrickBoard::NEUTRAL, 1, 1, POL_CYAN, 0, 0 };
        Ball &bb = soloBall(g, cx, below, 90, POL_CYAN);
        const long s0 = g._score;
        g.moveBall(bb, 8);
        gains[i] = g._score - s0;
    }
    check("three cyan bricks in a row score 20, 40, 60", gains[0] == 20 && gains[1] == 40 && gains[2] == 60, ok);
    // Neutral: any colour, chain untouched.
    g._board.at(r, c) = BrickBoard::Cell{ BrickBoard::NEUTRAL, 1, 1, POL_NONE, 0, 0 };
    Ball &bn = soloBall(g, cx, below, 90, POL_MAGENTA);
    const int chain0 = g._chain;
    g.moveBall(bn, 8);
    check("a neutral brick breaks to either colour, chain kept", !g._board.occupied(r, c) && g._chain == chain0, ok);
    // A smash and a laser break the other colour.
    g._board.at(r, c) = BrickBoard::Cell{ BrickBoard::HARD, 2, 2, POL_MAGENTA, 0, 0 };
    Ball &bs = soloBall(g, cx, below, 90, POL_CYAN);
    bs.pierce = true;
    g.moveBall(bs, 8);
    check("a cyan smash breaks a hard magenta brick outright", !g._board.occupied(r, c), ok);
    g._board.at(r, c) = BrickBoard::Cell{ BrickBoard::NEUTRAL, 1, 1, POL_MAGENTA, 0, 0 };
    g.hitCell(r, c, nullptr, false, 1, POL_ANY);
    check("a laser breaks a magenta brick", !g._board.occupied(r, c), ok);

    // The ball takes the bat's colour at the bat.
    g._batPol = POL_MAGENTA;
    g._batX = 64;
    Ball &bb = soloBall(g, 64, BAT_Y - 6, -90, POL_CYAN);
    g.moveBall(bb, 8);
    check("a cyan ball off a magenta bat goes up magenta", bb.dy < 0 && bb.pol == POL_MAGENTA, ok);

    // B: a press swaps at once; another within the cooldown doesn't.
    g._batPol = POL_CYAN;
    g._bHeld = false;
    g._now = millis() + 1000;
    InputState press{}; press.btnB = true;
    InputState up{};
    g.updateButtonB(press);
    const bool first = g._batPol == POL_MAGENTA;
    g.updateButtonB(up);
    g._now += 100;
    g.updateButtonB(press);
    const bool blocked = g._batPol == POL_MAGENTA;
    g.updateButtonB(up);
    g._now += 300;
    g.updateButtonB(press);
    check("B swaps on the press, not again within 250ms, then again", first && blocked && g._batPol == POL_CYAN, ok);
    printf("polarity -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool scenarioLiving() {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("living:\n");
    startPlaying(g, audio, canvas);
    for (auto &b : g._balls) b = Ball{};
    clearGrid(g);
    g._now = millis();
    g._dt = 0.033f;

    // Guns: a clear line down fires; a brick under it keeps it quiet.
    auto bolts = [&]() { int n = 0; for (auto &b : g._bolts) n += b.active; return n; };
    g._board.at(3, 7) = BrickBoard::Cell{ BrickBoard::GUN, 2, 2, POL_MAGENTA, 0, 1 };
    g._board.at(6, 7) = BrickBoard::Cell{ BrickBoard::NEUTRAL, 1, 1, POL_NONE, 0, 0 };
    g.updateGuns();
    check("a buried gun doesn't fire", bolts() == 0, ok);
    g._board.at(6, 7) = BrickBoard::Cell{ BrickBoard::EMPTY, 0, 0, POL_NONE, 0, 0 };
    g._board.at(3, 7).timer = 1;
    g.updateGuns();
    check("a gun with a clear line fires a bolt of its colour", bolts() == 1 && g._bolts[0].pol == POL_MAGENTA, ok);

    // Bolts at the bat.
    for (auto &b : g._bolts) b.active = false;
    g._batX = 64; g._batPol = POL_MAGENTA; g._meter = 0; g._stunUntil = 0;
    g.fireBolt(64, BAT_Y - 2, 0, BOLT_SPEED, POL_MAGENTA);
    g.updateBolts();
    check("a bolt of the bat's colour is absorbed: +2 meter", g._meter == ABSORB_METER && g._stunUntil == 0, ok);
    g.fireBolt(64, BAT_Y - 2, 0, BOLT_SPEED, POL_CYAN);
    g.updateBolts();
    const float x0 = g._batX;
    InputState right{}; right.joyX = 1.0f;
    g.updateBat(right);
    check("one of the other colour stuns it: the bat won't move", g._stunUntil > g._now && g._batX == x0, ok);
    g._now += STUN_MS + 1;
    g.updateBat(right);
    check("...for STUN_MS", g._batX > x0, ok);

    // A magnet bends a passing ball towards it.
    clearGrid(g);
    g._board.at(6, 7) = BrickBoard::Cell{ BrickBoard::MAGNET, 2, 2, POL_NONE, 0, 0 };
    const float mx = BrickBoard::cellX(7) + 4, my = g._board.cellY(6) + 2.5f;
    Ball &b = soloBall(g, mx + 14, my + 20, 90, POL_CYAN);    // straight up, passing to its right
    for (int i = 0; i < 10; ++i) g.applyMagnets(b);
    check("a magnet turns a ball passing it towards it", b.dx < -0.05f, ok);

    // A portal carries the ball to its partner.
    g._board.load(layoutForLevel(11));               // Wormhole
    int pr = -1, pc = -1;
    for (int r = ROWS - 1; r >= 0 && pr < 0; --r)
        for (int c = 0; c < COLS; ++c)
            if (g._board.cell(r, c).kind == BrickBoard::PORTAL) { pr = r; pc = c; break; }
    const long tp0 = g._statTeleports;
    Ball &bp = soloBall(g, BrickBoard::cellX(pc) + 4, g._board.cellY(pr) + CELL_H + 2, 90, POL_CYAN);
    g.moveBall(bp, 6);
    check("into the low portal, out above the steel", g._statTeleports == tp0 + 1 && bp.y < g._board.cellY(3), ok);

    // Sparks: the third caught in a level is a life, once.
    g._lives = 2; g._sparksCaught = 0; g._sparkLife = false;
    int livesAfter[4];
    for (int i = 0; i < 4; ++i) {
        g.dropSpark(64, BAT_Y - 1);
        g._sparks[0].at = g._now;                    // no wobble
        g.updateSparks();
        livesAfter[i] = g._lives;
    }
    check("third spark gives a life, the fourth doesn't", livesAfter[1] == 2 && livesAfter[2] == 3 && livesAfter[3] == 3, ok);
    printf("living -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// The autopilot plays `level` with its lives topped up, until the level's
// cleared or `budget` frames pass. Returns frames taken (-1: not cleared).
static long playLevel(BrickFluxGame &g, AudioEngine &audio, GFXcanvas16 &canvas, int level, long budget,
                      int &refills, const char* &why) {
    startPlaying(g, audio, canvas);
    g._level = level;
    g.loadLevel();
    g._aHeld = false;
    const long cleared0 = g._statCleared;
    refills = 0;
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "brick_L%02d", level);
    FrameDumper dump(prefix);                    // DUMP_AT frames of each level
    for (long f = 0; f < budget; ++f) {
        if (g._lives < LIVES) { g._lives = LIVES; ++refills; }
        InputState in = g.autopilot();
        g.update(canvas, in, audio);
        dump.maybeDump(f, canvas);
        g_fakeMillis += STEP_MS;
        if (!ballsSound(g, why)) return -1;
        if (g._statCleared > cleared0) return f;
    }
    why = "not cleared in time";
    return -1;
}

static bool scenarioLevels(long budget) {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("levels (lives pinned, %ld frames each at most):\n", budget);
    for (int level = 1; level <= LEVELS_PER_LOOP; ++level) {
        if (bossForLevel(level)) continue;
        int refills;
        const char* why = nullptr;
        const long f = playLevel(g, audio, canvas, level, budget, refills, why);
        printf("  level %2d (layout %2d): %s", level, layoutForLevel(level), f >= 0 ? "cleared" : "FAILED");
        if (f >= 0) printf(" in %4.0fs, %d lives lost\n", f * STEP_MS / 1000.0, refills);
        else printf(": %s\n", why);
        ok &= f >= 0;
    }
    printf("levels -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool scenarioBoss(long budget) {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    printf("bosses (lives pinned, %ld frames each at most):\n", budget);
    for (int n = 1; n <= 4; ++n) {
        const int level = n * BOSS_EVERY;
        int refills;
        const char* why = nullptr;
        const long bosses0 = g._statBosses;
        const long f = playLevel(g, audio, canvas, level, budget, refills, why);
        const bool beaten = f >= 0 && g._statBosses == bosses0 + 1;
        printf("  level %2d, %-12s %s", level, g.bossName(), beaten ? "beaten" : "NOT BEATEN");
        if (beaten) printf(" in %4.0fs, %d lives lost, %ld smashes so far\n", f * STEP_MS / 1000.0, refills, g._statSmashes);
        else printf(": %s\n", why ? why : "?");
        ok &= beaten;
    }
    printf("boss -> %s\n", ok ? "PASS" : "FAIL");
    return ok;
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

// The stage-select cheat: B held with A on the attract screens opens the
// picker; the stick steps the level (left/right by one, up/down by five,
// wrapping round one loop); A starts a test run there, B goes back. A test
// run puts nothing on the table, at game over or on quitting, and A at its
// game over starts the same level again. Plain A still starts level 1.
static bool scenarioPick() {
    static BrickFluxGame g;
    AudioEngine audio;
    GFXcanvas16 canvas(W, H);
    bool ok = true;
    auto check = [&](const char* what, bool cond) {
        printf("  %-50s %s\n", what, cond ? "ok" : "FAIL");
        ok &= cond;
    };
    auto frame = [&](InputState in = InputState{}) { g.update(canvas, in, audio); g_fakeMillis += STEP_MS; };
    // Portrait (rotation 2): screen up is joyDown, down is joyUp.
    auto push = [&](bool right, bool left, bool up, bool down) {
        InputState in{};
        in.joyRight = right; in.joyLeft = left; in.joyDown = up; in.joyUp = down;
        frame(in);
        frame();
    };
    printf("pick:\n");
    hiscore::Table empty;
    hiscore::clear(empty);
    hiscore::save("brick", empty);
    InputState ba{}; ba.btnB = true; ba.btnA = ba.btnAPressed = true;
    InputState a{}; a.btnA = a.btnAPressed = true;
    InputState b{}; b.btnB = b.btnBPressed = true;
    InputState stillB{}; stillB.btnB = true;
    g.init(audio);
    for (int f = 0; f < 10; ++f) frame();
    frame(ba);
    check("B+A on the title: the picker", g._phase == BrickFluxGame::PHASE_PICK && g._pick == 1);
    frame(stillB);
    check("B still held from opening it doesn't close it", g._phase == BrickFluxGame::PHASE_PICK);
    push(true, false, false, false);
    push(true, false, false, false);
    check("right twice: level 3, and its bricks shown", g._pick == 3 && g._level == 3);
    push(false, false, true, false);
    check("up: five on, level 8", g._pick == 8);
    push(false, false, false, true);
    push(false, false, false, true);
    check("down twice: wraps to level 18", g._pick == 18);
    push(false, true, false, false);
    check("left: level 17", g._pick == 17);
    frame(b);
    check("B: back to the title", g._phase == BrickFluxGame::PHASE_ATTRACT && !g._test);
    frame(ba);
    frame();
    push(false, false, true, false);
    push(false, false, true, false);
    check("up twice: level 11", g._pick == 11);
    frame(a);
    InputState held{}; held.btnA = true;
    for (int f = 0; f < 60; ++f) frame(held);
    check("A: a test run on 11, fresh, still serving",
          g._phase == BrickFluxGame::PHASE_PLAYING && g._test && g._level == 11 && g._score == 0 &&
          g._lives == LIVES && g._serving && !g._demo);
    g._score = 54320;
    g._lives = 1;
    g.loseLife();
    for (unsigned long t = 0; t <= LOST_MS + STEP_MS; t += STEP_MS) frame();
    check("test game over: no name entry", g._phase == BrickFluxGame::PHASE_GAMEOVER);
    g._scores.begin("brick");
    check("  and nothing on the table", g._scores.best() == 0);
    for (unsigned long t = 0; t <= ArcadeConfig::GAMEOVER_INPUT_DELAY_MS + STEP_MS; t += STEP_MS) frame();
    frame(a);
    check("A at its game over: level 11 again, still a test",
          g._phase == BrickFluxGame::PHASE_PLAYING && g._test && g._level == 11);
    g._score = 43210;
    g.onQuit(audio);
    g._scores.begin("brick");
    check("Back mid test run: nothing on the table", g._scores.best() == 0);
    g.init(audio);
    frame();
    frame(a);
    check("plain A: level 1, not a test", g._phase == BrickFluxGame::PHASE_PLAYING && !g._test && g._level == 1);
    g.init(audio);
    frame();
    frame(ba);
    for (unsigned long t = 0; t <= PICK_TIMEOUT_MS + STEP_MS; t += STEP_MS) frame();
    check("idle picker: back to the title, level 1 loaded", g._phase == BrickFluxGame::PHASE_ATTRACT && g._level == 1);
    printf("pick -> %s\n", ok ? "PASS" : "FAIL");
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
    if (all || !strcmp(which, "polarity")) ok &= scenarioPolarity();
    if (all || !strcmp(which, "living"))   ok &= scenarioLiving();
    if (all || !strcmp(which, "levels"))   ok &= scenarioLevels(frames ? frames : 12000);
    if (all || !strcmp(which, "boss"))     ok &= scenarioBoss(frames ? frames : 15000);
    if (all || !strcmp(which, "idle"))     ok &= scenarioIdle(frames ? frames : 6000);
    if (all || !strcmp(which, "demoexit")) ok &= scenarioDemoExit();
    if (all || !strcmp(which, "pick"))     ok &= scenarioPick();
    return ok ? 0 : 1;
}
