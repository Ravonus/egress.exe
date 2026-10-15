# Chromatic support delivery — 2026-10-15

User-approved scope continues across the conversation. Preserve the shared native site theme.

## Verified live before the wireless extension

Both sites complete actual console/FPGA handshakes, SRAM-only cartridge preparation, console re-enumeration and cartridge identity. The physical NOCTURNES cartridge was identified on each site. See `preparation-20261015/VERIFICATION.md`. No persistent firmware/game/save writes occurred.

## Wireless preview deployed

Canonical source: keel-engine/packages/chromatic-wireless; shared UI: packages/chromatic.

- Native macOS ARM64 helper, browser client, UDP CWL2 forwarding, origin/token/target/rate checks.
- Canonical Shiny Relay CWL2 core compiled to WASM; service byte adapter with bounded buffering.
- 12 protocol/client/service tests and 4 real socket tests pass.
- Both site builds pass. Both desktop suites pass the original 15 checks; additional HTTPS-to-native-helper-to-UDP test passes with normal Chromium local-network permission, including timeout/cancel. Device in that test is a UDP fixture, not physical hardware.
- Physical device did not answer a single read-only HELLO probe in its temporary cartridge-engine state. No reset or retry performed. No Wi-Fi gameplay claim.
- Wi-Fi page, hash-verified downloads, local progress/recovery and ROM annotations deployed to both sites. All 85 public file hashes match the release. Remote management and multiplayer integration are not finished.
- Shared engine commit b70ee43; EGRESS site 530f09c; CRUCIBLE site 8887ccd. Both sites have 18 covered browser checks; the CRUCIBLE size-error assertion was corrected and retested after the first run.

## Live capture deployed

- Shared Live / OBS view published to EGRESS and CRUCIBLE: explicit labelled Chromatic video/audio, full screen, PiP, frame freshness, normal browser permissions, OBS setup and cleanup on stop/navigation/suspension.
- Both production builds pass; 2 browser capture tests per site pass with synthetic video. Shared suite passes 43 tests including 3 unpublished input prototype tests. No physical capture, audio or remote input is claimed.
- Final capture engine commit 81a982c; EGRESS site 5d8e00c; CRUCIBLE site 6679669.
- Canonical-only input prototype f1508b6: exclusive console lease, fixed self-releasing 80 ms game-button taps, no retries or queued overlap. Not vendored or enabled on public sites pending physical acceptance.
- User was asked to restore normal gameplay with a power cycle after temporary cartridge access. No answer yet; current browser reports no labelled capture device; permission state and normal gameplay still need checking. No private default camera or microphone was opened by the agent.

## Signed Mac helper and public source delivered

- Helper v0.2.0 is published at https://github.com/Ravonus/chromatic-helper/releases/tag/v0.2.0 with complete public source, exact revision, hashes and runtime/license notices.
- Replaced the invalidly signed command-line download with a native double-clickable app: Developer ID signed, Apple notarization accepted, ticket stapled, Gatekeeper accepted, rechecked after downloading the public release.
- Both live sites link the new app and GitHub source. Prior broken executable URLs are withdrawn; backups remain outside the web root. All 75 deployed file hashes verified.
- Both builds and packaged-helper browser/UDP fixture checks pass. Native app starts; Quit closes its loopback listener. Physical Wi-Fi gameplay remains unverified. Details: `mac-helper-20261015/VERIFICATION.md`.

## Remaining requested work

1. Optional in-site Shiny Relay firmware setup with verified fresh backups, exact-region confirmation, verified writes and recoverable operation records. Official firmware fetched directly; never rehost images. No persistent physical flash has been approved for a particular image/device yet.
2. Shared progress bars, real byte counts/stages, modest animation/fades with reduced-motion support. Restore section/library context after refresh. Never automatically resume uncertain hardware operations.
3. Browser database/OPFS, hash-pinned local download cache, standard ROM header fields and editable homebrew metadata associated with the content hash. Keep edits separate from ROM bytes unless a separately verified metadata format is chosen.
4. Optional online helper: website controls a paired USB-attached host. Off by default; authenticated control, explicit flash consent, operation journal/reconnect. Cable-free device management still needs a firmware extension. Existing helper is loopback-only and does not expose USB.
5. Live physical video, OBS-friendly capture and remote play. Use normal camera permissions and the selected Chromatic only. Current firmware source has console `poke <hex mask> [tap_ms]` and forwards input to FPGA; physical control is not yet tested. Keep emulation distinct from real video and hardware input.

## Constraints and unresolved hardware acceptance

- FPGA flash access stops the FPGA and can remove the USB console. Do not parallelize ESP32 operations with destructive FPGA state changes merely for speed.
- No faster-than-upstream claim without actual timings on equivalent hardware operations.
- First persistent firmware/cartridge write requires backup/verification/explicit image and region consent. Rollback must be hardware-tested before claiming it works.
- Shiny Relay supports game-link Wi-Fi already; stock users still need initial installation. A helper cannot conjure a missing wireless management endpoint.
- Existing public CRUCIBLE ROM is 8 MiB; small recipe editions and install support are still outstanding from the original request.

## Continuation pointers

Firmware transport prototypes remain under `/private/tmp/chromatic-firmware` (not canonical or deployed); the SLIP, MD5 and ESP32 code still needs tests, a verified full-backup coordinator, device binding, durable operation journal and explicit image/region consent UI before writes. Online helper pairing, USB management, remote media/input transport, actual game-service integration and small CRUCIBLE recipe editions remain outstanding. The downloadable helper currently provides loopback WS-to-UDP game-link probing only.
