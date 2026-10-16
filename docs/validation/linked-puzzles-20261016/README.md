# Composed native portal puzzles

The exact mapper images in artifacts.json passed the complete `make -j2 -s test` suite in full-gate.txt.
This includes the 768-room solver/replay, fixed legacy generator/RNG fingerprint, character pixel uploads,
gravity parity, movement trajectories, save migration/continuation, camera/OAM budgets and both no-hit bosses.

`make test-linked-puzzles` checks 64 seed variants with independent crate and crafted-weight solutions, a
necessary portal connection and finite wrong-spend branches (256 cases). HOT conservation is checked against
every piece and recipe. Without HOT, the sliced reachability check no longer assumes paper can always be opened.
The standalone native rule trace checks 512 complete states, including all room bytes, dirty cells, circuits,
occupied-gate safety, upward/downward transport and CRT gravity. It preserves bank 1 and the DDD0 stack canary.

`make test-native-linked-puzzles` drives both mapper images through both mirrored layouts, using separate
physical and crafted answers. The tool planner conservatively treats the unused crate as a wall; the actual
cartridge retains it. Every move, craft, tool use and crossing goes through ordinary input. Three portal crossings
prove backtracking retains objects and inventory, with no immediate rebound. Both variants retain 120/120 active
frames, floor under the feet, exact native BG writes and at most ten sprites per line. Menu drawing, bench scenes
and the intentional end-of-floor collapse are separate from active movement timing. Real wrong recipes exhaust
HOT, display the existing reset cue and restore the exact stage and entry bag through held SELECT.

The native item checks cover all sixteen items in all four poses, compare their actual metatile destination
with the canonical baked layout and verify that every other BG card remains unchanged. The old test repeated
the original wrong offset; it now catches the underlying neighboring-art corruption.

This remains a 16 by 14 simulation board in the office slice. Named circuits and cache-backed source values
prepare connected larger stages; they do not implement those stages or complete the remaining ledger. The public
site is unchanged, and physical hardware has not been tested.
