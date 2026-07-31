#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "ArcadeConfig.h"

// =============================================================================
// AUDIO ENGINE — MAX98357A via I2S
//
// WAV STREAMING: Runs on a dedicated FreeRTOS task (Core 0) so audio playback
// is completely independent of the render loop speed. No more slowing or
// stuttering regardless of how long a frame takes on Core 1.
//
// WAV HEADER: Scans for the 'data' chunk rather than assuming fixed 44-byte
// offset — handles non-standard headers (LIST, INFO chunks etc).
//
// TONE/MELODY: Still driven by update() on Core 1 (render loop).
//             Tones don't play while WAV is active.
//
// SD CARD PATHS:
//   /audio/gamestart.wav
//   /audio/gameend.wav
//   /audio/explosion.wav
//   /audio/jump.wav        (Platform Flux — falls back to a tone blip)
//   /audio/death.wav       (Platform Flux — falls back to a tone blip)
//
// FALLBACK: PROGMEM 8kHz 8-bit arrays used when SD unavailable.
//           Also streamed from the audio task.
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

static const i2s_port_t I2S_PORT          = I2S_NUM_0;
static const int        I2S_DMA_BUF_LEN   = 1024;   // Larger buffer = more headroom
static const int        I2S_DMA_BUF_COUNT = 8;
static const int        TONE_SAMPLES_PER_UPDATE = 700;

// Audio task config
static const int        AUDIO_TASK_STACK  = 8192;  // 8KB — file I/O needs headroom
static const int        AUDIO_TASK_PRIO   = 5;       // Higher than loop() (1)
static const int        AUDIO_TASK_CORE   = 0;       // Core 0, loop() on Core 1
static const size_t     WAV_READ_CHUNK    = 2048;    // bytes per task iteration

// =============================================================================
// Shared state between render loop and audio task — protected by mutex
// =============================================================================
struct AudioTaskState {
    // Command flags (written by Core 1, read by Core 0)
    volatile bool  startWAV       = false;
    volatile bool  startPROGMEM   = false;
    volatile bool  stopRequested  = false;
    volatile bool  loopEnabled    = false;   // replay when file ends

    // WAV file path (written before startWAV = true)
    char           wavPath[64]    = {0};

    // Path to resume after a one-shot WAV finishes (empty = don't resume)
    char           resumePath[64] = {0};

    // PROGMEM sample (written before startPROGMEM = true)
    const uint8_t* pgmData        = nullptr;
    size_t         pgmLen         = 0;

    // Status (written by Core 0, read by Core 1)
    volatile bool     playing          = false;
    volatile uint32_t wavDurationMs    = 0;    // computed from header
    volatile uint32_t dataOffset       = 0;    // byte offset of data chunk in file

    // Volume control — written by Core 1, read by Core 0
    // Range 0.0 (silent) to 1.0 (full). Applied to all audio output.
    volatile float volume = 0.8f;

    // WAV metadata (written by Core 0 after header parse)
    volatile uint32_t sampleRate    = 44100;
    volatile uint16_t bitsPerSample = 16;
    volatile uint16_t channels      = 1;
};

static AudioTaskState    _audioState;
static SemaphoreHandle_t _audioMutex = nullptr;

// Read buffer lives in internal RAM for fast SD access
static uint8_t _wavBuf[WAV_READ_CHUNK];

// =============================================================================
// AUDIO TASK — runs on Core 0
// =============================================================================
static void audioTask(void* param) {
    File wavFile;
    bool pgmMode      = false;
    size_t pgmPos     = 0;
    const uint8_t* pgmData = nullptr;
    size_t pgmLen     = 0;

    static int16_t outBuf[WAV_READ_CHUNK * 2]; // worst case: 8-bit mono → 16-bit stereo

    while (true) {
        // --- Check for new command ---
        if (_audioState.stopRequested) {
            _audioState.stopRequested = false;
            _audioState.playing       = false;
            if (wavFile) wavFile.close();
            pgmMode = false;
            i2s_zero_dma_buffer(I2S_PORT);
            taskYIELD();
            continue;
        }

        if (_audioState.startWAV) {
            _audioState.startWAV      = false;
            _audioState.wavDurationMs = 0;  // Clear stale value before new file opens
            if (wavFile) wavFile.close();
            pgmMode = false;

            wavFile = SD.open(_audioState.wavPath);
            if (!wavFile) {
                Serial.printf("[AUDIO] Cannot open: %s\n", _audioState.wavPath);
                _audioState.playing = false;
                taskYIELD();
                continue;
            }

            // --- Parse WAV header: scan for 'fmt ' and 'data' chunks ---
            uint8_t hdr[12];
            wavFile.read(hdr, 12);
            if (hdr[0]!='R'||hdr[1]!='I'||hdr[2]!='F'||hdr[3]!='F'||
                hdr[8]!='W'||hdr[9]!='A'||hdr[10]!='V'||hdr[11]!='E') {
                Serial.println("[AUDIO] Not a WAV file");
                wavFile.close();
                _audioState.playing = false;
                taskYIELD();
                continue;
            }

            uint32_t sampleRate = 44100;
            uint16_t bits = 16, ch = 1;
            bool foundData = false;

            // Scan chunks until we find 'data'
            while (wavFile.available()) {
                uint8_t chunkHdr[8];
                if (wavFile.read(chunkHdr, 8) != 8) break;
                uint32_t chunkSize = chunkHdr[4] | (chunkHdr[5]<<8) |
                                     (chunkHdr[6]<<16) | (chunkHdr[7]<<24);

                if (chunkHdr[0]=='f'&&chunkHdr[1]=='m'&&chunkHdr[2]=='t'&&chunkHdr[3]==' ') {
                    uint8_t fmt[16];
                    uint32_t toRead = min((uint32_t)16, chunkSize);
                    wavFile.read(fmt, toRead);
                    if (chunkSize > 16) wavFile.seek(wavFile.position() + chunkSize - 16);
                    ch          = fmt[2]  | (fmt[3]  << 8);
                    sampleRate  = fmt[4]  | (fmt[5]  << 8) | (fmt[6]  << 16) | (fmt[7]  << 24);
                    bits        = fmt[14] | (fmt[15] << 8);
                } else if (chunkHdr[0]=='d'&&chunkHdr[1]=='a'&&chunkHdr[2]=='t'&&chunkHdr[3]=='a') {
                    _audioState.sampleRate    = sampleRate;
                    _audioState.bitsPerSample = bits;
                    _audioState.channels      = ch;
                    _audioState.dataOffset    = wavFile.position();  // for loop seek

                    // Compute duration in ms from header metadata
                    uint32_t bytesPerSec = sampleRate * ch * (bits / 8);
                    _audioState.wavDurationMs = bytesPerSec > 0
                                               ? (chunkSize * 1000UL / bytesPerSec) : 0;

                    foundData = true;
                    Serial.printf("[AUDIO] %s: %uHz %u-bit %uch %ums\n",
                        _audioState.wavPath, sampleRate, bits, ch,
                        _audioState.wavDurationMs);

                    // All files are 44.1kHz 16-bit — no clock reconfiguration needed
                    _audioState.playing = true;

                    // --- Stream audio data, loop or resume when done ---
                    bool keepGoing = true;
                    while (keepGoing) {
                        uint32_t remaining = chunkSize;
                        while (remaining > 0 && !_audioState.stopRequested &&
                               !_audioState.startWAV && !_audioState.startPROGMEM) {
                            size_t toRead = min((size_t)WAV_READ_CHUNK, (size_t)remaining);
                            size_t nRead  = wavFile.read(_wavBuf, toRead);
                            if (nRead == 0) break;
                            remaining -= nRead;

                            int outIdx = 0;
                            float vol = _audioState.volume;
                            if (bits == 16) {
                                int16_t* src = (int16_t*)_wavBuf;
                                int nSamples = nRead / 2;
                                for (int i = 0; i < nSamples; i += ch) {
                                    int16_t L = (int16_t)(src[i] * vol);
                                    int16_t R = (ch > 1) ? (int16_t)(src[i+1] * vol) : L;
                                    outBuf[outIdx++] = L;
                                    outBuf[outIdx++] = R;
                                }
                            } else {
                                for (size_t i = 0; i < nRead; i += ch) {
                                    int16_t L = (int16_t)(((int16_t)_wavBuf[i] - 128) * 256 * vol);
                                    int16_t R = (ch > 1)
                                        ? (int16_t)(((int16_t)_wavBuf[i+1] - 128) * 256 * vol) : L;
                                    outBuf[outIdx++] = L;
                                    outBuf[outIdx++] = R;
                                }
                            }
                            size_t bw = 0;
                            i2s_write(I2S_PORT, outBuf,
                                      outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
                        }

                        // Check what to do when data is exhausted
                        if (_audioState.stopRequested || _audioState.startWAV ||
                            _audioState.startPROGMEM) {
                            keepGoing = false;  // interrupted — exit loop
                        } else if (_audioState.loopEnabled) {
                            // Seek back to data start and replay
                            wavFile.seek(_audioState.dataOffset);
                        } else if (_audioState.resumePath[0] != '\0') {
                            // One-shot finished — trigger resume track
                            // Copy resume path to wavPath and restart
                            strncpy(_audioState.wavPath, _audioState.resumePath,
                                    sizeof(_audioState.wavPath) - 1);
                            _audioState.resumePath[0] = '\0';
                            _audioState.loopEnabled   = true;
                            _audioState.startWAV      = true;
                            keepGoing = false;
                        } else {
                            keepGoing = false;
                        }
                    }

                    _audioState.playing = false;
                    wavFile.close();
                    break;

                } else {
                    // Unknown chunk — skip it
                    wavFile.seek(wavFile.position() + chunkSize);
                }
            }

            if (!foundData) {
                Serial.println("[AUDIO] No data chunk found");
                wavFile.close();
                _audioState.playing = false;
            }
            taskYIELD();
            continue;
        }

        if (_audioState.startPROGMEM) {
            _audioState.startPROGMEM = false;
            if (wavFile) wavFile.close();
            pgmData  = _audioState.pgmData;
            pgmLen   = _audioState.pgmLen;
            pgmPos   = 44; // skip 8kHz WAV header
            pgmMode  = true;
            _audioState.playing = true;
        }

        // --- PROGMEM streaming ---
        if (pgmMode && pgmData != nullptr) {
            int outIdx = 0;
            int srcCount = 0;
            const int CHUNK = 140;
            const int UP    = 5;
            float vol = _audioState.volume;
            while (pgmPos < pgmLen && srcCount < CHUNK) {
                int16_t s = (int16_t)(((int16_t)pgm_read_byte(&pgmData[pgmPos++]) - 128) * 200 * vol);
                for (int r = 0; r < UP; r++) {
                    outBuf[outIdx++] = s;
                    outBuf[outIdx++] = s;
                }
                srcCount++;
            }
            if (outIdx > 0) {
                size_t bw = 0;
                i2s_write(I2S_PORT, outBuf,
                          outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
            }
            if (pgmPos >= pgmLen) {
                pgmMode = false;
                _audioState.playing = false;
            }
            // No taskYIELD here — let i2s_write portMAX_DELAY pace us
            continue;
        }

        // Nothing to do — yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =============================================================================
// AUDIO ENGINE CLASS
// =============================================================================
class AudioEngine {
private:
    bool _i2sReady   = false;
    TaskHandle_t _taskHandle = nullptr;

    // ---- Deferred WAV-open-failed fallback (see playJumpSound) ----
    bool          _jumpFallbackPending = false;
    unsigned long _jumpFallbackCheckAt = 0;
    // ---- Deferred WAV-open-failed fallback (see playDeathSound) ----
    bool          _deathFallbackPending = false;
    unsigned long _deathFallbackCheckAt = 0;
    // ---- Deferred WAV-open-failed fallback (see playGameOverToneSound) ----
    bool          _gameOverFallbackPending = false;
    unsigned long _gameOverFallbackCheckAt = 0;

    // ---- Tone / melody state (Core 1 only) ----
    bool          _toneActive     = false;
    int           _toneFreq       = 0;
    unsigned long _toneEndMs      = 0;
    uint32_t      _sampleCounter  = 0;
    uint32_t      _halfPeriod     = 0;

    const int*    _melodyFreqs    = nullptr;
    const int*    _melodyDurations= nullptr;
    int           _melodyLength   = 0;
    int           _melodyIndex    = 0;
    bool          _melodyPlaying  = false;
    unsigned long _nextNoteMs     = 0;

    uint32_t freqToHalfPeriod(int freq) {
        if (freq <= 0) return 0;
        return (uint32_t)(ArcadeConfig::I2S_SAMPLE_RATE / (2 * freq));
    }

    void writeToneSamples(int count, int16_t amplitude) {
        static int16_t buf[TONE_SAMPLES_PER_UPDATE * 2];
        int filled = 0;
        int16_t scaledAmp = (int16_t)(amplitude * _audioState.volume);
        for (int i = 0; i < count; i++) {
            int16_t s = 0;
            if (_halfPeriod > 0) {
                s = (_sampleCounter < _halfPeriod) ? scaledAmp : -scaledAmp;
                if (++_sampleCounter >= _halfPeriod * 2) _sampleCounter = 0;
            }
            buf[filled++] = s;
            buf[filled++] = s;
        }
        size_t bw = 0;
        i2s_write(I2S_PORT, buf, filled * sizeof(int16_t), &bw, 0);
    }

    void writeSilence(int count = TONE_SAMPLES_PER_UPDATE) {
        static int16_t sil[TONE_SAMPLES_PER_UPDATE * 2] = {0};
        size_t bw = 0;
        i2s_write(I2S_PORT, sil, count * 2 * sizeof(int16_t), &bw, 0);
    }

    void stopAudioTask() {
        _audioState.stopRequested = true;
        _audioState.playing       = false;
        // Give the audio task time to finish its current i2s_write and
        // restore the I2S clock before we start a new file
        vTaskDelay(pdMS_TO_TICKS(50));
    }

public:
    AudioEngine() {}

    // -------------------------------------------------------------------------
    // INIT
    // -------------------------------------------------------------------------
    bool begin() {
        i2s_config_t cfg = {
            .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate          = ArcadeConfig::I2S_SAMPLE_RATE,
            .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count        = I2S_DMA_BUF_COUNT,
            .dma_buf_len          = I2S_DMA_BUF_LEN,
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
        _i2sReady = true;

        // Start audio streaming task on Core 0
        xTaskCreatePinnedToCore(audioTask, "audioTask",
                                AUDIO_TASK_STACK, nullptr,
                                AUDIO_TASK_PRIO, &_taskHandle,
                                AUDIO_TASK_CORE);

        Serial.println("[AUDIO] I2S + audio task ready.");
        return true;
    }

    // -------------------------------------------------------------------------
    // SD WAV PLAYBACK — primary path
    // -------------------------------------------------------------------------
    void playWAV(const char* path) {
        if (!_i2sReady) return;
        stopAudioTask();
        _toneActive    = false;
        _melodyPlaying = false;
        _audioState.loopEnabled   = false;
        _audioState.resumePath[0] = '\0';
        strncpy(_audioState.wavPath, path, sizeof(_audioState.wavPath) - 1);
        _audioState.startWAV = true;
    }

    // Play WAV and loop it indefinitely until stopped
    void loopWAV(const char* path) {
        if (!_i2sReady) return;
        stopAudioTask();
        _toneActive    = false;
        _melodyPlaying = false;
        _audioState.loopEnabled   = true;
        _audioState.resumePath[0] = '\0';
        strncpy(_audioState.wavPath, path, sizeof(_audioState.wavPath) - 1);
        _audioState.startWAV = true;
    }

    // Play a one-shot WAV then seamlessly resume looping another
    void playWAVThenLoop(const char* oneShotPath, const char* loopPath) {
        if (!_i2sReady) return;
        stopAudioTask();
        _toneActive    = false;
        _melodyPlaying = false;
        _audioState.loopEnabled   = false;
        strncpy(_audioState.wavPath,    oneShotPath, sizeof(_audioState.wavPath)    - 1);
        strncpy(_audioState.resumePath, loopPath,    sizeof(_audioState.resumePath) - 1);
        _audioState.startWAV = true;
    }

    void stopLoop() { stopAudioTask(); }

    // Duration of the most recently opened WAV file in ms.
    // Valid as soon as playing == true. Use to time gameplay phases.
    uint32_t getLastWAVDurationMs() const { return _audioState.wavDurationMs; }

    bool isWAVPlaying() const { return _audioState.playing; }

    // -------------------------------------------------------------------------
    // PROGMEM FALLBACK
    // -------------------------------------------------------------------------
    void startSamplePROGMEM(const uint8_t* data, size_t len) {
        if (!_i2sReady || !data || len <= 44) return;
        stopAudioTask();
        _toneActive    = false;
        _melodyPlaying = false;
        _audioState.pgmData        = data;
        _audioState.pgmLen         = len;
        _audioState.startPROGMEM   = true;
    }

    bool isSamplePlaying() const { return _audioState.playing; }

    // -------------------------------------------------------------------------
    // CONVENIENCE WRAPPERS — attempt SD WAV, task handles file-not-found
    // SD.exists() removed — avoids SPI bus collision with audio task on Core 0
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

    void setVolume(float v) { _audioState.volume = constrain(v, 0.0f, 1.0f); }
    float getVolume() const { return _audioState.volume; }

    void playLanderStartSound() {
        if (SD.cardType() != CARD_NONE) playWAV("/audio/lander_start.wav");
        else playLaunchMelody();
    }

    void playLandingSuccessSound() {
        if (SD.cardType() != CARD_NONE) playWAV("/audio/land_success.wav");
        else playLandingSuccess();
    }

    // Drop a WAV at /audio/jump.wav to override — falls back to a short
    // rising two-note blip if the SD card isn't present, or if it is but
    // that specific file is missing/fails to open. The open result isn't
    // known synchronously (WAV streaming runs on its own task and opening
    // over SPI can take a while, especially if it's contending with
    // display traffic), so this is polled from update() with a generous
    // deadline rather than checked once at a fixed short delay — a single
    // too-early check was concluding "failed" while the file was still
    // legitimately opening, firing the fallback tone on top of the WAV
    // once it did start.
    void playJumpSound() {
        if (SD.cardType() != CARD_NONE) {
            playWAV("/audio/jump.wav");
            _jumpFallbackPending  = true;
            _jumpFallbackCheckAt  = millis() + 300;
        } else {
            playJumpBlip();
        }
    }

    // Drop a WAV at /audio/death.wav to override — falls back to a short
    // descending tone if the SD card isn't present, or if it is but that
    // specific file is missing/fails to open (same polled-deadline pattern
    // as playJumpSound).
    void playDeathSound() {
        if (SD.cardType() != CARD_NONE) {
            playWAV("/audio/death.wav");
            _deathFallbackPending = true;
            _deathFallbackCheckAt = millis() + 300;
        } else {
            playDeathBlip();
        }
    }

    // Reuses the shared /audio/gameend.wav path (same convention as
    // AsteroidFlux's playGameOverSound) but falls back to a tone melody
    // instead of requiring a PROGMEM sample — same polled-deadline pattern
    // as playJumpSound/playDeathSound.
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
    // TONE MODE — Core 1, update() driven
    // Does not play if WAV/sample is active
    // -------------------------------------------------------------------------
    void playTone(int freqHz, int durationMs) {
        if (!_i2sReady || _audioState.playing) return;
        _toneFreq      = freqHz;
        _halfPeriod    = freqToHalfPeriod(freqHz);
        _sampleCounter = 0;
        _toneActive    = true;
        _toneEndMs     = millis() + durationMs;
    }

    void playMelody(const int* freqs, const int* durs, int len) {
        if (!_i2sReady || _audioState.playing) return;
        _melodyFreqs     = freqs;
        _melodyDurations = durs;
        _melodyLength    = len;
        _melodyIndex     = 0;
        _melodyPlaying   = true;
        _nextNoteMs      = millis();
        _toneActive      = false;
    }

    bool isMelodyPlaying() const { return _melodyPlaying; }
    bool isTonePlaying()   const { return _toneActive; }

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
    // UPDATE — call every frame from render loop (Core 1)
    // Only drives tone/melody. WAV streaming is handled by audio task.
    // -------------------------------------------------------------------------
    void update() {
        unsigned long now = millis();

        // Polled check: did the jump/death WAV actually start? wavDurationMs
        // is only set once the header is successfully parsed, and stays set
        // (not cleared on natural playback end) until the next WAV request
        // resets it — so it reliably distinguishes "never opened" from
        // "played and already finished," unlike the transient playing flag.
        // Polled every frame rather than checked once at a fixed delay:
        // opening the file over SPI can legitimately take longer than a
        // short fixed wait, especially under display-traffic contention, so
        // a too-early single check was misreading "still opening" as
        // "failed" and firing the fallback tone alongside the real WAV.
        if (_jumpFallbackPending) {
            if (_audioState.wavDurationMs != 0) {
                _jumpFallbackPending = false; // WAV opened fine — no fallback needed
            } else if (now >= _jumpFallbackCheckAt) {
                _jumpFallbackPending = false;
                playJumpBlip();
            }
        }
        if (_deathFallbackPending) {
            if (_audioState.wavDurationMs != 0) {
                _deathFallbackPending = false;
            } else if (now >= _deathFallbackCheckAt) {
                _deathFallbackPending = false;
                playDeathBlip();
            }
        }
        if (_gameOverFallbackPending) {
            if (_audioState.wavDurationMs != 0) {
                _gameOverFallbackPending = false;
            } else if (now >= _gameOverFallbackCheckAt) {
                _gameOverFallbackPending = false;
                playGameOverBlip();
            }
        }

        if (!_i2sReady || _audioState.playing) return;

        if (_melodyPlaying) {
            if (now >= _nextNoteMs) {
                if (_melodyIndex >= _melodyLength) {
                    _melodyPlaying = false;
                    _toneActive    = false;
                    writeSilence();
                    return;
                }
                int freq = _melodyFreqs[_melodyIndex];
                int dur  = _melodyDurations[_melodyIndex];
                _melodyIndex++;
                _toneFreq      = freq;
                _halfPeriod    = freqToHalfPeriod(freq);
                _sampleCounter = 0;
                _toneActive    = (freq != NOTE_REST);
                _toneEndMs     = now + (unsigned long)(dur * 0.85f);
                _nextNoteMs    = now + dur;
            }
        }

        if (_toneActive) {
            if (millis() >= _toneEndMs) {
                _toneActive = false;
                writeSilence();
            } else {
                writeToneSamples(TONE_SAMPLES_PER_UPDATE, 8000);
            }
        }
    }

    void mute() {
        stopAudioTask();
        _toneActive    = false;
        _melodyPlaying = false;
        i2s_zero_dma_buffer(I2S_PORT);
    }

    void stopAll() { mute(); }
};

#endif // AUDIO_ENGINE_H