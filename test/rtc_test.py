"""PyBoy tests for the clocks: `python test/rtc_test.py build/egress.gbc build/egress-rtc.gbc [--shots DIR]`.

PyBoy emulates the MBC3 RTC from the host's wall clock: the chip's counter is `time.time() - timezero`, latched on a
0 -> 1 write to 0x6000, and kept beside the save as an .rtc blob (an 8-byte double `timezero`, then the halt and carry
bytes). A power-off is a stop; moving `timezero` back by N seconds before the next boot makes N seconds pass while the
cartridge was off. The boots run at real-time speed until the title, because the cartridge's probe needs the chip to
tick across the KEEL boot (about 2.5 s).

  rtc_host       MBC3: a HOST CLOCK block (2026-10-15 12:00:00 at UTC-5) sets the clock and anchors the RTC
  rtc_off        MBC3: 3 days 2 hours off: the next boot reads 2026-10-18 14:00, RTC OK, no drift prompt
  rtc_carry      MBC3: 520 days off: the 9-bit day counter wraps and sets carry; the boot folds 512 days into the
                 RTC block (folds = 1) and clears the chip's carry; the date is right
  rtc_no_chip    MBC3 header without the timer (0x13): the RTC selects read SRAM; it never ticks: the set-once
                 fallback, stale (the drift prompt waits for the 2nd power-on), and no write reaches the missing chip
  rtc_open_bus   MBC3 with the dbg_rtc hook (every RTC read 0xFF) poked during the boot: the fallback
  rtc_reset      MBC3 whose chip lost power (counter back at 0): it reads earlier than the kept clock: the fallback
  set_once       MBC5: the same HOST CLOCK; after a power-off the clock resumes where it stopped, stale, and on the 2nd power-on the drift
                 prompt; SETUP > CLOCK re-sets it (an hour on) and the prompt goes
  first_ask      MBC5, blank save: PRESS START asks WHAT IS TODAY? once; A keeps it and the run starts
  move_5_to_3    the MBC5 save boots on the RTC build: the kept clock carries over (stale) until it is set; a set
                 anchors the RTC and the next boot is RTC OK
  move_3_to_5    the RTC save boots on the MBC5 build: the kept clock carries over (stale); the RTC block is left alone
  titles         both ROMs boot to the title (screenshots into --shots)
"""

from __future__ import annotations

import argparse
import io
import json
import re
import struct
import sys
import tempfile
import time
from pathlib import Path

from pyboy import PyBoy

MODE_TITLE, MODE_PLAY, MODE_SETUP = 1, 2, 5
RTC_UNANCHORED, RTC_OK, RTC_DEAD = 1, 2, 3
CLK_STALE, CLK_PLAYER, CLK_HOST = 0, 1, 2
SRAM_KEPT, SRAM_HOST, SRAM_RTC = 0x14C0, 0x14E0, 0x14F0  # offsets into the 8 KiB save
DAY = 86400
EPOCH_2026_10_15 = 9784  # days since 2000-01-01
failures = 0
report: dict[str, object] = {}


def check(cond: bool, what: str) -> None:
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def crc16(b: bytes) -> int:
    c = 0xFFFF
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def host_clock(y: int, mo: int, d: int, h: int, mi: int, s: int, off_q: int) -> bytes:
    """a HOST CLOCK version 2 block (docs/redesign.md 9.7.1): local time and the UTC offset in quarter hours"""
    b = b"HCLK" + bytes([2, 1]) + struct.pack("<H", y) + bytes([mo, d, h, mi, s, off_q & 0xFF])
    return b + struct.pack("<H", crc16(b))


def blank_with_host(block: bytes) -> bytes:
    sram = bytearray(8192)
    sram[SRAM_HOST:SRAM_HOST + 16] = block
    return bytes(sram)


def rtc_blob(timezero: float, halt: int = 0, carry: int = 0) -> bytes:
    return struct.pack("d", timezero) + bytes([halt, carry])


def rtc_parse(blob: bytes) -> tuple[float, int, int]:
    return struct.unpack("d", blob[:8])[0], blob[8], blob[9]


class Cart:
    def __init__(self, rom: Path, sram: bytes, rtc: bytes | None = None, noi: Path | None = None) -> None:
        self.rtc_in = io.BytesIO(rtc) if rtc is not None else None
        self.pb = PyBoy(str(rom), window="null", cgb=True, sound_emulated=False, log_level="ERROR",
                        ram_file=io.BytesIO(sram), rtc_file=self.rtc_in)
        self.pb.set_emulation_speed(1)  # real time: the RTC is the wall clock
        text = (noi or rom.with_suffix(".noi")).read_text(encoding="latin-1")
        self.sym = {m[1]: int(m[2], 16) for m in re.finditer(r"DEF (\S+) 0x([0-9A-Fa-f]+)", text)}
        self.held: set[str] = set()

    def peek(self, name: str, off: int = 0) -> int:
        return self.pb.memory[self.sym["_" + name] + off]

    def poke(self, name: str, v: int) -> None:
        self.pb.memory[self.sym["_" + name]] = v & 0xFF

    def tick(self, n: int = 1) -> None:
        for _ in range(n):
            self.pb.tick(1, True)

    def press(self, button: str, frames: int = 3) -> None:
        self.pb.button_press(button)
        self.tick(frames)
        self.pb.button_release(button)
        self.tick(2)

    def wait_mode(self, mode: int, limit: int = 1500) -> bool:
        for _ in range(limit):
            if self.peek("game_mode") == mode:
                return True
            self.tick()
        return False

    def boot(self, hook=None) -> bool:
        """to the title; `hook(frame)` runs each frame of the boot (for pokes)"""
        for f in range(1500):
            if hook:
                hook(f)
            if self.peek("game_mode") == MODE_TITLE:
                self.pb.set_emulation_speed(0)
                self.tick(70)  # the fade in and the first clock-row phase
                return True
            self.tick()
        return False

    def clock(self) -> dict:
        a = self.sym["_clk_now"]
        m = self.pb.memory
        days = m[a] | m[a + 1] << 8
        minute = m[a + 2] | m[a + 3] << 8
        sec = m[a + 4]
        off = self.peek("clk_off")
        off = off - 256 if off > 127 else off
        return {"days": days, "minute": minute, "sec": sec, "secs": days * DAY + minute * 60 + sec, "off_q": off,
                "quality": self.peek("clk_quality"), "known": self.peek("clk_known"), "drift": self.peek("clk_drift"),
                "rtc": self.peek("clk_rtc")}

    def shot(self, path: Path) -> None:
        self.pb.screen.image.convert("RGB").save(path)

    def stop(self) -> tuple[bytes, bytes | None]:
        ram, rtc = io.BytesIO(), io.BytesIO()
        self.pb.stop(save=True, ram_file=ram, rtc_file=rtc)
        return ram.getvalue()[:8192], (rtc.getvalue() or None)


def utc_secs(days: int, h: int, mi: int, s: int = 0) -> int:
    return days * DAY + h * 3600 + mi * 60 + s


def near(c: dict, want: int, slack: int = 300) -> bool:
    """the cartridge's time is `want` plus the wall/emulated time spent booting and playing (seconds, < slack)"""
    return want <= c["secs"] <= want + slack


def kept(sram: bytes) -> dict | None:
    best = None
    for off in (SRAM_KEPT, SRAM_KEPT + 16):
        r = sram[off:off + 16]
        if r[:3] != b"CK\x01" or crc16(r[:14]) != (r[14] | r[15] << 8):
            continue
        rec = {"flags": r[3] & 15, "boots": r[3] >> 4, "seq": r[4], "days": r[6] | r[7] << 8,
               "minute": r[8] | r[9] << 8, "sec": r[10]}
        if best is None or ((rec["seq"] - best["seq"]) & 0xFF) < 128:
            best = rec
    return best


def rtc_block(sram: bytes) -> dict | None:
    r = sram[SRAM_RTC:SRAM_RTC + 16]
    if r[:3] != b"RT\x01" or crc16(r[:14]) != (r[14] | r[15] << 8):
        return None
    return {"flags": r[3], "base": struct.unpack("<I", r[4:8])[0], "folds": r[8]}


def patch_type(rom: Path, cart_type: int, out: Path) -> Path:
    data = bytearray(rom.read_bytes())
    data[0x147] = cart_type
    s = 0
    for i in range(0x134, 0x14D):
        s = (s - data[i] - 1) & 0xFF
    data[0x14D] = s
    out.write_bytes(data)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom5", type=Path)
    ap.add_argument("rom3", type=Path)
    ap.add_argument("--shots", type=Path, default=Path("build/rtc_shots"))
    a = ap.parse_args()
    a.shots.mkdir(parents=True, exist_ok=True)
    noon = utc_secs(EPOCH_2026_10_15, 17, 0)  # 12:00 at UTC-5
    host = host_clock(2026, 10, 15, 12, 0, 0, -20)

    # --- the RTC build ---
    c = Cart(a.rom3, blank_with_host(host))
    check(c.boot(), "rtc_host: the RTC build boots to the title")
    k = c.clock()
    report["rtc_host"] = k
    check(k["quality"] == CLK_HOST and k["rtc"] == RTC_OK and near(k, noon) and k["off_q"] == -20,
          f"rtc_host: HOST CLOCK set 2026-10-15 17:00 UTC (UTC-5), RTC anchored and ticking: {k}")
    c.shot(a.shots / "rtc_title.png")
    sram, rtc = c.stop()
    blk = rtc_block(sram)
    check(blk is not None and blk["folds"] == 0 and sram[SRAM_HOST] == 0,
          f"rtc_host: the RTC block is written, the host block consumed: {blk}")
    sram_rtc_anchored = sram

    tz, halt, carry = rtc_parse(rtc)
    off = 3 * DAY + 2 * 3600
    c = Cart(a.rom3, sram, rtc_blob(tz - off, halt, carry))
    check(c.boot(), "rtc_off: boots")
    k = c.clock()
    report["rtc_off"] = k
    check(k["rtc"] == RTC_OK and k["drift"] == 0 and near(k, noon + off),
          f"rtc_off: 3 days 2 hours off reads 2026-10-18 19:00 UTC (+ boot time): {k}")
    c.shot(a.shots / "rtc_after_power_off.png")
    sram, rtc = c.stop()
    sram_rtc_later = sram

    tz, halt, carry = rtc_parse(rtc)
    off2 = 520 * DAY
    c = Cart(a.rom3, sram, rtc_blob(tz - off2, halt, carry))
    check(c.boot(), "rtc_carry: boots")
    k = c.clock()
    report["rtc_carry"] = k
    sram, rtc = c.stop()
    blk = rtc_block(sram)
    _, _, carry_after = rtc_parse(rtc)
    report["rtc_carry_block"] = blk
    check(k["rtc"] == RTC_OK and near(k, noon + off + off2, 600),
          f"rtc_carry: 520 more days reads 2028-03-21 (day {(noon + off + off2) // DAY}): {k}")
    check(blk is not None and blk["folds"] == 1 and not (blk["flags"] & 2) and carry_after == 0,
          f"rtc_carry: 512 days folded into the block, FOLDING clear, the chip's carry cleared: {blk}, "
          f"carry {carry_after}")

    with tempfile.TemporaryDirectory() as d:
        nochip = patch_type(a.rom3, 0x13, Path(d) / "egress-nochip.gbc")
        c = Cart(nochip, sram_rtc_later, None, noi=a.rom3.with_suffix(".noi"))
        check(c.boot(), "rtc_no_chip: MBC3 without a timer boots")
        k = c.clock()
        report["rtc_no_chip"] = k
        kk = kept(sram_rtc_later)
        want = utc_secs(kk["days"], 0, 0) + kk["minute"] * 60 + kk["sec"]
        check(k["rtc"] == RTC_DEAD and k["quality"] == CLK_STALE and k["drift"] == 0 and near(k, want, 30),
              f"rtc_no_chip: the set-once fallback resumes the kept clock, stale, no prompt on the 1st power-on since the last good time: {k}")
        c.shot(a.shots / "rtc_no_chip_title.png")
        c.stop()

    c = Cart(a.rom3, sram_rtc_later, rtc_blob(time.time() - 3600))

    def poke_bus(f: int) -> None:
        if f == 20:
            c.poke("dbg_rtc", 1)

    check(c.boot(poke_bus), "rtc_open_bus: boots")
    k = c.clock()
    report["rtc_open_bus"] = k
    check(k["rtc"] == RTC_DEAD and k["quality"] == CLK_STALE and k["drift"] == 0,
          f"rtc_open_bus: RTC reads of 0xFF fall back to the set-once clock: {k}")
    c.stop()

    c = Cart(a.rom3, sram_rtc_later, None)  # a fresh chip: the counter starts at 0 now
    check(c.boot(), "rtc_reset: boots")
    k = c.clock()
    report["rtc_reset"] = k
    check(k["rtc"] == RTC_DEAD and k["quality"] == CLK_STALE,
          f"rtc_reset: a chip reading earlier than the kept clock is not trusted: {k}")
    c.stop()

    # --- the set-once build ---
    c = Cart(a.rom5, blank_with_host(host))
    check(c.boot(), "set_once: the MBC5 build boots to the title")
    k = c.clock()
    report["set_once_host"] = k
    check(k["quality"] == CLK_HOST and k["rtc"] == 0 and near(k, noon, 30), f"set_once: HOST CLOCK set: {k}")
    c.shot(a.shots / "mbc5_title.png")
    sram, _ = c.stop()
    stopped = kept(sram)
    sram_mbc5 = sram
    c = Cart(a.rom5, sram)
    check(c.boot(), "set_once: boots again")
    k = c.clock()
    report["set_once_resumed"] = k
    check(k["quality"] == CLK_STALE and k["drift"] == 0 and near(k, noon, 60),
          f"set_once: after a power-off it resumes where it stopped (stale, no prompt on the 1st power-on): {k}, "
          f"kept {stopped}")
    sram, _ = c.stop()
    c = Cart(a.rom5, sram)
    check(c.boot(), "set_once: boots a 3rd time")
    k = c.clock()
    report["set_once_resumed2"] = k
    check(k["quality"] == CLK_STALE and k["drift"] == 1,
          f"set_once: the 2nd power-on after the set shows the gentle prompt: {k}")
    for _ in range(3):  # the clock row cycles: date, MAY HAVE DRIFTED, SELECT TO SET IT
        c.tick(64)
        c.shot(a.shots / f"mbc5_drift_{_}.png")
    c.press("select")
    check(c.wait_mode(MODE_SETUP, 120), "set_once: SELECT opens SETUP")
    c.tick(90)  # each screen fades in before it reads the pad
    c.shot(a.shots / "setup.png")
    c.press("a")
    c.tick(90)
    c.shot(a.shots / "clock_screen.png")
    before = c.clock()
    for b in ("right", "right", "right"):  # year -> month -> day -> hour
        c.press(b)
        c.tick(3)
    c.press("up")
    c.tick(4)
    c.shot(a.shots / "clock_screen_edit.png")
    c.press("a")
    c.tick(90)
    c.press("b")  # SETUP: back to the title
    check(c.wait_mode(MODE_TITLE, 200), "set_once: back to the title")
    k = c.clock()
    report["set_once_reset"] = k
    check(k["quality"] == CLK_PLAYER and k["drift"] == 0 and 3600 - 60 <= k["secs"] - before["secs"] <= 3600 + 60,
          f"set_once: SETUP > CLOCK set it an hour on; no prompt: {k}")
    c.stop()

    c = Cart(a.rom5, bytes(8192))
    check(c.boot(), "first_ask: blank save boots")
    c.shot(a.shots / "first_title.png")
    c.press("start")
    check(c.wait_mode(MODE_SETUP, 200), "first_ask: PRESS START asks WHAT IS TODAY?")
    c.tick(90)
    c.shot(a.shots / "first_ask.png")
    c.press("a")
    check(c.wait_mode(6, 900), "first_ask: A keeps the clock and opens heir selection")
    c.tick(30); c.press("a")
    check(c.wait_mode(MODE_PLAY, 900), "first_ask: selected heir enters the run")
    sram, _ = c.stop()
    kk = kept(sram)
    check(kk is not None and kk["flags"] & 3 == 3, f"first_ask: the kept clock is known and asked: {kk}")

    # --- moving saves ---
    c = Cart(a.rom3, sram_mbc5, None)
    check(c.boot(), "move_5_to_3: the MBC5 save boots on the RTC build")
    k = c.clock()
    report["move_5_to_3"] = k
    check(k["rtc"] == RTC_UNANCHORED and k["quality"] == CLK_STALE and k["drift"] == 0 and near(k, noon, 60),
          f"move_5_to_3: the kept clock carries over, stale, until it is set: {k}")
    sram, rtc = c.stop()
    sram = bytearray(sram)
    sram[SRAM_HOST:SRAM_HOST + 16] = host_clock(2026, 10, 20, 9, 30, 0, -20)
    c = Cart(a.rom3, bytes(sram), rtc)
    c.boot()
    sram, rtc = c.stop()
    c = Cart(a.rom3, sram, rtc)
    c.boot()
    k = c.clock()
    report["move_5_to_3_set"] = k
    check(k["rtc"] == RTC_OK and k["drift"] == 0 and near(k, utc_secs(EPOCH_2026_10_15 + 5, 14, 30), 120),
          f"move_5_to_3: once set, the RTC is anchored and read the next boot: {k}")
    c.stop()

    c = Cart(a.rom5, sram_rtc_later)
    check(c.boot(), "move_3_to_5: the RTC save boots on the MBC5 build")
    k = c.clock()
    report["move_3_to_5"] = k
    kk = kept(sram_rtc_later)
    want = utc_secs(kk["days"], 0, 0) + kk["minute"] * 60 + kk["sec"]
    sram, _ = c.stop()
    check(k["quality"] == CLK_STALE and k["drift"] == 0 and near(k, want, 30),
          f"move_3_to_5: the kept clock carries over, stale: {k}")
    check(sram[SRAM_RTC:SRAM_RTC + 16] == sram_rtc_later[SRAM_RTC:SRAM_RTC + 16] and sram[:0x40] == sram_rtc_later[:0x40],
          "move_3_to_5: the RTC block and the meta records are left alone")
    report["sram_rtc_anchored_block"] = rtc_block(sram_rtc_anchored)

    (a.shots / "rtc_report.json").write_text(json.dumps(report, indent=1))
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
