# Host harnesses (Tank Flux, Tube Flux, Star Flux, Brick Flux)

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
test/build.sh star god 12000   # and Star Flux
test/build.sh audio            # the audio mixer/loader tests, full output
test/build.sh --build-only
```

The third argument is milliseconds of fake clock per frame, default 16 (33
for Star Flux). The
game reads it as its own frame time, so it sets the frame rate being
simulated — useful now that movement scales with frame time (see
REFERENCE_FRAME_MS). At 33 the scale is exactly 1.0, so a trace there should
match one taken before that change.

Needs a host `g++` with C++17. Jet is picked up from `.pio/libdeps/` once
`pio run` has fetched it, or from `JET_SRC=/path/to/Jet/src`. Build artifacts
land in `test/.build/` (gitignored); delete it to force a rebuild.

Scenarios (every game has the first four):

| Mode | Tank Flux | Tube Flux | Star Flux |
|---|---|---|---|
| `play` | Normal run: the bot dies and restarts, so game over is covered | Same; the bot looks only a short way ahead, so it gets caught | The autopilot with slow reactions and no bombs, so it takes hits and loses lives; prints each stage's results |
| `god` | Health pinned: many bosses and arena resets | Shield pinned: climbs every tier; prints hits per tier | Shield pinned: every stage and boss, into the next loop |
| `menus` | A Back quit from the attract screen and mid-game (`onQuit()`, `onExit()`, `init()`, as `main.cpp` does), game-over timeout | Same | Same, plus a B press (one bomb), a life lost (the segment must restart: PASS/FAIL), last life lost, game over |
| `profile` | `god`, plus render cost by tanks on screen | `god`, plus render cost by tier | `god`, plus render cost by stage and segment |
| `pose` | | Renders fixed set-ups to `pose_*.ppm` (see below) | Same: fighters, rocks, banking, each stage's hazards (with the flight aids, the rapid-fire pod and the canyon's mines, the mothership's blast doors), the five bosses, the title |
| `idle` | | No input: the attract cycle (title, how-to-play, demo); checks the demo is silent | Same |
| `demoexit` | | Presses A mid-demo: the real game must start clean. Prints PASS/FAIL and fails the build script | Same |
| `passive` | | | Shield pinned and the bot never fires: waves must still end on their own. Any non-boss segment over 40s prints STALL and fails |
| `loops` | | | Extra lives at 75k/125k/175k, shield overcharge (cap, carried, reset), difficulty by loop and its cap, rings never inside an obstacle (including a forced gapped wall), stage 4's frost shards slowing steering and mines homing, stage 5's blast doors (gap range, and the marker judging them on arrival). PASS/FAIL |
| `reach` | | | Every fighter of every wave, stage and loop 1-5 spends at least 700ms where your lasers can hit it (the V formations' outer ranks). PASS/FAIL |
| `rapid` | | | No pod in loop 1; in loop 2 the pod comes, catching it doubles the fire rate, a lost life takes it away; the flight-aid marker against a barrier and a gate. PASS/FAIL |

`profile` reports Jet's per-frame triangle counts and host render time,
bucketed by how many tanks were on screen, plus the scene's total object and
triangle count as a drift check (an arena reset only moves existing objects,
so the totals must not grow). Host microseconds aren't ESP32 microseconds —
compare the buckets to each other, not to a frame budget.

## Brick Flux

`test/brickflux_harness.cpp`, run with `test/build.sh brick <scenario> [frames]`.
Its bot is the game's own autopilot (`BrickFluxGame::autopilot()`, in
`BrickAutopilot.h`), as for Tube and Star Flux. Each prints PASS/FAIL.

| Scenario | What it checks |
|---|---|
| `play [N]` | The autopilot plays real games for N frames (default 40000, restarting through the name entry and game over): no ball ever inside a brick or outside the field, the score never drops within a game, and it clears levels and makes smashes |
| `wall` | A breakable brick past the danger line costs exactly one life, its row goes and the formation is pushed back three rows; steel there shatters and costs nothing; the step interval by level and loop |
| `smash` | A ball dropping onto the bat with A released 20, 100 and 200ms before it arrives, and 30ms after: Perfect, Good, nothing (meter kept full), Good |
| `tunnel` | Balls at near top speed, 50ms frames, at angles from 20 to 160 degrees, at a single row of steel: none gets through |
| `polarity` | A ball off the other colour bounces (brick intact, chain broken); three of its own colour in a row score 20, 40, 60; neutral bricks break to either colour; smashes and lasers break either; the ball takes the bat's colour at the bat; B's 250ms cooldown |
| `living` | A buried gun stays quiet, a clear one fires its colour; a bolt of the bat's colour is absorbed (+2 meter), one of the other stuns it for 0.75s; a magnet turns a passing ball; a portal carries the ball above the steel; the third spark caught is a life, the fourth isn't |
| `levels [N]` | The autopilot, lives pinned, plays every regular level of the first loop; each must be cleared within N frames (default 12000), with no ball ever inside a brick. Prints the time and lives lost per level. This is what found a portal putting the ball out inside a brick |
| `boss [N]` | The same for each of the four bosses (default 15000 frames) |
| `idle [N]` | No input: title, how-to-play, scores, then the demo: silent, high score untouched, and it ends back at the title |
| `demoexit` | A mid-demo starts a clean game (level 1, no score, three lives, a ball waiting to be served) |

`DUMP_AT=20,400 test/build.sh brick play 401` writes `brick_000020.ppm` etc.;
with `levels` or `boss`, it writes those frames of every level
(`brick_L05_001500.ppm` is the Warden 50 seconds in).

## Looking at frames

The GFX stub keeps a real framebuffer, so any frame can be written out:

```bash
DUMP_AT=500,4000 test/build.sh tube play 5000   # tube_000500.ppm, tube_004000.ppm
```

Files land in `test/.build/`. The stub draws no text, so HUD text is absent;
everything Jet or the game draws directly is there, lines and circles
included. For Tube Flux,
`DUMP_STATE=1` also prints the angle, distance and camera rotation for each
dumped frame, and `DEBUG_HITS=1` prints the situation (angle, roll speed,
blocks nearby) every time the ship is hit.

Tube Flux's `pose` mode renders set-ups chosen to check conventions by eye:
a block in lane 2 must be on the right wall, and on the floor once the ship
rolls to 90 degrees. That is how Jet's roll and object-rotation directions
were confirmed (`CAMERA_ROLL_SIGN`, `OBSTACLE_ROLL_SIGN`), and how the
hand-drawn tunnel was checked against Jet's own projection. Star Flux's does
the same for its camera roll and the fighters' yaw, pitch and roll
(`CAMERA_ROLL_SIGN`, `YAW_SIGN`, `PITCH_SIGN`, `ROLL_SIGN`), and marks where
its own `project()` puts each rock, which must be the rock's centre.

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
- **Most of the older 2D games.** Beyond the 3D games and Brick Flux, only
  the Runner, Asteroid and Lander attract demos are covered
  (`games2d_harness.cpp`: every demo silent, high score untouched, A
  mid-demo starts a clean game), not their gameplay.

So a clean harness run means "nothing changed unintentionally", not "this is
good to ship". Hardware still decides that.

## Layout

```
test/
├── build.sh                    # finds Jet, builds every harness + audio_test, runs
├── audio_test.cpp              # audio mixer/loader unit tests (no stubs needed)
├── games2d_harness.cpp         # Runner, Asteroid and Lander attract demos: idle + demoexit
├── brickflux_harness.cpp       # Brick Flux: play, wall, smash, tunnelling, polarity, living bricks, levels, bosses, demo
├── cabinet_sim.cpp             # all of main.cpp: launch every game, quit it with Back (B and a short Back mustn't); a Back quit records the score; menu scrolling; idle score cycle
├── hiscore_test.cpp            # high-score tables: storage, carry-over, ranking, name entry, timeout
├── harness_common.h            # fake clock, seeded RNG, trace hashing, frame dumps
├── tankflux_harness.cpp        # Tank Flux: scripted bot, scenarios, profile
├── tubeflux_harness.cpp        # Tube Flux: scripted bot, scenarios, profile, poses
├── starflux_harness.cpp        # Star Flux: the autopilot, scenarios, profile, poses
└── stub/                       # shadows the hardware headers (-I'd first)
    ├── Arduino.h               # millis()/random()/math, no hardware
    ├── Adafruit_GFX.h          # GFXcanvas16 with a real RGB565 buffer
    ├── Preferences.h           # in-memory NVS: settings and tables persist within a run
    ├── Adafruit_ST7735.h, Fonts/   # display driver + font, for the 2D games
    ├── driver/, freertos/, SD.h, esp_heap_caps.h  # enough ESP32 for the real (inert) AudioEngine
    └── cabinet/AudioEngine.h   # call counter
```

Tube Flux's bot is the game's own autopilot (`TubeFluxGame::pilot()`, in
`TubeFluxDemo.cpp`), the same code that plays the attract demo, so the demo's
player is the one these scenarios exercise. Star Flux's is too
(`StarFluxGame::pilot()`, in `StarFluxDemo.cpp`).
