# Chromatic support: research record

Date: 2026-10-15. Status: research and design review; no device connected, no firmware written, no deployment. This record supersedes assumptions in the task where current source provides different evidence. Site and cartridge trees were read only. Other sessions have uncommitted changes in both site trees.

## Decision that governs the work

Detect the actual cartridge and automatically select a compatible version of our game. There is no universal 2 MiB application limit. Each transport/board adapter declares its proven capabilities; each game release declares its requirements. CRUCIBLE gets smaller catalogue builds for smaller carts, with fewer baked recipes and their assets. Recipe counts must come from successful builds, not invented tier labels.

## Evidence and sources

The two installers were inspected in Codex's built-in browser. Their raw JavaScript URL was blocked by that browser (`ERR_BLOCKED_BY_CLIENT`); the public text modules were downloaded separately for source inspection. They were not executed or copied into either site. The installed sites were also visually inspected. `source-index.json` records the downloaded module hashes; research copies remain in `/private/tmp/egress-chromatic-research` and are not production dependencies.

Primary sources:

1. [Firmware installer](https://install.shinyrelay.com/), [cartridge installer](https://install.shinyrelay.com/cart/), [user documentation](https://shinyrelay.com/docs/).
2. [Installer configuration](https://install.shinyrelay.com/config.js), [image manifest](https://install.shinyrelay.com/images/manifest.json), [cart configuration](https://install.shinyrelay.com/cart/config.js).
3. [Firmware source 1.0.2](https://install.shinyrelay.com/download/shiny-relay-src-1.0.2.tar.gz), SHA-256 `b7a7c60f56b47f97f0f98898f0303e5225216900fc7086d8bac2f3777e3eb22f`.
4. [Cart engine source c4aebde587b8](https://install.shinyrelay.com/download/shiny-relay-src-cart-engine-c4aebde587b8.tar.gz), SHA-256 `d2b28c95647c479106ca07691924139624226e8f87b78e823a5e1d4b937841e3`. The owner's supplied archive has this exact hash.
5. [ModRetro FPGA source](https://github.com/ModRetro/oss-chromatic-console-fpga), [FPGA releases](https://github.com/ModRetro/oss-chromatic-console-fpga/releases), [MCU source](https://github.com/ModRetro/oss-chromatic-console-mcu), [MCU releases](https://github.com/ModRetro/oss-chromatic-console-mcu/releases).
6. [Official Chromatic updater](https://support.modretro.com/en_us/chromatic-firmware-updater-ryhoYnzCx), [Developer Mode setup](https://support.modretro.com/en_us/chromatic-devday-edition-quickstart-guid-By1iOlcMg). Do not collect activation codes or bypass activation.
7. openFPGALoader v1.1.1: [GW5A implementation](https://github.com/trabucayre/openFPGALoader/blob/v1.1.1/src/gowin.cpp), [GWU2X implementation](https://github.com/trabucayre/openFPGALoader/blob/v1.1.1/src/gwu2x_jtag.cpp), [licence](https://github.com/trabucayre/openFPGALoader/blob/v1.1.1/LICENSE).
8. Espressif: [app image format](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32/api-reference/system/app_image_format.html), [partition tables](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html), [ROM/stub serial protocol](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/serial-protocol.html), [esptool-js](https://github.com/espressif/esptool-js).
9. [Web Serial](https://developer.chrome.com/docs/capabilities/serial), [OPFS](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system).

## What their flows actually do

Firmware: two chooser clicks → check → backup selection (fresh, existing files, or their skip option) → explicit install → optional Wi-Fi → done. The advertised estimate is about 15 minutes. Restore accepts a JSON manifest plus two full flash files. Rescue can operate through GWU2X without the CDC serial device. Their no-backup rescue installs their supplied FPGA image; we will not reproduce this behavior.

`lib/installer.js` reads 4 MiB ESP32 flash, verifies its MD5 on-chip, then reads the FPGA flash twice and compares hashes. It checks the selected serial port disappears during FPGA access to help detect crossed devices. It verifies saved export files before unlocking a write. Its fallback definition of stock is absence of known Shiny markers; our catalogue must never use absence as proof of stock.

Cartridge: choose → two connections → SRAM engine preparation → ROM/save backup → confirmation → stage/install/verify → cleanup. Their page advertises roughly 10 minutes. Their docs give approximate cartridge backup times of 2 minutes for a small game, 5 for 1 MiB, 8 for a full cart; writes around 2/4/8 minutes for 256 KiB/1 MiB/1.94 MiB. These are publisher estimates, not timings measured by us.

The engine is 7,341,130 bytes as a textual `.fs` download; that is not the number of configuration bytes shifted over JTAG. The firmware manifest similarly distinguishes a 7,447,490-byte `.fs` file from its 928,574-byte flash payload. Cache and compare both representations with explicit scopes.

Their pages send step/error reports. Our design contains no such reporting, and no online lookup keyed by a device ID, hash of private device data, filename, SSID, save, or cartridge bytes.

## Hardware facts and confidence

| Fact | Evidence | Boundary |
|---|---|---|
| CDC serial USB 374e:0101 | `lib/ids.js`; ModRetro composite USB design | Not a unique device ID |
| GWU2X USB 33aa:0120, interface 0, OUT 02 / IN 81 / status IN 83 | browser port and upstream GWU2X code | WebUSB endpoint number excludes direction bit |
| GW5A-25 IDCODE 0x0001281b, IR length 8 | upstream ModRetro README and loader | Must read and verify actual chain |
| FPGA W25Q32, JEDEC ef4016, 4 MiB | reference device allowlist | Do not assume every future revision is identical |
| ESP32-U4WDH rev 3, 4 MiB, manufacturer/device 20:4016 | reference unit's allowlist | Not measured on the owner's unit |
| Console 115200 8N1, DTR/RTS low, two wake newlines | public console implementation | Opening a port can still glitch reset on some platforms; test before claiming non-disruptive health |
| Tetris cart SST39VF1682 BF:C9 and SST39VF1681 support | engine source comments; published site docs | Per-board profile, not a universal cartridge description |
| Current reference cart route: MBC5-like, 2 MiB, 32 KiB save, final 64 KiB protected | public cart docs and source | Needs chip/board/protection verification per device |

The GB header describes the game's requirements, not physical flash or SRAM capacity. A cart labelled Tetris is not enough evidence. Identification mode/CFI is a volatile bus operation and must be isolated from plain health; never determine capacity by destructive write/alias tests. Unknown hardware remains readable only where a documented safe reader exists.

### Critical reset dependency

Upstream `Gowin::prepare_flash_access()` resets GW5A and erases SRAM before entering SPI mode. The reference browser port follows that sequence. FPGA flash reads therefore stop the running image, remove the CDC USB bridge, and restart afterward. These reads do not write nonvolatile flash, but they are not an untouched-session health check. Do not run them concurrently with ESP32 UART transfers. Do not load the cart engine from Health.

JTAG IDCODE and supported register queries can be separate from flash access. A reported FPGA version/usercode is not cryptographic proof of the running image. Flash hashes describe stored configuration, not necessarily a temporary SRAM image.

## Public protocol documentation found

The separately cited `docs/CART_BACKUP_USB_PROTOCOL.md` is absent from both published archives. The direct documented path on both public sites returned 404. The public docs index is a user guide, not a wire specification. This is a missing document, not evidence that the protocol is proprietary.

The firmware archive does publish useful protocol documentation in `mcu/components/nw/cartflash/cf_usb.h`, with the reference client in `host/cart_usb.py` and device implementation `cf_usb.c`. Its header documents `cart usb info`, `status`, `read rom`, `read sav`, streamed `write rom`, `write sav`, chunk transfer, and frame layouts. This is the source-backed subset we can specify independently; undocumented or contradictory behavior must stay disabled pending conformance evidence. Do not copy the unlicensed web files to fill gaps.

Research summary of that published subset:

- Command prefix `cart usb`, ASCII lines, 115200 8N1. Response lines start `CBU1`; other console chatter must not become protocol frames.
- Read frame: magic A5 43 42 55, type byte, offset u32 LE, length u16 LE, data, CRC32 LE over type/offset/length/data. Maximum documented payload 1024 bytes.
- Binary write frame: A5 43 42 44, offset u32 LE, length u16 LE, data, CRC32 LE over offset/length/data. Send only requested `CBU1 want` chunks; validate range, state, duplicate handling and sequence.
- `CBU1 ready` is the job boundary. `status` can report an unfinished job; never replay an uncertain write just because the page timed out. Pin source identity and job identity before resume.
- Sparse `skipblank` ROM reads are described in the reference client's comments. Reconstruct FF bytes and verify the full logical image; sparse transfer is not incomplete backup.
- `info` and reads take a network hold in this firmware and may stop its Wi-Fi session. They belong to explicit cartridge preparation, not passive Health.
- The header lists baud changes, but the actual 1.0.2 handler rejects them with `CBU1 fail baud`. Treat implementation contradiction as unsupported. Never send a guessed baud command.
- The ESP32 ↔ FPGA internal path is also 115200 in this route; raising USB baud alone need not speed up cart access.

Console status is discoverable in public firmware source: `fwversion` reports MCU, FPGA and overall Chromatic version; `cwl status` exposes SSID, connected state, IP, peer and link/power state. `cwl relay status` also exposes pairing fields. Parse an allowlist and discard pairing codes, URLs, device IDs and unknown values immediately. Do not retain raw console logs by default. RSSI of the associated access point and battery voltage are not established by these status responses. Peer-discovery RSSI is not Wi-Fi AP RSSI. A last HTTP result is historical evidence, not a current internet test. Do not issue pairing, scans, joins, pings, mode changes or Wi-Fi writes from Health.

## ESP32 layout and identity

The currently served partition-table binary was downloaded and SHA-256 checked against the manifest: `73c0b5c3e5fcba3a151cc70c453c93dd5f4798899e7f2f8cca76da1f32ffc501`.

| Region | Start | Length | End (exclusive) |
|---|---:|---:|---:|
| Bootloader location | 0x1000 | derive image/sector extent | before table |
| Partition table sector | 0x8000 | 0x1000 | 0x9000 |
| NVS | 0x9000 | 0x6000 | 0xF000 |
| PHY | 0xF000 | 0x1000 | 0x10000 |
| Factory app partition | 0x10000 | 0x300000 | 0x310000 |

This is the published Shiny image's layout, not a read of the owner's device. The archive also includes an alternative draft cartflash table. Parse the connected unit's table, validate lengths, alignment, overlaps and checksum, and identify the running app using factory/OTA evidence. Never assume that the first app partition is running. Unsupported encrypted/secure-download layouts fail closed; never change eFuses.

The task's staging assumption needs correction: `cartstage.js` stages saves at 0x208000 and ROM at 0x210000, but 0x208000–0x310000 is INSIDE the served factory app partition. The firmware's `cf_dev.c` comments explicitly rely on unused app tail and custom erase access. Our default cartridge route must use streaming without persistent ESP32 staging. Do not treat those fixed offsets as universally free space. A staging adapter would need an exact firmware/layout contract, fresh backup, and an explicit firmware-storage write review; defer it.

ESP app descriptor is app base + 0x20, magic 0xABCD5432. Relative to descriptor: version +16 (32 bytes), project +48 (32), time +80 (16), date +96 (16), IDF +112 (32), ELF SHA-256 +144 (32). Thus factory descriptor is 0x10020 for this layout. ELF hash differs from app-bin hash and whole-partition hash. Check all lengths and image segment bounds. Hash each complete partition and all other flash ranges in deep identity/backup; label sampling as candidate matching only.

## Firmware catalogue

`catalogue-research.json` contains source metadata and available published digests, not firmware bytes and not a production-approved catalogue.

- Official MCU release tags found: v4.6, v4.5, v4.2, v0.13.4, v0.13.3, v0.13.2, v0.13.0. The overall version and ESP app version can differ: stock main declares overall 4.6 and app 0.13.8. v4.2's asset is named v0.13.4 but differs from the asset under the v0.13.4 tag. Keep both identities.
- Official FPGA tags: v18.14, v18.13, v18.8, v18.7, v18.6, v18.5, v18.4, v18.0, v17.0. The last two have no binary assets in the fetched release list. GitHub's asset `digest` supplies transport-file SHA-256 for newer assets; it is not a flash payload hash. The source tree alone does not prove which binary a device has.
- Shiny currently publishes ESP 1.0.2 / FPGA 18.21 and the separate cart-engine source/image. Its current manifest supplies offsets, file lengths and SHA-256, plus FPGA payload length/hash. No complete historical Shiny image index was found. Record missing versions explicitly; never invent hashes or classify an unknown as stock.
- For device recognition, compare exact image lengths and normalized payload hashes, with verified partition layout. Full flash hashes are per-backup identities and include changing personal data. Samples reduce candidate selection cost; only complete coverage permits exact-match language.

## Licences and distribution boundary

| Component | Published evidence | Proposed handling |
|---|---|---|
| openFPGALoader v1.1.1 | Apache-2.0 | Independent browser port from pinned upstream; retain licence, copyright, modifications, notices |
| esptool-js 0.5.7 | Apache-2.0; pako MIT/Zlib | Pin vendored build and all notices; audit bounded buffers and logging |
| ModRetro MCU/FPGA and Shiny firmware/engine | GPL-3.0 plus component notices | Source reference; any distributed engine requires corresponding source/build inputs and licence review |
| Gowin encrypted USB IP | Source archive NOTICE says vendor terms | GPL label alone does not clear every bundled IP component; record exact provenance |
| Shiny browser JS | No per-file licence headers in much of it; firmware NOTICE calls installer code Apache-2.0 | Stronger evidence than task assumed, but exact deployed-file coverage still to establish. Study; do not copy into product |

No third-party firmware or engine binary is hosted by this proposal. Owner approval is required before shipping any such image, including a temporary SRAM engine. Restoring the visitor's own verified backup is separate. Official updater links remain the stock/Shiny installation path.

## Exact local site ROMs

At review time, the live page download links matched these local manifest paths. Local file hashes and headers were verified; remote ROM bytes were not downloaded again.

| Game | Download bytes | Declared ROM | Mapper/save | SHA-256 |
|---|---:|---:|---|---|
| EGRESS | 81,920 | 2 MiB | 1B, MBC5 + 8 KiB battery RAM | 72a14c35a8b766cedf61718cb3ceafcd5313b3c139b2ee547d98079dcc20bcb0 |
| CRUCIBLE | 8,388,608 | 8 MiB | 1B, MBC5 + 128 KiB battery RAM | 57b7f1e3c2df960f05df615b9ad00544edc1eacad3dbfe176a6c38425734b540 |

EGRESS's source-file global checksum passes. Padding with FF to its declared 2 MiB changes that checksum calculation: store both interpretations, do not rewrite the original file or silently repair its header. Its normalized padded SHA is `de428cd2f095f158ea2b931ce76f7468b7f871278b8e8e12a8b6b2cdec5411be`. The final 64 KiB of the desired padded image is FF; a protected tail on a real cart still needs to be read and equal before install. SHA of a trimmed download must not be compared directly with SHA of a full cart dump.

## CRUCIBLE build findings

`/Users/ravonus/dev/crucible.exe/tools/build-rom.ts` currently links the baked data, fixes 16 SRAM banks and uses the current ROM header finisher's 128 KiB setting. `cartridge/src/crucible_save.c` explicitly selects `CRU_LAYOUT_128K`; the portable core has both 32K and 128K layouts. This is a starting point, not proof that the whole Story cartridge works at 32K.

The actual generator is in `/Users/ravonus/dev/rictus/alchemy/tools/bake.ts`, with `build-gbc.mjs` and `core/tools/tables.ts`; the public cartridge repository carries baked output. The baker reserves fixed catalogue banks and places art up to bank 511, including directory banks at the top. Deleting recipes or truncating the file cannot produce a smaller working game. Add an explicit profile budget before recipe/table/art packing, derive bank allocations from actual packed content, and rebuild headers and save layout together. Preserve the canonical KEEL art for retained objects.

Source provenance must be reconciled against the exact current site ROM before making profile builds. The public cartridge HEAD read was `222c661515bc1009447d82d912876a3ad626cfde`; this is not automatically the source of the current October 15 site artifact. Do not overwrite concurrent generator output.

## Outstanding evidence

No real hardware timings, backups, rollback, power measurements, cartridge identification, JTAG recovery under bad ESP firmware, or direct JTAG cartridge transport were tested. The engine exposes a CPU/MMIO cartridge bus interface, not a documented host JTAG cart API. Adding direct JTAG cart access is a separate engine/protocol project, not a flag on the current loader. The current loaded Mac reported a 1-minute load of 162.73; no full build or test suite was started here.
