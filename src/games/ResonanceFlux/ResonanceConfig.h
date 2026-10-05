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
constexpr float CORE_R  = 18.0f;         // your figure's size, and the ring signals burst on
constexpr float SIG_R   = 10.0f;         // a signal's figure (complex ratios blur smaller)

// Show the match gaps and the beat on the tuning strip, for tuning the
// tolerances on the board.
constexpr bool DEBUG_LINE = true;

// ---- The dial ----
// One stop per clean ratio the game has reached so far (1:2 and 1:1 on
// wave 1), in ratio order, round in a ring: right from the last is the
// first. You're always on a stop.
constexpr float STEP_PUSH = 0.5f;        // a push past this steps one stop
constexpr unsigned long STEP_REPEAT_DELAY_MS = 350, STEP_REPEAT_MS = 180;   // held
constexpr float PHASE_SPEED = 3.1416f;   // radians/s at full push: a full turn in 2s
constexpr float STICK_CURVE = 2.0f;      // push^curve: small pushes creep

// ---- Matching ----
constexpr float PHASE_TOL   = 0.12f;     // phase gap (radians) for resonance: also the
                                         // most the figures differ, unit size (ResonanceFigure.h),
                                         // so ~2px at the core
constexpr unsigned long FIRE_COOLDOWN_MS = 400;   // after a misfire

// ---- Signals and waves ----
constexpr int   MAX_SIGNALS = 6;
constexpr int   ON_SCOPE_FIRST = 2;      // signals on the scope at once: 2, one more every
constexpr int   ON_SCOPE_EVERY = 3;      //   third wave, up to MAX_SIGNALS
constexpr float DRIFT_START = 4.0f;      // px/s on wave 1
constexpr float DRIFT_GROWTH = 1.05f;    // per wave
constexpr float DRIFT_MAX   = 14.0f;
constexpr unsigned long SPAWN_FIRST_MS = 4000, SPAWN_MIN_MS = 1500, SPAWN_STEP_MS = 150;
// Signals start on the left and right edges, or the top and bottom within
// this of a corner: anywhere else on those is under 40px from your figure.
constexpr float SPAWN_CORNER = 22.0f;
// Signals meander: each weaves either side of the way to the core, its
// heading swaying by up to SWAY_MAX radians at its own rate.
constexpr float SWAY_MIN = 0.6f, SWAY_MAX = 1.0f;
constexpr float SWAY_RATE_MIN = 0.7f, SWAY_RATE_MAX = 1.5f;   // radians/s
constexpr unsigned long WAVE_INTRO_MS = 1800, WAVE_CLEAR_MS = 2200;

// ---- Static (0-100 is the game) ----
constexpr float STATIC_HIT     = 18.0f;  // a signal reaching the core
constexpr float STATIC_MISFIRE = 3.0f;
constexpr float STATIC_SHATTER = -1.0f;
constexpr float STATIC_CLEAR   = -25.0f; // a wave cleared
constexpr float STATIC_DECAY   = 0.5f;   // per second
constexpr float JITTER_PHASE   = 0.35f;  // your phase's wander at full static (radians)

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
// Each ratio has its own pitch, whatever stop it's on: HUM_F_LO for the
// first in RATIOS, HUM_F_STEP higher for each after.
constexpr float HUM_F_LO = 300.0f;
constexpr float HUM_F_STEP = 40.0f;
constexpr float HUM_YOU_LEVEL = 0.35f;
constexpr float HUM_TARGET_LEVEL = 0.35f;
constexpr float HISS_LEVEL = 0.4f;       // at full static

constexpr unsigned long GAMEOVER_MIN_MS = 1500, GAMEOVER_TIMEOUT_MS = 15000;

// ---- Attract cycle, demo and the wave select ----
constexpr unsigned long ATTRACT_SLIDE_MS = 6000;
constexpr unsigned long DEMO_MIN_MS = 30000, DEMO_MAX_MS = 40000;
constexpr int   DEMO_MAX_WAVE = 8;       // the demo plays a random wave 1-8
constexpr unsigned long AP_REACT_MIN_MS = 250, AP_REACT_MAX_MS = 600;   // the autopilot's pause on a new target
constexpr int   PICK_MAX_WAVE = 20;
constexpr unsigned long PICK_TIMEOUT_MS = 20000;

}  // namespace resonance

#endif  // RESONANCE_CONFIG_H
