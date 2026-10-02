# Brick Flux: design

A bat-and-ball brick breaker for the cabinet, the eighth game. The familiar
part (bat, ball, bricks, capsules) is there so anyone can pick it up; six
features make it play differently from Breakout and Arkanoid:

1. **Aimable bat**: tilt it to aim each rebound.
2. **Flux Smash**: a timed, charged shot that ploughs through a column.
3. **Polarity**: cyan and magenta bricks, a bat that switches between them.
4. **Advancing wall**: the formation creeps down; let it reach you and you
   lose a life.
5. **Living bricks**: bricks that shoot, pull, teleport and drop sparks.
6. **Bosses** every fifth level.

Menu name **Brick**, folder `src/games/BrickFlux/`, high-score key `brick`.
Header-only like the other 2D games.

---

## 1. Screen and layout

Portrait (rotation 2), 128x160.

| Area | y (px) | Notes |
|---|---|---|
| HUD | 0-9 | score left, lives as small bats right, level centre |
| Flux meter | 10-11 | 2px bar across the top; white when full, pulsing |
| Playfield | 12-159 | side walls 4px each, so the field is x 4-123 (120px) |
| Brick grid | from y 16 | 15 columns x 8px, rows 5px (7x4 brick + 1px gap) |
| Danger line | y 128 | dotted red line, brighter as the wall nears it |
| Bat | y 148 | 24px wide (36 wide-bat), 3px thick |

Up to 22 brick rows fit between the top of the grid and the danger line;
layouts use at most 12, leaving 10 rows of creep.

Walls are drawn in the bat's current polarity colour (dim), so the player
always sees which colour they are in without looking down.

## 2. Controls

Portrait directions (`hiscore::screenDirs`): left/right as named, screen up
is `joyDown`, screen down is `joyUp`.

| Input | Action |
|---|---|
| Stick left/right | Move the bat. Speed follows deflection (analogue), up to 150 px/s at full push; small pushes give fine control |
| Stick up/down | Tilt the bat face up to ±25° (up tilts the right end up, so the ball goes left; down the reverse). Eases to the target in ~0.12s; flat again when the stick returns to the middle |
| A tap | Serve the ball; release a caught ball (Catch); fire lasers (Laser) |
| A hold (meter full) | Charge the Flux Smash; release just as the ball reaches the bat |
| B tap | Swap polarity (on press, so it is instant) |
| Back hold 1s | Quit to the launcher (the cabinet's own button, handled in `main.cpp`; B is only the swap) |

Tap vs hold on A: a press counts as a hold once it has been down 0.2s.
Lasers and serves fire on the press, so a hold that becomes a charge still
fires its first laser pair; that is fine.

Diagonal pushes move and tilt at once. This is intended: it is how you line
up an aimed shot on the move. Tilt only acts on deflection beyond 0.35, so
a sloppy horizontal push doesn't tilt by accident.

## 3. Ball and bat physics

- Ball 3x3, speed starts at 90 px/s, +3% each level, capped at 170 px/s
  (loops raise the start, see §10). Slow capsule: x0.65.
- Movement scales with frame time (speeds in px/s against the measured
  frame time), and the ball moves in substeps of at most 1.5px so it never
  tunnels through a 4px brick at speed.
- **Bat reflection**: the ball reflects about the bat's *tilted* normal,
  then gets the usual contact-point English (±12° from the centre to the
  ends). The result is clamped to 20°-160° from horizontal, so no tilt can
  send the ball skimming flat or back down.
- Collision uses the bat's flat box (generous and predictable); only the
  bounce direction uses the tilt. The bat is *drawn* tilted (a thick line,
  ends up to 5px apart).
- If the ball goes 12 bounces without touching a brick or the bat, it gets
  a ±4° nudge so it can't loop forever between steel bricks.
- Lose the last ball off the bottom: lose a life.

## 4. Flux Smash

- The meter fills by 1 per brick broken (2 per brick at a chain of x4 or
  more), and gun bolts you absorb add 2 (§6). Full at 24.
- With the meter full, hold A: the bat glows white and crackles (sparks
  from its ends). Release A to open a **150ms window**: if the ball meets
  the bat within it, it smashes. Letting go up to 60ms *after* the ball
  left the bat still counts (a Good: the ball is turned onto the bat's
  aim), as players tend to release a touch late. Miss and the bat just
  plays a normal bounce, *and the meter stays full*, so a mistimed release
  costs nothing but the chance.
- **Good** (contact 50-150ms after release): the ball turns white-hot and
  goes straight up the bat's aim, piercing every brick in its path
  (steel, portals, either polarity) until it reaches the top wall, then
  carries on as normal. Score x2 for what it breaks.
- **Perfect** (contact within 50ms of release): as Good, but three columns
  wide, score x3, +500, a 4-frame screen shake and a big particle burst.
- The meter empties on any smash. Losing a life halves it.

## 5. Polarity

- Bricks are **neutral** (white/grey, anything breaks them), **cyan** or
  **magenta**.
- The bat is cyan or magenta (B swaps it). The ball takes the bat's colour
  **when it touches the bat**, so you choose the colour for the *next*
  flight, not the current one. It is drawn in its colour with a white
  centre, and the side walls show the bat's colour.
- A ball matching a brick's colour breaks it (or knocks a hit off a hard
  one). A mismatched ball bounces off with no damage and the brick flashes.
- **Chain**: each coloured brick broken in its own colour scores x the
  chain and adds 1 to it, x1 up to x8, shown beside the score. Neutral
  bricks neither add to nor break it. A mismatched bounce or a lost life
  resets it; swapping colour and touching the bat don't (an earlier draft
  had a swap mid-flight reset it, but swapping mid-flight is the point:
  it chooses the next flight's colour).
- Smashes and lasers break either colour; neither adds to the chain.
- Swapping has a 0.25s cooldown, so mashing B can't flicker the colour.

## 6. Bricks

| Brick | Look | Hits | Points | Notes |
|---|---|---|---|---|
| Neutral | by row: white, yellow, orange, green, blue, amber | 1 | 10 | never cyan or magenta, which are polarity's |
| Cyan / Magenta | colour, a dark centre mark | 1 | 20 x chain | polarity |
| Hard | grey, darker with a bevel, or dark cyan/magenta | 2-3 (coloured: 2) | 30 per hit | neutral or coloured; cracks show |
| Steel | grey, rivets | Smash only | 300 | not needed to clear a level |
| Gun | red frame, coloured eye | 2 (10 for the first) | 100 | fires a bolt down at the bat every 3-5s, in its eye's colour; either colour of ball breaks it |
| Magnet | blue, pulsing ring | 2 (10 for the first) | 50 | turns a ball within 28px towards it, up to 150°/s at the magnet |
| Portal | purple ring, in pairs | Smash only (both go) | 200 | the ball enters one and leaves the other's centre, keeping its direction; it ignores portals for 0.25s after |
| Spark | yellow, twinkling | 1 | 50 | drops a spark (see below) |

- **Gun bolts** fall at 60 px/s. One of the bat's colour is absorbed (+2
  meter, a ping). A mismatched one stuns the bat for 0.75s: it greys out
  and can't move or tilt. A gun only fires when it has a clear column below
  it, so a buried gun stays quiet until you dig it out.
- **Sparks** fall at 40 px/s, wobbling. Catch one for 250. The third spark
  caught in a level gives an extra life (once per level, up to 5).
- Steel and portals reaching the danger line don't cost a life: they
  shatter (no points). Breakable bricks, hard ones included, do.
- A level is clear when every brick except steel and portals is gone.

## 7. The advancing wall

- Every *step interval* the whole formation moves down one row (5px), with
  a deep thud, a brief shake of the bricks, and a tick mark on the meter
  bar 0.5s before so it never comes as a surprise.
- Interval: 14s on level 1, down 0.5s a level to 7s; loops take 15% more
  off (floor 4s). Boss levels have no wall.
- If a breakable brick crosses the danger line: lose a life, the bricks
  that crossed it explode, and the formation is pushed back up 3 rows;
  play restarts with a ball on the bat, as after a lost ball.
- The wall waits while a ball is on the bat to be served.
- Clearing the level scores a **headroom bonus**: 100 for each empty row
  left between the formation's lowest row reached and the danger line.

## 8. Capsules

A broken brick drops a capsule one time in 8 (never from a spark brick,
none while one is already falling). Capsules fall at 45 px/s; catch them
with the bat. Only one timed capsule runs at once (a new one replaces it);
Multiball stacks with any of them.

| Capsule | Letter | Effect |
|---|---|---|
| Wide | W | bat 36px, 15s |
| Multi | M | splits each ball into three (max 6 balls), until they're lost |
| Catch | C | the ball sticks to the bat; A releases it (tilt still aims it). 15s |
| Laser | L | A fires twin neutral shots (1 hit each, no chain), 10s |
| Slow | S | ball speed x0.65, 10s |
| Flux | F | fills the meter (rare: 1 in 6 capsules) |

## 9. Bosses

Every fifth level is a boss, with no wall (except the Flux Engine's) and
no capsules except Flux. Each boss has a **core** with an HP bar under the
meter: a ball hit does 1, a Good smash 4, a Perfect 6 (and the smash stops
at the core). After a ball's hit a core ignores balls for 0.3s (they still
bounce), so a ball trapped between a core and its shields can't pinball it
down in seconds. Beaten: 5,000 x boss number (+2,000 for no life lost), a
big explosion, then the next level. Cores and shields are off the grid,
checked by the ball like bricks; one that moves onto the ball lets it
slip out rather than trapping it. A boss only fires once the ball's served.

1. **Warden** (level 5): a core sliding along the top, with a ring of 12
   orbiting bricks in alternating colours as its shield, which grows back
   a brick every 5s. It fires single bolts, aimed, alternating colour.
   16 HP.
2. **Hive** (level 10): a fixed core that buds gun bricks every 6s (up to
   10), which creep down at 2.5px/s, firing (a bud with another under it
   holds fire). One reaching the danger line costs a life, as with the
   wall: those that crossed go, the rest move back up 3 rows. 20 HP.
3. **Twins** (level 15): two cores, one cyan and one magenta, each shielded
   by six bricks of its own colour; only a ball of a core's colour harms it
   or its shield. They swap places every 8s, up and over each other, and
   fire aimed bolts of their colour. 14 HP each.
4. **Flux Engine** (level 20): a core in a chamber above a band of steel;
   two portal pairs link the chamber with the space below the band, and
   its own advancing wall of coloured bricks sits under that. The core is
   reached through the portals, or by smashing up through the steel. It
   fires fans of three bolts, one colour a volley; below half HP the
   middle bolt is the other colour. 30 HP.

All four get 20% more HP each loop, and fire 20% more often.

## 10. Progression and lives

- 20 levels: 16 layouts and the 4 bosses. Layouts are 15x12 character
  grids in PROGMEM (`.` empty, `n` neutral, `h`/`H` hard neutral (2/3
  hits), `c`/`m` cyan/magenta, `C`/`M` hard cyan/magenta, `s` steel,
  `g`/`G` gun with a cyan/magenta eye, `o` magnet, `p` and `q` two portal
  pairs, `*` spark): ~180 bytes each, ~3KB in all, plus the Flux Engine's
  grid.
- New mechanics come in one at a time: levels 1-2 neutral and hard only;
  polarity from 3 (two big colour blocks); the wall from level 1; magnets 6; guns 7; portals 11; sparks from 3.
  Flux Smash is there from level 1 (the how-to-play shows it).
- After level 20 the game loops (up to loop 5): ball +10% start speed, wall
  15% faster, guns fire 20% more often, bosses +20% HP, each loop. The
  level counter carries on (21, 22...).
- 3 lives. Extra lives at 30,000 and every 50,000 after, plus the spark
  life (§6), up to 5.
- A level clear scores 1,000 + the headroom bonus. A level cleared without
  losing a life adds 2,000.

## 11. Look and feel

- The title is drawn, not a bitmap (a band of bricks, the name, a ball
  bouncing off a bat), which saves the 40kB a 128x160 image would take.

- Black field, a faint dot grid scrolling down very slowly as the wall
  advances (the only backdrop: cheap, and it shows the creep).
- Breaking a brick sends 4-6 particles of its colour (`ParticleManager`).
  A smash leaves a white streak that fades over 0.3s. Score popups ("+60
  x3") rise from bricks with a multiplier.
- The level intro "LEVEL n" with the layout dropping in row by row (0.8s),
  and the ball served on A.
- Boss intro (2.4s): "WARNING" flashing twice, then the boss's name. The
  game's music carries on (one track per game, as elsewhere).

## 12. Audio

Music, effects and fallbacks, each checked once with `audio.exists()` and
preloaded. All mono 44.1kHz 16-bit and short; the whole set is well under
the 1.25MB cache.

| Sound | File | Fallback |
|---|---|---|
| Music (title and play) | `flux-brick.wav` | silence |
| Ball on bat | `brick_bat.wav` | tone 880Hz 20ms |
| Brick break | `brick_break.wav` | tone, pitch rising with the chain |
| Mismatch bounce | `brick_clank.wav` | tone 220Hz 30ms |
| Hard brick hit | `brick_crack.wav` | tone 440Hz 25ms |
| Polarity swap | `brick_swap.wav` | tone, high for cyan, low for magenta |
| Meter full | `brick_ready.wav` | tone |
| Flux Smash | `brick_smash.wav` | `explosion.wav` |
| Perfect | `brick_perfect.wav` | `explosion.wav` |
| Wall about to step / steps | `brick_tick.wav`, `brick_step.wav` | tones |
| Gun fires | `brick_bolt.wav` | tone |
| Stunned | `brick_zap.wav` | tone |
| Absorbed bolt | `brick_absorb.wav` | tone 1320Hz 15ms |
| Portal | `brick_portal.wav` | tone |
| Capsule caught | `powerup.wav` (shared) | tone |
| Spark caught | `pickup.wav` (shared) | tone |
| Ball lost | `death.wav` (shared) | tone |
| Level clear | `brick_clear.wav` | tone |
| Serve, extra life | `brick_serve.wav`, `brick_extra.wav` | tones |
| Boss warn / hit / down | `brick_boss_warn.wav`, `brick_boss_hit.wav`, `brick_boss_die.wav` | tones; `star_boss_die.wav` |
| Start / game over | melodies, as Tube Flux | |

## 13. Attract cycle and demo

Title (drawn, with the best score and name) → how-to-play (three slides:
the bat, with a small one tilting; colour and the smash; the bricks, drawn,
and the wall) →
high-score table → a silent 30-40s demo → title. A starts a real game from
any of them. Nothing from the demo is scored or saved
(`audio.setSilenced(true)`).

The demo plays a random level 2-12, one time in four the Warden. The
autopilot:

- predicts where the ball will reach the bat line (wall reflections only),
  and gets there early;
- picks a target: the lowest breakable brick (nearest where the ball
  lands), or on a boss level the core (the Hive's lowest bud first; the
  Flux Engine's wall, then a portal into its chamber), and sets the tilt
  that aims the rebound at it;
- swaps to the target's colour (the Warden: that of the shield brick
  under its core);
- for a bolt of the other colour coming down on it, swaps to absorb it if
  the ball isn't due first, else dodges; catches sparks and capsules on the
  way when it can afford to;
- smashes when the meter is full, Perfect about 70% of the time.

## 14. Implementation notes

- Files: `BrickFluxGame.h` (class, phases, attract, sounds), `BrickPlay.h`
  (bat, balls, bricks, smash, capsules, wall), `BrickLiving.h` (polarity,
  guns, bolts, magnets, portals, sparks), `BrickBosses.h`, `BrickRender.h`
  (drawing), `BrickAutopilot.h`, `BrickBoard.h` (grid and creep),
  `BrickBall.h` (types and bounce maths), `BrickLevels.h` (layouts as
  text), `BrickConfig.h` (all tuning, in its own header like the 3D games'
  rather than in `ArcadeConfig.h`).
- State is all in the game object: grid 15x22 bytes, up to 6 balls, 8
  bolts, 4 capsules, 4 sparks, 16 laser shots: well under 2kB.
- Draw: bricks as filled rects into the canvas, only the 15x22 grid walked.
  Nothing per-pixel, so frame cost is well inside the budget; the display
  push (~8ms) dominates.
- Flash: no large assets (the title is drawn); still check `pio run`'s
  size report.
- Registry: `{ "Brick", makeGame<BrickFluxGame>, "brick" }` and `brick` in
  `hiscore::GAMES`. The launcher already scrolls past six.

## 15. Host harness

`test/brickflux_harness.cpp` (`test/build.sh brick <scenario>`), with the
fake clock and seeded RNG: `play` (real games by the autopilot: no ball
inside a brick or out of the field, the score never drops), `wall`,
`smash` (release timing), `tunnel` (fast balls at a steel row never get
through), `polarity`, `living` (guns, bolts, magnets, portals, sparks),
`levels` (every regular level cleared by the autopilot, lives pinned),
`boss` (each boss beaten likewise), `idle` and `demoexit`. `DUMP_AT`
writes frames of `play`, `levels` and `boss`. test/README.md has the
details.

## Built

Built in two stages, as agreed: the core (bat and tilt, smash, wall,
capsules, layouts 1-4, attract, high scores), then polarity, the living
bricks, the four bosses and layouts 5-20 (level 3 and 4's layouts gained
their colours then).

## Needs the board

Stage 1 checked on the cabinet: frame rate, bat feel and play are good.
Still to judge there: whether cyan and magenta read apart at a glance on
the panel, bolt speed and the stun's length, how hard each boss is, and the
new levels' pace (the autopilot, lives pinned, takes 1.5-5 minutes a level
and 1-2 minutes a boss).
