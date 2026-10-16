# Connected stages: implementation boundary

The development cartridge now moves its fixed 16 × 14 simulation window through generated worlds up to
128 × 54 cells. Quiet puzzle worlds are integrated;
their ordinary-input and frame-budget acceptance passes on both mappers. Combat actor streaming and the complete run
remain required. Keep these distinctions in release notes and the completion ledger.

## Current terrain reuse

Untouched terrain comes from an 83-byte seeded plan. Four-cell packets preserve material, visual skin,
protection and crack state in WRAM banks 2–5; bank 6 holds initialization and weight-search metadata. The active
16 × 14 board, collision map and stack retain their existing addresses. Both axes advance in four-cell strips.
The overlapping cells and their five presentation bitmaps are shifted; only incoming strips need loading.
The hardware background map rotates its origin rather than being replaced by another screen.

Approach work is incremental: prepare at most six generated cells before scan line 35 while spare time remains, clear a bounded
number of off-screen background slots, and save outgoing packets during spare scan lines. Every material or skin mutation
invalidates its saved certificate. Source/gate/portal metadata is prepared ahead of the transfer and revoked
when relevant state changes. The actual transfer accepts only prepared incoming and outgoing strips.
Certificates for unchanged overlapping packets survive a transfer. Perpendicular background clearance is reused;
only a newly exposed padding corner needs clearing again. Movement into an unloaded continuation retains the
complete body inside the active window; the input gate reports any waiting there separately from frame rate.

Terrain assembly shares the existing eight animated-card slots. The roaming queue plus active cards is capped
at twelve; hidden terrain is revisited as capacity returns, and the player's immediate ground takes priority.
This is tile-slot reuse, with no general mesh allocator or unbounded queue. Cosmetic disappearance never
regenerates consumed items or moved objects. Timed fragile-floor restoration addresses global coordinates.

The native component gates currently compare 288 transfers of all 716 state bytes, including all five
presentation bitmaps, and 792 lazy-world mutation/backtracking/reset states. Those component results do not
replace the full game's timing, input, combat or continuation gates below.
Prepared data transfers peak at 44,872 T-cycles horizontally and 43,424 vertically in that component fixture.
`make test-world-play` plays S–XXL composed routes on both mappers. `make test-world-environments` covers XS and
all eight biome records on both mappers. These explicit input gates pass 42 scenarios and supplement the passing complete regression suite.
After the card-span timing pass, the actual window-margin observer measures at most eight frames of terrain
waiting at large-map corners; the input gate rejects more than twelve. See
`validation/world-reuse-tuning-20261016/` for the final exact source/ROM hashes and independent presentation proof.

## Required architecture

Keep the proven 16 × 14 active simulation window while giving the stage a larger global coordinate space. Move
that window in four-cell strips before the player reaches its edge. A window shift must preserve the visible
world coordinates: adjust the local player/camera together and rotate the background map's tile origin. Rewrite
only the entering strip. An ordinary window shift must not fade, open a menu, change floors or reset a puzzle.
The existing walk, attacks, forward roll-jump and holes remain usable across the seam.

The backing store must retain the actual material cells, protected cells, item identities and collection flags,
independent circuit state, actor health/status, obstacle state and persistent object mutations. Presentation
state is separate: a slab can collapse behind the camera and assemble again on approach without restoring a
consumed item, repaired floor, broken wall, moved crate or killed enemy. Camera presentation must not silently
replace a conditional bridge or a chase stage's real destruction.

The core now supports two independent named circuits with AND/OR/latching sources, player/object weights and
powered sockets. Twelve appended room bytes also describe a conditional portal pair. Off-window source snapshots
have explicit values, so an inactive source cannot be mistaken for an unweighted local plate. The complete
16 by 14 portal motif has host crate/tool alternatives and native input replay; that is a dependency prerequisite,
not a larger connected stage. The larger constructor still needs a replay of its complete global dependency
graph. Individual chamber proofs alone are insufficient. Portals and falling-object routes must use the same
persistent world and finite resource accounting as ordinary movement.

## Native constraints to preserve

- `EG_N` and local cell indices currently fit a byte. Changing the constant alone would break gravity assembly,
  queue lengths, bitsets, region searches, item records and the fixed WRAM layout.
- The floor grid starts at D800; the 256-byte collision map starts at D940. Narrator, fall, stamina and continuation
  state occupy reserved regions above them. The floor-only skin map ends at DDCF; DDD0..DFFF is the live stack.
- WRAM-bank transfers must use the tested leaf routines. The live stack stays in bank 1, interrupts are masked
  only for bounded transfers, and every leaf restores bank 1 before returning or touching the stack.
- The 32 × 32 BG tilemap wraps in hardware. Strip uploads and every cell/skin/edge draw must agree on its rotated
  tile origin. Sprite positions must retain their screen location through a window shift.
- Inactive actors cannot keep unsigned local coordinates that wrap onto the opposite side. Preserve global
  identities and activate a bounded nearby set, including health and status when an actor returns.
- Entrance columns must be prepared incrementally; copying all cached state and drawing a new room in one frame
  would defeat the frame budget. Keep complete character bodies and the existing OAM priority rules.

## Completion evidence

Play the actual ROM through several seams in both directions. Move a crate, collect finite ingredients, change a
circuit, break a wall and kill an enemy; leave and return, then verify that each state persists. Exercise a portal,
a roll-jump and a real fall near a seam. Verify forward assembly and rear collapse under camera movement without
covering the character or resetting conditional terrain. Repeat on both mapper builds and after continue/reset.

Measure active game frames, VRAM writes, sprite scan lines, cache bank restoration and the stack canary while
crossing and fighting. Keep the existing 768-room replay, curriculum, gravity parity, art and no-hit boss gates.
A larger playable stage, its composed proof and native evidence are required before this milestone is complete.
