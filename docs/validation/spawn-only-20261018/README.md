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

## Published release

[PR #8](https://github.com/Ravonus/egress.exe/pull/8) merged as `5edbd2cc1e1d756c8ddf1fb693ecbbc67a36bd8b`. The [public player and downloads](https://egress.onkeel.io/) serve this exact pair. Actual standard/RTC downloads, emulator ROM identity and layouts at 1280/768/390/320 pass a fresh live browser run. That driver waits for the page's existing interactive-ready marker before operating controls. HTTP hashes independently match the export. The site source is committed locally as `ef9fa691e78386a4d27e83ad35ba99a440ecdbd3` in its checkout with no Git remote. All 65 pre-existing changed paths are preserved.

Only the primary game worktree and `main` remain. Private browser outputs are archived under the ignored candidate directory; no temporary production-host test checkout was reused, and the prior temporary directory remains absent. Production rollback backups are preserved. See `site-release.json`, `cleanup.json` and `live-site/report.json`.
