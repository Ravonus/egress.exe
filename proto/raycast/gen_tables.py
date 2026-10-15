"""Generate the per-config lookup tables for the raycaster prototype.

    python gen_tables.py OUT.c --w 128 --h 80 --colw 4

Angles are 1024 units per turn (0 = east, 256 = south, y grows downwards). Distances are 8.8 fixed point in cells.
"""

from __future__ import annotations

import argparse
import math

ANG = 1024
FOV = 192  # 67.5 degrees


def arr(name: str, ctype: str, vals: list[int], per: int = 16) -> str:
    rows = [", ".join(str(v) for v in vals[i : i + per]) for i in range(0, len(vals), per)]
    return f"const {ctype} {name}[{len(vals)}] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


# 5x7 font for the HUD (only the glyphs the HUD prints)
FONT = {
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    " ": ["00000"] * 7,
}
HUD_CHARS = "0123456789EGRSX.FP/ "


def glyph_tile(rows: list[str]) -> list[int]:
    # background colour 3, ink colour 1
    out = []
    pix = [[3] * 8 for _ in range(8)]
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch == "1":
                pix[y][x + 1] = 1
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            c = pix[y][x]
            lo |= (c & 1) << (7 - x)
            hi |= ((c >> 1) & 1) << (7 - x)
        out += [lo, hi]
    return out


def solid_tile(c: int) -> list[int]:
    return [0xFF if c & 1 else 0, 0xFF if c & 2 else 0] * 8


def frame_tile(kind: str) -> list[int]:
    # border lines in colour 2 on colour 3
    pix = [[3] * 8 for _ in range(8)]
    if len(kind) == 2:  # corner: one pixel where the two lines meet
        pix[7 if "t" in kind else 0][7 if "l" in kind else 0] = 2
        kind = ""
    for i in range(8):
        if "t" in kind:
            pix[7][i] = 2
        if "b" in kind:
            pix[0][i] = 2
        if "l" in kind:
            pix[i][7] = 2
        if "r" in kind:
            pix[i][0] = 2
    out = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            c = pix[y][x]
            lo |= (c & 1) << (7 - x)
            hi |= ((c >> 1) & 1) << (7 - x)
        out += [lo, hi]
    return out


def textures() -> list[int]:
    """TEX[type 0 wall / 1 door][side 0 x-face / 1 y-face][u 0..7][v 0..15] colour indices 1..3."""
    out = []
    for typ in range(2):
        for side in range(2):
            B = 1 if side == 0 else 2
            A = 3
            for u in range(8):
                col = []
                for v in range(16):
                    if typ == 0:  # backrooms wallpaper: top trim, one seam per cell, baseboard
                        c = B
                        if v == 0 or v >= 13:
                            c = A
                        elif u == 0:
                            c = A
                        elif u == 4 and v in (5, 9):
                            c = A
                    else:  # door: light frame, dark slab, light knob
                        c = A
                        if u in (0, 7) or v <= 1:
                            c = B
                        elif u == 5 and v in (8, 9):
                            c = B
                        elif v == 15:
                            c = B
                    col.append(c)
                out += col
    return out


def sprite_tex() -> list[int]:
    """16x16 billboard, column-major [u][v], 0 = transparent, else colour index 1..3. A thin figure."""
    art = [
        "................",
        "......3333......",
        ".....333333.....",
        ".....313313.....",
        ".....333333.....",
        "......3333......",
        ".......33.......",
        "....33333333....",
        "...3333333333...",
        "...33.3333.33...",
        "...33.3333.33...",
        "......3333......",
        "......3..3......",
        "......3..3......",
        "......3..3......",
        ".....33..33.....",
    ]
    out = []
    for u in range(16):
        for v in range(16):
            ch = art[v][u] if u < len(art[v]) else "."
            out.append(int(ch) if ch in "123" else 0)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--w", type=int, required=True)
    ap.add_argument("--h", type=int, required=True)
    ap.add_argument("--colw", type=int, required=True)
    a = ap.parse_args()
    n = a.w // a.colw
    half = math.tan(math.radians(FOV / ANG * 360 / 2))
    proj_px = (a.w / 2) / half  # wall height in px at distance 1 cell (square pixels)

    cos_abs = []
    delta = []
    for i in range(ANG):
        c = abs(math.cos(2 * math.pi * i / ANG))
        cos_abs.append(min(255, round(c * 256)))
        delta.append(0x7F00 if c < 1e-6 else min(0x7F00, round(256 / c)))

    coloff = []
    colcos = []
    for i in range(n):
        x = ((i + 0.5) / n * 2 - 1) * half
        ang = math.atan(x)
        coloff.append(round(ang / (2 * math.pi) * ANG) % ANG)
        colcos.append(min(255, round(math.cos(ang) * 256)))

    height = []
    for p in range(2048):  # perp distance index: perp_8.8 >> 2, so cells = p / 64
        d = max(p, 1) / 64
        height.append(max(1, min(255, round(proj_px / d))))

    voff = 255 - (a.h + 255) // 2
    vtop = [voff + ((a.h - h) >> 1) for h in range(256)]
    hv = []
    for h in range(256):
        hv += [(h * (k + 1)) >> 4 for k in range(16)]
    tex = textures()
    runs = []
    for col in range(32):
        cols = tex[col * 16 : col * 16 + 16]
        rl = []
        for v in range(16):
            if v == 15 or cols[v + 1] != cols[v]:
                rl += [v, cols[v]]
        assert len(rl) <= 32
        runs += rl + [0] * (32 - len(rl))
    spr = sprite_tex()
    sruns = []
    for u in range(16):
        cols = spr[u * 16 : u * 16 + 16]
        rl = []
        for v in range(16):
            if v == 15 or cols[v + 1] != cols[v]:
                rl += [v, cols[v]]
        sruns += rl + [0] * (32 - len(rl))
    sq = [i * i // 4 for i in range(512)]

    # sprite projection: column = n/2 + lateral * SPR_PROJ / depth  (SPR_PROJ in rays per unit tan)
    spr_proj = round((n / 2) / half * 16)  # x16 for precision

    tiles = []
    for ch in HUD_CHARS:
        tiles += glyph_tile(FONT[ch])
    for kind in ["", "t", "b", "l", "r", "tl", "tr", "bl", "br"]:
        tiles += frame_tile(kind)

    s = ["/* generated by gen_tables.py, do not edit */", '#include "ray.h"', ""]
    s.append(arr("T_COSABS", "uint8_t", cos_abs))
    s.append(arr("T_DELTA", "uint16_t", delta))
    s.append(arr("T_COLOFF", "uint16_t", coloff))
    s.append(arr("T_COLCOS", "uint8_t", colcos))
    s.append(arr("T_HEIGHT", "uint8_t", height))
    s.append(arr("T_TEX", "uint8_t", textures()))
    s.append(arr("T_SPRTEX", "uint8_t", sprite_tex()))
    s.append(arr("T_HUDTILES", "uint8_t", tiles))
    s.append(arr("T_VTOP", "uint8_t", vtop))
    s.append(arr("T_HV", "uint8_t", hv))
    s.append(arr("T_RUNS", "uint8_t", runs))
    s.append(arr("T_SRUNS", "uint8_t", sruns))
    s.append(arr("T_SQ", "uint16_t", sq))
    s.append(f"const uint16_t T_SPRPROJ = {spr_proj};\n")
    open(a.out, "w").write("\n".join(s))
    # page-aligned quarter squares for the asm multiply (placed at 0x7800 by the linker: lo[512], hi[512])
    sqs = ["; generated by gen_tables.py", "\t.module sqtab", "\t.area _SQTAB (ABS)", "\t.org 0x7800"]
    for part in (bytes(v & 255 for v in sq), bytes(v >> 8 for v in sq)):
        for i in range(0, 512, 16):
            sqs.append("\t.db " + ", ".join(f"0x{b:02X}" for b in part[i : i + 16]))
    # T_HV transposed, page per vend: HV2[vend][h] at 0x6800
    sqs.append("\t.org 0x6800")
    for k in range(16):
        page = bytes(hv[h * 16 + k] for h in range(256))
        for i in range(0, 256, 16):
            sqs.append("\t.db " + ", ".join(f"0x{b:02X}" for b in page[i : i + 16]))
    open(a.out.replace(".c", "_sq.s"), "w").write("\n".join(sqs) + "\n")
    print(f"{a.out}: rays={n} proj_px={proj_px:.1f} sprproj={spr_proj}")


if __name__ == "__main__":
    main()
