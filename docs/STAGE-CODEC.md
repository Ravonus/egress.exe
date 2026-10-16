# Authored goals, generated worlds

The owner wants stage goals and conditions authored separately from the map that realizes them. The same goal
can occur on a short island, a broad courtyard or a long turning route, with different item placements and
resource-feasible answers. Geography must carry the puzzle rather than merely decorate a fixed room.

The editable source is `assets/stages/codec.json`. `tools/bake_stage_codec.py` validates it and writes the compact
native tables in `src/gen/stage_codec.inc`. `src/core/eg_land.c` consumes those tables on both host and SM83.
Changing allowed goals or conditions does not require editing a map coordinate list.

## Authoring contract

Each stage rule specifies a pool of goals, required goals, allowed load conditions, map shapes, allowed sizes
and alternative verbs. The initial goal vocabulary is load, heat, cold, socket power and powered transport.
Load conditions are hold, latch, either source and both sources. Transport requires an earlier load; two
independent native circuit channels remain the hardware limit. A powered socket owns its named gate, so it
cannot open an unrelated later lock.

The seeded constructor selects dimensions, orientation, outline, goal count and order, working areas, apparatus
positions, finite ingredient positions, stream contours, foliage, props and visual variants. Progression cuts
separate the goal regions. Source/recipe ingredients are placed on the reachable side of their cut. Deliberate
working areas and a continuous approach survive scenery placement. The generator has its own stateless stream;
cell-query order never changes a map or advances the combat RNG.

| Profile | Width in cells | Height in cells | Goal count before hardware limits |
|---|---:|---:|---:|
| XS | 16–20 | 14 | 1 |
| S | 24–32 | 14–18 | 1–2 |
| M | 36–48 | 18–26 | 1–3 |
| L | 52–64 | 26–34 | 2–3 |
| XL | 72–96 | 34–42 | 2–4 |
| XXL | 100–128 | 42–54 | 2–4 |

Size is separate from difficulty. The codec can restrict the first load to XS/S, while a later mixed stage can
use a large footprint without adding more locks. Some eligible maps progress vertically. The four current outline
families are terraces, broad basins, bends and courtyards with side holes. They retain purposeful empty space and
real abyss edges instead of filling a bounding rectangle with floor.

Biome records control foliage/water/prop frequency and the water rendition: ordinary, industrial, digital and
glitched. Streams are connected reactive water beside the route; some spill at an exposed edge. Ferns, flowers,
reeds and props have seeded variants. These are design records in the generated world, not new material IDs.
Water still freezes and conducts; foliage uses diggable material. Rendering these records needs the native
terrain-art integration, with animations and reactions retaining their material identity.

## Proof and integration boundary

`make test-land` checks 6,144 accepted seeded contracts. It replays physical and crafted alternatives across the
complete global map, including finite inventories, actual recipes, real pushes, independent gates and conditional
portals. Every tool/push mutation uses the game's shared 16×14 rules projected onto its global neighborhood.
Reachability is recomputed between actions. A route also asserts that a preceding socket did not clear a later
cut. This is a constructive witness for these generated goals; it does not prove every possible player choice.
`tools/test_stage_codec.py` rejects invalid authored contracts before compilation.

`make test-native-land` compares 24 actual SM83 constructions to the host: 604 rows, 36,640 cells, complete plans
and every material/design/variant/protection byte. It checks bank restoration and the DDD0 stack canary through
normal handshakes. This establishes cartridge generation parity, not player-input replay of a larger stage.

The playable office cartridge still uses a 16×14 active board. The separately tested horizontal strip primitive
preserves materials, skins, protection and crack bits, with 256 host seams and 64 native seams. Its maximum native
cost is 68,884 T-cycles. The active game still needs the larger backing world, both-axis window travel, rebased
actors/queues, persistent source/goal/item state, native environment art and full input replay. Keep the public ROM
on its verified release until those boundaries are completed and the complete native regression gate passes.

## Remaining grammar work

- Add moving/falling CRT dependencies, actor-wave conditions and route-sensitive alternatives to these larger
  worlds, using the same physical and combat matrix rather than a separate abstract solver.
- Make remaining-goal resource/deadlock checking cover the global world, including wrong spending and permanently
  lost objects. A local room flood cannot certify an off-window objective.
- Connect the run curriculum to the authored pools, with the first appearance of each verb taught independently.
- Preserve mutation, item and actor identities through window travel, backtracking, reset and continuation.
- Finish the native renderer's connected terrain grammar, subtle dither/palette variation, foliage movement,
  digital rivers/waterfalls and their reaction/spent states within the existing VRAM/OAM/frame budgets.
