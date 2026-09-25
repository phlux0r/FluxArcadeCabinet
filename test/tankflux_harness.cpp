// Host-side regression harness for Tank Flux.
//
// Runs the real game logic and the real Jet rasteriser on a desktop, against
// a fake clock, a seeded RNG and a scripted bot, then prints a trace of game
// state and framebuffer hashes. An identical trace before and after a change
// means behaviour was preserved; that is what this is for. Built with
// AddressSanitizer, it also catches memory errors and leaks.
//
// It does NOT verify anything about how the game looks or feels, and the
// hardware it stubs out (audio especially) is invisible to it.
//
// See test/README.md. Build and run with test/build.sh.

#include <Arduino.h>

// The bot reads game state (health, enemy positions, phase) that the game
// rightly keeps private. Nothing else in the project does this.
#define private public
#include "games/TankFlux/TankFluxGame.h"
#undef private

// --- Fake clock + deterministic RNG (see stub/Arduino.h) ---------------------
unsigned long g_fakeMillis = 1000;
static uint32_t g_rng = 12345;
static uint32_t lcg() { g_rng = g_rng * 1103515245u + 12345u; return (g_rng >> 8) & 0xFFFFFF; }
long random(long hi) { return hi <= 0 ? 0 : (long)(lcg() % (uint32_t)hi); }
long random(long lo, long hi) { return hi <= lo ? lo : lo + (long)(lcg() % (uint32_t)(hi - lo)); }
void randomSeed(unsigned long s) { g_rng = (uint32_t)s; }

static uint32_t fnv(const void* p, size_t n, uint32_t h = 2166136261u) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    return h;
}

// Scenarios:
//   play   normal run: the bot dies and restarts, so game-over is covered
//   god    health pinned, so a long run reaches many bosses and arena resets
//   menus  exercises attract exit, in-game A+B quit and the game-over timeout
int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "play";
    const bool god   = strcmp(mode, "god") == 0;
    const bool menus = strcmp(mode, "menus") == 0;
    const long frames = argc > 2 ? atol(argv[2]) : 20000;

    GFXcanvas16 canvas(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);
    AudioEngine audio;
    TankFluxGame g;
    g.init(audio);

    bool prevA = false, prevB = false, wasBoss = false;
    int bossesSeen = 0, quits = 0, gameOvers = 0, lastPhase = -1;
    uint32_t traceHash = 2166136261u;

    for (long f = 0; f < frames; ++f) {
        InputState in{};
        bool a = false, b = false;

        if (g._phase != TankFluxGame::PHASE_PLAYING) {
            a = (f % 40) == 0;   // press A to start / restart
        } else {
            if (god) g._health = tankflux::HEALTH_MAX;

            // Turn towards the nearest target (the boss if there is one),
            // hold a mid-range distance, and fire when roughly lined up.
            float bx = 0, bz = 0, bd = 1e30f;
            bool have = false;
            for (auto &e : g._enemies) {
                if (!e.alive) continue;
                float d = (e.x - g._x) * (e.x - g._x) + (e.z - g._z) * (e.z - g._z);
                if (d < bd) { bd = d; bx = e.x; bz = e.z; have = true; }
            }
            if (g._bossActive) { bx = g._boss.x; bz = g._boss.z; have = true; }
            if (have) {
                float want = degrees(atan2f(bx - g._x, bz - g._z));
                float err = want - g._headingDeg;
                while (err >  180) err -= 360;
                while (err < -180) err += 360;
                in.joyY = constrain(err / 8.0f, -1.0f, 1.0f);
                float dist = sqrtf((bx - g._x) * (bx - g._x) + (bz - g._z) * (bz - g._z));
                in.joyX = dist > 1400 ? -1.0f : (dist < 700 ? 0.7f : 0.0f);
                if (fabsf(err) < 3.0f && (f % 6) == 0) a = true;
            }
            // Strafe now and then, so hold-B is covered.
            long cycle = f % 600;
            if (cycle >= 300 && cycle < 360) { b = true; in.joyX = (f / 600) % 2 ? 1.0f : -1.0f; }
            // ~720ms of A+B: long enough to show the quit hint, short enough
            // not to quit.
            if (f % 5000 >= 4000 && f % 5000 < 4045) { a = true; b = true; }
        }

        if (menus) {
            if (f < 300)                 { a = false; b = (f >= 20 && f < 200); }  // attract: hold B to exit
            else if (f >= 600 && f < 760) { a = true;  b = true; }                  // playing: hold A+B to quit
            else if (f > 2000 && g._phase == TankFluxGame::PHASE_GAMEOVER) { a = false; b = false; }
        }

        in.btnA = a; in.btnB = b;
        in.btnAPressed = a && !prevA;
        in.btnBPressed = b && !prevB;
        prevA = a; prevB = b;

        if (!g.update(canvas, in, audio)) {
            // What main.cpp's returnToLauncher() + launchGame() do.
            ++quits;
            printf("quit at f=%ld phase=%d\n", f, (int)g._phase);
            g.onExit();
            g.init(audio);
        }

        if (g._bossActive && !wasBoss) ++bossesSeen;
        wasBoss = g._bossActive;
        if (g._phase == TankFluxGame::PHASE_GAMEOVER && lastPhase != TankFluxGame::PHASE_GAMEOVER) ++gameOvers;
        lastPhase = g._phase;

        int32_t st[12] = { (int32_t)g._phase, g._score, g._health, g._kills, g._level,
                           (int32_t)g._x, (int32_t)g._z, (int32_t)g._headingDeg,
                           g._bossActive, g._bossActive ? g._boss.hp : -1,
                           (int32_t)g._bossPending, g._bossesDefeated };
        traceHash = fnv(st, sizeof(st), traceHash);
        // Hashing every frame's pixels would dominate the runtime; once a
        // second is enough to catch a rendering change.
        if (f % 60 == 0) {
            traceHash = fnv(canvas.getBuffer(),
                            ArcadeConfig::LANDSCAPE_WIDTH * ArcadeConfig::LANDSCAPE_HEIGHT * 2,
                            traceHash);
        }
        if (f % 2000 == 0) {
            printf("f=%6ld ph=%d sc=%6d hp=%3d k=%3d lv=%d pos=(%6d,%6d) boss=%d/%d hash=%08x\n",
                   f, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[8], st[9], traceHash);
        }

        g_fakeMillis += 16;   // nominal 60fps; the game only reads millis()
    }

    printf("DONE frames=%ld bosses=%d defeated=%d gameovers=%d quits=%d "
           "tones=%d melodies=%d wavs=%d final=%08x\n",
           frames, bossesSeen, g._bossesDefeated, gameOvers, quits,
           audio.tones, audio.melodies, audio.wavs, traceHash);
    return 0;
}
