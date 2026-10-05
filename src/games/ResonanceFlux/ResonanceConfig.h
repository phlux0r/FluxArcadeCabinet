#ifndef RESONANCE_CONFIG_H
#define RESONANCE_CONFIG_H

#include <stdint.h>

// =============================================================================
// RESONANCE FLUX: every tuning number in one place. The prototype's: the
// match tolerances, speeds and static amounts are first guesses, to be
// judged on the board (docs/design/ResonanceFlux.md, "Needs the board").
// =============================================================================

namespace resonance {

// ---- Screen (landscape, 160x128) ----
constexpr int W = 160, H = 128;
constexpr int HUD_H     = 10;            // score, wave, dampen charges
constexpr int METER_Y   = 10;            // the static meter, 2px
constexpr int SCOPE_Y   = 12;            // the scope: y 12-117
constexpr int SCOPE_H   = 106;
constexpr int STRIP_Y   = 118;           // the tuning strip: y 118-127
constexpr float CORE_X  = W / 2.0f;
constexpr float CORE_Y  = SCOPE_Y + SCOPE_H / 2.0f;
constexpr float CORE_R  = 26.0f;         // your figure's size, and the ring signals burst on
constexpr float SIG_R   = 10.0f;         // a signal's figure (complex ratios blur smaller)

// Show the match gaps and the beat on the tuning strip, for tuning the
// tolerances on the board.
constexpr bool DEBUG_LINE = true;

// ---- The dial: a = b * d for ratio a:b ----
constexpr float DIAL_MIN = 0.30f, DIAL_MAX = 1.0f;
constexpr float DIAL_SPEED  = 0.47f;     // dial units/s at full push: across it in ~1.5s
constexpr float PHASE_SPEED = 3.1416f;   // radians/s at full push: a full turn in 2s
constexpr float STICK_CURVE = 2.0f;      // push^curve: small pushes creep
constexpr float SNAP_ZONE   = 0.012f;    // within this of a clean ratio, with the stick
constexpr float SNAP_STICK  = 0.15f;     //   nearly still, the needle eases onto it
constexpr float SNAP_RATE   = 8.0f;      //   (per second)
// A figure a little off a clean ratio rolls (its phase drifts) at
// 2*pi*error*ROLL_HZ radians/s: the visual twin of the beat.
constexpr float ROLL_HZ     = 20.0f;

// ---- Matching ----
constexpr float RATIO_TOL   = 0.006f;    // dial gap for resonance
constexpr float PHASE_TOL   = 0.12f;     // phase gap (radians) for resonance: also the
                                         // most the figures differ, unit size (ResonanceFigure.h),
                                         // so ~3px at the core
constexpr unsigned long FIRE_COOLDOWN_MS = 400;   // after a misfire

// ---- Signals and waves ----
constexpr int   MAX_SIGNALS = 6;
constexpr float DRIFT_START = 7.0f;      // px/s on wave 1
constexpr float DRIFT_GROWTH = 1.06f;    // per wave
constexpr float DRIFT_MAX   = 20.0f;
constexpr unsigned long SPAWN_FIRST_MS = 2500, SPAWN_MIN_MS = 1000, SPAWN_STEP_MS = 100;
constexpr unsigned long WAVE_INTRO_MS = 1800, WAVE_CLEAR_MS = 2200;

// ---- Static (0-100 is the game) ----
constexpr float STATIC_HIT     = 18.0f;  // a signal reaching the core
constexpr float STATIC_MISFIRE = 3.0f;
constexpr float STATIC_SHATTER = -1.0f;
constexpr float STATIC_CLEAR   = -25.0f; // a wave cleared
constexpr float STATIC_DECAY   = 0.5f;   // per second
constexpr float JITTER_DIAL    = 0.010f; // your figure's wander at full static
constexpr float JITTER_PHASE   = 0.35f;  //   (radians)

// ---- Dampen (B) ----
constexpr int   DAMPEN_PER_WAVE = 2;
constexpr unsigned long DAMPEN_MS = 4000;
constexpr float DAMPEN_SLOW = 0.3f;

// ---- Scoring ----
constexpr long  PTS_TONE = 100;          // up to double far out at the scope's edge
constexpr long  PTS_STATIC_LEFT = 50;    // wave clear: per point of static below 100
constexpr long  PTS_DAMPEN_LEFT = 500;

// ---- Look ----
constexpr float GLOW_HALF_MS = 90.0f;    // the afterglow's half-life
constexpr int   MAX_SHARDS = 144;
constexpr unsigned long SHARD_MS = 650;

// ---- Hum (the two tones) ----
constexpr float HUM_F_LO = 300.0f;       // Hz at DIAL_MIN
constexpr float HUM_F_SPAN = 570.0f;     // Hz per dial unit: the match tolerance is a ~3Hz beat
constexpr float HUM_YOU_LEVEL = 0.35f;
constexpr float HUM_TARGET_LEVEL = 0.35f;
constexpr float HUM_FADE_ZONE = 0.08f;   // the target tone fades in within this dial gap
constexpr float HISS_LEVEL = 0.4f;       // at full static

constexpr unsigned long GAMEOVER_MIN_MS = 1500, GAMEOVER_TIMEOUT_MS = 15000;

}  // namespace resonance

#endif  // RESONANCE_CONFIG_H
