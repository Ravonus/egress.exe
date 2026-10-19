# First-floor combat travel and plate feedback

Clear floor at a generated window edge could stop the heir during combat because preparation and view transfers were disabled while any enemy lived. The same ordinary opening seeds now cross that boundary during the original encounter. Off-window enemies retain their own pool slots, HP, state and timers; returning restores them. No extra enemies are created to replace them. One AI update yields to the transfer while player input, animation and the complete sprite publication continue.

Floor contacts now explain their actual circuit: holding a gate, latching it open, either plate, or both weights. A missing second weight gets “WEIGHT BOTH PLATES”; pressing a linked plate signals the plate and visible gate through the existing effects and HUD. Puzzle conditions are unchanged.

Camera easing waits for backing floor to be painted during rapid reversals. One slab outside the display is prepared ahead of time. Spawn assembly remains the only cosmetic construction; walking and backtracking do not assemble or drop tiles. Native packet and pooled-particle transfers are bounded, with independent spatial oracles. New direction inputs receive their own pose budget before a held direction transfers the world.

The released-ROM reproduction used ordinary generated seeds 0x69 and 0x99 and controller movement, with invulnerability only for repeated observation. Both previously stopped at free western floor (global x=152 and x=88). Opening-route tests pin the seed and disable enemies to isolate all four circuit conditions; they use inputs to reach the western floor, trigger the plate and solve to floor 2. These are reproducible cases, not a recovered copy of the owner's exact save.

Both mapper ROMs pass 15 focused phases and the full native suite. Eight opening routes per mapper cover horizontal/vertical stages and all four circuit conditions. Frame/OAM budgets, canonical sprite pixels, exact map writes, rapid reversals, particle rebasing and procedural progression pass. Four site gates and eight private browser checks pass, including a mortal seed-0x69 Continue save in the real website player, actual keyboard input and an exact ROM-fetch hash. Browser instrumentation reads native state; it never writes positions, maps, actors, health or inventory. The initial shorter browser input and first native candidate failures are preserved in the ignored first-attempt candidate, including their logs. The final tests retain the same presentation and frame-budget assertions.

Source `0f4e0963cc2968959951b7fa9d1f7560d922c2d3` is frozen against 508 input hashes.

| Cartridge | Bytes | SHA-256 |
|---|---:|---|
| egress.gbc | 1572864 | `60ad1ff72f39b0ae2e6a369ddb571807f002ef36733316e60c8e0e2b723baa20` |
| egress-rtc.gbc | 1572864 | `6a2f2cfc3528e6fe59722e52ba7a104cfe453b701915ab7b7a9f2b47a63411dc` |

This remains the ten-floor office development campaign. Physical cartridge playback has not been tested for this pair. Publication is recorded separately.

## Published release

[PR #9](https://github.com/Ravonus/egress.exe/pull/9) merged as `3cf8164f8974c342cf962964a55677f0f5b5d3a6`. The [public player and downloads](https://egress.onkeel.io/) serve this exact pair. The live browser test moves from window x=8 to x=0 while the original 2 enemies remain alive, reaching global x=63. Actual ROM downloads and the browser player match the frozen standard/RTC hashes. Layouts at 1280/768/390/320 pass with no page errors or overflow.

Website source is committed locally as `94b6fd2c3b17f79020d76efbd84b94e3fe334f03` in its checkout with no Git remote. All 65 pre-existing changed paths retain their status and bytes. Only the primary game worktree and main remain; the merged feature branch is deleted locally and remotely. Private test output is archived under the ignored candidate. No temporary production-host test checkout was created for this release; the prior temporary directory remains absent, and production rollback backups are preserved. See `site-release.json`, `cleanup.json`, `live-site/report.json` and `live-combat/report.json`.
