# Flux Arcade Cabinet v2.0

ESP32-S3 handheld arcade cabinet running multiple games from a unified launcher.

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
├── sd_assets/                  # Mirror of SD card contents
│   ├── asteroid_flux/
│   │   ├── splash.raw
│   │   ├── explosion.wav
│   │   └── gameend.wav
│   └── lander_flux/
│       ├── title.raw
│       └── theme.wav
│
└── FluxMasterArcade/           # Arduino project (folder must match .ino name)
    ├── FluxMasterArcade.ino    # State machine orchestrator (~100 lines)
    │
    ├── cabinet/                # Shared subsystems — no game logic here
    │   ├── ArcadeConfig.h      # All pins, screen constants, shared colours
    │   ├── InputManager.h      # Joystick + buttons, deadzone, edge detection
    │   ├── AudioEngine.h       # I2S audio: tones, melodies, WAV from PROGMEM/SD
    │   └── ParticleManager.h   # Shared particle system (explosion + fire trails)
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
    │   └── LanderFlux/
    │       ├── LanderFluxGame.h     # IGame wrapper (thin)
    │       ├── GameEngineLander.h   # Updated: takes AudioEngine& + InputState
    │       ├── Ship.h
    │       ├── CavernObstacles.h
    │       └── assets/
    │           └── TitleScreen.h
    │
    └── launcher/
        └── LauncherMenu.h      # Menu UI — receives InputState, no direct HW reads
```

## Adding a New Game

1. Create `games/MyGame/MyGameGame.h` implementing `IGame`
2. Add `STATE_MY_GAME` to the `CabinetState` enum in `ArcadeConfig.h`
3. `#include` the game in `FluxMasterArcade.ino`
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
