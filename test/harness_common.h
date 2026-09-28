#pragma once
// Shared by the per-game harnesses: the fake clock and seeded RNG the
// Arduino stub declares, trace hashing, and framebuffer dumps.
//
// Include once per harness executable: it defines, not just declares.

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// --- Framebuffer dumps -------------------------------------------------------
// DUMP_AT=500,4000 writes <prefix>_000500.ppm etc. to the working directory
// (test/.build when run through build.sh). The only way to look at a frame
// without the board. Note the GFX stub draws no text, so HUD text is absent.
struct FrameDumper {
    long at[32];
    int  n = 0;
    const char* prefix;

    explicit FrameDumper(const char* p) : prefix(p) {
        const char* env = getenv("DUMP_AT");
        while (env && *env && n < 32) {
            at[n++] = atol(env);
            env = strchr(env, ',');
            if (env) ++env;
        }
    }

    void maybeDump(long frame, GFXcanvas16 &c) const {
        for (int i = 0; i < n; ++i) {
            if (at[i] != frame) continue;
            char path[96];
            snprintf(path, sizeof(path), "%s_%06ld.ppm", prefix, frame);
            FILE* f = fopen(path, "wb");
            if (!f) return;
            fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
            const uint16_t* px = c.getBuffer();
            for (int j = 0; j < c.width() * c.height(); ++j) {
                uint16_t v = px[j];
                uint8_t rgb[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31),
                                   (uint8_t)(((v >> 5) & 63) * 255 / 63),
                                   (uint8_t)((v & 31) * 255 / 31) };
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
            printf("dumped %s\n", path);
        }
    }
};
