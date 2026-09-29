#pragma once
// Host stub: counts calls instead of making sound. Shadows the real
// src/cabinet/AudioEngine.h, which needs I2S, FreeRTOS and an SD card.
//
// Anything specific to the real engine is therefore invisible here — it was
// blind to the bug where its shared task state was declared `static` in a
// header, which silenced the whole cabinet once more than one .cpp included
// it. Audio behaviour still has to be checked on hardware; the real mixer's
// logic (audio/AudioMixer.h, audio/AudioLoader.h) has its own host test,
// test/audio_test.cpp.
#include <Arduino.h>

struct AudioEngine {
    int tones = 0, melodies = 0, wavs = 0;
    int silencedCalls = 0;   // sounds asked for while silenced (a demo)
    bool silenced = false;

    void setSilenced(bool on) { silenced = on; }
    void playTone(int, int) { if (silenced) ++silencedCalls; else ++tones; }
    void playMelody(const int*, const int*, int) { if (silenced) ++silencedCalls; else ++melodies; }
    void playWAV(const char*) { if (silenced) ++silencedCalls; else ++wavs; }
    void loopWAV(const char*) {}
    void stopLoop() {}
    void mute() {}
    bool isSamplePlaying() const { return false; }
    bool isWAVPlaying() const { return false; }
    bool isMelodyPlaying() const { return false; }
    // The real wrappers go through playWAV()/playMelody(), so they're
    // silenced the same way.
    void playExplosionSound(const uint8_t*, size_t) { playWAV("/audio/explosion.wav"); }
    void playTankStartSound() {}
    void playLaunchMelody() { playMelody(nullptr, nullptr, 0); }
    void preload(const char*) {}
    void setVolume(float) {}
    void setMusicEnabled(bool) {}
    void setFxEnabled(bool) {}
};
