# Handover

Where things stand, for a new session picking the cabinet up. CLAUDE.md
(working rules, layout, testing) and README.md (the full reference) come
first; this is what isn't in them: the state of the work, what's open, and
what's been learnt the hard way.

## State (main at 15126e2)

Nine games, all merged and played on the board: Asteroid, Brick, Lander,
Maze, Roll, Runner (Platform Flux), Star, Tank, Tube. A tenth, Resonance,
is a prototype on its branch (below).

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
- The full suite was 60 PASS at the last push.

## Open

- **Sounds.** Roll's and Brick's own effect WAVs aren't on the card yet;
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
- **Resonance Flux** is a prototype, in the menu as Resonance: an
  original oscilloscope game where you match Lissajous figures by eye and
  by ear (the mixer's new hum: two sines and a hiss). Built as a slice of
  docs/design/ResonanceFlux.md to judge on the board; that note's
  "Prototype" section says what's in and out, "Needs the board" what to
  try. Not yet run on the cabinet. The owner decides from it whether to
  build the rest.
- **Maze has no attract demo, and that stays.** The owner doesn't want
  one: it isn't an exciting game to watch. Maze's attract cycle is title,
  how-to-play and the table.
- Flash is the tight limit (huge_app, ~3MB). Before adding assets or a new
  game, ask the owner for `pio run`'s size line; a cloud session can't
  build for the board.

## Learnt the hard way

- **Run the suite before every push**, the whole of it
  (`JET_SRC=/path/to/Jet/src test/build.sh`, exit 0). A cloud session
  needs Jet cloned and checked out at the commit pinned in platformio.ini.
  It takes a while (ASan builds); run it in the background.
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
