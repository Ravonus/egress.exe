# Chromatic Helper Mac release — 2026-10-15

## Delivered

- Public source: https://github.com/Ravonus/chromatic-helper
- Release: https://github.com/Ravonus/chromatic-helper/releases/tag/v0.2.0
- Exact source commit: `9b5bbc157f7be712d31345cf9be0e8da7f35b120`.
- Source CI passed: https://github.com/Ravonus/chromatic-helper/actions/runs/38821629482 (12 Node protocol/client/service tests, 4 Bun real socket tests, 2 Python packaging preflight tests).
- The prior standalone executable had an invalid embedded signature. It is replaced by a native AppKit launcher with an embedded Bun helper, requiring macOS 13+ on Apple Silicon. No Terminal setup is required.
- Both shared site panels retain their theme and layout, link the versioned app ZIP and public GitHub source, and explain opening the appropriate site from the helper.

## Distribution verification

The app was Developer ID signed with hardened runtime (JIT entitlement only on the nested runtime), accepted by Apple notarization, stapled, and assessed as `Notarized Developer ID` by Gatekeeper. After downloading the public GitHub release, the ZIP SHA-256 matched `release.json`; strict/deep signatures, stapled ticket validation and Gatekeeper acceptance passed again after extraction. No quarantine removal or Gatekeeper bypass was used. No signing/notarization credentials are included in source or this evidence.

The downloaded native app opened and reported its running local helper. Quit removed the app and closed 127.0.0.1:48180. A screenshot of the actual app is included. This was a downloaded-artifact launch plus explicit Gatekeeper assessment; a separate browser-quarantined first-launch prompt was not exercised.

## Browser and deployment verification

Both isolated production builds passed. Each site passed the HTTPS page → actual packaged helper → UDP fixture test, including a valid response, honest five-second timeout, cancellation, source/download links and source archive availability. Normal Chromium local-network access was granted for that fixture test. These checks do not establish physical Wi-Fi gameplay or cartridge/firmware write support.

The deployment guarded the prior live page hashes, backed up the Chromatic routes, and published only those routes, helper metadata and required static assets. Live screenshots confirm both helper panels. All 75 staged public file hashes match the deployed content. Both known-broken legacy executable URLs return 404; rollback copies are outside the public web root at `/opt/velatis-gateway/backups/chromatic-mac-release-20261015`. Home pages and ROM files were not replaced by this deployment.

## Scope

The helper is a loopback WebSocket-to-UDP game-link preview. It does not flash devices, manage cartridges, provide internet remote play, or complete multiplayer integration. No physical device reset, flash, gameplay or firmware change was performed for this release.

## Recorded commits

- Canonical engine/helper and shared panel: `667f244`.
- EGRESS site helper update: `fcbfbd0`.
- CRUCIBLE site helper update: `bb58e63`.
- Public standalone helper source/tag: `9b5bbc157f7be712d31345cf9be0e8da7f35b120`, `v0.2.0`.
