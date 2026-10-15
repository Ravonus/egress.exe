# Live capture delivery — 2026-10-15

Published to https://egress.onkeel.io/chromatic/ and https://crucible.onkeel.io/chromatic/.

- Canonical capture implementation: keel-engine `7808e5b`, followed by navigation/cleanup `81a982c`.
- EGRESS static site: `5d8e00c`; CRUCIBLE static site: `6679669`.
- Both Next production exports pass. Each site passes two browser tests covering synthetic video frames, exact camera selection, no default-camera fallback for the preview, permission denial, late permission cancellation, stop, navigation, page suspension and refresh without reopening media.
- Shared package suite: 43 tests pass. This includes three tests for the canonical-only input prototype `f1508b6`; that adapter is not included in the public site bundles.
- All 63 final public page/chunk hashes match the tested exports; see public-verification.json. The preceding Wi-Fi/helper release has separate 85-file public verification under ../wireless-20261015/.
- Final public page SHA-256: EGRESS `90fcc138b994c07cfe42c056e1d04748c462dcf8490cae19191acc862f7faf87`; CRUCIBLE `937ecfa6865a561ae25a94fd2fe6f1f9569df4de7f7c6f976050ae7bb23f1849`.
- Browser screenshots show the real deployed page with capture **off**. Reload restored the Live / OBS section on both sites. They are UI evidence, not physical footage.

The preview requests only an exact selected, labelled Chromatic video/audio device. Optional permission discovery is separately explained: it briefly opens and immediately stops the browser's default camera/microphone to obtain labels, without rendering, storing or transmitting that temporary stream. The agent did not invoke that discovery step on the user's physical cameras/microphones. No labelled Chromatic was reported by the current browser list; permission state and normal gameplay still need checking.

Physical acceptance is pending a user power cycle back into normal gameplay after the previous SRAM cartridge-engine session, plus normal camera/audio permissions. No new firmware/cartridge/save writes, physical input commands or hardware resets were performed for this capture release. OBS setup follows ModRetro's official capture guide; OBS and physical audio/video have not been tested in this run.

Remote play, internet helper pairing/management/streaming, in-site firmware install/rollback and smaller CRUCIBLE recipe variants remain outstanding. The shared UI states these boundaries. The current downloadable helper is a local game-link UDP bridge/probe, not a USB or online management service.

Deployment backups: `/opt/velatis-gateway/backups/chromatic-live-release-20261015` and `/opt/velatis-gateway/backups/chromatic-live-navigation-20261015`. Only Chromatic route documents and additive hashed browser assets were published; no homepage or ROM replacements.
