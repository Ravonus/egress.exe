# EGRESS.EXE super matrix: prototype results

Design-stage proof of the piece / reaction / mob / stage-deck / treasure systems, plus an abstract balance model.
All code is portable C99 in the SDCC style (uint8_t/int16_t, const tables, no floats, no malloc, seeded and
deterministic). Only `sim.c` and the build model are host-only.

```
cd proto/matrix
python3 bred_to_txt.py          # only when ../genetics/out/bred_lines.json changes
make && ./mxsim 10000 32 > report.md   # 10,000 world runs, 32 combat runs per build (~5 min, one core)
```

| file | what |
|---|---|
| `mx.h` | all types: 5-byte piece, tags, reactions, mobs, bosses, stages, treasure, run |
| `mx_core.c` | rng, stacking (diminishing returns + soft/hard caps), the reaction matrix and its tile use |
| `mx_mob.c` | mob axes, compatibility rules, elites, boss templates, kill-rule means, names |
| `mx_world.c` | biome tables, stage deck, fairness guarantee, treasure placement, CHAT/WORLD coupling |
| `mx_build.c/.h` | player builds (weapon, mods, heir traits, dodge) and the abstract combat model |
| `sim.c` | host driver; prints every table below |
| `bred_to_txt.py`, `bred_lines.txt` | flattens the genetics prototype's bred heirs for the sim |

## Headline numbers (10,000 seeded runs; all 250,614 player builds)

- **Mobs:** 46,464 raw axis combos, **15,981 valid** after the rules; 883,325 elite builds; 2,415,192 valid boss
  templates. A run meets ~77 distinct mob builds (min 65, max 93) with a **0.2% repeat rate** (a 24-pick history
  window); 10,000 runs touch 100% of valid mob builds, 132,591 distinct elites and 47,722 distinct bosses.
- **Fairness:** 86% of stages need a specific means. Before the guarantee step 65.1% would be unwinnable from the
  room alone (49.3% even with a random player kit); **after: 0**. No main path depends on a treasure-only source (0).
- **Decks:** **0 back-to-back repeats**; the same type two stages apart 2.5%; a trick repeats within 6 trick rooms in
  0.6% of rooms (forced only when every candidate was recent; 105 DARK/MIX rooms were converted to puzzle rooms
  instead). The difficulty climbs every floor once the mid-boss saw-tooth is removed (27 -> 52 mean), and with heir
  generation (40 -> 52).
- **Treasure:** 1.5 patches per stage, 76 per run; 44% hidden (a third of those hinted); **15.3% trait-gated**; reach
  cost vs tier r = 0.94; **16% unreachable before the placement guarantee, 0 after**.
- **Builds:** calibrated field clear rate 40.9%; every archetype is between 34.8% (ARCANE) and 47.8% (MIRROR), every
  mod and trait between 32% and 52%, and every weapon between 36% and 45%. 37 of 250,614 builds clear >= 90%; none clear
  <= 3%. The tuning loop needed no cap change after the data fixes listed below; diminishing returns cut the >= 90%
  builds from 0.2% to 0.0% and the < 10% builds from 2.4% to 1.3%.
- **Bred heirs (final, adopted):**
  - The reference is the doc-defined trait model plus the 40-point stat budget, with HEAVY softened to -10% speed and a
    staggering B->A dodge-cancel lunge (stagger 5). Enemies stay on the base-build balance (K=42).
  - Clear rates: random 56.1%, heavy 56.8% (+0.7), blink 58.7% (+2.6), fire 59.8% (+3.7), power 58.5% (+2.4); all heirs
    58.0%.
  - No strategy is more than +3.7 points over random, and every strategy is under 60%. Fire is the closest at 59.8%.
  - History: 84-90% before the stat budget; about 65% with the budget and the three costs (heavy +8.1); 52% with the doc
    traits but HEAVY at -20% speed (heavy -9.4).

## What the sims found and what changed

1. **Lifesteal bypassed stacking.** MOSS GRIP cleared 71% because its heal was added outside the stacks and also
   counted as armour. Fix: ALIVE-tagged DEFENSE pieces only heal, capped at 8 HP per room (`mx_heal_cap`). It now
   sits at 50.9%, the strongest mod but inside the band.
2. **Avoid is the strongest axis in this model.** TINY (+14 avoid) was at 68%. It is now +8 avoid and -20% damage
   (52%). BRITTLE, GIANT and SHINY TEETH sit at 49-51%. Keep an eye on avoid stacking in the real game.
3. **Weak pieces were raised as data, not caps:** BLADE dps 10->11 with reach 2, GAUNTLET 10->11, WAND/DISC reach
   12/10 -> 9/8, GRAVEL CORE 40->60, KILN 50->70, WHETSTONE 25->35, SLEEPWALKER 30->40, BERSERK 60->80 with its cost
   cut to 30% less healing. The weakest pieces are now around 32-35%, and the weakest mod pair (GRAVEL CORE +
   WHETSTONE) clears 25.7%.
4. **Caps:** the shipped soft caps are DAMAGE 90, STATUS 70, AREA 80, PROJ 80, DEFENSE 45, SPEED 40, SUMMON 60 and
   REACT 60 (hard stop x1.5). The auto-tuner, which lowers a group's cap when an archetype, mod or trait goes over 60%
   and raises it when one falls under 20%, found nothing to change.
5. **Bred lines (adopted):**
   - The genome stats share one 40-point budget (genes.py), and all ten genetic traits are modelled as the redesign
     doc defines them, with real costs for REMEMBERS, COLOURBLIND and ALLERGIC.
   - HEAVY is -10% speed (-5 avoid points) with a staggering dodge-cancel lunge. The stagger sweep (0/3/5/8/12) puts
     heavy at -3.8/-1.3/+0.7/+3.2/+6.7 against random. 5 was chosen: the smallest gap with a real upside.
   - Fire sits closest to the 60% line (59.8%). If affinities ever get stronger, halving the affinity magnitude
     (10 -> 5 per level) is the lever: about -4 points across the board.
6. **Boss rejection sampling** gives up after 2,000 draws about once in 10,000 runs. A deterministic walk of the
   template space supplies a valid template instead, so 0 boss rooms are invalid.

## Honest limits

- The combat model is an abstract budget (time-to-kill x incoming damage x avoid/defence/control) with skill noise. It
  has no positioning, no frame data and no AI. It finds runaway stacks and dead pieces; it does not predict how the
  game feels.
- Completability is a proxy: every needed means (tag source, crate/pit, projectile, light) is present in the stage.
  Puzzle rooms still need the real solver (`host/eg_solve.c`) on the generated grid.
- Treasure reachability uses the same means proxy. Trait gates are taken as reachable with the trait.
- The coupling is simple and measurable but weak: hated CHARGE raises SHOCK/HOT enemies from 17.3% to 26.8%, a DREAM
  leaning raises DREAM GARDEN visits from 46.7% to 61.4%, and liked WATCH raises heists from 6.1% to 7.2%.

---

## 1. Piece schema

Every piece (weapon base, mod, heir trait, dodge, mob part, affix, boss part, element) is one 5-byte rule.

| byte | bits 7..4 | bits 3..0 |
|---|---|---|
| b0 | trigger (12 used) | effect (8 used) |
| b1 | tag A (15 = none) | tag B (15 = none) |
| b2 | magnitude 0..255 (percent-ish units) | |
| b3 | argA: count / period s / radius | argB: duration (1/4 s) |
| b4 | kind:3 (bits 7..5) group:3 (4..2) | cap class:2 (1..0; 3 = COST) |

Triggers: 0=on hit, 1=on dodge, 2=on parry, 3=on kill, 4=on charge release, 5=on low HP, 6=on push, 7=on recipe made, 8=every N s, 9=on hurt, 10=on enter tile, 11=passive

Effects: 0=projectile, 1=area, 2=status, 3=terrain change, 4=stat change, 5=summon, 6=trail, 7=beam

Kinds: 0=weapon base, 1=weapon mod, 2=heir trait, 3=dodge style, 4=mob part, 5=elite affix, 6=boss part, 7=element

Tags: 0=HOT, 1=COLD, 2=WET, 3=SHOCK, 4=SHINY, 5=ALIVE, 6=MAGIC, 7=GLITCH, 8=STONE, 9=AIRY

Stacking: pieces in the same group are sorted by magnitude and weighted 16,12,8,6,4,3,2,2 /16 (diminishing), then a soft knee at the group cap (excess counts 1/4) and a hard stop at cap x1.5. Costs (cap class 3) subtract after stacking and never diminish.

Example encodings (weapon mods):

| piece | bytes | reads as |
|---|---|---|
| EMBER EDGE | 02 0F 1E 08 24 | on hit -> HOT status mag 30, group STATUS |
| FROST CORE | 41 1F 28 26 28 | on charge release -> COLD area mag 40, group AREA |
| TIDE HEM | 11 2F 1E 18 28 | on dodge -> WET area mag 30, group AREA |
| SPARK PLUG | 02 3F 1E 32 24 | on hit -> SHOCK status mag 30, group STATUS |
| MIRROR GUARD | 20 4F 32 20 2C | on parry -> SHINY projectile mag 50, group PROJ |
| SEED POUCH | 35 5F 28 1F 38 | on kill -> ALIVE summon mag 40, group SUMMON |

## 2. Reaction matrix (one table: combat and tiles)

|  | HOT | COLD | WET | SHOCK | SHINY | ALIVE | MAGIC | GLITCH | STONE | AIRY |
|---|---|---|---|---|---|---|---|---|---|---|
| **HOT** | FLARE | SHATTER | STEAM | OVERLOAD | LANTERN | BURN | CINDER | GLITCH | MELT | FIRESTORM |
| **COLD** | SHATTER | DEEPFREEZE | FREEZE | BRITTLE | ICE MIRROR | WILT | STASIS | GLITCH | CRACK | SNOW |
| **WET** | STEAM | FREEZE | FLOOD | SHORT | MIRAGE | OVERGROW | DISPEL | GLITCH | MUD | SQUALL |
| **SHOCK** | OVERLOAD | BRITTLE | SHORT | OVERCHARGE | LASER | SPASM | ARC | GLITCH | GROUND | THUNDER |
| **SHINY** | LANTERN | ICE MIRROR | MIRAGE | LASER | MIRROR | BLOOM | PRISM | GLITCH | GLINT | DAZZLE |
| **ALIVE** | BURN | WILT | OVERGROW | SPASM | BLOOM | TREE | FAMILIAR | GLITCH | ROOTBREAK | POLLEN |
| **MAGIC** | CINDER | STASIS | DISPEL | ARC | PRISM | FAMILIAR | SURGE | GLITCH | GOLEM | VORTEX |
| **GLITCH** | GLITCH | GLITCH | GLITCH | GLITCH | GLITCH | GLITCH | GLITCH | CRASH | GLITCH | GLITCH |
| **STONE** | MELT | CRACK | MUD | GROUND | GLINT | ROOTBREAK | GOLEM | GLITCH | WALL | SANDSTORM |
| **AIRY** | FIRESTORM | SNOW | SQUALL | THUNDER | DAZZLE | POLLEN | VORTEX | GLITCH | SANDSTORM | GUST |

Symmetric: yes. Plus the context rule: SHINY + any projectile = REFLECT (a shot already reflected splits). GLITCH + x replays a seeded reaction from x's row (never GLITCH again, so no recursion) and adds a 1-frame stutter.

| reaction | combat | dmg x | ctrl % | dur s | tile it converts | tile result |
|---|---|---|---|---|---|---|
| STEAM | blind | 1.12 | 45 | 3.00 | water | water boils to a blinding cloud; vents jam |
| SHATTER | burst | 2.50 | 0 | 0.00 | ice | ice path melts; frozen foes burst x2.5 |
| FREEZE | root | 1.00 | 70 | 2.00 | water | water freezes into a sliding ice path |
| SHORT | stun | 1.25 | 80 | 1.50 | water | water carries the shock; powers nodes, chains stun |
| OVERGROW | root | 1.00 | 60 | 2.50 | pit | vines bridge a pit or water; foes rooted |
| BURN | damage over time | 1.50 | 10 | 4.00 | vines | vines/hedges burn away; spreads to ALIVE |
| MELT | armour break | 1.37 | 0 | 3.00 | cracked wall | cracked wall melts to cooling lava (opens later) |
| FIRESTORM | damage over time | 1.62 | 20 | 2.50 | vent | vent throws a fire jet; pushes |
| OVERLOAD | burst | 2.25 | 0 | 0.00 | power node | node blows: door opens, blast radius 2 |
| LANTERN | light | 1.00 | 15 | 8.00 | mirror | mirror heats into a light source; reveals |
| CINDER | burst | 1.87 | 0 | 0.00 | floor | delayed mark explodes after 1 s |
| DEEPFREEZE | root | 1.00 | 90 | 4.00 | water | hard freeze; stops reforming foes |
| BRITTLE | armour break | 1.75 | 0 | 3.00 | ice | ice conducts; next hit on target crits |
| ICE MIRROR | reflect | 1.00 | 20 | 4.00 | floor | raises a reflective ice pane |
| WILT | slow | 1.00 | 35 | 4.00 | vines | vines go brittle and break; stops regrowth |
| STASIS | stun | 1.00 | 50 | 2.00 | floor | orbs in flight stop; platform freezes |
| CRACK | armour break | 1.25 | 0 | 3.00 | wall | frost cracks a wall (breakable) |
| SNOW | reveal | 1.00 | 30 | 6.00 | floor | snow slows and shows invisible footprints |
| FLOOD | slow | 1.00 | 20 | 4.00 | pit | water spreads; fills a pit |
| MIRAGE | confuse | 1.00 | 40 | 3.00 | water | puddle reflects light; decoy draws attacks |
| DISPEL | dispel | 1.00 | 25 | 4.00 | rift | strips a shield/affix for 4 s; closes rifts |
| MUD | slow | 1.00 | 35 | 4.00 | floor | ground turns to mud; burrowers surface |
| SQUALL | push | 1.12 | 25 | 1.00 | crate | wave pushes crates one cell |
| OVERCHARGE | chain | 1.37 | 30 | 1.50 | power node | node stays powered longer; +1 chain |
| LASER | burst | 2.12 | 0 | 0.00 | mirror | mirror relays a piercing beam |
| SPASM | stun | 1.12 | 60 | 1.00 | vines | vines twitch open for 2 s; foes interrupted |
| ARC | chain | 1.62 | 30 | 1.50 | floor | chain hits 3 targets |
| GROUND | nothing (null) | 0.75 | 0 | 0.00 | wall | stone eats the shock (anti-synergy) |
| THUNDER | stun | 1.37 | 55 | 1.50 | vent | stun ring radius 2 |
| MIRROR | split shot | 1.50 | 0 | 0.00 | mirror | double reflect; shots split in 2 |
| BLOOM | heal | 1.00 | 0 | 4.00 | vines | light grows vines into a bridge; heal pulse |
| PRISM | split shot | 1.62 | 0 | 0.00 | mirror | shot splits into 3 |
| GLINT | reflect | 1.00 | 10 | 0.00 | wall | polished wall becomes a reflector |
| DAZZLE | confuse | 1.00 | 45 | 2.50 | floor | foes swing at each other |
| TREE | wall | 1.00 | 30 | 8.00 | floor | sprouts a tree: cover / blocker |
| FAMILIAR | summon ally | 1.25 | 15 | 10.00 | floor | a small ally sprite fights 10 s |
| ROOTBREAK | armour break | 1.50 | 0 | 3.00 | wall | roots crack stone |
| POLLEN | stun | 1.00 | 65 | 3.00 | floor | sleep cloud; breaks on hit |
| SURGE | burst | 1.62 | 0 | 0.00 | rift | magnitude up; rift widens |
| GOLEM | summon ally | 1.12 | 25 | 8.00 | crate | animates a crate: it walks 3 cells |
| VORTEX | pull | 1.12 | 35 | 1.50 | crate | pulls foes/crates together |
| WALL | wall | 1.00 | 25 | 8.00 | floor | raises rubble (pushable) |
| SANDSTORM | blind | 1.12 | 35 | 3.00 | floor | blind + chip damage |
| GUST | push | 1.00 | 30 | 1.00 | crate | strong knockback; slides crates to a wall |
| GLITCH | random | 1.50 | 20 | 1.00 | floor | plays a seeded reaction of the partner tag + 1 frame stutter |
| CRASH | stun | 1.00 | 70 | 2.00 | floor | room stutter: all foes freeze 2 s, two tiles swap |
| FLARE | damage over time | 1.37 | 0 | 4.00 | lamp | burn stacks deeper; lamp flares |
| REFLECT | reflect | 1.25 | 0 | 0.00 | mirror | SHINY + any projectile: shot reflects (or splits if already reflected) |

48 reactions over 55 unordered tag pairs.

Tile reactions from the same table (tile's intrinsic tag x incoming tag):

| tile | + tag | reaction | becomes |
|---|---|---|---|
| water | COLD | FREEZE | ice |
| water | HOT | STEAM | steam |
| water | SHOCK | SHORT | (same tile, effect only) |
| ice | HOT | SHATTER | water |
| vines | HOT | BURN | floor |
| pit | ALIVE | OVERGROW | plant bridge |
| wall | COLD | CRACK | cracked wall |
| cracked wall | HOT | MELT | lava |
| mirror | HOT | LANTERN | lit |
| mirror | SHOCK | LASER | (same tile, effect only) |
| wall | SHINY | GLINT | mirror |
| power node | HOT | OVERLOAD | floor |
| vines | SHINY | BLOOM | plant bridge |
| rift | WET | MUD | mud |
| vent | HOT | FIRESTORM | (same tile, effect only) |
| crate | AIRY | - | (same tile, effect only) |
| floor | STONE | WALL | crate |
| water | ALIVE | OVERGROW | (same tile, effect only) |

## 3. Mob composer

Axes: 11 bodies x 8 attacks x 8 defences/kill rules x 11 elements (10 tags + untagged) x 6 behaviours = 46464 raw combos.

**Valid after compatibility rules: 15981 (34.4% of raw).** Elite variants (1 affix): 199343; (2 affixes): 683982; total elite builds: 883325.

Distinct generated names over all valid mobs x 8 biomes: 2504 (names are short CRUCIBLE-voice pairs, so they repeat across builds by design; the build, not the name, is the identity).

| body | valid builds | | defence | valid builds | rule (means) |
|---|---|---|---|---|---|
| walker | 2715 | | plain | 3038 | - |
| flyer | 1725 | | armoured front | 1760 | flank (base dodge) or pierce |
| phaser | 1764 | | shield: wet then shock | 2126 | needs WET + SHOCK |
| burrower | 1098 | | crush only | 1483 | needs crate/boulder or pit (CRUSH) |
| wall-crawler | 2024 | | splits on death | 2647 | AoE helps; splits capped at 2 gens |
| gravity-bound | 2337 | | reforms unless frozen | 2596 | needs COLD |
| heavy | 912 | | reflected shots only | 655 | needs an enemy projectile (PROJ) + parry |
| teleporter | 1716 | | invisible in dark | 1676 | needs LIGHT (lamp, HOT or SHINY) |
| swarm | 391 | | | | |
| turret | 576 | | | | |
| mimic | 723 | | | | |

Attack tells (frames @60 Hz; minimum readable = 12; hasted x0.75 must stay >= 12): lunge 18, projectile 16, slam/ring 28, beam 36, trail hazard 20 (unlit), summon 30, grab/pull 18, terrain change 32 (unlit).

Behaviours: coward/kiter, aggressive, flanker, guard, pack leader, thief.

Elite affixes (max 2, max 1 aura):

| affix | rule |
|---|---|
| shielded | shield = 3 hits; not on wet/shock-shield mobs |
| vampiric | heals 25% of damage dealt; not on reformers |
| splitting | splits once on death; not swarm/splitter/reformer |
| hasted | x0.75 tell; refused if tell would drop under 12 f |
| teleport-on-hit | blinks 3 cells on hit; not armoured/turret/mimic |
| collapse trail | drops a crumble trail; ground movers only, not in chase stages |
| element aura (x10) | not its own element; never one that solves or contradicts its kill rule (e.g. no WET aura on wet-then-shock, no HOT aura on reformers, no SHINY aura on dark-invisible); never GROUND-cancelling |
| pairs | no vampiric+shielded, hasted+teleport-on-hit, splitting+vampiric |

Sample generated mobs:

| name | biome | body | attack | defence | element | behaviour |
|---|---|---|---|---|---|---|
| LIGHT CHAIR | OFFICE | turret | projectile | reflected shots only | AIRY | guard |
| STATIC FAN | VENTS | mimic | lunge | crush only | SHOCK | guard |
| FURRY PLANTER | FLOODED ATRIUM | burrower | trail hazard | shield: wet then shock | ALIVE | flanker |
| TOASTY DRIVE | SERVER FARM | burrower | summon | splits on death | HOT | pack leader |
| WAXED NAPKIN | FOOD COURT | wall-crawler | lunge | splits on death | SHINY | guard |
| FIRM TULIP | DREAM GARDEN | heavy | summon | armoured front | STONE | pack leader |
| ZAPPY VANE | ROOFTOP SKY | walker | projectile | plain | SHOCK | thief |
| CHILLY PIXEL | GLITCH CORE | burrower | lunge | shield: wet then shock | COLD | flanker |
| GRABBY STAPLE | OFFICE | walker | projectile | splits on death | HOT | thief |
| MINTY FILTER | VENTS | walker | lunge | invisible in dark | MAGIC | thief |
| WAXED PLANTER | FLOODED ATRIUM | teleporter | projectile | shield: wet then shock | SHINY | flanker |
| DENSE CABLE | SERVER FARM | gravity-bound | summon | splits on death | STONE | flanker |

### Boss templates

Parts: core (4) x 3 ordered phase attacks (distinct) x 3 ordered phase weak points (distinct; plain/splits not allowed) x arena hazard (6) x element pair (ordered, primary first). Raw (after distinctness): 270950400. **Valid: 2415192.** Per core: COLOSSUS 599472, HIVE 1046340, ENGINE 608580, SPECTER 160800.

Rules: each phase's tell x(8-phase)/8 >= 12 f; each phase must be a valid mob for the core's body class; HIVE needs a summon phase; ENGINE never lunges; no GROUND-cancelling element pair; dark weak point only with the darkness hazard; no crush weak point in flooding arenas; no COLD element over rising lava. Mid-bosses use the first 2 phases.

## 4. Headless simulation: 10000 seeded runs (heir generation = run % 10, random starting faction standing, one random story leaning)

Determinism: same seed generated twice -> hash 9E41D0CA / 9E41D0CA (identical).

### 4a. Variety

| | per run (mean) | min | max | repeat rate within a run | distinct across all runs | possible |
|---|---|---|---|---|---|---|
| mob builds (base) | 77.1 distinct of 77.3 picks | 65 | 93 | 0.2% | 15981 (100.0% coverage) | 15981 |
| elites | 18.1 distinct of 18.1 elite picks | 5 | 34 | 0.0% | (see below) | 883325 |
| bosses + mid-bosses | 5.0 per run | | | 0.0% | | 2415192 |

Distinct elites seen across all runs: 132591; distinct boss/mid-boss templates seen: 47722. Mob generator rejection rate (invalid combos resampled): 61.4% of 6943475 draws.

### 4b. Fairness (kill/win means present in the stage)

| | stages | % of non-rest stages |
|---|---|---|
| stages needing a specific means | 356628 | 86.1% |
| FAIL before guarantee (room only) | 269644 | 65.1% |
| FAIL before guarantee (room + a random player kit) | 204448 | 49.3% |
| **FAIL after guarantee** | **0** | 0.0% |
| boss rooms without a valid template | 0 | |
| main path needing a means only treasure placement added | 0 | |

Guarantee step placed 406189 source tiles and 11590 pickups (MAGIC has no tile, so it is always a pickup). Missing means before the guarantee, by kind: HOT 31721, COLD 96479, WET 75870, SHOCK 63144, SHINY 25807, ALIVE 2418, MAGIC 461, GLITCH 4783, STONE 16376, AIRY 13291, CRUSH 27813, LIGHT 56657, (stages can miss several).

### 4c. Stage decks

50 stages per run on average (10 floors x 4 + 9 landings + optional rifts).

| stage type | per run | share | win condition |
|---|---|---|---|
| puzzle room | 5.88 | 11.7% | reach the egress door by changing tiles with the room's trick reaction (solver-verified) |
| holdout | 5.31 | 10.5% | survive N waves / T seconds on a shrinking floor |
| escort | 3.88 | 7.7% | walk the NPC to the door; NPC HP > 0 |
| chase/collapse | 4.61 | 9.1% | reach the exit before the floor collapses behind you |
| hunt | 4.17 | 8.3% | kill the marked target (conditional kill rule) |
| mid-boss | 4.00 | 7.9% | break a 2-phase guardian |
| boss | 1.00 | 2.0% | break a 3-phase boss (each phase its own weak point) |
| heist/trade | 3.20 | 6.4% | leave with the item without the alarm, or trade it |
| dark room | 4.14 | 8.2% | light the exit: braziers (HOT) or reflect light (SHINY); invisible foes |
| mix under pressure | 3.77 | 7.5% | make the named recipe at the pedestal while waves attack |
| rest/landing | 9.00 | 17.8% | none: heal, trader, chat (truth matrix) |
| glitch rift | 1.43 | 2.9% | optional: close the rift (DISPEL/kill core) for a glitch mod |

Back-to-back repeats of a stage type: **0**. Same type two stages apart: 12788 (2.5%). Trick (the room's key reaction / recipe / dark-room method) repeated within the last 6 trick rooms: 951 of 151659 (0.6%). When every method for a DARK/MIX/RIFT room was recent, the room became a puzzle room instead: 105 times; still forced to repeat: 951.

Boss generator: 13 templates came from the deterministic fallback walk (rejection sampling gave up after 2000 draws); 0 invalid.

Modifiers: - 70.6%, flooded 1.6%, dark 10.3%, windy 3.2%, overgrown 1.6%, glitchy 6.9%, heat 3.4%, collapsing 2.4%.

Difficulty score = solver-steps estimate x3 + enemy budget spent + conditions x4 (rests excluded).

| floor (depth) | mean | min | max | mean without boss/mid-boss rooms |
|---|---|---|---|---|
| 1 | 27 | 13 | 56 | 27 |
| 2 | 37 | 13 | 98 | 28 |
| 3 | 33 | 14 | 69 | 33 |
| 4 | 45 | 14 | 111 | 35 |
| 5 | 37 | 15 | 80 | 37 |
| 6 | 52 | 17 | 131 | 40 |
| 7 | 43 | 17 | 98 | 43 |
| 8 | 59 | 18 | 142 | 45 |
| 9 | 50 | 23 | 106 | 50 |
| 10 | 75 | 24 | 203 | 52 |

(Even floors end in the biome's mid-boss, which is the saw-tooth in the first column; the room curve without it rises every floor.)

| heir generation | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| mean difficulty | 40 | 41 | 42 | 43 | 45 | 46 | 48 | 49 | 51 | 52 |

Completability proxy: every stage's required means are present after the guarantee (above). Puzzle rooms are only checked for the means of their trick reaction here; the real BFS solver in host/eg_solve.c still has to verify each generated grid at generation time.

Biome run order (OFFICE first, GLITCH CORE last, middle three drawn by story leaning and grudges):

| slot | OFFICE | VENTS | FLOODED ATRIUM | SERVER FARM | FOOD COURT | DREAM GARDEN | ROOFTOP SKY | GLITCH CORE |
|---|---|---|---|---|---|---|---|---|
| 1 |100.0% |0.0% |0.0% |0.0% |0.0% |0.0% |0.0% |0.0% |
| 2 |0.0% |16.5% |16.1% |17.0% |17.0% |16.6% |16.8% |0.0% |
| 3 |0.0% |16.7% |15.5% |17.1% |17.1% |17.1% |16.5% |0.0% |
| 4 |0.0% |16.9% |15.9% |16.8% |17.0% |16.9% |16.5% |0.0% |
| 5 |0.0% |0.0% |0.0% |0.0% |0.0% |0.0% |0.0% |100.0% |

### 4d. Treasure placement

Placed after the main path from its own RNG stream (seeded from the stage, so the main path's generation is byte-identical with or without treasure). Budget per stage = 2 + floor/2 + type bonus (rest = 1); a patch costs tier+1.

Patches: 1.51 per stage, 76.2 per run (min 59, max 101). Hidden: 44.2% (of those hinted 32.9%). Trait-gated: 15.3% (SMALL (vent) 2.6%, HEAVY (two-block push) 9.1%, BLINK (gap) 3.5%).

| tier | share | mean reach cost |
|---|---|---|
| T0 | 21.4% | 5 |
| T1 | 29.4% | 9 |
| T2 | 28.9% | 13 |
| T3 | 20.3% | 18 |

Reach cost vs reward tier: Pearson r = 0.943 (tier is a step function of cost, so r < 1 only from the step edges).

| kind | share | | spot | share |
|---|---|---|---|---|
| tokens | 18.4% | | raised block | 18.8% |
| rare ingredient | 19.7% | | ledge over void | 7.3% |
| matrix piece | 15.7% | | crumble floor | 9.3% |
| lore fragment | 16.1% | | behind a hazard | 8.5% |
| lineage relic | 8.1% | | flyer route | 5.5% |
| cosmetic avatar part | 10.9% | | assembling/collapsing card | 6.4% |
| cursed item | 11.1% | | under foam/rubble/CRT | 16.7% |
| | | | in drywall | 10.4% |
| | | | crumbling patch | 7.6% |
| | | | under water (freeze/drain) | 9.5% |

Needs: recipe 2.9%, reaction 32.6%, collapse 14.0%, timed 15.7%. **Unreachable before the placement guarantee: 121832 (16.0%); after: 0.** Gated patches count as reachable with their trait; the means check is the same proxy as 4b.

### Coupling (CHAT/truth -> WORLD/FUNCTION)

- CHARGE hated (< -30): 26.8% of enemies are SHOCK/HOT; CHARGE liked (> 30): 17.3%.
- WATCH liked: heists are 7.2% of stages; WATCH hated: 6.1%.
- DREAM leaning runs visit DREAM GARDEN 61.4% of the time; other runs 46.7%.

### 4e. World tables

| biome | faction turf | top body | top element | top stage | modifier |
|---|---|---|---|---|---|
| OFFICE | WATCH | walker | untagged | puzzle room | dark |
| VENTS | CHARGE | wall-crawler | AIRY | chase/collapse | windy |
| FLOODED ATRIUM | ROOT | flyer | WET | puzzle room | flooded |
| SERVER FARM | COMPILE | turret | SHOCK | holdout | heat |
| FOOD COURT | LOCK | walker | HOT | heist/trade | heat |
| DREAM GARDEN | WATCH | flyer | ALIVE | puzzle room | overgrown |
| ROOFTOP SKY | SKY | flyer | AIRY | chase/collapse | windy |
| GLITCH CORE | COMPILE | teleporter | GLITCH | holdout | glitchy |

Factions own tags (hated -> those tags field more enemies; liked -> those tags appear as helpful room sources): COMPILE GLITCH/MAGIC, CHARGE SHOCK/HOT, SKY AIRY/COLD, WATCH SHINY/SHOCK, ROOT ALIVE/WET, LOCK STONE/COLD.

MIX recipes (CRUCIBLE's own), unlocked by depth (steps <= 1 + floor/3): STEAM (HOT+WET, 1), LAVA (HOT+STONE, 1), SNOW (WET+AIRY, 1), PLANT (WET+STONE, 1), METAL (HOT+STONE, 1), GLASS (HOT+STONE, 1), ENERGY (HOT+AIRY, 1), ICE (WET+AIRY, 2), TREE (WET+STONE, 2), BRICK (HOT+WET+STONE, 2), SWORD (HOT+STONE, 2), SHOVEL (HOT+STONE, 2), LANTERN (HOT+STONE, 2), MIRROR (HOT+STONE, 3), ELECTRIC (HOT+STONE+AIRY, 3), LASER (HOT+SHINY+STONE+AIRY, 4).

## 5. Builds and the abstract combat model

Build = weapon base (6) + 2 of 18 weapon mods + 2 of 14 heir traits (each an upside piece + a COST piece) + dodge (3) = 250614 builds; all are enumerated. Each build runs 32 fixed generated runs (same runs for every build). The model: time-to-kill = enemy HP pool (x kill-rule penalty when the build lacks the means and must use the room) / build DPS (with stacked bonuses and reaction bonuses); damage taken = enemy DPS x time x (1 - control) x (1 - avoid +/- skill noise) x (1 - defence) x K. Rests heal; player power grows per floor. It is an abstract budget model, not the game.

Design target: field clear rate 40%; every archetype within [20%, 60%] (none more than +50% relative / +20 points over the target, none under half the target = useless).

### Before: linear stacking, no caps (K=76)



| archetype (tag the build carries most) | share of builds | clear rate |
|---|---|---|
| FIRE | 11.3% | 41.0% |
| FROST | 5.1% | 39.4% |
| TIDE | 8.7% | 35.0% |
| STORM | 8.2% | 43.2% |
| MIRROR | 20.6% | 44.9% |
| GROWTH | 3.7% | 38.5% |
| ARCANE | 16.6% | 37.8% |
| GLITCH | 8.6% | 36.5% |
| STONE | 13.9% | 41.5% |
| WIND | 3.4% | 39.0% |
| **field** | 100% | 40.4% |
| (builds clearing >= 90%) | 0.2% | |
| (builds clearing < 10%) | 2.4% | |
| (mono-element: top tag >= 55%) | 14.8% | 36.5% |
| (hybrid: two or more tags) | 85.2% | 41.0% |

Starting soft caps: DAMAGE 90, STATUS 70, AREA 80, PROJ 80, DEFENSE 45, SPEED 40, SUMMON 60, REACT 60

- all archetypes, mods and traits inside [20%, 60%] after 0 step(s).

Tuned soft caps (hard stop = cap x1.5): DAMAGE 90, STATUS 70, AREA 80, PROJ 80, DEFENSE 45, SPEED 40, SUMMON 60, REACT 60

Lifesteal heal cap: 8 HP per room.

### After: diminishing returns + tuned caps, all 250614 builds (K=42)



| archetype (tag the build carries most) | share of builds | clear rate |
|---|---|---|
| FIRE | 10.8% | 39.4% |
| FROST | 5.2% | 39.8% |
| TIDE | 9.0% | 36.5% |
| STORM | 7.7% | 40.4% |
| MIRROR | 20.9% | 47.8% |
| GROWTH | 4.1% | 42.8% |
| ARCANE | 16.9% | 34.8% |
| GLITCH | 8.3% | 37.5% |
| STONE | 13.5% | 44.6% |
| WIND | 3.6% | 39.2% |
| **field** | 100% | 40.9% |
| (builds clearing >= 90%) | 0.0% | |
| (builds clearing < 10%) | 1.3% | |
| (mono-element: top tag >= 55%) | 15.3% | 40.4% |
| (hybrid: two or more tags) | 84.7% | 40.9% |

| weapon base | clear | | dodge | clear |
|---|---|---|---|---|
| BLADE | 36.3% | | ROLL | 42.1% |
| HAMMER | 39.0% | | BLINK | 41.9% |
| SPEAR | 44.5% | | SLIDE | 38.6% |
| DISC | 43.6% | | | |
| WAND | 43.9% | | | |
| GAUNTLET | 37.7% | | | |

| weapon mod | clear (builds with it) | | heir trait | clear |
|---|---|---|---|---|
| EMBER EDGE | 41.6% | | GLITCHED | 35.5% |
| FROST CORE | 42.9% | | WET HANDS | 34.8% |
| TIDE HEM | 39.7% | | HOTHEAD | 38.8% |
| SPARK PLUG | 41.0% | | BRITTLE | 51.0% |
| MIRROR GUARD | 47.2% | | GREEN THUMB | 38.8% |
| SEED POUCH | 44.5% | | SHINY TEETH | 49.2% |
| HEX INK | 38.2% | | GIANT | 51.4% |
| BAD SECTOR | 42.5% | | TINY | 52.3% |
| GRAVEL CORE | 34.9% | | FROSTBITTEN | 37.8% |
| FAN BLADE | 38.3% | | STATIC HAIR | 36.7% |
| LENS | 45.3% | | SLEEPWALKER | 32.4% |
| KILN | 37.1% | | BERSERK | 33.2% |
| WHETSTONE | 34.6% | | STONE SKIN | 46.1% |
| LUCKY CABLE | 38.4% | | FEATHERWEIGHT | 34.1% |
| MOSS GRIP | 50.9% | | | |
| PRISM CUT | 43.1% | | | |
| BRINE TIP | 38.4% | | | |
| RECIPE BELL | 37.1% | | | |

Spread of per-build clear rate (32 runs each):

| clear rate | 0-9% | 10-19 | 20-29 | 30-39 | 40-49 | 50-59 | 60-69 | 70-79 | 80-89 | 90-100 |
|---|---|---|---|---|---|---|---|---|---|---|
| builds | 3145 | 19212 | 34860 | 49967 | 64484 | 61992 | 13374 | 3020 | 523 | 37 |

Where runs end (floor of death; 11 = cleared): 1:1.4% 2:1.7% 3:3.6% 4:3.3% 5:5.9% 6:3.5% 7:5.3% 8:4.8% 9:9.9% 10:19.7% 11:40.9%

### Degenerate / broken scan

Builds clearing >= 90%: 37 (0.0%); builds clearing <= 3%: 0 (0.0%).

| strongest mod pairs | clear | | weakest mod pairs | clear |
|---|---|---|---|---|
| MIRROR GUARD + MOSS GRIP | 56.9% | | GRAVEL CORE + WHETSTONE | 25.7% |
| LENS + MOSS GRIP | 55.1% | | WHETSTONE + RECIPE BELL | 28.8% |
| FROST CORE + MOSS GRIP | 54.1% | | GRAVEL CORE + RECIPE BELL | 29.5% |
| EMBER EDGE + MOSS GRIP | 53.1% | | GRAVEL CORE + KILN | 30.1% |
| BAD SECTOR + MOSS GRIP | 53.1% | | KILN + WHETSTONE | 30.1% |

Top build: SPEAR + MIRROR GUARD + MOSS GRIP, BRITTLE + SHINY TEETH, BLINK: 100%. Bottom build: BLADE + TIDE HEM + GRAVEL CORE, GLITCHED + WET HANDS, ROLL: 9%.

## 6. Ten example builds

| build | pieces | archetype | off/def/ctrl/avoid | clear | what it does |
|---|---|---|---|---|---|
| **FROST AFTERIMAGE** | BLADE + FROST CORE + WHETSTONE; GLITCHED + FROSTBITTEN; BLINK | FROST | 80/-10/12/0 | 25.0% | Each BLINK leaves a GLITCHED afterimage at both ends; FROSTBITTEN hits chill, FROST CORE's charge freezes the ring. A HOT foe or lamp touching a frozen afterimage = SHATTER (x2.5 burst). GLITCH + COLD replays a seeded COLD reaction (often FREEZE or SNOW). |
| **STEAM BLINDER** | SPEAR + EMBER EDGE + WHETSTONE; WET HANDS + BRITTLE; ROLL | FIRE | 71/-27/6/26 | 44.9% | EMBER EDGE (HOT) + WET HANDS (WET) means every hit makes STEAM: a blind cloud that removes ~45% of enemy output. Pays for it: shock hurts +25% and BRITTLE takes +20%. |
| **SPLIT MIRROR** | GAUNTLET + MIRROR GUARD + PRISM CUT; SHINY TEETH + GIANT; ROLL | MIRROR | 82/25/0/11 | 54.7% | Perfect parry reflects orbs (REFLECT); MIRROR GUARD makes the reflected orb SHINY, so it hits SHINY again = MIRROR (splits in 2), PRISM CUT splits on MAGIC (3). Only-hurt-by-reflected-shots foes melt. GIANT for the HP. |
| **SHORT CIRCUIT** | DISC + BRINE TIP + SPARK PLUG; STATIC HAIR + TINY; SLIDE | STORM | 31/-8/8/21 | 47.3% | BRINE TIP soaks, SPARK PLUG every 3rd hit shocks: WET + SHOCK = SHORT, a stun chain through every wet thing. Kills shield-until-wet-then-shocked foes on its own. STATIC HAIR pulses SHOCK when hurt; TINY makes it hard to hit. |
| **GREENHOUSE** | HAMMER + SEED POUCH + TIDE HEM; GREEN THUMB + GIANT; SLIDE | GROWTH | 28/39/5/-12 | 50.8% | Kills sprout SEED POUCH vines, TIDE HEM puddles on each slide: ALIVE + WET = OVERGROW roots everything; the same reaction bridges pits in puzzle rooms. Hammer pushes crush-only foes. |
| **CINDER HEX** | WAND + HEX INK + EMBER EDGE; SLEEPWALKER + HOTHEAD; ROLL | ARCANE | 89/-10/10/12 | 27.0% | HEX INK (MAGIC) + EMBER EDGE (HOT) = CINDER: a delayed mark that bursts after 1 s. SLEEPWALKER drifts but its periodic MAGIC buff stacks into SURGE. |
| **STORM KITE** | DISC + FAN BLADE + LUCKY CABLE; FEATHERWEIGHT + TINY; BLINK | WIND | 16/-12/3/28 | 42.2% | FAN BLADE throws AIRY shots on every blink (x1.5 at both ends), LUCKY CABLE shocks when hurt: SHOCK + AIRY = THUNDER stun ring. FEATHERWEIGHT and TINY: a kiter with no HP. |
| **MUD BRICK** | HAMMER + GRAVEL CORE + BRINE TIP; STONE SKIN + GIANT; ROLL | STONE | 94/45/5/-12 | 53.5% | GRAVEL CORE turns every push into STONE terrain, BRINE TIP makes WET: STONE + WET = MUD (slows, forces burrowers up). STONE SKIN + GIANT: the slow wall build. Armour break from STONE tag. |
| **LANTERN DIVER** | SPEAR + LENS + EMBER EDGE; SHINY TEETH + BRITTLE; BLINK | MIRROR | 51/-20/3/36 | 78.5% | LENS beam (SHINY) + EMBER EDGE (HOT) = LANTERN: lights dark rooms and reveals invisible foes; beams relay through mirrors. SHINY TEETH widens parry but glows in the dark. |
| **BERSERK KILN** | BLADE + KILN + WHETSTONE; BERSERK + HOTHEAD; ROLL | FIRE | 82/-10/4/10 | 12.5% | KILN + BERSERK both fire on low HP: the only stack that goes past the DAMAGE cap's soft knee. 30% less healing is the bill. The sim's check on low-HP stacking. |

## 7. Bred lines (proto/genetics, generation-30 heirs)

1000 heirs per file, 200 per strategy, 32 runs each, enemies kept on the base-build balance (K=42, tuned caps). Before = `bred_lines_before.json` (stats up to ~93 points) with REMEMBERS/COLOURBLIND/ALLERGIC free. After = `bred_lines.json` (one shared 40-point stat budget, applied in genes.py) plus real costs for those three: REMEMBERS forgets a different recipe (+1 recipe step in every mix room); COLOURBLIND merges reds and greens (HOT and ALIVE foes' tells read worse: they deal +25%); ALLERGIC makes one seeded element hurt (+50%) and its opposite hurt less (-50%). Their upsides (a kept recipe, hidden SHINY paths) are run meta and treasure, so they add nothing in combat. Mapping as before: WHIP->SPEAR, LAUNCHER->DISC, HEAVY->GIANT, SMALL->TINY, GLITCHED->GLITCHED, DAMP->WET HANDS, INSOMNIAC->SLEEPWALKER, LOUD->HOTHEAD, ECHO->BRITTLE; stats POW->DAMAGE, SPD/2->SPEED, GRD/2->DEFENSE, FOC->charge AREA; each affinity level is a 10-point on-hit status. Every line gets the same seeded pair of found mods by line index.

| strategy | clear before | vs random before | budget only | clear after | vs random after | heirs > 60% after | p10 / p50 / p90 after | flag |
|---|---|---|---|---|---|---|---|---|
| random | 84.0% | +0.0 | 64.7% (+0.0) | 64.8% | **+0.0** | 114 (57.0%) | 50 / 65 / 81 | **over 60%** |
| heavy | 90.1% | +6.1 | 73.0% (+8.3) | 72.9% | **+8.1** | 180 (90.0%) | 62 / 71 / 84 | **over 60%** |
| blink | 84.4% | +0.4 | 65.5% (+0.8) | 65.5% | **+0.7** | 126 (63.0%) | 50 / 65 / 81 | **over 60%** |
| fire | 84.7% | +0.7 | 66.3% (+1.6) | 66.3% | **+1.5** | 130 (65.0%) | 50 / 65 / 84 | **over 60%** |
| power | 88.5% | +4.5 | 64.9% (+0.2) | 64.9% | **+0.1** | 126 (63.0%) | 50 / 62 / 81 | **over 60%** |

All bred heirs after: 66.9% (base-build field 40.9%).

Carriers in the new file: REMEMBERS 37, COLOURBLIND 20, ALLERGIC 108 (of 1000). Each cost alone, all strategies:

| cost applied | clear |
|---|---|
| none | 66.9% |
| REMEMBERS only | 66.8% |
| COLOURBLIND only | 66.8% |
| ALLERGIC only | 66.9% |

What-ifs past the requested change (same K):

| change | all heirs | random | heavy | blink | fire | power | worst gap vs random |
|---|---|---|---|---|---|---|---|
| every genetic trait as section 9 of the redesign doc defines it (HEAVY push+crush/-speed, SMALL -1 heart, ECHO killer spawns, INSOMNIAC -1 i-frame, DAMP WET/fire-proof/shock x2, LOUD holdout/escort easier, heist/dark harder) | 51.9% | 52.0% | 42.6% | 54.6% | 56.2% | 54.2% | +4.2 |
| doc traits + affinity 5 per level (was 10) | 47.9% | 48.7% | 38.3% | 50.8% | 51.2% | 50.6% | +2.5 |
| doc traits + stat budget 30 (was 40) | 47.7% | 47.6% | 38.0% | 50.4% | 52.3% | 50.3% | +4.7 |
| doc traits + affinity 5 + stat budget 30 | 43.6% | 44.3% | 33.9% | 46.0% | 47.0% | 46.9% | +2.7 |

### Final (adopted): doc-defined traits, HEAVY at -10% speed with a staggering dodge-cancel lunge

Stagger sweep (random heirs carry HEAVY too, so both columns move; the B->A lunge interrupts; modelled as % of enemy output removed, before the 60% control cap):

| stagger | random | heavy | heavy vs random |
|---|---|---|---|
| 0 | 54.3% | 50.5% | -3.8 |
| 3 | 55.4% | 54.1% | -1.3 |
| 5 | 56.1% | 56.8% | +0.7 |
| 8 | 57.3% | 60.5% | +3.2 |
| 12 | 58.9% | 65.6% | +6.7 |

Chosen stagger: 5 (the non-zero value with the smallest heavy-vs-random gap). Final per-strategy table (K=42, base-build balance):

| strategy | clear | vs random | heirs > 60% | p10 / p50 / p90 |
|---|---|---|---|---|
| random | 56.1% | +0.0 | 61 (30.5%) | 40 / 56 / 75 |
| heavy | 56.8% | +0.7 | 60 (30.0%) | 43 / 56 / 71 |
| blink | 58.7% | +2.6 | 80 (40.0%) | 43 / 56 / 75 |
| fire | 59.8% | +3.7 | 81 (40.5%) | 46 / 56 / 78 |
| power | 58.5% | +2.4 | 89 (44.5%) | 43 / 59 / 75 |
| **all heirs** | 58.0% | | | |

## 8. ROM/CPU/OAM notes (estimates)

- Numeric tables in this prototype: 756 bytes (reaction matrix 100 B full, 55 B as a triangle). Reaction records 49 x ~7 B + names. Pieces: 5 B each; a full player build is 6 pieces = 30 B of WRAM; mobs are 7 B.
- Event resolution is fixed-order, once per frame: hurt -> hit -> parry -> kill -> charge/dodge -> timers -> tile-enter -> reactions -> terrain writes. Each equipped piece's trigger is a 4-bit field, so a 16-bit trigger mask per build filters which pieces wake (no scan). Max 8 queued events/frame; overflow drops the oldest cosmetic one, never a damage event.
- Reactions: one table read per (tag, tag) contact; GLITCH adds one more read. No recursion: a reaction's result cannot itself react in the same frame (it is queued for the next).
- Stacking runs only when a piece is equipped or lost (cached per group), never per hit.
- OAM: 40 sprites, 10 per line. Budget: player 2 (8x16), weapon swing 1, mobs up to 6 x 2 = 12, boss 8-12 (mobs drop to 3 in boss rooms), material overlay ~6, **shared particle pool cap 10** (steam, sparks, shards, reflected orbs). Per-line pressure is the real limit: particles are culled first on busy lines; split shots reuse the pool and a 3-way PRISM splits only when 2 slots are free (else it reflects as 1).
- CPU (CGB double speed ~70k cycles/frame): budget ~2k cycles for the piece/reaction layer: <= 8 events x (mask test + up to 6 piece reads ~40 cycles) + <= 4 reaction lookups ~60 cycles each. The generator (mobs, deck, guarantee) runs on floor load, not per frame; boss rejection sampling is the slowest piece (mean draws reported above) and can be baked into a seed table if it stalls.

