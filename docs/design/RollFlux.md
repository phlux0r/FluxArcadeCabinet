# Roll Flux: design

A 3D marble game for the cabinet, the ninth game and the fourth rendered
with Jet. Tilt the course to roll a ball from the start to the goal gate
before the clock runs out, over narrow paths, slopes and gaps, with a
void below. The familiar part (Marble Madness, Super Monkey Ball) is there
so anyone can pick it up; five features make it its own and tie it to the
rest of the cabinet:

1. **Flux Dash**: charge and release A for a burst of speed, the same
   hold-and-release feel as Brick Flux's smash. It breaks crystal walls,
   clears gaps and beats bosses.
2. **Polarity**: the ball is cyan or magenta (tap B). Colour gates only
   let your colour through, and **phase bridges** are only solid in their
   colour: in the other one, you fall straight through.
3. **Moving courses**: tilting platforms, rotating bridges, conveyors,
   bumpers, lifts.
4. **Gems and routes**: gems add time and fill the dash meter; the risky
   line has more of them, so there's always a fast route and a safe one.
5. **Guardians**: every fifth course is an arena with a boss to beat with
   the dash.

Menu name **Roll**, folder `src/games/RollFlux/`, high-score key `roll`.
Landscape (rotation 1), 160x128. Split into .cpp files like the other 3D
games.

---

## 0. The risk, and stage 0

Jet fills pixels expensively in this build (README, "3D Rendering": no
half-width buffers, so every pixel takes the general per-pixel path). A
course floor covers much of the screen, the way Tube's tunnel did before
it was drawn directly. So the first piece of work is a **renderer
prototype**, measured on the board before anything else is built:

- one course, the ball and the chase camera, no gameplay beyond rolling;
- the harness's `profile` mode reporting render cost and triangle counts
  per frame, and `-DSHOW_FPS` on the board;
- two floor renderers compared: Jet drawing the floor tiles, and the floor
  drawn directly with Jet's own projection (flat-coloured quads in spans,
  as Tube's `drawTunnel()` does), Jet then drawing only the ball and the
  things on the course.

The target is Tube's or Tank's frame time on the board (about 30fps). If
neither renderer gets there, the fallbacks, in order: a higher, steeper
camera (less floor on screen, more sky); narrower courses; coarser tiles.
The design below doesn't change with the choice.

### Stage 0: built, and what the host shows

`src/games/RollFlux/` (menu entry **Roll**): one course (ramps, a gap, a
block, a sideways ramp to the goal), the ball with its physics (rolling,
ramps, steps as walls, falls), the chase camera at three heights (A), and
both floor renderers (B), with the render time and Jet's triangle count on
screen. `test/rollflux_harness.cpp` checks the physics, profiles every
renderer and camera with a scripted driver, and writes frames of both
renderers to compare.

Host cost per frame (`test/build.sh roll profile`, -O2, same build as
Tube's `profile`):

| Floor | Camera LOW / MID / HIGH | Jet's triangles |
|---|---|---|
| DIRECT | ~46 / 49 / 46 µs | ~20 (the ball) |
| JET | ~124 / 127 / 114 µs | ~140-230 |
| Tube Flux, for scale (30fps on the board) | ~55-63 µs | ~30 |

So DIRECT costs about what Tube does, and JET about twice that. The frames
match (the course and ball line up in both); JET also shows ragged edges
and loses triangles crossing the near plane close to the camera, which
DIRECT clips properly. Unless the board says otherwise, DIRECT is the floor
renderer, and the camera height is free to choose for play rather than
cost. The board's numbers (the overlay, and `-DSHOW_FPS`) decide.

### On the board, and what changed for stage 1

DIRECT held 55-57fps on the board; JET bottomed out at 24-25fps
(playable, but with half the headroom). DIRECT also looked smoother, and
the middle camera height played best, so both are fixed (`CAMERA_BACK`,
`CAMERA_UP`). The rolling and the camera's turn rate felt right and are
unchanged.

With the floor drawn directly, Jet's Scene drew only the ball, always over
the floor, so a ball falling behind an edge stayed in front of it. Stage 1
drops the Scene: the ball (a striped low-poly sphere, turned as it rolls),
its shadow and the gems are drawn in the same pass as the floor, and a
floor piece nearer than one of them goes after it only if it could hide it
(its top's plane, carried on to under the item, is above the item's
bottom; or it's a rail standing higher). Jet's Camera still does the maths
(projection and rotation tables), so `initializeTrigTables()` is called
once, which a Scene would otherwise do. On the host this costs a little
more than stage 0's DIRECT (more on screen), all of it pixel filling.

### Stage 1, first half: built

Rails (`R`: floor with rails on its void edges), ice (`i`), boost pads
(`^ v > <`), gems (`*`), checkpoints (`C`) and the chequered goal; the
time limit per course, gems' time, falls back to the last checkpoint with
its time, time up (the course again), 4 balls and an extra every 100 gems,
the goal's tally, the course loop with less time each round, game over;
the HUD; the course leaning with the stick; the ball turning as it rolls,
striped, with a shadow; the starfield. Still to come in stage 1: the
other courses of worlds 1-2, the Flux Dash, the attract cycle and demo,
high scores and name entry, and the sounds (tones for now).

On the board: over 55fps; rails and the boost pad good, 60s about right.
Ice felt no different, so it now grips far less (28% of the stick's push,
8% of the friction). Course 1's sideways ramp to the goal was two ramp
cells in a row, a sawtooth (up, a drop, up again) that trapped the ball;
it's one ramp now, and the harness checks every course for it. The camera
no longer swings round to face the ball rolling back down a slope, which
had swapped the stick's sense round under the player.

## 1. Screen and camera

- A chase camera behind and above the ball, looking down at about 35
  degrees, so the path ahead and the drops either side both show. It
  follows the ball's position closely and its direction of travel slowly
  (yaw eases over ~0.6s), so the course turns under you without the view
  swinging with every wobble.
- Controls are **camera-relative**: stick up always rolls the ball away
  from the camera.
- The course tilts visually with the stick (up to ~8 degrees of camera
  roll and pitch), so the tilt reads on screen.
- Backdrop: a sky gradient and a starfield over the void, drawn directly
  (Star Flux's backdrop code), so the void below the course is cheap.
- HUD along the top: time (big, counting down, red under 10s), gems,
  score, lives; the dash meter as a bar along the bottom edge; the ball's
  colour as the bar's colour.

## 2. Controls

Landscape directions (`hiscore::screenDirs`): screen up is `joyLeft`,
screen right is `joyDown`. As analogue values: up = `-joyX`, right =
`+joyY`.

| Input | Action |
|---|---|
| Stick | Tilt the course: the ball accelerates that way (camera-relative), more for a bigger push |
| A hold | Charge the Flux Dash (meter at least one bar): the ball glows, slows slightly |
| A release | Dash: a burst of speed in the stick's direction (or the ball's own, with the stick centred) |
| B tap | Swap the ball's colour, cyan/magenta (on the press, 0.25s cooldown) |
| Back hold 1s | Quit to the launcher (main.cpp's, as for every game) |

## 3. The course

Courses are grids of cells (1 unit square), up to 24x48, each with a
height (0-7, in half-unit steps) and a kind:

| Kind | Look | Behaviour |
|---|---|---|
| Floor | checkerboard, two shades of the world's colour | normal friction |
| Ramp (N/S/E/W) | as floor, shaded by slope | rises one height step across the cell |
| Void | nothing | the ball falls |
| Edge rail | low wall along a cell's side | bounces the ball (damped) |
| Ice | pale blue | a quarter of the friction |
| Sticky | dark green | twice the friction, dash halved |
| Boost pad | arrows | a push along its arrow |
| Bumper | red post | knocks the ball back hard |
| Crystal wall | translucent blocks | solid; a dash smashes it (+points) |
| Colour gate | cyan/magenta bars across a path | only the ball of its colour passes |
| Phase bridge | cyan/magenta tiles | solid only to its colour: the other falls through |
| Conveyor | moving stripes | carries the ball along |
| Gem | spinning diamond above a cell | +2s, +100, +1 dash meter step |
| Checkpoint | ring | a fall respawns here |
| Goal gate | arch | finish |

Moving parts are their own objects: platforms that slide or rotate on a
path, bridges that turn, lifts between heights, sweepers.

Courses are text in PROGMEM like Brick's layouts: two characters a cell
(height digit and kind), so a 24x48 course is ~2.3kB, 20 courses under
50kB; moving parts as a short list per course.

## 4. Ball physics

- A ball of radius 0.3 units on the grid, its height taken from the floor
  under it. Position, velocity on the ground plane, and a vertical speed
  only while falling or going over a lip.
- The stick gives an acceleration (up to ~6 units/s²); slopes add gravity
  along their fall line; friction per kind; a speed cap (~7 units/s, more
  in a dash).
- Moves in substeps of at most 0.1 units, so it can't pass through a rail
  or a gate at any speed or frame time (Brick's lesson).
- Off the course (over void, or a phase bridge of the other colour), the
  ball falls; below the course by 3 units, it's a fall: a life, and the
  ball respawns at the last checkpoint with the time it had there.
- A ball rolling off a higher cell onto a lower one drops (small
  bounce); one rolling at a higher cell's side bounces off it.
- Movement scales with real frame time (speeds per second, as Brick).

## 5. Flux Dash

- The meter has 3 steps, each filled by 5 gems (or one gem-burst pickup).
- Hold A to charge (from one step): 0.4s to full charge, ball glowing,
  speed held to 60%. Release: the ball's speed jumps to ~2.2x the cap in
  the stick's direction (or its own, stick centred), decaying over 0.6s;
  one step used.
- While dashing it smashes crystal walls, knocks bumpers' targets and
  damages a guardian's weak points; it also carries the ball over gaps of
  up to 2 cells at full speed.
- No timing window here (unlike Brick's smash): the skill is choosing
  where and when to spend it.

## 6. Polarity

- The ball is cyan or magenta, shown on the ball, the dash bar and a rim
  on the HUD. Tap B to swap (0.25s cooldown).
- **Colour gates**: bars across a path, passable only in their colour;
  the other colour bounces off them.
- **Phase bridges**: tiles solid only in their colour. Roll onto a cyan
  bridge as magenta and you drop through. Swapping mid-bridge drops you;
  swapping mid-air over one lands you on it. Courses use this for
  timing puzzles: alternating bridge segments with a swap between each.
- Introduced on course 6, after the basics.

## 7. Time, score and lives

- Each course has a time limit (45-90s). Gems add 2s.
- Score per course: 100 per gem, 50 per crystal smashed, time left x 100
  at the goal, +2,000 for a course with no fall, +5,000 for every gem on
  it.
- 4 lives; a fall costs one. An extra life every 100 gems (up to 9). Time
  running out costs a life and restarts the course.
- High-score table and name entry as every game; `onQuit()` records a game
  in progress.

## 8. Courses and worlds

20 courses in 4 worlds of 5, the fifth of each a guardian:

| World | Look | Introduces |
|---|---|---|
| 1. Orbit Garden | green and white floor, blue sky | floor, ramps, rails, gems, gaps, checkpoints |
| 2. Ice Relay | pale blue, night sky | ice, conveyors, boost pads, moving platforms, dash |
| 3. Prism Works | magenta/cyan, purple dusk | colour gates, phase bridges, crystal walls, bumpers |
| 4. Flux Core | dark metal and orange, starfield | rotating bridges, lifts, sweepers, everything together |

After course 20 the loop restarts: less time per course (-15% a loop,
to a floor), faster moving parts, up to the fifth loop.

## 9. Guardians

Arenas: a round or square platform over the void, with the guardian in
the middle. Each has three weak points to hit with a dash (lit when
vulnerable); plain bumps do nothing. A health bar along the top.

1. **The Sweeper** (course 5): a spinning bar sweeps the arena; its three
   nodes glow on the bar's ends and centre in turn. Dash into the lit one.
2. **The Piston** (course 10): pistons rise under parts of the floor to
   throw the ball off; the core drops after each slam and glows: dash in.
3. **The Prism** (course 15): a core shielded by cyan and magenta panels
   that turn; only your colour's panels give way. Swap to match the open
   side and dash through.
4. **The Gyre** (course 20): the arena tilts itself, fighting your
   stick, with gaps opening in rings; three nodes ride the rim.

## 10. Attract cycle and demo

Title (a generated image like Tube's and Star's), how-to-play (two
slides: rolling and the dash; colour gates and bridges), the high-score
table, then a silent 30-40s demo on a random course of 1-8 (sometimes the
Sweeper). The autopilot:

- plans a route on the course grid (breadth-first over cells it can roll
  on, through gems where they're close, as Lander's demo plans through
  rocks), and follows it with a tilt that steers for a point a little
  ahead, slowing for turns and edges;
- swaps colour before gates and bridges, dashes on straights and at
  crystal walls;
- sometimes takes the gem route and falls, so a demo shows a fall too.

## 11. Audio

| Sound | File | Fallback |
|---|---|---|
| Music | `flux-roll.wav` | none |
| Rolling | (a low tone that follows speed, from the synth) | |
| Bump / rail | `roll_bump.wav` | tone |
| Gem | `pickup.wav` (shared) | tone |
| Dash charge / release | `roll_charge.wav`, `roll_dash.wav` | tones |
| Crystal smash | `roll_crystal.wav` | `explosion.wav` |
| Colour swap | `roll_swap.wav` | tone, high cyan, low magenta |
| Gate bounce | `roll_gate.wav` | tone |
| Fall | `roll_fall.wav` | falling tone |
| Checkpoint | `roll_check.wav` | tone |
| Goal | `roll_goal.wav` | melody |
| Time warning (last 10s) | (ticks) | tone |
| Guardian warn / hit / down | `roll_boss_warn.wav`, `roll_boss_hit.wav`, `star_boss_die.wav` (shared) | tones |

Each checked once with `audio.exists()` and preloaded; mono 44.1kHz
16-bit, short; the set well inside the 1.25MB cache.

## 12. Implementation notes

- Files: `RollFluxGame.h` (class), `RollFluxConfig.h` (all tuning),
  `RollFluxGame.cpp` (phases, update), `RollFluxCourse.cpp` (loading a
  course, its meshes, moving parts), `RollFluxPhysics.cpp` (the ball),
  `RollFluxScene.cpp` (camera, backdrop, the floor renderer chosen in
  stage 0, the ball), `RollFluxGuardians.cpp`, `RollFluxDemo.cpp`
  (autopilot, attract), `RollFluxHud.cpp`, `RollCourses.h` (course text).
- Meshes: the course is built into chunks of 8x8 cells when a course
  loads, top faces and only the exposed sides; chunks out of the camera's
  reach are disabled each frame, keeping Jet's queue to a few hundred
  triangles (Tank's ground is 242; the queue costs ~100 bytes a triangle).
  The scene is built on launch and freed in `onExit()`, as Tank's is.
- The ball: a `Sprite2D` (pre-rendered rolling frames in two colours, as
  Tube's ship) or a low-poly sphere, whichever stage 0 shows is cheaper.
- RAM: the game object holds the course grid (24x48 cells, ~2.3kB) and
  moving parts; the Jet scene sits on the heap.
- Flash: courses ~50kB, title image 40kB, ball sprite frames ~10kB.

## 13. Host harness

`test/rollflux_harness.cpp`, built with Jet like the other 3D games:

- `profile`: render cost and triangle counts per course and camera angle
  (stage 0's measuring tool);
- `play` and `god`: the autopilot on every course; `god` (lives pinned)
  must reach every goal within the time;
- `physics`: the ball never passes through a rail or gate at top speed and
  the longest frame; falls detected; phase bridges solid to one colour
  only; the dash crosses a 2-cell gap and not a 3-cell one;
- `menus`, `idle`, `demoexit`: as the other 3D games;
- `pose`: fixed views written as frames, to check the camera, tilt and
  course look by eye.

## 14. Stages

0. **Renderer prototype** (above): one course, rolling, the chase camera,
   both floor renderers, profiled on the host and measured on the board.
1. **Core**: physics, falls and checkpoints, gems and time, the dash,
   rails, ramps, ice, boost pads, worlds 1-2's courses, HUD, attract,
   high scores.
2. **Second pass**: polarity (gates, bridges), crystal walls, bumpers,
   conveyors, moving parts, worlds 3-4, the four guardians.

## Needs the board

Stage 0's frame rate decides the floor renderer and camera. After that:
how the tilt feels (acceleration, friction), the camera's follow speed,
whether the void and edges read clearly on the 1.8" panel, and course
difficulty.
