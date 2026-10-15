"""EGRESS sky event scenes mock (docs/redesign.md, "Sky event scenes"). Design-stage only: nothing here is game code.

    .venv/bin/python tools/mock/skyscene_mock.py      # -> docs/shots/skyscene_eclipse.gif, skyscene_retrograde.gif,
                                                      #    skyscene_meteors.gif, skyscene_eclipse_variants.png,
                                                      #    skyscene_report.json, and prints the cost table

Every scene is authored the way the cartridge would hold it:
- the BG is authored in index space: one palette id per 8x8 tile (the CGB attribute map) and a 2-bit colour index per
  pixel; the light, the dark, the corona, the stars and the street lights are palette writes over a fixed map;
- moving things that change shape (trails, meteor streaks, the VHS noise band) are tile writes, counted per frame;
- moving solid things (the moon, the planet glyph, the heir, OSD text) are 8x16 sprites, 3 colours + transparent,
  with the worst scanline counted;
- the caption is the window layer, legalised against its two palettes (the font is the game's own font tiles);
- the BG palette set is checked at 8 x 4 colours, and per-line SCX tears are line shifts of the BG only.
Event times are real (proto/sky/out/events.json and proto/skyscenes/out/meteors.json), shown at a sample local
offset of UTC-5 (the cartridge uses the player's RTC offset).
"""
from __future__ import annotations

import json
import math
import random
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import egkit as E  # noqa: E402
from egkit import BAYER, CYAN, INK, PINK, WHITE, YEL, text  # noqa: E402

OUT = ROOT / "docs/shots"
HZ = 60
STEP = 3  # GIF frame every 3 ticks (50 ms)
TZ = -5 * 60  # sample local offset for the captions
LILAC = (214, 190, 246)
M2026 = 9497 * 1440


def lerp(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def lerpp(pa, pb, t):
    return [lerp(a, b, t) for a, b in zip(pa, pb)]


def ease(u):
    u = max(0.0, min(1.0, u))
    return u * u * (3 - 2 * u)


def bay(x, y):
    return BAYER[y & 3][x & 3] / 16.0


def hhmm(m2000):
    m = (m2000 + TZ) % 1440
    return f"{m // 60:02d}:{m % 60:02d}"


# =====================================================================================================================
# 1. the shared window kit: wall, a three-pane window, sill, carpet (tile rows 0..14); caption rows 15..17
# =====================================================================================================================
SKY, SUN, HOR, WALL, FLOOR, CAP1, CAP2, XTRA = range(8)
PANES = [(1, 5), (7, 12), (14, 18)]  # tile columns of the three panes (mullions at 6 and 13)
WIN_T, WIN_B = 1, 10  # tile rows of the panes
SILL_ROW = 11


class Bg:
    def __init__(self):
        self.pid = np.full((18, 20), WALL, dtype=np.int32)
        self.idx = np.zeros((144, 160), dtype=np.int32)

    def copy(self):
        b = Bg()
        b.pid, b.idx = self.pid.copy(), self.idx.copy()
        return b

    def is_sky(self, x, y):
        return 0 <= x < 160 and 0 <= y < 144 and self.pid[y // 8, x // 8] in (SKY, SUN, HOR, XTRA)


def in_pane(x, y):
    tx, ty = x // 8, y // 8
    return WIN_T <= ty <= WIN_B and any(a <= tx <= b for a, b in PANES)


def kit(stars_seed=3, star_n=46, sun_block=None, top_band=0.32):
    """the room with the window; sky pixels: 0 top, 1 mid, 2 low (SKY pal) and stars (3, on 'mid' pixels only)"""
    bg = Bg()
    r = random.Random(stars_seed)
    # wall: lilac office panelling, vertical seams every 16 px, a dado rail at the sill
    for y in range(0, 120):
        for x in range(160):
            i = 1
            if x % 16 == 0:
                i = 0
            elif x % 16 == 1:
                i = 2
            elif bay(x, y) < 0.18 * (1 - y / 120):
                i = 2
            bg.idx[y, x] = i
    # window panes
    for (a, b) in PANES:
        for ty in range(WIN_T, WIN_B + 1):
            for tx in range(a, b + 1):
                bg.pid[ty, tx] = SKY if ty < 9 else HOR
    for y in range(WIN_T * 8, (WIN_B + 1) * 8):
        for x in range(160):
            if not in_pane(x, y):
                continue
            v = (y - 8) / 80.0  # 0 top .. 1 horizon
            if v < top_band:
                i = 1 if bay(x, y) < v / top_band else 0
            elif v < 0.62:
                i = 1
            else:
                i = 2 if bay(x, y) < (v - 0.62) / 0.38 else 1
            if y >= 72:  # HOR rows: 0 = sky mid, 1 = sky low (same colours as SKY 1, 2)
                i = i - 1 if i else 0
            bg.idx[y, x] = i
    # the frame around the panes: a bevel on the wall tiles that touch the window, mullions with a lit edge
    for y in range(WIN_T * 8 - 3, (WIN_B + 1) * 8 + 1):
        for x in range(8 - 3, 152 + 3):
            if in_pane(x, y):
                continue
            near = any(in_pane(x + dx, y + dy) for dx in (-3, 0, 3) for dy in (-3, 0, 3))
            if near:
                left_lit = in_pane(x + 1, y) or in_pane(x + 2, y) or in_pane(x + 3, y)
                bg.idx[y, x] = 3 if (left_lit or in_pane(x, y + 2) or in_pane(x, y + 3)) else 0
                if in_pane(x - 1, y) or in_pane(x, y - 1):
                    bg.idx[y, x] = 0
    for mx in (6, 13):  # mullions: a bar with a light left face and a shadow right face
        for y in range(WIN_T * 8, (WIN_B + 1) * 8):
            for x in range(mx * 8, mx * 8 + 8):
                d = x - mx * 8
                bg.idx[y, x] = 0 if d in (0, 7) else (3 if d == 1 else (2 if d < 4 else 1))
    # sill (row 11): top highlight, a front face, a shadow line
    for y in range(SILL_ROW * 8, SILL_ROW * 8 + 8):
        for x in range(160):
            d = y - SILL_ROW * 8
            bg.idx[y, x] = 3 if d == 0 else (2 if d < 3 else (1 if d < 6 else 0))
    # carpet rows 12..14 (FLOOR): dithered depth, a perspective seam fan, and the window's light patch
    for ty in range(12, 15):
        for tx in range(20):
            bg.pid[ty, tx] = FLOOR
    for y in range(96, 120):
        dz = (y - 96) / 24.0
        for x in range(160):
            i = 1 if bay(x, y) < 0.35 + 0.5 * dz else 0
            u = (x - 80) / (1.0 + 2.2 * (1 - dz))
            if abs(u - round(u / 22) * 22) < 0.6:
                i = 0
            lp = abs(x - 80) < 50 + 20 * dz and y < 112
            if lp and bay(x, y) < 0.8 - dz:
                i = 2
            bg.idx[y, x] = i
        if y == 96:
            bg.idx[y, :] = 3
    # skyline in the HOR rows: blocky towers of the shared world, lit windows on 3
    for (a, b) in PANES:
        x = a * 8
        while x <= b * 8 + 7:
            w = r.randrange(6, 15)
            h = r.randrange(5, 15)
            for xx in range(x, min(x + w, b * 8 + 8)):
                for yy in range(88 - h, 88):
                    bg.idx[yy, xx] = 2
                    if (xx - x) % 3 == 1 and (yy - (88 - h)) % 3 == 1 and r.random() < 0.55 and yy < 86:
                        bg.idx[yy, xx] = 3
                if r.random() < 0.15 and h > 10:  # an antenna
                    bg.idx[88 - h - 2:88 - h, x + w // 2] = 2
            x += w + r.randrange(0, 3)
    # stars on mid pixels of the SKY rows
    n = 0
    while n < star_n:
        x, y = r.randrange(8, 152), r.randrange(8, 66)
        if bg.pid[y // 8, x // 8] == SKY and bg.idx[y, x] == 1:
            bg.idx[y, x] = 3
            n += 1
    if sun_block:
        tx0, ty0, tx1, ty1 = sun_block
        bg.pid[ty0:ty1 + 1, tx0:tx1 + 1] = SUN
        for y in range(ty0 * 8, ty1 * 8 + 8):
            for x in range(tx0 * 8, tx1 * 8 + 8):
                bg.idx[y, x] = 0  # SUN 0 = sky mid; the band is flat mid there
    return bg


def wall_pals(light, tint=(10, 8, 30)):
    """the room palettes at a light level 0..1 (the room darkens towards a cool ink)"""
    w = [(46, 30, 92), (104, 72, 168), (150, 116, 212), (232, 214, 255)]
    f = [(96, 54, 92), (176, 110, 138), (238, 180, 170), (255, 232, 222)]
    k = 1 - light
    return [lerp(c, lerp(tint, c, 0.25), k) for c in w], [lerp(c, lerp(tint, c, 0.2), k) for c in f]


CAPS = [[INK, (40, 26, 86), CYAN, WHITE], [INK, (40, 26, 86), PINK, YEL]]


def caption(l1, l2, slide=1.0, rim=CYAN):
    """the window layer (24 px). l1 white (CAP1 rows 15-16), l2 yellow/pink (CAP2 row 17); returns RGB 160x24"""
    c = Image.new("RGB", (160, 24), CAPS[0][1])
    px = c.load()
    for x in range(160):
        px[x, 0] = rim
        px[x, 1] = INK
    if l1:
        text(c, 4, 4, l1[:19], WHITE)
    if l2:
        col = YEL
        parts = l2.split("|")
        x = 4
        for k, p in enumerate(parts):
            text(c, x, 16, p, col if k == 0 else PINK)
            x += 8 * len(p)
    return c


def legal_caption(c):
    a = np.asarray(c, dtype=np.int32)
    pals = [np.array(p, dtype=np.int32) for p in CAPS]
    idx = np.zeros((24, 160), dtype=np.int32)
    pid = np.zeros((3, 20), dtype=np.int32)
    err = 0
    for ty in range(3):
        for tx in range(20):
            blk = a[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            best = None
            for p_i in ([0] if ty < 2 else [1]):
                d = ((blk[:, :, None, :] - pals[p_i][None, None]) ** 2).sum(-1)
                e = d.min(-1).sum()
                if best is None or e < best[0]:
                    best = (e, p_i, d.argmin(-1))
            err += best[0]
            pid[ty, tx] = CAP1 + best[1]
            idx[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] = best[2]
    return pid, idx, err


# =====================================================================================================================
# 2. sprites
# =====================================================================================================================
class Spr:
    def __init__(self):
        self.items = []  # (RGBA image, x, y, obj palette name)

    def add(self, img, x, y, pal):
        self.items.append((img, int(round(x)), int(round(y)), pal))

    def stats(self):
        boxes, objs, pals = [], 0, set()
        for img, x, y, p in self.items:
            bb = img.getbbox()
            if not bb:
                continue
            cols = {c[:3] for c in img.convert("RGBA").get_flattened_data() if c[3]}
            assert len(cols) <= 3, (p, len(cols))
            w, h = bb[2] - bb[0], bb[3] - bb[1]
            n = math.ceil(w / 8) * math.ceil(h / 16)
            objs += n
            boxes.append((x + bb[0], y + bb[1], w, h))
            pals.add(p)
        worst = 0
        for line in range(144):
            k = sum(math.ceil(w / 8) for (x, y, w, h) in boxes if y <= line < y + h and x < 160 and x + w > 0)
            worst = max(worst, k)
        return objs, worst, len(pals)

    def draw(self, im, fade=1.0):
        for img, x, y, _ in self.items:
            if fade < 1.0:  # the OBJ palettes fade with the BG ones
                img = img.copy()
                px = img.load()
                for yy in range(img.height):
                    for xx in range(img.width):
                        c = px[xx, yy]
                        if c[3]:
                            px[xx, yy] = (*lerp(INK, c[:3], fade), 255)
            im.paste(img, (x, y), img)


def heir_back(rim=(150, 130, 230), body=(16, 10, 34), hair=(30, 18, 56)):
    """the heir from behind at the window, 16x30, a silhouette with a window-lit rim"""
    s = Image.new("RGBA", (16, 30), (0, 0, 0, 0))
    px = s.load()
    rows = [
        ".....222222.....", "....22111122....", "...2211111122...", "...2111111112...", "...2111111112...",
        "...2111111112...", "....21111112....", ".....211112.....", "......2112......", "...22211112222..",
        "..2111111111112.", ".211111111111112", ".211111111111112", ".211111111111112", ".211111111111112",
        ".211111111111112", ".211111111111112", ".2111111111111..", "..21111111111...", "..21111111112...",
        "..2111111111....", "...211111112....", "...211111112....", "...21111.1112...", "...2111..1112...",
        "...2111..1112...", "...2111..1112...", "...211...112....", "..2211...1122...", "..2221...2222...",
    ]
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c == "1":
                px[x, y] = (*(hair if y < 8 else body), 255)
            elif c == "2":
                px[x, y] = (*rim, 255)
    return s


def disc(r, col, rim=None, rimw=1.0, size=24, cx=None, cy=None):
    s = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = s.load()
    cx = size / 2 - 0.5 if cx is None else cx
    cy = size / 2 - 0.5 if cy is None else cy
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy)
            if d <= r:
                c = col
                if rim and d > r - rimw and bay(x, y) < 0.6:
                    c = rim
                px[x, y] = (*c, 255)
    return s


def sparkle(col=WHITE, col2=YEL, n=7):
    s = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    c = n // 2
    for k in range(n):
        d = abs(k - c)
        cc = col if d < 2 else col2
        s.putpixel((k, c), (*cc, 255))
        s.putpixel((c, k), (*cc, 255))
    s.putpixel((c - 1, c - 1), (*col2, 255))
    s.putpixel((c + 1, c + 1), (*col2, 255))
    s.putpixel((c + 1, c - 1), (*col2, 255))
    s.putpixel((c - 1, c + 1), (*col2, 255))
    return s


def osd_text(s, col=WHITE):
    im = Image.new("RGBA", (8 * len(s) + 2, 9), (0, 0, 0, 0))
    rgb = Image.new("RGB", im.size, (1, 2, 3))
    text(rgb, 0, 0, s, col, shadow=INK)
    for y in range(im.height):
        for x in range(im.width):
            p = rgb.getpixel((x, y))
            if p != (1, 2, 3):
                im.putpixel((x, y), (*p, 255))
    return im


REW = ["................", "...1.....1......", "..11....11......", ".121...121......", "1221..1221......",
       ".121...121......", "..11....11......", "...1.....1......"]


def rew_icon():
    s = Image.new("RGBA", (16, 8), (0, 0, 0, 0))
    for y, row in enumerate(REW):
        for x, c in enumerate(row):
            if c != ".":
                s.putpixel((x, y), (*(WHITE if c == "2" else INK), 255))
    return s


# planet glyphs, 12x14 inside a 16x16 sprite: 1 ink outline, 2 body, 3 highlight
GLYPH = {
    "MERCURY": [
        "...2......2.....", "....2....2......", ".....2222.......", "....2....2......", "...2......2.....",
        "...2......2.....", "...2......2.....", "....2....2......", ".....2222.......", ".......2........",
        ".......2........", ".....22222......", ".......2........", ".......2........"],
}
PLANET_COL = {  # (body, highlight) per planet: the per-planet colour (and the trail)
    "MERCURY": ((64, 224, 248), (230, 255, 255)), "VENUS": ((255, 140, 200), (255, 230, 245)),
    "MARS": ((255, 110, 80), (255, 210, 170)), "JUPITER": ((255, 196, 110), (255, 244, 210)),
    "SATURN": ((210, 180, 120), (250, 236, 200)), "URANUS": ((100, 230, 200), (220, 255, 245)),
    "NEPTUNE": ((96, 140, 255), (210, 225, 255)), "PLUTO": ((176, 120, 236), (236, 214, 255)),
}


def glyph_sprite(name, flash=0.0):
    body, hi = PLANET_COL[name]
    body = lerp(body, WHITE, flash)
    rows = GLYPH[name]
    s = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    on = {(x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c != "."}
    for (x, y) in on:  # thicken + outline
        for dx in (0, 1):
            s.putpixel((x + dx, y + 1), (*body, 255))
    for (x, y) in list(on):
        for dx in (-1, 0, 1, 2):
            for dy in (0, 1, 2):
                X, Y = x + dx, y + dy
                if 0 <= X < 16 and 0 <= Y < 16 and s.getpixel((X, Y))[3] == 0:
                    s.putpixel((X, Y), (*INK, 255))
    for (x, y) in on:
        if (x + y) % 3 == 0:
            s.putpixel((x, y + 1), (*hi, 255))
    return s


# =====================================================================================================================
# 3. frame assembly + checks
# =====================================================================================================================
def tile_keys(idx, pid):
    out = {}
    for ty in range(18):
        for tx in range(20):
            b = idx[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            ks = [b.tobytes(), b[:, ::-1].tobytes(), b[::-1].tobytes(), b[::-1, ::-1].tobytes()]
            out[(ty, tx)] = min(ks)
    return out


class Recorder:
    def __init__(self, name, base=None, extra=()):
        self.name = name
        self.base = set()  # tiles the scene stores in ROM: its base BG + stored extras (noise); the rest is plotted
        if base is not None:
            k = tile_keys(base.idx, base.pid)
            self.base = {v for (ty, tx), v in k.items() if ty < 15} | {np.asarray(e, dtype=np.int32).tobytes()
                                                                        for e in extra}
        self.union_scene, self.union_cap = set(), set()
        self.prev = None
        self.worst = dict(tiles_frame=0, tile_writes=0, map_writes=0, objs=0, objs_line=0, obj_pals=0, bg_pals=0,
                          cap_err=0)

    def frame(self, bg, pals, cap, spr, shift=None, fade=1.0):
        assert len(pals) == 8 and all(len(p) == 4 for p in pals)
        cpid, cidx, err = legal_caption(cap) if cap is not None else (None, None, 0)
        pid, idx = bg.pid.copy(), bg.idx.copy()
        if cap is not None:
            pid[15:18], idx[120:144] = cpid, cidx
        keys = tile_keys(idx, pid)
        scene_k = {v for (ty, tx), v in keys.items() if ty < 15}
        cap_k = {v for (ty, tx), v in keys.items() if ty >= 15}
        self.union_scene |= scene_k
        self.union_cap |= cap_k
        w = self.worst
        w["tiles_frame"] = max(w["tiles_frame"], len(scene_k | cap_k))
        if self.prev is not None:
            changed = [k for k, v in keys.items() if k[0] < 15 and self.prev.get(k) != v]
            w["map_writes"] = max(w["map_writes"], len(changed))
            plotted = sum(1 for k in changed if keys[k] not in self.base)
            w["tile_writes"] = max(w["tile_writes"], plotted)
        self.prev = keys
        objs, line, op = spr.stats()
        w["objs"], w["objs_line"], w["obj_pals"] = max(w["objs"], objs), max(w["objs_line"], line), max(w["obj_pals"], op)
        w["bg_pals"] = max(w["bg_pals"], len(set(pid.flatten().tolist())))
        w["cap_err"] = max(w["cap_err"], int(err))
        pl = np.array(pals, dtype=np.uint8)
        pp = np.repeat(np.repeat(pid, 8, 0), 8, 1)
        im = pl[pp, idx]
        if shift is not None:
            for y in range(120):
                if shift[y]:
                    im[y] = np.roll(im[y], shift[y], axis=0)
        img = Image.fromarray(im, "RGB")
        spr.draw(img, fade)
        return img

    def cost(self, pal_keys, script=48, shared=frozenset()):
        own = self.base - shared
        return dict(base_tiles=len(self.base), kit_tiles=len(self.base & shared), own_tiles=len(own),
                    plotted_seen=len(self.union_scene - self.base), tile_bytes=len(own) * 16, map_bytes=2 * 15 * 20,
                    pal_key_bytes=pal_keys * 64, script_bytes=script, **self.worst)


def composite(im, label=None, sub=(), scale=3):
    W = 4 + 160 + 8 + 160 * scale + 4
    H = 4 + 144 * scale + 4
    c = Image.new("RGB", (W, H), (12, 8, 26))
    c.paste(im, (4, 4))
    c.paste(im.resize((160 * scale, 144 * scale), Image.NEAREST), (172, 4))
    text(c, 4, 152, "1X", (110, 80, 170))
    text(c, 4, 162, f"{scale}X >", (110, 80, 170))
    if label:
        text(c, 4, 180, label[:20], PINK)
    for i, ln in enumerate(sub):
        text(c, 4, 192 + i * 10, ln[:20], LILAC)
    return c


def save_gif(frames, durs, path):
    q = [f.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
    q[0].save(path, save_all=True, append_images=q[1:], duration=durs, loop=0, optimize=True)


def run(name, n_ticks, frame_fn, beats, base, extra=()):
    rec = Recorder(name, base, extra)
    frames, durs, last = [], [], None
    for t in range(0, n_ticks, STEP):
        im = frame_fn(t, rec)
        b = next((b for b in beats if b[0] <= t < b[1]), beats[-1])
        c = composite(im, f"{b[2]} {t / HZ:4.2f}S", b[3])
        if last is not None and c.tobytes() == last:
            durs[-1] += STEP * 1000 // HZ
            continue
        frames.append(c)
        durs.append(STEP * 1000 // HZ)
        last = c.tobytes()
    durs[-1] += 1200
    save_gif(frames, durs, OUT / f"skyscene_{name}.gif")
    return rec, len(frames)


# =====================================================================================================================
# 4. SOLAR ECLIPSE (total; annular and partial as stills)
# =====================================================================================================================
EVENTS = json.loads((ROOT / "proto/sky/out/events.json").read_text())
ECL = next(e for e in EVENTS["eclipses"] if e[1] == "solar" and e[2] == "total")  # 2026-08-12
MERC = next(s for s in EVENTS["stations"]["MERCURY"] if s[1] == "R" and s[0] > (M2026 + 270 * 1440))
METEORS = json.loads((ROOT / "proto/skyscenes/out/meteors.json").read_text())["showers"]
GEM = METEORS["GEM"]["peaks"][0] + M2026  # 2026-12-14

SUN_C, SUN_R = (80, 40), 9
DAY = [(150, 170, 240), (196, 206, 252), (252, 214, 226), (196, 206, 252)]  # top, mid, low, star(=mid by day)
DARK = [(10, 8, 34), (30, 20, 74), (214, 92, 140), (240, 232, 255)]  # totality: the 360-degree sunset on the rim


def corona_bg(bg, cx=SUN_C[0], cy=SUN_C[1], r_in=SUN_R):
    for y in range(16, 64):
        for x in range(56, 104):
            d = math.hypot(x - cx, y - cy)
            a = math.atan2(y - cy, x - cx)
            stream = 0.55 + 0.45 * (math.cos(2 * a + 0.4) ** 2) + 0.15 * math.cos(7 * a)  # equatorial streamers
            if d <= r_in:
                bg.idx[y, x] = 3
            elif d < r_in + 4:
                bg.idx[y, x] = 2 if bay(x, y) < 0.95 - (d - r_in) * 0.12 else 1
            elif d < r_in + 4 + 11 * stream:
                u = (d - r_in - 4) / (11 * stream)
                bg.idx[y, x] = (2 if bay(x, y) < 0.35 * (1 - u) else 1) if bay(x + 1, y) < 1 - u * 0.9 else 0
            else:
                bg.idx[y, x] = 0


ECL_BG = kit(stars_seed=5, star_n=40, sun_block=(7, 2, 12, 7), top_band=0.1)
corona_bg(ECL_BG)


def overlap(d, r1, r2):
    if d >= r1 + r2:
        return 0.0
    if d <= abs(r2 - r1):
        return min(1.0, (min(r1, r2) / r1) ** 2)
    a = r1 * r1 * math.acos((d * d + r1 * r1 - r2 * r2) / (2 * d * r1))
    b = r2 * r2 * math.acos((d * d + r2 * r2 - r1 * r1) / (2 * d * r2))
    c = 0.5 * math.sqrt((-d + r1 + r2) * (d + r1 - r2) * (d - r1 + r2) * (d + r1 + r2))
    return (a + b - c) / (math.pi * r1 * r1)


def moon_path(t, kind):
    s = t / HZ
    end = {"total": (80.0, 40.0), "annular": (80.0, 40.0), "partial": (86.0, 33.0)}[kind]
    if s < 2.7:
        u = 1 - (1 - s / 2.7) ** 1.6
        return 67 + (end[0] - 67) * u, 30 + (end[1] - 30) * u
    if s < 4.5:
        u = (s - 2.7) / 1.8
        return end[0] - 0.6 + 1.2 * u, end[1] - 0.3 + 0.6 * u
    u = (s - 4.5) / 1.5
    return end[0] + 0.6 + 11 * u * u, end[1] + 0.3 + 5 * u * u


def eclipse_frame(t, rec, kind="total"):
    s = t / HZ
    mr = {"total": 10.0, "annular": 7.5, "partial": 10.0}[kind]
    mx, my = moon_path(t, kind)
    d = math.hypot(mx - SUN_C[0], my - SUN_C[1])
    cov = overlap(d, SUN_R, mr)
    tot = kind == "total" and d <= mr - SUN_R + 0.35
    dark = cov ** 3.5 if not tot else 1.0
    if kind == "annular":
        dark = min(dark, 0.72)
    if kind == "partial":
        dark = min(dark, 0.5)
    sky = lerpp(DAY, DARK, dark * 0.96)
    star = lerp(sky[1], DARK[3], (dark - 0.85) / 0.15) if kind == "total" else sky[1]
    sky_p = [sky[0], sky[1], sky[2], star]
    glow = (1 - dark) * 0.75 * (1 - cov)
    if tot:
        sh = 0.5 + 0.5 * math.sin(t * 0.21)  # corona shimmer: two palette states
        sun_p = [sky[1], lerp((120, 100, 210), (150, 128, 230), sh), lerp((226, 210, 255), (246, 236, 255), sh),
                 (255, 250, 236)]
    else:
        sun_p = [sky[1], lerp(sky[1], (238, 236, 255), glow * 0.5), lerp(sky[1], (255, 248, 236), 0.25 + glow),
                 (255, 250, 236)]
        if kind == "annular" and cov > 0.4:
            sun_p[2] = lerp(sun_p[2], (255, 200, 150), (cov - 0.4) * 1.5)
            sun_p[3] = (255, 236, 196)
    lit = lerp((60, 40, 100), YEL, (dark - 0.6) / 0.3)  # street lights come on in the shared world
    hor_p = [sky[1], sky[2], lerp((120, 110, 170), (20, 14, 44), dark), lit]
    light = 1 - 0.82 * dark
    wp, fp = wall_pals(light)
    pals = [sky_p, sun_p, hor_p, wp, fp, CAPS[0], CAPS[1], CAPS[0]]
    # fade in from the game (0.25 s) and back (last 0.25 s)
    fin = min(1.0, s / 0.25, (6.0 - s) / 0.25)
    pals = [lerpp([INK] * 4, p, fin) if i < 5 else p for i, p in enumerate(pals)]
    spr = Spr()
    moon_col = lerp((150, 150, 210), (8, 6, 24), 0.4 + 0.6 * min(1, cov * 1.6))
    earth = lerp(moon_col, (70, 60, 120), 0.6 if tot else 0.2)
    m = disc(mr, moon_col, earth, 1.2)
    spr.add(m, mx - 11.5, my - 11.5, "MOON")
    # diamond ring at second and third contact (total only): the last bead of sunlight on the limb
    for tc, sgn in ((2.62, 1), (4.48, -1)):
        if kind == "total" and abs(s - tc) < 0.14:
            k = 1 - abs(s - tc) / 0.14
            ang = math.atan2(16, 32) * 1 + (0 if sgn > 0 else math.pi)
            bx, by = SUN_C[0] + SUN_R * math.cos(ang), SUN_C[1] + SUN_R * math.sin(ang)
            n = 9 if k > 0.5 else 7
            spr.add(sparkle(WHITE, (255, 236, 180), n), bx - n // 2, by - n // 2, "SPARK")
    rim = lerp((246, 214, 236), (120, 100, 200), dark)
    spr.add(heir_back(rim=rim, body=lerp((40, 26, 70), (8, 6, 20), dark), hair=lerp((60, 36, 90), (14, 8, 30), dark)),
            72, 77, "HEIR")
    name = {"total": "TOTAL SOLAR ECLIPSE", "annular": "ANNULAR ECLIPSE", "partial": "PARTIAL ECLIPSE"}[kind]
    if s < 4.6:
        ev = next(e for e in EVENTS["eclipses"] if e[1] == "solar" and e[2] == kind)  # its own real time
        cap = caption(name, f"{hhmm(ev[0])}  |B:SKIP")
    else:
        cap = caption("SKY TODAY: ECLIPSE", "DARK ROOMS  |ECHO+1")
    return rec.frame(ECL_BG, pals, cap, spr, fade=fin)


ECL_BEATS = [
    (0, 40, "CUT IN", ["PALETTE FADE IN;", "MUSIC DUCKS TO PAD"]),
    (40, 156, "FIRST BITE", ["MOON = 6 SPRITES;", "SKY + ROOM DARKEN", "BY PALETTE ONLY"]),
    (156, 170, "DIAMOND RING", ["SPARK SPRITE ON", "THE LIMB"]),
    (170, 270, "TOTALITY", ["CORONA TILES WERE", "THERE ALL ALONG:", "PAL 1 TURNS THEM", "ON. STARS, STREET", "LIGHTS = PAL",
                             "WRITES. MUSIC HUSH"]),
    (270, 276, "THIRD CONTACT", ["SECOND RING"]),
    (276, 360, "SKY TODAY", ["CAPTION ANNOUNCES", "THE DAY'S EFFECT;", "STING RESOLVES"]),
]


# =====================================================================================================================
# 5. RETROGRADE STATION (Mercury; per-planet colour from PLANET_COL)
# =====================================================================================================================
NIGHT = [(12, 8, 36), (34, 22, 80), (64, 224, 248), (236, 226, 255)]  # top, mid, trail (planet), star


def retro_kit():
    bg = kit(stars_seed=11, star_n=52)
    # night: the band below 'mid' stays mid (no low colour in the SKY pal: idx 2 is the trail); remap low -> mid
    for y in range(8, 72):
        for x in range(160):
            if bg.pid[y // 8, x // 8] == SKY and bg.idx[y, x] == 2:
                bg.idx[y, x] = 1
    # Scorpius-ish: a hook of brighter stars (2x2 crosses) on the path's side of the sky
    for (x, y) in [(118, 20), (124, 26), (128, 34), (126, 44), (120, 50), (114, 52), (110, 48), (36, 30), (44, 18)]:
        if bg.is_sky(x, y):
            for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
                if bg.is_sky(x + dx, y + dy) and (dx == 0 or dy == 0):
                    bg.idx[y + dy, x + dx] = 3 if (dx, dy) == (0, 0) else 1
    return bg


RETRO_BG = retro_kit()


def retro_pos(s):
    """the apparent path: east, slowing to the station, then back west on a lower arc (the loop)"""
    if s < 0.3:
        s = 0.3
    if s < 1.7:
        u = (s - 0.3) / 1.4
        u = 1 - (1 - u) ** 2.2
        return 22 + 84 * u, 48 - 12 * math.sin(u * math.pi * 0.55)
    if s < 2.5:
        return 106.0, 48 - 12 * math.sin(math.pi * 0.55)
    if s < 4.7:
        u = ease((s - 2.5) / 2.2)
        y0 = 48 - 12 * math.sin(math.pi * 0.55)
        return 106 - 50 * u, y0 + 16 * math.sin(u * math.pi * 0.9) + 2 * u
    u = (s - 4.7) / 1.3
    y0 = 48 - 12 * math.sin(math.pi * 0.55)
    return 56 - 6 * (1 - (1 - u) ** 2), y0 + 16 * math.sin(math.pi * 0.9) + 2


def noise_tile(seed):
    r = random.Random(seed)
    return np.array([[r.choice((0, 1, 1, 3)) if r.random() < 0.5 else 1 for _ in range(8)] for _ in range(8)])


NOISE = [noise_tile(k) for k in range(2)]


def retro_frame(t, rec, planet="MERCURY"):
    s = t / HZ
    bg = RETRO_BG.copy()
    # trail: the path so far, every other sample, drawn into the sky tiles (tile writes)
    pts = []
    k = 0.3
    while k <= s:
        pts.append(retro_pos(k))
        k += 1 / 60
    for i, (x, y) in enumerate(pts):
        X, Y = int(round(x)) + 8, int(round(y)) + 8
        if i % 2 == 0 and bg.is_sky(X, Y) and bg.pid[Y // 8, X // 8] == SKY:
            bg.idx[Y, X] = 2
    rew = 2.5 <= s < 4.7
    shift = None
    if rew:
        # VHS tracking band: one tile row of noise tiles marching down (20 map writes), plus line tears (SCX)
        band = int(((s - 2.5) / 2.2) * 14) + 1
        for tx in range(20):
            if band < 15:
                bg.idx[band * 8:band * 8 + 8, tx * 8:tx * 8 + 8] = NOISE[(t // 6 + tx) & 1]
        shift = [0] * 144
        for y in range(120):
            near = abs(y - (band * 8 + 4))
            if near < 14:
                shift[y] = int(round((14 - near) / 4 * math.sin(y * 1.7 + t)))
            elif (y + t * 3) % 37 == 0:
                shift[y] = 2
    body, hi = PLANET_COL[planet]
    sky_p = list(NIGHT)
    sky_p[2] = body
    if rew and (t // 6) % 2:  # chroma wobble: the trail and the stars swap hue for a frame
        sky_p[2], sky_p[3] = lerp(body, PINK, 0.7), (200, 255, 255)
    hor_p = [NIGHT[1], (86, 38, 110), (18, 12, 40), (255, 210, 120)]
    wp, fp = wall_pals(0.42, tint=(8, 8, 36))
    if 1.7 <= s < 2.5:  # the station: a cold flash through the room
        f = max(0.0, 1 - abs(s - 1.8) / 0.5)
        wp, fp = lerpp(wp, [lerp(c, body, 0.35) for c in wp], f), lerpp(fp, [lerp(c, body, 0.25) for c in fp], f)
    pals = [sky_p, sky_p, hor_p, wp, fp, CAPS[0], CAPS[1], sky_p]
    fin = min(1.0, s / 0.25, (6.0 - s) / 0.25)
    pals = [lerpp([INK] * 4, p, fin) if i < 5 or i == 7 else p for i, p in enumerate(pals)]
    spr = Spr()
    x, y = retro_pos(s)
    flash = 0.0
    if 1.7 <= s < 2.5:
        flash = 0.5 + 0.5 * math.sin((s - 1.7) * 30)
        x += 1 if (t // 4) % 2 else 0  # it judders in place
    spr.add(glyph_sprite(planet, flash), x, y, "GLYPH")
    if rew:
        if (t // 15) % 3 != 2:
            spr.add(rew_icon(), 8, 10, "OSD")
        spr.add(osd_text("REW"), 26, 10, "OSD")
    if 1.7 <= s < 2.5 and (t // 8) % 2 == 0:
        spr.add(osd_text("STATION", hi), 52, 70, "OSDP")
    spr.add(heir_back(rim=lerp((110, 90, 190), body, 0.3)), 72, 77, "HEIR")
    if s < 4.7:
        cap = caption(f"{planet} STATIONS", f"RETROGRADE {hhmm(MERC[0])}")
    else:
        cap = caption("SKY TODAY: MERC R", "TALK SCRAMBLES|")
    return rec.frame(bg, pals, cap, spr, shift, fade=fin)


RETRO_BEATS = [
    (0, 18, "CUT IN", ["NIGHT PALETTES"]),
    (18, 102, "DIRECT", ["GLYPH = 2 SPRITES;", "TRAIL IS DRAWN", "INTO SKY TILES", "(PLANET COLOUR)"]),
    (102, 150, "STATION", ["GLYPH HALTS AND", "JUDDERS; ROOM", "FLASHES COLD", "(PAL WRITES)"]),
    (150, 282, "REWIND", ["LOOPS BACK; VHS", "BAND = 20 MAP", "WRITES, TEARS =", "PER-LINE SCX,", "OSD = 3 SPRITES"]),
    (282, 360, "SKY TODAY", ["EFFECT ON THE", "SKY TODAY PANEL"]),
]


# =====================================================================================================================
# 6. METEOR SHOWER (Geminids), wishes as matrix talk
# =====================================================================================================================
RADIANT = {0: (20, 10), 1: (80, 4), 2: (140, 10), 3: (154, 40), 4: (140, 80), 5: (80, 84), 6: (20, 80), 7: (6, 40)}
STREAK_COL = [WHITE, CYAN, YEL, PINK, (140, 240, 170)]

# matrix talk: a wish = WHO + the want, composed from the family's strongest matrix tags (5.1) like any talk line
WHO = ["VEDA", "OREN", "PIM", "CORA'S GHOST", "GRAN IDA"]
TAG_ADJ = {"FIRE": "WARM", "WATER": "CALM", "SEED": "GREEN", "SPARK": "BRIGHT", "COLD": "QUIET", "GLITCH": "ODD"}
NOUN = ["ROOM", "DOOR", "STAIR", "WINDOW", "SONG", "KEY", "GARDEN", "FLOOR"]
VERB = ["STAYS LIT", "REMEMBERS", "OPENS OUT", "KEEPS GROWING", "HUMS BACK", "LEADS HOME"]


def wishes(seed=7, n=4):
    r = random.Random(seed)
    tags = ["SEED", "WATER", "SPARK", "FIRE", "GLITCH", "COLD"]
    out = []
    for i in range(n):
        who = WHO[i % len(WHO)] if i else "PIM"
        for _ in range(40):  # the talk composer re-picks until the line fits the 2 x 19 caption, as the game's talk does
            adj, noun, verb = TAG_ADJ[tags[i]], r.choice(NOUN), r.choice(VERB)
            words = f"{who}: A {adj} {noun} THAT {verb}".split()
            l1 = ""
            while words and len(l1) + len(words[0]) + (1 if l1 else 0) <= 19:
                l1 = (l1 + " " + words.pop(0)).strip()
            l2 = " ".join(words)
            if len(l2) <= 19:
                break
        else:  # a long name: the wish loses its clause
            l1, l2 = f"{who} WISHES", f"A {adj} {noun}"
        out.append((l1, l2))
    return out


WISHES = wishes()


def meteor_list(seed=21):
    r = random.Random(seed)
    rx, ry = RADIANT[METEORS["GEM"]["octant"]]
    ms = []
    t = 24
    while t < 300:
        ang = math.radians(r.uniform(12, 72))
        d0 = r.uniform(14, 60)
        ln = r.uniform(22, 46)
        dur = r.randrange(16, 24)
        ms.append(dict(t0=t, ang=ang, d0=d0, ln=ln, dur=dur, big=False))
        t += r.randrange(8, 24)
    ms.insert(5, dict(t0=168, ang=math.radians(38), d0=30, ln=70, dur=40, big=True))  # the fireball
    for m in ms:
        m["x0"], m["y0"] = rx + m["d0"] * math.cos(m["ang"]), ry + m["d0"] * math.sin(m["ang"])
    return ms


METS = meteor_list()
MET_BG = retro_kit()


def meteor_frame(t, rec):
    s = t / HZ
    bg = MET_BG.copy()
    spr = Spr()
    flash = 0.0
    for m in METS:
        u = (t - m["t0"]) / m["dur"]
        if not (0 <= u < 1.5):
            continue
        head = min(u, 1.0) * m["ln"]
        tl = 18 if not m["big"] else 30
        tail = max(0.0, head - tl) if u < 1 else max(0.0, head - tl) + (head - max(0.0, head - tl)) * (u - 1) * 2
        k = tail
        while k <= head:
            x = int(round(m["x0"] + k * math.cos(m["ang"])))
            y = int(round(m["y0"] + k * math.sin(m["ang"])))
            if bg.is_sky(x, y) and bg.pid[y // 8, x // 8] == SKY:
                frac = (k - tail) / max(1e-3, head - tail)
                bg.idx[y, x] = 3 if frac > 0.8 else (2 if frac > 0.25 or bay(x, y) < 0.5 else 1)
                if m["big"] and bg.is_sky(x, y + 1) and bg.pid[(y + 1) // 8, x // 8] == SKY and frac > 0.4:
                    bg.idx[y + 1, x] = 2
            k += 0.5
        if m["big"] and u < 1.0:
            hx = m["x0"] + head * math.cos(m["ang"])
            hy = m["y0"] + head * math.sin(m["ang"])
            spr.add(sparkle(WHITE, STREAK_COL[METEORS["GEM"]["colour"]], 9), hx - 4, hy - 4, "SPARK")
            flash = max(flash, math.sin(u * math.pi))
    col = STREAK_COL[METEORS["GEM"]["colour"]]
    sky_p = [NIGHT[0], NIGHT[1], col, WHITE]
    hor_p = [NIGHT[1], (86, 38, 110), (18, 12, 40), (255, 210, 120)]
    wp, fp = wall_pals(0.42 + 0.3 * flash, tint=(8, 8, 36))
    if flash:
        wp = [lerp(c, col, 0.25 * flash) for c in wp]
        fp = [lerp(c, col, 0.2 * flash) for c in fp]
    pals = [sky_p, sky_p, hor_p, wp, fp, CAPS[0], CAPS[1], sky_p]
    fin = min(1.0, s / 0.25, (6.5 - s) / 0.25)
    pals = [lerpp([INK] * 4, p, fin) if i < 5 or i == 7 else p for i, p in enumerate(pals)]
    # the fireball's wish lands on the sill: a wish star (the day's drop), twinkling
    if s > 3.6:
        u = min(1.0, (s - 3.6) / 0.6)
        wx, wy = 98 + 10 * u, 50 + (84 - 50) * ease(u)
        tw = sparkle(YEL, PINK, 9) if (t // 10) % 2 else sparkle(WHITE, YEL, 7)
        spr.add(tw, wx - tw.width // 2, wy - tw.height // 2, "WISH")
    spr.add(heir_back(rim=lerp((110, 90, 190), col, 0.25 + 0.5 * flash)), 72, 77, "HEIR")
    if s < 1.0:
        cap = caption("GEMINIDS PEAK", f"{hhmm(GEM)}  |B:SKIP")
    elif s < 5.0:
        w = WISHES[min(3, int((s - 1.0) / 1.0))]
        cap = caption(w[0], w[1] + "|")
    else:
        cap = caption("SKY TODAY: GEMINIDS", "WISH STAR  |+1 DROP")
    return rec.frame(bg, pals, cap, spr, fade=fin)


MET_BEATS = [
    (0, 60, "CUT IN", ["RADIANT FROM THE", "BAKED TABLE (GEM:", "TOP-LEFT, PINK)"]),
    (60, 168, "STREAKS", ["TILE WRITES: HEAD", "+ FADING TAIL", "IN SKY TILES;", "EACH WISH = A", "MATRIX TALK LINE"]),
    (168, 216, "FIREBALL", ["SPARK SPRITE HEAD,", "ROOM FLASH BY", "PALETTE"]),
    (216, 300, "WISH STAR", ["THE WISH DROPS", "TO THE SILL:", "THE DAY'S DROP"]),
    (300, 390, "SKY TODAY", ["EFFECT ON THE", "SKY TODAY PANEL"]),
]


# =====================================================================================================================
# 7. stills: the three solar eclipse variants at their peak
# =====================================================================================================================
def stills():
    tiles = []
    # eclipse variants at their peak
    for kind, t in (("total", 210), ("annular", 210), ("partial", 210)):
        tiles.append((eclipse_frame(t, Recorder("x"), kind), kind.upper(), ""))
    sheet = Image.new("RGB", (3 * 336 + 16, 288 + 56), (12, 8, 26))
    for i, (im, lab, _) in enumerate(tiles):
        x = 8 + i * 336
        sheet.paste(im.resize((320, 288), Image.NEAREST), (x, 8))
        big = Image.new("RGB", (160, 12), (12, 8, 26))
        text(big, 0, 0, lab + " ECLIPSE", PINK)
        sheet.paste(big.resize((320, 24), Image.NEAREST), (x, 300))
    sheet.save(OUT / "skyscene_eclipse_variants.png")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = {}
    recs = {}
    recs["eclipse"] = run("eclipse", 360, lambda t, rec: eclipse_frame(t, rec, "total"), ECL_BEATS, ECL_BG)
    recs["retrograde"] = run("retrograde", 360, retro_frame, RETRO_BEATS, RETRO_BG, NOISE)
    recs["meteors"] = run("meteors", 390, meteor_frame, MET_BEATS, MET_BG)
    # the shared window kit: base tiles common to the scenes are stored once
    shared = set.intersection(*(r.base for r, _ in recs.values()))
    meta = dict(eclipse=(4, 48), retrograde=(3, 64), meteors=(3, 96))
    for k, (r, n) in recs.items():
        report[k] = dict(r.cost(pal_keys=meta[k][0], script=meta[k][1], shared=shared), gif_frames=n)
    report["kit"] = dict(tiles=len(shared), bytes=len(shared) * 16)
    print("shared kit tiles", len(shared))
    stills()
    for k, v in report.items():
        if k == "kit":
            continue
        v["rom_total"] = v["tile_bytes"] + v["map_bytes"] + v["pal_key_bytes"] + v["script_bytes"]
        print(k, v)
    print("eclipse", hhmm(ECL[0]), "| mercury R", hhmm(MERC[0]), "| geminids", hhmm(GEM))
    (OUT / "skyscene_report.json").write_text(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
