# Fresh procedural runs, combat encounters and visible edge construction

New runs now construct spatial office stages from the existing codec instead of repeatedly opening with the fixed push lesson. The opening deals XS, S or M dimensions, four geography shapes and hold/latch/either/both switch conditions. The next spatial stage deals cold, power or a falling-load chute; later spatial stages deal transport or mixed apparatus. Floor roles remain recognizable while the run seed determines their construction. Continue preserves the current run. Death and explicitly abandoning a run roll a fresh seed; settling the previous journal no longer overwrites that new seed.

Generated puzzle openings contain real matrix enemies. They use the ordinary movement, attack, damage, animation and kill paths, then let the player solve the spatial puzzle. A native encounter owns its current window until cleared, with a clear message at the continuation boundary. This is bounded encounter integration, not a completed persistent actor stream across the entire large world. Survival stages still require killing actual waves; their generated arenas now vary their void-edge silhouette, while keeping all required ground connected. Waves already increase population, health, speed and roster diversity with stage difficulty.

The construction effect is visible again. Incoming slabs assemble at the leading camera edge; trailing slabs crack and fall while their last pixels remain visible. The scanner gives those edges priority within its existing six-probe ceiling. A bounded landing sweep keeps busy cards from drifting into the central view. Streamed camera easing can move four pixels per tick, so that path inspects one complete edge and lands at most two slabs; office combat retains its smaller allowance. Visible terrain gets its early allowance before speculative world reads. Ready world bindings also get an early slot, independent of cosmetic work; outgoing cache saves precede the remaining late cosmetic animation. Large-world streaming also reserves an early binding slot and retires one offscreen quad near an approaching boundary. Generation retains its six-query ceiling with a line-55 admission deadline. Once the requested strip is generated, the other axis can warm while outgoing work finishes. Late clearing and saving prioritize the requested transfer; speculative clearing cannot consume its remaining save allowance. The mandatory swept 12 by 12 foot box and seven-pixel roll/blink lead remain independent of cosmetic work.

Constructor version bits occupy the existing saved slice byte. Journals from the previous public ROM retain their original physical, survival and spatial maps. Explicit legacy motif fixtures keep isolated crafting, collision and migration regressions reproducible; separate ordinary-run tests exercise the real procedural entry, actual fights, survival waves, real enemy deaths, family generations, Continue and abandoning a run.

`test/harness/procedural_runs.py` compares complete generated material boards, not just seed bytes. Its pinned-seed sample checks 24 distinct maps, all three opening size profiles, multiple dimension pairs, all four shapes and multiple switch conditions. Normal combat independently checks visible animation and exact completed-update/VBlank deadlines. Camera observers require both visible leading assembly and trailing falls, reject construction outside the 24-pixel edge band, and reject unbuilt central cells.

The 24-run sample also compares floor-versus-void masks: 22 distinct playable outlines, with 22 nonrectangular footprints. This is separate from changed object placement or map dimensions. The [shape atlas](generated-shape-atlas.png) shows nine actual generated material boards at the same cell scale.

The clean-link object order places main's existing prefix before the fixed upload state, keeping the GDMA pixel buffer page aligned on both mappers without extra WRAM. The full campaign remains in development; this release does not declare all sprints complete.

## Verified release

Tested source: `6241c3d41f103ec5d58c7b29bfda6574b7bddff2`. Full serial `PYTHONUNBUFFERED=1 make -j1 test` passed with zero failures, including the 1,000-run curriculum audit. All 261 source-file hashes and both ROMs still match the frozen candidate. The full raw log is retained in `build/candidates/visible-edges-fresh-runs-20261017/native-full-test.log`; its SHA-256 is in [native-full-gate.json](native-full-gate.json).

| ROM | Bytes | SHA-256 |
| --- | ---: | --- |
| Standard MBC5 | 1228800 | `75fe5e766b762445b8d0db40535a826a65135d55de1b8f2a4cbe3ce73c6a1391` |
| MBC3 + RTC | 1245184 | `5f94ccf9a7af9909fa259a13b82cd562a08ae94d59f18663ecaa1260141c5abe` |

Both mapper versions pass the normal opening fight, puzzle, survival waves and following puzzle through inputs, plus real enemy deaths and new family generations. Both reject seed reuse when abandoning a run and preserve the current map on Continue. Native migration compares the preceding public ROM’s complete opening, survival and spatial maps.

The twelve normal late-floor world routes on each mapper solve without missing bodies, pixel errors, unbuilt foot cells or frame hitches. The maximum recorded seam wait is one frame on either mapper. Both office-camera routes retain visible leading assembly and trailing falls with no unbuilt central cells; generated-opening observers also require both visible effects.

The exact-ROM private browser checks verify advancing sprite buffers, three portraits matching native pixel structure, a real old-save heir carried over unchanged, actual floor pickups merging through four painted phases and finite material consumption, and both ROM downloads on desktop and narrow screens. Prior hashed ROM assets and browser save identities are retained.

[PR #4](https://github.com/Ravonus/egress.exe/pull/4) merged as `49562662200a3be452248b526412358bbc6212f3`. Published at https://egress.onkeel.io/. All 25 changed production files match the tested export. Public HTTP readback verifies both ROM hashes above and preserves the previous standard ROM. Live browser checks confirm the exact new cartridge, advancing animation and both downloads at 1280, 768, 390 and 320 pixels, with no page errors or horizontal overflow.

The site’s nine release files are committed locally as `f5dc263e7e03d07cfdc921c13a8ed93d5a0defe1`; that checkout has no remote. Its 65 preexisting changes remain byte-for-byte and status-for-status intact. The merged task branch is removed locally and remotely; only the primary game worktree remains and no game branches are unmerged. The private preview is stopped. Existing user files under `docs/chromatic/native-ui-20261015/` are preserved.

Production rollback copies and a before-file manifest are at `/opt/velatis-gateway/backups/egress-visible-edges-20261017T142736Z`. Old immutable ROMs and assets were retained. No physical cartridge was written.
