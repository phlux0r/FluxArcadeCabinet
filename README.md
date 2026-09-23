# Flux Arcade Cabinet v2.0

ESP32-S3 handheld arcade cabinet running multiple games from a unified launcher:
Asteroid Flux, Lander Flux, Maze Flux, Platform Flux, and **3D Combat Flux** — a
first-person turret shooter rendered with [Jet](https://github.com/CubeCoders/Jet),
a dependency-free fixed-function 3D rasteriser. It's the cabinet's first 3D game;
low-poly fighters close in from a fixed cockpit view and a centre-screen reticle
fires through Jet's own screen-space picking.

## Build

This is a [PlatformIO](https://platformio.org/) project — open the folder in VS
Code with the PlatformIO extension installed, or from the CLI:

```bash
pio run -t upload      # build and flash
pio device monitor      # serial log, 115200
```

The ESP32-S3 talks to the host over native USB, so auto-reset into the
bootloader works normally — no BOOT/EN button dance needed. `platformio.ini`
targets `esp32-s3-devkitc-1` with the flash size and partition table adjusted
for the cabinet's SuperMini-class boards; see the comments at the bottom of
that file if your exact board's flash/PSRAM differ (e.g. a 16MB/8MB N16R8
variant vs. a 4MB/2MB one).

## Hardware

| Component | Part |
|---|---|
| MCU | ESP32-S3 |
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

## Project Structure

```
FluxArcadeCabinet/
├── platformio.ini              # Build config — board, partitions, lib_deps (incl. Jet)
├── include/
│   └── JetConfig.hpp           # Jet's per-frontend render config (see Jet's own README)
│
├── sd_assets/                  # Mirror of SD card contents
│   ├── asteroid_flux/
│   │   ├── splash.raw
│   │   ├── explosion.wav
│   │   └── gameend.wav
│   └── lander_flux/
│       ├── title.raw
│       └── theme.wav
│
└── src/                        # PlatformIO source root
    ├── main.cpp                # State machine orchestrator
    │
    ├── cabinet/                # Shared subsystems — no game logic here
    │   ├── ArcadeConfig.h      # All pins, screen constants, shared colours
    │   ├── InputManager.h      # Joystick + buttons, deadzone, edge detection
    │   ├── AudioEngine.h       # I2S audio: tones, melodies, WAV from PROGMEM/SD
    │   └── ParticleManager.h   # Shared 2D particle system (explosion + fire trails)
    │
    ├── games/
    │   ├── IGame.h             # Pure virtual interface all games implement
    │   ├── AsteroidFlux/
    │   │   ├── AsteroidFluxGame.h   # IGame wrapper + refactored game loop
    │   │   ├── AsteroidManager.h
    │   │   ├── PlayerShip.h         # Updated: takes joyY param, no analogRead
    │   │   ├── PowerUpManager.h
    │   │   ├── BackgroundStars.h
    │   │   ├── NebulaManager.h
    │   │   └── assets/
    │   │       ├── splash_image.h
    │   │       ├── explosion.h
    │   │       ├── gamestart.h
    │   │       └── gameend.h
    │   ├── LanderFlux/
    │   │   ├── LanderFluxGame.h     # IGame wrapper (thin)
    │   │   ├── GameEngineLander.h   # Updated: takes AudioEngine& + InputState
    │   │   ├── Ship.h
    │   │   ├── CavernObstacles.h
    │   │   └── assets/
    │   │       └── TitleScreen.h
    │   └── CombatFlux/
    │       └── CombatFluxGame.h     # First-person turret shooter, rendered via Jet
    │
    └── launcher/
        └── LauncherMenu.h      # Menu UI — receives InputState, no direct HW reads
```

## Adding a New Game

1. Create `src/games/MyGame/MyGameGame.h` implementing `IGame`
2. Add `STATE_MY_GAME` to the `CabinetState` enum in `src/cabinet/ArcadeConfig.h`
3. `#include` the game in `src/main.cpp`
4. Instantiate it and add it to `gameRegistry[]`
5. Add a `case STATE_MY_GAME:` to the switch in `loop()`

## Controls (in-game)

| Control | Action |
|---|---|
| Joystick | Move / steer |
| Button A | Fire / thrust / confirm |
| Button B (hold 2s) | Return to launcher |

## Audio

All audio is routed through the MAX98357A via I2S. The `AudioEngine` provides:
- Non-blocking tone/melody playback (`playTone`, `playMelody`)
- Canned sound effects (`playExplosionTones`, `playGameOverMelody` etc.)
- PROGMEM WAV streaming (`playSamplePROGMEM`)
- SD WAV streaming (planned — `SDCardManager.h`)

WAV files for SD playback should be: **8kHz, mono, 8-bit unsigned PCM**.

## 3D Rendering (Combat Flux)

3D Combat Flux is the first game built on [Jet](https://github.com/CubeCoders/Jet),
pulled in via `platformio.ini`'s `lib_deps` (a git dependency — Jet isn't on the
PlatformIO registry). Jet expects each frontend to supply its own
`JetConfig.hpp` on the include path; this project's copy lives at
`include/JetConfig.hpp`, tuned for the cabinet's 160×128 canvas (no Z-buffer,
no buffered post-FX, `MAX_PICK_QUERIES = 1` for the reticle hit test). See the
comments in that file, and in `src/games/CombatFlux/CombatFluxGame.h`, before
changing either.

The game renders straight into the launcher's `GFXcanvas16` buffer (same
RGB565 layout Jet expects), then draws the HUD and reticle over it with
ordinary `Adafruit_GFX` calls afterward — no separate framebuffer or extra
copy needed.
