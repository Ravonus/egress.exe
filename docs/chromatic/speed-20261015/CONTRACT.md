# Firmware verification contract and optimization evidence

Source: the user-supplied shiny-relay-src-1.0.2 tree, extracted under /private/tmp/egress-chromatic-research/shiny-source. No firmware or physical cartridge was modified by these tests.

The native harness compiles and executes the original cf_esp.c, cf_usb.c, cf_sha256.c, cf_chips.c and nw_esp.c against an in-memory cartridge bus. It starts at the verification phase (it does not simulate the physical erase/program timing). The command parser receives the exact direct USB write command with computed size, CRC32 and SHA-256; assertions confirm it sets strm_have_sha and copies the expected SHA into the job. Simulated timing values are artificial and are not a performance benchmark.

Source chain:
- cf_usb.c:326-340 sets strm_src=1, parses size/CRC/SHA and arms the job.
- cf_esp.c:1914-1926 copies the job options, CRC and SHA into the active journal.
- cf_esp.c:595 restricts fast CRC16 verification to staged source=2. The browser uses direct source=1, never install/fast.
- cf_esp.c:697-725 hashes actual cartridge readback after the withheld boot block. It compares SHA-256 and CRC32 against this job's supplied target. Failure returns CF_D_VERIFY.
- cf_esp.c:750-768 verifies the boot block's actual cartridge readback CRC32 against the block hashed with the body. Failure erases that block and returns CF_D_VERIFY.
- cf_usb.c:418-435 emits completed phase timings. cf_usb.c:475-509 emits matching done only on CF_D_OK, otherwise fail, then ready.

Native results (firmware-verification.log): correct image emits completed verify and check-header, exact done size/CRC, ready, and all cartridge bytes match. Corrupt body, wrong expected SHA with correct CRC, and corrupt boot block each fail. A boot-block failure emits BOTH phase timings but NO done; hence timings alone are never trusted.

Proposed browser gate requires completed verify followed by completed check-header, then exact done size/CRC matching the command and ready in the same exclusive write request. The command includes SHA-256. Missing phase evidence falls back to the existing full browser SHA-256 readback. Failure/disconnect never retries a write. Protected tail is separately compared before and after the write. Save readback remains independent and mandatory, and an actual save mismatch continues to fail.

This eliminates one full ROM transfer only on firmware that confirms the audited completed phases and successful checksum-bound job. It is not a faster baud setting, a skipped firmware check, or a claim of physical speed measurement. Firmware explicitly rejects the baud command in cf_usb.c:242-249; 115200 remains unchanged.
