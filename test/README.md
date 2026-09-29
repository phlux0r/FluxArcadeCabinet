# Host harnesses (Tank Flux, Tube Flux)

Run the 3D games' real game logic and the real Jet rasteriser on a desktop,
so a change can be checked without flashing the board. The point is regression
detection: each harness drives a scripted bot against a fake clock and a seeded
RNG, then hashes game state and the framebuffer each frame. **The same trace
before and after a change means behaviour was preserved.**

This is not PlatformIO's `pio test` — it doesn't use Unity and isn't run by
`pio test`. `pio run` ignores this folder, so it never affects a firmware
build.

## Running

```bash
test/build.sh                  # build, run every game's scenarios, print summaries
test/build.sh god 30000        # one Tank Flux scenario, full trace
test/build.sh god 30000 45     # ...at a 45ms frame time (~22fps)
test/build.sh tube god 30000   # the same for Tube Flux (tank is the default)
test/build.sh audio            # the audio mixer/loader tests, full output
test/build.sh --build-only
```

The third argument is milliseconds of fake clock per frame, default 16. The
game reads it as its own frame time, so it sets the frame rate being
simulated — useful now that movement scales with frame time (see
REFERENCE_FRAME_MS). At 33 the scale is exactly 1.0, so a trace there should
match one taken before that change.

Needs a host `g++` with C++17. Jet is picked up from `.pio/libdeps/` once
`pio run` has fetched it, or from `JET_SRC=/path/to/Jet/src`. Build artifacts
land in `test/.build/` (gitignored); delete it to force a rebuild.

Scenarios (both games have the first four):

| Mode | Tank Flux | Tube Flux |
|---|---|---|
| `play` | Normal run: the bot dies and restarts, so game over is covered | Same; the bot looks only a short way ahead, so it gets caught |
| `god` | Health pinned: many bosses and arena resets | Shield pinned: climbs every tier; prints hits per tier |
| `menus` | Attract exit, in-game A+B quit, game-over timeout | Attract exit, in-game hold-B quit, game-over timeout |
| `profile` | `god`, plus render cost by tanks on screen | `god`, plus render cost by tier |
| `pose` | | Renders fixed set-ups to `pose_*.ppm` (see below) |
| `idle` | | No input: the attract cycle (title, how-to-play, demo); checks the demo is silent |
| `demoexit` | | Presses A mid-demo: the real game must start clean. Prints PASS/FAIL and fails the build script |

`profile` reports Jet's per-frame triangle counts and host render time,
bucketed by how many tanks were on screen, plus the scene's total object and
triangle count as a drift check (an arena reset only moves existing objects,
so the totals must not grow). Host microseconds aren't ESP32 microseconds —
compare the buckets to each other, not to a frame budget.

## Looking at frames

The GFX stub keeps a real framebuffer, so any frame can be written out:

```bash
DUMP_AT=500,4000 test/build.sh tube play 5000   # tube_000500.ppm, tube_004000.ppm
```

Files land in `test/.build/`. The stub draws no text, so HUD text is absent;
everything Jet or the game draws directly is there. For Tube Flux,
`DUMP_STATE=1` also prints the angle, distance and camera rotation for each
dumped frame, and `DEBUG_HITS=1` prints the situation (angle, roll speed,
blocks nearby) every time the ship is hit.

Tube Flux's `pose` mode renders set-ups chosen to check conventions by eye:
a block in lane 2 must be on the right wall, and on the floor once the ship
rolls to 90 degrees. That is how Jet's roll and object-rotation directions
were confirmed (`CAMERA_ROLL_SIGN`, `OBSTACLE_ROLL_SIGN`), and how the
hand-drawn tunnel was checked against Jet's own projection.

## Checking a change

```bash
git stash && test/build.sh god 30000 > /tmp/before.txt
git stash pop && test/build.sh god 30000 > /tmp/after.txt
diff /tmp/before.txt /tmp/after.txt      # empty = behaviour unchanged
```

Expect a difference whenever the change was *meant* to alter behaviour — the
value is in knowing which of those two cases you're in. This is how the
Tank Flux file split and cleanup were verified as behaviour-preserving.

It builds with AddressSanitizer and UndefinedBehaviorSanitizer, so the same
run also catches memory errors and leaks (it's what found the 3D scene never
being freed on exit to the launcher).

## What it does not cover

- **Audio output.** The game harnesses use `stub/cabinet/AudioEngine.h`,
  which only counts calls. `audio_test.cpp` does test the real mixer and
  loader logic (mixing, clipping, voice stealing, the synth, WAV parsing,
  resampling, caching, streaming and seamless loops, mute ordering) against
  WAV files it writes to a temp dir. What neither sees is the device glue in
  `src/cabinet/AudioEngine.h`: I2S, the FreeRTOS tasks, the SD card, timing.
  The stub was blind to the bug where the engine's shared task state was
  `static` in a header, which silenced the whole cabinet once more than one
  `.cpp` included it.
- **How it looks or plays.** A framebuffer hash notices that pixels changed,
  never whether they're right. Same for feel, timing and difficulty.
- **Real timing.** The clock is fake and advanced a fixed step per frame, so
  nothing here reflects the frame rate on hardware. `profile` compares
  relative cost only.
- **Most of the 2D games, and `main.cpp`.** Beyond Tank Flux and Tube
  Flux, only the Runner and Asteroid attract demos are covered
  (`games2d_harness.cpp`: every demo silent, high score untouched, A
  mid-demo starts a clean game), not their gameplay.

So a clean harness run means "nothing changed unintentionally", not "this is
good to ship". Hardware still decides that.

## Layout

```
test/
├── build.sh                    # finds Jet, builds both harnesses + audio_test, runs
├── audio_test.cpp              # audio mixer/loader unit tests (no stubs needed)
├── games2d_harness.cpp         # Runner and Asteroid attract demos: idle + demoexit
├── harness_common.h            # fake clock, seeded RNG, trace hashing, frame dumps
├── tankflux_harness.cpp        # Tank Flux: scripted bot, scenarios, profile
├── tubeflux_harness.cpp        # Tube Flux: scripted bot, scenarios, profile, poses
└── stub/                       # shadows the hardware headers (-I'd first)
    ├── Arduino.h               # millis()/random()/math, no hardware
    ├── Adafruit_GFX.h          # GFXcanvas16 with a real RGB565 buffer
    ├── Preferences.h           # no-op settings/high score storage
    ├── Adafruit_ST7735.h, Fonts/   # display driver + font, for the 2D games
    ├── driver/, freertos/, SD.h, esp_heap_caps.h  # enough ESP32 for the real (inert) AudioEngine
    └── cabinet/AudioEngine.h   # call counter
```

Tube Flux's bot is the game's own autopilot (`TubeFluxGame::pilot()`, in
`TubeFluxDemo.cpp`), the same code that plays the attract demo, so the demo's
player is the one these scenarios exercise.
