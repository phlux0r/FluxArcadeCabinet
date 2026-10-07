# Runner (Platform Flux): new stages

A plan for making Runner hold up for long play. The game itself is in
README.md; the review that came before this is in docs/HANDOVER.md. The
autopilot already reaches stage 50, so length isn't the problem: every
loop is the same eight stages, only faster and recoloured. This adds
variety, a world at a time.

Owner's decisions (2026-10): a world per loop; B ducks, under low
obstacles only (not ships, which fly high); a boss stretch to survive at
the end of each loop; Night first.

---

## 1. Structure: a world per loop

| Loop | Stages | World | New |
|---|---|---|---|
| 1 | 1-8 | Outpost (today's game) | pits, platforms, spikes, boulders, the ship |
| 2 | 9-16 | Night | B ducks: low beams, swinging burning ropes, darts |
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

## 2. Night (loop 2): duck

### The duck (B)

- **B held on the ground: the runner ducks** (crouched pose, a new 18x12
  sprite frame squashed from the run frames by a tools/ script). Its
  hitbox drops from 20px to 12px tall; it keeps running at the same speed
  and the stick still shifts it forward and back.
- **In the air B does nothing**, and **A jumps from a duck** (it stands
  up and jumps). Letting B go stands it up at once.
- Available in every world, so it's learnt once; only Night and later
  worlds need it.
- How-to screen: a "[BTN B] DUCK" line; the Outpost's hazards list stays.

### Low obstacles

All of them hit only the top of a standing runner (from 13px above its
feet up), so a duck passes under; none can be jumped over.

1. **Low beams.** A girder hanging from a gantry, its underside 13px
   above the ground, with a lamp. The simple one: see it, duck, pass.
   On flat ground in its first stage; later on stairs and slabs.
2. **Burning ropes.** A rope hanging from above, swinging slowly, its
   end on fire, sweeping through head height on each swing. Duck as it
   swings over, or time the run to pass while it's away. Embers drop
   from it (particles, harmless).
3. **Darts.** A launcher ahead glints, then fires a dart along at head
   height towards the runner (faster than the scroll). Duck it.
   Telegraphed half a second before it fires.

Combinations come later in the loop: a beam just before a pit (duck,
then jump), a dart while a boulder rolls in (the jump and the duck both
needed in turn). Never two needs at once that contradict (a dart at
head height over a spike); the generator keeps them apart, like it keeps
spikes apart now.

### Stages 9-16

| Stage | Terrain (as now) | Night's addition |
|---|---|---|
| 9 | ground, fire pits (2) | low beams, spaced, flat ground only |
| 10 | ground, fire pits (3), stairs | beams, some on stairs |
| 11 | floating platforms | beams over slabs |
| 12 | moving platforms | burning ropes |
| 13 | platforms + the ship | darts |
| 14 | stairs, spikes | beams and darts |
| 15 | spikes, boulders | ropes |
| 16 | all + ships | all three; then the boss |

### The look

Darker backdrop colours (deep blue-black hills, more stars, a moon).
Hazards and ground far ahead are drawn dim and brighten as they come
within about 100px of the runner: a lantern's reach, cheap (a colour per
object by its distance, not per-pixel lighting). The flames and lamps
stay bright, so they read first.

### Sounds (optional, tone fallbacks)

`runner_duck.wav` (a short scuff), `runner_dart.wav` (the launch),
`runner_rope.wav` (a crackle when a rope swings close); the hits reuse
the rock/boulder hit sounds.

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

## 4. The boss stretch (end of every loop)

After the loop's last stage, before the extra life: a big ship crosses
above for a set distance (about 20s), dropping rocks in patterns (a
line, a pair with a gap to stand in, a tracking drop that lands where the
runner was). The ground is plain so the rocks are the test. Survive it to
finish the loop: the banner, the extra life, the next world. A life lost
restarts the stretch, like a stage. Each world's boss adds its world's
twist (Night: darts from the ship; Ruins: rocks crumble the ground;
Storm: gusts while it drops).

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
