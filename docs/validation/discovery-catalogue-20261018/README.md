# Verified discovery, companion and reactive-terrain candidate

Source `ad859cab67bf1807169778200a87950e8bc3f5d8` is frozen against 503 exact input hashes in `source-manifest.json`. All 43 focused phases and the complete native `make -k -j1 test` regression pass for this pair. Source inputs and ROM bytes remain unchanged across acceptance. Website unit, asset, type and static-export checks pass. All seven browser gates are rerun on this exact final ROM pair. The earlier functional predecessor was not published: the owner rejected the sky arrival, tried a rise from below, then authorized retaining only the departure if placement could not read clearly from space. This pair uses that fallback during play. Publication and host cleanup are recorded separately; this document does not claim physical cartridge playback.

| Variant | Bytes | SHA-256 |
| --- | ---: | --- |
| Standard MBC5 + RAM + battery | 1,572,864 | `a9d31dcf3c41d66f9b783388061a8f16c4aa9bded13ca7740e26234de5ecb1c1` |
| RTC MBC3 + timer + RAM + battery | 1,572,864 | `e39dbffe56e966c6a2b7ec010a4c3a4d148257e292cb0a2865225703b2808c62` |

Both images contain 96 whole banks and valid CGB-only headers/checksums, below the 1,900,000-byte limit. A save battery alone does not provide a real-time clock. This game epoch requires a fresh save; old progress is rejected without rewriting it during boot. Clock records retain their separate format.

## Actual native presentation

Incoming floor is prepared offscreen, a full tile earlier, without an OBJ upload or a flight slot. The complete backing map remains ready at the initial load. Fallen cells are restored after leaving view, and an immediate reversal uses a stationary canonical card only until its BG commit. Whole departing cards retain their approved sixteen-pose downward flight. Initial assembly retains the short rise from below while the loading scene is empty; roaming does not run an incoming flight.

The controller-driven camera recording observes 17 offscreen restorations and no moving incoming cards during play. It independently observes visible trailing drops on both travel legs. Requiring the departed offscreen cell to remain blank would contradict the selected preloading behavior, so the camera test now measures its actual departure rather than its later offscreen presence bit. Existing exact canonical-art, departure-trajectory, reversal, full-body, floor-support, 24-pixel rim, board/inventory, strict 120-update timing, OAM and stack-guard checks remain. Native GIF timing alternates 20/20/10 ms to preserve 60 Hz without interpolation.

The same 24 authored substance silhouettes serve ground cards, catalogue icons and the native fusion scene. Mud is an earthy puddle/clump, water a droplet, air a gust, fire a flame and metal an ingot. Derived marks preserve the substance outline. All 432 non-critter identities have distinct packed bodies. Connected floor/carpet keeps the original slab palette with quieter Bayer weaving. The owner's three unchanged reference screenshots are retained under `reference/`; they are targets, not presented as current-ROM captures.

Offscreen preloads no longer starve item idles: sparse uploads use spare time after terrain, without taking its cold-card allowance or adding combat work. Pause and assembly suppress OAM DMA while composing. Both native portal routes verify actual animated-item destinations and uploaded bytes across all four poses, as well as the independent BG reference, sprite-line cap, floor support, backtracking and real-input completion. The full native heir checks verify body layers, frames and facings against compiled art.

## Monitors and stamina

Lift strength limits monitor count multiplied by tiles raised. Strength 1 raises one monitor one tile; strength 2 permits one monitor two tiles or two stacked monitors one tile. Direction release does not reset the height budget. Raising spends stamina; supporting the load continues draining it and blocks normal and haste recovery. Moving clear allows recovery. Exhaustion or excess weight breaks the brace and gravity resumes. World-supported monitors do not charge the player.

Early heirs and blade, whip and launcher classes have strength 1. Hammer and gauntlet heirs with power 2 reach strength 2 from generation 6; hammer heirs with power 3 reach 3 from generation 18. Class and inherited power also change stamina costs. The four live bytes occupy existing two-byte slots at DB9E and DBBE, with size assertions and no growth of general WRAM. DBBE/DBBF are interpolation keys only during world preparation; lift load/strength uses them after the complete store is ready. Native world gates reject default terrain queries during active play.

Both mapper builds pass 13 controller-driven lift assertions, including native strength calculation after a complete real reset, stack limits, retained height, no held recovery, a plank-supported load, exhaustion and the floor stack guard. Separate native-generated CRT courses use ordinary input with no board, position or stamina writes; both retain exact 120-update timing, zero late completions and the sprite-line limit. Their GIF is every actual native frame.

The landing profile exposed a full-room C shutter mutation scan. Native code now skips non-gate cells; the portable host retains its complete scan as the independent reference. The measured landing gravity pass fell from 101,324 to 62,084 cycles, about 39 percent. The full cartridge gravity parity gate validates complete boards on both mappers. Combat admits one queued terrain operation on alternate steady-input frames, including at most one cold departing card, while new-input, attack, push and gravity-heavy frames retain actor priority. Swept feet and existing departures keep their normal updates.

## Finite discoveries and individual lives

The shared catalogue defines 444 identities: 384 materials, 24 fuse bombs, 24 rechargeable tools and 12 critters. Its 5,283 symmetric input pairs include 443 canonical chains. Every discovery is reachable from FIRE, WATER, AIR, EARTH and METAL. Canonical chains use at most 28 base units; alternate recipes reduce the largest minimum to 22. Maps supply base reagents. Knowledge of a recipe does not create ingredients.

The bag packs 24 nine-bit identities into 27 bytes. A separate 56-byte collection bitmap has paired CRC-protected 64-byte journal records. The native MIX, BAG and LOG pages remain separate. The actual two ingredient contours merge into the actual collected result through a shared 1,126-byte paused SRAM workspace. Materials spend finite units, bombs have a fuse, and tools recharge. No extra actor, terrain-card, projectile or VRAM pools are introduced.

Twelve directional critters cover six roles and 24 named abilities. Each retains its own health, cooldowns, instance ID and spent revival charge through swaps and matching save checkpoints. Ordinary death is permanent. Vine Frog's Second Spring revives that individual once at half health; its next lethal hit is permanent. Medicine heals the heir first, then a living companion, and remains on the floor when both are healthy. Ordinary healing cannot revive a dead companion.

Both mapper builds pass 76 companion assertions, 21 crafted-effect assertions and 21 environmental assertions each. Main-ROM tests cover collection, commands, damage, death, revival, healing, combat, resets, save continuity and old-save rejection. Native fixtures supply explicit starting inventory or encounters; outcomes execute through cartridge code and controller input.

Plant VINE on clear ground, water it into a sprout and then solid cover, burn it with embers and rising smoke, and replant the ash. Cold freezes bounded patches of natural water, with shore/wall occlusion and stronger area forms; heat thaws them. Growth waits for bodies to clear. Material and skin persist together across actual camera transfers. Environmental fields reuse the existing orb records with global coordinates and one terrain transition allowance per tick.

## Procedural and timing evidence

The 768-room bank contains 128 solver-accepted base-material rooms per tier. Its SHA-256 is `44c70db7abaf2b84bfd9a6ae30993c3a637ed49754926d3e3876ec99d043d598`. Shared host/cartridge rules cover finite spending, alternate CRT/crafted routes, switches, named circuits and portals. New runs receive new seeds; a token reset restores the current floor. The fixed tutorial is a separate stage.

Both mapper builds clear three survival difficulties with actual attacks and kills, escalating waves and off-camera entries. The largest case requires 26 kills over four waves. Strict simulation/display deadlines, HUD rows, particles, mob and shot requests, full bodies and per-line sprite budgets pass their independent native observers. Both bosses complete the original mortal no-hit, phase, hit-count, timing and OAM gates. Controller drivers wait for actual assembly, retain shot aim through release/startup, and stop attacks at real victory before checking the bench; they never inject victory or enemy damage.

All six extended worlds (small, large and XXL on both mappers) retain exact 120/120 updates, zero late completions, zero movement-margin waits and no live default terrain queries. Persistent packets are prepared before play; the 16×14 active view and actor pools remain bounded. Preparation includes native assembly for the tested 32×18, 64×26 and 128×46 worlds. Progress shows real dimensions and completed rows, with no stale sprites.

Separate native oracle cartridges verify 45,056 visibility/order requests, 640 bounded medicine cases, 1,155 CRC journal requests, 3,221 contour requests and 4,843 art requests. These prove compiled primitives; main-game and browser integration are separately checked above.

## Website and limits

The site's playable ROM, download aliases, portable cartridge metadata and native previews share this exact pair. Gameplay images carry a ROM-hash/source-commit capture manifest and per-image hashes. The separate scroll-driven KEEL illustration uses 14 canonical cartridge textures and records their provenance; it is labelled as an illustration. Current native captures replace stale gameplay images. All 65 pre-existing website changes are preserved by byte/status guards.

This remains the ten-floor office development campaign with LAST FAX and THE TENANT. The full campaign, remaining biomes/bosses, family tree/stats pages, adaptive score, playable first-person stages and cycle consequences remain open in `docs/SPRINTS.md`. Emulator proof does not establish physical cartridge playback.

## Published release

[PR #7](https://github.com/Ravonus/egress.exe/pull/7) merged as `ae6bcbc14c96fff1f57061e3639544b4595379e5`. The [public player and downloads](https://egress.onkeel.io/) serve the exact pair above. Public HTTP hashes and a fresh live browser run at widths 1280, 768, 390 and 320 verify the pages, source link, standard/RTC downloads and player ROM identity. The website source is committed locally as `788e2b2539b06162432d3bf933df68e4cc3654e6`; this checkout has no Git remote. Its 65 pre-existing changed paths are preserved. See `site-release.json`, `live-site/report.json` and the deployment receipt.

Both merged feature branches are removed; the only game branch is `main` and the primary worktree remains. Temporary verification containers, copied source/build inputs, downloaded tools and the pulled image are removed; production and rollback backups remain. See `branch-audit-after.json` and `verification-host-cleanup.json`.
