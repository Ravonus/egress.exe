# Real Chromatic device route — 2026-10-15

Published on both EGRESS and CRUCIBLE. Source commits: shared engine `8e67605`, EGRESS site `d50073f`, CRUCIBLE site `78006e8`.

Replaced simulated state transitions with browser Web Serial/WebUSB grants, existing-grant discovery/reconnection, real console health, GWU2X IDCODE reads, CBU1 geometry and double-read backups, and IndexedDB/OPFS file storage. Import/export/rename/tags/delete operate on real bytes. No scenario fixtures are in production. Both sites have byte-identical shared package files and retain their native shells, fonts, controls and cast.

## Validation

- Shared unit/simulator tests: 22/22 pass, including fragmented binary frames, corrupt CRC, missing coverage, differing double reads, timeouts, unplug, unsupported firmware commands, busy jobs, chooser filters and JTAG packet sequence.
- Both static production builds pass.
- Both site Chromatic suites: 36/36 pass across desktop, tablet and phone. After final connection cleanup and license link changes, both exports also pass 12/12 desktop checks. Following the hardware-discovered timeout-message issue, the final exports pass 13/13 desktop checks each, including the new timeout/cleanup regression.
- EGRESS existing unit suite: 33/33 pass.
- CRUCIBLE existing unit suite: 44/49 pass. The five pre-existing `link-bridge.test.mjs` failures are co-op/race/fight lobby entry, cable-pull recovery and VBlank timing. These tests do not import the Chromatic package; no emulator, ROM or link code changed here.
- Public root HTML, Chromatic HTML, all referenced JS/CSS and license files match the exact exported artifact hashes. See `public-readback.json`.
- Initial static backup before deployment: `/opt/velatis-gateway/backups/chromatic-real-20261015-1420/`. The error-handling follow-up backup is `/opt/velatis-gateway/backups/chromatic-read-errors-20261015/`.

## Physical evidence

The owner selected the physical console in the live EGRESS browser permission flow. The port opened at 115200 baud. The read-only health check returned MCU `v1.0.2`, FPGA image `18.21`, Chromatic firmware `v4.6`, Wi-Fi disconnected and relay `NOT PAIRED`. SSID and other private device details are intentionally omitted from this record. The owner also selected the USB bridge. A physical read returned FPGA IDCODE `0x0001281b` (GW5A-25). Cartridge identity did not complete before the 180-second read deadline. Port cleanup then replaced its failure message with a generic disconnect notice; a follow-up regression fix preserves the actionable error. No cartridge capacity or backup has been physically verified. The final CRUCIBLE deployment was then checked through its own separate browser grants: console versions/status and FPGA IDCODE matched the same physical device. No firmware or cartridge writes occurred. The physical device is left connected to the CRUCIBLE health page; EGRESS released its handles.

## Still outstanding

Firmware dump/identification/rollback, temporary FPGA engine loading, cartridge install, smaller CRUCIBLE recipe editions, physical backup/readback verification and comparative hardware timings. The page states these limits; no successful install or recovery is fabricated. Read-only cartridge jobs require compatible firmware and an already prepared engine. Current published engine support is bounded by its actual 2 MiB address path; general selection does not override hardware constraints.
