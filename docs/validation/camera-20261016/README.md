# Native camera checkpoint

Both mapper images in artifacts.json passed the complete `make -j2 -s test` gate, recorded in full-gate.txt.
The additional `make test-camera` checks use ordinary movement and action input through the cartridge.
Quiet routes collect ingredients, move a crate, travel diagonally, evade, reverse and backtrack; the board and
pocket persist. Combat routes keep real AI and waves active, use both attacks and achieve real kills. Both
retain 120 game frames per 120 display frames, complete bodies, a ten-sprite scan-line maximum and the DDD0
stack canary. Read-only hooks compare the native allocator and every sampled BG quad against independent
reference calculations, including tile attributes and interrupt-masked transfer duration.

The native movement fast path is separately compared against 4,608 complete portable states, including pushes,
diagonals, four-way controls, mapped/raw material grids and nonstandard strides and heights. The main gate also
checks the fixed 4,864-case generator fingerprint, the 768 accepted rooms and no-hit office boss fights.

This checkpoint improves camera presentation within the existing 16 by 14 simulation grid. It does not complete
connected larger stages, the full native run or the remaining sprint ledger. It has not been deployed or tested
on physical hardware. The tested ROMs are retained locally as build/egress-camera-checkpoint.gbc and
build/egress-rtc-camera-checkpoint.gbc, with their exact symbols.
