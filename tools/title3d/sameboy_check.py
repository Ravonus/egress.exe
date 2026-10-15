"""The baked title in SameBoy (cycle-accurate) against PyBoy (the harness, whose VRAM is checked against the bake).

    python tools/title3d/sameboy_check.py SAMEBOY_TESTER [OUT_DIR] [--hold 8]

Per loop: a test ROM (title3d.c with T3D_TEST_SEED = that loop and T3D_TEST_HOLD = a frame of the second pass, past
the wrap; everything else the shipping objects) runs headless in SameBoy's tester and in PyBoy. At the hold the ROM
writes its measurements into cartridge RAM (loop, frame, VBlanks and flips since the title began and since its 200th
flip, flips that missed their VBlank, the latest line one landed on) and stops the walk. The check: SameBoy's steady
frame rate is 30, no flip missed its VBlank, and its
last frame shows the same view as PyBoy's at the hold (pixel for pixel through a one-to-one colour map, since
SameBoy colour-corrects; the logo band, PRESS START and the info row are skipped: they run on the title's own clock).
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
LCC = Path.home() / ".cache/gbdk-4.5.0/bin/lcc"


def build(seed: int, hold: int, out: Path) -> Path:
    objs = {}
    for name in ("title3d", "t3d_core"):
        objs[name] = out / f"{name}_{seed:04X}.o"
        r = subprocess.run([str(LCC), "-c", "-Isrc", "-Ibuild/t3d_bake/gen", f"-DT3D_TEST_SEED=0x{seed:04X}u",
                            f"-DT3D_TEST_HOLD={hold}u", "-o", str(objs[name]), f"src/title3d/{name}.c"], cwd=ROOT,
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stdout + r.stderr)
    dry = subprocess.run(["make", "-n", "-W", "src/title3d/t3d.h", "build/linked-mbc5.gbc"], cwd=ROOT,
                         check=True, capture_output=True, text=True).stdout
    link = next(line for line in dry.splitlines() if " -o build/linked-mbc5.gbc " in line)
    rom = out / f"t3d_{seed:04X}.gbc"
    for name, obj in objs.items():
        link = link.replace(f"build/obj/title3d/{name}.o", str(obj))
    link = link.replace("-o build/linked-mbc5.gbc", f"-o {rom}")
    subprocess.run(link, shell=True, cwd=ROOT, check=True, capture_output=True)
    subprocess.run([sys.executable, "tools/finish_rom.py", str(rom), str(rom.with_suffix(".map")), str(rom),
                    "--mapper", "mbc5"], cwd=ROOT, check=True, capture_output=True)
    return rom


def stats(sram: bytes) -> dict | None:
    m = sram[0x1F00:0x1F10]
    if m[:2] != b"T3":
        return None
    vb, nf = m[5] | m[6] << 8, m[7] | m[8] << 8
    svb, snf = m[12] | m[13] << 8, m[14] | m[15] << 8
    return {"loop": m[2], "frame": m[3] | m[4] << 8, "vblanks": vb, "flips": nf,
            "fps_since_title": round(nf * 60 / vb, 2), "fps": round(snf * 60 / max(svb, 1), 2),
            "flips_missed": m[9], "flip_line_max": 144 + m[10], "variant": m[11]}


def pyboy_run(rom: Path, hold: int, limit: int = 60 * 90) -> tuple[dict | None, Image.Image]:
    """the same ROM in PyBoy; the same measurements, read from WRAM at the hold"""
    sys.path.insert(0, str(ROOT / "test/harness"))
    from play import Game

    g = Game(rom)
    g.tick(150)
    out = None
    last = -1
    steady: tuple = ()
    for _ in range(limit):
        g.tick()
        nf = g.peek16("t3d_nflips")
        if nf == 200 and not steady:
            steady = (g.peek16("t3d_vbl"),)
        if g.peek("game_mode") == 1 and g.peek16("t3d_frame") == hold and nf == last and nf > hold + 100:
            vb = g.peek16("t3d_vbl")
            out = {"loop": g.peek("t3d_loop"), "frame": hold, "vblanks": vb, "flips": nf,
                   "fps_since_title": round(nf * 60 / vb, 2),
                   "fps": round((nf - 200) * 60 / max(vb - steady[0], 1), 2) if steady else None, "flips_missed": g.peek("t3d_flip_late"),
                   "flip_line_max": 144 + g.peek("t3d_flip_line_max"), "variant": g.peek("t3d_var")}
            break
        last = nf
    g.tick(10)
    img = g.pb.screen.image.convert("RGB")
    g.stop()
    return out, img


VIEW_BOXES = [(0, 32, 160, 114), (0, 126, 160, 136)]  # the view, less PRESS START's band (lines 116-123, it blinks)


def same_picture(a: Image.Image, b: Image.Image) -> tuple[bool, int]:
    """the view pixel for pixel through a one-to-one colour map (SameBoy colour-corrects); returns (same, pixels
    that break the map)"""
    fwd: dict = {}
    back: dict = {}
    bad = 0
    for box in VIEW_BOXES:
        for p, q in zip(a.crop(box).convert("RGB").get_flattened_data(), b.crop(box).convert("RGB").get_flattened_data()):
            if fwd.setdefault(p, q) != q or back.setdefault(q, p) != p:
                bad += 1
    return bad == 0, bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("tester", type=Path)
    ap.add_argument("out", type=Path, nargs="?", default=ROOT / "build/sameboy")
    ap.add_argument("--hold", type=int, default=8)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    report, ok = [], True
    sys.path.insert(0, str(ROOT / "test/harness"))
    from title3d import nloops, seed_for

    n = nloops(ROOT / "build/t3d_bake/gen/tb.h")
    for k, seed in enumerate(seed_for(k, n) for k in range(n)):  # loop k, palette variant k
        rom = build(seed, a.hold, a.out)
        run = a.out / f"sb_{k}"
        shutil.rmtree(run, ignore_errors=True)
        run.mkdir()
        sbrom = run / rom.name
        shutil.copy(rom, sbrom)
        subprocess.run([str(a.tester), "--cgb", "--length", "40", "--sav", str(sbrom)], check=True,
                       capture_output=True)
        sav = sbrom.with_suffix(".sav")
        sb = stats(sav.read_bytes()) if sav.exists() else None
        sb_img = Image.open(sbrom.with_suffix(".bmp")).convert("RGB")
        py, py_img = pyboy_run(rom, a.hold)
        sb_img.save(a.out / f"sameboy_loop{k}.png")
        py_img.save(a.out / f"pyboy_loop{k}.png")
        same, bad = same_picture(py_img, sb_img)
        r = {"loop": k, "seed": f"{seed:04X}", "sameboy": sb, "pyboy": py, "picture_same": same,
             "pixels_off": bad}
        ok &= bool(sb and py and same and sb["loop"] == k and sb["fps"] >= 29.5 and sb["flips_missed"] == 0
                   and py["flips_missed"] == 0 and sb["frame"] == py["frame"] == a.hold)
        report.append(r)
    print(json.dumps({"ok": ok, "loops": report}, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
