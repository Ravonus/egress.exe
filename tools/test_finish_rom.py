"""Tests for the ROM finish and its 1,900,000-byte gate: `python tools/test_finish_rom.py [build/egress.gbc]`.
(`make test` runs it on both ROMs: build/egress.gbc, MBC5, and build/egress-rtc.gbc, MBC3 + RTC.)

  - a small link is trimmed to its last bank, keeps the 2 MiB size code, and both checksums verify on the trimmed file
  - a link that reaches bank 115 (ending past the gate) fails
  - a stray byte past the cut fails; a link that declares the other mapper fails; the MBC3 finish writes 0x10
  - the real ROM's cartridge type matches its name (egress-rtc: 0x10, otherwise 0x1B)
  - the real trimmed ROM's header verifies and it boots in PyBoy to the title screen
"""

from __future__ import annotations

import io
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import finish_rom  # noqa: E402

BANK = 0x4000
failures = 0


def check(cond: bool, what: str) -> None:
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def fake_link(banks_used: int) -> bytearray:
    rom = bytearray(b"\xff" * (128 * BANK))
    rom[0x100:0x150] = bytes(0x50)
    rom[0x148] = 0x06
    rom[0x147] = 0x1B
    for b in range(banks_used):
        rom[b * BANK : b * BANK + 16] = bytes(range(1, 17))
    return rom


def fake_map(top_bank: int) -> str:
    lines = ["Area                       Addr        Size        Decimal Bytes (Attributes)",
             "_CODE                  00000200    00001000 =        4096. bytes (REL,CON)"]
    for b in range(1, top_bank + 1):
        lines.append(f"_CODE_{b}                {b:04X}4000    00000010 =          16. bytes (REL,CON)")
    lines.append("_DATA                  0000C0A0    00000100 =         256. bytes (REL,CON)")
    return "\n".join(lines) + "\n"


def verify_header(rom: bytes) -> bool:
    ok = rom[0x14D] == finish_rom.header_checksum(bytearray(rom))
    g = (sum(rom) - rom[0x14E] - rom[0x14F]) & 0xFFFF
    return ok and (rom[0x14E] << 8 | rom[0x14F]) == g


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        t = Path(d)
        (t / "a.gbc").write_bytes(fake_link(4))
        (t / "a.map").write_text(fake_map(3))
        rc = finish_rom.finish(t / "a.gbc", t / "a.map", t / "a.out")
        out = (t / "a.out").read_bytes() if rc == 0 else b""
        check(rc == 0 and len(out) == 4 * BANK, "a 4-bank link is trimmed to 65,536 bytes")
        check(rc == 0 and out[0x148] == 0x06, "the header keeps the 2 MiB size code")
        check(rc == 0 and verify_header(out), "header and global checksums verify on the trimmed file")
        check(rc == 0 and out[0x134:0x13E] == b"EGRESS.EXE", "the title is written")

        (t / "b.gbc").write_bytes(fake_link(116))
        (t / "b.map").write_text(fake_map(115))
        check(finish_rom.finish(t / "b.gbc", t / "b.map", t / "b.out") == 1, "a link into bank 115 fails the gate")

        (t / "c.gbc").write_bytes(fake_link(115))
        (t / "c.map").write_text(fake_map(114))
        check(finish_rom.finish(t / "c.gbc", t / "c.map", t / "c.out") == 0, "115 banks (1,884,160 bytes) pass")

        rom = fake_link(4)
        rom[60 * BANK + 5] = 0x42
        (t / "d.gbc").write_bytes(rom)
        (t / "d.map").write_text(fake_map(3))
        check(finish_rom.finish(t / "d.gbc", t / "d.map", t / "d.out") == 1, "a stray byte past the cut fails")

        check(finish_rom.finish(t / "a.gbc", t / "a.map", t / "e.out", "mbc3") == 1,
              "an MBC5 link finished as MBC3 fails")
        rom = fake_link(4)
        rom[0x147] = 0x10
        (t / "f.gbc").write_bytes(rom)
        rc = finish_rom.finish(t / "f.gbc", t / "a.map", t / "f.out", "mbc3")
        out = (t / "f.out").read_bytes() if rc == 0 else b""
        check(rc == 0 and out[0x147] == 0x10 and verify_header(out), "an MBC3 link finishes as 0x10, checksums verify")

    real = Path(sys.argv[1] if len(sys.argv) > 1 else "build/egress.gbc")
    if real.exists():
        data = real.read_bytes()
        check(len(data) <= finish_rom.GATE and len(data) % BANK == 0, f"{real} is {len(data)} bytes, whole banks, "
              f"under the gate")
        check(verify_header(data), f"{real} header and global checksums verify")
        want = 0x10 if "rtc" in real.name else 0x1B
        check(data[0x147] == want and data[0x148] == 0x06 and data[0x149] == 0x02,
              f"{real} cartridge type 0x{data[0x147]:02x} (want 0x{want:02x}), 2 MiB declared, 8 KiB SRAM")
        from pyboy import PyBoy

        pb = PyBoy(str(real), window="null", cgb=True, sound_emulated=False, log_level="ERROR",
                   ram_file=io.BytesIO(bytes(8192)))
        pb.set_emulation_speed(0)
        noi = real.with_suffix(".noi").read_text(encoding="latin-1")
        mode = int(re.search(r"DEF _game_mode 0x([0-9A-Fa-f]+)", noi)[1], 16)
        reached = False
        for _ in range(900):
            pb.tick(1, False)
            if pb.memory[mode] == 1:
                reached = True
                break
        pb.stop(save=False)
        check(reached, f"{real} boots in PyBoy to the title")
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
