"""Trick test check: run tt_*.gbc in SameBoy's headless tester (or read a finished run), then
  1. read the five results the ROM left in cartridge RAM (TT header, pass flags, numbers),
  2. compare the screenshot with the model of what the hardware must show (tt_ref_A/B.png, written here): every
     pixel of the view must fall in the colour class the model gives it (one screen colour per class, the four
     distinct; this is immune to the emulator's colour correction), the HUD strip must hold the HUD classes and not
     the view's palette-0 magenta. The flip indicator (top-left 8x8) may show either buffer; the result numbers
     are masked (they are measurements).

  python tt/tt_check.py ROM [--tester PATH] [--length 15]     exit 0 = all pass
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

NAMES = ["ROW2X  SCY in mode 2", "HUDPAL palette outside mode 3", "GDMA   inside VBlank", "VBLEND copy+flip before line 0",
         "WRAMBK banks 2/3 + 1 flip / 2 frames"]
VIEW = [(16, 24, 40), (40, 160, 70), (200, 40, 40), (240, 240, 220)]
HUD = [(10, 10, 14), (90, 90, 100), (160, 160, 170), (230, 230, 230)]


def model(which: int):
    """framebuffer colour per (x, row) as tt_main.c's image() + boxes() draw it (boxes all pass)"""
    fb = [[0] * 160 for _ in range(64)]
    for r in range(48):
        for x in range(160):
            dx, dy = x - 80, (r - 24) * 2
            q = dx * dx + dy * dy
            if 1450 < q <= 1600:
                fb[r][x] = 3
    for r in range(48):
        fb[r][r * 2] = 3
        fb[r][r * 2 + 1] = 3
    for r in range(24):
        for x in range(128, 160):
            if (((x - 128) >> 2) + (r >> 1)) & 1:
                fb[r + 24][x] = 1
    for r in range(4):
        for x in range(8):
            fb[r][x] = 2 if which else 1
    for k in range(5):
        x0 = 6 + k * 32
        for r in range(50, 56):
            for x in range(x0, x0 + 20):
                fb[r][x] = 1
    return fb


def screen_classes(which: int):
    """per screen pixel: ('v', colour) view, ('h', colour) HUD, or None (masked)"""
    fb = model(which)
    out = []
    for y in range(144):
        row = []
        for x in range(160):
            if y < 128:
                r = y >> 1
                if r >= 57 and 6 <= x < 160:  # result numbers
                    row.append(None)
                elif r < 4 and x < 8:  # flip indicator: either buffer
                    row.append(None)
                else:
                    row.append(("v", fb[r][x]))
            else:
                row.append(("h", 1 if y < 136 else 0))
        out.append(row)
    return out


def write_refs(d: Path):
    for which, name in ((0, "A"), (1, "B")):
        im = Image.new("RGB", (160, 144))
        fb = model(which)
        for y in range(144):
            for x in range(160):
                im.putpixel((x, y), VIEW[fb[y >> 1][x]] if y < 128 else HUD[1 if y < 136 else 0])
        im.save(d / f"tt_ref_{name}.png")


def check_image(im: Image.Image):
    im = im.convert("RGB")
    cls = screen_classes(0)
    seen, bad = {}, 0
    for y in range(144):
        for x in range(160):
            c = cls[y][x]
            if c is None:
                continue
            p = im.getpixel((x, y))
            if c not in seen:
                seen[c] = p
            elif seen[c] != p:
                bad += 1
    errs = []
    if bad:
        errs.append(f"{bad} pixels differ from their colour class (row doubling / image broken)")
    cols = list(seen.values())
    if len(set(cols)) != len(cols):
        errs.append(f"colour classes collide: {seen}")
    hud = [seen.get(("h", 0)), seen.get(("h", 1))]
    if any(p and p[0] > 200 and p[1] < 80 and p[2] > 200 for p in hud):
        errs.append("HUD strip shows palette 0 magenta: the line-127 palette write missed")
    return errs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("--tester", default="/Users/ravonus/dev/tools-ext/SameBoy/build/bin/tester/sameboy_tester")
    ap.add_argument("--length", type=int, default=15)
    a = ap.parse_args()
    write_refs(Path(__file__).parent)
    with tempfile.TemporaryDirectory() as td:
        r = Path(td) / a.rom.name
        shutil.copy(a.rom, r)
        subprocess.run([a.tester, "--cgb", "--sav", "--length", str(a.length), str(r)], capture_output=True)
        sav = r.with_suffix(".sav").read_bytes()
        im = Image.open(r.with_suffix(".bmp"))
        im.load()
        shot = a.rom.with_name(a.rom.stem + "_sameboy.png")
        im.convert("RGB").save(shot)
    ok = True
    if sav[:2] != b"TT":
        print("no results in cartridge RAM (did the ROM run long enough?)")
        return 1
    units = ["SCY writes outside mode 2", "palette writes in mode 3", "spare VBlank ticks after the GDMA (16 cycles)",
             "spare VBlank ticks at the end (16 cycles)", "flips in 2 s"]
    for k in range(5):
        p = sav[2 + k] == 1
        ok &= p
        print(f"{k + 1} {NAMES[k]:38s} {'PASS' if p else 'FAIL'}  {sav[8 + k]:3d} {units[k]}")
    print(f"  VBlank end line max {sav[14]}, GDMA end tick {sav[15]}, VBlank work end tick {sav[16]} (of 142)")
    errs = check_image(Image.open(shot))
    for e in errs:
        print("IMAGE FAIL:", e)
    print("image:", "PASS" if not errs else "FAIL", f"({shot.name}, models tt_ref_A.png / tt_ref_B.png)")
    return 0 if ok and not errs else 1


if __name__ == "__main__":
    sys.exit(main())
