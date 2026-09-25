#pragma once
// Host stub: counts calls instead of making sound. Shadows the real
// src/cabinet/AudioEngine.h, which needs I2S, FreeRTOS and an SD card.
//
// Anything specific to the real engine is therefore invisible here — it was
// blind to the bug where its shared task state was declared `static` in a
// header, which silenced the whole cabinet once more than one .cpp included
// it. Audio behaviour still has to be checked on hardware.
#include <Arduino.h>

struct AudioEngine {
    int tones = 0, melodies = 0, wavs = 0;

    void playTone(int, int) { ++tones; }
    void playMelody(const int*, const int*, int) { ++melodies; }
    void playWAV(const char*) { ++wavs; }
    void loopWAV(const char*) {}
    void stopLoop() {}
    void mute() {}
    bool isSamplePlaying() const { return false; }
    bool isMelodyPlaying() const { return false; }
    void playExplosionSound(const uint8_t*, size_t) { ++wavs; }
    void playTankStartSound() {}
};
