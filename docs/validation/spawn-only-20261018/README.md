# Spawn-only terrain and left-edge movement

The owner replaced camera-driven building/collapse with a single assembly around the heir at spawn. Floor remains resident during walking, rolls, backtracking, ordinary exits and death. Larger worlds retain bounded offscreen background maintenance. Real fragile-floor failures, chase destruction, crafted bridges and boss damage remain gameplay effects.

Movement and CRT lifting use material collision. They no longer reject a floor cell because its visual-presence flag is stale. An explicit native fixture on empty left-edge floor stopped at x=22 in the previous ROM; the same input reaches the normal grid limit x=6 in the corrected ROM. This is a controlled reproduction of the visual/physical coupling, not a claim to have recovered the owner's exact seed.

Native frame capture verifies animated spawn assembly, zero camera assembly/departure during ordinary travel, unchanged board materials and input-driven left/right/backtracking on both mappers. The focused terrain, hole, ledge, combat-camera and procedural routes pass. The complete current-source native suite, all four website gates and seven fresh browser checks pass. Publication is recorded separately.

The local GBDK linker exposed host-dependent autobank output ordering, which displaced the page-aligned sprite-upload buffer. Banking and ordered linking are now separate. The text writer uses one loop for the same attribute/tile writes, preserving output while keeping fixed ROM within budget. Standard and RTC layouts pass the existing memory, bank and header gates.

Rendering observers now watch actual initial map writes, retaining their >20-write coverage requirement after removing cosmetic retire/restore writes. The art observer reads to the true 16,384-byte bank boundary rather than excluding the final 384 bytes. Its independent delta/pixel comparison and sample thresholds remain unchanged.

Source `f19136c84a4294f33270ffcad93ce92137d147ba` is frozen against 504 input hashes.

| Cartridge | Bytes | SHA-256 |
|---|---:|---|
| egress.gbc | 1556480 | `c04e9703b973173112911aa94769b1a6871803c0a33edb637960d897770e923d` |
| egress-rtc.gbc | 1572864 | `38364434c967c53c844236736c886708d3ac2ac31ef7c6069655a7184daeacef` |

This remains the ten-floor office development campaign. Physical cartridge playback has not been tested for this pair.
