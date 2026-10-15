# Egress ROM and cartridge-site release — 2026-10-15

Published to https://egress.onkeel.io/ and https://egress.onkeel.io/chromatic/.

The browser player and standard download now use the completed B2 cartridge instead of the older 81,920-byte image.
Both downloads are versioned by their own SHA-256. The prior standard hash is retained for the existing browser-save
carryover path. GitHub and ROM navigation remain in the existing site header; phones use icons with accessible names
and tooltips. The existing game scene, controls and theme are preserved.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Standard MBC5 | 1,064,960 | `8191bc71cd5fb0067dabf58c8886e072658a5053286a0d98c890b299c09e413e` |
| MBC3 + RTC | 1,064,960 | `df11b82b8e14d164aedb75b9d48a4ec73e2c2ad137e420654c2c6364d3d28bd8` |

## Metadata and switching games

Both editions have named library cards, actual gameplay previews, creator, version, description, source/license links,
mapper/save requirements and a hash-bound portable metadata file. `game/releases.json` indexes the ROMs, previews and
metadata. Imported ROMs and full-chip backups of our releases are recognized by exact release-byte hashes.
Metadata never modifies the ROM. Deduplicated files retain all ROM/save backup-group associations.

The cartridge panel now offers choose ROM → two-read verified backup → explicit review/confirmation → direct USB
flash → ROM/save readback. Selecting an earlier backup restores its paired save. A missing paired save blocks restore.
The current adapter supports ROM-only and MBC5 images within 2 MiB ROM / 32 KiB save access. RTC/MBC3 carts need their compatible
external programmer; oversized images are blocked. Protected bytes must match the current verified backup and are
excluded from the write. No data is staged in ESP32 firmware flash. Interrupted jobs never retry or cancel automatically.

## Validation and limits

- 54 shared cartridge/metadata tests pass, including identity changes, write refusal, unplug, corrupt readback,
  protected bytes, and late duplicate data requests. The existing live-input prototype remains intact and unexposed.
- 33 existing site emulator/save/interaction tests pass; the exact new cartridge runs in binjgb and round-trips SRAM.
- Static production export passes. Browser checks pass at 320, 390, 768 and 1280 pixels: icon controls, no horizontal
  overflow, exact download filenames/bytes, GitHub links and real browser gameplay.
- A simulated browser cartridge completes backup → Egress install → original ROM/save restore. Both restored files
  match their original hashes. Repeated identical backups retain the correct paired save. RTC writes are blocked.
- All 45 deployed files match the tested export byte for byte. Live browser checks confirm the preview images,
  verified local import, write guards and the signed v0.2.0 Mac helper link from the concurrent helper release.
- No physical device was opened or written by these tests. Physical acceptance of the new browser writer is pending.

Native game source: `92011bd`; canonical shared cartridge implementation: keel-engine `47d8f9a`, with ROM-only restore fix `b651836`.
Site implementation: `7b46e0a`, with compatibility and repeatable browser checks in `4a16787`. The latest helper site commit `fcbfbd0` is preserved. Pre-existing site edits and native-UI evidence were left intact.

Local raw evidence: `build/site-release-20261015/` in the game repository. Immutable files and prior ROM URLs remain
on the host for cached pages. Mutable page/alias rollback copies are at
`/opt/velatis-gateway/backups/egress-rom-20261015-2340` (final compatibility update), with the original release rollback at
`/opt/velatis-gateway/backups/egress-rom-20261015-2329`.
