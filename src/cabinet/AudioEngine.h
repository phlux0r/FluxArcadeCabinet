#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

// Build with -DAUDIO_LEGACY for the old one-sound-at-a-time engine, in case
// the mixer misbehaves on the hardware (see platformio.ini).
#ifdef AUDIO_LEGACY
#include "AudioEngineLegacy.h"
#else

#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include "ArcadeConfig.h"
#include "audio/AudioLoader.h"

// =============================================================================
// AUDIO ENGINE: MAX98357A via I2S, with a software mixer.
//
// Music and effects play at the same time. Everything playing is summed
// into one stream for the single I2S output (audio/AudioMixer.h): one music
// track, a jingle, four effects and the tone synth at once. Music and
// effects can each be switched off, under a master volume (the launcher's
// setup page, saved in NVS).
//
// Two tasks on core 0, the game loop being on core 1:
//   mixer   (priority 5): mixes 256-frame blocks and writes them to I2S.
//           It never touches the SD card, so it never stalls; only ~35ms
//           of audio is queued ahead of the speaker, so sounds start fast.
//   loader  (priority 3): all SD access (audio/AudioLoader.h). Effects are
//           decoded into PSRAM the first time and kept; music and jingles
//           stream into ring buffers holding ~743ms, which rides out a
//           slow card.
// The game loop only posts commands; nothing here blocks it.
//
// API: the same as the old engine, so every game works unchanged, and now
//   loopWAV()         music (was: stopped by any other sound)
//   playWAV()         an effect, mixed over everything
//   playTone/Melody   the synth, mixed over everything (was: skipped while
//                     a WAV played)
//   preload()         load an effect ahead of its first play
//   setMusicVolume / setFxVolume (0 = off), setVolume (master)
//
// SD CARD PATHS: see the README's SD card section. WAVs: 8 or 16-bit PCM,
// mono or stereo, any rate (mixed at 44.1kHz).
// FALLBACK: PROGMEM 8kHz 8-bit arrays when there's no SD card.
// =============================================================================

#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_D5   587
#define NOTE_E5   659
#define NOTE_G5   784
#define NOTE_A5   880
#define NOTE_C6  1047
#define NOTE_REST   0

// Serial diagnostics: file opens, stream starts, read failures, and every
// 2s (while anything's happening) a line of counters. See the README's
// "Audio diagnostics". Build with -DAUDIO_DEBUG=0 to silence them.
#ifndef AUDIO_DEBUG
#define AUDIO_DEBUG 1
#endif

static const i2s_port_t I2S_PORT = I2S_NUM_0;

namespace audiocfg {
constexpr int      MIX_BLOCK       = 256;          // frames per mix (~5.8ms)
constexpr int      DMA_BUF_LEN     = 256;          // frames
constexpr int      DMA_BUF_COUNT   = 6;            // ~35ms queued ahead of the speaker
constexpr uint32_t CACHE_BUDGET    = 768 * 1024;   // decoded effects, in PSRAM
constexpr uint32_t CACHE_MAX_ENTRY = 320 * 1024;   // ~3.7s at 44.1kHz mono; longer plays as a jingle
constexpr int      MIXER_PRIO      = 5, LOADER_PRIO = 3, AUDIO_CORE = 0;
constexpr int      MIXER_STACK     = 4096, LOADER_STACK = 8192;
constexpr int      PGM_SLOTS       = 4;            // PROGMEM fallback samples playing at once
}  // namespace audiocfg

// SD card access for the loader.
class SdAudioFile : public audiomix::AudioFile {
public:
    bool     open(const char* path) override { close(); _f = SD.open(path, FILE_READ); return (bool)_f; }
    void     close() override { if (_f) _f.close(); }
    bool     isOpen() const override { return (bool)_f; }
    int      read(uint8_t* b, int n) override { return _f ? (int)_f.read(b, (size_t)n) : 0; }
    bool     seek(uint32_t p) override { return _f && _f.seek(p); }
    uint32_t position() override { return _f ? (uint32_t)_f.position() : 0; }
private:
    File _f;
};

// Effects and rings go in PSRAM (2MB, mostly unused), internal RAM being
// the scarce kind on this board; internal only if PSRAM is missing.
inline void* audioAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_8BIT);
}
inline void audioFree(void* p) { heap_caps_free(p); }

// `inline`, not `static`: this header is included from more than one .cpp,
// and every file must share the one mixer and loader the tasks run.
inline audiomix::Mixer        _audioMixer;
inline SdAudioFile            _audioMusicFile, _audioJingleFile, _audioLoadFile;
inline audiomix::AudioLoader  _audioLoader(_audioMixer, &_audioMusicFile, &_audioJingleFile, &_audioLoadFile,
                                           audioAlloc, audioFree,
                                           audiocfg::CACHE_BUDGET, audiocfg::CACHE_MAX_ENTRY);

// Diagnostics the tasks and the game side fill in (see AUDIO_DEBUG).
namespace audiodiag {
inline std::atomic<uint32_t> wavRequests{0}, toneRequests{0};
inline std::atomic<uint32_t> mixQueueFull{0}, loadQueueFull{0};
inline std::atomic<uint32_t> renderMaxUs{0};    // longest render() (CPU cost)
inline std::atomic<uint32_t> periodMaxUs{0};    // longest time between blocks: over
                                                // ~35ms the speaker ran dry
inline std::atomic<uint32_t> stepMaxUs{0};      // longest loader step (mostly SD time)

inline void raiseMax(std::atomic<uint32_t>& m, uint32_t v) { if (v > m.load()) m.store(v); }

inline void log(const char* fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
}

// One line of counters since boot, and the worst timings since the last
// line. Printed from the loader task, only when a counter has moved.
inline void report() {
    static uint32_t last[8] = {};
    const auto& M = _audioMixer;
    const auto& L = _audioLoader;
    uint32_t now[8] = { wavRequests.load(), toneRequests.load(), M.voicesStarted.load(),
                        M.underruns[audiomix::STREAM_MUSIC].load(), L.readErrors.load(),
                        L.shortReads.load(), mixQueueFull.load() + loadQueueFull.load(),
                        L.loops.load() };
    if (memcmp(now, last, sizeof(now)) == 0) return;
    memcpy(last, now, sizeof(now));
    Serial.printf("[AUDIO] wav %u (hit %u load %u big %u drop %u) voices %u stolen %u | tone %u/%u"
                  " | music underrun %u loops %u, jingle underrun %u | sd err %u short %u"
                  " | qfull %u/%u epoch-drop %u | max: loader %ums mix %uus period %ums\n",
                  (unsigned)now[0], (unsigned)L.cacheHits.load(), (unsigned)L.loads.load(),
                  (unsigned)L.bigPlays.load(), (unsigned)L.dropped.load(),
                  (unsigned)now[2], (unsigned)M.stolen.load(),
                  (unsigned)M.tonesStarted.load(), (unsigned)now[1],
                  (unsigned)now[3], (unsigned)now[7],
                  (unsigned)M.underruns[audiomix::STREAM_JINGLE].load(),
                  (unsigned)now[4], (unsigned)now[5],
                  (unsigned)mixQueueFull.load(), (unsigned)loadQueueFull.load(),
                  (unsigned)M.epochDropped.load(),
                  (unsigned)(stepMaxUs.load() / 1000), (unsigned)renderMaxUs.load(),
                  (unsigned)(periodMaxUs.load() / 1000));
    stepMaxUs.store(0);
    renderMaxUs.store(0);
    periodMaxUs.store(0);
}
}  // namespace audiodiag

inline void audioMixerTask(void*) {
    static int16_t out[audiocfg::MIX_BLOCK * 2];
    uint32_t prev = micros();
    for (;;) {
        const uint32_t t0 = micros();
        _audioMixer.render(out, audiocfg::MIX_BLOCK);
        audiodiag::raiseMax(audiodiag::renderMaxUs, micros() - t0);
        audiodiag::raiseMax(audiodiag::periodMaxUs, t0 - prev);
        prev = t0;
        size_t bw = 0;
        i2s_write(I2S_PORT, out, sizeof(out), &bw, portMAX_DELAY);   // paces the task
    }
}

inline void audioLoaderTask(void*) {
    uint32_t lastReport = millis();
    for (;;) {
        const uint32_t t0 = micros();
        _audioLoader.step();
        audiodiag::raiseMax(audiodiag::stepMaxUs, micros() - t0);
#if AUDIO_DEBUG
        if (millis() - lastReport >= 2000) {
            lastReport = millis();
            audiodiag::report();
        }
#endif
        // Always sleep a tick: step() is bounded, and core 0's idle task
        // must run or the task watchdog fires.
        vTaskDelay(1);
    }
}

// =============================================================================
// AUDIO ENGINE CLASS: the game loop's side. Posts commands; never blocks.
// =============================================================================
class AudioEngine {
private:
    bool     _ready = false;
    uint32_t _epoch = 0;                 // bumped by mute(); see AudioMixer.h
    uint32_t _melodiesRequested = 0;
    audiomix::Pcm _pgm[audiocfg::PGM_SLOTS];
    int      _pgmNext = 0;

    // ---- Deferred WAV-open-failed fallbacks (see playJumpSound) ----
    bool          _jumpFallbackPending = false;
    unsigned long _jumpFallbackCheckAt = 0;
    bool          _deathFallbackPending = false;
    unsigned long _deathFallbackCheckAt = 0;
    bool          _gameOverFallbackPending = false;
    unsigned long _gameOverFallbackCheckAt = 0;

    void loaderCmd(audiomix::LoadCmdType t, const char* path = "", const char* path2 = "") {
        audiomix::LoadCmd c;
        c.type = t;
        c.epoch = _epoch;
        strncpy(c.path, path, sizeof(c.path) - 1);
        strncpy(c.path2, path2, sizeof(c.path2) - 1);
        if (t == audiomix::LC_PLAY_FX) {
            _audioLoader.inFlight.fetch_add(1);
            audiodiag::wavRequests.fetch_add(1);
        }
        if (!_audioLoader.fromGame.push(c)) {
            audiodiag::loadQueueFull.fetch_add(1);
            if (t == audiomix::LC_PLAY_FX) _audioLoader.inFlight.fetch_sub(1);
        }
    }

    bool mixerCmd(audiomix::MixCmd m) {
        m.epoch = _epoch;
        if (_audioMixer.fromGame.push(m)) return true;
        audiodiag::mixQueueFull.fetch_add(1);
        return false;
    }

public:
    AudioEngine() {}

    // -------------------------------------------------------------------------
    // INIT
    // -------------------------------------------------------------------------
    bool begin() {
        for (int s = 0; s < audiomix::STREAMS; ++s) {
            _audioMixer.streams[s].buf = (int16_t*)audioAlloc(audiomix::RING_SAMPLES * sizeof(int16_t));
            if (!_audioMixer.streams[s].buf) return false;
        }
        i2s_config_t cfg = {
            .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate          = ArcadeConfig::I2S_SAMPLE_RATE,
            .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count        = audiocfg::DMA_BUF_COUNT,
            .dma_buf_len          = audiocfg::DMA_BUF_LEN,
            .use_apll             = false,
            .tx_desc_auto_clear   = true,
            .fixed_mclk           = 0
        };
        i2s_pin_config_t pins = {
            .bck_io_num   = ArcadeConfig::I2S_BCLK,
            .ws_io_num    = ArcadeConfig::I2S_LRC,
            .data_out_num = ArcadeConfig::I2S_DIN,
            .data_in_num  = I2S_PIN_NO_CHANGE
        };
        if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;
        if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) return false;
        i2s_zero_dma_buffer(I2S_PORT);

        xTaskCreatePinnedToCore(audioMixerTask, "audioMix", audiocfg::MIXER_STACK, nullptr,
                                audiocfg::MIXER_PRIO, nullptr, audiocfg::AUDIO_CORE);
        xTaskCreatePinnedToCore(audioLoaderTask, "audioLoad", audiocfg::LOADER_STACK, nullptr,
                                audiocfg::LOADER_PRIO, nullptr, audiocfg::AUDIO_CORE);
#if AUDIO_DEBUG
        _audioLoader.log = audiodiag::log;
#endif
        _ready = true;
        Serial.printf("[AUDIO] Mixer ready: %d effect voices, music + jingle streams, %s cache.\n",
                      audiomix::FX_VOICES, psramFound() ? "PSRAM" : "internal-RAM");
        return true;
    }

    // -------------------------------------------------------------------------
    // SETTINGS
    // -------------------------------------------------------------------------
    void setVolume(float v) {
        v = constrain(v, 0.0f, 1.0f);
        _audioMixer.masterQ15.store((int32_t)(v * 32768.0f));
    }
    float getVolume() const { return _audioMixer.masterQ15.load() / 32768.0f; }
    // Each bus's volume, under the master. 0 switches it off (music then
    // isn't streamed from SD at all).
    void setMusicVolume(float v) {
        v = constrain(v, 0.0f, 1.0f);
        _audioMixer.musicQ15.store((int32_t)(v * 32768.0f));
        _audioMixer.musicOn.store(v > 0.0f);
    }
    void setFxVolume(float v) {
        v = constrain(v, 0.0f, 1.0f);
        _audioMixer.fxQ15.store((int32_t)(v * 32768.0f));
        _audioMixer.fxOn.store(v > 0.0f);
    }
    float getMusicVolume() const { return _audioMixer.musicQ15.load() / 32768.0f; }
    float getFxVolume() const    { return _audioMixer.fxQ15.load() / 32768.0f; }
    void setMusicEnabled(bool on) { _audioMixer.musicOn.store(on); }
    void setFxEnabled(bool on)    { _audioMixer.fxOn.store(on); }
    bool isMusicEnabled() const   { return _audioMixer.musicOn.load(); }
    bool isFxEnabled() const      { return _audioMixer.fxOn.load(); }

    // -------------------------------------------------------------------------
    // WAVs FROM SD
    // -------------------------------------------------------------------------
    void playWAV(const char* path) {
        if (!_ready) return;
        _audioLoader.lastDurationMs.store(0);   // set again once its header's read
        loaderCmd(audiomix::LC_PLAY_FX, path);
    }
    void loopWAV(const char* path) {
        if (_ready) loaderCmd(audiomix::LC_LOOP_MUSIC, path);
    }
    // A one-shot, then loop another as music.
    void playWAVThenLoop(const char* oneShotPath, const char* loopPath) {
        if (_ready) loaderCmd(audiomix::LC_PLAY_THEN_LOOP, oneShotPath, loopPath);
    }
    void stopLoop() {
        if (_ready) loaderCmd(audiomix::LC_STOP_STREAMS);
    }
    // Loads an effect into memory now, so its first play isn't held up by
    // the SD card. Worth calling in a game's init() for its frequent sounds.
    void preload(const char* path) {
        if (_ready) loaderCmd(audiomix::LC_PRELOAD, path);
    }

    // Duration of the most recent effect, once its header's been read (0
    // until then, or if it couldn't be opened). Use to time gameplay phases.
    uint32_t getLastWAVDurationMs() const { return _audioLoader.lastDurationMs.load(); }

    // An effect or jingle playing, or asked for and not yet started.
    bool isWAVPlaying() const    { return _audioMixer.fxBusy.load() || _audioLoader.inFlight.load() > 0; }
    bool isSamplePlaying() const { return isWAVPlaying(); }

    // -------------------------------------------------------------------------
    // PROGMEM FALLBACK (8kHz 8-bit WAV arrays, 44-byte header)
    // -------------------------------------------------------------------------
    void startSamplePROGMEM(const uint8_t* data, size_t len) {
        if (!_ready || !data || len <= 44) return;
        for (int tries = 0; tries < audiocfg::PGM_SLOTS; ++tries) {
            audiomix::Pcm& p = _pgm[_pgmNext];
            _pgmNext = (_pgmNext + 1) % audiocfg::PGM_SLOTS;
            if (p.playing.load() > 0 || p.queued.load() > 0) continue;
            p.data = data + 44;
            p.frames = (uint32_t)(len - 44);
            p.rate = 8000;
            p.is8bit = true;
            p.queued.fetch_add(1);
            audiomix::MixCmd m;
            m.type = audiomix::MC_PLAY_PCM;
            m.pcm = &p;
            if (!mixerCmd(m)) p.queued.fetch_sub(1);
            return;
        }
    }

    // -------------------------------------------------------------------------
    // CONVENIENCE WRAPPERS: SD WAV, else the PROGMEM fallback or a melody
    // -------------------------------------------------------------------------
    void playStartupSound(const uint8_t* fallback, size_t fbLen) {
        if (SD.cardType() != CARD_NONE) playWAV("/audio/gamestart.wav");
        else startSamplePROGMEM(fallback, fbLen);
    }
    void playGameOverSound(const uint8_t* fallback, size_t fbLen) {
        if (SD.cardType() != CARD_NONE) playWAV("/audio/gameend.wav");
        else startSamplePROGMEM(fallback, fbLen);
    }
    void playExplosionSound(const uint8_t* fallback, size_t fbLen) {
        if (SD.cardType() != CARD_NONE) playWAV("/audio/explosion.wav");
        else startSamplePROGMEM(fallback, fbLen);
    }
    void playLanderStartSound() {
        if (SD.cardType() != CARD_NONE) playWAV("/audio/lander_start.wav");
        else playLaunchMelody();
    }
    void playTankStartSound() {
        if (SD.cardType() != CARD_NONE) playWAV("/audio/tank_start.wav");
        else playTankStartMelody();
    }
    void playLandingSuccessSound() {
        if (SD.cardType() != CARD_NONE) playWAV("/audio/land_success.wav");
        else playLandingSuccess();
    }

    // /audio/jump.wav, or a two-note blip if there's no card or no such
    // file. Whether the file opened isn't known at once (the loader reads
    // it), so update() checks for its duration appearing, with a generous
    // deadline, before falling back.
    void playJumpSound() {
        if (SD.cardType() != CARD_NONE) {
            playWAV("/audio/jump.wav");
            _jumpFallbackPending = true;
            _jumpFallbackCheckAt = millis() + 300;
        } else {
            playJumpBlip();
        }
    }
    void playDeathSound() {
        if (SD.cardType() != CARD_NONE) {
            playWAV("/audio/death.wav");
            _deathFallbackPending = true;
            _deathFallbackCheckAt = millis() + 300;
        } else {
            playDeathBlip();
        }
    }
    void playGameOverToneSound() {
        if (SD.cardType() != CARD_NONE) {
            playWAV("/audio/gameend.wav");
            _gameOverFallbackPending = true;
            _gameOverFallbackCheckAt = millis() + 300;
        } else {
            playGameOverBlip();
        }
    }

    // -------------------------------------------------------------------------
    // SYNTH: square-wave tones and melodies, mixed with everything else.
    // Melody arrays must outlive the melody (the canned ones are static).
    // -------------------------------------------------------------------------
    void playTone(int freqHz, int durationMs) {
        if (!_ready) return;
        audiomix::MixCmd m;
        m.type = audiomix::MC_TONE;
        audiodiag::toneRequests.fetch_add(1);
        m.a = freqHz;
        m.b = durationMs;
        mixerCmd(m);
    }
    void playMelody(const int* freqs, const int* durs, int len) {
        if (!_ready) return;
        audiomix::MixCmd m;
        m.type = audiomix::MC_MELODY;
        m.freqs = freqs;
        m.durs = durs;
        m.a = len;
        if (mixerCmd(m)) ++_melodiesRequested;
    }
    // Playing, or asked for and not yet started.
    bool isMelodyPlaying() const {
        return _audioMixer.melodyBusy.load() || _audioMixer.melodiesStarted.load() != _melodiesRequested;
    }
    bool isTonePlaying() const { return _audioMixer.toneBusy.load(); }

    // --- Canned in-game tones ---
    void playLaunchMelody() {
        static const int n[] = {523,659,784,1047};
        static const int d[] = { 80, 80, 80, 150};
        playMelody(n, d, 4);
    }
    void playLandingSuccess() {
        static const int n[] = {392,523,659,784,1047};
        static const int d[] = {100,100,100,100, 300};
        playMelody(n, d, 5);
    }
    void playTankStartMelody() {
        static const int n[] = {110,147,185,220};
        static const int d[] = { 90, 90, 90,180};
        playMelody(n, d, 4);
    }
    void playCountdownBeep()    { playTone(800,  100); }
    void playPowerUpShield()    { playTone(1000, 250); }
    void playPowerUpExtraLife() { playTone(1500, 150); }
    void playPowerUpSlow()      { playTone(600,  400); }
    void playAsteroidPass()     { playTone(800,   30); }
    void playCometPass()        { playTone(1200, 100); }
    void playThrustTick()       { playTone(180,   20); }
    void playSound(int f, int d){ playTone(f, d); }  // compat alias

    void playJumpBlip() {
        static const int n[] = {700, 1050};
        static const int d[] = { 35,   45};
        playMelody(n, d, 2);
    }
    void playDeathBlip() {
        static const int n[] = {500, 350, 220};
        static const int d[] = {100, 100, 200};
        playMelody(n, d, 3);
    }
    void playGameOverBlip() {
        static const int n[] = {392, 330, 262, 196};
        static const int d[] = {150, 150, 150, 350};
        playMelody(n, d, 4);
    }

    // -------------------------------------------------------------------------
    // UPDATE: call every frame. The mixing happens on its own task; this
    // only runs the WAV-missing fallbacks.
    // -------------------------------------------------------------------------
    void update() {
        const unsigned long now = millis();
        const bool opened = _audioLoader.lastDurationMs.load() != 0;
        if (_jumpFallbackPending) {
            if (opened) _jumpFallbackPending = false;
            else if (now >= _jumpFallbackCheckAt) { _jumpFallbackPending = false; playJumpBlip(); }
        }
        if (_deathFallbackPending) {
            if (opened) _deathFallbackPending = false;
            else if (now >= _deathFallbackCheckAt) { _deathFallbackPending = false; playDeathBlip(); }
        }
        if (_gameOverFallbackPending) {
            if (opened) _gameOverFallbackPending = false;
            else if (now >= _gameOverFallbackCheckAt) { _gameOverFallbackPending = false; playGameOverBlip(); }
        }
    }

    // Everything stops: music, effects, synth, and anything asked for but not
    // yet started (the epoch makes sure it never does).
    void mute() {
        if (!_ready) return;
        ++_epoch;
        audiomix::MixCmd m;
        m.type = audiomix::MC_STOP_ALL;
        mixerCmd(m);
        loaderCmd(audiomix::LC_STOP_ALL);
        _jumpFallbackPending = _deathFallbackPending = _gameOverFallbackPending = false;
    }
    void stopAll() { mute(); }
};

#endif  // AUDIO_LEGACY
#endif  // AUDIO_ENGINE_H
