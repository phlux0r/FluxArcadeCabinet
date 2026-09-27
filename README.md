# Flux Arcade Cabinet v2.0

ESP32-S3 handheld arcade cabinet: six games behind one launcher, all sharing
the cabinet's display, audio, input and particle subsystems.

| Game | Menu name | Orientation | What it is |
|---|---|---|---|
| Asteroid Flux | Asteroids | Landscape | Asteroid shooter with power-ups and a nebula backdrop |
| Lander Flux | Lander | Portrait | Fuel-limited landing through a scrolling cavern |
| Maze Flux | Maze | Portrait | Generated mazes, collectibles and roaming obstacles |
| Platform Flux | Runner | Landscape | Side-scrolling runner: platforms, boulders, flying enemies |
| Tank Flux | Tank | Landscape | First-person 3D tank battle (see below) |
| Tube Flux | Tube | Landscape | 3D tunnel runner: roll round the wall to dodge blocks |

Tank Flux and Tube Flux are rendered with
[Jet](https://github.com/CubeCoders/Jet), a dependency-free fixed-function
rasteriser. In Tank Flux you drive an arena of hills, rocks, trees and a
river, fighting tanks that flank and fire back, with a boss every 15 kills
that re-rolls the arena when it dies. In Tube Flux you fly down an endless
octagonal tunnel, rolling round its wall to dodge blocks; every ~30 seconds
a gate raises the tier, which speeds things up, packs the blocks closer and
brings in wider ones; from tier 4 the tunnel starts to curve, hiding what's
coming, and the curves get sharper from tier 6. You start unarmed: a yellow
chevron late in tier 3 is the gun, and from tier 4 orange crystals appear
that it can shoot, some of them sitting in the only open lane.

## Build

A [PlatformIO](https://platformio.org/) project — open the folder in VS Code
with the PlatformIO extension, or from the CLI:

```bash
pio run                 # build
pio run -t upload       # build and flash
pio device monitor      # serial log, 115200
```

The ESP32-S3 talks to the host over native USB, so auto-reset into the
bootloader works normally — no BOOT/EN button dance. `platformio.ini` targets
`esp32-s3-devkitc-1`, overridden for this cabinet's 4MB-flash/2MB-PSRAM
SuperMini-class board and built as C++17 (Jet requires it). If your board is a
larger 16MB/8MB N16R8 variant, see the commented block at the bottom of that
file; `esptool.py flash_id` confirms which chip you have.

## Hardware

| Component | Part |
|---|---|
| MCU | ESP32-S3 (4MB flash, 2MB PSRAM) |
| Display | ST7735 TFT (160×128 physical) |
| Audio | MAX98357A I2S amplifier + speaker |
| Storage | SD card (shared SPI) |
| Controls | X-Y joystick + 2 buttons |

## Pin Assignments (ArcadeConfig.h is the single source of truth)

| Signal | GPIO |
|---|---|
| TFT CS | 10 |
| TFT RST | 9 |
| TFT DC | 8 |
| TFT BLK | 7 |
| SD CS | 2 |
| I2S BCLK | 16 |
| I2S LRC | 15 |
| I2S DIN | 14 |
| JOY X | 1 |
| JOY Y | 17 |
| BTN A | 4 |
| BTN B | 21 |

## Controls

| Control | Action |
|---|---|
| Joystick | Move / steer |
| Button A | Fire / thrust / confirm |
| Button B (hold 2s) | Return to launcher |

Tank Flux differs: **hold B** strafes while driving, so quitting mid-game is
**hold A+B** for 2s instead (a progress bar appears once you've held them long
enough for it not to be a normal shot).

Tube Flux: the joystick rolls you round the tunnel (left/right) and nudges
the speed (up boosts, down brakes). A starts a run, and fires once you've
picked up the gun. Hold B to quit.

The launcher's "[JOY] MOVE / [BTN A] GO" hint sits below the background
art's menu box (rows 126 and 136), leaving the box for the game list: six
rows fit, and a seventh would need a tighter row pitch.

## Project Structure

Everything is header-only except Tank Flux, Tube Flux and `main.cpp`, so PlatformIO
compiles one translation unit per `.cpp` and pulls the rest in by include.

```
FluxArcadeCabinet/
├── platformio.ini              # Board, partitions, C++17 flags, lib_deps (incl. Jet)
├── include/
│   └── JetConfig.hpp           # Jet's per-frontend render config (see Jet's own README)
│
└── src/                        # PlatformIO source root
    ├── main.cpp                # State machine: launcher <-> games, frame timing
    │
    ├── cabinet/                # Shared subsystems — no game logic here
    │   ├── ArcadeConfig.h      # Pins, screen constants, shared colours, CabinetState
    │   ├── InputManager.h      # Joystick + buttons, deadzone, edge detection
    │   ├── AudioEngine.h       # I2S audio: tones, melodies, WAV from PROGMEM/SD
    │   ├── ParticleManager.h   # Shared 2D particle system (explosions, trails)
    │   └── PowerManager.h      # Power button, checked from the menu only
    │
    ├── assets/shared/          # Assets used by more than one game
    │   ├── SharedAssets.h      # PROGMEM fallbacks: explosion, game start/end
    │   └── ArcadeScreen.h      # Launcher artwork
    │
    ├── launcher/
    │   └── LauncherMenu.h      # Menu UI — receives InputState, no direct HW reads
    │
    └── games/
        ├── IGame.h             # Interface every game implements
        ├── AsteroidFlux/       # AsteroidFluxGame.h + ship/asteroid/power-up/
        │                       # background managers + assets/
        ├── LanderFlux/         # LanderFluxGame.h (thin) + GameEngineLander.h,
        │                       # Ship.h, CavernObstacles.h + assets/
        ├── MazeFlux/           # MazeFluxGame.h + GameEngineMaze.h, generator,
        │                       # renderer, player, collectibles, sprites + assets/
        ├── PlatformFlux/       # PlatformFluxGame.h + platform/boulder/enemy/
        │                       # power-up managers, PlayerRunner.h + assets/
        ├── TankFlux/           # First-person 3D battle, rendered via Jet:
        │   ├── TankFluxGame.h      # Class declaration (state + method groups)
        │   ├── TankFluxConfig.h    # All tuning constants + per-tank-type TankSpec
        │   ├── TankFluxGame.cpp    # Lifecycle, phases, per-frame update loop
        │   ├── TankFluxScene.cpp   # Jet scene, materials, tank models
        │   ├── TankFluxArena.cpp   # Arena layout -> 3D objects, boss-kill reset
        │   ├── TankFluxPlayer.cpp  # Driving, collisions, shells, repair kits
        │   ├── TankFluxEnemies.cpp # Enemy/boss spawning, AI, firing
        │   ├── TankFluxHud.cpp     # HUD, radar, overlays, menu screens
        │   ├── ArenaLayout.h/.cpp  # Obstacle/tree/kit placement + obstacle collision
        │   ├── TankGeometry.h/.cpp # Mesh builders and terrain height
        │   ├── TankMath.h          # Inline angle/distance/timing helpers
        │   └── assets/             # Attract-screen art, 160x128 landscape
        └── TubeFlux/           # Tunnel runner, rendered via Jet:
            ├── TubeFluxGame.h      # Class declaration
            ├── TubeFluxConfig.h    # All tuning: tunnel, speed, tiers, spacing, fairness
            ├── TubeFluxGame.cpp    # Lifecycle, phases, per-frame update loop
            ├── TubeFluxScene.cpp   # Tunnel (drawn directly), block meshes, ship sprite
            ├── TubeFluxPlay.cpp    # Steering, speed, tiers, spawning, collisions
            ├── TubeFluxHud.cpp     # HUD, overlays, menu screens
            ├── TubeShipSprite.h    # GENERATED ship sprite (tools/tube_ship_sprite.py)
            └── assets/             # GENERATED title screen (tools/tube_title_screen.py)
```

`test/` sits alongside `src/` and holds the desktop harnesses (see Testing
and Profiling). `pio run` ignores it, so it never reaches a firmware build.
`tools/` holds asset generators. Tube Flux's ship sprite is edited as text
art in `tools/tube_ship_sprite.py`, and its title screen is rendered by
`tools/tube_title_screen.py` (needs numpy and Pillow); each writes its header.

## Audio

All audio is routed through the MAX98357A via I2S. `AudioEngine` runs playback
on its own FreeRTOS task (Core 0) so nothing blocks the game loop, and provides:

- Non-blocking tones and melodies (`playTone`, `playMelody`)
- PROGMEM sample playback (`playSamplePROGMEM`), used as the no-SD fallback
- SD WAV playback, one-shot or looping (`playWAV`, `loopWAV`, `playWAVThenLoop`)

Only one sound plays at a time: starting a new one stops whatever was playing,
and `playTone()` is skipped entirely while a WAV is streaming.

### SD card

Optional — without a card (or without a given file) the cabinet falls back to
PROGMEM samples or generated melodies. WAV files live in a single flat
`/audio/` folder on the card:

| File | Used by |
|---|---|
| `gamestart.wav`, `gameend.wav`, `explosion.wav` | Shared across games |
| `asteroid_loop.wav` | Asteroid Flux |
| `lander_start.wav`, `countdown.wav`, `land_success.wav` | Lander Flux |
| `jump.wav`, `death.wav` | Platform Flux |
| `tank_start.wav`, `tank_loop.wav`, `shot.wav`, `repair.wav` | Tank Flux |

The header parser accepts any sample rate, mono or stereo, 8-bit unsigned or
16-bit signed PCM. Keep them small: they stream from the SD card over the SPI
bus the display also uses.

## Testing and Profiling

`test/` runs the 3D games' real game logic (Tank Flux and Tube Flux) and the
real Jet rasteriser on a desktop against a fake clock and a seeded RNG,
hashing game state and the framebuffer each frame. The same trace before and after a change means
behaviour was preserved — which is how the Tank Flux file split was verified,
and how the 3D scene leaking on exit to the launcher was found (it builds with
AddressSanitizer). It is not `pio test` and does not need the board.

```bash
test/build.sh                # build, run every scenario, print summaries
test/build.sh god 30000      # one Tank Flux scenario, full trace
test/build.sh tube god 30000 # one Tube Flux scenario
test/build.sh profile 40000  # per-frame render cost by what was on screen
DUMP_AT=500,4000 test/build.sh tube play 5000   # also write those frames as .ppm
```

See `test/README.md` for the scenarios, how to diff a change, and — just as
important — what it cannot see (audio, how anything looks or plays, real
timing, the four 2D games).

For timing on the actual hardware, uncomment `-DSHOW_FPS` in `platformio.ini`.
It draws `fps avgMs/peakMs` in the corner and logs a fuller line to serial
once a second. The peak column is the useful one: a single stalled frame is
invisible in the average.

Two things dominate the frame budget and are worth knowing before chasing a
slow frame:

- **The display push is a fixed cost.** 160x128x2 = 40KB per frame over SPI,
  about 8ms at `TFT_SPI_SPEED`'s current 40MHz (12ms at the old 26.67MHz).
  Nothing in game code makes that cheaper.
- **Per-object work beats triangle count in Tank Flux.** Three enemy tanks
  cost more per frame than a boss does, despite fewer triangles between them —
  it is the object count Jet walks, not the geometry.
- **Pixels Jet fills are expensive in this build.** Jet's fast span path for
  flat unlit triangles needs half-width buffers and no depth fog, and this
  project's `JetConfig.hpp` has neither, so every pixel Jet draws goes
  through the general per-pixel path. A tunnel covers the whole screen, and
  drawn by Jet it cost more than Tank Flux's worst case. Tube Flux therefore
  fills its tunnel directly (`drawTunnel()`), using Jet's own projection, and
  leaves Jet only the blocks and the sprite: about the cost of a typical Tank
  Flux frame.

Tank Flux and Tube Flux scale their movement by measured frame time
(`REFERENCE_FRAME_MS` in their config headers), so they play at the same
speed whether running at 40 or 22fps. The other games do not; their speed
still follows the frame rate.

## Adding a New Game

1. Create `src/games/MyGame/MyGameGame.h` implementing `IGame`
2. Add `STATE_MY_GAME` to the `CabinetState` enum in `src/cabinet/ArcadeConfig.h`
3. `#include` the game in `src/main.cpp`
4. Instantiate it and add it to `gameRegistry[]`
5. Add a `case STATE_MY_GAME:` to the switch in `loop()`

`IGame::onExit()` is optional and worth implementing if the game allocates
anything substantial: it's called when returning to the launcher, so the heap
goes back to whatever runs next (Tank Flux frees its whole 3D scene there).

## 3D Rendering (Tank Flux, Tube Flux)

Jet is pulled in via `platformio.ini`'s `lib_deps` as a git dependency — it
isn't on the PlatformIO registry — pinned to a commit, so every machine builds
the same renderer. Tube Flux needs at least that commit (its ship is a
`Sprite2D` using `FLIP_X`). To move the pin, change the hash and re-run
`test/build.sh`: a changed trace shows what the new Jet changed. Jet expects each frontend to supply its own
`JetConfig.hpp` on the include path; this project's copy is at
`include/JetConfig.hpp`, tuned for the 160×128 canvas (no Z-buffer, no
buffered post-FX). Read the comments there, and in `TankFluxConfig.h`, before
changing either — particularly the ground-mesh and fog sizing notes, which
exist because getting them wrong starved Jet's render queue on real hardware.

The game renders straight into the launcher's `GFXcanvas16` buffer (the same
RGB565 layout Jet expects), then draws the HUD, gun barrel and radar over it
with ordinary `Adafruit_GFX` calls — no second framebuffer or extra copy.
