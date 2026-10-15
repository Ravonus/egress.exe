"""B2 obstacles and wall structures: room pictures from the real cartridge.

  rooms ROM OUTDIR [--per 2]   the first PER baked rooms of every tier, each as the whole 256x224 room
                               (test/harness/roomimg.py), saved as OUTDIR/room_<tier>_<seed>.png
  compose BEFORE AFTER OUT     the before/after sheet (docs/shots/b2_rooms_structures.png): before rooms above,
                               after rooms below, each with its wall metric line (egtool walls)
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent))
import play as H  # noqa: E402
from roomimg import room_image  # noqa: E402


def rooms(rom: Path, out: Path, per: int) -> int:
    out.mkdir(parents=True, exist_ok=True)
    for tier in range(6):
        for seed in H.baked_seeds(tier)[:per]:
            g = H.Game(rom)
            g.wait_mode(H.MODE_TITLE, 1500)
            g.poke16("dbg_fseed", seed)
            g.poke("dbg_ftier", tier)
            if not H.start_run(g, god=True, no_enemies=True):
                print("no start", tier, seed)
                g.stop()
                return 1
            g.tick(60)
            room_image(g).save(out / f"room_{tier}_{seed}.png")
            g.stop()
    return 0


def metric(egtool: Path, tier: int, seed: int) -> str:
    r = subprocess.run([str(egtool), "walls"], input=f"{seed} {tier}\n", capture_output=True, text=True).stdout
    f = r.split("\n")[0].split()
    return f"comps {f[5]} single {f[7]}" if len(f) > 7 else ""


def compose(before: Path, after: Path, out: Path, egb: Path, ega: Path) -> int:
    def load(d: Path, eg: Path) -> list[tuple[str, Image.Image]]:
        res = []
        for p in sorted(d.glob("room_*.png"), key=lambda p: tuple(int(x) for x in p.stem.split("_")[1:])):
            _, t, s = p.stem.split("_")
            res.append((f"t{t} #{s}  {metric(eg, int(t), int(s))}", Image.open(p).convert("RGB")))
        return res
    b, a = load(before, egb), load(after, ega)
    cols, cw, ch, lab = 6, 256, 224, 14
    rows = (len(b) + cols - 1) // cols + (len(a) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (cw + 4) + 4, rows * (ch + lab + 4) + 2 * 18 + 4), (16, 16, 20))
    d = ImageDraw.Draw(sheet)
    y = 4
    for title, group in (("BEFORE (main generator)", b), ("AFTER (wall structures)", a)):
        d.text((6, y), title, fill=(230, 230, 230))
        y += 18
        for k, (label, im) in enumerate(group):
            x = 4 + (k % cols) * (cw + 4)
            yy = y + (k // cols) * (ch + lab + 4)
            d.text((x + 2, yy), label, fill=(200, 200, 200))
            sheet.paste(im, (x, yy + lab))
        y += ((len(group) + cols - 1) // cols) * (ch + lab + 4)
    sheet.save(out)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("rooms")
    r.add_argument("rom", type=Path)
    r.add_argument("out", type=Path)
    r.add_argument("--per", type=int, default=2)
    c = sub.add_parser("compose")
    c.add_argument("before", type=Path)
    c.add_argument("after", type=Path)
    c.add_argument("out", type=Path)
    c.add_argument("--egtool-before", type=Path, default=Path("build/host/egtool"))
    c.add_argument("--egtool-after", type=Path, default=Path("build/host/egtool"))
    a = ap.parse_args()
    if a.cmd == "rooms":
        return rooms(a.rom, a.out, a.per)
    return compose(a.before, a.after, a.out, a.egtool_before, a.egtool_after)


if __name__ == "__main__":
    sys.exit(main())
