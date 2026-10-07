# Handover

Where things stand, for a new session picking the cabinet up. CLAUDE.md
(working rules, layout, testing) and README.md (the full reference) come
first; this is what isn't in them: the state of the work, what's open, and
what's been learnt the hard way.

## State (main at the Maze redesign merge)

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
- **Stage select** (a test cheat), in Roll, Brick, Resonance and Maze: on
  the title, how-to or scores screens, hold B and press A; the stick picks
  the course, level or wave, A starts a test run (an orange T before the
  score, nothing put on the high-score table), B goes back. Harness
  scenario `pick` in each.
- **Since Resonance**, all merged: particle trails (Asteroid, Lander,
  Maze), Lander's flight rebalanced, Asteroid's Fire power-up and
  rebalanced power-ups, and Maze Flux reviewed and redesigned; the entries
  under Open below say what each did and what's left to check.
- The full suite was 60 PASS before Resonance; since then, by the owner's
  choice, only the harnesses a change touches run before a push (CLAUDE.md).

## Open

- **Sounds.** Roll's, Brick's and Resonance's own effect WAVs aren't on the card yet;
  the fallback tones are good enough for now and the owner will make the
  WAVs another time. README.md's audio section lists every file each game
  looks for.
- **Runner (Platform Flux) review**, on `claude/nice-carson-z0uxjl`, not
  merged. Fixed so far, each with a games2d scenario seen failing first:
  - Fire pits needed a trick nobody was told: only back-on-the-ground,
    forward-in-the-air cleared one (1 of 40 first pits with the stick
    left alone). The flames now burn under the runner's middle rather
    than its whole 18px box, and `PlatformManager::pitGapWidth` sizes
    them from the jump's own airtime: on the first loop a plain jump
    clears them (12-21px, 7+ frames to choose from), each later loop 8px
    wider, capped at what a pushed jump clears (`runnerpits`).
  - Where the floating platforms end (stage 5 into 6), the first ground
    block could stand up to 28px over the last slab with no gap; walked
    into, the runner sank through it and died (21 of 200 runs). It now
    starts level with the ground (`runnerwall`).
  - The game-over timeout went straight into the demo when the game had
    been started from the scores screen; now back to the title
    (`runnerover`).
  - Levitating with the stick down sank the runner into the stone, and it
    fell through and died when the flight ended; now its feet stay on or
    above the surface under it (`runnerlevitate`).
  - The diamond's "just before a fire pit" placement never ran (pits only
    in stages 1-2, the diamond only from 3). It now also turns up in
    stages 1-2 from the second loop, where pits are wide; the first loop's
    stay diamond-free (`runnerdiamond`).
  - Spike traps were invisible while retracted and scored 10 even if they
    never rose. Retracted ones show dim tips; points only if the trap rose
    or stood while the runner was over it (`runnerspikes`).
  - The title's best score and start prompt overlapped side by side; now
    two centred lines in the art's bottom strip.
  Needs the board: how the short pits look and feel at stages 1-2, and the
  wide ones from stage 9; whether the dim spike tips read on each loop's
  colours; the title strip.
  - Sounds: every Runner sound is an entry in its own table, the file
    checked once with `exists()` and preloaded, else a shared file, else
    the tone or melody it played before (`jump.wav` and `death.wav` were
    opened per play, so a missing one's blip came 300ms late). New
    optional files: `runner_title/points/star/fly/rock/boulder/stage/
    life.wav` (README's audio section); the managers now report a pickup
    or hit and the game plays it (`runnersounds`).
  - Harness scenarios of its own: `runnerplay` (the autopilot playing
    five real games to game over, checking respawn, stage bonuses and the
    loop's life; it reaches stages 8-50, which says the stages hold up
    for a perfect-reaction player, not how they feel), `runnerstages`,
    `runnername`.
  Still open from the review, in the proposed order: a stage select;
  the terrain vanishing during the 0.8s death; the score ticking by real
  time (a function `static`) not distance; particle trails; per-frame
  movement with no `_frameScale` (like the other 2D games).
- **Runner: longer play (owner's request, to plan next).** Stages repeat
  the same eight hazards each loop, only faster and recoloured. Ideas to
  propose, for the owner to pick from:
  - B is unused in Runner: a slide or duck under low beams and swooping
    ships, or a Moon Patrol gun (perhaps as a pickup) for ships and
    boulders.
  - Fire pits back in the later ground stages (6-8), mixed with spikes and
    boulders, so the wide pits have a home.
  - Crumbling slabs in the floating stages that drop a beat after you
    land, and springs that launch to a high route.
  - A high route with gem chains along jump arcs (a combo multiplier for
    taking the risky line) over a safe low one.
  - From the second loop, mixed stages: hazards from different tiers
    together (spikes on slabs, boulders under ships).
  - A boss stretch ending each loop: a big ship dropping rock patterns,
    survived for a set distance or shot down.
  - Conveyor or wind sections that push the runner's position, and a
    darker stage where only what's near the runner is lit.
- **Also outstanding:** Lander still has no optional WAVs checked with
  `exists()` and no stage select; trails could go on Runner and Brick;
  the owner's own WAVs for Roll, Brick, Resonance and Maze (the
  maze_door/switch/teleport files).
- **Needs the board:** the frame rate of Maze (its textured walls doubled
  the host cost), Asteroid (exhaust and Fire bolts) and Lander; the last
  `pio run` size line seen was 38.4% flash, 31.1% RAM, before the Maze
  redesign and Asteroid's Fire.
- **Particle trails** (merged):
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
- **Lander flight** (merged): the owner found it
  unbalanced from the start. Gravity 0.012 a physics step on level 1 (was
  0.025), +0.0025 every level to level 20 (0.0595); the HUD's G is the
  level up to 20; fuel tanks still from level 7. The engine was a fixed
  0.09 (7.5x gravity on level 1, a kick at every tap; 1.5x by level 20):
  now 2.8x the level's gravity, spooling up over 150ms and down over
  100ms, burning fuel as hard as it pushes. The stick is the normalised
  one (deadzone; the raw reading held the ship a few degrees off upright)
  and the ship turns to it at 180 degrees a second. A landing is over the
  pad with descent under 1.0, drift under 0.5 and tilt under 15 degrees,
  fuel or not (it was total speed under 1.1 with fuel left); V H A under
  the HUD show each check green or red, and the hull's flash near the
  ground follows the same rule. The demo autopilot was retuned for it
  (holds A while it wants more than the engine gives, brakes on a
  stopping-distance profile, comes upright over the pad). Feel needs the
  board. By level, too: the pad narrows a pixel every two levels, 24px
  to 16px from level 17 (the owner's floor), and the tank starts full to
  level 10, then 4% less a level to 60% from level 20; the fuel cores
  (from level 7, +40) stay. Not done: stick up/down as a throttle.
- **Asteroid Fire power-up** (merged): from 500
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
- **Maze redesign** (merged): a
  review found doors that blocked nothing, bullets through walls at a
  cell a frame, teleports that rebuilt the level, instant silent deaths
  and badly placed items; all fixed, plus a harness
  (test/mazeflux_harness.cpp: layout, doors, bullets, death, teleport,
  complete, buffer, wayfinding, a bot to level 15+, quit, idle). The bot
  also found design flaws, fixed: type A traps couldn't be walked
  against, type B's switch was out of reach from the far end, pads on a
  way through couldn't be passed. Then the owner's redesign: 16px cells,
  4px textured walls with a theme a level, a sprite explorer, smooth
  movement with a remembered push, an easing camera with look-ahead,
  mazes growing from 10x12 to 24x30, loops, breadcrumbs and a compass.
  README.md's Maze Flux paragraph has the rules. Needs the board: frame
  rate (host cost doubled with the textured walls, 22 to ~50us), feel,
  the clock's tuning, the themes' colours. Then, after the owner played
  it: cells passed with the stick held weren't marked or their items
  collected (fixed: PlayerMaze::arrivedX/Y), and the rest of the list:
  a map on B (seen cells only; you stand still, the rest goes on), a
  square per key in the HUD, the stage select (B held with A on the
  title), optional maze_door/maze_switch/maze_teleport.wav, and the
  attract demo (the harness's bot, moved into the game as autopilot()).
- **Maze now has an attract demo**, at the owner's request (it had been
  left out as dull to watch; at the new scale it isn't).
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
- A bot that plays the game through (lives pinned, counting what kills
  it and where) finds design flaws no rule check does: in Maze it found
  traps that couldn't be walked past, a switch out of reach and teleport
  pads that couldn't be crossed. Make it play like a careful player, so
  each death it can't avoid is the game's fault.
- Movement that starts the next step in the same update as it finishes
  one hides the cell just reached: Maze checked the cell the player was
  already heading into, so cells passed with the stick held weren't
  marked or their items collected. Record the arrival separately, and
  test with the stick held, not just stopping on each target.
- The owner sometimes commits WAVs (Git LFS) to the working branch: fetch
  before merging, and use the `-c filter.lfs.*` flags from CLAUDE.md.

## Working with the owner

As CLAUDE.md says: short answers, overview first; say briefly what you'll
write and ask before writing code; one `claude/<topic>` branch per piece
of work, pushed; merge to main only when told. They test on the board and
report back on feel, frame rate and sound; say what's been checked on the
host and what needs the board.
