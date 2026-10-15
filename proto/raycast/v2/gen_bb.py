"""Billboard data for the v2 raycaster scenes: mobgen mobs (proto/anim) pre-scaled to size buckets, as 8x16 OBJ
tile columns in ROM banks 5+ (one bank per mob variant), plus the first-person weapon, the elite crown and a spark.

gen_bb.py OUTDIR [--preview PNG]  ->  OUTDIR/bb_data.c, bb_data.h, bank<N>_bb.s

Per frame and bucket: the opaque columns are cropped, the rows are bottom-anchored in 16 px sprite rows, and the
3 most used colours of the variant become OBJ palette entries 1-3 (light, mid, dark); tells, hits and deaths are
palette swaps on the cartridge, so the frames only carry shape."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "anim"))
import billboards as B  # noqa: E402
import mobs  # noqa: E402
from core import M  # noqa: E402

BUCKETS = [12, 16, 24, 32, 40, 48]
# (state, frame index) baked per variant; the cartridge indexes them with BB_* below
FRAMES = [("walk", 0), ("walk", 2), ("windup", 1), ("attack", 1), ("hurt", 0), ("death", 2), ("death", 4)]
FNAMES = ["WALK0", "WALK1", "TELL", "ATTACK", "HURT", "DIE0", "DIE1"]
PICKS = [("OFFICE", 1, "WALKER", 0x1111, False), ("VENTS", 1, "WALKER", 0x2222, False),
         ("OFFICE", 2, "WALKER", 0x3333, True)]
BANK0 = 5
# fixed OBJ tiles (VRAM bank 0, both buffers): crown, spark, weapon frames
T_CROWN, T_SPARK, T_WEAPON = 230, 232, 234


def q555(c):
    r, g, b = (v >> 3 for v in c[:3])
    return r | (g << 5) | (b << 10)


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def kmeans3(cols):
    """cols: {rgb: count} -> 3 centres, light to dark"""
    items = sorted(cols.items(), key=lambda kv: lum(kv[0]))
    n = len(items)
    cent = [items[0][0], items[n // 2][0], items[-1][0]]
    for _ in range(12):
        acc = [[0, 0, 0, 0] for _ in range(3)]
        for c, w in items:
            k = min(range(3), key=lambda i: sum((c[j] - cent[i][j]) ** 2 for j in range(3)))
            for j in range(3):
                acc[k][j] += c[j] * w
            acc[k][3] += w
        cent = [tuple(round(a[j] / a[3]) for j in range(3)) if a[3] else cent[i] for i, a in enumerate(acc)]
    return sorted(cent, key=lum, reverse=True)


def to_index(img, pal):
    w, h = img.size
    px = img.load()
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                continue
            out[y][x] = 1 + min(range(3), key=lambda i: (r - pal[i][0]) ** 2 + (g - pal[i][1]) ** 2 + (b - pal[i][2]) ** 2)
    return out


def tile_rows(ix, x0, y0):
    """32 bytes: an 8x16 block of the indexed image at (x0, y0), out-of-range pixels transparent"""
    h, w = len(ix), len(ix[0])
    out = []
    for y in range(y0, y0 + 16):
        lo = hi = 0
        for x in range(x0, x0 + 8):
            v = ix[y][x] if 0 <= y < h and 0 <= x < w else 0
            lo = (lo << 1) | (v & 1)
            hi = (hi << 1) | (v >> 1)
        out += [lo, hi]
    return out


def pack(ix):
    """crop + bottom-anchored 8x16 columns -> (ncols, nrows, x0, ytop, bytes column-major)"""
    S = len(ix)
    xs = [x for x in range(S) if any(ix[y][x] for y in range(S))]
    ys = [y for y in range(S) if any(ix[y])]
    if not xs:
        return 0, 0, 0, 0, []
    cx0, cx1, cy0 = xs[0], xs[-1] + 1, ys[0]
    ncols = (cx1 - cx0 + 7) // 8
    nrows = (S - cy0 + 15) // 16
    ytop = S - nrows * 16
    data = []
    for c in range(ncols):
        for r in range(nrows):
            data += tile_rows(ix, cx0 + c * 8, ytop + r * 16)
    return ncols, nrows, cx0, ytop, data


def fixed_art():
    """crown (8x8 in the top of an 8x16), spark, weapon idle + strike (16x32 each): indexed 1 light, 2 mid, 3 dark"""
    def blank(w, h):
        return [[0] * w for _ in range(h)]
    crown = blank(8, 16)
    for x, y in [(0, 2), (2, 1), (4, 0), (6, 1), (7, 2)]:
        crown[y][x] = 1
    for y in range(3, 6):
        for x in range(8):
            crown[y][x] = 2 if (x + y) & 1 else 1
    for x in range(8):
        crown[6][x] = 3
    crown[4][3] = crown[4][4] = 3
    spark = blank(8, 16)
    for x, y, v in [(3, 0, 2), (3, 1, 1), (2, 2, 2), (3, 2, 1), (4, 2, 2), (3, 3, 1), (3, 4, 2)]:
        spark[y][x] = v

    def weapon(strike):
        im = Image.new("P", (16, 32), 0)
        d = ImageDraw.Draw(im)
        if not strike:  # a length of conduit held low on the right, glowing tip
            d.line([(13, 31), (6, 6)], fill=3, width=4)
            d.line([(12, 31), (6, 7)], fill=2, width=2)
            d.line([(11, 30), (6, 8)], fill=1, width=1)
            d.ellipse([(3, 2), (9, 8)], fill=1, outline=2)
            d.rectangle([(9, 24), (15, 31)], fill=3)
            d.rectangle([(10, 25), (14, 30)], fill=2)
        else:  # swung across: tip to the upper left, hand pulled in
            d.line([(15, 26), (0, 10)], fill=3, width=4)
            d.line([(14, 26), (1, 11)], fill=2, width=2)
            d.line([(13, 25), (2, 12)], fill=1, width=1)
            for x, y in [(0, 6), (3, 4), (1, 15), (5, 8)]:
                d.point((x, y), fill=1)
            d.rectangle([(10, 24), (15, 31)], fill=3)
            d.rectangle([(11, 25), (14, 30)], fill=2)
        px = im.load()
        return [[px[x, y] for x in range(16)] for y in range(32)]
    return crown, spark, weapon(False), weapon(True)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--preview", type=Path)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    desc, pals, banks, prev = [], [], [], []
    for vi, (fam, sz, mob, seed, el) in enumerate(PICKS):
        mv = mobs.make(fam, sz, mob, M.mix_seed(seed, 0), elite=el)
        imgs = {}
        cols = {}
        for fi, (st, k) in enumerate(FRAMES):
            for b in BUCKETS:
                im = B.billboard_frame(mv, st, k, 0, b).convert("RGBA")
                imgs[fi, b] = im
                if b == 48 and st in ("walk", "hurt"):
                    for p in im.getdata():
                        if p[3] >= 128:
                            cols[p[:3]] = cols.get(p[:3], 0) + 1
        pal = kmeans3(cols)
        pals.append(pal)
        blob = []
        vdesc = []
        for fi in range(len(FRAMES)):
            row = []
            for b in BUCKETS:
                ix = to_index(imgs[fi, b], pal)
                nc, nr, x0, yt, data = pack(ix)
                row.append((0x4000 + len(blob), nc, nr, x0, yt, b))
                blob += data
                if b == 48:
                    prev.append((vi, fi, ix, pal))
            vdesc.append(row)
        assert len(blob) <= 0x4000, (vi, len(blob))
        desc.append(vdesc)
        banks.append(blob)
        lines = [f"; generated by gen_bb.py: billboard tiles for mob variant {vi} ({mv['id']})",
                 f"\t.module bank{BANK0 + vi}_bb", f"\t.area _CODE_{BANK0 + vi}", f"_bb_bank{vi}::"]
        for i in range(0, len(blob), 32):
            lines.append("\t.db " + ",".join(f"0x{v:02X}" for v in blob[i : i + 32]))
        (a.out / f"bank{BANK0 + vi}_bb.s").write_text("\n".join(lines) + "\n")
        print(f"variant {vi} {mv['id']}: {len(blob)} B, palette {pal}")

    crown, spark, w0, w1 = fixed_art()
    fixed = tile_rows(crown, 0, 0) + tile_rows(spark, 0, 0)
    for w in (w0, w1):  # 2 cols x 2 rows, column-major
        for c in range(2):
            for r in range(2):
                fixed += tile_rows(w, c * 8, r * 16)

    nv, nf, nb = len(PICKS), len(FRAMES), len(BUCKETS)
    c = ["/* generated by gen_bb.py */", '#include "bb_data.h"',
         "const uint8_t BB_DESC[%d] = {" % (nv * nf * nb * 7)]
    for vi in range(nv):
        for fi in range(nf):
            for addr, nc, nr, x0, yt, b in desc[vi][fi]:
                c.append(f"  {BANK0 + vi}, 0x{addr & 255:02X}, 0x{addr >> 8:02X}, {nc}, {nr}, {x0}, {yt & 255},")
    c.append("};")
    c.append("const uint8_t BB_BUCKET[%d] = {%s};" % (nb, ", ".join(map(str, BUCKETS))))
    c.append("const uint16_t BB_PAL[%d] = {" % (nv * 3))
    for p in pals:
        c.append("  " + ", ".join(f"0x{q555(x):04X}" for x in p) + ",")
    c.append("};")
    c.append("const uint8_t BB_FIXED[%d] = {" % len(fixed))
    for i in range(0, len(fixed), 32):
        c.append("  " + ", ".join(f"0x{v:02X}" for v in fixed[i : i + 32]) + ",")
    c.append("};")
    (a.out / "bb_data.c").write_text("\n".join(c) + "\n")
    h = ["/* generated by gen_bb.py */", "#ifndef BB_DATA_H", "#define BB_DATA_H", "#include <stdint.h>",
         f"#define BB_NVAR {nv}", f"#define BB_NFRAME {nf}", f"#define BB_NBUCKET {nb}", f"#define BB_BANK0 {BANK0}",
         f"#define T_CROWN {T_CROWN}", f"#define T_SPARK {T_SPARK}", f"#define T_WEAPON {T_WEAPON}",
         f"#define BB_NFIXED {len(fixed) // 16}"]
    for i, n in enumerate(FNAMES):
        h.append(f"#define BB_{n} {i}")
    h += ["extern const uint8_t BB_DESC[]; /* [var][frame][bucket] x {bank, addr lo, hi, ncols, nrows, x0, ytop} */",
          "extern const uint8_t BB_BUCKET[];", "extern const uint16_t BB_PAL[];", "extern const uint8_t BB_FIXED[];",
          "#endif"]
    (a.out / "bb_data.h").write_text("\n".join(h) + "\n")

    if a.preview:
        sc = 4
        im = Image.new("RGB", ((nf * 52 + 4) * sc, (nv * 52 + 40) * sc), (30, 24, 48))
        px = im.load()
        for vi, fi, ix, pal in prev:
            ox, oy = 4 + fi * 52, 4 + vi * 52
            for y in range(48):
                for x in range(48):
                    v = ix[y][x]
                    if v:
                        for dy in range(sc):
                            for dx in range(sc):
                                px[(ox + x) * sc + dx, (oy + y) * sc + dy] = pal[v - 1]
        im.save(a.preview)


if __name__ == "__main__":
    main()
