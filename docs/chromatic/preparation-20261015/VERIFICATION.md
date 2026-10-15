# Chromatic cartridge preparation — 2026-10-15

## Fixed

Both live sites use the identical canonical package. Cartridge identity now performs in-page preparation instead of linking to the official cart page or issuing `cart usb info` against an unprepared FPGA. Connections require real firmware/IDCODE responses. The MCU can still be starting after serial open; only idempotent status queries are reissued inside the original deadline. No cartridge operation is retried or canceled automatically.

The official temporary engine is fetched directly from its CORS-enabled host with credentials omitted and no referrer. SHA-256 is pinned to 6e30f490e9d4702f5a96f63e368afb56802ba7c9e38544428302b8fbd10204ee. Parsed payload: 915342 bytes; configuration checksum: 0x3e83; target: GW5A-25 / 0x0001281b. It is loaded only into volatile FPGA SRAM. No image is redistributed by the sites.

Preparation checks idle status, checks the image, loads SRAM, requires the chosen console to physically disappear, waits for a single returning console, reopens it, and requires cartridge identity with engine=1. Visible stages and deadlines cover download, USB transfers, restart and identity.

## Physical live verification

Using actual browser-granted devices, both https://crucible.onkeel.io/chromatic/ and https://egress.onkeel.io/chromatic/ completed preparation and returned:

- Title: NOCTURNES
- Firmware-reported flash range: 2097152 bytes
- Firmware-reported writable range: 2097152 bytes
- Firmware-reported save range: 32768 bytes
- Header checksum: valid
- Cartridge engine: responding

The successful UI requires actual SRAM DONE status, observed USB disconnect/reconnect, and a CBU1 identity response. No browser API mocks were injected into either live page. No persistent firmware or cartridge bytes were written. Both sites' console and bridge handles were explicitly released after verification. Screenshots preserve the real cartridge response without network credentials or device identifiers.

Geometry is the firmware-reported accessible range, not proof of arbitrary cartridge/mapper support. Full backup, install, firmware rollback, larger engines and smaller CRUCIBLE editions were not physically tested in this run. This is not a claim that those outstanding original-task features are complete.

## Automated and deployment verification

- Shared package: 31/31 tests passed, including failed hash/ID/DONE, missing disconnect/reconnect, unfinished jobs, delayed console startup and protocol integrity.
- Both static Next builds passed.
- Both site browser suites: 15/15 desktop checks passed. Includes in-site preparation using simulated hardware and the real pinned engine bytes, library roundtrip, denied/missing permission, timeout, corrupted read and native responsive controls.
- Final public page and every referenced JS/CSS chunk match the tested artifacts; see public-readback.json.
- Only Chromatic route HTML/RSC, notice and required static chunks were published. Home pages and ROMs were unchanged. Previous static chunks remain available.
- Production backup: /opt/velatis-gateway/backups/chromatic-preparation-startup-20261015.
