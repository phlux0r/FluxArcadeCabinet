# FluxArcadeCabinet: notes for Claude

A handheld arcade cabinet: an ESP32-S3 (4MB flash, 2MB QSPI PSRAM), a
160x128 ST7735 TFT, an analogue joystick, buttons A and B, a MAX98357A I2S
amp and an SD card for audio. A launcher menu runs one game at a time.
README.md is the full reference (hardware, pins, controls, audio, every
game); test/README.md covers the host harnesses. Read the parts you need
before changing anything.

## Working with the owner

- Short answers, overview first. Before writing code, say briefly what
  you'll write and ask to go ahead (they often answer "go ahead" or
  "yes"). Don't apologise; fix and move on.
- Look for authoritative sources first (datasheets, official docs, the
  library's own source) before popular ones.
- They build and flash with PlatformIO and play on the cabinet; a cloud
  session can't download the ESP32 toolchain, so `pio run` won't work
  there. Say what's been checked on the host and what needs the board
  (frame rate, sound, feel).
- One branch per piece of work (`claude/<topic>`), pushed; merge to main
  only when they say so. They sometimes commit to the same branch
  (usually audio), so fetch before merging.

## Layout

- `src/main.cpp`: the launcher/game state machine and `gameRegistry[]`.
- `src/cabinet/`: shared subsystems, no game logic: `ArcadeConfig.h`
  (pins, screen, colours, per-game constants for the 2D games),
  `InputManager.h`, `AudioEngine.h` (+ `audio/` mixer and loader, plain
  C++ and host-tested), `HighScores.h` (tables and 3-letter name entry),
  `ParticleManager.h`.
- `src/launcher/`: the menu (scrolls six rows at a time), setup, the
  high-score viewer and its idle cycle.
- `src/games/<Name>Flux/`: one folder per game. The 2D games are
  header-only; the 3D ones (Tank, Tube, Star) have .cpp files and render
  with Jet (https://github.com/CubeCoders/Jet, pinned in platformio.ini).
- `sd/audio/`: the SD card's `/audio/` folder, WAVs in Git LFS.
- `test/`: host harnesses and stubs; `tools/`: asset generators.

## Adding a game

README.md's "Adding a New Game" is the checklist. On top of it, every game
so far has:

- an attract cycle: title, how-to-play, its high-score table, then a
  silent autopilot demo (`audio.setSilenced(true)`; nothing from a demo is
  scored or saved), A starting a real game from any of them;
- a 3-letter name entry at game over via `hiscore::ScoreBoard`, `record()`
  on a mid-game quit, and its best on the title;
- quitting is main.cpp's (hold Back, GPIO 18, for 1s): the game implements
  `onQuit()` to `record()` a game in progress, or `finishNow()` a name
  entry, and A and B stay purely its own;
- optional WAVs on the SD card, each with a fallback (a PROGMEM sample or
  a tone), checked once with `audio.exists()` and preloaded;
- a host harness scenario (bot play, attract/demo exit, menus), added to
  `test/build.sh`'s list.

Each game is built when launched and destroyed on exit, so only the one
being played uses RAM: keep state in the object. Flash is the tighter
limit (huge_app partition, ~3MB): title art and PROGMEM audio add up, so
check `pio run`'s size report when adding assets.

Joystick to screen directions depend on the game's rotation: landscape
(rotation 1) has up = joyLeft, down = joyRight, left = joyUp, right =
joyDown (games read joyY for horizontal); portrait (rotation 2) has up =
joyDown, down = joyUp, left/right as named. `hiscore::screenDirs` does it.

## Testing

```bash
JET_SRC=/path/to/Jet/src test/build.sh     # everything; exit 0 = all PASS
test/build.sh star god 12000                # one scenario, full trace
DUMP_AT=500,4000 test/build.sh tube play 5000   # frames as .ppm
```

`JET_SRC` is needed when `.pio/libdeps` hasn't been fetched (a cloud
session): clone Jet and check out the commit pinned in platformio.ini.
Harnesses use a fake clock and seeded RNG, so traces are repeatable; look
at frames (pose modes, `DUMP_AT`, `RUNNER_DUMP`) for anything visual.
When fixing a bug, write the check first and see it fail. Run the whole
suite before every push.

## Conventions

- Comments in the existing voice: plain British English, saying what a
  thing is for and why, matching the surrounding density.
- Keep README.md and test/README.md up to date with each change (controls,
  rules, sounds, scenarios).
- Frame timing: movement scales with real frame time (`_frameScale`
  against a reference frame), tuned around 30fps. Per-pixel backdrops
  (Tube, Star stages 2-5) work in 16-pixel spans in fixed point; profile
  with the harness's `profile` mode and compare stages, not absolute µs.
- Audio: the mixer plays one music stream, one long-jingle stream, six
  effect voices and a synth. Effects up to ~3.7s are decoded into a 1.25MB
  PSRAM cache (LRU); keep WAVs mono 44.1kHz 16-bit. A game's whole set
  should fit the cache, or its least-used sounds are re-read from SD late.
- Git LFS: `*.wav` goes through LFS (.gitattributes). A cloud session has
  no git-lfs; commits that only move or merge WAV pointers work with
  `-c filter.lfs.smudge= -c filter.lfs.process= -c filter.lfs.required=false`.
- Commit messages: a short subject, then what changed and why.
