#ifndef RESONANCE_CONFIG_H
#define RESONANCE_CONFIG_H

#include <stdint.h>

// =============================================================================
// RESONANCE FLUX: every tuning number in one place, tuned over several
// plays on the board (docs/design/ResonanceFlux.md has how they got here).
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

// The phase gap to the signal on your stop, in the scope's corner, for
// tuning on the board: this is its default; the options switch it.
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
// The screen's too small for speed to make later waves harder (a signal
// starts 40-70px out), so the ramp is gentle and capped: later waves are
// harder through more ratios, more signals and more at once instead.
constexpr int   ON_SCOPE_FIRST = 2;      // signals on the scope at once: 2, one more every
constexpr int   ON_SCOPE_EVERY = 4;      //   fourth wave, up to ON_SCOPE_MAX
constexpr int   ON_SCOPE_MAX = 4;
constexpr float DRIFT_START = 4.0f;      // px/s on wave 1
constexpr float DRIFT_GROWTH = 1.03f;    // per wave
constexpr float DRIFT_MAX   = 7.0f;      // reached about wave 20
constexpr unsigned long SPAWN_FIRST_MS = 4000, SPAWN_MIN_MS = 2500, SPAWN_STEP_MS = 150;
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
// The options' score multipliers: each help switched off, and the pace.
constexpr float MULT_NO_HINT = 1.25f, MULT_NO_NOTES = 1.25f, MULT_FAST = 1.5f, MULT_CALM = 0.75f;
// The pace option: drift speed (and how often signals come) at CALM and FAST.
constexpr float PACE_CALM_SPEED = 0.75f, PACE_FAST_SPEED = 1.25f;

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

// ---- The Chord (the boss, every CHORD_EVERY waves with BOSSES on) ----
constexpr int   CHORD_EVERY = 5;
constexpr int   CHORD_LAYERS = 3;
constexpr float CHORD_R = 16.0f;          // its figure's size
constexpr unsigned long CHORD_SEND_MS = 5000;   // an escort (an ordinary signal) this often
constexpr int   CHORD_ESCORTS_MAX = 2;   //   while fewer than this are on the scope
constexpr int   CHORD_DRIFT_WAVE = 10;   // from this wave its layers drift in phase,
constexpr float CHORD_DRIFT_RATE = 0.12f;   //   up to this (radians/s; the tolerance is 0.12)
constexpr int   CHORD_MORPH_WAVE = 15;   // from this wave its middle layer swaps ratio
constexpr unsigned long CHORD_MORPH_MS = 3000;   //   this often
constexpr long  PTS_LAYER = 300;         // each layer stripped
constexpr long  PTS_CHORD = 5000;        // brought down, plus PTS_CHORD_SEC for each
constexpr long  PTS_CHORD_SEC = 1000;    //   second under CHORD_PAR_S
constexpr float CHORD_PAR_S = 45.0f;

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
