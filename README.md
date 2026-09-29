# Flux Arcade Cabinet v2.0

ESP32-S3 handheld arcade cabinet: seven games behind one launcher, all sharing
the cabinet's display, audio, input and particle subsystems.

| Game | Menu name | Orientation | What it is |
|---|---|---|---|
| Asteroid Flux | Asteroids | Landscape | Asteroid shooter with power-ups and a nebula backdrop |
| Lander Flux | Lander | Portrait | Fuel-limited landing through a scrolling cavern |
| Maze Flux | Maze | Portrait | Generated mazes, collectibles and roaming obstacles |
| Platform Flux | Runner | Landscape | Side-scrolling runner in stages: 3 lives, platforms, boulders, flying enemies |
| Star Flux | Star | Landscape | 3D on-rails space shooter: three stages of fighter waves, hazards and bosses (see below) |
| Tank Flux | Tank | Landscape | First-person 3D tank battle (see below) |
| Tube Flux | Tube | Landscape | 3D tunnel runner: roll round the wall to dodge blocks |

Tank Flux, Tube Flux and Star Flux are rendered with
[Jet](https://github.com/CubeCoders/Jet), a dependency-free fixed-function
rasteriser. In Tank Flux you drive an arena of hills, rocks, trees and a
river, fighting tanks that flank and fire back, with a boss every 15 kills
that re-rolls the arena when it dies. The boss fires on its own faster
timer, leads you with its bursts, and turns angry (red turret, faster,
5-shell spread) below half health; each boss beaten makes the next one
quicker. In Tube Flux you fly down an endless
octagonal tunnel, rolling round its wall to dodge blocks; every ~30 seconds
a gate raises the tier, which speeds things up, packs the blocks closer and
brings in wider ones; from tier 4 the tunnel starts to curve, hiding what's
coming, and the curves get sharper from tier 6. You start unarmed: a yellow
chevron late in tier 3 is the gun, and from tier 4 orange crystals appear
that it can shoot, some of them sitting in the only open lane. Magenta
chevrons upgrade it to twin guns (tier 7) and rapid fire (tier 9), and a
green cross restores a shield when you've lost one. From the tier-6 gate,
and every five gates' distance after, a drone chases you: behind you first,
where lanes flash red before its bolts come down them, then overtaking to
fly ahead, weaving and dropping crystals, until you shoot it down or it
escapes. At tier 9, a stretch of one lane now and then flashes gold: fly
through that portal for a 30-second bonus round of gem formations (green
100, cyan-white 250, gold 500) to shoot, with +10,000 for getting them all.
Left alone, its attract screen cycles the title, a how-to-play slide and a
silent 30-45 second demo, the autopilot playing a random tier (sometimes
into a bonus round).

In Star Flux you fly on rails, the camera behind your ship, steering it
round the screen while the stage comes at you: a fly-in with the stage
name, waves of red fighters in formations (V dives, sweeping columns,
head-on pairs, a wing overtaking from behind, a weaving snake) that swoop,
fire and break away, hazard fields between them, silver rings that restore
40 shield, and a boss. Each stage runs 70-100 seconds and ends with a
results screen (fighters downed, targets, rings, and a bonus for the
shield you kept); after the third, the game loops back to the first with
faster fire and tougher bosses.

1. **Aurora Belt** (space): rock fields (big rocks take three hits and
   split). The dreadnought: two wing cannons firing aimed shots; lose one
   and it adds ring bursts that close round where you were (stay put); lose
   both and its core opens, firing spreads and faster bursts.
2. **Ember Reach** (a planet's surface at dusk): pillars to go round or hop
   over, arches to fly under, turret towers that shoot back (shoot the
   turret on top). The crawler, a tracked fortress: two missile pods; lose
   one and it launches homing missiles (shoot them down, or dodge late:
   they stop steering close in); lose both and its dome fires five-way
   spreads between missiles.
3. **Trench Run** (a space station): barriers across the trench with a slot
   to fly through (upright, level or a window), laser gates that blink on
   and off (fly over or under while they're on), turret towers. The
   reactor at the trench's end: two emitters; then spiral streams; then a
   rotating shield fan over its core, which only shots through the gap
   reach.

A shield bar and 3 lives: an empty shield costs a life and restarts the
wave (or field) you were in; a boss keeps the damage you'd done. Two sights
show your line of fire, red when a target is on it. Shooting down a whole
wave scores a 500 bonus.

**Attract demos.** Left alone, every game except Maze does the same: title,
how-to-play, then a silent 30-40 second demo played by an autopilot, and
back to the title. A starts a real game straight from a demo; nothing from
a demo (score, high score) is kept.

| Game | The demo |
|---|---|
| Runner | a random stage 2-7; the autopilot predicts on the game's own rules and jumps at the best moment |
| Tank | a random level 3-6, sometimes with a boss due; aims, keeps its range, sidesteps a glowing barrel |
| Asteroid | a busier field (3-6 asteroids); steers for the spot furthest from every predicted asteroid path |
| Lander | random levels; plans a route through the rocks, lands, and flies the next; sometimes comes in too fast and crashes |
| Tube | a random tier, sometimes into the bonus round |
| Star | a random stage and point in it, sometimes the boss; dodges by where shots, rocks and obstacles will cross its path, leads its targets, bombs packs |

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

Runner (Platform Flux) plays in stages, like Moon Patrol's checkpoints:
each hazard section is a numbered stage, and the rule under the HUD fills
as you get through it. You have 3 lives; losing one restarts the stage you
were on (score kept) with 2s of blinking protection. Clearing a stage
scores 50, or 100 if you didn't die in it, and finishing a full loop of 8
stages gives an extra life (up to 5). Jumps are forgiving: A pressed just
before landing still jumps, and so does one just after running off an edge.

Tube Flux: the joystick rolls you round the tunnel (left/right) and nudges
the speed (up boosts, down brakes). A starts a run, and fires once you've
picked up the gun. Hold B to quit.

Star Flux: the joystick flies the ship round the screen. A fires twin
lasers, once per press as fast as you tap, or steadily while held. Tap B
for a smart bomb (3, topped back up to 3 when you lose a life): it flies
ahead and blows everything near it apart, clears every enemy shot in the
air, and hurts the boss. Hold B to quit; a bomb needs B released within
0.4s, so a hold never drops one.

The launcher's "[JOY] MOVE / [BTN A] GO / [BTN B] SETUP" hint sits below the
background art's menu box, leaving the box for the game list: six rows fit,
and a seventh would need a tighter row pitch. The hint uses the 5px TomThumb
font, centred, at a 9px pitch (baselines 129/138/147): the box's border is
row 123 and the last line just touches the art's INSERT COIN at row 146.

**B** in the launcher opens **SETUP**: MASTER, MUSIC and FX volume bars
(joystick up/down to pick, left/right to set, 0 is off) and BACK (or B
again). Every change is saved to NVS at once and applied on boot.

Every game's game-over screen ignores A and B for its first second, then
acts only on a fresh press (`ArcadeConfig::GAMEOVER_INPUT_DELAY_MS`), so
buttons still being mashed when a game ends can't restart it or quit.

## Project Structure

Everything is header-only except the 3D games (Tank, Tube and Star Flux) and `main.cpp`, so PlatformIO
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
    │   ├── AudioEngine.h       # I2S output, mixer/loader tasks, the games' audio API
│   ├── AudioEngineLegacy.h # the old one-sound engine (-DAUDIO_LEGACY)
│   ├── audio/AudioMixer.h  # software mixer: voices, streams, synth (host-tested)
│   ├── audio/AudioLoader.h # WAV parsing, effect cache, music streaming (host-tested)
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
        ├── StarFlux/           # On-rails space shooter, rendered via Jet:
        │   ├── StarFluxGame.h      # Class declaration
        │   ├── StarFluxConfig.h    # All tuning: camera, ship box, weapons, enemies, boss
        │   ├── StarFluxGame.cpp    # Lifecycle, phases, per-frame update loop
        │   ├── StarFluxScene.cpp   # Meshes, space backdrop, starfield, 2D shots/rings/reticle
        │   ├── StarFluxWorld.cpp   # Planet and trench backdrops, obstacles, gates, turrets
        │   ├── StarFluxPlay.cpp    # Stage scripts, ship, lasers, bombs, rocks, rings
        │   ├── StarFluxEnemies.cpp # Fighter flight paths, enemy shots and missiles
        │   ├── StarFluxBoss.cpp    # The three bosses
        │   ├── StarFluxDemo.cpp    # Attract demo, and the autopilot (also the harness's bot)
        │   ├── StarFluxHud.cpp     # HUD, overlays, results and menu screens
        │   ├── StarShipSprite.h    # GENERATED ship sprite (tools/star_ship_sprite.py)
        │   └── assets/             # GENERATED title screen (tools/star_title_screen.py)
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
            ├── TubeFluxPlay.cpp    # Steering, speed, tiers, bends, spawning, pickups, shots
            ├── TubeFluxChase.cpp   # The drone chase: warnings, bolts, overtake, the fight
            ├── TubeFluxBonus.cpp   # Tier-9 portal, bonus round gem formations, tally
            ├── TubeFluxDemo.cpp    # Attract demo, and the autopilot (also the harness's bot)
            ├── TubeMath.h          # Angle and timing helpers
            ├── TubeFluxHud.cpp     # HUD, overlays, menu screens
            ├── TubeShipSprite.h    # GENERATED ship sprite (tools/tube_ship_sprite.py)
            └── assets/             # GENERATED title screen (tools/tube_title_screen.py)
```

`test/` sits alongside `src/` and holds the desktop harnesses (see Testing
and Profiling). `pio run` ignores it, so it never reaches a firmware build.
`tools/` holds asset generators. Tube Flux's ship sprite is edited as text
art in `tools/tube_ship_sprite.py`, and its title screen is rendered by
`tools/tube_title_screen.py` (needs numpy and Pillow); each writes its header.
Star Flux's ship is a small 3D model in `tools/star_ship_model.py`, which
`tools/star_ship_sprite.py` renders into the sprite's three bank frames and
`tools/star_title_screen.py` puts on the title screen, so the two match.

## Audio

All audio goes out through the MAX98357A via I2S. Like the retro-go
emulators, the cabinet mixes in software: every playing source is summed
into the one output stream, so music, effects and tones play together.
At once it can play:

- one music track (`loopWAV`) and one jingle (`playWAVThenLoop`'s intro, or
  any effect too long to cache), both streamed from SD
- four effects (`playWAV`, PROGMEM fallbacks); a fifth replaces the oldest
- the tone/melody synth (`playTone`, `playMelody`)

Music and effects are separate buses, each switchable in the launcher's
SETUP page, under a master volume. The sum is soft-clipped rather than
scaled down, so a lone sound keeps its full level.

Two FreeRTOS tasks run on core 0 (the game loop is on core 1, and only posts
commands to them through lock-free queues, so it never blocks):

- **mixer** (priority 5) mixes 256-frame blocks at 44.1kHz. It never touches
  the SD card, so a slow card can't make it stutter, and only ~35ms of audio
  is queued ahead of the speaker, so sounds start promptly.
- **loader** (priority 3) does all SD access. Effects are decoded into PSRAM
  the first time they play (or at `preload()`) and then play from memory;
  the cache is 768KB, least-recently-used first out. Music streams through
  a ~743ms ring buffer (it rides out the SD card stalling while the
  display holds the shared bus, seen at up to 400ms).

The mixer and loader are plain C++ (`src/cabinet/audio/`) with a host test,
`test/audio_test.cpp`. `AudioEngine.h` is the device glue around them. If
the mixer misbehaves on the hardware, build with `-DAUDIO_LEGACY`
(commented out in `platformio.ini`) to get the old one-sound-at-a-time
engine back; it honours the music/FX switches too.

### Audio diagnostics

For now the engine logs to serial (build with `-DAUDIO_DEBUG=0` to stop it):
each file it opens (rate, bits, channels, length, cached or streamed), read
failures, and every 2s while anything is happening a line like

```
[AUDIO] wav 40 (hit 36 load 4 big 0 drop 0) voices 40 stolen 3 | tone 12/12 | music underrun 0 loops 2, jingle underrun 0 | sd err 0 short 0 | qfull 0/0 epoch-drop 0 | max: loader 9ms mix 180us period 6ms
```

- `wav`: playWAVs asked for; `hit` played from memory, `load` read in from
  SD, `big` too long to cache (streamed), `drop` given up on.
- `voices`: effects that actually sounded; `stolen`: cut off for a newer one.
- `tone started/asked`: a gap means tones went missing.
- `underrun`: a stream ran dry (the SD card couldn't keep up): a gap in it.
- `sd err`/`short`: reads that failed or came back short (retried; neither
  restarts the music any more).
- `qfull`: commands lost to a full queue (mixer/loader); `epoch-drop`:
  sounds dropped for being asked for before a mute().
- `max`: the slowest loader step (SD time), the slowest mix, and the longest
  gap between mixed blocks, over the last 2s. A `period` past ~35ms means
  the speaker ran out of audio.

### SD card

Optional — without a card (or without a given file) the cabinet falls back to
PROGMEM samples or generated melodies. WAV files live in a single flat
`/audio/` folder on the card:

| File | Used by |
|---|---|
| `gameend.wav`, `explosion.wav` | Shared across games |
| `lander_start.wav`, `land_success.wav`, `pickup.wav` (fuel pickup) | Lander Flux |
| `powerup.wav` (extra life, shield and slow-time pickups) | Asteroid Flux |
| `jump.wav`, `death.wav` | Platform Flux (Runner) |
| `tank_start.wav`, `shot.wav`, `repair.wav` | Tank Flux |
| `powerup.wav` (any pickup), `tube_shot.wav`, `tube_bump.wav` (losing a shield); also `explosion.wav` | Tube Flux |
| `tube_shot.wav` (lasers), `tube_bump.wav` (hit), `explosion.wav` (shot down); optional sounds below | Star Flux |

Star Flux's optional sounds: each is used if it's on the card, else what
the last column says. Mono 16-bit 44.1kHz, short (they're cached).

| File | When | Length | Otherwise |
|---|---|---|---|
| `star_pop.wav` | a fighter, rock, turret or missile destroyed | ~0.3s | `explosion.wav` |
| `star_hit.wav` | a laser hits a boss weak point or a turret | ~80ms | a tone |
| `star_armor.wav` | a laser hits boss armour or the reactor's fan | ~60ms | a tone |
| `star_boss_warn.wav` | a boss arrives | ~1.5s | three beeps |
| `star_boss_fire.wav` | a boss fires | ~0.25s | a tone |
| `star_burst.wav` | ring burst, missile launch, spiral stream | ~0.4s | a tone |
| `star_part_down.wav` | a cannon, pod or emitter destroyed | ~0.8s | `explosion.wav` |
| `star_core_open.wav` | a boss's core exposed | ~0.6s | a tone |
| `star_boss_die.wav` | a boss destroyed | ~2.5s | `explosion.wav` |
| `star_bomb.wav` | a smart bomb goes off | ~1s | `explosion.wav` |
| `star_ring.wav` | a shield ring collected | ~0.4s | `powerup.wav` |

Each game also has a music track, named after its launcher entry:
`flux-asteroids.wav`, `flux-lander.wav`, `flux-maze.wav`, `flux-runner.wav`,
`flux-star.wav`, `flux-tank.wav`, `flux-tube.wav`. It loops during a game only (not on the
attract/title screen), carries on through lost lives and between-level
screens, and stops at game over. A missing track just means no music.
Mono 16-bit 44.1kHz is the best fit: it streams from SD while the game
plays, and mono halves the card traffic.

The header parser accepts any sample rate, mono or stereo, 8-bit unsigned or
16-bit signed PCM (mixed at 44.1kHz, stereo folded to mono). Effects up to
~3.7s (320kB decoded) are cached; longer ones stream like music, which
costs an SD open per play, and only one plays at a time. Music and long
sounds share the SPI bus with the display, so keep them modest.

## Testing and Profiling

`test/` runs the 3D games' real game logic (Tank, Tube and Star Flux) and the
real Jet rasteriser on a desktop against a fake clock and a seeded RNG,
hashing game state and the framebuffer each frame. The same trace before and after a change means
behaviour was preserved — which is how the Tank Flux file split was verified,
and how the 3D scene leaking on exit to the launcher was found (it builds with
AddressSanitizer). It is not `pio test` and does not need the board.

```bash
test/build.sh                # build, run every scenario, print summaries
test/build.sh god 30000      # one Tank Flux scenario, full trace
test/build.sh tube god 30000 # one Tube Flux scenario
test/build.sh star god 12000 # one Star Flux scenario
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

The 3D games scale their movement by measured frame time
(`REFERENCE_FRAME_MS` in their config headers), so they play at the same
speed whether running at 40 or 22fps. The other games do not; their speed
still follows the frame rate.

## Adding a New Game

1. Create `src/games/MyGame/MyGameGame.h` implementing `IGame`
2. `#include` the game in `src/main.cpp`
3. Add `{ "My Game", makeGame<MyGameGame> }` to `gameRegistry[]`

Games are built when launched and destroyed on the way back to the menu
(`makeGame()` in `main.cpp`, in internal RAM where there's room), so only the
game being played takes memory, however many there are: each game object is
7-11kB, and six of them used to sit in RAM permanently. Keep a game's state
in its object rather than in globals or statics, which stay allocated.

`IGame::onExit()` is optional and worth implementing if the game allocates
anything substantial outside its object (Tank Flux frees its 3D scene there).
A game that pushes its canvas to the display itself overrides `setTFT()` and
returns true from `flushesItself()`; otherwise `main.cpp` pushes each frame.

The launcher list shows six games at a time and scrolls, with an arrow above
or below when there are more that way, and keeps its place when you return
from a game.

## 3D Rendering (Tank Flux, Tube Flux, Star Flux)

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
