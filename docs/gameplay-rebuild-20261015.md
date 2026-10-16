# Gameplay rebuild: implementation contract

The owner rejected the delivered office slice on 2026-10-15. This document supersedes the earlier proposal where
they conflict. It is a delivery checklist, not a claim that every entry is implemented.

## Initial audit (before this rebuild)

The public standard ROM is SHA-256 `8191bc71cd5fb0067dabf58c8886e072658a5053286a0d98c890b299c09e413e`;
the RTC ROM is `df11b82b8e14d164aedb75b9d48a4ec73e2c2ad137e420654c2c6364d3d28bd8`. Both match the local
build. Public main matched `b9fd6ea` when checked. No source worktree is left unmerged. This is an implementation
and play-quality gap, rather than a stale download or an omitted worktree.

* `proto/genetics/genes.py` contains inheritance and sibling selection. Native `heir_roll` only makes a fresh founder.
  There is no native family selection/tree/save. The rolled weapon field is not read by combat.
* The native pocket is emptied by `pocket_reset` at every floor and reset. It therefore cannot support resource
  decisions between floors. The portable bench only tells the player about START-to-use on one small footer line.
* The planner forces introduce/develop puzzle rooms before dealing combat. Its initial order starts BURN, COLD.
  Most of the ten-floor slice is consumed by introductory puzzles. CRT gravity exists, but no named falling-block
  or pushing puzzle is required by that curriculum.
* The shared eight-tag reaction table is broad, but room application implements only burn/freeze/thaw/power/weight.
  A table entry alone does not establish an interactive object or usable combat action.
* Heir animation data and generated mob state art exist. Static object cards do not establish idle, activation,
  spent, damaged and destruction states for every interactive. Animation acceptance must exercise each state.

## The game loop

Fights are the common activity. Short physical decisions happen inside fights; dedicated brainteaser rooms punctuate
them. A first run must visibly contain pushing, falling blocks, breakable walls, a wave fight, a bench recipe and a
resource choice. Fire is one useful tool, never the mandatory opening lesson or the default answer to every wall.

The bench keeps Crucible's two ingredients, visible result and commit/reveal sequence, with a small fixed recipe
catalogue. The satchel has finite quantities, carries between floors and clearly shows what will be consumed.
Failed combinations and invalid targets retain the items. A successful optional use can spend a resource useful
later; optional caches and easier combat routes may consequently be lost. Required egress must have a proved
resource-feasible route from the actual inventory, or a visible physical fallback. A reset restores both the room
and its entry inventory, preventing item duplication and permanent recipe mistakes within that room.

## Shared interaction matrix

| Action/tool | Room object | Enemy/environment interaction | Observable states |
|---|---|---|---|
| Push | crate, CRT, plate | moving cover, crush or trap | rest, contact, moving, supported, settled |
| Remove support | foam/drywall under CRT | falling trap, pit bridge, plate load | supported, warning, falling, impact |
| Strike | cracked drywall | shortcut/optional cache; heavy hit is faster | intact, cracked, broken |
| Water/rain | hot surface, conductive pool | wet, extinguish; enables shock/freeze | dry, wet, reacting, drained |
| Cold/ice | flood, wet body | walkable temporary bridge; slow/freeze/shatter | wet, frosting, frozen, cracked, thawed |
| Shock | socket, connected water, wet body | circuit, stun, chain; dry stone insulates | unpowered, charged, active, discharge |
| Mud/earth | one-cell gap | bridge/cover; slow a lane | empty, filling, solid, damaged |
| Air | light object, smoke | shove, clear sight, alter a projectile | idle, gust, moving, settle |
| Heat | marked paper/wood, ice | burn, thaw; can destroy a useful bridge | intact, smoulder, spread, spent |
| Weight/brick | plate, open floor | hold a gate, block a lane | carried, placed, loaded, pushed |
| Steam/dust | sight line | concealment and blind, including sentry counterplay | idle, bloom, active cloud, disperse |

The same rule resolves player-to-object, player-to-mob, mob-to-object and mob-to-player interactions. Each rule
declares a precondition, consumed quantity, spatial footprint, state transition, cue and recovery. No silent
element immunity, unnamed fire target or use that succeeds only because a hidden menu happened to face the right tile.

## Puzzle grammar and variety

Generate a dependency graph before decorating a room: objective -> locks -> alternative ways to change each lock ->
required access, position, timing and resources. Embed small validated motifs, then compose only compatible motifs.
Randomly scattered hazards and differently coloured copies do not count as new puzzles.

| Motif | Core reasoning | Alternative route | Later composition |
|---|---|---|---|
| Crate to plate | push position, standing space, avoid corners | make a finite brick weight | two loads, movable cover, timed waves |
| Falling CRT | remove support, retreat, predict landing | redirect a mobile heavy | plate behind a gate, fragile landing |
| Fill a gap | align a push and its support | spend mud to build the crossing | limited mud, two gaps, optional cache |
| Break a wall | inspect cracks, reach the strike side | lure a heavy or spend a tool | expose a dangerous lane while opening a shortcut |
| Circuit | connected wet cells and source/target order | move a conductor/return an orb | enemy charges the same network |
| Freeze route | path length, thaw/step budget | a physical bridge or longer safe route | moving load and combat pressure |
| Sliding/redirected object | direction, stopping space, intermediate states | air impulse or hand push | mirrors, switches, momentum |
| Sequence/phase | remember a readable pattern, then act | pay a limited bypass item | earlier motif plus phase changes |
| Holdout | positioning, target priority, resource restraint | use room machinery against waves | fight while protecting a changing safe lane |

Variation axes are independent: topology/embedding, object counts and sizes, initial state, goal position, dependency
order, alternate route cost, enemy role, patrol phase, visible optional reward, theme and animation skin. Avoid recent
structural signatures within a family. Teach a verb on its own, then vary its use, then combine it; escalation is
measured by dependent decisions, reversible/irreversible branches, spatial/timing demands and enemy pressure.

The solver must track player position, moving objects, tile states, held quantities, consumed resources, switches,
timers and relevant enemy phases. Prove a complete route; replay it through the native rules. For rooms advertising
multiple answers, prove at least two routes with different significant actions/costs. Test failing branches too:
spending a scarce item must have its advertised consequence, while passive waiting must not silently destroy a room.

## Family and combat

Death records the actual parent and presents three related heirs. Choice survives power-off. Looks, dodge style,
weapon behaviour and useful tradeoffs must be visible before choosing. A fresh unrelated roll on every floor or run
is not inheritance. Family history and the current generation belong in a versioned, CRC-checked save alongside
the existing meta record; migrate rather than discard the current save.

The five weapon genes need different effective reach, shape, force/recovery and charged behaviour. Both A and B have tap/hold attacks, with distinct A then B and B then A combos. Directional double-taps evade relative to the pre-tap aim (forward dodge, sideways sidestep, backward backstep); holding the direction runs, with or without a preceding evade. These controls supersede the former B-dodge proposal. Existing dodge roll/slide/blink code and all authored animation states stay
in use. A comparison must exercise actual hit boxes and frame data, not just different clothes or move names.

## Acceptance

Use fresh player input to demonstrate the first three floors without debug warps. Also capture a full physical
puzzle, both routes through a resource puzzle, a wave victory, an invalid use, a successful use, inventory carry,
reset without duplication, death, sibling selection, power-off persistence and changed heir combat behaviour.
Produce a coverage ledger for every object/mob/heir animation state. Keep host proofs, emulator evidence, browser
release identity and physical-device acceptance separate. Do not publish this rebuild as complete while any of
these user-visible requirements remains a prototype or unreachable code.

## Native checkpoint

This checkpoint implements the ten-floor office slice described in the README, not the full motif-composition
grammar or the roughly 52-floor run above. Physical rooms use three validated motif constructors with varied
positions, not arbitrary dependency-graph composition. The bench is portable on SELECT; ingredients and tools
have finite carried quantities. A physical workbench-only crafting restriction is not implemented.

The native family now persists the selected genome, two parental allele sets, three successors, the full 13-lesson
curriculum and eight real ancestors. Five weapon profiles are exercised against the same stationary targets with
normal A inputs. B hold repeats beyond frame 255 and works while running. Both ordered combos and all directional
evades are checked on both mappers. The separate journal migrates existing meta without inventing past ancestors.

The elemental tools enter the existing mob status matrix: water extinguishes/wets, cold freezes a wet body,
electricity stuns wet targets, mud/earth slows, steam/dust blinds, and air pushes light bodies/crates. This is a
bounded native subset of the design matrix. Air does not yet redirect projectiles; the generator does not yet
compose all of these actions with timed fights into arbitrary multi-solution rooms.

### Validation and animation ledger

`make test` builds and checks both mapper editions. Detailed logs and machine-readable reports are generated under
`build/gameplay-rebuild/`; they are local evidence, not a physical Chromatic certification.
The committed [evidence record](validation/gameplay-20261015/report.json) pins both artifact hashes and the native
results; adjacent PNGs and GIFs show the played rooms, bench, descendants and animation fixtures.

| System | Native implementation and evidence | Limit |
|---|---|---|
| Heirs | All 45 authored states, eight directions, six skin tones: 2,160 sprite-mode samples per mapper; real input checks cover attacks, charge, combos, evades, running, bench and death | Sprite-mode sweep verifies the full-body title handoff; it is not an art comparison of every pixel |
| Items | 16 cards, four native VRAM phases each, canonical original pose retained; pickup, mixing, valid use and refused use exercised | Hover shares the object's existing art; no new bespoke character was generated |
| Seven mob families | Native renderer fixtures cover both idle/walk poses, tell, hurt flash and dead-body removal; wave fights and returned-orb kills exercise real transitions | Seven families sampled, not every combination of all 42 looks and elite affixes |
| Living obstacles | Existing two-pose idle, tick movement tween, stop/freeze/root/reversal cues; host canonical tick solver and native gravity parity | Individual freeze/root states retain a held pose, not a separate illustrated animation |
| Physical objects | Push/load, supported/falling CRT, plate/gate changes, cracked/broken drywall and debris use the shared board plus existing look effects | CRT movement is grid based; no new independent physics engine |
| Fragile/reaction floor | Existing crack stages, wobble/fall/shatter and glitch phases; spread reveals changed cells with sparks | Static architecture does not receive an idle pose |
| Bosses | Both complete fights, every phase, death, actual damage, OAM <= 10 per line and 120 gameplay updates per 120 VBlanks | Automated no-hit player demonstrates feasibility; it is not human difficulty acceptance |

The first three floors are played consecutively through controller input with no floor warps or forced kills
(health protection is explicit during combat). Separate floor-7 fixtures prove the CRT route and the consumed-mud
route. Additional fixtures prove invalid recipes/targets retain units, full-bag discard, cross-floor carry,
reset without duplication, actual death/siblings, selected-heir power-off persistence and torn-journal fallback.
Host tests cover 192 physical solver replays with both crossing routes, all 768 existing accepted rooms, 16,384
interaction cases, 12,288 inherited siblings, 64 families seeing all 13 lessons within six generations, and exact
projectile aim parity over 233,928 vectors. Input tests retain charge/parry/buffer boundaries and deterministic
mixed-input checks for all 15 weapon/dodge combinations.

The standard checkpoint is SHA-256 `1fab0cb81ef3532d3c7fae52ccbcfa464171e1d3c078f4df4b4b7f2d74e7e5f2`;
the RTC checkpoint is `a06151f43c7ab2994ee8f97e291ba559de9810f8946013a986e9989de49305c1`.
Each is 1,081,344 bytes, with the existing 1,900,000-byte gate unchanged. The former one-frame LAST FAX fan hitch
was removed by an exactly equivalent dominant-axis aim calculation; the complete native boss pacing gate passes.

Remaining product work includes the full run/biomes, arbitrary motif composition and more extensive multi-route
resource branches, richer enemy-to-object combinations, and human/device playtesting. This is a development
checkpoint with the implemented subset reachable, not a declaration that those remaining requirements are done.
