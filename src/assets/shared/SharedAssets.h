#ifndef SHARED_ASSETS_H
#define SHARED_ASSETS_H

// =============================================================================
// SHARED ASSETS — used by more than one game
//
// PRIMARY: WAV files on SD card at these paths:
//   /audio/gamestart.wav   — title screen / attract entry sound
//   /audio/gameend.wav     — game over sound
//   /audio/explosion.wav   — ship/crash explosion
//
// FALLBACK: PROGMEM arrays below are used automatically when SD unavailable.
// They are 8kHz mono 8-bit unsigned PCM — lower quality than the SD WAVs.
//
// AudioEngine convenience wrappers (playStartupSound, playGameOverSound,
// playExplosionSound) try SD first and fall back to PROGMEM automatically.
// Games never need to know which path was used.
//
// WAV file format for SD card:
//   - 44.1kHz (or 22.05kHz), mono or stereo, 16-bit signed PCM
//   - Standard 44-byte RIFF/WAVE header
//   - Export from Audacity: File > Export > WAV, PCM 16-bit
// =============================================================================

#include "../../games/AsteroidFlux/assets/explosion.h"
#include "../../games/AsteroidFlux/assets/gamestart.h"
#include "../../games/AsteroidFlux/assets/gameend.h"

#endif // SHARED_ASSETS_H