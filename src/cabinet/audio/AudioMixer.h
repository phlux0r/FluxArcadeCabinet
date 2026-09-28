#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

// The cabinet's software mixer: plain C++ (no Arduino, no FreeRTOS), so the
// host tests can run it (test/audio_test.cpp).
//
// Every sound playing is added together, sample by sample, into one block,
// and that block is what goes to the speaker. Sources:
//   - 1 music stream: a looping WAV, streamed from SD by AudioLoader into a
//     ring buffer.
//   - 1 jingle stream: one-shot WAVs too long to hold in memory, same way.
//   - FX_VOICES effect voices: short WAVs held in memory (AudioLoader's
//     cache) or PROGMEM samples. A new effect with every voice busy
//     replaces the oldest.
//   - 1 synth: the square-wave tones and melodies playTone/playMelody make.
// Music is one bus; everything else is the effects bus. Each bus can be
// switched off, and a master volume applies to both. Sources are summed at
// their own level (a sound plays as loud mixed as alone) and only the peaks
// that would overflow are rounded off (softClip), rather than dividing by
// the number of sources, which makes everything quieter in busy moments.
//
// Threading: render() and everything it calls run on the mixer task only.
// Commands arrive through two single-producer queues, one from the game
// loop and one from the loader, and streams through single-producer rings.
// Commands carry an epoch: mute() bumps it, and anything requested before
// the mute that turns up afterwards (a cached effect the loader posts late,
// say) is dropped rather than played.
//
// Design reference: retro-go's Doom port (github.com/ducalex/retro-go,
// prboom-go/main/main.c) mixes the same way: music into the block, then
// effects on top from voices held in memory, oldest stolen. No code is
// taken from it (it's GPL-2.0); the differences are deliberate (streamed
// WAV music, no averaging, epochs).

#include <stdint.h>
#include <string.h>
#include <atomic>

namespace audiomix {

constexpr uint32_t OUT_RATE      = 44100;
constexpr int      FX_VOICES     = 4;
constexpr int      STREAM_MUSIC  = 0;
constexpr int      STREAM_JINGLE = 1;
constexpr int      STREAMS       = 2;
constexpr uint32_t RING_SAMPLES  = 8192;       // per stream: ~186ms at 44.1kHz
constexpr int16_t  SYNTH_AMP     = 8000;       // the old engine's tone level

static_assert((RING_SAMPLES & (RING_SAMPLES - 1)) == 0, "ring size must be a power of two");

// A sound held in memory, mono: int16, or unsigned 8-bit (the PROGMEM
// fallback samples). `playing` counts the voices using it, so the loader
// never frees one mid-play.
struct Pcm {
    const void* data = nullptr;
    uint32_t    frames = 0;
    uint32_t    rate = OUT_RATE;
    bool        is8bit = false;
    std::atomic<int> playing{0};    // voices using it (mixer)
    std::atomic<int> queued{0};     // MC_PLAY_PCMs posted, not yet applied
};

// Single producer, single consumer queue.
template <typename T, uint32_t N>
class Spsc {
public:
    bool push(const T& v) {
        uint32_t h = _head.load(std::memory_order_relaxed);
        uint32_t n = (h + 1) % N;
        if (n == _tail.load(std::memory_order_acquire)) return false;   // full: dropped
        _items[h] = v;
        _head.store(n, std::memory_order_release);
        return true;
    }
    bool pop(T& v) {
        uint32_t t = _tail.load(std::memory_order_relaxed);
        if (t == _head.load(std::memory_order_acquire)) return false;
        v = _items[t];
        _tail.store((t + 1) % N, std::memory_order_release);
        return true;
    }
private:
    T _items[N];
    std::atomic<uint32_t> _head{0}, _tail{0};
};

// A stream's samples, mono int16 at the source's rate: the loader writes,
// the mixer reads. Indices run free and wrap with the mask. To restart a
// stream the loader asks for a flush (gen) and waits for the mixer to
// acknowledge it (ack), since only the reader may move the read index.
struct StreamRing {
    int16_t* buf = nullptr;                    // RING_SAMPLES, allocated by the engine
    std::atomic<uint32_t> w{0}, r{0};
    std::atomic<uint32_t> rate{OUT_RATE};
    std::atomic<bool>     ended{false};        // the loader has written the last sample
    std::atomic<uint32_t> gen{0}, ack{0};

    uint32_t filled() const { return w.load(std::memory_order_acquire) - r.load(std::memory_order_acquire); }
    uint32_t space() const  { return RING_SAMPLES - filled(); }
    // Loader side.
    void write(const int16_t* s, uint32_t n) {
        uint32_t wi = w.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) buf[(wi + i) & (RING_SAMPLES - 1)] = s[i];
        w.store(wi + n, std::memory_order_release);
    }
};

enum MixCmdType : uint8_t {
    MC_PLAY_PCM,        // pcm
    MC_TONE,            // a = Hz, b = ms
    MC_MELODY,          // freqs, durs, a = length
    MC_STOP_ALL,        // everything (mute); sets the epoch
    MC_STREAM_START,    // a = stream
    MC_STREAM_STOP,     // a = stream
};

struct MixCmd {
    MixCmdType type = MC_STOP_ALL;
    uint32_t   epoch = 0;
    Pcm*       pcm = nullptr;
    const int* freqs = nullptr;
    const int* durs = nullptr;
    int        a = 0, b = 0;
};

// Rounds off only what would overflow: linear up to the knee, then eases
// towards full scale instead of clipping hard.
inline int16_t softClip(int32_t x) {
    const int32_t K = 24576, R = 32767 - K;
    if (x > K)  { int32_t e = x - K;  return (int16_t)(K + (int32_t)((int64_t)e * R / (e + R))); }
    if (x < -K) { int32_t e = -x - K; return (int16_t)-(K + (int32_t)((int64_t)e * R / (e + R))); }
    return (int16_t)x;
}

class Mixer {
public:
    Spsc<MixCmd, 32> fromGame;
    Spsc<MixCmd, 32> fromLoader;
    StreamRing streams[STREAMS];

    // Settings, written by the game loop.
    std::atomic<int32_t> masterQ15{26214};      // 0.8
    std::atomic<bool>    musicOn{true};
    std::atomic<bool>    fxOn{true};

    // Status, written here after each block, read by the game loop.
    std::atomic<bool> fxBusy{false};            // an effect voice or the jingle
    std::atomic<bool> musicBusy{false};
    std::atomic<bool> melodyBusy{false};
    std::atomic<bool> toneBusy{false};
    std::atomic<uint32_t> melodiesStarted{0};   // lets the game side tell "queued" from "done"

    // Diagnostics (the engine logs them): only ever counted up, here.
    std::atomic<uint32_t> underruns[STREAMS];   // a stream ran dry mid-play (events)
    std::atomic<uint32_t> stolen{0};            // effects cut short for a new one
    std::atomic<uint32_t> epochDropped{0};      // commands dropped as older than a mute
    std::atomic<uint32_t> voicesStarted{0};     // effects that started sounding
    std::atomic<uint32_t> tonesStarted{0};      // playTone()s that started sounding

    // Mixes `frames` stereo frames (L, R interleaved; the cabinet has one
    // speaker, so they're equal) into `out`.
    void render(int16_t* out, int frames) {
        drainCommands();
        for (int s = 0; s < STREAMS; ++s) serviceFlush(s);

        const bool music = musicOn.load(std::memory_order_relaxed);
        const bool fx = fxOn.load(std::memory_order_relaxed);
        const int32_t master = masterQ15.load(std::memory_order_relaxed);

        for (int i = 0; i < frames; ++i) {
            int32_t fxSum = 0;
            for (auto &v : _voices) if (v.pcm) fxSum += voiceSample(v);
            if (_streamOn[STREAM_JINGLE]) fxSum += streamSample(STREAM_JINGLE);
            if (_synthOn) fxSum += synthSample();
            int32_t musicSum = _streamOn[STREAM_MUSIC] ? streamSample(STREAM_MUSIC) : 0;

            int32_t mix = (music ? musicSum : 0) + (fx ? fxSum : 0);
            int16_t o = softClip((int32_t)(((int64_t)mix * master) >> 15));
            out[2 * i] = o;
            out[2 * i + 1] = o;
        }
        publishStatus();
    }

    Mixer() { for (auto &u : underruns) u.store(0); }

    // Host tests: how many voices are sounding.
    int activeVoices() const {
        int n = 0;
        for (auto &v : _voices) n += v.pcm != nullptr;
        return n;
    }

private:
    struct Voice {
        Pcm*     pcm = nullptr;
        uint32_t pos = 0;          // 16.16 fixed point, in source frames
        uint32_t step = 0;
        uint32_t age = 0;
    };

    Voice    _voices[FX_VOICES];
    uint32_t _age = 0;
    uint32_t _epoch = 0;

    bool     _streamOn[STREAMS] = { false, false };
    uint32_t _streamFrac[STREAMS] = { 0, 0 };
    uint32_t _streamStep[STREAMS] = { 1u << 16, 1u << 16 };
    int16_t  _streamCur[STREAMS] = { 0, 0 };
    bool     _starved[STREAMS] = { false, false };   // counted once per dry spell

    // Synth: a 32-bit phase accumulator for the square wave.
    bool       _synthOn = false;
    uint32_t   _phase = 0, _phaseStep = 0;
    uint32_t   _noteLeft = 0, _gateLeft = 0;   // samples left in the note, and sounding
    const int* _melF = nullptr;
    const int* _melD = nullptr;
    int        _melLen = 0, _melIdx = 0;
    bool       _melody = false;

    void drainCommands() {
        MixCmd c;
        while (fromGame.pop(c)) apply(c);
        while (fromLoader.pop(c)) apply(c);
    }

    void apply(const MixCmd& c) {
        if (c.type == MC_STOP_ALL) {
            _epoch = c.epoch;
            for (auto &v : _voices) stopVoice(v);
            for (int s = 0; s < STREAMS; ++s) _streamOn[s] = false;
            _synthOn = _melody = false;
            return;
        }
        // Counted however it ends (played, dropped, fx off), so the game
        // side can tell a melody still queued from one that's been handled,
        // and the loader an effect it can free from one about to play.
        if (c.type == MC_MELODY) melodiesStarted.fetch_add(1, std::memory_order_relaxed);
        if (c.type == MC_PLAY_PCM && c.pcm) c.pcm->queued.fetch_sub(1, std::memory_order_acq_rel);
        if (c.epoch < _epoch) {            // asked for before a mute: drop it
            epochDropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        switch (c.type) {
            case MC_PLAY_PCM:
                if (fxOn.load(std::memory_order_relaxed)) startVoice(c.pcm);
                break;
            case MC_TONE:
                if (!fxOn.load(std::memory_order_relaxed)) break;
                _melody = false;
                tonesStarted.fetch_add(1, std::memory_order_relaxed);
                startNote(c.a, (uint32_t)c.b * OUT_RATE / 1000, (uint32_t)c.b * OUT_RATE / 1000);
                break;
            case MC_MELODY:
                if (!fxOn.load(std::memory_order_relaxed) || c.a <= 0) break;
                _melF = c.freqs; _melD = c.durs; _melLen = c.a; _melIdx = 0;
                _melody = true;
                nextMelodyNote();
                break;
            case MC_STREAM_START: {
                int s = c.a;
                _streamOn[s] = true;
                _streamFrac[s] = 0;
                _streamCur[s] = 0;
                _starved[s] = false;
                _streamStep[s] = (uint32_t)(((uint64_t)streams[s].rate.load() << 16) / OUT_RATE);
                break;
            }
            case MC_STREAM_STOP:
                _streamOn[c.a] = false;
                break;
            default:
                break;
        }
    }

    void startVoice(Pcm* pcm) {
        if (!pcm || !pcm->data || pcm->frames == 0) return;
        Voice* slot = nullptr;
        for (auto &v : _voices) if (!v.pcm) { slot = &v; break; }
        if (!slot) {                        // all busy: the oldest makes way
            slot = &_voices[0];
            for (auto &v : _voices) if (v.age < slot->age) slot = &v;
            stopVoice(*slot);
            stolen.fetch_add(1, std::memory_order_relaxed);
        }
        slot->pcm = pcm;
        slot->pos = 0;
        slot->step = (uint32_t)(((uint64_t)pcm->rate << 16) / OUT_RATE);
        slot->age = ++_age;
        pcm->playing.fetch_add(1, std::memory_order_acq_rel);
        voicesStarted.fetch_add(1, std::memory_order_relaxed);
    }

    void stopVoice(Voice& v) {
        if (v.pcm) v.pcm->playing.fetch_sub(1, std::memory_order_acq_rel);
        v.pcm = nullptr;
    }

    // Nearest-sample resampling: fine for effects recorded at 8-44.1kHz.
    int32_t voiceSample(Voice& v) {
        uint32_t idx = v.pos >> 16;
        if (idx >= v.pcm->frames) { stopVoice(v); return 0; }
        int32_t s = v.pcm->is8bit
            ? ((int32_t)((const uint8_t*)v.pcm->data)[idx] - 128) * 256
            : (int32_t)((const int16_t*)v.pcm->data)[idx];
        v.pos += v.step;
        return s;
    }

    int32_t streamSample(int s) {
        StreamRing& ring = streams[s];
        int32_t out = _streamCur[s];
        _streamFrac[s] += _streamStep[s];
        while (_streamFrac[s] >= (1u << 16)) {
            _streamFrac[s] -= (1u << 16);
            uint32_t r = ring.r.load(std::memory_order_relaxed);
            if (r != ring.w.load(std::memory_order_acquire)) {
                _streamCur[s] = ring.buf[r & (RING_SAMPLES - 1)];
                ring.r.store(r + 1, std::memory_order_release);
                _starved[s] = false;
            } else if (ring.ended.load(std::memory_order_acquire)) {
                _streamOn[s] = false;       // played to the end
                _streamCur[s] = 0;
                break;
            } else {                        // underrun: hold the last sample
                if (!_starved[s]) underruns[s].fetch_add(1, std::memory_order_relaxed);
                _starved[s] = true;
                break;
            }
        }
        return out;
    }

    // A flush the loader asked for: drop everything unread.
    void serviceFlush(int s) {
        StreamRing& ring = streams[s];
        uint32_t g = ring.gen.load(std::memory_order_acquire);
        if (g != ring.ack.load(std::memory_order_relaxed)) {
            ring.r.store(ring.w.load(std::memory_order_acquire), std::memory_order_release);
            _streamOn[s] = false;
            ring.ack.store(g, std::memory_order_release);
        }
    }

    void startNote(int hz, uint32_t total, uint32_t gate) {
        _phase = 0;
        _phaseStep = hz > 0 ? (uint32_t)(((uint64_t)hz << 32) / OUT_RATE) : 0;
        _noteLeft = total;
        _gateLeft = hz > 0 ? gate : 0;
        _synthOn = total > 0;
    }

    // Each melody note sounds for 85% of its length, then rests: the old
    // engine's articulation.
    void nextMelodyNote() {
        if (_melIdx >= _melLen) { _melody = false; _synthOn = false; return; }
        int hz = _melF[_melIdx];
        uint32_t total = (uint32_t)_melD[_melIdx] * OUT_RATE / 1000;
        ++_melIdx;
        startNote(hz, total, total * 85 / 100);
    }

    int32_t synthSample() {
        int32_t s = 0;
        if (_gateLeft > 0) {
            s = (_phase < 0x80000000u) ? SYNTH_AMP : -SYNTH_AMP;
            _phase += _phaseStep;
            --_gateLeft;
        }
        if (_noteLeft > 0 && --_noteLeft == 0) {
            if (_melody) nextMelodyNote();
            else _synthOn = false;
        }
        return s;
    }

    void publishStatus() {
        bool busy = _streamOn[STREAM_JINGLE];
        for (auto &v : _voices) busy |= v.pcm != nullptr;
        fxBusy.store(busy, std::memory_order_relaxed);
        musicBusy.store(_streamOn[STREAM_MUSIC], std::memory_order_relaxed);
        melodyBusy.store(_synthOn && _melody, std::memory_order_relaxed);
        toneBusy.store(_synthOn && !_melody, std::memory_order_relaxed);
    }
};

}  // namespace audiomix

#endif  // AUDIO_MIXER_H
