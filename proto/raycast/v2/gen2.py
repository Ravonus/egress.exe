"""Generate raycaster v2 tables: projection, fog, wall texture runs, floor/ceiling sets, biome palettes.

    python gen2.py OUTDIR --w 96 --h 64 [--fr 2]

Writes OUTDIR/tables2.c (shared by ROM and host), OUTDIR/tables2_abs.s (page-aligned: height index 0x6B00, quarter
squares 0x6C00, mid-ray weights 0x7000, for the asm), OUTDIR/tex_ids.h (texture and set ids for main2.c).
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import art as A  # noqa: E402
import gen_tables as G1  # noqa: E402  (v1: HUD font + frame tiles)

ANG = 1024
FOV = 192
FOG_NEAR, FOG_FAR = 3.0, 10.0  # cells: fog starts / is total


def arr(name, ctype, vals, per=16):
    rows = [", ".join(str(v) for v in vals[i : i + per]) for i in range(0, len(vals), per)]
    return f"const {ctype} {name}[{len(vals)}] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


WALLS = [  # (id name, texture)
    ("O_BASE0", lambda: A.office_base(0)),
    ("O_BASE1", lambda: A.office_base(1)),
    ("O_BASE2", lambda: A.office_base(2)),
    ("O_OUTLET", lambda: A.office_decor("outlet", 0)),
    ("O_POSTER", lambda: A.office_decor("poster", 1)),
    ("O_VENT", lambda: A.office_decor("vent", 2)),
    ("O_EXIT", lambda: A.office_decor("exit", 0)),
    ("O_CRT", lambda: A.office_decor("crt", 1)),
    ("O_STAIN", lambda: A.office_decor("stain", 2)),
    ("O_WATER", lambda: A.office_decor("water", 0)),
    ("O_TEAR", lambda: A.office_decor("tear", 1)),
    ("O_CLOCK", lambda: A.office_decor("clock", 2)),
    ("O_DOOR", lambda: A.office_door(False)),
    ("O_DOOREXIT", lambda: A.office_door(True)),
    ("O_BURN0", lambda: A.office_burn(0)),
    ("O_BURN1", lambda: A.office_burn(1)),
    ("O_BURN2", lambda: A.office_burn(2)),
    ("O_CHAR", lambda: A.office_char()),
    ("V_BASE0", lambda: A.vents_base(0)),
    ("V_BASE1", lambda: A.vents_base(1)),
    ("V_BASE2", lambda: A.vents_base(2)),
    ("V_GRATE", lambda: A.vents_decor("grate", 0)),
    ("V_FAN", lambda: A.vents_decor("fan", 1)),
    ("V_PIPES", lambda: A.vents_decor("pipes", 0)),
    ("V_HAZARD", lambda: A.vents_decor("hazard", 1)),
    ("V_SIGN", lambda: A.vents_decor("sign", 0)),
    ("V_STEAM", lambda: A.vents_decor("steam", 2)),
    ("V_LADDER", lambda: A.vents_decor("ladder", 0)),
    ("V_HATCH", lambda: A.vents_hatch()),
    ("B_OV4", lambda: A.blend(A.office_base(0), A.vents_base(0), 4)),
    ("B_OV8", lambda: A.blend(A.office_base(0), A.vents_base(0), 8)),
    ("B_OV12", lambda: A.blend(A.office_base(0), A.vents_base(0), 12)),
]

SETS = [
    ("S_OFFICE", A.office_floor(True), A.office_ceiling(True)),
    ("S_OFFDARK", A.office_floor(False), A.office_ceiling(False)),
    ("S_VENTS", A.vents_floor(True), A.vents_ceiling(True)),
    ("S_VENTDARK", A.vents_floor(False), A.vents_ceiling(False)),
]
for f in (4, 8, 12):
    SETS.append((f"S_BLEND{f}", A.tex_blend_sets(SETS[0][1], SETS[2][1], f), A.tex_blend_sets(SETS[0][2], SETS[2][2], f)))
SETS.append(("S_SPARE", A.office_floor(True), A.office_ceiling(True)))  # 8 sets: the asm masks the set with & 7

PALS = ["office", "vents", "dark"]


def fogk(d: float) -> int:
    return max(0, min(16, round((d - FOG_NEAR) * 16 / (FOGFAR[0] - FOG_NEAR))))


MIPH = 32
FOGFAR = [FOG_FAR]


def runs_of(col):
    rl = []
    for v in range(16):
        if v == 15 or col[v + 1] != col[v]:
            rl += [v, col[v]]
    return rl


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--w", type=int, default=96)
    ap.add_argument("--h", type=int, default=64)
    ap.add_argument("--fr", type=int, default=2)
    ap.add_argument("--frfar", type=int, default=4)
    ap.add_argument("--split", type=int, default=32)
    ap.add_argument("--fogfar", type=float, default=FOG_FAR, help="cells where the fog is total (rows past it are skipped)")
    ap.add_argument("--sidek", type=int, default=4)
    ap.add_argument("--vs", type=int, default=1, help="vertical display scale (2: rows are doubled on screen by SCY)")
    a = ap.parse_args()
    FOGFAR[0] = a.fogfar
    a.out.mkdir(parents=True, exist_ok=True)
    W, H, FR = a.w, a.h, a.fr
    n = W // 4
    half = math.tan(math.radians(FOV / ANG * 360 / 2))
    proj = (W / 2) / half  # horizontal, in screen px
    projv = proj / a.vs  # vertical, in framebuffer rows (each shown a.vs times)

    cos_abs, delta = [], []
    for i in range(ANG):
        c = abs(math.cos(2 * math.pi * i / ANG))
        cos_abs.append(min(255, round(c * 256)))
        delta.append(0x7F00 if c < 1e-6 else min(0x7F00, round(256 / c)))
    coloff, colcos = [], []
    for i in range(n):
        x = ((i + 0.5) / n * 2 - 1) * half
        ang = math.atan(x)
        coloff.append(round(ang / (2 * math.pi) * ANG) % ANG)
        colcos.append(min(255, round(math.cos(ang) * 256)))
    height = [max(1, min(255, round(projv / (max(p, 1) / 64)))) for p in range(2048)]
    voff = 255 - (H + 255) // 2
    vtop = [(voff + ((H - h) >> 1)) & 255 for h in range(256)]
    hv2 = [[(h * (k + 1)) >> 4 for h in range(256)] for k in range(16)]

    fog_k = [16 if h < 1 else fogk(projv / h) for h in range(256)]
    fogpat = []
    for k in range(17):
        for row in range(4):
            nib = 0
            for x in range(4):
                if A.bayer(x, row) >= k:
                    nib |= 8 >> x
            fogpat.append(nib << 4 | nib)
    # floor sample-row groups from the horizon down: FRFAR rows per group while o < SPLIT (far, fogged), then FR
    fd, fpat, fy0, fnr = [], [0] * H, [], []
    o = 0
    while o < H // 2:
        fr = a.frfar if o < a.split else FR
        fr = min(fr, H // 2 - o)
        d = projv / (2 * (o + fr / 2))
        fd.append(0 if fogk(d) >= 16 else min(255, round(d * 16)))
        fy0.append(H // 2 + o)
        fnr.append(fr)
        o += fr
    for y in range(H // 2, H):
        d = projv / (2 * (y - H / 2 + 0.5))
        k = fogk(d)
        fpat[y] = fogpat[k * 4 + (y & 3)]
        yc = H - 1 - y
        fpat[yc] = fogpat[k * 4 + (yc & 3)]
    x0 = (0.5 / n * 2 - 1) * half
    lk = [round(x0 * 64), round(2 * half / n * 2048)]

    woff, wruns = [], []
    for name, fn in WALLS:
        t = fn()
        for u in range(16):
            woff.append(len(wruns))
            wruns += runs_of([t[v][u] for v in range(16)])
    # mip 1 (walls shorter than MIPH px): 8 texel rows, each pair keeps the less common colour (details survive)
    nmip0 = len(woff)
    for name, fn in WALLS:
        t = fn()
        for u in range(16):
            col = [t[v][u] for v in range(16)]
            com = max(set(col), key=col.count)
            m1 = []
            for j in range(8):
                a0, a1 = col[2 * j], col[2 * j + 1]
                c = a1 if (a0 == com and a1 != com) else a0
                m1 += [c, c]
            woff.append(len(wruns))
            wruns += runs_of(m1)
    ftex = []
    for name, fl, ce in SETS:
        for v in range(16):
            for u in range(16):
                ftex.append(fl[v][u] | (ce[v][u] << 2))

    bpal = []
    for p in PALS:
        for row in A.biome_palettes(p):
            for c in row:
                r, g, b = (v >> 3 for v in c)
                bpal.append(r | (g << 5) | (b << 10))

    tiles = []
    for ch in G1.HUD_CHARS:
        tiles += G1.glyph_tile(G1.FONT[ch])
    for kind in ["", "t", "b", "l", "r", "tl", "tr", "bl", "br"]:
        tiles += G1.frame_tile(kind)

    s = ["/* generated by gen2.py, do not edit */", '#include "ray2.h"', ""]
    s.append(arr("T_COSABS", "uint8_t", cos_abs))
    s.append(arr("T_DELTA", "uint16_t", delta))
    s.append(arr("T_COLOFF", "uint16_t", coloff))
    s.append(arr("T_COLCOS", "uint8_t", colcos))
    s.append(arr("T_HEIGHT", "uint8_t", height))
    s.append(arr("T_VTOP", "uint8_t", vtop))
    s.append(arr("T_FOGK", "uint8_t", fog_k))
    s.append(arr("T_FOGPAT", "uint8_t", fogpat))
    s.append(arr("T_FD", "uint8_t", fd))
    s.append(arr("T_FY0", "uint8_t", fy0))
    s.append(arr("T_FN", "uint8_t", fnr))
    s.append(arr("T_FPAT", "uint8_t", fpat))
    s.append("#ifndef __SDCC /* the cartridge reads these from the bank blobs (bank2_walls.s, bank3_floors.s) */")
    s.append(arr("T_WOFF", "uint16_t", woff))
    s.append(arr("T_WRUNS", "uint8_t", wruns))
    s.append(arr("T_FTEX", "uint8_t", ftex))
    s.append("#endif")
    s.append(arr("T_LK", "int8_t", lk))
    s.append(arr("T_BPAL", "uint16_t", bpal))
    s.append("#if !defined(__SDCC) || !FS /* fullscreen: the cartridge copies them from bank 3 (HUDTILES_ADDR) */")
    s.append(arr("T_HUDTILES", "uint8_t", tiles))
    s.append("#endif")
    s.append("#ifndef __SDCC")
    s.append(arr("T_HV2", "uint8_t", [v for row in hv2 for v in row]))
    s.append("#endif")

    sq = [i * i // 4 for i in range(512)]
    # mid-ray weight: r = 256 h1 / (h0 + h1) at quantised heights (exact below 24, then steps of 2, then 10)
    def hix(h):
        return h if h < 24 else (24 + (h - 24) // 2 if h < 64 else min(63, 44 + (h - 64) // 10))
    def hrep(k):
        return max(0.5, k) if k < 24 else (24 + 2 * (k - 24) + 0.5 if k < 44 else 64 + 10 * (k - 44) + 4.5)
    t_hix = [hix(h) for h in range(256)]
    t_rt = [min(255, round(256 * hrep(k1) / (hrep(k0) + hrep(k1)))) for k0 in range(64) for k1 in range(64)]
    s.append("#ifndef __SDCC")
    s.append(arr("T_HIX", "uint8_t", t_hix))
    s.append(arr("T_RT", "uint8_t", t_rt))
    s.append("#endif")
    (a.out / "tables2.c").write_text("\n".join(s))
    lines = ["; generated by gen2.py: quarter squares for the cast's multiply, mid-ray weights", "\t.module tabs_abs",
             "\t.area _TABS (ABS)", "\t.org 0x7000"]
    for i in range(0, 4096, 16):
        lines.append("\t.db " + ", ".join(f"0x{b:02X}" for b in t_rt[i : i + 16]))
    lines.append("\t.org 0x6C00")
    for part in (bytes(v & 255 for v in sq), bytes(v >> 8 for v in sq)):
        for i in range(0, 512, 16):
            lines.append("\t.db " + ", ".join(f"0x{b:02X}" for b in part[i : i + 16]))
    lines.append("\t.org 0x6B00")
    for i in range(0, 256, 16):
        lines.append("\t.db " + ", ".join(f"0x{b:02X}" for b in t_hix[i : i + 16]))
    (a.out / "tables2_abs.s").write_text("\n".join(lines) + "\n")

    # ---- ROM bank blobs for the asm (each starts at 0x4000 of its bank) ----
    def blob(area, parts, name):
        out = bytearray()
        lay = {}
        for key, addr, data in parts:
            off = addr - 0x4000
            assert off >= len(out), (key, hex(addr), hex(0x4000 + len(out)))
            out += bytes(off - len(out))
            lay[key] = addr
            out += bytes(data)
        assert len(out) <= 0x4000, (name, len(out))
        lines = [f"; generated by gen2.py: {name}", f"\t.module {name}", f"\t.area {area}", f"_{name}::"]
        for i in range(0, len(out), 32):
            lines.append("\t.db " + ",".join(f"0x{b:02X}" for b in out[i : i + 32]))
        (a.out / f"{name}.s").write_text("\n".join(lines) + "\n")
        return lay, len(out)

    hv2_flat = [v for row in hv2 for v in row]
    # mip 0 in bank 2, mip 1 in bank 4 (same table layout; the asm maps the bank by wall height)
    nr0 = woff[nmip0] if nmip0 < len(woff) else len(wruns)
    base_runs = 0x5600 + 2 * nmip0
    # first run end (vend) that can reach the screen, per wall height: earlier runs end at or above VOFF
    v0t = []
    for h in range(256):
        v = 0
        while v < 15 and vtop[h] + hv2[v][h] <= voff:
            v += 1
        v0t.append(v)
    woff_abs, woff_abs1 = [], []
    for o in woff[:nmip0]:
        woff_abs += [(base_runs + o) & 255, (base_runs + o) >> 8]
    for o in woff[nmip0:]:
        o = base_runs + o - nr0
        woff_abs1 += [o & 255, o >> 8]
    # colour tables for the walls asm: CTAB[slot * 256 + (side * 6 + kclass) * 16 + colour * 4] = (lo, hi, mode, 0)
    # modes: 0 plain, 1 checker (level 8), 2 Bayer pattern (level k), 3 zero/skip, 4 fixed pattern (level SIDEK, the
    # y-face mid colour's floor); kclass of fog level k: 0 | 1..SIDEK-1 | SIDEK..7 | 8 | 9-15 | 16
    assert fogpat[32] == 0x55 and fogpat[33] == 0xAA and fogpat[34] == 0x55 and fogpat[35] == 0xAA
    SK = a.sidek
    side1 = [0, 2, 2, 3]
    ct = bytearray(512)
    for slot in range(2):
        m = 0x0F if slot else 0xF0
        for side in range(2):
            for kc in range(6):
                for c in range(4):
                    cc = side1[c] if side else c
                    lo, hi = (m if cc & 1 else 0), (m if cc & 2 else 0)
                    mode = (0, 2, 2, 1, 2, 3)[kc]
                    if side and c == 2 and kc in (0, 1):
                        mode = 1 if SK == 8 else 4
                    if lo == 0 and hi == 0:
                        mode = 3
                    off = slot * 256 + (side * 6 + kc) * 16 + c * 4
                    ct[off : off + 4] = bytes([lo, hi, mode, 0])
    kclass = [0] + [1] * (SK - 1) + [2] * (8 - SK) + [3] + [4] * 7 + [5]
    assert len(kclass) == 17
    lay2, n2 = blob("_CODE_2", [("HV2", 0x4000, hv2_flat), ("VTOP", 0x5000, vtop), ("FOGK", 0x5100, fog_k),
                                ("FOGPAT", 0x5200, fogpat), ("KCLASS", 0x5280, kclass), ("CTAB", 0x5300, ct),
                                ("V0T", 0x5500, v0t), ("WOFF", 0x5600, woff_abs), ("WRUNS", base_runs, wruns[:nr0])],
                    "bank2_walls")
    blob("_CODE_4", [("HV2", 0x4000, hv2_flat), ("VTOP", 0x5000, vtop), ("FOGK", 0x5100, fog_k),
                     ("FOGPAT", 0x5200, fogpat), ("KCLASS", 0x5280, kclass), ("CTAB", 0x5300, ct),
                     ("V0T", 0x5500, v0t), ("WOFF", 0x5600, woff_abs1), ("WRUNS", base_runs, wruns[nr0:])], "bank4_walls1")
    nib = [0x00, 0x0F, 0xF0, 0xFF]
    lo16 = [nib[((i >> 2) & 1) << 1 | (i & 1)] for i in range(16)]  # index (c0 << 2) | c1, bit 0 of each
    hi16 = [nib[((i >> 3) & 1) << 1 | ((i >> 1) & 1)] for i in range(16)]
    ftex_end = 0x4400 + len(ftex)
    # per camera angle: f_lx, f_ly, f_ux, f_uy (int8, same integer math as core2.c's floors_all), 4 x 1 KB
    def scos(a):
        a &= 1023
        v = cos_abs[a]
        return v if (a < 256 or a >= 768) else -v
    def i8(v):
        return ((v + 128) & 255) - 128
    lkt = [[], [], [], []]
    for ca in range(1024):
        cs, sn = scos(ca), scos(ca - 256)
        lkt[0].append(i8((cs >> 2) - ((lk[0] * sn) >> 8)) & 255)
        lkt[1].append(i8((sn >> 2) + ((lk[0] * cs) >> 8)) & 255)
        lkt[2].append(i8((lk[1] * -sn) >> 8) & 255)
        lkt[3].append(i8((lk[1] * cs) >> 8) & 255)
    lkt_flat = lkt[0] + lkt[1] + lkt[2] + lkt[3]
    # asm form: slot-0 texel planes in bits 7-4 (floor lo, floor hi, ceil lo, ceil hi), slot-1 copy in bits 3-0;
    # the OR of both samples indexes four 256-byte pages giving the tile bytes directly
    ftex0 = [((b & 1) << 7) | ((b & 2) << 5) | ((b & 4) << 3) | ((b & 8) << 1) for b in ftex]
    ftex1 = [v >> 4 for v in ftex0]
    lut = []
    for bl, bh in ((7, 3), (6, 2), (5, 1), (4, 0)):
        lut += [(0xF0 if x >> bl & 1 else 0) | (0x0F if x >> bh & 1 else 0) for x in range(256)]
    lay3, n3 = blob("_CODE_3", [("SQLO", 0x4000, [v & 255 for v in sq]), ("SQHI", 0x4200, [v >> 8 for v in sq]),
                                ("FTEX", 0x4400, ftex0), ("FD", ftex_end, fd), ("FPAT", ftex_end + 0x40, fpat), ("FY0", ftex_end + 0xC0, fy0),
                                ("FN", ftex_end + 0xE0, fnr),
                                ("NLO", ftex_end + 0x100, lo16), ("NHI", ftex_end + 0x110, hi16),
                                ("NIB", ftex_end + 0x120, nib), ("FTEX1", ftex_end + 0x200, ftex1),
                                ("FLUT", ftex_end + 0x200 + len(ftex1), lut),
                                ("LKT", 0x6000, lkt_flat), ("HUDTILES", 0x7000, tiles)], "bank3_floors")
    inc = [f"{k} = 0x{v:04X}" for k, v in {**lay2, **lay3}.items()]
    inc += [f"NFROW = {len(fd)}", f"FR = {FR}", f"MIPH = {MIPH}", "MIPOFF = 0", f"SIDEK = {a.sidek}"]
    # rows y0..: the leading d = 0 sample rows (the horizon band) are left to the walls' zeroing
    zb = next(y for y, d in zip(fy0, fd) if d)
    assert all(d == 0 for y, d in zip(fy0, fd) if y < zb)
    inc += [f"ZBAND = {zb}"]
    (a.out / "layout.inc").write_text("\n".join(inc) + "\n")
    print(f"bank2 {n2} B, bank3 {n3} B")

    hdr = ["/* generated by gen2.py */", "#ifndef TEX_IDS_H", "#define TEX_IDS_H"]
    for i, (name, _) in enumerate(WALLS):
        hdr.append(f"#define {name} {i + 1}u")
    hdr.append(f"#define NWALLTEX {len(WALLS)}u")
    hdr.append(f"#define NFROWS {len(fd)}u")
    hdr.append(f"#define MIPH {MIPH}u")
    hdr.append(f"#define SIDEK {a.sidek}u")
    hdr.append(f"#define MIPN {nmip0}u")
    for i, (name, _, _) in enumerate(SETS):
        hdr.append(f"#define {name} {i}u")
    for i, p in enumerate(PALS):
        hdr.append(f"#define PAL_{p.upper()} {i}u")
    hdr.append("extern const uint16_t T_BPAL[];\nextern const uint8_t T_HUDTILES[];\n#define HUDTILES_ADDR 0x7000u /* bank 3 */")
    hdr.append("#endif")
    (a.out / "tex_ids.h").write_text("\n".join(hdr) + "\n")
    print(f"tables: rays={n} proj={proj:.1f} walls={len(WALLS)} runs={len(wruns)}B sets={len(SETS)} lk={lk} fd={fd}")


if __name__ == "__main__":
    main()
