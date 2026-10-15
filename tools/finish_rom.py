"""Finish the linked ROM: `python tools/finish_rom.py LINKED.gbc MAP OUT.gbc`.

The link declares MBC5 with 2 MiB of ROM (header 0x148 = 0x06) because the owner's cartridge holds up to 1.94 MB and
accepts a file that is not a power of two. This script:

  1. finds the highest ROM bank anything was linked into (from the .map),
  2. fails if that bank, or any byte that is not padding, lies past the 1,900,000-byte gate,
  3. trims the image after that bank (whole 16 KiB banks: the MBC maps banks, so the cut is on a bank boundary),
  4. writes the title, keeps MBC5 + RAM + battery and the 2 MiB size code, and
  5. computes the header checksum and then the global checksum over the trimmed file.

Prints the size against the gate; exit status 1 on any violation.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

GATE = 1_900_000
BANK = 0x4000
TITLE = b"EGRESS.EXE"
AREA = re.compile(r"^(_\w+)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+=")


def last_bank(map_text: str) -> int:
    top = 1
    for line in map_text.splitlines():
        m = AREA.match(line)
        if not m:
            continue
        addr, size = int(m[2], 16), int(m[3], 16)
        if size == 0 or (addr & 0xFFFF) >= 0x8000:
            continue  # RAM areas
        bank = addr >> 16
        if (addr & 0xFFFF) < 0x4000:
            bank = 0
        top = max(top, bank)
    return top


def header_checksum(rom: bytearray) -> int:
    s = 0
    for i in range(0x134, 0x14D):
        s = (s - rom[i] - 1) & 0xFF
    return s


def global_checksum(rom: bytearray) -> int:
    return (sum(rom) - rom[0x14E] - rom[0x14F]) & 0xFFFF


def finish(linked: Path, map_path: Path, out: Path) -> int:
    rom = bytearray(linked.read_bytes())
    top = last_bank(map_path.read_text(encoding="latin-1"))
    cut = (top + 1) * BANK
    errors = []
    if cut > GATE:
        errors.append(f"bank {top} ends at {cut} bytes, past the {GATE}-byte gate")
    past = next((i for i in range(min(cut, GATE), len(rom)) if rom[i] != 0xFF), None)
    if past is not None:
        errors.append(f"data at 0x{past:06x} (bank {past // BANK}) lies past the cut")
    if rom[0x148] != 0x06:
        errors.append(f"header ROM size 0x{rom[0x148]:02x}, expected 0x06 (2 MiB)")
    if errors:
        for e in errors:
            print("ROM GATE:", e, file=sys.stderr)
        return 1
    rom = rom[:cut]
    rom[0x134:0x143] = TITLE.ljust(15, b"\0")
    rom[0x143] = 0xC0  # CGB only
    rom[0x147] = 0x1B  # MBC5 + RAM + battery
    rom[0x149] = 0x02  # 8 KiB SRAM
    rom[0x14D] = header_checksum(rom)
    g = global_checksum(rom)
    rom[0x14E], rom[0x14F] = g >> 8, g & 0xFF
    out.write_bytes(rom)
    print(f"{out}: {len(rom)} bytes ({top + 1} banks), gate {GATE}, headroom {GATE - len(rom)}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(finish(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])))
