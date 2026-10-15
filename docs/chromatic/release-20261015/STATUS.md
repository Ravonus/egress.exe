# Chromatic preview publication — 2026-10-15

The owner approved the mocks and requested publication. This authorizes these preview routes and additive entry links; it does not authorize physical device writes or third-party firmware image distribution.

Published and read back:

- https://egress.onkeel.io/chromatic/
- https://crucible.onkeel.io/chromatic/

Both pages explicitly say **SIMULATED DEVICE / NO HARDWARE ACCESS**. They contain no device API calls. Their CSP blocks outbound connections; fonts, character portraits and JavaScript load from the same site. Firmware, backups, cartridge state and operation results in the preview are examples. No smaller CRUCIBLE binary is offered or implied to exist.

The actual shared selector is implemented in `/Users/ravonus/dev/keel-engine/packages/chromatic/src/cartridge.mjs`, with identical vendored packages in each site. Static previews import its exact bytes as `chromatic/cartridge.js`. The UI supplies synthetic fixtures; the hardware adapter is not implemented. The selector chooses the highest-content eligible release and checks ROM addressability, RAM, mapper, peripherals, protected bytes, and writer limits. When no compatible writer can program a candidate it reports that separately. It never grants write permission.

The ROM inspector correctly reads the current EGRESS and CRUCIBLE files and validates both header/global checksums. It preserves actual versus declared lengths and never treats a ROM header as physical capacity evidence.

Public readback SHA-256:

| Asset | SHA-256 |
|---|---|
| EGRESS preview HTML | e997c3094e3e0c118fc4583799422ebd58954bcc13321fa74e188bb1c4e7b679 |
| CRUCIBLE preview HTML | e982f42dd24cf7cba3061f315c7c3d8730b60730319f9a9a7b887184cbc16538 |
| Shared selector, both origins | 3bedcefeb72421285b412d43aa5292087fc0dfbbf1687da959aa83fcf9a3c228 |

Initial verification: 7 shared selector/metadata tests passed; EGRESS's 33 unit tests passed; both isolated static builds passed. CRUCIBLE's 49 unit tests had 44 passes and 5 cartridge-link failures. The same 5 failures reproduced from its unchanged committed baseline in `test/link-bridge.test.mjs` (4 passes / 5 failures). They are not caused by the preview. Final browser results and homepage publication are recorded below.

Live preview browser checks: both origins load and run without browser errors. CRUCIBLE's 4/8 MiB examples select their corresponding planned editions, unknown cartridges block install preparation, and the 320px layout fits without horizontal overflow.

Still required for real use: hardware capability discovery; full transport simulator/fault injection; verified backups and write/readback engine; actual budgeted CRUCIBLE builds and save migration; owner-assisted hardware recovery/rollback and measured timing comparisons. The current release is a public UI preview plus tested selection logic, not a hardware installer.

## Final publication and verification

Both homepages now include **On a Chromatic** in Where to play and beneath the existing action controls after leaving Play. The latter is checked with `toBeInViewport`, not just DOM visibility. Both static exports rebuilt successfully after the final placement change.

- Focused final browser suite: **18/18 passed** (9 each site), across desktop, tablet and phone. Checks cover capability selection, preview consent, no hardware or outbound calls, overflow, new-tab behavior and the visible after-Play entry.
- EGRESS broader regression: desktop 24 passed / 2 skipped before offloading; remote tablet/phone 52 passed / 3 skipped / 3 failed. Two failures were in the earlier version of the new test and passed in the final focused rerun. One tablet tour-scroll timeout remains unresolved; the complete regression suite is not claimed green.
- CRUCIBLE legacy browser suite references controls absent from the committed baseline. Focused tests use current controls. Five unrelated link-bridge unit failures reproduce in the unchanged baseline.
- Public homepage bytes match the tested static exports exactly; see `homepage-readback.json`. Live CRUCIBLE Play → Back to tour shows the new link; screenshot saved as `crucible-after-play-live.jpg`.
- Published only new immutable static chunks and homepage HTML/RSC files, preserving old assets, ROMs and server configuration. Previous homepage files are retained on the deployment host for rollback.

Local source commits: EGRESS site `973d196`, CRUCIBLE site `d058f32`, shared engine package `d5e4c36`. The two site repositories have no Git remote; publication was a static deployment, not a Git push. No unrelated changes were committed.

This completes the approved preview publication and adds the shared capability-based selector. Physical detection, automatic delivery of real smaller editions and flashing remain unfinished; no smaller CRUCIBLE ROM was built or deployed.
