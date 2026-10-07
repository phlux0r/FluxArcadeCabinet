# Runner (Platform Flux): new stages

A plan for making Runner hold up for long play. The game itself is in
README.md; the review that came before this is in docs/HANDOVER.md. The
autopilot already reaches stage 50, so length isn't the problem: every
loop is the same eight stages, only faster and recoloured. This adds
variety, a world at a time.

Owner's decisions (2026-10): a world per loop; B ducks, under low
obstacles only (not ships, which fly high): beams, flame jets and darts;
a boss stretch to survive at the end of every loop, the first included;
Night first, then the boss.

---

## 1. Structure: a world per loop

| Loop | Stages | World | New |
|---|---|---|---|
| 1 | 1-8 | Outpost (today's game) | pits, platforms, spikes, boulders, the ship |
| 2 | 9-16 | Night | B ducks: low beams, flame jets, darts |
| 3 | 17-24 | Ruins | crumbling slabs, springs to a high route with gem chains |
| 4 | 25-32 | Storm | wind gusts and conveyor ground pushing the runner |
| 5+ | 33- | the worlds again, faster | |

Each world keeps the old hazards, mixed with its own, so it's harder,
not just different. Each loop ends with the boss stretch (section 4) and
its extra life. Each world has its own backdrop colours and its
name in the stage banner ("NIGHT 3", say) and on the stage select.

Order of work: Night, then the boss, then Ruins, then Storm. Each phase
merges on its own and is played on the board before the next starts.

---

## 2. Night (loop 2): duck (built)

As built (the owner confirmed: beams, flame jets and darts; nothing for B
in the air; the lantern's reach; ropes dropped).

### The duck (B)

- **B held on the ground: the runner ducks**: two 18x12 frames,
  `assets/duck1.png`/`duck2.png`, made from the run frames by
  `tools/runner_duck_sprite.py` (head leant forward, body and legs taken a
  row in two or three; `--from-png` converts hand-drawn ones instead).
  Its box drops from 20px to 12px tall, feet where they were
  (`PlayerRunner::hitTop`); it keeps running and the stick still works.
- In the air or flying, B does nothing; A from a duck stands and jumps;
  letting B go stands it up. Running off an edge ends a duck.
- The how-to screen has "[A] JUMP [B] DUCK" and a Night line.

### The obstacles (`PlatformManager`, on ground blocks like the spikes)

All hit only a standing runner from 14px above its feet up; a ducked one
(12px) passes under. Sizes in ArcadeConfig's NIGHT_ block.

1. **Beams**: a lattice gantry from under the HUD to 14px above the
   ground, a lamp under it. Nothing gets over it: duck.
2. **Flame jets**: a shorter housing, to 22px above the ground, which a
   standing runner passes under; its flame (off 1.3s, sputtering 0.4s, lit
   0.9s, like the spikes) fills the gap down to 14px while lit.
3. **Darts**: a launcher on the ground glints once the runner is 40-115px
   behind it, and after 0.5s fires a dart at 16px above the ground, 2.2px
   a frame faster than the scroll. Duck it, or jump it.

Placement, each rule found by the harness bot dying to its absence:

- By the stage the runner will reach the block in (`arrivalAt`, as the
  pits), clear of the stage's last 40 frames, so it's met in its own stage.
- A column's block is wide enough that the runner is wholly on it while
  under it, and level with the block before: off a step down the runner
  stays on the higher block till it's wholly past it, then falls, and it
  can't duck in the air.
- In the pit stages (9-10) a beam goes in each gap round the pits
  (before the first, then halfway between each pit's mark and the next),
  giving way when the next pit is due; never on a pit's block, and no
  pit on the block after one (a duck then a jump at once). Pits stay
  exact.
- 110px between obstacles (60 in the pit stages, where the pits part
  them), 72px from spikes; a launcher wants level ground and no spike for
  130px behind it (the dart's path); the dart stage (15) has no spikes and
  Night's dart stages keep the ground level.
- No boulder is sent while an obstacle is on screen ahead or a dart is in
  flight (a boulder wants a jump, these a duck).
- No diamond at Night, nor in the stage before it (a flying runner can't
  duck; ten seconds of flight would carry over).

### Stages 9-16

| Stage | Terrain (as the Outpost) | Night's addition |
|---|---|---|
| 9 | ground, fire pits (2) | beams round the pits |
| 10 | ground, fire pits (3), stairs | beams round the pits |
| 11 | floating platforms | the dark only |
| 12 | moving platforms | the dark only |
| 13 | platforms + the ship | the dark only |
| 14 | stairs, spikes | flame jets |
| 15 | boulders (no spikes) | darts |
| 16 | all + ships | all three; then (next phase) the boss |

The harness bot, over 30 runs of stages 9-16, lost no lives to Night's
obstacles (its deaths there were rocks and boulders at Night's speed).

### The look

Night's own backdrop (deep blue-black hills, brighter stars, a moon),
slate ground and pale spikes. Past the runner's lantern (100px ahead) the
ground, gantries and spikes are drawn at half brightness for 30px, then a
quarter; flames, lamps, the launcher's eye and darts stay bright.

### Sounds (optional)

`runner_duck.wav` (nothing without it), `runner_dart.wav` (a high tone),
`runner_jet.wav` (a low tone, when a jet on screen lights).

---

## 3. Ruins (loop 3) and Storm (loop 4), in outline

Detailed when their turn comes.

- **Ruins:** slabs that crumble a beat after you land (shake, then fall);
  springs that throw the runner to a high route of slabs with gem chains
  (points, a combo for collecting a chain unbroken) over the safe low
  route. Risk for score.
- **Storm:** gusts (telegraphed by blown particles and a sound) push the
  runner's position forward or back for a second; conveyor ground does
  the same while you stand on it. The stick fights them. Rain streaks in
  the backdrop.

---

## 4. The boss stretch (end of every loop, the first included)

After the loop's last stage, before the extra life: a big ship crosses
above for a set distance (about 20s), dropping rocks in patterns (a
line, a pair with a gap to stand in, a tracking drop that lands where the
runner was). The ground is plain so the rocks are the test. Survive it to
finish the loop: the banner, the extra life, the next world. A life lost
restarts the stretch, like a stage. Each world's boss adds its world's
twist (Night: darts from the ship; Ruins: rocks crumble the ground;
Storm: gusts while it drops). The first loop's is the plain one, so a
first-time player meets it.

---

## 5. Every phase includes

- The demo autopilot taught the new rules (the duck as a third move
  beside running and jumping, in its prediction), so the demo keeps
  working and `runnerplay` keeps going through the new loop.
- The stage select naming the world and the stage's hazards.
- Harness scenarios: the duck's hitbox; each obstacle passable by its
  move and fatal without it; counts per stage; combinations fair (the
  bot clears them); the boss's patterns survivable.
- README and test/README up to date; HANDOVER says where it stands.
- `pio run`'s size line checked (a sprite frame is ~720 bytes; flash is
  at 38.8%).
