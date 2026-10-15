# Chromatic support: design for approval

Status: proposed, 2026-10-15. Research is in [RESEARCH.md](RESEARCH.md). Implementation, hardware trials and deployment are separate gates. The owner's additional requirement is automatic cartridge-dependent game versions, especially CRUCIBLE builds with fewer baked recipes on smaller carts.

## Visitor flow and placement

Keep the current KEEL room, game controls, chapter rail, audio settings and Play behavior. Add **On a Chromatic** in the existing Where to play / On glass section, and offer it after the visitor leaves Play, below the existing action strip. Do not interrupt play or open a chooser from the Play gesture. The new route `/chromatic/` uses the site's existing header, pixel font, dialogue frame, colours and cast. Back to game returns to the preserved game/tab state. Recovery has its own static `/chromatic/recovery/` route, independent of the room and emulator.

The contained mocks show the new route and the post-Play offer. EGRESS uses its indigo/cyan/pink frame and TICKET. CRUCIBLE uses its pink/white/violet frame and FIG.EXE. These are proposed additions, not a replacement for the world scene. Ordinary prose explains facts; the cast supplies one short line. Critical instructions do not depend on typewriter animation, sound or jokes.

Screen sequence:

1. **Connect**: immediately list already-granted devices without opening them. Explicit first button grants the console; second grants GWU2X. Explain why there are two. Canceled permission leaves the completed step intact. Disconnect/reconnect events update state; no repeated polling chooser. Multiple devices require selection/pairing evidence, never first-device defaults.
2. **Health**: independent check rows with observed, unsupported, not checked, stale or failed status. Passive reads only. No cart-engine load, reset-to-fix, flash identification, Wi-Fi scan or room join. Expose a separate “Identify stored firmware” action that clearly says it stops the running session without writing flash.
3. **Cartridge**: explicitly prepare temporary SRAM access, identify chip/board/mapper/save/protected regions, then choose the best compatible release automatically. “CRUCIBLE selected for this cartridge: smaller baked catalogue” is the normal small-cart result; oversized full ROM is not the only choice.
4. **Back up**: ROM + save or both firmware chips, independently verified, committed to local storage, storage readback verified. Export is available. No write unlocks on an in-memory-only or partial backup.
5. **Review change**: exact target identity, current and target hashes, release revision/profile, changed ranges, save policy, backup location, power evidence, expected interruption and recovery link. Separate named action for each kind of write. Click confirms the frozen plan, not a mutable set of options.
6. **Write → readback → complete**: progress per phase, elapsed time, bytes and verification result. An interrupted write becomes an unresolved operation, not success or an automatic retry. Present resume/recovery only after state reconciliation.

Phone layouts stack the same content and keep 44px controls. Missing WebUSB/Web Serial shows “Connect on desktop Chrome or Edge”; metadata import/export and the local library remain usable where storage APIs work. No fake connected state on mobile. Use native buttons, labels, definition lists, focus return, visible focus and concise live-region announcements. No horizontal scrolling for hashes: wrap them. No colour-only status.

## Dynamic cart capability and release selection

One shared capability model, with no global ROM-size constant:

```ts
type CartCapability = {
  identity: { adapterId: string; boardProfile?: string; chipId?: string };
  romBytes: number | null;
  writableRanges: Array<{ start: number; end: number }>;
  protectedRanges: Array<{ start: number; end: number; readSha256?: string }>;
  eraseRegions: Array<{ start: number; end: number; sectorBytes: number }>;
  supportedMappers: string[]; ramBytes: number | null;
  battery: 'present' | 'absent' | 'unknown';
  features: string[]; // RTC, rumble, etc. only from verified evidence
  evidence: Array<{ field: string; method: string; confidence: 'verified' | 'reported' | 'unknown' }>;
  readerVersion: string; writerVersion?: string;
};
```

Detect JEDEC/CFI via a board-specific documented read-identification route. Tie mapper and RAM capability to a verified board profile or reliable documented report. Never infer physical capacity from the loaded game header. Keep unknown distinct from zero. Adapter limits, physical capacity and protected regions are separate. Adding an 8 MiB-capable adapter does not require rewriting the selector; the existing Tetris/CBU1 adapter honestly retains its own limits.

Each game manifest gains `variants[]`: variant ID, source commit, content-set hash, ROM URL and SHA, actual length, declared address space, intended fill, install-layout hash, mapper/features, RAM requirement, save schema, catalogue fingerprint, exact item/recipe counts, ranking and validation evidence. Keep existing `romUrl` as the browser/default full release. A write resolves one variant and pins it for the whole job, even if a newer release appears.

Recognize an installed release by exact normalized ROM hash and validated layout, using the header only to narrow candidates. An earlier compatible revision offers **Update to this revision** after backup and save-compatibility checks. The current compatible revision says **No update needed**, with backup and **Restore my save** still available. A header/title match alone cannot identify our game. Uploaded ROMs and saves enter the same metadata, capability, backup and review flow; a save-only write backs up the original save and checks the target game's schema and SRAM layout before changing only save storage.

Selection is local: filter by physical ROM addressability, writable byte ranges, protected-tail equality, mapper, SRAM, required peripherals and transport capability; then choose the highest validated content rank. A smaller cart gets the matching smaller game version. If a compatible game variant exists but this adapter cannot program the board, offer its exact download plus the supported writer path. Unknown boards never gain permission through a size override.

Start the build matrix with 1, 2, 4 and 8 MiB ROM targets, crossed with feasible 32K/128K save layouts, plus the special 2 MiB cart with a protected 64 KiB tail. These are candidates, not promises that each fits. The matrix is generated from supported cart profiles, not a hardcoded menu the visitor must understand. Add lower budgets only when the mandatory game core fits and passes tests. EGRESS participates in the same manifest/selector; a smaller declared ROM needs a separately rebuilt and validated variant, not an edited download header.

## CRUCIBLE smart builds

Introduce `CartBuildProfile` in the canonical generator, consumed by baking, table compilation, banking, linker and header finishing. Fields include ROM bank count, writable/protected layout, SRAM layout, mapper capabilities, required core content and deterministic content-order seed/version. Build in profile-specific output roots; never clean or overwrite another session's generated engine.

Pipeline:

1. Establish the source snapshot that produced the current release; pin engine/toolchain and catalogue inputs. Reproduce or explain differences before issuing a new variant family.
2. Reserve actual linked code, UI, music, mandatory Story progression, bosses, starters, quest rewards and save infrastructure. This base is measured, not a guessed percentage.
3. Construct a deterministic recipe dependency graph. Choose additional recipes by progression/category coverage; include their ingredient/output closure, names, dialogue references and canonical art. Every included result must remain reachable. Do not select the first N rows or orphan Story requirements.
4. Reuse cached art for retained objects. Pack records into the target's real banks and protected-region constraints. Derive catalogue chunks and art-directory banks instead of hardcoded bank 511/255 assumptions. Recheck every banked pointer and code-bank constraint.
5. Measure actual packed/link bytes; trim optional content deterministically and repeat if necessary. Keep stable canonical item and recipe identities; map to compact per-build indices where needed. The maximum canonical ID must not accidentally force full-catalogue SRAM bitmaps on a small profile.
6. Generate a truthful header/save profile and exact manifest, including counts and exclusions. Small versions retain the same mechanics and visual rendition of included objects. They contain fewer baked recipes and corresponding assets. Unavailable content stays unavailable; do not imply Wi-Fi supplies it unless a separate native service actually does.
7. Validate reachability, complete Story, catalogue lookup parity, bank boundaries, exact ROM boot, save/reload, version upgrade/downgrade and install compatibility for every offered variant. Only passing artifacts enter the release manifest.

32K support exists in the portable core, but the cartridge wrapper, codec, Story slots, cache banks and header currently assume 128K in places. Audit all SRAM addressing; changing the header alone is insufficient. An 8K CRUCIBLE save profile is a separate design effort if needed, not silently selected. EGRESS currently requires 8K.

Save migration uses canonical IDs and explicit schema/content-set versions. Back up the raw source save first. Upgrade restores retained progress; downgrade preserves out-of-profile discoveries in the browser archive and never reinterprets a compact index as a different item. Show the number of retained/unavailable discoveries before conversion; keep both saves. Previous ROM SHA membership indicates lineage, not proven save compatibility. Linked games must negotiate compatible rules/catalogue sets or decline safely; cross-profile links cannot assume matching dense indices.

## Health and identity

Each observation includes source command/protocol, timestamp, freshness and confidence. A responsive console is not proof that all chips are healthy. Show firmware-reported identity separately from exact catalogue match.

Rows: console response; MCU/overall version; Wi-Fi configured; Wi-Fi connected; SSID; IP; AP signal; relay and link status; FPGA JTAG IDCODE; reported running FPGA version; cartridge presence/readability; power. Each unsupported field reads “This firmware cannot report that.” Cached or unavailable checks cannot be green. No secret fields rendered in technical details.

Deep identity is an explicit maintenance step: pause/end the hardware session, enter ESP ROM loader if needed, read/validate partition table and app descriptors; use bounded sample hashes for candidates; read complete regions for exact identities. FPGA configuration read stops its USB bridge. Store `storedFpgaImage` and `runningFpgaEvidence` separately. Unknown image hashes are recorded only locally. The raw MAC is never sent anywhere; use an origin-local random device record with a salted local lookup key. Recovery without a readable MAC requires manual backup selection and chip evidence; a matching model is not proof of the same unit.

## Shared package and site adapters

Proposed package `@keel-engine/chromatic`, source owned once alongside KEEL workspace packages and pinned into both `vendor/keel/chromatic` snapshots using the existing vendoring/provenance convention. Both sites already depend on `@keel-engine/*` with `workspace:*`. Do not create divergent copied protocol modules. GPL engine source/build artifacts stay a separate, explicitly licensed component; an npm package boundary does not itself resolve licensing obligations.

Modules: `transports/serial`, `transports/webusb`, `protocol/esp`, `protocol/gwu2x`, `protocol/gw5a`, `protocol/cbu1`, `capabilities`, `rom-metadata`, `release-selection`, `catalogue`, `library`, `operations`, `simulator`. Interfaces accept injected clocks/transports/storage; browser globals are only in adapters. Hashing, parsing and large-file IO run in workers. Per-site React route components and cast copy remain local.

State machine has separate discovery, passive observation, disruptive read, SRAM preparation, persistent-write plan, backup, confirmation, write, verification, recovery and completion states. Only the persistent-write executor receives a verified backup token plus a frozen, hashed plan and consent token. Consent is invalidated by any device/target/save-policy change. Expired observations and disconnects invalidate write preconditions. Expose no arbitrary offset write API to UI code. Transport timeouts do not cancel a USB operation that may still be running; retain an uncertain state and reconcile before reuse.

Use Web Locks per origin and operation journals; instruct one active device page across the two site origins because origin locks cannot arbitrate each other. Do not revoke all grants as routine reconnect. Close and forget only explicit selections when requested.

## Local library model

IndexedDB stores metadata/indices, OPFS stores large immutable blobs addressed by SHA-256. Fallback IndexedDB Blob storage is quota checked. Both are origin scoped: the two sites share code, not automatic access to each other's private library. Cross-site transfer uses an explicit local export/import bundle; no upload or secret-bearing URL.

| Store | Key fields |
|---|---|
| blobs | sha256, bytes, OPFS relative name/storage kind, pending/committed state, verification |
| assets | UUID, blob SHA, kind ROM/save/ESP/FPGA, original name, editable display name/tags, timestamps, sensitivity |
| games | canonical game ID, variant/revision links, catalogue fingerprint, save schema |
| romMetadata | title/CGB/manufacturer/licensee, cartridge type, ROM/RAM codes, declared/actual size, header/global checksums, padding policy, source and normalized hashes |
| devices | local UUID/lookup key, user label, chip facts and confidence; no cloud ID |
| observations | device ID, timestamp, region hashes/lengths, reported and matched firmware, source evidence |
| backups | backup UUID, device/cart identity, blob refs, complete coverage, two-read/device-digest proof, stored-readback proof, sensitive flag |
| timeline | device ID, observed/backed-up/restored event, before/after identities, backup/operation links; an observation is not a write |
| operations | UUID, frozen plan digest, device epoch, backup IDs, phase, sector/chunk journal, last verified range, uncertain outcome |

Write blob to a temporary OPFS name, close/flush and read/hash it, then commit metadata; reconcile orphan temp files and pending rows after interruption. Dedup immutable content without collapsing separate asset names/history. Delete metadata references first; garbage-collect unreferenced blobs only. Retain backups used by unresolved operations. Quota or persistence denial prevents writes if durable recovery cannot be established. Show used/available estimate and offer `persist()`; never promise permanence. Private windows, browser clearing and origin changes can remove data.

Full ESP backups are marked **Sensitive: includes Wi-Fi and pairing credentials** in list, review and export manifest. Offer encrypted local export; encryption is distinct from integrity. Use a local file picker; warn that a chosen synced folder may upload outside the site's control. No service worker cache for private blobs, no analytics, external fonts/scripts or remote error reports on device routes. Public assets/catalogue may be cached by known public SHA. Local logs are allowlisted and sanitized before storage or optional export.

## Backup and firmware rollback

ESP32: read full detected capacity and verify using device MD5 against local MD5 or an independent reread; calculate SHA-256 for archive identity. FPGA: read entire 4 MiB on the supported profile twice and compare; calculate SHA-256. Commit and reread saved blobs. A corrupt but consistently read broken-firmware dump is still a valid raw recovery backup, with parsing marked invalid. Never discard it because the boot header is damaged.

Firmware restore choices: app only, selected chip, or full two-chip backup. App-only preserves current NVS/PHY/bootloader/table and needs verified layout/boot compatibility, target image bounds and eFuse security compatibility. NVS preservation does not guarantee that an older application understands new settings; flag unknown schema compatibility. A full restore is a sensitive restore of personal settings. “Go back to stock” appears only for a positively identified stock backup; otherwise offer the official updater.

Before each persistent write: live power evidence if available, otherwise an explicit charged-battery/stable-power acknowledgement; fresh verified backup of all affected storage, complete plan, and confirmation. No inference that USB presence means safe power. Firmware writes require full backups of both chips as the normal gate. In recovery where one chip is unreachable, back up the reachable chip and limit repair to it using an earlier matching backup; make the missing evidence explicit in that recovery confirmation, never silently waive the normal gate. If the owner's strict full-backup requirement cannot be met, block that restore and show the official recovery route.

Sector writes use read-modify-erase-program-verify with preservation of unchanged bytes inside erase sectors. Skip sectors only using reliable comparison; CRC is transport integrity, not archival identity. Under the task's hard readback requirement, every changed persistent range is read back and SHA-256 checked. MD5 can speed backup verification and skip decisions, but does not replace final write readback. Verify preserved regions too. Check flash status/protection without clearing protection opportunistically. Reconfigure/reset only after verification, then confirm reported/hashed target state. Never silently auto-rollback on an uncertain write: reconcile original operation and present its known recovery plan.

## Recovery page and failure matrix

Static, lightweight, self-contained route with public assets cached ahead of hardware work. Exportable offline instructions. No room renderer, remote catalogue fetch or working application firmware required to show the page. Device permissions remain secure-context/browser dependent. Steps below are a test plan, not hardware-certified instructions.

| Failure | Recovery behavior |
|---|---|
| Unsupported browser / insecure context | Library stays available if possible; explicit desktop-browser guidance; no chooser |
| Canceled chooser / denied permission | Preserve other grant; retry only by visitor gesture |
| Missing/ambiguous/crossed devices | Stop pairing; unplug other units; no destructive test in Health |
| Console silent or unrecognized | No reset from Health; offer maintenance/recovery with exact effects |
| Port busy | Explain other updater/page may hold it; do not kill processes |
| Unknown chip, mapper, SRAM or protection | No install offer; retain verified readable metadata/download option |
| No compatible variant | Show which evidence/requirement failed; do not choose oversized ROM |
| Wrong hash/checksum/length or stale plan | Stop before write; preserve source and backup; refresh plan |
| Low/unknown power, quota failure, failed backup verification | No write; resolve condition first |
| Unplug/brownout mid-sector | Mark outcome unverified; retain journal and backup; rediscover, re-identify and reread affected range |
| Lost CBU reply | Query status; resume only same source/job and verified prefix; never send cancel as generic recovery |
| ESP app broken, CDC still present | Explicit ROM-download control-line sequence; verify chip ID; raw backup; restore compatible app; readback; reset to app |
| FPGA image broken, CDC missing | Connect GWU2X alone; detect chain/flash, raw verified FPGA backup, review prior matching FPGA restore, write/readback/reconfigure; then attempt CDC recovery |
| Both images broken | Recover FPGA bridge first only from approved prior backup; then ROM loader. Stop if bridge/power/identity cannot be established |
| Readback mismatch | No success/reset-to-normal claim; preserve mismatched range and recovery options; no automatic erase/retry |
| No usable backup / security-locked device | No invented stock image or bypass; official updater/support link |
| Reload/second tab | Reload exact journal; device state wins over cached completion; no automatic write |
| Save schema/content mismatch | Keep originals; offer explicit migration only if validated; otherwise export and block save write |

Recovery test must demonstrate GWU2X enumeration and flash access while ESP32 app is broken or held in download mode, including whether CDC returns after FPGA reconfigure. Record exact OS/browser, chip IDs, control-line timings and screenshots. Do not induce broken firmware on the owner's device until backups, recovery plan and explicit hardware approval exist.

## Speed plan and acceptance

At 115200 baud with 8N1 the line ceiling is 11,520 bytes/s: 4 MiB takes at least 364.1 seconds for one raw pass before framing/flash overhead. 460800/921600 would reduce that transport-only floor to 91.0/45.5 seconds. The temporary engine source includes these two higher UART divider rates, but the published CBU1 baud command rejects them and the internal cart link is separate. Higher-rate esptool ROM/stub reads are an experiment for a proven bridge image, not a promised optimization.

| Phase | Proposed target / experiment | Evidence now |
|---|---|---|
| Granted-device discovery | <1 s bookkeeping; prompt wait measured separately | Not hardware measured |
| Passive health | 2–5 s supported responses, bounded timeout per unsupported check | Not measured |
| Quick identity | 2–10 s descriptor/sample pass, plus required reset overhead; exact hashes may take minutes | Estimate only |
| ESP backup | Baseline >=364 s at 115200; test stub 460800 then 921600 with fallback and full digest verification | Candidate only |
| FPGA backup | Batch larger read windows, reuse session safely; compare double-read time | No measured target yet |
| Cart backup | Sparse FF-bank transfer + independent verification; benchmark real byte counts | Reference already skips blanks |
| Cart install | Stream binary requested chunks; sector diffs where protocol permits; full readback; smaller variant reduces data | Not measured |
| Engine/catalogue | Cache public bytes by SHA; parse once in worker; separate cold download from transfer | Can measure locally later |

Do not parallelize FPGA flash access with ESP serial. Parallelize CPU hashing/OPFS IO with independent transfers only where bounded and safe. A persistent firmware change to improve baud is never implicit in an optimization.

Benchmark both tools on the same hardware/browser/cable/firmware/cart/content and the same verified backup/write semantics. Three or more paired runs; alternate order, record cold/warm cache, data sparsity, retries, user wait and transport time separately. Record median/range and total time including stronger verification. Compare detect, health, identify, firmware backup, cart backup and install individually; do not claim faster full firmware backup by omitting FPGA's second read. Baseline writes are hardware operations requiring their own approval. No timings have been measured yet.

## Simulator and delivery gates

After design approval, implement deterministic simulated serial console, ESP ROM/stub, GWU2X TAP/packet transport, W25Q32 erase/program rules, and cart boards of several sizes/protection/mapper/RAM combinations. Inject unplug, brownout at every boundary, short/corrupt reads, lost ACK, wrong ID, crossed devices, stale grants, flash protection, quota failure and reload. Track attempted writes in an independent audit sink; no Health/read path may write persistent storage or load SRAM.

Use `node --test` for parsers, capability selection, game profile graph closure, save migration and operation invariants. Model forbidden writes independently of implementation logic. Playwright stubs Web Serial/WebUSB and storage failures, tests two permission gestures, accessible focus, 320px layout, interrupted-operation recovery and no network leakage after public assets load. Run each site's existing `node --test test/*.test.ts test/*.test.mjs` and `browser-tests/` after integration. Check `uptime` before suites/builds; use the other Mac after disk check when local load is over 40. Do not offload secrets/device dumps.

Hardware gates in order: (1) owner selects device, passive read-only checks; (2) separately approved disruptive identification; (3) full verified firmware/cart backup; (4) capacity/profile selection; (5) explicit write and readback; (6) rollback and recovery validation; (7) paired performance trials. Before each run state exactly what is read, what is written, and whether SRAM/session state changes. No deployment until the owner requests it.

Approval requested now: the additive route/offer placement and mocked screens, with automatic cart-specific versions. Approval does not authorize a physical write, third-party image hosting or deployment. Remaining implementation includes shared package, simulator, profile baker, both site UIs and tested recovery.
