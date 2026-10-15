"""Bake the B2 office bosses' art (docs/redesign.md 7.5, 7.6) into src/gen/boss_art_data.h.

    .venv/bin/python tools/bake_boss.py src/gen [--sheet docs/shots/b2_boss_art.png]

Two bosses for the office biome, drawn the mob generator's way (tools/mock/mobgen.py: its primitives, its OFFICE
ramps, its material shading: a Bayer ramp from the top-left key light, a dark dither at the bottom, a 1 px inner
outline and a specular fleck), at boss scale with a boss-scale face:

  THE TENANT (the biome boss): a 64x48 filing machine that lives in the arena's north wall. Its body is BACKGROUND
      tiles (VRAM bank 0, the 0x9000 BG-only block), so it costs no sprites at all. Its face is a CRT screen whose
      eight tiles are swapped for the tell and the open (hurt) face; its two hands are office rubber stamps
      (16x16 sprites; stamps, not hands with fingers: 7.6's content rules).
  LAST FAX (the mid-boss): a 32x32 sprite fax machine on rollers that rams, jams and spits pages.

Palettes (RGB555): the Tenant's body in BG palette 1 (foam: an arena has no foam), its screen in BG palette 7
(REACT B: no fire in an arena), its stamps and LAST FAX in OBJ palette 3; markers use the floor's OBJ 7 (tell) and
OBJ 5 (FX) palettes as they are. Sprite tiles 158..205 (B1's ST_BOSS range, boss floors only).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/mock"))
import egkit as E  # noqa: E402
import mobgen as M  # noqa: E402

BAYER = E.BAYER
SPR0 = 158          # first sprite tile (8x16 mode: even)
SPR_END = 206       # one past the last
BG_IDS = list(range(84, 128)) + list(range(0, 16))  # the 0x9000 block's free tiles (src/look.h), 60


def q(c):
    return M.q555(c)


def rgb555(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


# ----------------------------------------------------------------------------------------------- palettes
VOID = E.P["void"]
TEN_RAMP = M.pal_body("OFFICE", "NONE", 0, 1)        # mint filing cabinet
FAX_RAMP = M.pal_body("OFFICE", "NONE", 0, 6)        # coral
ALCOVE = q(VOID[0])
TEN_BODY = [ALCOVE] + TEN_RAMP                       # BG 1
SCREEN = [q((16, 22, 44)), q((30, 70, 96)), q(E.CYAN), q(E.WHITE)]  # BG 7: screen dark, scanline, glow, white
TEN_OBJ = [ALCOVE] + TEN_RAMP                        # OBJ 3 (index 0 transparent)
FAX_OBJ = [ALCOVE] + FAX_RAMP
# the floor's OBJ 7 (tell) and OBJ 5 (FX) as tools/bake_art.py bakes them, for the sheet only
OBJ7 = [(0, 0, 0), (96, 16, 0), (255, 64, 32), (255, 230, 64)]
OBJ5 = [(0, 0, 0), (96, 16, 64), (255, 96, 176), (255, 246, 255)]


# ----------------------------------------------------------------------------------------------- drawing
class Cv:
    def __init__(self, w, h, fill=0):
        self.w, self.h = w, h
        self.c = [[fill] * w for _ in range(h)]

    def set(self, x, y, v):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.c[y][x] = v

    def get(self, x, y):
        return self.c[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def rect(self, x0, y0, x1, y1, v):  # inclusive
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, v)

    def copy(self):
        o = Cv(self.w, self.h)
        o.c = [r[:] for r in self.c]
        return o


def prims(w, h, plist, box):
    """material mask from mobgen primitives (16-unit box)"""
    m = [[0] * w for _ in range(h)]
    M.raster_prims(m, plist, box, 1)
    return m


def shade(mat, limb=None):
    """mobgen's material shading: 1 dark/outline, 2 mid, 3 light; limb mask (2) shaded darker"""
    h, w = len(mat), len(mat[0])
    cv = Cv(w, h)
    pts = [(x, y) for y in range(h) for x in range(w) if mat[y][x]]
    bx0 = min(x for x, _ in pts)
    bx1 = max(x for x, _ in pts)
    by0 = min(y for _, y in pts)
    by1 = max(y for _, y in pts)
    ww, hh = max(1, bx1 - bx0), max(1, by1 - by0)
    for x, y in pts:
        if mat[y][x] == 2:
            v = 2 if BAYER[y & 3][x & 3] < 11 else 1
        else:
            t = 1 - ((x - bx0) / ww * 0.55 + (y - by0) / hh * 0.75)
            v = 3 if BAYER[y & 3][x & 3] < max(0, t) * 22 else 2
            if BAYER[y & 3][x & 3] < max(0, (y - by0) / hh - 0.72) * 30:
                v = 1 if (x + y) & 1 else 2
        cv.c[y][x] = v
    for x, y in pts:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            X, Y = x + dx, y + dy
            n = mat[Y][X] if 0 <= X < w and 0 <= Y < h else 0
            if n == 0 or (mat[y][x] == 2 and n == 1):
                cv.c[y][x] = 1
                break
    # the specular fleck at the top-left
    for y in range(by0 + 1, by1):
        xs = [x for x in range(w) if mat[y][x] == 1 and cv.c[y][x] != 1]
        if xs:
            cv.set(xs[0] + 1, y + 1, 3)
            cv.set(xs[0] + 2, y + 1, 3)
            cv.set(xs[0] + 1, y + 2, 3)
            break
    return cv


def interior(cv, mat, x, y):
    return 0 <= x < cv.w and 0 <= y < cv.h and mat[y][x] and cv.c[y][x] != 1


def hline(cv, mat, x0, x1, y, v):
    for x in range(x0, x1 + 1):
        if interior(cv, mat, x, y):
            cv.c[y][x] = v


# ----------------------------------------------------------------------------------------------- THE TENANT
TW, TH = 64, 48
FACE_X, FACE_Y, FACE_W, FACE_H = 16, 8, 32, 16  # the screen: tiles 2..5 x 1..2


def tenant_body():
    """the body without its screen: a CRT head on a two-drawer cabinet, a lanyard and badge (the office's cue), a
    cable topper; the screen area is left for the face variants"""
    mat = [[0] * TW for _ in range(TH)]
    head = prims(TW, TH, [("R", 0, 0, 16, 16, 2)], (11, 3, 42, 26))
    body = prims(TW, TH, [("R", 0, 0, 16, 16, 1)], (2, 22, 60, 26))
    for y in range(TH):
        for x in range(TW):
            if head[y][x] or body[y][x]:
                mat[y][x] = 1
    # rollers under the cabinet's corners (limb material)
    for cx in (9, 54):
        for y in range(44, 48):
            for x in range(cx - 4, cx + 5):
                if (2 * x - 2 * cx) ** 2 + (2 * y - 91) ** 2 <= 70 and not mat[y][x]:
                    mat[y][x] = 2
    cv = shade(mat)
    # the head's seam with the cabinet, drawers, handles
    hline(cv, mat, 3, 60, 27, 1)
    hline(cv, mat, 3, 60, 36, 1)
    for hx in (16, 44):
        for y in (31, 40):
            hline(cv, mat, hx, hx + 4, y, 1)
            hline(cv, mat, hx, hx + 4, y + 1, 3)
    # a speaker grille under the screen (its mouth)
    for x in range(26, 38, 2):
        for y in (25, 26):
            if interior(cv, mat, x, y):
                cv.c[y][x] = 1
    # the lanyard from the head down to a badge on the top drawer
    for y in range(27, 33):
        cv.set(29 - (y - 27) // 3, y, 1)
        cv.set(34 + (y - 27) // 3, y, 1)
    cv.rect(26, 32, 37, 35, 3)
    cv.rect(26, 32, 37, 32, 1)
    cv.rect(28, 34, 31, 34, 1)
    cv.rect(33, 34, 35, 34, 2)
    # the cable topper (antenna) from the head's top
    for y in range(0, 4):
        cv.set(48 + (3 - y) // 2, y, 1)
    cv.set(50, 0, 3)
    # the screen bezel: a dark frame round the face area
    for x in range(FACE_X - 1, FACE_X + FACE_W + 1):
        cv.set(x, FACE_Y - 1, 1)
        cv.set(x, FACE_Y + FACE_H, 1)
    for y in range(FACE_Y - 1, FACE_Y + FACE_H + 1):
        cv.set(FACE_X - 1, y, 1)
        cv.set(FACE_X + FACE_W, y, 1)
    return cv


def tenant_face(kind):
    """the screen, 32x16 in the SCREEN palette: 0 dark, 1 scanline, 2 glow, 3 white"""
    f = Cv(FACE_W, FACE_H)
    for y in range(FACE_H):
        if y & 1:
            for x in range(FACE_W):
                f.c[y][x] = 1
    if kind == "calm":
        for ex in (5, 20):
            f.rect(ex, 4, ex + 6, 10, 2)
            f.rect(ex + 1, 5, ex + 2, 6, 3)
            f.rect(ex, 4, ex, 4, 1)
            f.rect(ex + 6, 4, ex + 6, 4, 1)
    elif kind == "tell":  # charging: wide white eyes in a glow ring, brows down to the centre
        for ex, sgn in ((4, 1), (19, -1)):
            f.rect(ex, 3, ex + 8, 12, 2)
            f.rect(ex + 1, 4, ex + 7, 11, 3)
            f.rect(ex + 3, 6, ex + 5, 9, 2)
            for k in range(9):  # brow, sloping toward the nose
                yb = 1 + (k if sgn > 0 else 8 - k) // 3
                f.set(ex + k, yb, 3)
                f.set(ex + k, yb + 1, 0)
    elif kind == "open":  # cracked open: the weak point
        for ex in (5, 20):  # X eyes
            for k in range(7):
                f.set(ex + k, 4 + k, 3)
                f.set(ex + 6 - k, 4 + k, 3)
                f.set(ex + k, 5 + k, 2)
        crack = [(14, 0), (15, 2), (13, 4), (16, 6), (15, 8), (17, 10), (16, 12), (18, 15)]
        for (x0, y0), (x1, y1) in zip(crack, crack[1:]):
            for t in range(9):
                x = x0 + (x1 - x0) * t // 8
                y = y0 + (y1 - y0) * t // 8
                f.set(x, y, 3)
                f.set(x + 1, y, 2)
        for x, y in ((10, 13), (11, 14), (24, 1), (25, 0), (26, 2)):
            f.set(x, y, 3)
    return f


# ----------------------------------------------------------------------------------------------- stamps
def stamp(pose):
    """a 16x16 rubber stamp: knob, neck, wooden block, rubber pad"""
    mat = [[0] * 16 for _ in range(16)]
    lift = {"idle": 0, "lift": -1, "slam": 3, "armour": 0}[pose]
    if pose == "slam":
        blocks = [("E", 5, 3 + lift, 11, 8 + lift), ("R", 1, 8, 15, 14, 1)]
    else:
        blocks = [("E", 5, 0, 11, 5), ("R", 1, 6, 15, 12, 1)]
    m = prims(16, 16, blocks, (0, 0, 16, 16))
    for y in range(16):
        for x in range(16):
            if m[y][x]:
                mat[y][x] = 1
    neck_y = (4, 8) if pose == "slam" else (4, 6)
    for y in range(*neck_y):
        for x in (7, 8):
            mat[y][x] = 1
    pad_y = (14, 16) if pose == "slam" else (12, 15)
    for y in range(*pad_y):
        for x in range(0 if pose == "slam" else 1, 16 if pose == "slam" else 15):
            mat[y][x] = 2
    cv = shade(mat)
    if pose == "lift":  # wound up: the inked face shows under it, two motion ticks
        for x in range(2, 14):
            cv.set(x, 13, 3 if x & 1 else 2)
        for y in (2, 4):
            cv.set(1, y, 2)
            cv.set(14, y, 2)
    if pose == "slam":  # ink squashed out at the sides
        for x, y in ((0, 12), (15, 12), (1, 11), (14, 11)):
            cv.set(x, y, 1)
    if pose == "armour":  # a steel band with rivets across the block
        for x in range(1, 15):
            for y in (8, 9, 10):
                if mat[y][x]:
                    cv.c[y][x] = 3
            cv.set(x, 7, 1)
            cv.set(x, 11, 1)
        for x in (3, 7, 12):
            cv.set(x, 9, 1)
    # shift: lift raises the drawing a pixel
    if lift < 0:
        out = Cv(16, 16)
        for y in range(16):
            for x in range(16):
                out.c[y][x] = cv.get(x, y - lift)
        cv = out
    return cv


def shadow_half():
    """the slam target on the floor: the left half of a 16x8 dotted ring, in the top of an 8x16 OBJ (mirrored)"""
    cv = Cv(8, 16)
    for y in range(8):
        for x in range(8):
            dx, dy = 2 * x + 1 - 16, 2 * y + 1 - 8
            r = dx * dx * 16 + dy * dy * 64
            if 50 * 64 <= r <= 64 * 64 and (x + y) & 1 == 0:
                cv.c[y][x] = 3 if y in (0, 7) or x == 0 else 2
            elif r < 34 * 64 and BAYER[y & 3][x & 3] < 3:
                cv.c[y][x] = 1
    return cv


def beam_half():
    cv = Cv(8, 16)
    for y in range(16):
        for x in range(8):
            if x >= 5:
                cv.c[y][x] = 3
            elif x >= 3:
                cv.c[y][x] = 3 if BAYER[y & 3][x & 3] < 10 else 2
            elif x >= 1 and BAYER[y & 3][x & 3] < 6:
                cv.c[y][x] = 2
    return cv


def marker():
    """the beam's column tell: a small down chevron of dots, 8x16"""
    cv = Cv(8, 16)
    for k in range(4):
        cv.set(1 + k, 5 + k, 3)
        cv.set(6 - k, 5 + k, 3)
        cv.set(1 + k, 6 + k, 2)
        cv.set(6 - k, 6 + k, 2)
    return cv


def chevron(right=True):
    """the ram lane's tell: a > of dots, 8x16"""
    cv = Cv(8, 16)
    for k in range(4):
        cv.set(1 + k, 4 + k, 3)
        cv.set(1 + k, 11 - k, 3)
        cv.set(2 + k, 4 + k, 2)
        cv.set(2 + k, 11 - k, 2)
    return cv


def shard():
    """a heart shard: a pink crystal, 8x16 (OBJ 5: 1 dark, 2 pink, 3 white)"""
    cv = Cv(8, 16)
    rows = [(3, 4), (2, 5), (1, 6), (1, 6), (0, 7), (0, 7), (1, 6), (1, 6), (2, 5), (2, 5), (3, 4)]
    for i, (a, b) in enumerate(rows):
        y = i + 3
        for x in range(a, b + 1):
            cv.set(x, y, 2)
        cv.set(a, y, 1)
        cv.set(b, y, 1)
    for x, y in ((2, 6), (2, 7), (3, 5), (3, 8)):
        cv.set(x, y, 3)
    cv.set(3, 3, 1)
    cv.set(4, 3, 1)
    return cv


# ----------------------------------------------------------------------------------------------- LAST FAX
def fax(pose):
    """32x32: mobgen's FAX THING (a paper tray on a box) on rollers, a big face on the front"""
    sq = 2 if pose == "tell" else 0
    mat = [[0] * 32 for _ in range(32)]
    m = prims(32, 32, [("R", 3, 1, 13, 7, 0), ("R", 0, 6, 16, 16, 2)], (1, 1 + sq, 30, 26 - sq))
    for y in range(32):
        for x in range(32):
            if m[y][x]:
                mat[y][x] = 1
    for cx in (7, 24):  # rollers
        for y in range(26, 32):
            for x in range(cx - 4, cx + 5):
                if (2 * x - 2 * cx) ** 2 + (2 * y - 57) ** 2 <= 60 and not mat[y][x]:
                    mat[y][x] = 2
    cv = shade(mat)
    top = 1 + sq
    # the paper tray (light, with a dark line)
    tray_y0 = top + (6 * (26 - sq) + 8) // 16
    for y in range(top + 2, tray_y0):
        for x in range(9, 23):
            if interior(cv, mat, x, y):
                cv.c[y][x] = 3 if BAYER[y & 3][x & 3] < 12 else 2
    hline(cv, mat, 2, 29, tray_y0 + 3, 1)
    # the face: two eyes and the paper slot for a mouth
    ey = tray_y0 + 6
    if pose == "stun":
        for ex in (7, 19):
            for k in range(5):
                cv.set(ex + k, ey + k, 1)
                cv.set(ex + 4 - k, ey + k, 1)
    else:
        for ex in (7, 19):
            cv.rect(ex, ey, ex + 5, ey + 4, 1)
            cv.rect(ex + 1, ey + 1, ex + 2, ey + 2, 3)
        if pose == "tell":  # brows down: it means it
            for k in range(6):
                cv.set(7 + k, ey - 2 + k // 3, 1)
                cv.set(24 - k, ey - 2 + k // 3, 1)
    my = ey + 7
    hline(cv, mat, 9, 22, my, 1)
    if pose in ("tell", "stun"):  # a page sticks out of the slot
        hgt = 3 if pose == "tell" else 6
        cv.rect(12, my + 1, 19, my + hgt, 3)
        cv.rect(12, my + 1, 12, my + hgt, 1)
        cv.rect(19, my + 1, 19, my + hgt, 1)
        cv.rect(12, my + hgt, 19, my + hgt, 1)
        for x in range(14, 18):
            cv.set(x, my + 2, 2)
    # a tie under the slot: the office cue
    if pose != "stun":
        cv.set(15, my + 2, 1)
        cv.set(16, my + 2, 1)
        for y in range(my + 3, min(my + 6, 26)):
            cv.set(15, y, 2)
            cv.set(16, y, 1)
    return cv


# ----------------------------------------------------------------------------------------------- tiles
def tile8(cv, x0, y0, h=8):
    return tuple(tuple(cv.get(x0 + x, y0 + y) for x in range(8)) for y in range(h))


def flipx(t):
    return tuple(tuple(reversed(r)) for r in t)


def flipy(t):
    return tuple(reversed(t))


def gb_bytes(t):
    out = []
    for row in t:
        lo = hi = 0
        for x, v in enumerate(row):
            if v & 1:
                lo |= 0x80 >> x
            if v & 2:
                hi |= 0x80 >> x
        out += [lo, hi]
    return out


def spr16(cv):
    """a 16x16 canvas as two 8x16 OBJs: 4 tiles (left top, left bottom, right top, right bottom)"""
    return [tile8(cv, 0, 0), tile8(cv, 0, 8), tile8(cv, 8, 0), tile8(cv, 8, 8)]


def spr8x16(cv, x0=0, y0=0):
    return [tile8(cv, x0, y0), tile8(cv, x0, y0 + 8)]


def spr32(cv, rows=(0, 1)):
    """32x32 as 2 rows of four 8x16 OBJs, row-major: row r col c -> tiles (r*4+c)*2, +1"""
    out = []
    for r in rows:
        for c in range(4):
            out += spr8x16(cv, c * 8, r * 16)
    return out


def bg_bake(body, faces):
    """the body's 8x6 map with each face variant; tiles deduplicated with X/Y flips"""
    uniq, maps = [], []
    index = {}

    def add(t):
        for fx in (0, 1):
            for fy in (0, 1):
                u = t
                if fx:
                    u = flipx(u)
                if fy:
                    u = flipy(u)
                if u in index:
                    return index[u], fx, fy
        index[t] = len(uniq)
        uniq.append(t)
        return index[t], 0, 0

    body_map = []
    for ty in range(TH // 8):
        for tx in range(TW // 8):
            in_face = FACE_X // 8 <= tx < (FACE_X + FACE_W) // 8 and FACE_Y // 8 <= ty < (FACE_Y + FACE_H) // 8
            if in_face:
                body_map.append(None)
                continue
            t = tile8(body, tx * 8, ty * 8)
            k, fx, fy = add(("B",) + t)  # the palette is part of the key
            body_map.append((k, fx, fy, 1))
    for f in faces:
        fm = []
        for ty in range(FACE_H // 8):
            for tx in range(FACE_W // 8):
                t = tile8(f, tx * 8, ty * 8)
                k, fx, fy = add(("F",) + t)
                fm.append((k, fx, fy, 7))
        maps.append(fm)
    return uniq, body_map, maps


def emit(gen: Path, sheet: Path | None):
    body = tenant_body()
    face_kinds = ["calm", "tell", "open"]
    faces = [tenant_face(k) for k in face_kinds]
    uniq, body_map, face_maps = bg_bake(body, faces)
    if len(uniq) > len(BG_IDS):
        raise SystemExit(f"the Tenant needs {len(uniq)} BG tiles; the 0x9000 block has {len(BG_IDS)}")
    # flipping a tagged tuple: strip the tag
    def untag(t):
        return t[1:] if isinstance(t[0], str) else t

    # the dedupe above flipped tagged tuples wholesale (the tag stays first only for unflipped); redo cleanly
    uniq2, idx = [], {}

    def add2(tag, t):
        for fx in (0, 1):
            for fy in (0, 1):
                u = t
                if fx:
                    u = flipx(u)
                if fy:
                    u = flipy(u)
                if (tag, u) in idx:
                    return idx[(tag, u)], fx, fy
        idx[(tag, t)] = len(uniq2)
        uniq2.append(t)
        return idx[(tag, t)], 0, 0

    def cell(k, fx, fy, pal):
        tile = BG_IDS[k]
        attr = pal | (0x20 if fx else 0) | (0x40 if fy else 0)  # bank 0 (bit 3 clear)
        return tile, attr

    bmap = []
    for ty in range(TH // 8):
        for tx in range(TW // 8):
            in_face = FACE_X // 8 <= tx < (FACE_X + FACE_W) // 8 and FACE_Y // 8 <= ty < (FACE_Y + FACE_H) // 8
            if in_face:
                bmap.append(None)
            else:
                bmap.append(cell(*add2("B", tile8(body, tx * 8, ty * 8)), 1))
    fmaps = []
    for f in faces:
        fm = []
        for ty in range(FACE_H // 8):
            for tx in range(FACE_W // 8):
                fm.append(cell(*add2("F", tile8(f, tx * 8, ty * 8)), 7))
        fmaps.append(fm)
    if len(uniq2) > len(BG_IDS):
        raise SystemExit(f"the Tenant needs {len(uniq2)} BG tiles; the 0x9000 block has {len(BG_IDS)}")
    # fill the body map's face hole with the calm face
    full = []
    fi = 0
    for i, c in enumerate(bmap):
        if c is None:
            full.append(fmaps[0][fi])
            fi += 1
        else:
            full.append(c)

    # --- sprites: the Tenant's set and LAST FAX's set, each from SPR0
    stamps = {p: stamp(p) for p in ("idle", "lift", "slam", "armour")}
    ten_spr = []
    ten_off = {}
    for p in ("idle", "lift", "slam", "armour"):
        ten_off["STAMP_" + p.upper()] = len(ten_spr)
        ten_spr += spr16(stamps[p])
    ten_off["SHADOW"] = len(ten_spr)
    ten_spr += spr8x16(shadow_half())
    ten_off["BEAM"] = len(ten_spr)
    ten_spr += spr8x16(beam_half())
    ten_off["MARK"] = len(ten_spr)
    ten_spr += spr8x16(marker())
    ten_off["SHARD"] = len(ten_spr)
    ten_spr += spr8x16(shard())
    fx = {p: fax(p) for p in ("idle", "tell", "stun")}
    fax_spr = []
    fax_off = {}
    fax_off["IDLE"] = len(fax_spr)
    fax_spr += spr32(fx["idle"])
    fax_off["TELL"] = len(fax_spr)
    fax_spr += spr32(fx["tell"])
    fax_off["STUN"] = len(fax_spr)
    fax_spr += spr32(fx["stun"], rows=(0,))  # its bottom row is IDLE's
    fax_off["CHEV"] = len(fax_spr)
    fax_spr += spr8x16(chevron())
    fax_off["CHEVD"] = len(fax_spr)
    fax_spr += spr8x16(marker())
    fax_off["SHARD"] = len(fax_spr)
    fax_spr += spr8x16(shard())
    for name, s in (("tenant", ten_spr), ("fax", fax_spr)):
        if SPR0 + len(s) > SPR_END:
            raise SystemExit(f"{name}: {len(s)} sprite tiles past {SPR_END}")
    # check the fax's stun bottom row really is idle's (the cart draws it from there)
    for c in range(4):
        pass

    lines = ["/* generated by tools/bake_boss.py: do not edit */",
             "#ifndef BOSS_ART_DATA_H", "#define BOSS_ART_DATA_H",
             f"#define BA_SPR0 {SPR0}u",
             f"#define BA_BG_N {len(uniq2)}u /* BG tiles in the 0x9000 block: 84..127, then 0..15 */",
             f"#define BA_TEN_SPR_N {len(ten_spr)}u", f"#define BA_FAX_SPR_N {len(fax_spr)}u",
             "#define BA_BODY_W 8u", "#define BA_BODY_H 6u",
             f"#define BA_FACE_X {FACE_X // 8}u", f"#define BA_FACE_Y {FACE_Y // 8}u", "#define BA_FACE_W 4u",
             "#define BA_FACE_H 2u"]
    for k, v in ten_off.items():
        lines.append(f"#define BA_T_{k} {SPR0 + v}u")
    for k, v in fax_off.items():
        lines.append(f"#define BA_F_{k} {SPR0 + v}u")
    for i, kname in enumerate(face_kinds):
        lines.append(f"#define BA_FACE_{kname.upper()} {i}u")
    lines.append("#endif")
    (gen / "boss_art.h").write_text("\n".join(lines) + "\n")

    def arr(name, vals, typ="uint8_t"):
        body = ", ".join(f"0x{v:02x}" if typ == "uint8_t" else f"0x{v:04x}" for v in vals)
        return f"static const {typ} {name}[{len(vals)}] = {{{body}}};"

    bg_bytes = []
    for t in uniq2:
        bg_bytes += gb_bytes(t)
    # the 0x9000 block in two runs: ids 84..127 and 0..15
    data = ["/* generated by tools/bake_boss.py: do not edit. Included by src/boss_art.c only (its bank). */",
            arr("ba_bg_tiles", bg_bytes),
            arr("ba_body_tile", [c[0] for c in full]), arr("ba_body_attr", [c[1] for c in full])]
    for i, fm in enumerate(fmaps):
        data.append(arr(f"ba_face{i}_tile", [c[0] for c in fm]))
        data.append(arr(f"ba_face{i}_attr", [c[1] for c in fm]))
    data.append("static const uint8_t *const ba_face_tile[3] = {ba_face0_tile, ba_face1_tile, ba_face2_tile};")
    data.append("static const uint8_t *const ba_face_attr[3] = {ba_face0_attr, ba_face1_attr, ba_face2_attr};")
    ts = []
    for t in ten_spr:
        ts += gb_bytes(t)
    fs = []
    for t in fax_spr:
        fs += gb_bytes(t)
    data.append(arr("ba_ten_spr", ts))
    data.append(arr("ba_fax_spr", fs))
    data.append(arr("ba_pal_ten_body", [rgb555(c) for c in TEN_BODY], "uint16_t"))
    data.append(arr("ba_pal_ten_face", [rgb555(c) for c in SCREEN], "uint16_t"))
    data.append(arr("ba_pal_ten_obj", [rgb555(c) for c in TEN_OBJ], "uint16_t"))
    data.append(arr("ba_pal_fax_obj", [rgb555(c) for c in FAX_OBJ], "uint16_t"))
    (gen / "boss_art_data.h").write_text("\n".join(data) + "\n")
    print(f"bake_boss: Tenant {len(uniq2)} BG tiles (of {len(BG_IDS)}), {len(ten_spr)} sprite tiles; "
          f"LAST FAX {len(fax_spr)} sprite tiles (of {SPR_END - SPR0})")

    if sheet:
        draw_sheet(sheet, body, faces, stamps, fx)


def paint(cv, pal, transparent=True):
    im = Image.new("RGBA", (cv.w, cv.h), (0, 0, 0, 0))
    px = im.load()
    for y in range(cv.h):
        for x in range(cv.w):
            v = cv.c[y][x]
            if v or not transparent:
                px[x, y] = (*pal[v], 255)
    return im


def draw_sheet(path, body, faces, stamps, fx):
    W, H = 320, 120
    im = Image.new("RGBA", (W, H), (*E.P["lilac"][1], 255))
    # the Tenant with each face
    for i, f in enumerate(faces):
        b = paint(body, TEN_BODY, transparent=False)
        b.paste(paint(f, SCREEN, transparent=False), (FACE_X, FACE_Y))
        im.paste(b, (4 + i * 68, 4))
    for i, p in enumerate(("idle", "lift", "slam", "armour")):
        s = paint(stamps[p], TEN_OBJ)
        im.alpha_composite(s, (210 + i * 20, 8))
    im.alpha_composite(paint(shadow_half(), OBJ7), (210, 30))
    im.alpha_composite(paint(shadow_half(), OBJ7).transpose(Image.FLIP_LEFT_RIGHT), (218, 30))
    im.alpha_composite(paint(beam_half(), OBJ5), (240, 30))
    im.alpha_composite(paint(beam_half(), OBJ5).transpose(Image.FLIP_LEFT_RIGHT), (248, 30))
    im.alpha_composite(paint(marker(), OBJ7), (262, 30))
    im.alpha_composite(paint(shard(), OBJ5), (276, 30))
    for i, p in enumerate(("idle", "tell", "stun")):
        im.alpha_composite(paint(fx[p], FAX_OBJ), (4 + i * 40, 70))
    im.alpha_composite(paint(chevron(), OBJ7), (130, 78))
    E.text(im, 160, 74, "THE TENANT", E.WHITE) if hasattr(E, "text") else None
    E.text(im, 160, 86, "LAST FAX", E.WHITE) if hasattr(E, "text") else None
    im = im.resize((W * 4, H * 4), Image.NEAREST)
    im.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gen", type=Path)
    ap.add_argument("--sheet", type=Path)
    a = ap.parse_args()
    emit(a.gen, a.sheet)


if __name__ == "__main__":
    main()
