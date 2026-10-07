# Host harnesses (Tank Flux, Tube Flux, Star Flux, Brick Flux, Roll Flux, Resonance Flux)

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
| `pick` | The stage-select cheat: B held with A on the title opens it (B still held doesn't close it); the stick steps the level by one and by five, wrapping round the loop, loading each; B goes back; A starts a test run on the chosen level, whose game over skips the name entry and whose quit saves nothing, and A at its game over starts the same level again; plain A still starts level 1; left alone the picker goes back to the title |

`DUMP_AT=20,400 test/build.sh brick play 401` writes `brick_000020.ppm` etc.;
with `levels` or `boss`, it writes those frames of every level
(`brick_L05_001500.ppm` is the Warden 50 seconds in).

## Roll Flux

`test/rollflux_harness.cpp`, run with `test/build.sh roll <scenario>`:

| Scenario | What it does |
|---|---|
| `physics` | The ball rolls up a ramp, and climbs one met at any speed up to a boost pad's (not bounced off it); can't climb a block two steps high and stops a radius short of it; stays between rails at full speed with 100ms frames (and goes over the same edge without them); coasts further and steers far less on ice; the camera follows the ball sideways but holds its heading while it rolls back towards it; no course has a ramp that drops away at its top (a sawtooth); a boost pad throws it on, past its usual top speed; a full dash clears a two-cell gap but not a three, and rolling at top speed doesn't clear the two; a dash on open floor is over (braked back) within 3.5 cells; a gate turns back the other colour and passes its own; a phase bridge holds its colour, drops the other, and catches a ball swapped to it in the air; a crystal wall stops a roll and a dash smashes it (50 points); a bumper kicks the ball back hard; a conveyor carries a ball at rest; falls into a gap; turns the way it rolls, by the distance over its radius; and is never below the floor it's on at full speed with 100ms frames. PASS/FAIL |
| `rules` | A gem's points, time and dash-meter gem; a fall back to the start, then (after the checkpoint row) to the checkpoint with its time; time running out (the course from the top); the goal's tally and the next course, and round again after the last with 15% less time; the dash (A does nothing without a step, a tap uses nothing, a held charge holds the ball back, letting go dashes and uses the step); a slider carries a ball sitting on it across, a lift raises it, a turning bridge turns it round, a sweeper's arm knocks it away; a tap of B swaps the ball's colour on the release (not with the stick moved, not inside the cooldown); B and the stick turn the camera without pushing the ball, the camera stays turned after B, and stick up then rolls the ball its new way; game over into name entry, then A to start again. PASS/FAIL |
| `guardians` | Each guardian's rules on its own course: a weak point hit only by a dash while it's lit (and not again while it's shut); three hits beat it (the course clear, its bonus in); the Piston's slam throws a ball up and its core drops to the floor, lit; the Prism's core hit through a panel of the ball's colour and not through the other; the Gyre's ring opens under a ball and its tilt pushes one; an arena's gems grow back. PASS/FAIL |
| `courses` | Every course (a guardian's has no goal, beating it clears it): rows the right length, one start, a goal, and the autopilot's planner finds a way from the start to the goal, to every gem, and on from each gem to the goal. PASS/FAIL |
| `god [N]` | The autopilot on every course in turn, its balls topped up: it must reach each goal before the clock runs out (N frames a course at most, default 4000). Prints time left, falls, gems, dashes and colour swaps per course |
| `play [N]` | A real game from 1-1 with the autopilot at the stick until its balls run out (or N frames, default 20000): it must clear at least two courses, and the course must cover most of the view. Prints how far it got and host µs per frame (mean, 95th percentile, max) |
| `idle` | No input: title, the three how-to-play slides and the scores, then the demo (rolling, making no sound) and back to the title, the high scores untouched. PASS/FAIL |
| `demoexit` | A mid-demo: a real game starts clean (1-1, no score, all balls, an empty dash meter, sound back on). PASS/FAIL |
| `menus` | A starts a game from each attract screen; Back (`onQuit()`) mid-game puts the score on the table, and during name entry keeps it. PASS/FAIL |
| `ticks` | The rolling sound: one tick per tile edge crossed on the floor, none at rest or in the air, none while the synth plays another sound or one the game asked for that frame. PASS/FAIL |
| `pick` | The stage-select cheat: B held with A on the title opens it (B still held doesn't close it); the stick steps the course by one and by a world, wrapping, loading each; B goes back; A starts a test run on the chosen course (full balls, empty meter), whose game over skips the name entry and whose quit saves nothing, and A at its game over starts the same course again; plain A still starts 1-1; left alone the picker goes back to the title. PASS/FAIL |
| `pose` | Frames at fixed points as `roll_<spot>_000000.ppm`: course 1-1 (start, rails, ramp, gap, railed bridge, ice and checkpoint, block, a sideways view, the goal, the lean with the stick, a ball fallen just behind the near edge, which must hide its lower half), 1-4's levels, the Ice Relay's palette with the ball glowing in a dash, the aim line while a dash charges, the Prism Works (a shut gate, the crystal wall, the bumpers, phase bridges with ghosts, conveyors), the Flux Core (a shuttle, a lift, the sweeper's plaza, a ball riding a turning bridge), the four guardians' arenas, the title over the orbiting course, and the stage select on 4-5 (`roll_pick`) |

Host µs compare runs with each other (and with Tube's `profile`, whose
cost runs at ~30fps on the board), not with a frame budget.

## Resonance Flux

`test/resonanceflux_harness.cpp`, run with `test/build.sh resonance
<scenario>`. Its bot is the game's own autopilot
(`ResonanceAutopilot.h`). Each prints PASS/FAIL.

| Scenario | What it checks |
|---|---|
| `figure` | The figure maths, against densely sampled curves: phases a multiple of 2π/b apart, and phases mirrored about π − aπ/b, draw the same figure; a phase gap bounds how far apart the figures are (so the phase tolerance is a bound in pixels); phases three tolerances apart look visibly different; the ratios are in dial order |
| `match` | Wave 1's dial is two stops (1:2, 1:1) and wave 3's four; resonance needs your stop on the signal's ratio and the phase inside the tolerance, a third of a turn round counting for 2:3; on the next stop it's no match, and the signal is only marked as nearest the core; a small push doesn't step, a push steps one stop, right from the last goes to the first and left from the first to the last, and held it steps once then repeats; up turns the phase and it holds when let go; on a signal's stop you hear its note; a misfire costs static and holds the next shot back; a shot in resonance shatters and scores, and the focus moves off it; a signal at the core adds static; dampen on the HUD: a block per charge, the one spent draining over the slowdown and gone after, the core's ring cyan meanwhile; high static makes your phase wander, and it settles after; a swaying signal strays from the straight line in and arrives later; 400 spawns all start on the sides or near the corners, 40px or more out; a new wave's stops keep you on your ratio; the nearest signal's stop is lit amber on the dial (not yours); the pace through wave 20 (speed capped, four at once at most, arrivals 2.5s apart, over 5s from the nearest start); static at 100 ends the game |
| `play [N]` | The autopilot plays real games for N frames (default 20000), restarting through name entry and game over: static stays in 0-100, the score never drops within a game, at least three waves are cleared, and shatters outnumber signals let through three to one |
| `quit` | The title waits for A; Back mid-game (`onQuit()`, `onExit()` freeing the scope's planes, `init()`) puts the score on the table, and a clean game after |
| `idle [N]` | No input (default 3000 frames): title, the three how-to slides and the scores, then the demo (the autopilot shattering signals, making no sound) and back to the title, the high scores untouched |
| `demoexit` | A mid-demo: a real game starts clean (wave 1's two stops, no score, no static, no signals, sound back on) |
| `sounds` | The optional WAVs: with no card each event's tone, melody or nothing; `explosion.wav` for a shatter when it's there and `res_shatter.wav` isn't; with every file on the card, a misfire, resonance beginning, a shatter, a signal reaching you, dampen, a wave cleared, the next wave starting and overload each ask for their own file |
| `options` | The how-to slides clear the bands above and below the scope (the last screen's lines once showed under theirs); B pressed and let go on the title opens the options and B there goes back, while B held with A is still the wave select and letting B go there opens nothing; right turns the ratio hint off, down and right the notes, the pace steps round both ways, bosses goes off and on again, A turns the debug line off; the score multiplier for those (x1.25 x1.25 x1.5); saved and back after `init()`; in play, no amber on the dial, neither note, 1.25x the drift, a shatter scoring its points times the multiplier; then the defaults back |
| `chord` | The boss: no Chord on wave 4, one on wave 5 (three different ratios, no wave signals of its own), none with bosses off; the brightest layer in resonance and fired strips it, scores and moves it to the other side; a dimmer one fired at brings the stripped layer back with static; escorts come while it's up, two at most; wave 10's layers drift, wave 15's middle layer swaps ratio; the autopilot (static pinned) brings down waves 5, 10, 15 and 20's and the wave clears |
| `pick` | The wave select: B held with A on the title opens it (B still held doesn't close it); the stick steps the wave by one and by five, wrapping 1-20; B goes back; A starts a test run on the chosen wave (fresh, its stops), whose game over skips the name entry and whose quit saves nothing, and A at its game over starts the same wave again; plain A still starts wave 1; left alone the picker goes back to the title |

`DUMP_AT=6000,6300 test/build.sh resonance play 6301` writes
`resonance_006000.ppm` etc.; with `idle`, `resonance_idle_<frame>.ppm`
(each attract slide lasts ~182 frames: title from 0, the how-to slides
from ~182, ~364 and ~546, scores ~728, then the demo), and with `pick`,
`resonance_pick_<frame>.ppm`. The harness builds the game against the real
(inert) audio engine, so it can't hear the hum; `audio_test` checks the
mixer's side of it (pitch, glide, a 3Hz beat from 400 and 403Hz, mute).

## Maze Flux

`test/mazeflux_harness.cpp`, run with `test/build.sh maze <scenario>`. Its
bot is the game's own autopilot (`GameEngineMaze::autopilot()`, which
plays the attract demo): it walks the shortest open way to the next key (lowest colour first),
then the exit, never through a teleport pad it isn't heading for; at a
trap's line of fire it waits for a gap (type A) or presses A for the
switch (type B). Each prints PASS/FAIL.

| Scenario | What it checks |
|---|---|
| `layout` | Levels 1-20, four mazes each: a key per door plus the exit's, each in its stretch and reachable once the doors before it are open; with doors shut the exit can't be reached; nothing outside the maze, on the exit, within three steps of the start or two on one cell; no bomb that reaches the start; every trap with three or more cells of lane, none through the start's surroundings; teleport pads paired, in one stretch, apart and in dead ends; the clock sized to the maze |
| `doors` | A shut door stops you; collecting its key opens it and you go through |
| `bullets` | Three thousand frames of levels 9-15 at frame times from 17 to 56ms: every cell a bullet enters, it enters through an open way, and its cell matches its position; 300ms of flight covers the same distance at 17ms and 50ms frames |
| `death` | A bullet: the pause, then a life gone, back at the start, safe (a bullet on you does nothing), then hit again once that's over; the clock: a life gone and the clock back to the level's time; a bomb you stand by goes off after its fuse and costs a life |
| `teleport` | Walking onto a pad puts you on its partner, and you stay there |
| `complete` | Keys and the exit score as the rules say; the level-complete screen ignores A held through it and goes on at a fresh press |
| `buffer` | A push towards a side passage, made mid-step and let go, is taken at the next cell |
| `passing` | Levels 1-12, straight runs of four or more cells walked with the stick held at uneven frame times: every cell passed is marked (breadcrumbs), and a key and a boost on the way are collected, the key only once the player has reached its cell (not as the step before it ends) |
| `play [N]` | The bot plays from level 1 for N frames (default 60000), lives pinned: it must reach level 15 losing no more than five lives (so no level is unfair to careful play), and the score never drops |
| `quit` | Back mid-game puts the score on the table |
| `idle` | No input for two minutes: title, how-to, scores, then the demo (a level 2-8, about 35s), back to the title and round again; A held from the menu ignored, the table untouched, sound on afterwards |
| `demoexit` | A mid-demo starts a real game at level 1, score 0, three lives, at the start, sound on |
| `pick` | The stage select: B held with A on the title opens it; the stick steps by one and five, wrapping 1-30, loading each maze; A starts a test run there, whose game over skips the name entry and whose A starts the same level again; its quit saves nothing; B goes back, and plain A still starts level 1 |
| `map` | B held shows the map; you don't move while it's up but the clock goes on; only cells that have been on screen are drawn (the far corner black); letting go closes it |

`DUMP_AT=300,9000 test/build.sh maze play 9001` writes `maze_000300.ppm`
etc. `MAZE_TRACE=1` prints the bot's progress and every life lost.

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
  resampling, caching, streaming and seamless loops, mute ordering, the hum's
  pitch, glide and beating) against
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
  mid-demo starts a clean game), not their gameplay; besides those it
  checks Runner's fire pits (`runnerpits`: every jump frame tried on a
  copy of the game; on stages 1 and 2 a plain jump clears every first pit
  with 4 or more frames to choose from, and on stages 10, 19 and 28 the
  pits are wider than the first loop's and a jump pushed forward still
  clears them), the ground after the floating platforms (`runnerwall`:
  200 runs through stages 5-6, the first ground block never more than a
  stair step, 8px, above the slab before it) and the game-over timeout
  (`runnerover`: back to the title, not straight into the demo, even when
  the game was started from the scores screen), levitation held down
  (`runnerlevitate`: never inside the ground, no fall when it ends), the
  diamond (`runnerdiamond`: none on stages 1-2 nor Night's 10-11, placed
  before fire pits on stages 19-20) and spike traps (`runnerspikes`: a retracted trap shows,
  and passing one scores only if it rose or stood while the runner was
  over it), its sounds (`runnersounds`: each event's tone or melody with
  no card, a shared file such as `powerup.wav` when that's all there is,
  its own file when that's there; each event in play asking for its
  sound; a demo asking for none), real games played by the autopilot
  (`runnerplay`: five seeded games to game over; a lost life restarts its
  stage with the score kept and a shield, a cleared stage scores 100 or
  50 after a lost life, a loop gives a life up to 5; it has to finish a
  loop), stage starts (`runnerstages`: stages 1-25 started directly match
  a run from stage 1 in tier, loop and speed) and the end of a game
  (`runnername`: name entry, the stick changing a letter, the score on the
  table; Back mid-game records it, a demo's doesn't) and the stage
  select (`runnerpick`: B held and A opens it, the stick's steps and
  wrap, a test run at the stage with nothing put on the table at game
  over or on Back, A at game over running it again, B or a timeout back
  to the title, A alone still a real game from stage 1), fire pits per
  stage (`runnerpitcount`: 200 runs through two loops, exactly 2 pits in
  stages 1 and 10 and 3 in stages 2 and 11, none elsewhere or closer than
  60 frames) and the star (`runnerstars`: 30s or more from each wait's
  start, none while invincible; with the wait over, none while shielded
  and one as soon as the shield's gone), a lost life (`runnerdeath`: the
  world stays drawn and stopped under the burst; the burst and the flight
  exhaust are trailed) and the distance score (`runnerscore`: a point
  every 3 frames, the same at 17ms and 50ms a frame and in a second game),
  and Night: the duck (`runnerduck`: B on the ground crouches to 12px and
  stands on release, nothing in the air or flying, A from a duck jumps),
  each obstacle alone on flat ground (`runnerobstacles`: a beam kills
  standing or jumping and passes ducked, +10; a jet kills standing when
  lit, passes standing unlit and ducked lit, +10; a dart kills standing,
  passes ducked or jumped, +20), its generation (`runnernight`: 200 runs
  through loop 2, every run meeting beams in 10 and 11, jets in 15, darts
  in 16 and something in 17, none in 12-14, its boss or the Outpost, every column
  with the runner's width of its own block either side, none on a pit's
  block, pits still exact) and the lantern (`runnerdark`: the ground at
  full, half and a quarter brightness past it, a far flame bright, the
  Outpost even), and the boss (`runnerboss`: stages 9, 18, 27 are bosses;
  its ground level with nothing on it over 100 runs; it flies in at the
  stage's start and again after a lost life; no ship, boulder, star or
  diamond comes once it's in; the runner reaches offset 60 and walks back
  a pixel a frame after; clearing it gives the stage bonus, 250 and a
  life; the autopilot gets through 20 runs each of 9, 18 and 27 with no
  lives lost, at least 4 patterns a run, every pattern met, each line's gap
  in reach). `runnerplay` has to get through Night and its boss (to stage 19) and
  prints the lives lost by stage; `runnerpits` ducks at Night;
  `runnerdiamond` checks the diamond stays away from Night;
  checks Lander's crash debris moves one step a frame and its gravity by
  level (gentle on level 1, rising every level to 20) (`lander`); its
  flight (`landerphys`: the engine 2.8x gravity on levels 1 and 20,
  spooling up over 8 steps and down over 5-6, the ship turning to the
  stick at 180 degrees a second and ignoring an off-centre raw reading,
  and the landing rule case by case: down under 1.0, sideways under 0.5,
  tilt under 15 degrees, an empty tank still landing; the pad 24px
  narrowing a pixel every two levels to 16px from level 17, and the tank
  full to level 10 then 4% less a level to 60% from level 20);
  Asteroid's Fire power-up (`fire`: A shoots nothing without it; with
  it a bolt breaks an asteroid ahead, scoring and counting as passing it
  would; the HUD line is orange for the time left and green again after
  20s, when A stops shooting), its power-ups (`powerups`: the mix drawn
  at 300, 550 and 700 points, Fire only from 500 and the extra life only
  when due, repeats only from the one redraw, 12-25s apart, and over 30000
  frames of a full demo field with one always on its way, under 0.1% of
  its on-screen frames over an asteroid), and
  `ParticleManager`'s trails (`trails`: a spark with one leaves a line
  behind it, dimmer back along it, one without doesn't, and neither
  crosses the HUD line; a trailed spark dims out instead of flashing white
  and its trail lingers a few updates after it, still dimming, while one
  without a trail still ends white).
  `DUMP_AT=1557,1617 test/build.sh games2d asteroid 1700` writes those idle
  frames as `asteroid_01557.ppm` etc. (likewise `runner_`, `lander_`).

So a clean harness run means "nothing changed unintentionally", not "this is
good to ship". Hardware still decides that.

## Layout

```
test/
├── build.sh                    # finds Jet, builds every harness + audio_test, runs
├── audio_test.cpp              # audio mixer/loader unit tests (no stubs needed)
├── games2d_harness.cpp         # Runner, Asteroid and Lander attract demos: idle + demoexit; Runner's pits, ground, game over, levitation, diamond, spikes, sounds, play, stages, names, stage select, pit counts, stars, death, score, Night, boss; particle trails
├── rollflux_harness.cpp        # Roll Flux: physics, rules, play, poses
├── resonanceflux_harness.cpp   # Resonance Flux: figure maths, matching, play, attract, options, Chords
├── mazeflux_harness.cpp        # Maze Flux: layout, doors, bullets, deaths, teleports, the bot
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
