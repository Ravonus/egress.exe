# Stage codec and window prerequisite

The complete `make -j2 -s test` run in full-gate.txt passed against the mapper images in artifacts.json.
It includes all prior office, art, movement, camera, save, puzzle, boss and timing gates, plus these new checks.
The game images are frozen locally in build/codec-checkpoint/. This checkpoint has not been deployed.

The authored codec compiles into a portable/native generator. Host replay checks 6,144 plans and 12,288 physical
or crafted answers with finite inventories, actual recipes/pushes/circuits and global route recomputation.
The cartridge fixture matches complete plans and all four design bytes of 36,640 cells over 24 maps, covering
all profiles and biomes. Twelve invalid authored contracts are rejected. The generator is independent of combat
RNG and query order. land-gate.txt contains its exact native image and trace hashes.

The horizontal window prerequisite checks exact active/inactive material, skin, protection and crack state under
256 host seams and 4,352 mutations. The native cache trajectory matches all 508 bytes of each of 64 states,
restores WRAM bank 1, preserves the stack canary and costs at most 68,884 T-cycles per shift.
window-camera-gate.txt additionally retains full-body, BG/OAM, active-frame and backtracking checks on both mapper
images with the BG-origin hooks dormant at zero.

These are prerequisites. The game remains the ten-floor office slice with a single active simulation board.
Generated large-world input replay, both-axis streaming, persistent actors/goals and native environment art are
still required. No physical Chromatic test or public deployment is claimed.
