# Persistent generated-world checkpoint

This is the quiet-world continuation of `stage-codec-20261016`, not completion of the full-game sprint ledger.
The public ROM has not been replaced by this checkpoint.

The cartridge keeps a 16 × 14 simulation window over XS–XXL seeded geography, with a 128 × 54 maximum. Four-cell
packets preserve material, artwork, protection and cracks. Prepared strips reuse the overlap and perpendicular
background clearance. Moving crates, finite pickups, named circuits and conditional portals retain their state
on return. The terrain animation queue plus active cards is bounded at twelve.

The ordinary-input matrix covers twelve S–XXL composed routes, XS, and all eight authored biome records on each
mapper: 42 scenarios. Both images pass the strict 120 game frames per 120 display frames, complete-body,
independent heir-pixel, BG tile/attribute, OAM-line and stack-canary checks. The tests use only pre-start scenario
selectors; they do not teleport the player or change puzzle state. Read-only CPU hooks observe completed updates.

Native component checks independently compare 24 complete constructions, 288 window transfers of 716 bytes,
384 metadata bindings, and 792 persistent-world states. A separate native regression changes material, artwork,
protection and cracks, crosses both axes, evicts the changed packet and returns. Twenty-two negative cases check
remaining-resource and weight certificates. The host checks 6,144 plans, 12,288 finite-resource physical/tool
replays and 35,599 dead-room certificates.

`make test-world-failure` discards the alternative tools, pushes the required crate into an irreversible edge
position, observes the actual reset cue, and holds SELECT. The same plan, crate and ingredients must return.
The complete regression target includes this test and both world input matrices.

Terrain preparation can still briefly block movement at an internal window margin; this is measured separately
from frame rate. True island edges retain their fall/jump behavior. Latency tuning, actor streaming, the complete
run and campaign biome/content integration remain in `docs/SPRINTS.md`. These emulator results do not establish
physical Chromatic behavior.

The ROM finishing gate now rejects a linked area crossing its ROM-bank boundary before producing an output.
Native generation uses one KiB of constant-product tables; the independent host oracle retains its arithmetic.
The common 64-frame clock increment is exact against every possible 16-bit poll gap.

Frozen ROMs, maps, symbols, full reports and screenshots are under `build/candidates/world-stream-20261016/`.
The compact checked-in manifest records their hashes and the final regression outcome.
