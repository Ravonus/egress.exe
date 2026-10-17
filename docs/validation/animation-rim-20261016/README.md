# Continuing animation and peripheral construction

The previous release could defer a new full heir pose every time the draw pass ran late. A real holdout/movement reproduction left a requested pose unserved for 38 active samples while its logical animation advanced. Keeping all body layers visible did not prove that their animation progressed.

Deferred poses now get an early opportunity before speculative terrain work. Gravity and action replacement have priority, and intentionally invisible damage-blink/fall frames do not spend time composing hidden bodies. A pending complete upload cannot be overwritten by a second composition. The compressed stream stops after its final selected gene, including selected empty deltas.

Office combat reuses inactive world-memory banks 2 through 5 for 120 complete composed poses in 60 two-way sets. Cache leaves run without stack access while SVBK is switched, restore SVBK before returning, and run within a critical section. The pixel buffer and cache enable flag stay in fixed WRAM. A new expression disables reuse; floor sprite initialization clears the keys. Generated worlds keep exclusive ownership of those banks and bypass pose caching. Their normal world initialization resets all cache certificates before loading payloads.

The central camera view and two cells outside it stay warm, including during reversals. The existing one-cell cosmetic preparation allowance sweeps the approaching offscreen edge instead of rebuilding around the character. All four movement-footprint corners and the swept roll lead remain protected. Object/puzzle data and genuine pits are unchanged.

The mandatory sweep covers the next 7px step (the fastest blink moves 6.5px per tick). The previous 14px point could skip adjacent cells in a diagonal crossing and make the safe mover stall. Cold-strip tests now exercise 7px cardinal and 5px diagonal blink steps in all eight directions with every foot corner built and one update per display.

Sprint and evade share the player presentation state. Preparation now checks the actual input move before retaining an evade vector; a held sprint uses the held direction even when no evade has happened. Eight additional cold-strip cases exercise uninterrupted held sprints with zero evade vectors. The generated-world portal route for seed 0x5100 also catches this stall through ordinary play.

Generated-world preparation completes the requested strip before binding a speculative perpendicular strip. Its six-query generation pass admits work through line 40 instead of 35, while later presentation and saving retain their existing deadlines. All twelve normal world routes solve on both mappers; the longest observed seam wait is nine frames, within the existing twelve-frame gate.

The common byte-coordinate overlap primitive uses the same strict absolute distances in register-only SM83 code. The exceptional wide-coordinate path remains separate in a ROM bank. A native test checks all ordered byte-coordinate pairs along both axes at ten radii, plus mixed boxes: 1,314,816 comparisons. Late particle drawing retains lifetime updates without calculating unused projections.

Regression observers check actual composed/uploaded pixels, requested-pose progress, visible sprite buffers, complete layer counts, central-view terrain and camera-rim animation. Camera combat timing observes completed updates against real VBlank epochs and rejects late completion; raw line-zero counter samples are retained because ISR-return phase can pair a zero and two without skipping an update.

The stress trace also exposed an EI/HALT boundary race: VBlank could be serviced, then execution returned to HALT and slept until the next speech-timer interrupt. The wait now halts with IME disabled, uses a harmless NOP for the pending-at-HALT duplicate fetch, and enables/services interrupts before checking the epoch again. This retains low-power waiting. The behavior is documented in [Pan Docs](https://github.com/gbdev/pandocs/blob/master/src/halt.md). The regression checks input-start phase as well as completed-update deadlines.

Preparation avoids decoding a multi-tick pose that will advance in the same update, while terminal and one-tick frames remain eligible. Completed poses that did not fit the late cache allowance get the next early allowance before their pixels can be replaced. Existing fields align the fixed-WRAM pixel buffer to a page without adding RAM, eliminating per-patch carry arithmetic and allowing byte increments in bounded copies. Eight CGB GDMA blocks upload its exact 128 bytes in VBlank; both mapper links gate its alignment. The pixel observer checks the actual completed VRAM upload.

The HUD copies fixed rows in a bounded VBlank handler. Periodic clock arithmetic runs after complete actors when enough time remains; an observer verifies exact elapsed-VBlank arithmetic and bounded polling lag. Late particles advance through a register-only path, checked against their original lifetime, position and velocity rules. Mob overlays use the same allocator and coordinates through a register-only path. One-to-four-column sprite groups clip through the same visible-column rules without the C spill overhead; larger unusual groups keep the portable path. Native observers check all allocation writes against the portable rules and every marker/overlay sprite request against the art rules.

Mob bodies also use a twelve-byte stack packet, preserving all body, tell, lunge, flyer, underground, freeze, elite and flash rules. A separate native fixture gate exercises 7,680 combinations per mapper across every overlay mask and face, eight states, five animation phases, status variants and screen-edge positions. It compares exact body/marker/overlay requests and allocator writes with the portable rules and checks stack balance. These are explicit renderer fixtures, separate from ordinary generated combat and its strict frame deadlines.

Projectile drawing uses an eight-byte stack packet and retains the complete twelve-bit pixel-coordinate range. The same fixtures exercise 50,000-plus projectile requests per mapper through coordinate 4095, alongside all visible-column clipping cases; ordinary waves independently verify real enemy projectiles.

Harness accesses to game data in D000-DFFF explicitly select WRAM bank 1: an emulator display boundary can pause inside a stack-free pose-cache copy with another bank selected. This changes inspection/fixture addressing, without writing the CPU's bank register or advancing extra frames to normalize observations.

## Verified release

[PR #3](https://github.com/Ravonus/egress.exe/pull/3) merged as `e9e3ff3cd8982f90d612530e7ef0ed1729237899`. Tested game source: `212adf04bdc1ea32d1595cd8cc6282fe870849c3`.

| ROM | Bytes | SHA-256 |
| --- | ---: | --- |
| Standard MBC5 | 1,228,800 | `03597246c14a74e74e566e61bb4eb810bfe9f4d0827cf8fcc2234a17f00087d5` |
| MBC3 + RTC | 1,228,800 | `2faab26f0f34edbf82e52ee4c05bde69cd1b10f6851f21a0090e1a45a2719a92` |

- Full serial `PYTHONUNBUFFERED=1 make -j1 test` passed with zero failures. Both frozen ROMs and every source-file hash still match the tested build. The complete raw log is retained at `build/candidates/animation-rim-20261016/native-full-test.log`; its digest is in `native-full-gate.json`.
- Each mapper passed 2,932 canonical native uploads and 4,091 selected-empty-delta cases with zero pixel mismatches. Renderer fixtures cover 7,680 actor cases per mapper plus full-range projectiles and allocation parity.
- Camera combat retained complete bodies and advanced visible buffers on both mappers. Maximum unserved pose: three frames on MBC5, four on MBC3. All normal wave cases meet their strict 120-update/120-VBlank deadlines.
- Each camera-travel run observed 1,163 samples with no unbuilt central cells or central construction. Both mappers pass the eight-direction blink and held-sprint cold-strip checks. The generated-world suite covers 42 routes plus both reset/dead-room checks; the maximum seam wait is nine frames.
- Exact-ROM browser play verifies changing sprite buffers. All three heir portraits match native pixel structure. A real legacy family save retains its inherited genome; real floor pickups combine through four painted crafting phases and consume the finite ingredients.

Published at https://egress.onkeel.io/. All 42 changed production files match the tested export. Public HTTP readback matches both ROM hashes above and confirms the previous standard ROM remains available. Existing browser save identities and all 47 baseline game assets are retained. The live animation and download reports accompany this record.

The site's nine release files are committed locally as `ed98df9f9ce0ad4d90b841cc29094cb137f86d36`; that checkout has no remote. Its 65 preexisting edited/untracked files remain byte-for-byte intact. This task's local preview is stopped and its merged local/remote branch is removed. Only the primary game worktree remains, and there are no unmerged game branches. Historical merged branch labels are retained.

Production rollback copies are at `/opt/velatis-gateway/backups/egress-animation-rim-20261017T042158Z`, with a before-file manifest. Old immutable ROM/assets were not deleted. The other Mac is offline, so two task-owned rsync scratch directories remain there: `/Users/chad/work/egress-animation-rim-20261016` and `/Users/chad/work/egress-animation-rim-site-20261016`. These are copies, not Git worktrees.

This release addresses animation progression and peripheral terrain preparation. The full campaign remains in development. No physical cartridge has been written.
