# Connected stages: implementation boundary

The current cartridge still uses one 16 × 14 simulation grid per floor. Opening its exterior creates real falls;
it does not make the stage larger. The 8 KiB banked WRAM cache has standalone native coverage and is not yet a
playable connected-stage implementation. Keep these distinctions in release notes and the completion ledger.

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
