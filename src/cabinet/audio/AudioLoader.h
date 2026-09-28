#ifndef AUDIO_LOADER_H
#define AUDIO_LOADER_H

// Everything the mixer needs from storage: plain C++ like AudioMixer.h, with
// file access behind AudioFile so the host tests can drive it too.
//
// - Effects (playWAV) are decoded into memory the first time they play and
//   kept (the cache), so after that they start instantly and don't compete
//   with the music for the SD card. preload() does the first load early.
//   A file too big for the cache plays as the jingle stream instead.
// - Music (loopWAV) and jingles stream from SD into the mixer's rings.
//
// It runs on its own task, below the mixer's priority: an SD card that
// stalls holds up only the loader, and the ring keeps the music going
// through ~743ms of that. step() does a bounded amount of work each call
// and never blocks: restarting a stream waits for the mixer's flush
// acknowledgement across calls rather than in one.

#include "AudioMixer.h"

namespace audiomix {

class AudioFile {
public:
    virtual ~AudioFile() {}
    virtual bool     open(const char* path) = 0;
    virtual void     close() = 0;
    virtual bool     isOpen() const = 0;
    virtual int      read(uint8_t* buf, int n) = 0;
    virtual bool     seek(uint32_t pos) = 0;
    virtual uint32_t position() = 0;
};

struct WavInfo {
    uint32_t rate = OUT_RATE;
    uint16_t bits = 16;
    uint16_t channels = 1;
    uint32_t dataOffset = 0;
    uint32_t dataBytes = 0;
    uint32_t frameBytes() const { return (uint32_t)(bits / 8) * channels; }
    uint32_t frames() const     { return frameBytes() ? dataBytes / frameBytes() : 0; }
    uint32_t durationMs() const { return rate ? (uint32_t)((uint64_t)frames() * 1000 / rate) : 0; }
};

inline uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Reads the header, scanning chunks for 'fmt ' and 'data' (files with LIST
// or other chunks are fine), and leaves the file at the first sample.
// 8-bit unsigned or 16-bit signed PCM, mono or stereo, any rate.
inline bool parseWav(AudioFile& f, WavInfo& info) {
    uint8_t hdr[12];
    if (f.read(hdr, 12) != 12) return false;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;
    bool haveFmt = false;
    for (int guard = 0; guard < 32; ++guard) {
        uint8_t ch[8];
        if (f.read(ch, 8) != 8) return false;
        uint32_t size = le32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            uint32_t n = size < 16 ? size : 16;
            if ((uint32_t)f.read(fmt, (int)n) != n || n < 16) return false;
            if (le16(fmt) != 1) return false;                       // PCM only
            info.channels = le16(fmt + 2);
            info.rate     = le32(fmt + 4);
            info.bits     = le16(fmt + 14);
            if (size > 16) f.seek(f.position() + size - 16 + (size & 1));
            haveFmt = true;
        } else if (memcmp(ch, "data", 4) == 0) {
            if (!haveFmt || (info.bits != 8 && info.bits != 16) ||
                info.channels < 1 || info.channels > 2 || info.rate == 0) return false;
            info.dataOffset = f.position();
            info.dataBytes  = size - size % info.frameBytes();
            return true;
        } else {
            f.seek(f.position() + size + (size & 1));               // chunks are word-aligned
        }
    }
    return false;
}

// Whole frames of raw WAV data to mono int16 (stereo is averaged).
inline void decodeFrames(const uint8_t* in, uint32_t frames, const WavInfo& info, int16_t* out) {
    for (uint32_t i = 0; i < frames; ++i) {
        int32_t s;
        if (info.bits == 16) {
            const int16_t* p = (const int16_t*)(in + i * info.frameBytes());
            s = info.channels == 2 ? ((int32_t)p[0] + p[1]) / 2 : p[0];
        } else {
            const uint8_t* p = in + i * info.frameBytes();
            s = info.channels == 2 ? (((int32_t)p[0] + p[1]) / 2 - 128) * 256 : ((int32_t)p[0] - 128) * 256;
        }
        out[i] = (int16_t)s;
    }
}

enum LoadCmdType : uint8_t {
    LC_PLAY_FX,         // path
    LC_PRELOAD,         // path: into the cache, not played
    LC_LOOP_MUSIC,      // path
    LC_PLAY_THEN_LOOP,  // path (one-shot, on the jingle stream), path2 (music after)
    LC_STOP_STREAMS,    // music and jingle (stopLoop)
    LC_STOP_ALL,        // everything (mute)
};

struct LoadCmd {
    LoadCmdType type = LC_STOP_ALL;
    uint32_t    epoch = 0;
    char        path[48] = "";
    char        path2[48] = "";
};

class AudioLoader {
public:
    using AllocFn = void* (*)(size_t);
    using FreeFn  = void (*)(void*);
    static constexpr int      CACHE_SLOTS = 24;
    static constexpr uint32_t CHUNK_BYTES = 2048;
    static constexpr int      PENDING_MAX = 6;
    static constexpr int      FEED_CHUNKS = 4;    // per stream per step(): up to ~8KB
    static constexpr int      LOAD_CHUNKS = 4;    // effect loading per step()
    static constexpr int      READ_RETRIES = 3;   // failed reads before a file counts as ended

    // Event log (file opens, stream starts, read failures): printf-style,
    // or null for none. Called on the loader's task only.
    using LogFn = void (*)(const char* fmt, ...);
    LogFn log = nullptr;

    // Diagnostics: only ever counted up, here.
    std::atomic<uint32_t> cacheHits{0};       // effects played from memory
    std::atomic<uint32_t> loads{0};           // effects read from SD into the cache
    std::atomic<uint32_t> bigPlays{0};        // effects too big to cache: streamed as the jingle
    std::atomic<uint32_t> dropped{0};         // playWAVs given up (too many waiting, fx off...)
    std::atomic<uint32_t> readErrors{0};      // reads that returned nothing mid-file
    std::atomic<uint32_t> shortReads{0};      // reads that returned less than asked
    std::atomic<uint32_t> loops{0};           // music loop restarts

    Spsc<LoadCmd, 16> fromGame;
    std::atomic<uint32_t> lastDurationMs{0};  // of the last effect started (getLastWAVDurationMs)
    std::atomic<int>      inFlight{0};        // playWAVs not yet playing or given up on

    AudioLoader(Mixer& mixer, AudioFile* musicFile, AudioFile* jingleFile, AudioFile* loadFile,
                AllocFn alloc, FreeFn freeFn, uint32_t cacheBudget, uint32_t maxEntry)
        : _mixer(mixer), _alloc(alloc), _free(freeFn), _budget(cacheBudget), _maxEntry(maxEntry), _loadFile(loadFile) {
        _music.file = musicFile;
        _jingle.file = jingleFile;
    }

    ~AudioLoader() {
        for (auto &e : _cache) if (e.inUse) freeEntry(e);
    }

    // Returns true if it did anything, so the task can yield less when busy.
    bool step() {
        bool work = false;
        LoadCmd c;
        while (fromGame.pop(c)) { handle(c); work = true; }
        if (_music.active && !_mixer.musicOn.load()) { stopStream(_music, STREAM_MUSIC, _music.epoch); work = true; }
        if (_loadSlot >= 0) { loadChunk(); work = true; }
        else if (_pendingCount > 0) { startNextPending(); work = true; }
        work |= feed(_music, STREAM_MUSIC);
        work |= feed(_jingle, STREAM_JINGLE);
        // A jingle played to its end: start the music that was to follow it.
        if (_jingle.active && _jingle.draining && _mixer.streams[STREAM_JINGLE].filled() == 0) {
            _jingle.active = false;
            if (_jingle.thenLoop[0] && _mixer.musicOn.load()) {
                startStream(_music, STREAM_MUSIC, _jingle.thenLoop, true, _jingle.epoch);
            }
            _jingle.thenLoop[0] = '\0';
        }
        return work;
    }

    // For the host tests.
    bool     isCached(const char* path) const { int i = find(path); return i >= 0 && _cache[i].ready; }
    uint32_t cacheBytes() const { return _used; }
    bool     loading() const { return _loadSlot >= 0; }
    bool     streaming(int s) const { return s == STREAM_MUSIC ? _music.active : _jingle.active; }

private:
    struct Entry {
        char     path[48] = "";
        Pcm      pcm;
        uint32_t bytes = 0;
        uint32_t lastUse = 0;
        bool     inUse = false;     // slot allocated
        bool     ready = false;     // fully loaded
    };
    struct Stream {
        AudioFile* file = nullptr;
        char       path[48] = "";
        int        failures = 0;        // consecutive failed reads
        WavInfo    info;
        bool       active = false;      // open, or still draining
        bool       loop = false;
        bool       starting = false;    // waiting for the mixer to acknowledge a flush
        bool       draining = false;    // file done, ring still playing
        uint32_t   flushGen = 0;
        uint32_t   remaining = 0;       // bytes left before end of data (or loop point)
        uint32_t   epoch = 0;
        char       thenLoop[48] = "";
    };
    struct Pending { char path[48]; uint32_t epoch; int waiters; };

    Mixer&   _mixer;
    AllocFn  _alloc;
    FreeFn   _free;
    uint32_t _budget, _maxEntry, _used = 0, _clock = 0;
    Entry    _cache[CACHE_SLOTS];
    AudioFile* _loadFile;
    int      _loadSlot = -1;        // entry being loaded
    WavInfo  _loadInfo;
    int      _loadFailures = 0;
    uint32_t _loadDone = 0;         // frames decoded so far
    int      _loadWaiters = 0;      // playWAVs waiting for it
    uint32_t _loadEpoch = 0;
    Pending  _pending[PENDING_MAX];
    int      _pendingCount = 0;
    Stream   _music, _jingle;
    uint8_t  _raw[CHUNK_BYTES];
    int16_t  _pcm[CHUNK_BYTES];     // worst case: 8-bit mono, one sample per byte

    template <typename... A>
    void note(const char* fmt, A... a) { if (log) log(fmt, a...); }

    void resolve(int n) { if (n > 0) inFlight.fetch_sub(n, std::memory_order_acq_rel); }

    int find(const char* path) const {
        for (int i = 0; i < CACHE_SLOTS; ++i) {
            if (_cache[i].inUse && strcmp(_cache[i].path, path) == 0) return i;
        }
        return -1;
    }

    void post(Entry& e, uint32_t epoch) {
        e.lastUse = ++_clock;
        e.pcm.queued.fetch_add(1, std::memory_order_acq_rel);
        MixCmd m;
        m.type = MC_PLAY_PCM;
        m.epoch = epoch;
        m.pcm = &e.pcm;
        if (!_mixer.fromLoader.push(m)) e.pcm.queued.fetch_sub(1, std::memory_order_acq_rel);
        lastDurationMs.store(e.pcm.rate ? (uint32_t)((uint64_t)e.pcm.frames * 1000 / e.pcm.rate) : 0);
    }

    void handle(const LoadCmd& c) {
        switch (c.type) {
            case LC_PLAY_FX:
            case LC_PRELOAD: {
                const int waiters = c.type == LC_PLAY_FX ? 1 : 0;
                if (waiters && !_mixer.fxOn.load()) { resolve(1); break; }
                int i = find(c.path);
                if (i >= 0 && _cache[i].ready) {
                    if (waiters) {
                        cacheHits.fetch_add(1, std::memory_order_relaxed);
                        post(_cache[i], c.epoch);
                        resolve(1);
                    }
                } else if (i >= 0 && i == _loadSlot) {
                    _loadWaiters += waiters;
                    if (c.epoch > _loadEpoch) _loadEpoch = c.epoch;
                } else if (_loadSlot < 0) {
                    startLoad(c.path, c.epoch, waiters);
                } else if (_pendingCount < PENDING_MAX) {
                    Pending& p = _pending[_pendingCount++];
                    strncpy(p.path, c.path, sizeof(p.path) - 1);
                    p.path[sizeof(p.path) - 1] = '\0';
                    p.epoch = c.epoch;
                    p.waiters = waiters;
                } else {
                    if (waiters) dropped.fetch_add(1, std::memory_order_relaxed);
                    resolve(waiters);            // too much at once: this one's dropped
                }
                break;
            }
            case LC_LOOP_MUSIC:
                if (_mixer.musicOn.load()) startStream(_music, STREAM_MUSIC, c.path, true, c.epoch);
                break;
            case LC_PLAY_THEN_LOOP:
                if (_mixer.fxOn.load()) {
                    startStream(_jingle, STREAM_JINGLE, c.path, false, c.epoch);
                    strncpy(_jingle.thenLoop, c.path2, sizeof(_jingle.thenLoop) - 1);
                } else if (_mixer.musicOn.load()) {
                    startStream(_music, STREAM_MUSIC, c.path2, true, c.epoch);
                }
                break;
            case LC_STOP_STREAMS:
                stopStream(_music, STREAM_MUSIC, c.epoch);
                stopStream(_jingle, STREAM_JINGLE, c.epoch);
                break;
            case LC_STOP_ALL:
                stopStream(_music, STREAM_MUSIC, c.epoch);
                stopStream(_jingle, STREAM_JINGLE, c.epoch);
                if (_loadSlot >= 0) abortLoad();
                for (int i = 0; i < _pendingCount; ++i) resolve(_pending[i].waiters);
                _pendingCount = 0;
                break;
        }
    }

    void startNextPending() {
        Pending p = _pending[0];
        for (int i = 1; i < _pendingCount; ++i) _pending[i - 1] = _pending[i];
        --_pendingCount;
        int i = find(p.path);
        if (i >= 0 && _cache[i].ready) {         // loaded meanwhile (queued twice)
            if (p.waiters) { post(_cache[i], p.epoch); resolve(p.waiters); }
            return;
        }
        startLoad(p.path, p.epoch, p.waiters);
    }

    // Frees least-recently-used entries nothing is playing or about to
    // play, until `bytes` more fits in the budget.
    bool makeRoom(uint32_t bytes) {
        while (_used + bytes > _budget) {
            int victim = -1;
            for (int i = 0; i < CACHE_SLOTS; ++i) {
                Entry& e = _cache[i];
                if (!e.inUse || !e.ready || i == _loadSlot) continue;
                if (e.pcm.playing.load() > 0 || e.pcm.queued.load() > 0) continue;
                if (victim < 0 || e.lastUse < _cache[victim].lastUse) victim = i;
            }
            if (victim < 0) return false;
            freeEntry(_cache[victim]);
        }
        return true;
    }

    void freeEntry(Entry& e) {
        if (e.pcm.data) _free((void*)e.pcm.data);
        _used -= e.bytes;
        e.pcm.data = nullptr;
        e.inUse = e.ready = false;
        e.bytes = 0;
        e.path[0] = '\0';
    }

    void startLoad(const char* path, uint32_t epoch, int waiters) {
        if (!_loadFile->open(path)) {
            note("[AUDIO] can't open %s\n", path);
            resolve(waiters);
            return;
        }
        WavInfo info;
        if (!parseWav(*_loadFile, info) || info.frames() == 0) {
            note("[AUDIO] %s: not a PCM WAV\n", path);
            _loadFile->close();
            resolve(waiters);
            return;
        }
        if (waiters) lastDurationMs.store(info.durationMs());
        const uint32_t bytes = info.frames() * 2;
        int slot = -1;
        if (bytes <= _maxEntry && makeRoom(bytes)) {
            for (int i = 0; i < CACHE_SLOTS; ++i) if (!_cache[i].inUse) { slot = i; break; }
        }
        void* mem = slot >= 0 ? _alloc(bytes) : nullptr;
        if (!mem) {
            // Too long for the cache (or no room): play it as the jingle
            // stream instead. A preload of one is simply skipped.
            _loadFile->close();
            note("[AUDIO] %s: %uHz %ubit %uch %ums, too big to cache (%ukB)%s\n", path,
                 (unsigned)info.rate, (unsigned)info.bits, (unsigned)info.channels,
                 (unsigned)info.durationMs(), (unsigned)(bytes / 1024),
                 waiters ? ": streaming it" : "");
            if (waiters) {
                bigPlays.fetch_add(1, std::memory_order_relaxed);
                startStream(_jingle, STREAM_JINGLE, path, false, epoch);
            }
            resolve(waiters);
            return;
        }
        note("[AUDIO] %s: %uHz %ubit %uch %ums, caching %ukB\n", path,
             (unsigned)info.rate, (unsigned)info.bits, (unsigned)info.channels,
             (unsigned)info.durationMs(), (unsigned)(bytes / 1024));
        loads.fetch_add(1, std::memory_order_relaxed);
        Entry& e = _cache[slot];
        strncpy(e.path, path, sizeof(e.path) - 1);
        e.path[sizeof(e.path) - 1] = '\0';
        e.pcm.data = mem;
        e.pcm.frames = info.frames();
        e.pcm.rate = info.rate;
        e.pcm.is8bit = false;
        e.bytes = bytes;
        e.inUse = true;
        e.ready = false;
        _used += bytes;
        _loadSlot = slot;
        _loadInfo = info;
        _loadDone = 0;
        _loadFailures = 0;
        _loadWaiters = waiters;
        _loadEpoch = epoch;
    }

    // Decodes up to LOAD_CHUNKS more of the effect being loaded. A short
    // read keeps its place (whole frames only); a failed one is retried on
    // the next call, and after READ_RETRIES the effect keeps what it has.
    void loadChunk() {
        Entry& e = _cache[_loadSlot];
        const uint32_t fb = _loadInfo.frameBytes();
        bool done = false;
        for (int n = 0; n < LOAD_CHUNKS && !done; ++n) {
            uint32_t frames = CHUNK_BYTES / fb;
            if (frames > e.pcm.frames - _loadDone) frames = e.pcm.frames - _loadDone;
            if (frames == 0) { done = true; break; }
            int got = _loadFile->read(_raw, (int)(frames * fb));
            if (got <= 0) {
                readErrors.fetch_add(1, std::memory_order_relaxed);
                if (++_loadFailures < READ_RETRIES) break;
                note("[AUDIO] %s: read failed at frame %u of %u, keeping what loaded\n",
                     e.path, (unsigned)_loadDone, (unsigned)e.pcm.frames);
                done = true;
                break;
            }
            _loadFailures = 0;
            uint32_t whole = (uint32_t)got / fb;
            if (whole < frames) shortReads.fetch_add(1, std::memory_order_relaxed);
            uint32_t extra = (uint32_t)got - whole * fb;
            if (extra) _loadFile->seek(_loadFile->position() - extra);   // stay frame-aligned
            decodeFrames(_raw, whole, _loadInfo, (int16_t*)e.pcm.data + _loadDone);
            _loadDone += whole;
            if (_loadDone >= e.pcm.frames) done = true;
            if (whole < frames) break;                                   // let the card catch up
        }
        if (!done) return;
        e.pcm.frames = _loadDone;
        e.ready = true;
        _loadFile->close();
        _loadSlot = -1;
        if (_loadWaiters) { post(e, _loadEpoch); resolve(_loadWaiters); }
        _loadWaiters = 0;
    }

    void abortLoad() {
        _loadFile->close();
        freeEntry(_cache[_loadSlot]);
        _loadSlot = -1;
        resolve(_loadWaiters);
        _loadWaiters = 0;
    }

    void startStream(Stream& st, int slot, const char* path, bool loop, uint32_t epoch) {
        stopStream(st, slot, epoch);
        const char* name = slot == STREAM_MUSIC ? "music" : "jingle";
        if (!st.file->open(path)) { note("[AUDIO] %s: can't open %s\n", name, path); return; }
        if (!parseWav(*st.file, st.info) || st.info.frames() == 0) {
            note("[AUDIO] %s: %s is not a PCM WAV\n", name, path);
            st.file->close();
            return;
        }
        note("[AUDIO] %s: %s %uHz %ubit %uch %ums%s\n", name, path, (unsigned)st.info.rate,
             (unsigned)st.info.bits, (unsigned)st.info.channels, (unsigned)st.info.durationMs(),
             loop ? ", looping" : "");
        strncpy(st.path, path, sizeof(st.path) - 1);
        st.path[sizeof(st.path) - 1] = '\0';
        st.failures = 0;
        if (slot == STREAM_JINGLE) lastDurationMs.store(st.info.durationMs());
        st.active = true;
        st.loop = loop;
        st.draining = false;
        st.remaining = st.info.dataBytes;
        st.epoch = epoch;
        // Flush whatever the ring still holds; feed() starts once the mixer
        // has acknowledged.
        st.flushGen = _mixer.streams[slot].gen.load() + 1;
        _mixer.streams[slot].gen.store(st.flushGen);
        st.starting = true;
    }

    void stopStream(Stream& st, int slot, uint32_t epoch) {
        if (st.file->isOpen()) st.file->close();
        if (st.active) {
            _mixer.streams[slot].gen.store(_mixer.streams[slot].gen.load() + 1);   // drop what's queued
            MixCmd m;
            m.type = MC_STREAM_STOP;
            m.epoch = epoch;
            m.a = slot;
            _mixer.fromLoader.push(m);
        }
        st.active = st.starting = st.draining = false;
        if (slot == STREAM_JINGLE) st.thenLoop[0] = '\0';
    }

    // Tops the ring up (at most two chunks a call). A new stream gets its
    // first chunk before the mixer is told to start, so it doesn't begin
    // with an underrun.
    bool feed(Stream& st, int slot) {
        if (!st.active || st.draining) return false;
        StreamRing& ring = _mixer.streams[slot];
        if (st.starting) {
            if (ring.ack.load() != st.flushGen) return false;   // mixer hasn't flushed yet
            ring.ended.store(false);                             // left over from the last stream
        }
        bool work = false;
        const uint32_t fb = st.info.frameBytes();
        for (int n = 0; n < FEED_CHUNKS; ++n) {
            uint32_t frames = CHUNK_BYTES / fb;
            if (frames > ring.space()) break;                    // full enough for now
            if (frames * fb > st.remaining) frames = st.remaining / fb;
            bool ended = frames == 0;
            if (!ended) {
                int got = st.file->read(_raw, (int)(frames * fb));
                if (got <= 0) {
                    // Nothing came back: try again next call, rather than
                    // taking it as the end of the file (which, looping,
                    // would jump back to the start).
                    readErrors.fetch_add(1, std::memory_order_relaxed);
                    if (++st.failures < READ_RETRIES) break;
                    note("[AUDIO] %s: read failed %u bytes before the end, %s\n", st.path,
                         (unsigned)st.remaining, st.loop ? "looping early" : "ending early");
                    st.failures = 0;
                    ended = true;
                } else {
                    st.failures = 0;
                    uint32_t whole = (uint32_t)got / fb;
                    uint32_t extra = (uint32_t)got - whole * fb;
                    if (whole < frames) shortReads.fetch_add(1, std::memory_order_relaxed);
                    if (extra) st.file->seek(st.file->position() - extra);   // stay frame-aligned
                    if (whole) {
                        decodeFrames(_raw, whole, st.info, _pcm);
                        ring.write(_pcm, whole);
                        st.remaining -= whole * fb;
                        work = true;
                    }
                    ended = st.remaining == 0;
                    if (!ended && whole < frames) break;             // let the card catch up
                }
            }
            if (ended) {
                if (st.loop) {
                    st.file->seek(st.info.dataOffset);          // seamless: the next chunk follows on
                    st.remaining = st.info.dataBytes;
                    loops.fetch_add(1, std::memory_order_relaxed);
                } else {
                    st.file->close();
                    ring.ended.store(true);
                    st.draining = true;
                    break;
                }
            }
        }
        if (st.starting && (ring.filled() > 0 || st.draining)) {
            st.starting = false;
            ring.rate.store(st.info.rate);
            MixCmd m;
            m.type = MC_STREAM_START;
            m.epoch = st.epoch;
            m.a = slot;
            _mixer.fromLoader.push(m);
        }
        return work;
    }
};

}  // namespace audiomix

#endif  // AUDIO_LOADER_H
