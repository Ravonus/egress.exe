# EGRESS.EXE redesign

Status: **proposal, waiting for approval.** Nothing in this document is built yet, apart from the proofs marked
**PROOF** (isolated modules and prototypes with their own tests, not wired into the game). The M2 build on `main`
stays as it is until this is signed off.

The owner's verdict on M2 was: "none of it's generative, nothing makes sense, no puzzles... the tile system sucks."
The redesign answers that with one idea. EGRESS is its own game: a sibling of CRUCIBLE set in **the shared world**
(a placeholder; the world isn't named yet, and the game never names it). It shares CRUCIBLE's family look and its
best ideas (combining, the face generator, the matrices), but it has its own tiles, UI, HUD and recipe set.
- you carry elements and objects;
- you combine them on a portable bench with EGRESS's own recipe set (inspired by CRUCIBLE's elements and traits,
  curated for rooms and fights);
- the results act on the room and on enemies through one shared reaction table;
- the world assembles in front of you and falls apart behind you;
- when you die, your heir (your child) carries on.

Every room, mob, weapon, heir and stage is composed from pieces by the seed, and checked by a solver or a simulator
before you ever see it.

**What is reused, and what is EGRESS's own:**

| Reused code (MIT, same owner) | EGRESS's own |
| --- | --- |
| the face generator (`src/avatar/`), the KEEL boot, the music driver approach, the save patterns (two CRC records), glitch/overlay techniques | tiles, UI frames and layouts, HUD, font (M2's), objects, mobs, recipe set, palettes, title |

CRUCIBLE's UI, tiles and colours are a style reference only: pastel, synthwave, nostalgic, backrooms, KEEL dithered
pixel art, glitch.

**Mocks.** All at 4x, 160x144 native. They are drawn with EGRESS's own mock kit (`tools/mock/egkit.py`: its own
dithered tiles, objects, panels, HUD and the M2 font), with faces from the shared face generator.

| File | Shows |
| --- | --- |
| `docs/shots/mock_room.png` | a puzzle room mid-assembly (cards dropping in, landing outlines, glitch tears), with 3 visible treasures and 1 hinted |
| `docs/shots/mock_bench.png` | the portable bench as an overlay on the dimmed room: FIRE + WATER = STEAM, trait chips, the pocket |
| `docs/shots/mock_combat.png` | combat with the far row collapsing, a FIRE-mod hit on a WET target making STEAM (blind) |
| `docs/shots/mock_reaction.gif` | that reaction combo animated: hitstop, steam particles, a parry reflecting an orb, the collapse wave |
| `docs/shots/mock_heir.png` | the heir pick: three human siblings with a family resemblance, with their parent shown |
| `docs/shots/mock_holdout.png` | a holdout stage: the egress assembles while waves come in from the void |
| `docs/shots/mock_cond.png` | a conditional kill: shielded until WET, then SHOCKED (the means are in the room) |
| `docs/shots/mock_mobs.png` | generated mob looks: 8 of one family, tag tells, elites of every size, an animation strip |
| `docs/shots/mobs/family_*.png` | 64 generated variants per family (readability and content review) |
| `docs/shots/mock_boss.png` | a boss concept in its arena, tearing tiles out of a half-flooded floor |
| `docs/shots/mock_title.png` | the PRESS START title around a real raycaster frame (section 15) |
| `docs/shots/anim.png` | animation states: 8 directions (5 drawn plus 3 mirrored), walk, idle, combo, charge, roll, blink, hurt, death |
| `docs/shots/characters.png` | before/after: M2, CRUCIBLE as style reference, EGRESS now |
| `proto/genetics/out/tree_*.png`, `drift.png` | 30 generations of a family: faces and bred-for abilities |
| `docs/shots/boot_compare.png` | the ported KEEL boot, frame by frame against CRUCIBLE's |

These are mock-ups: hand-placed compositions of the kit's pieces. Mobs on the scene mocks are 2:1 reductions of the
48 px faces; the generated mob looks are in `mock_mobs.png`.

---

## 1. Look: EGRESS's own tiles in the family style

The look is the family's (pastel, synthwave, nostalgic, backrooms, KEEL dithered pixel art with glitch). The pieces
are EGRESS's own:

| Layer | EGRESS design |
| --- | --- |
| **Void** (everything that is not floor) | a deep indigo gradient, ordered-dithered, with a faint synthwave grid far below and sparse stars. Whispers drift through it ("NOCLIP", "WAKE UP", "YOU WERE HERE") |
| **Floor slabs** | 16x16 slabs floating over the void: a 1 px highlight/shadow bevel, a Bayer-dithered diagonal light, seeded scuffs (carpet flecks, tile chips), and a dithered underside where a slab meets the void. That is the Bastion read: floating ground |
| **Raised blocks / walls** | a slab top over a front face of vent-grille stripes |
| **Water / hazards** | dithered water with palette-cycled wave highlights; lava, ice and foam the same way |
| **Objects** | EGRESS's own 16x16 pieces: the burner (FIRE), the water-cooler bottle (WATER), the crate, the mug, the CRT, the battery (SPARK), the sprout (SEED)... in the dithered style with a 1 px dark outline |
| **UI** | rounded, dithered panels with a cyan rim over a pink underline (the synthwave edge); dashed item sockets; trait chips; the M2 font with its drop shadow |
| **HUD** | one slim bar at the bottom: hearts, pocket icons, a message slot, tokens |
| **People** | the shared face generator (ported code) for portraits; the player sprite derived from the same genome |
| **Glitch and light** | scanline tears (SCX per row band, the technique CRUCIBLE uses), palette-cycled glows (door hum, CRT flicker, water), sparkles. No fake UI lighting: light only comes from things in the world |

**Biome palettes** (each biome gets one floor family, one void tint, one accent and its own ambient palette
animation):

| Biome | Floor slab | Void | Accent | Ambient animation |
| --- | --- | --- | --- | --- |
| Office | peach | indigo, ceiling-tile grid | fluorescent yellow | light buzz flicker (2-frame palette swap, random) |
| Vents | teal | dark, duct ribs | cyan | fan strobe |
| Flooded atrium | blue | haze | white | water shimmer (3-step cycle) |
| Server farm | teal/navy | glyph rain | green | LED chase |
| Mall food court | pink | sunset gradient | orange | neon sign blink |
| Dream garden | mint | pastel sky | lilac | slow breathing glow |
| Rooftop sky | lilac | stars | cyan | star twinkle |
| Glitch core | inverted lilac | everything torn | magenta | constant tear bands |

CGB budget: 8 BG palettes. 0 floor, 1 floor-lit, 2 walls, 3 void, 4 water/hazard, 5 HUD/text, 6 objects, 7 FX
(flash, glitch tint). Sprites: 0 player, 1–3 mobs, 4 held object/overlay, 5 particles, 6 pieces in flight, 7 elite
glow. Tiles: VRAM bank 1 BG (as now), sprites in bank 0, 8x16 mode.

---

## 2. The world falls into place and falls apart

Rooms are not drawn in one go. They assemble around you and come apart behind you, Bastion-style. This is both feel
and mechanics.

**Assembly.** When a room loads, the void is up and only the cell under the player exists. Cards arrive in a wave
ordered by distance from the player (or from the door you came through). Each card picks one entrance from the data:
- dropping from above (the default);
- rising from below;
- sliding in from the nearest edge;
- glitching in: a 2-frame torn flash, then solid.

It overshoots by 2 px, settles with a 1-frame squash, and commits to the BG. A dotted landing outline in the void
shows where it will land. You can walk onto a card the frame it commits. Rooms can also assemble **conditionally**: a
bridge whose cards only arrive once a recipe is made.

**Collapse.** Cells condemned by the story, cells behind you on a chase stage, cells a boss tears out, and crumble
floor after you leave it:
1. crack (one frame of crack tiles);
2. drop: the card splits into four 8x8 quarters that fall and darken through 2 palette steps;
3. void.

Anything standing on a cell when it drops falls: the player loses a heart and respawns at the last safe cell. Mobs
and pushed blocks are gone.

**Tech.**
- Settled cells are BG tiles. Cards in flight are sprites (2 sprites of 8x16 per card, 4 per falling quarter-set),
  and they commit to the BG on landing.
- The per-tile animation is data:
  `{entrance, delay, speed, overshoot, glitch}` for assembly and `{crack_frames, fall_speed, darken_steps}` for
  collapse. The generator emits the order, so assembly is part of the room's design: the aha cell can arrive last.
- **Sprite limit:** 10 sprites per line. In-flight cards are staggered on a diagonal wave so no more than 4 cards share
  a 16 px band (8 sprites), leaving room for the player.
- **Budget:** at most 8 cards in flight (16 OAM) at once. The wave spawns a new card when one commits.
- **VRAM writes per frame** (double speed, VBlank ≈ 2,280 M-cycles of safe access, plus HBlank-DMA for tile data):
  - committing one 16x16 cell = 4 map bytes plus 4 attribute bytes (VBK switch), about 120 M-cycles with the
    unrolled writer;
  - **budget: 6 cell commits per VBlank** (≈ 720 M-cycles), plus the OAM DMA (160 M-cycles), plus the palette writes
    for ambient animation (≤ 16 bytes, ≈ 100 M-cycles);
  - glitch tears are SCX writes in the STAT/LYC handler (no VRAM);
  - a 10x8 room fully assembles in about 14 frames at the cap. In practice the wave is slower, for feel.
- Tile data (new crack/quarter tiles) is preloaded per biome. No tile uploads during play except the held object's
  overlay (≤ 8 tiles via HBlank DMA, 1 per HBlank).

---

## 3. Puzzles built on combining

**The loop.** Elements and objects lie in the room. You pick things up (pocket of 6). On the portable bench (SELECT)
you combine two with a recipe from EGRESS's set. The result carries traits, and the traits act on the
room. Push, fall and dig remain the physical layer underneath (the existing `eg_room` gravity, rocks, plates and
gates carry over).

**Trait → room verbs** (the trait set follows CRUCIBLE's: HOT COLD WET AIRY STONE SHINY GLOWS ALIVE GREEN MADE BIG
MAGIC, plus EGRESS's GLITCH):

| Trait on the placed or thrown thing | Acts on | Effect |
| --- | --- | --- |
| HOT | wood, foam, drywall, vines | burns away (2 s), spreads to adjacent wood |
| HOT | ice | melts to water |
| COLD | water / flooded floor | freezes into a walkable path (until HOT touches it) |
| WET | fire, lava | puts out / cools to stone |
| WET + HOT (STEAM) | a vent | an updraft lifts you to the ledge above |
| AIRY | dust, smoke, fog | clears it; also pushes light objects one cell |
| ALIVE | earth / planter / gap with soil | grows (vine bridge across a 1–2 cell gap, roots hold a crumble floor) |
| SHINY | a beam | reflects it 90° (mirror placed on a cell, facing set by push) |
| GLOWS | a dark room | lights a radius; reveals invisible things |
| STONE + BIG | a pressure plate | holds it down (a weight you can make) |
| MADE | a lock, ladder slot, gap | tool use: SHOVEL digs, LADDER climbs a ledge, ROPE crosses a pit |
| MAGIC | glitch cells | phases them in or out |

**EGRESS's recipe set.** 38 recipes: 34 follow CRUCIBLE's elements and traits (the pairs and traits match CRUCIBLE's
catalogue, so the shared world stays consistent between the two games), and 4 are EGRESS's own (the backrooms
things at the end):

| Result | Recipe | Traits | Room use |
| --- | --- | --- | --- |
| STEAM | WATER + FIRE | HOT WET AIRY | vents lift; blinds in combat |
| MUD | EARTH + WATER | WET STONE | slows; fills a pit |
| LAVA | EARTH + FIRE | HOT STONE GLOWS | burns; cools to STONE with WATER |
| DUST | EARTH + AIR | AIRY STONE | blinds; reveals invisible things (they get dusty) |
| RAIN | AIR + WATER | WET | wets an area (shields off, plants grow) |
| ENERGY | AIR + FIRE | GLOWS | powers a dead door |
| CLOUD | AIR + STEAM | WET AIRY | floats one cell; rain source |
| SNOW | AIR + RAIN | COLD | freezes slowly; slows mobs |
| ICE | WATER + SNOW | COLD SHINY | freezes water into a path; reflects |
| PLANT | EARTH + RAIN | ALIVE GREEN | grows a 1-cell bridge |
| TREE | PLANT + PLANT | ALIVE GREEN | a 2-cell bridge; blocks a beam |
| MOSS | STONE + PLANT | ALIVE GREEN | holds a crumble cell |
| STONE | WATER + LAVA | STONE | a weight; fills a hole |
| SAND | WATER + STONE | STONE | fills a pit; with FIRE becomes GLASS |
| METAL | FIRE + STONE | SHINY MADE | conducts; a weight |
| GLASS | FIRE + SAND | SHINY MADE | a window: light passes, you don't |
| MIRROR | GLASS + METAL | SHINY MADE | reflects a beam (the dark room) |
| ELECTRIC | ENERGY + METAL | GLOWS | powers doors; shocks WET things |
| LIGHTNING | ENERGY + ENERGY | AIRY GLOWS BIG | a one-shot room-wide shock |
| FAN | AIR + ELECTRIC | AIRY SHINY MADE | pushes a lane of light things |
| BULB | GLASS + ELECTRIC | SHINY GLOWS MADE | a placeable light |
| LASER | CRYSTAL + BULB | SHINY GLOWS MADE | a beam source |
| CRYSTAL | ENERGY + STONE | STONE SHINY GLOWS MAGIC | phases glitch cells |
| SWORD | FIRE + METAL | SHINY MADE | weapon mod (edge) |
| SHOVEL | EARTH + METAL | SHINY MADE | digs 2 deep |
| HAMMER | STONE + WOOD | SHINY MADE | breaks STONE blocks |
| LADDER | WOOD + WOOD | MADE | climbs one ledge |
| ROPE | GRASS + GRASS | MADE | crosses a 1-cell pit |
| GRASS | EARTH + PLANT | ALIVE GREEN | rope source; soft landing |
| LANTERN | FIRE + GLASS | HOT SHINY GLOWS MADE | carried light (dark rooms) |
| FOG | EARTH + CLOUD | WET AIRY GREEN | hides you from turrets |
| FROST | ICE + PLANT | COLD SHINY GREEN | a slow freeze trap |
| GEYSER | EARTH + STEAM | HOT WET AIRY BIG | a permanent lift |
| BRICK | FIRE + MUD | STONE MADE | builds a wall cell; a weight |
| **STATIC** | DUST + ELECTRIC | AIRY GLITCH | EGRESS: phases glitch cells; scrambles mob AI for 2 s |
| **FOAM** | MUD + AIR | AIRY MADE | EGRESS: a soft block you can stand on; burrowers pass under it; burns |
| **COFFEE** | STEAM + SEED | HOT WET ALIVE | EGRESS: wakes a sleeping talker (extra talks); +1 i-frame for a stage |
| **NEON** | GLASS + ENERGY | SHINY GLOWS MAGIC | EGRESS: a light that also reflects; opens neon-locked doors |

Names left out on purpose (they'd duplicate jobs): VINE, WIND, TOOL, BLADE, LIGHT, BATTERY, WIRE. Their jobs go to
PLANT/TREE, FAN, BULB/LANTERN and ELECTRIC. The bench is EGRESS's own overlay (`mock_bench.png`): it rises over the
dimmed room, so making things never leaves the room. It has two sockets, the result socket lit with sparkles, trait
chips, a one-line hint, and the pocket row.

### 3.1 Puzzle grammar: 10 trick types

Each room has exactly one named **trick** (the aha), 0–2 **supporting** tricks and 1–2 **red herrings**.

| # | Trick | The aha | Typical chain |
| --- | --- | --- | --- |
| 1 | **Kettle** | steam rises | WATER + FIRE = STEAM → vent → lift to a ledge |
| 2 | **Cold path** | freeze the flood | AIR + RAIN = SNOW, + WATER = ICE → water cells become path |
| 3 | **Burn through** | fire eats wood, and spreads | FIRE on a wood barricade; spread order matters (don't burn the bridge you need) |
| 4 | **Green bridge** | living things grow across | EARTH + RAIN = PLANT on soil beside a gap → bridge |
| 5 | **Mirror** | shiny turns the beam | GLASS + METAL = MIRROR placed and pushed to face the receiver |
| 6 | **Dead weight** | make your own weight | FIRE + MUD = BRICK onto a plate; or push a block into a pit, then over a plate |
| 7 | **Lights out** | glow shows what's there | LANTERN reveals invisible floor / mobs |
| 8 | **Assembly key** | the bridge comes when the recipe is made | the conditional assembly wave: make X anywhere, cards arrive |
| 9 | **Collapse drop** | gravity is a tool | collapse the cell under a CRT so it falls onto the plate below / onto a mob |
| 10 | **Short circuit** | wet things conduct | RAIN on the floor + ELECTRIC → the whole wet area powers a door (and shocks mobs on it) |

**Red herrings** are real ingredients whose recipes lead somewhere plausible but wrong. Examples: SALT in a Kettle
room, or enough FIRE to burn the only wooden bridge. They must never soft-lock the room. The generator checks every
red-herring use against the solver: either it is harmless, or the room re-supplies the consumed ingredient (a spring,
a fire source).

### 3.2 Generator: built backwards from the goal

A room is a dependency graph that is grown backwards from the exit, then laid out and verified:

```
EXIT ← GATE(condition) ← OBJECT(trait) ← RECIPE(a+b) ← INGREDIENTS ← OBSTACLES(trick-specific) ← START
```

1. **Pick the trick** from the floor's deck (weighted by biome, depth, and the "seen" history so it doesn't repeat
   soon).
2. **Pick the gate**: what the exit needs. A ledge needs a lift or a ladder. A flooded floor needs a path. A dark
   corridor needs light. A dead door needs power. A plate needs weight.
3. **Pick the object** whose trait opens it, then **a recipe** for it from the subset. At depth ≥ 3, a two-step
   recipe (ICE needs SNOW first).
4. **Place the ingredients behind obstacles** from the supporting tricks: a wood barricade (burn), a flooded strip
   (freeze), a crumble cell (one-way), a rock (push/dig layer).
5. **Add red herrings**: 1–2 ingredients whose recipes are valid but don't open the gate.
6. **Lay out on the grid** (10x8 cells, walls, void edges), and emit the assembly order (the aha cell last).
7. **Verify with the solver**: the existing BFS/A* solver (`host/eg_solve.c`) extended with pickup, bench and place
   actions and trait effects. It reports:
   - **steps** (the shortest solution length);
   - **branching** (the average number of legal meaningful actions per state on the shortest path: the
     "how many things look possible" feel);
   - **uniqueness** (the number of distinct solution plans up to the move order; 1–2 is ideal, more means the trick
     is skippable);
   - **herring safety** (no sequence of herring uses makes the room unsolvable);
   - **trick necessity** (the room is unsolvable with the trick's key action removed: the aha is real).

   Rooms that fail are rerolled; ones that pass are scored as
   `difficulty = steps + 3·branching + 4·(recipe depth) + 2·(supporting tricks) − 2·(uniqueness − 1)`.
8. **Readability rules** (checked, not hoped):
   - every interactive cell uses a distinct card or icon;
   - nothing that matters is hidden by a wall from the default camera;
   - ingredients sit on lit cards;
   - no more than 6 objects per room.

As now, the game ships with **baked, verified seeds** (the cartridge can't afford to solve). The host tool bakes
thousands of rooms per trick and depth, and the ROM picks from them by seed.

### 3.3 Floor themes change the rules (category-style, as in CRUCIBLE)

| Theme (category) | Rule change |
| --- | --- |
| PLACE | locked: no bench on this floor; only pre-made objects |
| LIFE | ALIVE things grow by themselves every 8 s (vines close paths too) |
| WEATHER | the sky is set per floor (RAIN: everything is WET; SNOW: water freezes over time; STORM: lightning strikes WET cells) |
| ENERGY | doors need power; ELECTRIC lasts longer |
| MATTER | double the ingredients, half the pocket |
| CRAFT | MADE results are stronger (ladder +1 ledge, shovel +1 depth) |
| ELEMENT | only the four starters on the floor; everything must be made |

---

## 4. Stages: not always "find the key, open the door"

The world generator picks each stage from a **weighted deck** per biome and depth. It never deals the same type back
to back and keeps a "seen" history (the last 6 types and the last 10 tricks) so nothing repeats too soon.

| Stage type | Win condition | Notes |
| --- | --- | --- |
| Puzzle room | reach the exit | the 10 tricks above |
| Holdout | survive N waves (2–4) while the egress assembles or charges | the charge pauses when you leave its ring; waves arrive from the void as glitch-in cards |
| Escort / protect | a talker or a fragile object reaches the exit / survives | it walks a generated path; you clear and shield it |
| Chase / collapse | reach the exit before the collapse wave | the floor drops behind you at a set speed; shortcuts need recipes |
| Hunt | kill the marked target | the target has a kill condition (section 7.3); the means are in the room |
| Mid-boss / boss arena | win the fight | 2–3 phases, environment use (section 7.5) |
| Heist / trade | take or buy the thing, get out | a talker's shop: barter recipes, tokens or a heart; stealing triggers a holdout |
| Dark room | reach the exit lit | GLOWS lights a radius; SHINY reflects light / a beam onto a receiver |
| Mix under pressure | make the target recipe while mobs come | the bench is open in a ring, so you have to clear space to use it |
| Rest / landing | none (leave when ready) | a talker and a trader, the book, the lineage tree; history-gated talks |
| Glitch rift (optional) | survive a short random stage | glitch modifiers, high reward (rare pieces), risk (a curse affix) |

**Falling as a tool, not decoration.**
- **Puzzles:** collapse a cell so a CRT or rubble drops onto a switch below (Collapse drop trick).
- **Kills:** armoured things that only die when crushed. Lure them under a collapsing cell, or drop a block on them.

**Escalation.**
- Every stage is scored by its generator: solver steps for puzzles, the enemy budget (sum of mob cost), the
  condition count (how many kill/gate conditions must be met) and modifiers.
- The **target difficulty** for depth `d` and generation `g` is `D(d,g) = base + 6·d + 2·min(g, 8)`, with ±15%
  jitter. The generator rerolls until the stage lands in the band, so the score rises by depth (monotonic-ish, with
  some easy breathers) and slowly by generation.
- **Run shape** (8 floors): 3–4 stages per floor plus a landing; a mid-boss on floor 4, the boss on floor 8; one
  optional rift per run on average.
- **Biome order** is generated per run, under these constraints:
  - the glitch core is always last;
  - the office or the vents comes first;
  - no biome appears twice in a row.
- The biome sets which recipes and elements are common (the flooded atrium is WET-heavy and COLD-useful, the server
  farm is ELECTRIC, the dream garden is ALIVE).

**Verified headlessly** (10,000 runs, `proto/matrix/RESULTS.md`):
- **Repeats:** 0 back-to-back stage types. The same type two stages apart 2.5% of the time. A trick repeats within 6
  trick rooms in 0.6% of rooms.
- **Difficulty:** it climbs every floor (mean 27 → 52) and with heir generation (40 → 52).
- **Fairness:** 86% of stages need a specific means. Before the guarantee step, 65.1% would be unwinnable from the
  room alone; **after it, 0**.
- **Coupling** (measurable but weak): a hated CHARGE raises SHOCK/HOT enemies from 17% to 27%; a DREAM leaning
  raises dream-garden visits from 47% to 61%.
- **Proxy:** completability for puzzle rooms is a means proxy here; the real solver verifies the actual grids.

---

## 4b. Treasure: greed at risk

Treasure is layered through every stage to pull you off the safe path. The main path never depends on it.

**Where it is.**
- **Visible but hard to reach:**
  - on top of raised blocks;
  - on ledges over the void;
  - across crumble floor;
  - behind hazards;
  - on a flyer's route;
  - on a card that is still assembling or about to collapse.
- **Hidden:**
  - under foam or rubble;
  - under a CRT (push it to reveal);
  - inside breakable drywall;
  - in a crumbling patch;
  - under water you must freeze or drain.
- **Hinted:**
  - a card with a slightly different tint and a slow shimmer (palette-cycled, 1 colour);
  - a sound tell (a soft in-key chime when you're within 2 cells);
  - a talker line;
  - heir traits: INSOMNIAC sees the shimmer at night, REMEMBERS marks patches an ancestor found, COLOUR-BLIND sees
    the SHINY ones.

**Reach cost and risk tiers.** After the room is generated and verified, the generator places treasure from a
per-stage **budget** (it rises with depth). For each candidate spot, the solver/sim measures the **reach cost**:
- extra solver steps off the main path;
- hazards crossed;
- enemies within 3 cells;
- whether it needs a recipe, a reaction, a collapse or a specific heir trait;
- whether it is timed (a floor that drops, a holdout wave, a beam cycle).

Reward tier = f(reach cost). The richest patches need daring play:
- a timed run before a floor drops;
- grabbing it mid-holdout;
- dashing through a beam;
- riding a card as it falls.

Some patches are reachable **only** with a specific reaction or heir trait (SMALL through a vent, HEAVY pushing two
blocks, a BLINK through a gap), a reason to come back with another heir.

**What you find** (the weights shift with tier and biome):

| Find | Notes |
| --- | --- |
| tokens | the common tier |
| rare recipe ingredients | CRYSTAL, RUBY, SEED, OIL... ingredients that unlock deeper recipes |
| weapon/mod pieces | super-matrix pieces, some found-only |
| lore fragments | chosen by the truth matrix (they show the current lean, vaguely) |
| lineage relics | dropped by an ancestor on the floor where they died: their face, their last piece |
| cosmetic avatar parts | hair, hat, colour variants fed to the generator (seeded, diverse) |
| cursed items | an upside piece with a cost piece attached, clearly marked by a glitch outline |

**Rules.**
- Placement comes after the main path is built and verified.
- Every patch is verified reachable by the solver (or the stage sim for combat stages), and its reach cost is stored
  with the baked room.
- Treasure never blocks or softlocks the main path. Taking a patch may cost you the safe route (the floor drops), but
  never the exit.
- Deterministic from the seed.

**Tally.** At the end of each stage: "FOUND 2 OF 5". Hidden patches you didn't find show as "?". The book tracks the
best tally per stage type.

**Feel.**
- **Visible treasure** sparkles: 2 particle glints from the pool, on a timer, never more than 2 per patch.
- **The reveal** is its own animation state: a hidden → hinted → uncovered → lifted → collected sequence, with a
  rising arpeggio in the music's key (the tier sets the length: 2, 3 or 5 notes).
- **Cursed items** hiss instead (noise burst, then a minor second).

**Verified** (10,000 runs):
- 1.5 patches per stage, 76 per run;
- 44% hidden (a third of those hinted);
- 15.3% trait-gated;
- reach cost against tier correlates at r = 0.94;
- unreachable: 16% before the placement guarantee, **0 after**;
- 0 main paths depend on treasure.

`mock_room.png` shows:
- 3 visible patches: on a wall block, on a block still dropping in, and on a card mid-assembly;
- 1 hinted patch: the warmer card with a shimmer, bottom left, with a lineage relic under it;
- the HUD tally.

---

## 5. The super matrix: one composition system for everything

Traits, weapons, mods, elements, dodge styles, mob parts, elite affixes and boss parts are all **pieces**. Like
recipes, they combine through rules, not hand-written combos.

### 5.1 Piece schema (5 bytes)

```
byte 0  trigger (5 bits) | stack group (3 bits)
byte 1  effect  (5 bits) | shape (3 bits: self, melee arc, projectile, area, trail, aura, summon, terrain)
byte 2  tags    (8 bits: HOT COLD WET SHINY ALIVE MAGIC GLITCH STONE)
byte 3  magnitude (4 bits) | cooldown/period (4 bits, in 1/4 s)
byte 4  flags: found-only, bench-craftable, cost side (for paired traits), rarity (2 bits)
```

**Triggers:**
- on hit, on kill
- on dodge (start / end), on parry, on perfect parry
- on charge release
- on hurt, on low HP (≤ 1 heart)
- on push, on dig
- on recipe made
- on enter tile (water/ice/void edge)
- every N seconds
- on combo hit 3

**Effects:**
- projectile, area burst, status (wet, burn, frozen, shocked, blind, slowed, marked)
- terrain change (freeze/melt/burn/grow/collapse/assemble a cell)
- stat change (speed, damage, i-frames, push strength)
- summon (afterimage, sprout, mini-turret)
- heal / shield
- token gain

**Stacking:**
- Pieces in the same stack group add with **diminishing returns**: the 1st counts 100%, the 2nd 60%, the 3rd 30%,
  the 4th and later 0.
- Hard caps: speed +50%, damage x2.5, i-frames +6, projectiles per trigger 3, summons alive 2.
- Every trigger has an internal cooldown floor (6 frames), so "on hit" chains can't loop.

### 5.2 One reaction table for rooms and fights

Tags meet when an effect with tags touches a target with a status or tags: a mob, a cell, an object or the player.
The same table runs the puzzle tiles and the combat. Fire burns the wooden wall and the enemy; cold freezes the water
tiles and the enemy.

| × | HOT | COLD | WET | SHINY | ALIVE | MAGIC | GLITCH | STONE |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **HOT** | flare (+dmg) | thaw/shatter (frozen target takes x2, breaks ice) | STEAM cloud (blind, lifts at vents) | glare (blind short) | BURN (DoT, spreads to ALIVE/wood) | ember sigil (delayed burst) | flicker fire (random cell ignites) | glow-heat (STONE becomes LAVA cell 3 s) |
| **COLD** | | deep freeze (frozen 2 s) | FREEZE (frozen; water cells become ice path) | prism (beam splits in 2) | frost-bite (slow, stops growth) | crystal (target becomes a SHINY block you can push) | stutter (target skips frames) | brittle (next hit shatters) |
| **WET** | | | soak (+WET duration) | slick (target slides 1 cell) | OVERGROW (vines root target / bridge cell) | bloom (heal 1 for the player) | SHORT CIRCUIT if shocked (chain stun across connected WET) | MUD (slow area) |
| **SHINY** | | | | REFLECT (projectile reverses; parry reflection splits in 2) | sun-feed (ALIVE grows x2) | lens (charged shots pierce) | mirror ghost (a decoy) | polish (block slides farther when pushed) |
| **ALIVE** | | | | | swarm (+1 summon) | wild growth (cell becomes bridge/wall) | rot (armour −1) | moss (crumble cell holds) |
| **MAGIC** | | | | | | resonance (+cooldown speed) | GLITCH BURST (area tear, scrambles mob AI 2 s) | rune (block becomes a switch) |
| **GLITCH** | | | | | | | duplicate (the effect repeats once, 30 frames later) | noclip (cell phases out 2 s) |
| **STONE** | | | | | | | | crush (heavy hit, ignores armoured front) |

ELECTRIC/SHOCK is a status that ELECTRIC things carry, and it reacts with WET (short circuit). ICE is COLD+SHINY,
STEAM is HOT+WET+AIRY, and so on: an item's traits *are* its tags, so a crafted thing reacts exactly as its
traits say.

**Fixed resolution order per frame:**
1. input;
2. player state;
3. mob AI;
4. movement and collision;
5. hits (sorted by actor slot);
6. triggers (in piece order, by slot);
7. reactions (cell-major, then actor slot);
8. statuses tick;
9. terrain commits (queued, ≤ 6 per frame, VBlank budget);
10. particles and SFX requests;
11. HUD.

Everything is fixed-point (8.8 positions, 4.4 speeds) and deterministic from seed and input.

### 5.3 Generative weapons, mods and heirs

- A **weapon** = a base (blade, hammer, whip, gauntlet, launcher) + 1–3 pieces.
- A **mod** = 1 piece; crafted on the bench (SWORD → "EDGE" mod, LANTERN → "EMBER" mod, ICE → "RIME" mod) or found.
- **Heirs** = a dodge style + 1–2 paired trait pieces (an upside piece plus a cost piece).
- **Names** come in the family's wacky voice: a tag adjective plus an object noun from the piece pool, for example
  KETTLE EDGE, SORRY LAMP, DAMP INTERN, RIME TOASTER.
- Some pieces are found-only or bench-only, so crafting matters.

### 5.4 Ten example builds

| # | Build | What happens |
| --- | --- | --- |
| 1 | RIME blade + GLITCHED heir + blink dodge | each blink leaves a frozen afterimage (summon, COLD); a GLITCH × COLD stutter freezes mobs that touch it; hit it and it shatters (HOT × COLD) |
| 2 | EMBER mod + DAMP heir (always WET) | your hits are HOT on a WET source: every hit makes a STEAM cloud that blinds; steam also lifts you at vents |
| 3 | MIRROR gauntlet + parry | perfect parries REFLECT orbs; SHINY × SHINY splits each reflected orb in two |
| 4 | Hammer + HEAVY heir | push strength 2 (two blocks), the charged slam is STONE: crushes armoured fronts, collapses cracked cells under mobs |
| 5 | SPROUT whip + RAIN on the floor | combo hit 3 plants a seed; WET floor → OVERGROW roots mobs; on puzzle floors you grow bridges mid-fight |
| 6 | VOLT launcher + flooded atrium | shots carry SHOCK; WET floor → SHORT CIRCUIT chains across every mob standing in water |
| 7 | LANTERN mod + dark room + INSOMNIAC heir | the night palette; the lantern lights a radius and burns ALIVE; insomniac hears more talks, sees invisible mobs at the edge |
| 8 | Slide dodge + FROST trail mod | the slide leaves an ice trail: mobs slip (WET × SHINY slick), ice over water becomes a path |
| 9 | CRYSTAL blade + GLITCH rift curse | MAGIC × GLITCH bursts on kill; the curse (cost) means random cells phase out under you |
| 10 | Blade, no mods, ECHO heir | the baseline: the last heir's ghost path shows the route; clean combos and parries; the control build the sim compares against |

**Sim results** (`proto/matrix/`, a host C prototype; 10,000 seeded runs, all 250,614 player builds; an abstract
combat model, so it finds runaway stacks and dead pieces but doesn't predict feel):
- **Clear rate:** the calibrated field clear rate is 40.9%. Every archetype sits between 34.8% and 47.8%, every mod
  and trait between 32% and 52%, every weapon between 36% and 45%.
- **Extremes:** 37 of 250,614 builds clear 90% or more (a lucky stack, not an archetype); none clears 3% or less.
- **Fixes found by the sim:**
  - lifesteal bypassed stacking: it is now capped heal-only;
  - avoid was the strongest axis: TINY was trimmed;
  - weak pieces were raised as data.
- **Soft caps as shipped:** DAMAGE 90, STATUS 70, AREA 80, PROJ 80, DEFENSE 45, SPEED 40, SUMMON 60, REACT 60
  (hard stop x1.5). The auto-tuner needed no change.

### 5.5 Particles, FX and SFX per tag and reaction

**Particle pool:** 8 sprites, hard cap, shared, oldest-first recycling. Each tag has a particle (an ember, a frost
mote, a drop, a glint, a leaf, a rune, a tear pixel, a pebble), and each reaction has a burst pattern (steam: rising
puffs; shatter: 6 shards; short circuit: arcs along WET cells drawn as BG flashes, not sprites).

**Feedback scaled by magnitude:**
- palette flash on the target (1–3 frames);
- screen shake via SCX/SCY offsets (0–2 px);
- hitstop (0–4 frames).

The shake and flash stay off for anything under magnitude 4, so ordinary hits feel crisp, not noisy.

**SFX:** one short sound per reaction. The tonal ones are in the music's key (section 14). Steam is filtered noise
plus a falling pulse; shatter is a short high noise; overgrow is a soft rising wave; the short circuit is a buzz
under 120 ms. None is harsh: no full-volume square leads, envelopes on everything.

**OAM budget, worst case (40 entries):**

| Use | Count |
| --- | --- |
| player | 4 |
| weapon arc / held object | 2 |
| mobs (up to 4 on screen at 24 px, 4 sprites each) | 16 |
| pieces in flight (assembly/collapse) | 8 (4 cards) |
| particles | 8 |
| elite glow / boss parts | 2 |
| **total** | **40** |

The 10-per-line rule is enforced by a per-band counter. When a band is full, particles drop first, then flicker
cycling applies to mobs.

**CPU budget at 60 fps** (double speed, ≈ 35,000 M-cycles per frame):

| Work | Typical | Worst case |
| --- | --- | --- |
| input + player | 600 | 600 |
| mobs (≤ 8 AI, 6 active) | 4,000 | 8,000 |
| collision | 2,000 | 4,000 |
| hits + triggers + reactions (cap 16 reactions per frame, the rest queued) | | 6,000 |
| particles | | 1,500 |
| assembly/collapse | | 2,000 |
| music + SFX | | 2,500 |
| OAM build | | 2,000 |
| HUD diff | | 500 |
| **total** | | **≈ 27,000 (≈ 78%)** |

Anything over the reaction cap waits for the next frame, in order, so the result is the same.

---

## 6. Combat: one weapon, six moves, two buttons

Bastion-style: one weapon at a time, crafted mods, and the owner's exact mapping. **B is the dodge button.**

| Input | Move |
| --- | --- |
| **A tap** | main attack: a 3-hit combo |
| **A hold** | charge; release for the charged attack (levels by hold length) |
| **B tap** | dodge: the heir's own style (roll, blink or slide); starts **instantly** on press |
| **B hold** | a held B move that continues out of the dodge (see below) |
| **B then A** | dodge-cancel strike: A inside the cancel window cancels the dodge into a lunge that keeps its momentum |
| **A then B** | weapon secondary / parry (a perfect parry reflects orbs and staggers) |

SELECT opens the bench, START the menu.

**Input rules.**
- **Tap or hold.** A tap is a release within `T_HOLD` frames, a hold is still held at `T_HOLD`. The exact counts are
  in the frame table below.
- **No input lag.** A tap's anticipation (startup) starts on the press frame. If the button is still held at the
  threshold, the hold branches off the tap's startup, so a hold never makes a tap feel late.
- **No chord delay on B.** The dodge starts on the press frame. B→A is a cancel window inside the dodge, not a chord.
- **A→B uses order.** Whichever button went down first decides it, within a small window.
- **Same-frame presses** (A and B together) are a parry: the 0-frame end of the A→B window. B→A can't mean the lunge
  on that frame anyway, because every cancel window opens at dodge frame 3 or later.
- **Buffer.** A 6-frame buffer queues presses during recovery.
- **Determinism.** The whole thing is a pure function of the input stream, evaluated once per frame from the held
  byte (edges are computed inside).
- **Dodge style.** Each heir's style changes the dodge's frame data and look (roll, blink, slide); the six inputs
  stay the same.

**B hold: two options.**
- **(a) Sprint-slide:** B held past the threshold carries the dodge into a low slide and then a sprint (+40% speed,
  no attack until released), keeping the dodge's direction.
- **(b) Held guard:** B held after the dodge raises a guard that blocks frontal hits (chip damage) and turns into a
  parry on release-and-press.

**Pick: (a) sprint-slide.** A→B already owns parrying, so a second guard would blur the two. The sprint is the
missing movement verb for chase/collapse stages and holdouts. It also composes with pieces: a slide leaves trails
(build 8), and blink-style heirs blink-chain instead.

**PROOF.** The resolver is written as a pure portable C module, `src/core/eg_input.{h,c}`, with a host test,
`host/test_input.c`, that feeds exact frame timings. The frame-data table and the test results are in
`proto/input/FRAMES.md`; the table is reproduced here once it is final.

**Frame data** (60 fps frames, counted from 0 on the move's first frame; windows inclusive). **PROOF, done:**
`host/test_input.c` passes 106 checks and `host/test_move.c` passes 50.

| Move | Input | Startup | Active | Recovery | Total | Window |
|---|---|---|---|---|---|---|
| Dodge, roll | B press | 0-1 | 2-11 (i-frames) | 12-17 | 18 | B->A cancel 4-14 |
| Dodge, blink | B press | 0-2 | 3-6 (i-frames) | 7-13 | 14 | B->A cancel 6-11 |
| Dodge, slide | B press | 0 | 1-12 (i-frames) | 13-19 | 20 | B->A cancel 3-16 |
| Sprint | B held >= 11 frames | loop while B held | | | | starts at the dodge's first recovery frame, or at the threshold if that is later: roll 12, blink 10, slide 13 |
| Skid | B released in sprint | | | 0-3 | 4 | A or B cancels it at once |
| Lunge | A in the dodge cancel window, or A in sprint | 0-1 | 2-5 | 6-17 | 18 | combo link to Hit 2 from 10; keeps dodge momentum |
| Hit 1 | A press | 0-3 | 4-5 | 6-15 | 16 | combo link to Hit 2 from 8 |
| Hit 2 | A, buffered or in the link | 0-3 | 4-5 | 6-17 | 18 | combo link to Hit 3 from 9 |
| Hit 3 | A, buffered or in the link | 0-5 | 6-8 | 9-26 | 27 | finisher, no link |
| Charge | A held >= 11 frames | loop while A held | | | | branches off the hit at frame 10 (outside its active frames) |
| Charged | A released in charge | 0-2 | 3-6 | 7-20 | 21 | level 1: held 11-29, level 2: 30-49, level 3: 50+ |
| Parry | B within 0-4 frames of A's press, or while A is held | 0-1 | 2-9 (guard) | 10-21 | 22 | perfect 2-4 |

Rules:
- **Tap or hold:** the press frame counts as frame 1 of the hold. Released after 10 or fewer frames is a tap.
  Held for 11 or more is a hold (`EG_HOLD_T = 11`). Taps never wait: B starts the dodge and A starts Hit 1 on the
  press frame. The hold branches off that move afterwards.
- **Buffer:** one slot, `EG_BUF_FRAMES = 6`. A press can fire on its own frame or on any of the 5 frames after it.
  The newest press replaces the old one. A press the state can't take yet waits in the slot, for example A during
  attack startup or active frames, or B during an attack's active frames, a dodge, or a parry.
- **A before the cancel window:** buffered into the window. It fires on the window's first frame, which is always
  within 6 frames of a press made after the dodge started. A after the window but before the dodge ends is a normal
  A: Hit 1 starts on the first idle frame if the buffer hasn't expired. A after the dodge ends is a normal A.
- **A then B:** B counts as the secondary (parry) when A is held, or when A went down 0 to 4 frames earlier
  (`EG_AB_WIN`). Otherwise B is a dodge. The parry can cancel any phase of Hits 1-3 and the charge, but only the
  recovery of the lunge and the charged attack.
- **Same-frame A+B:** parry. This is the 0-frame end of the A->B window. B->A can't mean the lunge on that frame,
  since every cancel window opens at dodge frame 3 or later. Plain B already gives the dodge, so the chord gets a
  meaning of its own.
- **B during attacks:** a dodge cancels a hit's startup and recovery at once. During active frames it is buffered.
  For the lunge and the charged attack, a dodge only cancels recovery.
- **No infinite cancels:** a dodge started out of a lunge is marked as chained and can't lunge again. A in its
  window is held as a normal A. Hit 3, the charged attack, the parry and dodges can't be cancelled by A.
  Counters saturate at 255.

The tests feed exact frame-by-frame timings and cover:
- B tap and B hold;
- B→A on the first and last frame of each style's cancel window and one frame outside each edge;
- A pressed before the dodge ends;
- both pressed on the same frame;
- tap/hold at 10/11/12 frames;
- charge levels at their boundaries;
- A→B inside and outside the window;
- perfect-parry timing;
- the buffered 3-hit combo;
- 10,000 frames of seeded mashing, which checks invariants (no illegal state, never two moves at once, bounded
  counters) and determinism (the same stream twice gives identical traces).


**Enemies are designed so all six moves matter:**
- armoured fronts (dash-cancel behind them, B→A);
- projectiles (parry/reflect, A→B);
- lunges with a tell (dodge, B);
- swarms (charge spin, A hold);
- chasers on collapse floors (sprint, B hold);
- staggers that open combo windows (A tap).

---

## 7. Mobs, elites and bosses from the same matrix

### 7.1 Mob axes

A mob = **body** + **attack** + **defence/kill condition** + **tag** + **behaviour**, each a piece:

- **Body / mobility:**
  - walker
  - flyer (over pits and water)
  - phaser (through walls)
  - burrower (under foam)
  - wall-crawler
  - gravity-bound (falls, can be dropped, pulls things toward it)
  - heavy (slow, unstoppable, breaks blocks)
  - teleporter/blinker
  - swarm
  - turret
  - mimic (hides as one of the room's objects)
- **Attack:** melee lunge, projectile, AoE slam/ring, beam, trail hazard, summon, grab/pull, terrain change (freezes
  water, sets fire, collapses tiles).
- **Defence / kill condition:**
  - none
  - armoured front
  - shielded until WET, then SHOCKED
  - only dies when crushed
  - splits on death
  - reforms unless frozen
  - only hurt by its reflected shots
  - invisible in the dark
- **Tag:** one of HOT COLD WET SHINY ALIVE MAGIC GLITCH STONE, which drives the reactions.
- **Behaviour:**
  - aggressive
  - coward/kiter
  - flanker
  - guard (protects another mob)
  - pack leader (buffs others)
  - thief (steals tokens and runs)

**Compatibility rules** prune nonsense:
- a turret can't be a thief;
- a phaser can't be "crush only";
- a flyer can't burrow;
- a beam needs a 20-frame tell;
- a mimic must be a real icon from the biome's recipe pool.

**Every attack has a tell:** a palette flash plus a pose frame, at least 12 frames (24 for beams and slams).

**Families per biome** give a visual language, drawn by the mob look generator (section 7.6):
- office: human and robot clerks, lamps, fax things;
- vents: flyers and crawlers, sheet ghosts;
- flooded atrium: wet blobs, cloud styles;
- server farm: AI-eye turrets, pixel invaders;
- mall food court: mimics, flame styles, thieves;
- dream garden: ALIVE styles, radiant and poly faces;
- rooftop sky: flyers, cloud and observatory motifs;
- glitch core: everything torn.

**Names** are generated in the family's wacky voice: DAMP INTERN, SORRY LAMP, FAX MOTH, HOLD MUSIC, MALL SAINT, ROOT CLERK,
SPARE FAN, TINY CHORUS, PILOT LIGHT, LAST BUS, COLD CALL, GRAVE AUNT. See `mock_mobs.png`.

### 7.2 Elites

An elite is a mob plus 1–2 affixes (a cap of 2, and never two from the same stack group):
- shielded
- vampiric
- splitting
- hasted
- element aura
- teleport-on-hit
- collapse trail

Elites are marked by an outline in OBJ palette 7 (one colour per affix), affix particles from the pool, and a name
prefix. They give 2x tokens and a chance at a found-only piece.

### 7.3 Conditional enemies ("doesn't die unless X")

Hard enemies carry 1–2 conditions drawn from the reaction table:
- frozen then shattered;
- wet then shocked;
- lured under a collapse;
- hit from behind after a dash;
- its reflected orb returned to it;
- a recipe made nearby (it fears the thing).

**Fair by construction:**
- When the generator adds a conditional mob, it must also add the means: a WET source and a SHOCK source reachable
  in the room, or in the player's guaranteed kit (the weapon base always has a parry; the bench recipes reachable
  from pocket and room).
- The stage sim checks the conditions are satisfiable. Stages that fail are rerolled, and the reroll rate is reported.
- The player is told:
  - a visual tell on the mob (a shield ring, an armour glint, a frost reform shimmer);
  - its name, which hints at it;
  - a talker line or the whisper in the void ("IT HATES RAIN").

See `mock_cond.png`.

### 7.4 Variety numbers

From the prototype composer and the 10,000-run world sim (`proto/matrix/RESULTS.md`):

| Measure | Result |
| --- | --- |
| mob builds (behaviour axes) | 46,464 raw combos, **15,981 valid** after the compatibility rules |
| elite builds | 883,325 |
| valid boss templates | 2,415,192 |
| distinct mob builds met per run | ~77 (min 65, max 93) |
| repeat rate within a run | **0.2%** (24-pick history window) |
| coverage over 10,000 runs | 100% of valid mob builds; 132,591 distinct elites; 47,722 distinct bosses |
| **looks** (section 7.6) | **678,012,660** distinct looks; 22.3 billion with tag and defence cues |

A mob on screen is one behaviour build × one look, so no two runs look alike. Players learn **tags and defences**,
whose tells never change, not individual looks.

### 7.5 Bosses

Every boss = a **boss template** (body plan, arena rule, 3 phases) + generated pieces + phase scripts. The bosses
**use the environment**:
- tear tiles out of the floor and throw them;
- collapse the arena ring by ring;
- flood or freeze it;
- drop CRTs from the void;
- switch the lights off;
- rearrange the room as it falls apart;
- use the biome hazard.

The player can turn the same tools against them: freeze the flood to stand on, drop the torn tile back on the boss,
reflect its orbs.

**Phases** demand different moves:
1. dodge-heavy (lunges and slams);
2. parry-heavy (orbs and beams);
3. the arena is half gone: sprint and dash-cancel (punish windows behind armour).

Each phase is gated by its own environmental beat (the floor drops, the lights go). Target length: 20–25 good hits
across 3 phases (folds in the M2 balance note), with clear telegraphs (24+ frames).

**Tied to the matrix.** The boss's identity, lines and twist come from the truth matrix and faction standing. Bosses
are the calamity's unreliable witnesses. They each tell you who did it, and they disagree. Example templates:
- OLD MONITOR (server farm: pulls tiles toward itself, CRT drops);
- THE TENANT (office: lights out, drywall rearranging);
- LIFEGUARD (atrium: floods and freezes);
- THE GARDENER (dream garden: overgrowth closes the arena).

See `mock_boss.png`.

### 7.6 Mob looks: a generator, like the face generator

A mob's look is composed from parts by the seed (`tools/mock/mobgen.py`, spec `proto/mobs/MOBGEN.md`, sheets
`docs/shots/mobs/family_*.png`, overview `docs/shots/mock_mobs.png`). It is drawn from xorshift32 in a fixed field
order, each field picked from what the rules allow given the fields already drawn:

- **family base silhouette:** 8 biome families with 4 bodies each. Office: clerk blob, filing cabinet, desk lamp, fax
  thing. Server farm: rack, eye orb, block bug, cable snake. And so on;
- **size class:** S 12 px (swarms), M 16, L 24, XL 32;
- **topper:** antennae, horns, tuft, cap, bun, halo, sprout, cable, fin;
- **limbs:** 2/4/6 legs, arms, tentacles, wings, wheels, hover shadow, turret mount (the mobility axis forces some);
- **eyes:** 1–4, dot/ring/slit/screen/sleepy;
- **mouth:** line, teeth, O, smile, grille;
- **pattern:** stripes, spots, circuit, cracks, dither band;
- **biome accessories:** lanyard, tie, sticky note, coffee stain; cables and LEDs; vines and flower; scarf...;
- **palette:** a family body ramp (the office has 8: cream, mint filing cabinet, sticky-note pink and yellow, carpet
  teal, lavender CRT, coral, toner magenta) × an element ramp × 5 hue shifts, then nudged to meet contrast floors
  (outline ≥ 4.5 against the floor, body ≥ 4.0 against the void);
- **idle quirk:** twitch, bob, flicker, breathe, eye roll.

**Readability rules:**
- **Family cues survive any variation:** every office mob carries a lanyard, tie or sticky note; server mobs always
  have LED/screen eyes.
- **Tag and defence tells are fixed and drawn the same on every look:** COLD frost rim, WET drips, HOT ember, ALIVE
  leaf, MAGIC rune, GLITCH shifted bands, armoured front plate, shield ring.
- Conflicting decorations were removed so a tell is never ambiguous: atrium mobs use a water line, not drips;
  garden vines never use leaf green.

**Animation** is composed from the parts (pose offsets, part swaps, palette swaps), so every variant animates:
- idle (2 frames);
- move (2);
- tell (a crouch, a yellow/white flash and a `!`);
- attack (a 4 px lunge);
- hurt (a white flash);
- death (fade plus a Bayer dissolve).

**Sprite budget:**
- S and M are 2x2 tiles, L is 3x3, XL is 4x4. At most one XL per screen.
- Body palette A, overlay palette B, elite palette 7.

**Counts:** 678,012,660 distinct looks (the office has 422 million). 22.3 billion with tag and defence cues. The
idle quirks (x5) are not counted.

**Content review** (owner's rules, every sheet checked by eye):
- Shapes that could read as a hood, a cross, phallic or fingers were redesigned.
- The circuit pattern is one-orientation only.
- There are no human likenesses.
- Hue spread passes on every sheet: 6–10 hue bins with 3% or more of body pixels, the largest bin 21–33%. None is
  monotone.

**Elites are impossible to miss** (and ANY mob can be one: any family, mobility type, size or biome; elite chance
and
affix count scale with depth and generation):
- **Palette-cycle pulse:** a 1 px outline outside the silhouette, alternating the two affix colours each phase.
  Palette 7 is reserved for elites.
- **Crown piece** above the head, one icon per affix: SHIELDED badge, VAMPIRIC fangs, SPLITTING twin cells, HASTED
  `>>`, AURA drop in the element colour, TELEPORT brackets, COLLAPSE falling blocks. It bobs 1 px.
- **Orbiting particles** in the affix colours (4 on S/M, 6 on L/XL, from the shared pool).
- **Size bump:** S→M, M→L, L→XL. An XL elite gets a second dithered glow ring instead.
- **Entrance:** an "ELITE" name tag with the affix words ("ELITE · VAMPIRIC HASTED · PALE HEDGE"), its own spawn
  sting in the music key, a 12-frame pause, and the camera nudging toward it.
- **Tiny swarm elites:** glow, particles and the size bump only.
- **Elite mimics:** stay disguised as the object; the only giveaway is a 3-pixel shimmer travelling round the rim.

---

## 8. Movement: 8-way, with a 4-way option

Every GBC D-pad (the ModRetro Chromatic's too) reports diagonals as two directions held.

- **Default: 8-way free movement** on sub-pixel fixed-point positions. The diagonal speed is normalised to ≈ 0.7
  with an integer ratio (181/256), deterministic.
- **SETUP option: 4-way.** The most recently pressed direction wins (press order is tracked).
- **Facing:** 8 directions, kept when you stop. Attacks and all six moves use the 8-way facing. Sprites: 5 drawn
  directions (S, SE, E, NE, N) plus 3 mirrored, for every animation state (see `anim.png`).
- **Collision** is axis-separated, so you slide along walls. A **corner nudge** helps when you clip a corner by a
  few pixels: you're slid round it, deterministically.
- **Grid interaction stays cardinal:**
  - pushing, digging and placing work on the 16x16 grid;
  - on a diagonal into a block, the push resolves to the axis you're moving into (the blocked one), while the other
    axis keeps sliding;
  - a push needs 10 consecutive frames of contact with the block in the same direction, so it is never accidental;
  - placing goes to the faced cardinal cell (a diagonal facing places on the axis with the larger input history).
- **Fixed point:** positions are 12.4. The diagonal step is speed x 181/256, with the remainder carried so the
  distance is exact. The hitbox is 12x12 px. The corner nudge applies when the box overlaps a solid lane by 4 px or
  less. In 4-way mode, same-frame presses rank DOWN, UP, LEFT, RIGHT.

**PROOF:** `src/core/eg_move.{h,c}` and `host/test_move.c` cover:
- diagonal speed;
- 4-way most-recent-wins;
- 8-way facing;
- the wall slide;
- the corner nudge;
- the diagonal push resolving to the right axis only after the hold;
- no push on a brief bump;
- determinism.

---

## 9. Heirs, lineage and genes (Rogue Legacy, but a real family)

When you die, you pick **1 of 3 heirs: your children**, siblings with a family resemblance (`mock_heir.png` shows
them under their parent). Everything an heir is comes from **one genome**: how they look, how they dodge, which
weapon they reach for, their traits, their elemental affinities and their base stats. The heir you pick becomes the
parent of the next three, so **your picks breed the family**: a blink line, a fire-affine line, a heavy line.

### 9.1 One kind per line

**Pick: the kind is fixed per line, and changes only through a rare, explicit "glitch birth".**
- A new lineage's founder is human, with seeded diverse skin tones across the full range (the existing rule).
- Other kinds (constructs, sheet ghosts) can only come from a glitch birth: a matrix event that needs the SELF
  axis to lean hard toward "the same person every time", inside the glitch core or a rift. It's announced ("SOMETHING
  CAME THROUGH"), and that card wears a glitch outline. From then on, that kind is the line's kind.
- No ghost is ever mixed in at random among human siblings.

**Why:**
- Family resemblance only reads if the siblings share a body plan.
- A kind change is a story beat, and it feeds the open question of whether you're the same person each time.
- Making it rare and visible keeps it meaningful and never confusing.

### 9.2 The genome (14 bytes)

**Looks: 6 bytes.** These are the face generator's own genome, read as genes, so a face *is* its genes and the
ported code renders it unchanged.

| Bits | Gene | Values |
| --- | --- | --- |
| g0 hi nibble | skin tone | 0..5, a light-to-deep ramp (mapped onto the generator's skin table) |
| g0 lo nibble | kind | fixed per line (human = 1) |
| g1 0–2 | face shape | 0..7 |
| g2 0 | mouth part | 0..1 |
| g2 3–5 | hair style | bald, short, long, bun, spikes |
| g2 6–7 | neck | 0..3 |
| g3 0–2 / 3–5 | face pattern / shading dither | 0..7 / 0..5 |
| g3 6–7 | size | 0..2 (*linked*: HEAVY makes it 2, SMALL makes it 0) |
| g4 0–2 | clothes accent | 0..7 (*linked*: the strongest affinity sets the colour, so a fire line wears warm) |
| g4 3–5 | mark | freckles, mole, scar... |
| g5 3–4 | look | DREAM / BOY / GIRL |

**Abilities: 8 bytes.** These are diploid where it matters, so recessives can skip generations:

| Bits | Locus | Rule |
| --- | --- | --- |
| 2 + 2 | dodge (two alleles) | ROLL is dominant over SLIDE, which is dominant over BLINK (recessive): blink lines must be bred |
| 3 + 3 | weapon tendency (two alleles) | the allele from the played parent leads |
| 10 x 2 | traits (allele from the parent, allele from the mate) | HEAVY, SMALL, DAMP, LOUD, INSOMNIAC, ALLERGIC are dominant; ECHO, REMEMBERS, COLOURBLIND, GLITCHED are recessive |
| 7 x 2 | affinities: HOT COLD WET SHINY ALIVE MAGIC GLITCH | quantitative 0..3, **expressed capped at 2** |
| 4 x 3 | stats: speed, power, guard, focus | quantitative 0..7, expressed through diminishing returns (+0 10 18 24 28 31 33 34%), then scaled into **one shared 40-point budget** across all four |
| 4 | flags | mutated, glitch-born, founder, spare |

The same traits as before (each is an upside paired with a cost):

| Trait | Upside | Cost |
| --- | --- | --- |
| COLOUR-BLIND | sees hidden SHINY paths (one palette channel swapped) | the palette shifts: reds and greens merge |
| INSOMNIAC | more talks (night palette, extra whispers) | sleepy: −1 i-frame on dodges |
| GLITCHED | free blinks at random (dodge on a timer) | random blinks, sometimes the wrong way |
| ALLERGIC (to an element) | immune to its opposite | that element hurts you |
| REMEMBERS | keeps one recipe across death | forgets a different one |
| HEAVY | pushes two blocks; crushes; the dodge-cancel lunge staggers | slow (−10% speed); crumble floors break at once |
| SMALL | fits through vents (shortcut cells) | −1 heart |
| ECHO | sees the last heir's ghost path | the ghost's death spot spawns its killer |
| DAMP | always WET (steam builds, short-circuits hurt you) | fire does nothing to you; shocks do double |
| LOUD | mobs come to you (escort/holdout easier) | stealth and dark rooms are harder |

At most **2 traits express**; the rest stay latent and can resurface.

### 9.3 Inheritance and mutation

Each child = the played parent + an unseen mate. The mate is drawn 2 times in 3 from the family's gene pool (the
lineage ring: the last 16 played heirs, so recessives come back), otherwise a new partner from near the family's
look.

**Looks:**
- **Skin:** the parent's tone 69% of the time, one step lighter or deeper 31%. Siblings vary naturally, and a line
  drifts slowly.
- **Other look loci:** the parent's 78%, the mate's 22%.
- **Mutation:** 2.3% per locus (a new hair style, a new mark).

**Abilities:**
- one allele from each parent per diploid locus;
- affinities as (3·parent + mate + 1) / 4, ±1 drift at 8%;
- stats as (3·parent + mate + noise) / 4, ±1 mutation at 9%;
- **trait mutation:** 0.4% per allele, about 1 child in 16 carries a brand-new trait (6% measured). It's flagged "NEW" on the card.

**Caps keep bred lines honest:**
- at most 2 expressed traits;
- affinities expressed at most 2;
- stat bonuses through diminishing returns, inside one shared 40-point budget for all four stats;
- every trait is an upside paired with a cost.

The balance sim runs bred lines against the same ~60% bar (`proto/matrix/RESULTS.md`, "Bred lines"):

Final (adopted): every genetic trait as this section defines it, HEAVY at -10% speed, and the dodge-cancel lunge staggers (stagger 5). 200 generation-30 heirs per strategy, 32 runs each. Enemies stay on base builds (K=42, tuned caps).

| strategy | clear | vs random | heirs > 60% | p10 / p50 / p90 |
|---|---|---|---|---|
| random | 56.1% | +0.0 | 61 (30.5%) | 40 / 56 / 75 |
| heavy | 56.8% | +0.7 | 60 (30.0%) | 43 / 56 / 71 |
| blink | 58.7% | +2.6 | 80 (40.0%) | 43 / 56 / 75 |
| fire | 59.8% | +3.7 | 81 (40.5%) | 46 / 56 / 78 |
| power | 58.5% | +2.4 | 89 (44.5%) | 43 / 59 / 75 |
| all heirs | 58.0% | | | |

Every strategy clears at or under 60% and lands within +4 of random. Breeding is a playstyle choice, not a power climb.

How it got here (clear rate, with the gap to random):

| model | random | heavy | worst gap |
|---|---|---|---|
| before: no stat budget, REMEMBERS/COLOURBLIND/ALLERGIC free | 84.0% | 90.1% | heavy +6.1 |
| 40-point stat budget plus real costs for the three traits | 64.8% | 72.9% | heavy +8.1 |
| traits as defined in this doc, HEAVY at -20% speed | 52.0% | 42.6% | heavy -9.4 |
| **final: HEAVY at -10% speed, lunge stagger 5** | **56.1%** | **56.8%** | **fire +3.7** |

The three trait costs: REMEMBERS forgets a different recipe, adding one step in every mix room. COLOURBLIND merges reds and greens, so HOT and ALIVE foes' tells read worse and they deal +25%. ALLERGIC makes one seeded element hurt +50% and its opposite -50%. Alone, each cost moves the field by 0.1 point or less, because few heirs carry them (37, 20 and 108 of 1000).

The stagger sweep: 0 gives heavy -3.8, 3 gives -1.3, 5 gives +0.7, 8 gives +3.2, 12 gives +6.7. 5 is the non-zero value with the smallest gap.

### 9.4 Selection over 30 generations (simulated)

`proto/genetics/genes.py` (integer-only, seeded, the cartridge's rules). 200 lines per strategy, 30 generations;
the values are averages over the three offered siblings:

| Picking strategy | HEAVY share | BLINK share | HOT affinity | POWER gene | new-trait children per generation |
| --- | --- | --- | --- | --- | --- |
| random | 0.32 → 0.33 | 0.11 → 0.15 | 0.35 → 0.62 | 2.96 → 3.06 | 0.19 of 3 |
| heavy | 0.32 → 0.96 | 0.11 → 0.18 | 0.35 → 0.69 | 2.96 → 3.06 | 0.18 of 3 |
| blink | 0.32 → 0.29 | 0.11 → 0.73 | 0.35 → 0.72 | 2.96 → 2.91 | 0.19 of 3 |
| fire | 0.32 → 0.29 | 0.11 → 0.17 | 0.35 → 2.25 | 2.96 → 2.97 | 0.19 of 3 |
| power | 0.32 → 0.31 | 0.11 → 0.16 | 0.35 → 0.70 | 2.96 → 5.51 | 0.20 of 3 |

Reading it:
- **Selection works and is slow enough to feel earned.** Picking heavy heirs takes HEAVY from a third of children to
  nearly all of them in about 15 generations. Recessive BLINK goes from 11% to 73%. A fire line reaches HOT 2+ (the
  expression cap).
- **Random picking stays near the start values**, apart from a slow random drift.
- **Recessives reappear through the pool**: blink still shows up at 13–18% in lines that never picked for it.
- **About 6% of children carry a new trait.**


The face trees (`proto/genetics/out/tree_heavy.png`, `tree_random.png`) show 3 siblings per generation over 30
generations, with the played heir outlined. Siblings resemble each other and their parent, and skin and hair drift
slowly across the line. `drift.png` charts the four measures per strategy.

### 9.5 Lineage in the save

- The family tree is kept: names, faces, how each heir died, their floor and biome, and their generation. It shows
  on the TREE screen (from the heir pick and from the landing) as faces across generations, so the resemblance and
  drift are visible.
- **The next generation also inherits a bounded slice of the world:**
  - weapon mods unlocked;
  - known recipes, partly (each generation forgets 1 in 4, seeded; REMEMBERS protects one);
  - a trace of faction standing (halved each generation);
  - the truth matrix leaning (decayed by 1/4).
- **Ancestors return.** On the floor where an ancestor died, their ghost (ECHO) or their body (a lineage relic with
  their face) can appear. Some talks mention your parent, always vaguely.
- **The same person?** That's a matrix axis (section 10), never answered.

**SRAM layout** (8 KiB, bank 0, two-record CRC like now):

| Address | Size | Content |
| --- | --- | --- |
| `0xA000` | 32 | meta record A (save version, settings incl. 4/8-way, volumes, CRC-16) |
| `0xA020` | 32 | meta record B (mirror) |
| `0xA040` | 256 | mid-run continue record A (floor, stage seed, HP, pocket, weapon pieces, room state delta, CRC) |
| `0xA140` | 256 | mid-run continue record B |
| `0xA240` | 4 | lineage header: head index, count, generation number (16-bit) |
| `0xA244` | 320 | **lineage ring = the gene pool: 16 heirs x 20 bytes** (genome 14, name seed 1, death floor 1, biome 1, cause of death 1, generation 1, matrix snapshot 1) |
| `0xA384` | 42 | the current three siblings' genomes (3 x 14), so a pick survives power-off |
| `0xA3AE` | 2 | lineage CRC |
| `0xA3B0` | 64 | inheritance: mods unlocked 8, known recipes 6, forgotten 6, faction trace 6, truth leaning 8, reserved 28, CRC 2 |
| `0xA3F0` | 128 | seen history: tricks, stage types and biomes recently dealt; book bits for the recipes; talks heard; treasure tallies |
| `0xA470` | 256 | stats and book |
| `0xA570`–`0xB4FF` | ~3.9 KB | free |
| `0xB500`–`0xBFFF` | 2.75 KB | face generator work area (as ported) |

The ring keeps the last 16 heirs (they are also the gene pool). Older ones are summarised into a 16-bit "elders"
count, and their deaths still count in the matrix through the leaning bytes. **Lineage total: 432 bytes.**

---

## 10. Story: a small calamity

Something happened in the building. The floors come apart and reassemble wrong. You don't know what you did. The tone
is backrooms meets dreamscape: fluorescent hum, pastel sky, office carpet in the void.

**The truth matrix** (the CHAT matrix) has four signed axes. Each is a byte that leans, never a fixed fact:

| Axis | Negative end | Positive end |
| --- | --- | --- |
| CAUSE | it was the building (structural, natural) | it was made (someone combined something) |
| CULPRIT | a faction | you |
| REVERSIBLE | it can be undone | it can't, only survived |
| SELF | heirs are different people | it's the same person every time |

**What moves it:**
- **what you make:** recipes made push CAUSE toward "made", for example;
- **what you say:** talk choices push CULPRIT;
- **what you do:** collapsing things on purpose, saving the escorted talker, stealing;
- **how you die:** lineage deaths feed SELF.

The seed sets the starting lean, so every run differs from the first line, and play moves it from there.

**Delivery:**
- Short fragments only, never exposition:
  - whispers drifting in the void;
  - two- or three-line talks;
  - the book's margins;
  - boss lines.
- Each **faction** (COMPILE, CHARGE, SKY, WATCH, ROOT, LOCK; faces from the shared generator's PROGRAM, DAEMON, GHOST, AI,
  OPERATOR and RELIC styles) has its own partial, unreliable version, slanted by its standing with you.
- **Endings are questions.** The final room reads the four axes and asks one: "WAS IT ALWAYS THIS SHAPE", "WHO
  OPENED IT FIRST", "DO YOU REMEMBER THE STAIRS". Never cheesy, never a reveal.

**Three linked matrices:**
- **CHAT** (truth axes and faction standing):
  - standing shifts the **WORLD** deck (a friendly WATCH means fewer turrets and more dark rooms with lanterns;
    an angry ROOT means more ALIVE mobs);
  - the CAUSE lean tilts biome mood (palette warmth, music mood weights).
- **FUNCTION** (pieces and reactions):
  - the recipes you make feed CHAT;
  - the elements common in a biome come from **WORLD**.
- **WORLD** (biome x stage type x modifiers):
  - the stages you clear or flee feed CHAT (fleeing collapses leans toward REVERSIBLE = no);
  - mods found are seeded by **FUNCTION** rarity.

---

## 11. Characters and animation

- **Talk portraits:** the shared procedural face generator (CRUCIBLE's code, ported, not redrawn; `src/avatar/`,
  done and verified to produce the same faces). It covers blinking, the mouth moving while text types (`avatar_say`,
  `avatar_tick`), and glitch.
- **The player** is built from the same genome:
  - the skin and ink colours and the hair style (bald, short, long, bun, spikes) of the heir's face drive a 16x18
    chibi sprite;
  - the DREAM/BOY/GIRL look picks the silhouette;
  - clothes take the faction colour.
- **Mobs and bosses** use the avatar styles (robot, AI eye, sheet ghost, flame, cloud, pixel invader...) on bodies
  from the mob axes; mobs get their own generator (section 7.6).

**Animation is data:** `state → {frames[], ticks[], loop, events}`, with the events firing triggers (the hit frame,
the i-frame window, the footstep SFX). States:

| Owner | States |
| --- | --- |
| Player | idle (breathe, blink), walk x8 directions, push, dig, combo hits 1–3, charge glow (3 levels), charged release, spin, dodge (roll / blink / slide per heir), sprint-slide, dash-cancel strike, parry, perfect parry, hurt, death, pickup, door exit, fall into void |
| Mobs | idle, move, tell, attack, recover, hurt flash, death; boss: per phase, plus a phase transition |
| Items | tokens spin and glint; keycards bob; CRTs flicker → wobble → shatter; foam crumbles; drywall cracks → breaks; doors locked → hum → open; the shop idles; talker faces blink and mouths move while text types |
| Tiles | palette-animated ambient: water, LEDs, neon, fluorescent buzz, void twinkle |

`anim.png` shows the player plan: 5 drawn directions plus 3 mirrored, walk, idle, combo, charge glow levels, roll,
blink, hurt and death. The final frames are drawn per state and baked by the art tool.

---

## 12. Text and screens (folded in from the M2 notes)

**Text** uses EGRESS's own font (M2's, with its drop shadow) and 20-column rules:
- names ≤ 9 characters;
- HUD messages ≤ 18;
- talk lines ≤ 18 with a 1-row margin;
- descriptions never on the same row as names (the M2 trade overlap);
- one blank row between a portrait and its text (the M2 landing squeeze).

Every text screen gets a layout test in the harness, which renders every string in its box and fails on overflow.

**Screens:**
- title (section 15);
- heir pick;
- tree (the lineage);
- bench;
- book (the recipes you know, with margins for fragments);
- landing (talker plus trader);
- pause (resume, book, tree, setup, leave);
- setup (4/8-way, music and SFX volume, palette assist);
- ending (the question).

**Mid-run continue:** the run is saved at every stage entry (SRAM records above), and continuing resumes at that
stage's assembly.

---

## 13. Balance targets (folded in)

- **Boss:** 20–25 good hits over 3 phases; mid-boss: 10–12.
- **Hearts** start at 4; the SMALL trait costs 1; a heart shard is found every 2 floors.
- **Prices:** tune so that a full-clear run can buy 2 of 3 shop items.
- **Bot runs without invincibility.** A harness bot plays generated runs and reports the floor reached, the deaths
  by cause, and the tokens over time; prices and hearts are tuned from it.
- **Build spread:** no archetype above ≈ 60% clear rate in the sim (the same bar CRUCIBLE used), none useless.

---

## 14. Music and SFX: one semi-generative system

**PROOF.** `proto/music/egm.c` is a real cartridge driver and `compose.py` writes its data. A test ROM plays them
on an emulated CGB, and the previews in `docs/audio/` (16 MP3s) are recordings of that ROM.
- **Moods:** 9 (calm landing, uneasy explore, tension, combat, boss, collapse/chase, dream, glitch rift, ending),
  each with its BPM, mode and layers per intensity 0–3.
- **Biomes:** 8, each with its own key, wave/bass timbre, lead, 4-sound kit, echo and a four-note motif. The motif
  answers phrases in any mood, so a floor is recognisable.
- **Semi-generative scheme:**
  - authored chord progressions, phrase forms and grooves per mood;
  - notes are chord-relative, so harmony is data;
  - the seed picks the progression, the grooves and a lead transform per phrase, from a music-only RNG stream so
    gameplay stays deterministic;
  - `egm_intensity()` adds and removes layers by game state (more kit in combat, the pad in calm, no bass in
    collapse).
- **Transitions are on the bar:**
  - XFADE: the old song's tails fade while the new song takes its channels;
  - DROP: the combat→collapse bass dropout;
  - BEAT: for stingers.
- **SFX in the music's key:** tonal effects resolve against the current key and chord (pickups arpeggiate the bar's
  chord). Effects borrow pulse 1 (tonal) or noise (impacts) by priority (step < pickup < key/hit/door < hurt). The
  rest of the music ducks by about 2.5 dB and recovers. MUSIC and SFX volumes run 0–8 with fades.
- **Budgets:**
  - data 3.8 KB, code about 10 KB, WRAM 368 B;
  - CPU (measured): mean 5.3–7.5% of a double-speed frame, worst 21–31% on song-change frames;
  - the target is ≤3% mean with no dropped frames: move state to globals, write the hot paths in assembly, and
    spread song changes over two frames.
- **Levels:** no clipping; peaks −6.8 to −12.3 dBFS. The spectral centroid is 358–877 Hz with 1–2.6% of the energy
  above 5 kHz, which is warmer than or equal to CRUCIBLE's current recordings.

**Owner decisions (2026-10-14), recorded, not yet applied** (the previews predate them, and the owner is
listening):
- combat and boss lean toward CRUCIBLE's trailer groove (kicks on pulse 1, harmony thins in fights);
- the key is per biome;
- in calm rooms, SFX may borrow the lead so the pad never breaks;
- the driver runs in the main loop with frame catch-up, and VBlank stays free;
- hot paths in assembly to reach ≤3%;
- the hardware click/thump check comes later.

**Previews:**
- one per mood: `mood-*.mp3`, plus two seeds of explore/vents;
- transitions: explore→combat, combat→collapse (bass dropout), calm→tension (layers);
- `sfx-over-music_explore_office.mp3` (in-key effects with ducking);
- `volume-fades_explore_office.mp3`.

Full design: `proto/music/DESIGN.md`.

---

## 15. PRESS START title and rare first-person stages

**Title.**
- A slow, seeded backrooms walk, rendered by the same first-person renderer as the rare FPS stages:
  - pastel walls with posters and nostalgic objects (EGRESS's objects as wall decals);
  - fluorescent lights that blink and buzz (palette flicker plus the buzz SFX);
  - corners turned and random doors found.
- It's different every boot.
- The EGRESS.EXE logo and PRESS START sit above and below the viewport (a glitch-in neon logo,
  blinking prompt).
- See `mock_title.png`.

**Rare FPS stages.**
- A Wolfenstein-style raycaster on the CGB, as a rare stage type in the deck: exploration or escape, light combat,
  find the egress.
- The maze is generated by the same world generator.
- The same pieces and reactions, the same weapon and the same chat matrix apply.
- **Feasibility is measured first** (prototype in `proto/raycast/`). The results:

**Method.** The prototype is in `proto/raycast/`. It was measured in PyBoy over about 760 frames of the same
scripted walk per configuration, with frame timing counted in scanlines (154 per frame).
- CGB double speed; one ray per wide column, fixed-point DDA, 1024-angle tables, 67.5° field of view.
- Textured 8x16 walls, doors, distance dither, one billboard figure.
- A per-tile-row BG palette gradient for the ceiling and floor.
- The hot loops are hand-written assembly.
- Double buffering: a GDMA copy to the hidden VRAM bank, then one LCDC bit flips between two BG maps.
- A host build of the same C matched all 40 framebuffer snapshots byte for byte (an SDCC miscompile check).

| View | Column | fps min / avg | Cast + fill + sprite (scanlines) |
| --- | --- | --- | --- |
| **96x64** | 4 px | **29.9 / 32.8** | 115 + 126 + 22 |
| 112x72 | 4 px | 19.9 / 27.4 | 132 + 160 + 25 |
| 128x80 | 4 px | 19.9 / 23.6 | 152 + 193 + 26 |
| 96x64 | 2 px | 14.9 / 17.3 | 225 + 254 + 32 |
| 128x80 | 2 px | 9.9 / 12.3 | 303 + 386 + 38 |

**Verdict: viable at 96x64 with 4 px columns, at a steady 30 fps** (locked to every second frame). The edges are
chunky but the full 1 px vertical resolution keeps it readable.
- **FPS stages:** use 96x64 at 30 fps, with the HUD around it.
- **Title:** can use 128x80 at about 24 fps, because it is a slow walk.
- **Cost:** 2 px columns cost twice as much and are not viable.
- **Caveats:**
  - only tested in PyBoy; it needs a hardware check;
  - PyBoy treats DMA as free, so the ROM pads the equivalent time;
  - the two framebuffers need 3–5 KB of banked WRAM;
  - some tables need a planned ROM spot.

Shots: `proto/raycast/shots/ray_*.png` and `ray_walk.gif` (4.4 s walk). The title mock frames a real 128x80
render.

**FPS stage design (rare deck entry).**
- **The maze:** generated by the world generator (31x31 cells, loops, doors at dead ends).
- **Goal:** find the egress, under a light threat (one or two billboard mobs, which react through the same tags).
- **Bench:** the same pocket and bench (the overlay pauses the view).
- **Lights:** some lights are off (a GLOWS item lights a cone).
- **Collapse:** the corridor collapsing behind you is shown as walls closing (map cells flipping to solid).
- **Frequency:** at most one per run, never on floor 1.

---

## 16. Boot

EGRESS uses the **same KEEL startup as CRUCIBLE**, ported exactly (art, palette, animation, timing, sound, power
light). Proof: `docs/shots/boot_compare.png`, a frame-by-frame PyBoy comparison.

**PROOF, done.** CRUCIBLE's cartridge has no start-up of its own. Its KEEL start-up is the handheld's, on the
crucible.onkeel.io site (`boot.ts`, the chime in `sound.ts`, the mark `keel-mark.png`). That is what was ported,
into `src/boot/keel_boot.c` (tables generated by `proto/boot/keel_boot_ref.py`). The sequence:
1. A light screen. The KEEL glyph over "KEEL" scrolls down to the centre in whole-pixel steps (1.5 s).
2. It lands with the two-note chime (C6 for 5 frames, then B6 for 44; pulse 1, 50% duty).
3. It holds for 0.7 s.
4. It dithers away into the title in 16 ordered steps (0.3 s).

**Comparison** (`proto/boot/compare.txt`, `docs/shots/boot_compare.png`):
- **Frames:** 150 of 150 frames are pixel-identical to the site's boot at Game Boy frame times.
- **Chime:** both chime notes are within 0.3% of the site's pitch, at the same frame (90). The loudness-envelope
  correlation is 0.945.
- **Difference:** the APU's volume steps make the second note fall silent after 550 ms instead of the site's
  808 ms. The site ramps it linearly.
- **Power light:** it lives in the site's handheld shell (`package-scene.ts`: on at power-on, flickers 1,0,1,1,0,1
  in 25 ms steps, steady after 150 ms). A GBC cartridge has no LED it can drive. The flicker timing is recorded in
  the port's reference so a shell or website can reproduce it.
- **Build:** the boot is wired into `src/main.c`/`screens.c`; ROM 81,920 bytes after the gate.

---

## 17. Verification plan (what proves each part)

| Part | Proof |
| --- | --- |
| Rooms | the solver verifies every baked room: steps, branching, uniqueness, herring safety, trick necessity; `make test` re-solves all baked rooms |
| Stages | the headless stage sim at scale: every stage completable (its means present), difficulty monotonic-ish by depth, the type distribution per run, zero back-to-back repeats |
| Builds | the headless build sim (host build of the same C): clear-rate spread ≤ 60% per archetype, degenerate combos flagged |
| Mobs | the composer counts valid builds; the sim verifies every mob is killable with the means present and every attack has a tell |
| Input / movement | `host/test_input.c` and `host/test_move.c` with exact frame timings (**PROOF**, done) |
| Rendering | PyBoy harness: VRAM writes per VBlank logged, OAM per line counted, frame-time counters (no dropped frames in the worst-case scripted fight) |
| Audio | captured from the cartridge in PyBoy, mood and transition renders listened to |
| Save | SRAM layout round-trip tests, CRC corruption tests, lineage ring wrap |
| Boot | frame-exact comparison with CRUCIBLE |

## 18. Build order (after approval)

1. **Core:** input + movement (from the proofs), the room renderer with cards over the void, assembly/collapse, the
   VRAM budget.
2. **Puzzles:** pocket, bench, recipe subset, trait verbs, the backwards generator + solver, the bake.
3. **The matrix:** pieces, the reaction table, weapons/mods, particles/FX/SFX.
4. **Mobs, elites, conditions, stage types, biomes**; the stage sim.
5. **Heirs, lineage, save, tree screen**; story fragments and the truth matrix.
6. **Bosses.**
7. **Music system.**
8. **Title + FPS stages** (if the raycaster numbers hold up).
9. **Balance passes** with the bot.

Each milestone ships with its harness checks and screenshots.

The uncommitted M2 puzzle-feature edits (crumble, plates, gates, pits in `eg_room`) are kept as the physical layer.
They'll be committed with a rebake when step 2 starts.
