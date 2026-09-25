# Host harness (Tank Flux)

Runs Tank Flux's real game logic and the real Jet rasteriser on a desktop, so
a change can be checked without flashing the board. The point is regression
detection: the harness drives a scripted bot against a fake clock and a seeded
RNG, then hashes game state and the framebuffer each frame. **The same trace
before and after a change means behaviour was preserved.**

This is not PlatformIO's `pio test` — it doesn't use Unity and isn't run by
`pio test`. `pio run` ignores this folder, so it never affects a firmware
build.

## Running

```bash
test/build.sh                # build, run all three scenarios, print summaries
test/build.sh god 30000      # one scenario, full trace
test/build.sh --build-only
```

Needs a host `g++` with C++17. Jet is picked up from `.pio/libdeps/` once
`pio run` has fetched it, or from `JET_SRC=/path/to/Jet/src`. Build artifacts
land in `test/.build/` (gitignored); delete it to force a rebuild.

Scenarios:

| Mode | What it covers |
|---|---|
| `play` | Normal run — the bot dies and restarts, so game-over is covered |
| `god` | Health pinned, so a long run reaches many bosses and arena resets |
| `menus` | Attract exit, in-game A+B quit, game-over timeout |

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

- **Audio.** `stub/cabinet/AudioEngine.h` only counts calls. The real engine
  needs I2S, FreeRTOS and an SD card. It was blind to the bug where the
  engine's shared task state was `static` in a header, which silenced the
  whole cabinet once more than one `.cpp` included it.
- **How it looks or plays.** A framebuffer hash notices that pixels changed,
  never whether they're right. Same for feel, timing and difficulty.
- **Real timing.** The clock is fake and advanced a fixed 16ms per frame, so
  nothing here reflects the frame rate on hardware.
- **The other four games, and `main.cpp`.** Tank Flux only.

So a clean harness run means "nothing changed unintentionally", not "this is
good to ship". Hardware still decides that.

## Layout

```
test/
├── build.sh                    # finds Jet, builds, runs
├── tankflux_harness.cpp        # fake clock, seeded RNG, scripted bot, tracing
└── stub/                       # shadows the hardware headers (-I'd first)
    ├── Arduino.h               # millis()/random()/math, no hardware
    ├── Adafruit_GFX.h          # GFXcanvas16 with a real RGB565 buffer
    ├── Preferences.h           # no-op high score storage
    └── cabinet/AudioEngine.h   # call counter
```
