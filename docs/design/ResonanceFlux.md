# Resonance Flux: design

A prototype is built (see "Prototype" at the end); the rest is the
proposal. An original game rather than a remake: the
screen is a green phosphor oscilloscope, and you fight by matching shapes,
not by aiming at a position. Your weapon is a Lissajous figure (two sine
waves at right angles) that you shape with the stick. Hostile signals, each
its own figure, drift in towards the centre. Match one exactly and it
resonates; fire and it shatters.

What makes it different from the rest of the arcade:

1. **Aiming in shape space**: the stick tunes a frequency ratio and a
   phase, not a crosshair.
2. **Aiming by ear**: near a match you hear the two tones beat, slower the
   closer you are, pure when you're there. The amp becomes part of the play.
3. **Static** instead of lives: every signal that gets through adds noise
   to the screen, the sound and your own figure, so a game falls apart
   gradually, and you can claw it back.
4. **Resonance cascades**: one shot shatters every signal of the same
   shape at once.
5. **Chord bosses** every fifth wave, beaten by matching their layers in
   order.

Menu name **Resonance**, folder `src/games/ResonanceFlux/`, high-score key
`resonance` (`RESONANCE` in the table). Header-only like the other 2D games.

---

## 1. The figure

A Lissajous figure is the curve

    x = sin(p·t + φ),   y = sin(q·t)

for t across one full period. Two numbers shape it:

- **Ratio** r = p:q. Clean ratios give closed, still figures: 1:1 an
  ellipse (or line, or circle), 1:2 a figure-eight or parabola, 2:3 a
  pretzel, and so on. Between clean ratios the curve doesn't close and the
  figure precesses (rolls slowly). That's the visual twin of audible
  beating, and it makes "almost right" easy to see.
- **Phase** φ twists the figure: for 1:1, a diagonal line opens into an
  ellipse, a circle, then the other diagonal.

The ratios used, in dial order (q/p ascending):

| Ratio | q/p | First wave |
|---|---|---|
| 1:3 | 0.333 | 3 |
| 2:5 | 0.400 | 5 |
| 1:2 | 0.500 | 1 |
| 3:5 | 0.600 | 8 |
| 2:3 | 0.667 | 3 |
| 3:4 | 0.750 | 5 |
| 4:5 | 0.800 | 8 |
| 1:1 | 1.000 | 1 |

Neighbours late in the table (3:4 and 4:5, 3:5 and 2:3) are close on the
dial and alike to the eye, which is where twins (§6) come from.

**Phase symmetry.** Different phases can draw the same figure. For a:b
with phase p, two phases draw the same curve when they differ by a
multiple of 2π/b (shifting t by 2π/b leaves y alone and turns x's phase
by 2πa/b, which covers every multiple of 2π/b), or when they sum to
π − aπ/b (t → π/b − t leaves y alone and mirrors x's phase). Matching
compares phases with both taken out (`phaseGap()` in
`ResonanceFigure.h`). And since a change of phase g moves no point of the
figure more than g (unit size), the phase tolerance is also a bound on
how different two matched figures look: 0.12 rad is ~3px at the core.
The harness checks all of this against densely sampled curves.

## 2. Screen and layout

Landscape (rotation 1), 160x128.

| Area | y (px) | Notes |
|---|---|---|
| HUD | 0-9 | score left, wave centre, dampen charges right |
| Static meter | 10-11 | 2px bar, green to red as it fills |
| Scope | 12-117 | graticule (faint grid), signals drift in from the edges |
| Core | centred in the scope | your figure, radius 26px, inside a faint ring |
| Tuning strip | 118-127 | the ratio dial: a needle, tick marks at the clean ratios, the current ratio as text ("2:3") |

Signals are small figures (radius 7-9px) in a thin ring of their own, drawn
amber. Your figure is green. The signal you're nearest matching is
outlined; when you're in resonance both it and your figure glow white.

## 3. Controls

Landscape directions (`hiscore::screenDirs`): screen left/right is joyY,
screen up is `joyLeft`, screen down is `joyRight`.

| Input | Action |
|---|---|
| Stick left/right | Tune the ratio down/up the dial. Rate control: speed follows deflection, small pushes creep, full push crosses the dial in ~1.5s. Let go and it stays. A gentle snap pulls the needle onto a clean ratio when it's within a hair and the stick is near the middle |
| Stick up/down | Turn the phase, likewise rate-controlled; a full turn in ~2s at full push. Wraps around |
| A | Fire. In resonance: shatters the matched signal (and any of the same shape, §5). Out of tune: a misfire, a little static and a 0.4s cooldown |
| B | Dampen: everything slows to 30% for 4s and drifters stop rotating. Two per wave, unused ones scored at the wave's end |
| Back hold 1s | Quit to the launcher (main.cpp) |

Rate control rather than absolute position, so the stick returning to the
middle doesn't throw away a setting you've just found. Diagonal pushes tune
both at once, which is how good players will close in.

Static adds jitter to your ratio and phase (§4), so at high static the
figure won't sit still even with the stick let go.

## 4. Matching, resonance and static

**Resonance** for each signal is a closeness from 0 to 1, from the ratio
gap and the (symmetry-reduced) phase gap. A match is a ratio gap under
0.006 on the dial and a phase gap under 12°, both widened a little on the
first two waves. Both numbers are for tuning on the board.

**Static** runs from 0 to 100:

| Event | Static |
|---|---|
| A signal reaches the core | +18 (heavies +30) |
| Misfire | +3 |
| Shatter | −1 |
| Wave cleared | −25 |
| Time | −0.5 per second while nothing is touching the core |

At 100 the scope blows out: game over. What static does as it climbs:

- **Screen:** random bright pixels across the scope, more as it rises, and
  above 60 the graticule wobbles;
- **Sound:** hiss under everything, louder as it rises;
- **Your figure:** a random wander on ratio and phase whose size follows
  the static, so matching gets harder the worse things get.

## 5. Firing, cascades and chains

- A shot at a matched signal draws a bright pulse from the core out to it;
  it shatters into particles (its figure's sample points flung outward).
- **Cascade:** every other signal with the same ratio and an equivalent
  phase shatters with it, each one scored at a rising multiplier (×2, ×3,
  ...). Wave patterns deliberately send same-shape groups now and then to
  reward waiting for the cascade over picking them off one by one.
- **Chain:** shatters less than 2s apart build a chain multiplier, up to
  ×8; a misfire or a signal reaching the core breaks it.
- **Distance bonus:** a signal shattered near the scope's edge is worth up
  to double one shattered at the core's ring.

## 6. Signals

| Signal | From wave | Behaviour | Points |
|---|---|---|---|
| Tone | 1 | Fixed ratio and phase, drifts straight in | 100 |
| Drifter | 3 | Its phase turns slowly, so you have to track it | 150 |
| Twin | 5 | Two signals on neighbouring ratios arrive together; the beat (or a hard look) tells them apart | 200 each |
| Morpher | 6 | Slides between two ratios, pausing at each for ~1s; catch it in a pause | 250 |
| Heavy | 7 | Bigger and slower; the first hit changes its ratio, the second shatters it; +30 static if it gets in | 400 |
| Ghost | 10 | Flickers and plays no tone: eyes only | 300 |

Signals that reach the core ring burst against it (a flash and a crackle)
and add their static.

## 7. Chord bosses

Every fifth wave, a **Chord**: one big figure at the top of the scope made
of three ratios layered on one another, each drawn in a different
brightness. It hangs there, emitting tones towards the core every few
seconds.

- Match its brightest layer and fire: that layer strips away with a burst.
  Then the next brightest, then the last.
- Fire at a layer out of order (in resonance with it, but not the
  brightest) and the stripped layers come back.
- Later Chords: layers that drift in phase (wave 10), a morphing layer
  (wave 15), a ghost layer you can't hear (wave 20), then cycling.
- A Chord is worth 5,000, plus 1,000 per second under 45s.

## 8. Waves and progression

- A wave is 6 + 2n signals (n = wave number), capped at 30, at most 3 on
  the scope at once early on and up to 6 later.
- Drift speed starts at 12 px/s and rises 6% a wave, capped at 30 px/s.
- The ratio pool grows as in §1's table; types join as in §6.
- **Wave clear:** bonus = 50 × (100 − static) + 500 per unused dampen,
  counted up with a tick, then the static drop.
- After wave 20 the waves loop with speeds kept rising.
- A bonus dampen (a third for each later wave) every 25,000 points.

Movement scales with frame time (`_frameScale`), as everywhere else.

## 9. Look

- **Phosphor afterglow:** an 8-bit intensity buffer of the scope (160x106,
  ~17kB). Each frame, the figures draw into it at full brightness, then
  every pixel decays by a fixed fraction; the buffer is mapped through a
  green (and amber, for signals) palette into the canvas. Fast moves leave
  trails, still figures burn sharp, the way a real scope looks.
- Your figure is drawn with ~120 sample points joined by lines; signals
  with ~48. Precession when out of tune comes for free from the maths.
- Static: noise pixels written straight into the intensity buffer, so it
  glows and fades like everything else.
- Shatter: the signal's sample points become `ParticleManager` sparks.
- After each Chord the palette shifts (green → cyan → amber → white),
  then cycles.

## 10. Audio

**The resonance tones** are the heart of it, and need something the mixer
doesn't have yet: its synth is one square wave. Proposed: a small
**two-oscillator sine voice plus noise** in `audio/AudioMixer.h` (plain
C++, host-tested like the rest of the mixer):

- Oscillator 1, your tone: frequency mapped from your ratio, roughly
  240-560Hz across the dial.
- Oscillator 2, the target tone: the same mapping applied to the nearest
  signal's ratio. It fades in as you come within one ratio-step of it,
  louder as you close in.
- The beat is just the two summed: |f1 − f2| Hz, so the mapping is chosen
  so the match tolerance is a beat slower than ~2Hz.
- Noise channel: the static hiss, level from the static meter.

Phase can't be heard in two steady tones, so the ear finds the ratio and
the eye finds the phase. That split is part of the design.

Effects, each with a fallback, checked once with `audio.exists()` and
preloaded:

| Event | WAV | Fallback |
|---|---|---|
| Fire (in resonance) | `res_fire.wav` | tone |
| Shatter | `res_shatter.wav` | `explosion.wav` |
| Cascade (each extra) | `res_cascade.wav` | rising tone |
| Misfire | `res_miss.wav` | low tone |
| Signal reaches core | `res_hit.wav` | tone |
| Dampen | `res_damp.wav` | falling tone |
| Chord appears / layer stripped / down | `res_chord_warn.wav`, `res_chord_hit.wav`, `res_chord_die.wav` | tones; `star_boss_die.wav` |
| Wave clear, bonus tally | `res_clear.wav`, ticks | tones |
| Start / game over | melodies, as the other games | |

The resonance voice is silenced with everything else in the demo.

## 11. Attract cycle and demo

Title (a live Lissajous figure slowly morphing through the ratios, the
best score and name) → how-to-play (three slides: tuning, with a small
figure changing as a needle moves; resonance and firing; static) →
high-score table → a silent 30-40s demo → title. A starts a real game from
any of them. Nothing from the demo is scored or saved.

The autopilot:

- picks the signal nearest the core (in a Chord, the brightest layer);
- steers the ratio needle towards it with a proportional push, then the
  phase, with a little overshoot and reaction delay so it looks human;
- fires when in resonance; waits a moment for a cascade if a same-shape
  signal is on the scope and nothing is close;
- dampens when a signal is within 15px of the core ring and still not
  matched.

## 12. Implementation notes

- Files: `ResonanceFluxGame.h` (class, phases, attract, sounds),
  `ResonanceFigure.h` (Lissajous sampling, phase symmetry, the match
  test), `ResonanceSignals.h` (signals, waves, Chords), `ResonanceScope.h`
  (afterglow buffer and drawing), `ResonanceAutopilot.h`,
  `ResonanceConfig.h` (all tuning).
- RAM: the intensity buffer (~17kB) plus a few hundred bytes of state.
  Allocated with the game, freed on exit.
- Frame cost: the decay-and-map pass touches every scope pixel each frame
  (17k pixels). It should be fine at 30fps, but profile it with the
  harness's `profile` mode against Brick and Asteroid. Sine via a small
  table; no `sinf` per sample point.
- Flash: no large assets (the title is drawn).
- Registry: `{ "Resonance", makeGame<ResonanceFluxGame>, "resonance" }`
  and `{ "resonance", "RESONANCE", nullptr, nullptr }` in
  `hiscore::GAMES`.

## 13. Host harness

`test/resonanceflux_harness.cpp` (`test/build.sh resonance <scenario>`):

- `figure`: phase-symmetry table, matched settings draw near-identical
  point sets, clean ratios close and still;
- `match`: the tolerance and resonance curve;
- `play`: autopilot games; the score never drops, static stays in 0-100;
- `cascade`, `chord` (out-of-order fire restores layers), `waves` (every
  wave to 20 cleared by the autopilot with static pinned);
- `audio`: the new mixer voice; beat frequency from the two tones,
  silenced in the demo;
- `idle` and `demoexit`.

`DUMP_AT` frames of `play` and `chord` to check the scope look.

## Open questions

1. Should the tuning strip show tick marks for the ratios of signals on
   the scope (easier, more readable), or only the clean-ratio ticks (the
   player has to read the shapes)? Perhaps marks on the first few waves
   only.
2. A **Whisper** signal, invisible except a faint dot and found only by
   ear? The most original idea here, but unplayable with the volume down.
   Maybe a late-wave extra, or a setup option.
3. Should the hum of the tones be adjustable or switchable in setup, in
   case it wears on people over long games?
4. Is the match tolerance fair on the stick? That can only be judged on
   the board.

## Prototype

Built as a slice of the above, to judge on the board before the rest:
tuning (rate-controlled dial and phase, the snap, the roll off a clean
ratio, static's wander), Tone signals only, resonance and firing,
misfires, static and the overload, dampen, waves with the growing ratio
pool, scoring with the distance bonus and the wave tally, the afterglow
scope, the tuning strip, the hum (your tone, the focus signal's fading in
within one ratio-step, the hiss), and a title. High scores and name entry
too, since the cabinet's tests expect every game to have a table.

Left for later: cascades and chains, the other signals, Chords, the
attract cycle's how-to and demo (the autopilot is there, for the harness),
the WAV effects (tones, and `explosion.wav` when it's on the card), the
palette shifts.

Differences from the design above, found while building it:

- Signals are drawn at radius 10, not 7-9: at 8, a 3:4 or 4:5 figure
  blurred into a blob in the harness's frames.
- The match is a phase gap with the symmetries taken out (above), not a
  comparison of point sets: exact, cheap, and the gap bounds the look.
- Shattered figures fly apart as the game's own shards drawn into the
  afterglow (so they glow and fade like the rest), not `ParticleManager`.
- The hum's pitch is linear on the dial, 300-700Hz, so the match
  tolerance is a ~3Hz beat; the snap takes it to none.
- `DEBUG_LINE` (on) prints the focus's dial gap, phase gap and beat in the
  scope's corner, for tuning on the board.

## Needs the board

Everything that makes or breaks it: whether the figures read clearly at
this size on the panel, whether the beating can be heard and is pleasant
through the MAX98357A and speaker, how the rate-controlled tuning feels,
the match tolerance, the afterglow pass's frame cost, and whether the game
is fun once the novelty wears off. The prototype is for exactly this. The
first things to try: the tolerances (`RATIO_TOL`, `PHASE_TOL`, `SNAP_ZONE`),
the stick rates and curve, whether the beat helps, the hum and hiss
levels, the drift speeds, and the frame rate (the afterglow pass touches
~34k bytes a frame).
