// Host tests for the audio mixer and loader (src/cabinet/audio/). Runs the
// real code single-threaded: loader.step() then mixer.render(), in a loop,
// against WAV files written to a temp folder. What it can't cover is the
// device side: I2S, the two FreeRTOS tasks and the SD card (AudioEngine.h),
// and above all what it sounds like.

#include "cabinet/audio/AudioLoader.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>

using namespace audiomix;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) ++g_pass; else { ++g_fail; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// --- WAV files on the host --------------------------------------------------
static std::string g_dir;

static void put16(FILE* f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }
static void put32(FILE* f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

// samples are raw values, interleaved if stereo. A LIST chunk before
// 'data' checks the parser skips chunks it doesn't know.
static void writeWav(const char* name, uint32_t rate, int bits, int ch, const std::vector<int>& samples) {
    FILE* f = fopen((g_dir + name).c_str(), "wb");
    uint32_t dataBytes = (uint32_t)samples.size() * (bits / 8);
    fwrite("RIFF", 1, 4, f); put32(f, 4 + 8 + 16 + 8 + 4 + 8 + dataBytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, ch); put32(f, rate);
    put32(f, rate * ch * bits / 8); put16(f, ch * bits / 8); put16(f, bits);
    fwrite("LIST", 1, 4, f); put32(f, 4); fwrite("INFO", 1, 4, f);
    fwrite("data", 1, 4, f); put32(f, dataBytes);
    for (int s : samples) { if (bits == 16) put16(f, (uint16_t)(int16_t)s); else fputc(s & 255, f); }
    fclose(f);
}

// Set to make reads misbehave like a busy SD card: every third read
// returns nothing, and the others return one byte short (mid-sample).
static bool g_flaky = false;

class HostFile : public AudioFile {
public:
    bool open(const char* path) override { close(); _f = fopen((g_dir + path).c_str(), "rb"); return _f != nullptr; }
    void close() override { if (_f) fclose(_f); _f = nullptr; }
    bool isOpen() const override { return _f != nullptr; }
    int read(uint8_t* b, int n) override {
        if (!_f) return 0;
        if (g_flaky && n >= 256) {                  // the data, not the header
            if (++_reads % 3 == 0) return 0;
            if (n > 1) --n;
        }
        return (int)fread(b, 1, n, _f);
    }
    bool seek(uint32_t p) override { return _f && fseek(_f, p, SEEK_SET) == 0; }
    uint32_t position() override { return _f ? (uint32_t)ftell(_f) : 0; }
    ~HostFile() override { close(); }
private:
    FILE* _f = nullptr;
    int   _reads = 0;
};

static int g_allocs = 0;
static void* testAlloc(size_t n) { ++g_allocs; return malloc(n); }
static void  testFree(void* p) { --g_allocs; free(p); }

// A mixer + loader pair with rings, driven one block at a time.
struct Rig {
    Mixer mixer;
    HostFile mf, jf, lf;
    AudioLoader loader;
    std::vector<int16_t> ring0, ring1;
    uint32_t epoch = 0;
    bool stalled = false;          // the loader gets no time (an SD card stall)
    Rig(uint32_t budget = 1 << 20, uint32_t maxEntry = 256 * 1024)
        : loader(mixer, &mf, &jf, &lf, testAlloc, testFree, budget, maxEntry),
          ring0(RING_SAMPLES), ring1(RING_SAMPLES) {
        mixer.streams[0].buf = ring0.data();
        mixer.streams[1].buf = ring1.data();
        mixer.masterQ15 = 32768;   // unity, so levels are exact
    }
    // Runs `frames` of audio; returns the left channel.
    std::vector<int16_t> run(int frames, int block = 256) {
        std::vector<int16_t> out, buf(block * 2);
        while ((int)out.size() < frames) {
            if (!stalled) loader.step();
            int n = std::min(block, frames - (int)out.size());
            mixer.render(buf.data(), n);
            for (int i = 0; i < n; ++i) out.push_back(buf[2 * i]);
        }
        return out;
    }
    void cmd(LoadCmdType t, const char* path = "", const char* path2 = "") {
        LoadCmd c; c.type = t; c.epoch = epoch;
        snprintf(c.path, sizeof(c.path), "%s", path);
        snprintf(c.path2, sizeof(c.path2), "%s", path2);
        if (t == LC_PLAY_FX) loader.inFlight.fetch_add(1);
        loader.fromGame.push(c);
    }
    void mix(MixCmdType t, int a = 0, int b = 0, Pcm* pcm = nullptr, const int* f = nullptr, const int* d = nullptr) {
        MixCmd m; m.type = t; m.epoch = epoch; m.a = a; m.b = b; m.pcm = pcm; m.freqs = f; m.durs = d;
        if (t == MC_PLAY_PCM && pcm) pcm->queued.fetch_add(1);
        mixer.fromGame.push(m);
    }
    void mute() { ++epoch; mix(MC_STOP_ALL); cmd(LC_STOP_ALL); }
};

static int nonZero(const std::vector<int16_t>& v) { int n = 0; for (auto s : v) n += s != 0; return n; }

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    char tmpl[] = "/tmp/audiotestXXXXXX";
    g_dir = mkdtemp(tmpl);
    mkdir((g_dir + "/audio").c_str(), 0755);

    // --- softClip: identity to the knee, then bounded and still rising.
    CHECK(softClip(1000) == 1000 && softClip(-24576) == -24576, "linear below the knee");
    int prev = 0; bool mono = true;
    for (int x = 0; x < 200000; x += 97) { int y = softClip(x); if (y < prev || y > 32767) mono = false; prev = y; }
    CHECK(mono, "monotonic and <= 32767");
    CHECK(softClip(-200000) >= -32767, "bounded negative");

    // --- Two voices sum at their own level; master scales.
    {
        Rig r;
        static int16_t ones[1000]; for (auto &s : ones) s = 1000;
        Pcm a, b; a.data = ones; a.frames = 1000; b.data = ones; b.frames = 1000;
        r.mix(MC_PLAY_PCM, 0, 0, &a); r.mix(MC_PLAY_PCM, 0, 0, &b);
        auto out = r.run(100);
        CHECK(out[10] == 2000, "two voices sum: %d", out[10]);
        r.mixer.masterQ15 = 16384;
        out = r.run(100);
        CHECK(out[10] == 1000, "master 0.5: %d", out[10]);
        r.run(2000);
        CHECK(a.playing == 0 && b.playing == 0 && a.queued == 0 && r.mixer.activeVoices() == 0, "voices end and release");
    }

    // --- Five effects on four voices: the oldest makes way.
    {
        Rig r;
        static int16_t s[5][4000];
        Pcm p[5];
        for (int i = 0; i < 5; ++i) { for (auto &v : s[i]) v = (int16_t)(100 * (i + 1)); p[i].data = s[i]; p[i].frames = 4000; }
        for (int i = 0; i < 4; ++i) r.mix(MC_PLAY_PCM, 0, 0, &p[i]);
        r.run(10);
        r.mix(MC_PLAY_PCM, 0, 0, &p[4]);
        auto out = r.run(10);
        CHECK(r.mixer.activeVoices() == 4 && p[0].playing == 0 && p[4].playing == 1, "oldest stolen");
        CHECK(out[5] == 200 + 300 + 400 + 500, "mix after stealing: %d", out[5]);
    }

    // --- Synth: a tone lasts its length; a melody its notes' lengths, 85% sounding.
    {
        Rig r;
        r.mix(MC_TONE, 1000, 100);
        auto out = r.run(8000);
        int n = nonZero(out);
        CHECK(n > 4300 && n <= 4410, "100ms tone sounds for ~4410 frames: %d", n);
        CHECK(!r.mixer.toneBusy, "tone done");
        static const int f[] = { 500, 0, 800 }, d[] = { 50, 50, 100 };
        r.mix(MC_MELODY, 3, 0, nullptr, f, d);
        out = r.run(200);
        CHECK(r.mixer.melodyBusy && r.mixer.melodiesStarted == 1, "melody running");
        out = r.run(9000);
        n = nonZero(out);
        CHECK(!r.mixer.melodyBusy, "melody done");
        // 50ms*0.85 + rest + 100ms*0.85 ~ 5623 frames sounding, minus the 200 already run.
        CHECK(n > 5100 && n < 5700, "melody articulation: %d sounding", n);
    }

    // --- Tones no longer wait for a WAV: an effect and a tone at once.
    {
        Rig r;
        static int16_t ones[20000]; for (auto &s : ones) s = 1000;
        Pcm a; a.data = ones; a.frames = 20000;
        r.mix(MC_PLAY_PCM, 0, 0, &a);
        r.mix(MC_TONE, 1000, 100);
        auto out = r.run(100);
        CHECK(out[10] == 1000 + SYNTH_AMP || out[10] == 1000 - SYNTH_AMP, "tone over an effect: %d", out[10]);
    }

    // --- Epochs: something asked for before a mute doesn't play after it.
    {
        Rig r;
        static int16_t ones[1000]; for (auto &s : ones) s = 1000;
        Pcm a; a.data = ones; a.frames = 1000;
        a.queued.fetch_add(1);
        MixCmd late; late.type = MC_PLAY_PCM; late.epoch = 0; late.pcm = &a;   // e.g. the loader, late
        r.epoch = 1; r.mix(MC_STOP_ALL);
        r.mixer.fromLoader.push(late);
        r.mix(MC_TONE, 440, 50);           // asked for after the mute: plays
        auto out = r.run(100);
        CHECK(a.playing == 0 && a.queued == 0, "stale effect dropped");
        CHECK(nonZero(out) > 0, "post-mute tone plays");
    }

    // --- Loader: an effect from SD, then from the cache.
    {
        std::vector<int> ramp; for (int i = 0; i < 3000; ++i) ramp.push_back(i * 5);
        writeWav("/audio/fx16.wav", 44100, 16, 1, ramp);
        Rig r;
        r.cmd(LC_PLAY_FX, "/audio/fx16.wav");
        auto out = r.run(4000);
        CHECK(r.loader.isCached("/audio/fx16.wav"), "cached after first play");
        CHECK(r.loader.lastDurationMs == 68, "duration from header: %u", (unsigned)r.loader.lastDurationMs);
        CHECK(r.loader.inFlight == 0, "request resolved");
        int start = -1; for (int i = 0; i < (int)out.size(); ++i) if (out[i] == 5) { start = i; break; }
        bool same = start >= 1;
        for (int i = 0; same && i < 3000; ++i) same = out[start - 1 + i] == ramp[i];
        CHECK(same, "played sample-exact (start %d)", start);
        // Second play: straight from the cache, no load.
        r.cmd(LC_PLAY_FX, "/audio/fx16.wav");
        r.loader.step();
        CHECK(!r.loader.loading() && r.loader.inFlight == 0, "cache hit is immediate");
        r.mixer.render(std::vector<int16_t>(512).data(), 256);
        CHECK(r.mixer.activeVoices() == 1, "cache hit plays");
    }

    // --- 8-bit stereo at 22.05kHz: averaged to mono, each sample twice.
    {
        std::vector<int> st; for (int i = 0; i < 400; ++i) { st.push_back(128 + 10); st.push_back(128 + 30); }
        writeWav("/audio/fx8.wav", 22050, 8, 2, st);
        Rig r;
        r.cmd(LC_PLAY_FX, "/audio/fx8.wav");
        auto out = r.run(2000);
        int n = 0; for (auto s : out) n += s == 20 * 256;
        CHECK(n >= 798 && n <= 800, "8-bit stereo 22k -> mono 44.1k: %d samples of 5120", n);
    }

    // --- Music loops seamlessly.
    {
        std::vector<int> m; for (int i = 0; i < 1000; ++i) m.push_back(i + 1);
        writeWav("/audio/music.wav", 44100, 16, 1, m);
        Rig r;
        r.cmd(LC_LOOP_MUSIC, "/audio/music.wav");
        auto out = r.run(6000);
        int start = -1; for (int i = 0; i < (int)out.size(); ++i) if (out[i] == 1) { start = i; break; }
        bool seamless = start >= 0;
        for (int i = start; seamless && i < 6000; ++i) seamless = out[i] == (i - start) % 1000 + 1;
        CHECK(seamless, "loop seamless from %d", start);
        CHECK(r.mixer.musicBusy && r.loader.streaming(STREAM_MUSIC), "music playing");
        // Music off: silent, and the loader lets the file go.
        r.mixer.musicOn = false;
        out = r.run(1000);
        CHECK(nonZero(std::vector<int16_t>(out.begin() + 300, out.end())) == 0, "music off is silent");
        CHECK(!r.loader.streaming(STREAM_MUSIC), "music off stops streaming");
        r.cmd(LC_LOOP_MUSIC, "/audio/music.wav");
        r.run(1000);
        CHECK(!r.loader.streaming(STREAM_MUSIC), "music off ignores new music");
    }

    // --- A flaky card: failed and short reads neither restart the loop
    // nor knock the samples out of alignment, and effects load exactly.
    {
        std::vector<int> m; for (int i = 0; i < 3000; ++i) m.push_back(i + 1);
        writeWav("/audio/music.wav", 44100, 16, 1, m);
        std::vector<int> fx; for (int i = 0; i < 5000; ++i) fx.push_back(-(i + 1));
        writeWav("/audio/fxf.wav", 44100, 16, 1, fx);
        g_flaky = true;
        Rig r;
        r.cmd(LC_LOOP_MUSIC, "/audio/music.wav");
        auto out = r.run(20000);
        int start = -1; for (int i = 0; i < (int)out.size(); ++i) if (out[i] == 1) { start = i; break; }
        bool seamless = start >= 0;
        int bad = -1;
        for (int i = start; seamless && i < 20000; ++i) {
            seamless = out[i] == (i - start) % 3000 + 1;
            if (!seamless) bad = i;
        }
        CHECK(seamless, "flaky card: loop seamless from %d (broke at %d)", start, bad);
        CHECK(r.loader.readErrors > 0 && r.loader.shortReads > 0, "flaky card: errors %u, short %u",
              (unsigned)r.loader.readErrors, (unsigned)r.loader.shortReads);
        CHECK(r.mixer.underruns[STREAM_MUSIC] == 0, "flaky card: no underruns (%u)",
              (unsigned)r.mixer.underruns[STREAM_MUSIC]);
        r.mixer.musicOn = false;
        r.run(512);
        r.cmd(LC_PLAY_FX, "/audio/fxf.wav");
        out = r.run(8000);
        int fs = -1; for (int i = 0; i < (int)out.size(); ++i) if (out[i] == -1) { fs = i; break; }
        bool exact = fs >= 0;
        for (int i = 0; exact && i < 5000; ++i) exact = out[fs + i] == -(i + 1);
        CHECK(exact && r.loader.isCached("/audio/fxf.wav"), "flaky card: effect loads sample-exact");
        g_flaky = false;
    }

    // --- The music rides out a 400ms loader stall (seen on hardware while
    // the display held the SD card's bus).
    {
        std::vector<int> m(44100, 500);
        writeWav("/audio/flat.wav", 44100, 16, 1, m);
        Rig r;
        r.cmd(LC_LOOP_MUSIC, "/audio/flat.wav");
        r.run(20000);
        r.stalled = true;
        auto out = r.run(44100 * 400 / 1000);
        r.stalled = false;
        CHECK(r.mixer.underruns[STREAM_MUSIC] == 0 && nonZero(out) == (int)out.size(),
              "400ms stall: no underrun (%u)", (unsigned)r.mixer.underruns[STREAM_MUSIC]);
    }

    // --- Effects over music, at their own level.
    {
        std::vector<int> m(20000, 1000);
        writeWav("/audio/flat.wav", 44100, 16, 1, m);
        std::vector<int> fx(2000, 3000);
        writeWav("/audio/beep.wav", 44100, 16, 1, fx);
        Rig r;
        r.cmd(LC_LOOP_MUSIC, "/audio/flat.wav");
        r.run(2000);
        r.cmd(LC_PLAY_FX, "/audio/beep.wav");
        auto out = r.run(3000);
        int both = 0; for (auto s : out) both += s == 4000;
        CHECK(both > 1500, "effect + music = 4000 for most of the effect: %d", both);
        r.mixer.fxOn = false;
        r.cmd(LC_PLAY_FX, "/audio/beep.wav");
        out = r.run(3000);
        int fxOnly = 0; for (auto s : out) fxOnly += s != 1000 && s != 0;
        CHECK(fxOnly == 0 && r.loader.inFlight == 0, "fx off: music only, request resolved");
    }

    // --- A file too big for the cache plays as the jingle stream; then music after it.
    {
        std::vector<int> big(30000, 700);
        writeWav("/audio/big.wav", 44100, 16, 1, big);
        std::vector<int> m(5000, 50);
        writeWav("/audio/after.wav", 44100, 16, 1, m);
        Rig r(1 << 20, 16 * 1024);                   // 16KB entries at most
        r.cmd(LC_PLAY_FX, "/audio/big.wav");
        auto out = r.run(2000);
        CHECK(!r.loader.isCached("/audio/big.wav") && r.loader.streaming(STREAM_JINGLE), "big file streams");
        CHECK(r.loader.inFlight == 0 && r.loader.lastDurationMs == 680, "jingle resolved, duration %u", (unsigned)r.loader.lastDurationMs);
        out = r.run(40000);
        int played = 0; for (auto s : out) played += s == 700;
        CHECK(played > 27000, "jingle played through: %d", played);
        CHECK(!r.mixer.fxBusy && !r.loader.streaming(STREAM_JINGLE), "jingle finished");
        r.cmd(LC_PLAY_THEN_LOOP, "/audio/big.wav", "/audio/after.wav");
        out = r.run(40000);
        int music = 0; for (auto s : out) music += s == 50;
        CHECK(music > 5000 && r.mixer.musicBusy, "music follows the jingle: %d", music);
    }

    // --- Missing file, bad file.
    {
        FILE* f = fopen((g_dir + "/audio/junk.wav").c_str(), "wb"); fputs("not a wav at all", f); fclose(f);
        Rig r;
        r.cmd(LC_PLAY_FX, "/audio/nope.wav");
        r.cmd(LC_PLAY_FX, "/audio/junk.wav");
        r.run(1000);
        CHECK(r.loader.inFlight == 0 && r.loader.lastDurationMs == 0 && !r.mixer.fxBusy, "missing/bad resolve silently");
    }

    // --- Mute mid-load: the load is abandoned and nothing plays after.
    {
        std::vector<int> ramp(40000, 900);
        writeWav("/audio/slow.wav", 44100, 16, 1, ramp);
        Rig r;
        r.cmd(LC_PLAY_FX, "/audio/slow.wav");
        r.loader.step(); r.loader.step();
        CHECK(r.loader.loading(), "loading in chunks");
        r.mute();
        auto out = r.run(6000);
        CHECK(!r.loader.loading() && r.loader.inFlight == 0 && nonZero(out) == 0, "mute abandons the load");
        CHECK(!r.loader.isCached("/audio/slow.wav"), "half-loaded entry freed");
    }

    // --- Cache budget: least recently used goes; nothing playing is freed.
    {
        std::vector<int> a(4000, 11), b(4000, 22), c(4000, 33);   // 8000 bytes each decoded
        writeWav("/audio/a.wav", 44100, 16, 1, a);
        writeWav("/audio/b.wav", 44100, 16, 1, b);
        writeWav("/audio/c.wav", 44100, 16, 1, c);
        Rig r(17000, 16000);                          // room for two
        r.cmd(LC_PRELOAD, "/audio/a.wav");
        r.cmd(LC_PRELOAD, "/audio/b.wav");
        r.run(3000);
        CHECK(r.loader.isCached("/audio/a.wav") && r.loader.isCached("/audio/b.wav"), "preloaded two");
        r.cmd(LC_PLAY_FX, "/audio/a.wav");            // a is now the most recent
        r.run(300);
        r.cmd(LC_PLAY_FX, "/audio/c.wav");            // b is the one to go (a is playing anyway)
        r.run(6000);
        CHECK(r.loader.isCached("/audio/a.wav") && !r.loader.isCached("/audio/b.wav") &&
              r.loader.isCached("/audio/c.wav"), "LRU eviction");
        CHECK(r.loader.cacheBytes() <= 17000, "within budget: %u", (unsigned)r.loader.cacheBytes());
    }

    printf("audio_test: %d passed, %d failed; allocations outstanding in the last rig: %d\n",
           g_pass, g_fail, g_allocs);
    printf("%s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
