# Handover

Where things stand, for a new session picking the cabinet up. CLAUDE.md
(working rules, layout, testing) and README.md (the full reference) come
first; this is what isn't in them: the state of the work, what's open, and
what's been learnt the hard way.

## State (main at the Resonance merge)

Ten games, all merged and played on the board: Asteroid, Brick, Lander,
Maze, Resonance, Roll, Runner (Platform Flux), Star, Tank, Tube.

- **Resonance Flux** (the tenth, original: an oscilloscope where you match
  Lissajous figures by eye and by ear) is finished: the stepped dial of
  ratios, signals weaving in, static, dampen, Chord bosses every fifth
  wave, the attract cycle and demo, a wave select, options (ratio hint,
  notes, pace, bosses, debug line) with a score multiplier, optional
  WAVs. It brought the mixer's hum (two sine tones and a hiss). The
  owner played it to wave 14 and tuned it over several rounds;
  docs/design/ResonanceFlux.md has the design and that history. Still to
  check on the board: `pio run`'s size line and the frame rate (the
  afterglow touches ~34kB a frame).

- **Roll Flux** (the ninth, 3D marble) is finished: 20 courses in four
  worlds, a guardian ending each, the Prism Works' colour pieces, the Flux
  Core's moving parts, the attract cycle and demo, high scores, rolling
  ticks. The owner reports 55fps or more everywhere, the arenas included,
  and that every course plays well. Design and history:
  docs/design/RollFlux.md.
- **Brick Flux** is finished, with bosses every fifth level.
  docs/design/BrickFlux.md.
- **Stage select** (a test cheat), in Roll and Brick only: on the title,
  how-to or scores screens, hold B and press A; the stick picks the course
  or level, A starts a test run (an orange T before the score, nothing put
  on the high-score table), B goes back. Harness scenario `pick` in both.
- The full suite was 60 PASS before Resonance; since then, by the owner's
  choice, only the harnesses a change touches run before a push (CLAUDE.md).

## Open

- **Sounds.** Roll's, Brick's and Resonance's own effect WAVs aren't on the card yet;
  the fallback tones are good enough for now and the owner will make the
  WAVs another time. README.md's audio section lists every file each game
  looks for.
- **What's next** was left open. The idea on the table: bring the older
  2D games (Asteroid, Lander, Maze, Runner) up to the newer ones' standard
  (optional WAVs checked with `audio.exists()` and preloaded, a stage
  select where it fits, anything else the newer games have that they
  lack). Read each game before proposing: a quick grep suggested none of
  the four checks for WAVs with `exists()`, but they may get sound another
  way.
- **Particle trails** (branch `claude/particle-trails`, not merged yet):
  `ParticleManager`'s spawn calls take an optional trail length (0-4);
  a particle with one keeps its last positions, and how bright it was at
  each, and render() draws lines back through them, falling off to about
  70%, 45%, 25% and 12%. To look like Resonance's afterglow, a trailed
  spark dims to a quarter over the last 40% of its life (no white flash) and
  its trail lingers and drains for a few updates after it. On in Asteroid
  (every burst, plus an engine exhaust off the ship each frame, shortened
  once after the owner found it busy) and Lander (crash and fuel-pickup
  sparks 4, the thrust plume 2). Lander's particles were updated twice a
  frame while the ship broke up, so the debris flew at double speed; now
  once. Host-checked with `games2d` (`trails`, `lander`) and dumped frames;
  the look, feel and frame rate need the board. Then perhaps Maze,
  Runner and Brick, the other games on `ParticleManager`.
- **Asteroid Fire power-up** (branch `claude/asteroid-fire`): from 500
  points Fire is in the power-up draw; holding A then shoots bolts (BoltManager.h) every 160ms for 20s. A bolt
  breaks the asteroid it meets (a trailed burst), which scores and counts
  towards the field filling up as passing it would. The line under the HUD
  is the timer, flashing in the last 3s; a hit ends it. `tube_shot.wav`
  and `shot.wav` (the owner's choice) if on the card, else tones. The demo
  shoots when it has it. The owner found Fire rare, so the power-ups were
  rebalanced: one every 12-25s (was 15-40s), one draw weighted shield 30,
  slow 20, Fire 35 (from 500), extra life 15 (when due), drawn once more
  on a repeat (it was a chain of rolls that left Fire about 1 in 4 and
  slow-time the commonest). The extra life's threshold now resets with a
  new game. Power-ups also keep clear of asteroids: they spawn in a clear
  lane and steer away from any about to cross them (AsteroidManager::
  dodge(), 14 frames ahead; boxed in or at the top or bottom, forwards or
  back). Feel and balance need the board.
- **Maze has no attract demo, and that stays.** The owner doesn't want
  one: it isn't an exciting game to watch. Maze's attract cycle is title,
  how-to-play and the table.
- Flash is the tight limit (huge_app, ~3MB). Before adding assets or a new
  game, ask the owner for `pio run`'s size line; a cloud session can't
  build for the board.

## Learnt the hard way

- **Run the harnesses a change touches before every push** (CLAUDE.md
  lists which); the whole suite (`JET_SRC=/path/to/Jet/src
  test/build.sh`, exit 0) only when the owner asks. A cloud session needs
  Jet cloned and checked out at the commit pinned in platformio.ini. The
  ASan builds take a while; run them in the background.
- For quick iteration, build one harness at -O2 without ASan (the suite's
  own flags are in test/build.sh), and run a single scenario. Something
  that passes at -O2 can still fail under the suite, so the suite has the
  last word.
- Never `pkill -f test/build.sh` in the same command that then starts the
  suite: the pattern matches the shell's own command line and kills it.
- The host canvas stub doesn't draw text. Dumped frames (`DUMP_AT`, pose
  modes) show panels and shapes but not their words; check text layout by
  arithmetic (6 pixels a character at size 1, 12 at size 2).
- Course tables in headers (Roll's `COURSES`) are per translation unit:
  compare their strings, not their pointers, from the harness.
- The audio stub (test/stub/cabinet/AudioEngine.h) counts calls; add to it
  when a game starts asking the engine something new (it gained
  `isTonePlaying()` for Roll's ticks).
- The synth is one square wave at a fixed level, shared by every fallback
  tone and melody, and a new tone cuts the last one short. Anything
  continuous on it clicks; Roll's ticks give way to other synth sounds
  instead (sfxTone()/sfxMelody() note when their sound ends).
- When fixing a bug, write the harness check first and watch it fail.
- The owner sometimes commits WAVs (Git LFS) to the working branch: fetch
  before merging, and use the `-c filter.lfs.*` flags from CLAUDE.md.

## Working with the owner

As CLAUDE.md says: short answers, overview first; say briefly what you'll
write and ask before writing code; one `claude/<topic>` branch per piece
of work, pushed; merge to main only when told. They test on the board and
report back on feel, frame rate and sound; say what's been checked on the
host and what needs the board.
