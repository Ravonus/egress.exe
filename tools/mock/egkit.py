"""EGRESS's own mock kit: its own font (from tools/bake_art.py), its own dithered tiles, objects, HUD and frames.

Same family as CRUCIBLE (pastel, synthwave, KEEL dithered pixel art), but nothing here is cut from CRUCIBLE.
Faces come from the ported avatar generator (shared code, build/host/avatar_dump).
"""
from __future__ import annotations

import math
import random
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import avatar_png as AP  # noqa: E402

AP.DUMP = ROOT / "build/host/avatar_dump"

# ---- palettes (EGRESS's own, 4 colours each: shadow, base, light, highlight) ------------------------------------
P = {
    "void": [(14, 10, 34), (34, 20, 70), (70, 40, 120), (236, 200, 255)],
    "lilac": [(84, 52, 132), (178, 140, 222), (214, 190, 246), (250, 242, 255)],
    "peach": [(128, 70, 96), (230, 162, 150), (248, 204, 182), (255, 242, 226)],
    "teal": [(22, 76, 98), (84, 170, 176), (150, 218, 212), (230, 255, 246)],
    "mint": [(40, 104, 92), (124, 206, 168), (186, 238, 206), (240, 255, 244)],
    "water": [(30, 50, 130), (66, 120, 220), (130, 188, 248), (232, 248, 255)],
    "wall": [(40, 28, 84), (96, 66, 160), (150, 116, 210), (230, 214, 255)],
    "hud": [(20, 14, 46), (58, 36, 108), (255, 112, 200), (240, 236, 255)],
}
CYAN, PINK, YEL, WHITE, INK = (64, 224, 248), (255, 112, 200), (255, 224, 96), (250, 246, 255), (20, 14, 46)
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def dith(a, b, t, x, y):
    """t in 0..1: fraction of b, ordered 4x4 dither"""
    return b if BAYER[y & 3][x & 3] < t * 16 else a


# ---- font: EGRESS's own glyphs, parsed from tools/bake_art.py --------------------------------------------------
_src = (ROOT / "tools/bake_art.py").read_text()
_m = re.search(r"^FONT = (\{.*?^\})", _src, re.S | re.M)
FONT = eval(_m.group(1))  # noqa: S307 (our own source file)


def text(im, x, y, s, col=WHITE, shadow=INK):
    px = im.load()
    for ch in s.upper():
        if ch in FONT:
            rows = FONT[ch]
            w = len(rows[0])
            ox = (8 - w) // 2 if w < 4 else 1
            for yy, row in enumerate(rows):
                for xx, b in enumerate(row):
                    if b == "1":
                        for (dx, dy, c) in ((1, 1, shadow), (0, 0, col)):
                            X, Y = x + ox + xx + dx, y + yy + dy
                            if c and 0 <= X < im.width and 0 <= Y < im.height:
                                px[X, Y] = c
        x += 8


def heart(im, x, y, full=True):
    m = ["01100110", "11111111", "11111111", "11111111", "01111110", "00111100", "00011000"]
    for yy, r in enumerate(m):
        for xx, b in enumerate(r):
            if b == "1":
                im.putpixel((x + xx, y + yy), PINK if full else (90, 60, 130))
    if full:
        im.putpixel((x + 1, y + 1), WHITE)


# ---- tiles -------------------------------------------------------------------------------------------------------
def slab(pal, seed=0, lit=False):
    """16x16 floor slab: dithered diagonal light, bevel, a seeded scuff"""
    s, b, l, h = P[pal]
    t = Image.new("RGB", (16, 16))
    px = t.load()
    r = random.Random(seed)
    for y in range(16):
        for x in range(16):
            g = 1 - (x + y) / 30
            c = dith(b, l, g * 0.7 + (0.25 if lit else 0), x, y)
            px[x, y] = c
    for i in range(16):
        px[i, 0] = h
        px[0, i] = h
        px[i, 15] = s
        px[15, i] = s
    px[15, 0] = l
    px[0, 15] = l
    if r.random() < 0.5:  # scuff / carpet fleck
        x0, y0 = r.randrange(3, 12), r.randrange(3, 12)
        px[x0, y0] = s
        px[x0 + 1, y0] = b
    return t


def underside(im, x, y, pal="void", depth=6):
    s, b, l, h = P["wall"]
    px = im.load()
    for yy in range(depth):
        for xx in range(16):
            if 0 <= x + xx < im.width and 0 <= y + yy < im.height:
                px[x + xx, y + yy] = dith(s, P["void"][0], yy / depth, xx, yy) if yy else b


def wall(im, x, y, pal="wall", h=7):
    s, b, l, hi = P[pal]
    px = im.load()
    for yy in range(h):
        for xx in range(16):
            c = s if xx in (0, 15) else (b if xx % 3 else s)
            if yy == 0:
                c = l
            px[x + xx, y + 16 - h + yy] = c
    top = slab(pal, 7)
    im.paste(top, (x, y - h + 1))


def water(phase=0):
    s, b, l, h = P["water"]
    t = Image.new("RGB", (16, 16), b)
    px = t.load()
    for y in range(16):
        for x in range(16):
            px[x, y] = dith(b, s, y / 40, x, y)
            if (x + phase + (y // 4) * 5) % 8 < 3 and y % 4 == 1:
                px[x, y] = l
    px[(phase * 3) % 16, 6] = h
    return t


def void(seed=1, pal="void", grid=True):
    s, b, l, h = P[pal]
    im = Image.new("RGB", (160, 144))
    px = im.load()
    r = random.Random(seed)
    for y in range(144):
        for x in range(160):
            px[x, y] = dith(s, b, y / 200, x, y)
    if grid:  # faint synthwave grid far below
        for y in range(0, 144, 12):
            for x in range(0, 160, 2):
                if (x // 2 + y) % 3 == 0:
                    px[x, y] = dith(px[x, y], l, 0.35, x, y)
        for x in range(4, 160, 16):
            for y in range(0, 144, 3):
                px[x, y] = dith(px[x, y], l, 0.3, x, y)
    for _ in range(26):
        x, y = r.randrange(160), r.randrange(144)
        px[x, y] = h if r.random() < 0.4 else l
    return im


# ---- objects (16x16 templates, palette index 1..3, '.' clear) ---------------------------------------------------
OBJ = {
    "FIRE": ("""
.......3........
......323.......
.....32223......
....3221223.....
....3211123.....
...321111123....
...321111123....
....32111223....
.....322223.....
...1111111111...
..122222222221..
..133333333331..
...1111111111...
....1......1....
....1......1....
................""", [(110, 30, 60), (255, 128, 64), (255, 226, 110)]),
    "WATER": ("""
......1111......
......1331......
......1221......
....11122111....
...1333333331...
...1332222231...
...1322222221...
...1322222221...
...1322222221...
...1322222221...
...1232222221...
...1222222221...
....11111111....
....13333331....
....11111111....
................""", [(30, 50, 130), (90, 150, 236), (200, 236, 255)]),
    "CRATE": ("""
................
.11111111111111.
.12222222222221.
.12133313331221.
.12313131313121.
.12331113111321.
.12313131313121.
.12133313331221.
.12222222222221.
.12133313331221.
.12313131313121.
.12331113111321.
.12313131313121.
.12222222222221.
.11111111111111.
................""", [(70, 36, 40), (170, 100, 70), (226, 170, 118)]),
    "MUG": ("""
................
................
.....3..3.......
......3..3......
.....3..3.......
...11111111.....
...12222221111..
...13333331..1..
...13333331..1..
...13333331111..
...12222221.....
...12222221.....
....111111......
................
................
................""", [(40, 24, 60), (250, 240, 255), (120, 70, 50)]),
    "STEAM": ("""
................
.......33.......
.....333333.....
....33322333....
...3322222333...
..332222222233..
..322211112223..
.3322111111223..
.33222111122233.
..3322222222233.
...33322223333..
.....333333.....
.......2..2.....
......2..2......
.......2..2.....
................""", [(150, 130, 200), (214, 200, 250), (250, 246, 255)]),
    "SPARK": ("""
................
....11111111....
....12222221....
...1123333211...
...1222332221...
...1222232221...
...1223333221...
...1222232221...
...1222322221...
...1223222221...
...1222222221...
...1222222221...
...1111111111...
....1......1....
................
................""", [(30, 30, 70), (96, 110, 170), (255, 236, 110)]),
    "CRT": ("""
................
..111111111111..
..122222222221..
..123333333321..
..123131313321..
..123333333321..
..123313133321..
..123333333321..
..122222222221..
..111111111111..
......1221......
....11111111....
....12222221....
....11111111....
................
................""", [(30, 22, 60), (170, 150, 210), (90, 230, 240)]),
    "SEED": ("""
................
................
.......33.......
......3223......
.....322223.....
......3223......
.......11.......
.......11.......
....3..11..3....
.....3.11.3.....
......3113......
.......11.......
.....111111.....
....12222221....
.....111111.....
................""", [(40, 90, 70), (150, 100, 70), (140, 230, 150)]),
}


def obj(name):
    tmpl, pal = OBJ[name]
    rows = tmpl.strip("\n").split("\n")
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c in "123":
                im.putpixel((x, y), (*pal[int(c) - 1], 255))
    return im


def put(im, t, x, y):
    if t.mode == "RGBA":
        im.paste(t, (x, y), t)
    else:
        im.paste(t, (x, y))


# ---- people (shared face generator) -----------------------------------------------------------------------------
def face(fac, seed, bg=None):
    h, p = AP.faces(["seed", str(fac), str(seed)])[0]
    return AP.face_image(h, p, bg), [AP.rgb(c) for c in p]


def small(img, f=2):
    w, h = img.size
    out = Image.new("RGBA", (w // f, h // f), (0, 0, 0, 0))
    src = img.load()
    po = out.load()
    for y in range(h // f):
        for x in range(w // f):
            cs = [src[x * f + i, y * f + j] for j in range(f) for i in range(f) if src[x * f + i, y * f + j][3]]
            if len(cs) >= 2:
                po[x, y] = min(cs, key=lambda c: sum(c[:3]))
    return out


PLAYER = """
......1111......
....11111111....
...1111111111...
..111111111111..
..112222222211..
..122222222221..
..121222222121..
..122222222221..
...1222222221...
....11222211....
....13333331....
...1333333331...
..122333333221..
..121333333121..
...1133333311...
....13311331....
....1331.1331...
.....11...11...."""


def player(fac=2, seed=13, cloth=(64, 120, 232)):
    _, p = face(fac, seed)
    pal = [None, p[1], p[2], cloth]
    rows = PLAYER.strip("\n").split("\n")
    im = Image.new("RGBA", (16, len(rows)), (0, 0, 0, 0))
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c in "123":
                im.putpixel((x, y), (*pal[int(c)], 255))
    return im


# ---- EGRESS UI: rounded dithered panels with a neon rim (not CRUCIBLE's slot frames) ----------------------------
def panel(im, x0, y0, x1, y1, top=(58, 36, 108), bot=(20, 14, 46), rim=CYAN, under=PINK):
    px = im.load()
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            corner = (x - x0 < 2 and y - y0 < 2 and (x - x0) + (y - y0) < 2) or (
                x1 - x < 2 and y - y0 < 2 and (x1 - x) + (y - y0) < 2
            ) or (x - x0 < 2 and y1 - y < 2 and (x - x0) + (y1 - y) < 2) or (
                x1 - x < 2 and y1 - y < 2 and (x1 - x) + (y1 - y) < 2
            )
            if corner:
                continue
            px[x, y] = dith(top, bot, (y - y0) / max(1, y1 - y0), x, y)
    d = ImageDraw.Draw(im)
    d.line((x0 + 2, y0, x1 - 2, y0), fill=rim)
    d.line((x0 + 2, y0 + 1, x1 - 2, y0 + 1), fill=under)


def socket(im, x, y, size=24, glow=False):
    """item socket: a dashed rounded square (lit cyan when it holds a result)"""
    d = ImageDraw.Draw(im)
    col = CYAN if glow else (150, 116, 210)
    for i in range(2, size - 2):
        if i % 3 != 2:
            d.point((x + i, y), fill=col)
            d.point((x + i, y + size - 1), fill=col)
            d.point((x, y + i), fill=col)
            d.point((x + size - 1, y + i), fill=col)
    if glow:
        for (dx, dy) in ((-2, size // 2), (size + 1, size // 2), (size // 2, -2), (size // 2, size + 1)):
            d.point((x + dx, y + dy), fill=WHITE)


def chip(im, x, y, label, col):
    w = len(label) * 8 + 4
    d = ImageDraw.Draw(im)
    d.rectangle((x + 1, y, x + w - 2, y + 9), fill=col)
    d.line((x, y + 1, x, y + 8), fill=col)
    d.line((x + w - 1, y + 1, x + w - 1, y + 8), fill=col)
    text(im, x + 2, y + 1, label, INK, None)


def hud(im, hearts=3, maxh=4, pocket=(), tokens=0, msg=None):
    """EGRESS HUD: a slim bar at the bottom: hearts, pocket icons (8px), token count"""
    panel(im, 0, 128, 159, 143, top=(40, 26, 86), bot=(20, 14, 46))
    for i in range(maxh):
        heart(im, 3 + i * 9, 133, i < hearts)
    x = 44
    for name in pocket:
        o = obj(name).resize((8, 8), Image.NEAREST) if False else small(obj(name), 2)
        put(im, o, x, 133)
        x += 10
    if msg:
        text(im, 44 if not pocket else x + 2, 133, msg, WHITE)
    tok = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    m = ["00111100", "01222210", "12233221", "12333321", "12233221", "12222221", "01222210", "00111100"]
    for yy, r in enumerate(m):
        for xx, c in enumerate(r):
            if c != "0":
                tok.putpixel((xx, yy), (*[(140, 90, 30), YEL, WHITE][int(c) - 1], 255))
    put(im, tok, 124, 133)
    text(im, 132, 133, f"{tokens:02d}"[-3:], YEL)


def spark(im, x, y, col=WHITE):
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
        if 0 <= x + dx < im.width and 0 <= y + dy < im.height:
            im.putpixel((x + dx, y + dy), col)


def puff(im, cx, cy, r, seed=0):
    rr = random.Random(seed)
    px = im.load()
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if 0 <= x < 160 and 0 <= y < 128:
                d2 = (x - cx) ** 2 + (y - cy) ** 2
                if d2 <= r * r and (x * 3 + y * 5 + rr.randrange(3)) % 4:
                    px[x, y] = WHITE if d2 < (r * 0.55) ** 2 else (214, 200, 250)


def tear(im, y0, y1, dx, x0=0, x1=160):
    seg = im.crop((x0, y0, x1, y1))
    im.paste(seg, (x0 + dx, y0))


def darken(t, f=0.55):
    return t.point(lambda v: int(v * f))


def quarters(t):
    return [t.crop((0, 0, 8, 8)), t.crop((8, 0, 16, 8)), t.crop((0, 8, 8, 16)), t.crop((8, 8, 16, 16))]


def floor(im, cells, pal, seed=0, edge_rows=None):
    """cells: iterable of (x, y) pixel origins"""
    cs = set(cells)
    for (x, y) in cells:
        put(im, slab(pal, seed + x * 7 + y * 13), x, y)
        if (x, y + 16) not in cs:
            underside(im, x, y + 16)
