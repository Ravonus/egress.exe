"""Bake EGRESS's floor look (the office biome): `python tools/bake_look.py OUT_DIR [--sheet PNG] [--preview PNG]`.

EGRESS's own 16x16 cards in the shared world's family style: floating slabs with a bevel and a Bayer-dithered
diagonal light, raised wall blocks with grille fronts, drywall partitions, amber CRT terminals, egg-crate foam, the
void with a ceiling-tile grid far below and a dithered underside where ground meets it. Every card is a procedural
construction (shapes, value noise, a light from the upper left) ordered-dithered into one CGB palette's four colours.

Layout in VRAM bank 1 (see src/look.h): metatile m lives at tiles LOOK_T_BASE + 4m .. +3 in 8x16 sprite order
(TL, BL, TR, BR), never deduplicated. The two flying cards (a floor slab, a wall top) are copied to sprite VRAM
(bank 0, LOOK_SPR_BASE). The reaction range (M_REACT0 ..) is reserved for the puzzle workstream.

Writes OUT_DIR/look_art.c (banked: tiles, the biome palettes, the loader) and OUT_DIR/look_art.h.
--sheet writes the art sheet (1x and 4x, every card, every biome palette); --preview renders a seeded room the way
src/grid.c draws it (needs build/host/egtool), for fast iteration without the emulator.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_art as BA  # noqa: E402  (the shared helpers: dither, value noise, encode)

dither, vnoise, grid, outline, encode, rgb555, hx = BA.dither, BA.vnoise, BA.grid, BA.outline, BA.encode, BA.rgb555, BA.hx

# --- VRAM layout (bank 1) ------------------------------------------------------------------------------------------
T_BASE = 2  # tile 0 stays blank (clear_map), tile 1 pads to an even start
REACT_METAS = 11
SPR_BASE = 248  # VRAM bank 0 (sprites): the two flying cards, a floor slab and a wall top, 8 tiles in 8x16 order  # reserved for the puzzle workstream's reaction tiles (burning, wet, ...)
FIXED_BASE = BA.BG_FIXED_BASE  # bake_art.py's font + HUD block starts here

# --- palettes: 8 BG palettes per biome. Index 1 of every palette a thing can stand in is the biome's slab base, so
# objects and reaction tiles sit on the floor without a seam ---------------------------------------------------------
PAL_FLOOR, PAL_FOAM, PAL_WALL, PAL_VOID, PAL_REACT_A, PAL_HUD, PAL_OBJ, PAL_REACT_B = range(8)
PAL_NAMES = ["FLOOR", "FOAM", "WALL", "VOID", "REACT_A", "HUD", "OBJ", "REACT_B"]


def biome(floor: list[str], foam: list[str], wall: list[str], void: list[str], accent: str, accent_hi: str,
          react_a: list[str], react_b: list[str]) -> list[list[tuple[int, int, int]]]:
    """floor: shadow, base, light, highlight. foam/react: dark, -, mid, light (index 1 becomes the slab base)."""
    s, b, l, c = (hx(x) for x in floor)
    return [
        [s, b, l, c],
        [hx(foam[0]), b, hx(foam[1]), hx(foam[2])],
        [hx(x) for x in wall],
        [hx(x) for x in void],
        [hx(react_a[0]), b, hx(react_a[1]), hx(react_a[2])],
        [hx("#140e2e"), hx("#ff6cc4"), hx(accent), hx("#faf6ff")],  # HUD + door: ink, pink, accent, white
        [s, b, hx(accent_hi), c],  # objects: the terminals' glow, tokens, keycards, props (3: the slab highlight)
        [hx(react_b[0]), b, hx(react_b[1]), hx(react_b[2])],
    ]


WET = ["#18245c", "#5094ec", "#d6f0ff"]
FIRE = ["#601630", "#ff7830", "#ffe468"]
BIOMES = {
    # name: palettes, ambient animation (src/look.c look_ambient)
    # the office: lavender carpet slabs (docs/shots/mock_room.png), muted mint acoustic foam, deep violet walls,
    # the indigo void, fluorescent-yellow accent, warm gold for tokens and the terminals' text
    "OFFICE": biome(["#543484", "#b28cde", "#d6bef6", "#faf2ff"], ["#404878", "#94ccb8", "#dcf6e6"],
                    ["#281c54", "#60429e", "#9674d2", "#e6d6ff"], ["#0e0a22", "#221446", "#4c3284", "#ecc8ff"],
                    "#ffe060", "#ffc850", WET, FIRE),
    "VENTS": biome(["#123844", "#4a9ca4", "#8cd2cc", "#e4fff6"], ["#2c2440", "#7c6ca4", "#d8ccf4"],
                   ["#0c1c24", "#2c4c5c", "#5c8494", "#cce8f0"], ["#04080c", "#0c1820", "#24404c", "#9cf0ff"],
                   "#40e0f8", "#40e0f8", WET, FIRE),
    "ATRIUM": biome(["#1c2c6c", "#5c88d8", "#9cc0f4", "#eef6ff"], ["#204840", "#58a884", "#c4f0d0"],
                    ["#141c40", "#3c4c88", "#7888c4", "#e4ecff"], ["#101830", "#283458", "#5c6c98", "#f0f4ff"],
                    "#ffffff", "#f8e8a0", WET, FIRE),
    "SERVERS": biome(["#0c2438", "#2c6878", "#5ca4a8", "#d0f4ec"], ["#202c18", "#5c8c3c", "#c4f0a0"],
                     ["#080c20", "#1c2850", "#3c5490", "#c0d0ff"], ["#020806", "#06180e", "#126030", "#60ff90"],
                     "#58f070", "#58f070", WET, FIRE),
    "FOODCOURT": biome(["#5c1c44", "#e86c9c", "#f8a8c4", "#fff0f4"], ["#5c3010", "#e09040", "#ffe0a0"],
                       ["#2c1030", "#6c2c68", "#b45ca0", "#ffd8f0"], ["#2c0c2c", "#6c1c3c", "#d0584c", "#ffd890"],
                       "#ff9c30", "#ffb040", WET, FIRE),
    "GARDEN": biome(["#1c5444", "#74c8a0", "#b0ecc8", "#f0fff4"], ["#4c2c64", "#a080d0", "#ecdcff"],
                    ["#183c34", "#3c7c64", "#78b498", "#e0fff0"], ["#3c4478", "#7888c0", "#b4c4ec", "#fff4ff"],
                    "#c8a0ff", "#d8b0ff", WET, FIRE),
    "ROOFTOP": biome(["#3c2c6c", "#a890dc", "#d0c0f4", "#faf4ff"], ["#183c4c", "#4ca0b8", "#c4f4ff"],
                     ["#1c1840", "#4c4488", "#8c84c4", "#ecf0ff"], ["#04040c", "#0c0c24", "#2c2c58", "#ffffff"],
                     "#40e0f8", "#60e8ff", WET, FIRE),
    "GLITCH": biome(["#e0d8f8", "#5c3c8c", "#3c2068", "#140830"], ["#f8f0ff", "#ff40c0", "#400c38"],
                    ["#fff0ff", "#c040d0", "#6c1880", "#1c0428"], ["#000000", "#2c0030", "#ff00c0", "#40ffff"],
                    "#ff30d0", "#ff30d0", WET, FIRE),
}
# ambient palette animations (src/look.c): 0 none, 1 light buzz flicker, 2 fan strobe, 3 water shimmer, 4 LED chase,
# 5 neon blink, 6 breathing glow, 7 star twinkle, 8 tear bands
AMBIENT = {"OFFICE": 1, "VENTS": 2, "ATRIUM": 3, "SERVERS": 4, "FOODCOURT": 5, "GARDEN": 6, "ROOFTOP": 7, "GLITCH": 8}


def hsh(x: int, y: int, seed: int) -> float:
    return float(BA.hash2(np.array(x), np.array(y), seed))


def rng_points(seed: int, n: int, lo: int = 2, hi: int = 13) -> list[tuple[int, int]]:
    out = []
    for k in range(n):
        out.append((lo + int(hsh(k, 1, seed) * (hi - lo + 1)), lo + int(hsh(k, 2, seed) * (hi - lo + 1))))
    return out


# --- floor slab (FLOOR: 0 shadow, 1 base, 2 light, 3 highlight) ------------------------------------------------------
def slab_shade(seed: int) -> np.ndarray:
    """the slab body: a diagonal light from the upper left, dithered between base and light, with a little noise"""
    x, y = grid()
    n = vnoise(16, 4, seed) - 0.5
    return 1.1 + 1.25 * np.clip(1.0 - (x + y) / 24.0, 0, 1) + n * 0.3


def bevel(v: np.ndarray, hi: float = 3.0, lo: float = 0.0, inner: bool = True) -> np.ndarray:
    v = v.copy()
    v[0, :] = hi
    v[:, 0] = hi
    v[15, :] = lo
    v[:, 15] = lo
    if inner:  # a soft second line: the slab's thickness catching light
        v[1, 1:15] = np.maximum(v[1, 1:15], 2.0)
        v[1:15, 1] = np.maximum(v[1:15, 1], 2.0)
        v[14, 1:15] = np.minimum(v[14, 1:15], 1.0)
        v[1:15, 14] = np.minimum(v[1:15, 14], 1.0)
    v[15, 0] = 1.0
    v[0, 15] = 1.0
    return v


def slab(seed: int, kind: str = "plain") -> np.ndarray:
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    v = slab_shade(seed)
    idx = dither(bevel(v))
    # carpet flecks: a few seeded dark and bright specks
    for k, (px, py) in enumerate(rng_points(seed, 2, 3, 12)):
        idx[py, px] = 0 if k % 2 == 0 else 3
    if kind == "scuff":  # a dragged-chair scuff, faint
        for t in range(5):
            idx[10 + t // 3, 5 + t] = 1
        idx[10, 5] = 0
    elif kind == "seam":  # carpet tiles meet: a faint seam
        idx[3:13, 8] = np.where(idx[3:13, 8] >= 2, 1, idx[3:13, 8])
    elif kind == "stain":  # a coffee ring
        d = np.hypot(x - 9.5, y - 9.0)
        ring = (d > 2.0) & (d < 3.2)
        idx = np.where(ring & ((xi + yi) % 2 == 0), 0, np.where(ring, 1, idx))
    elif kind == "crack":
        pts = [(2, 3), (5, 5), (7, 4), (8, 8), (11, 9), (13, 13)]
        side = [(8, 8), (6, 11), (4, 13)]
        for path in (pts, side):
            for (ax, ay), (bx, by) in zip(path, path[1:]):
                for t in np.linspace(0, 1, 10):
                    cx, cy = round(ax + (bx - ax) * t), round(ay + (by - ay) * t)
                    idx[cy, cx] = 0
                    if cy + 1 < 15 and idx[cy + 1, cx] != 0:
                        idx[cy + 1, cx] = 3
    elif kind == "crumble":  # rotten carpet: dithered holes down to the dark, a few cracks
        n = vnoise(16, 4, seed + 31)
        rot = (n > 0.62) & (xi > 1) & (xi < 14) & (yi > 1) & (yi < 14)
        thr = np.tile(BA.BAYER, (4, 4))
        idx = np.where(rot & (thr < (n - 0.55) * 4), 0, np.where(rot, 1, idx))
        for (px, py) in rng_points(seed + 5, 3, 3, 12):
            idx[py, px] = 0
            idx[py, min(px + 1, 14)] = 0
    elif kind == "plate":  # a pressure plate set into the slab
        inset = (xi >= 3) & (xi <= 12) & (yi >= 3) & (yi <= 12)
        idx = np.where(inset, 2, idx)
        idx = np.where(inset & ((xi == 3) | (yi == 3)), 0, idx)
        idx = np.where(inset & ((xi == 12) | (yi == 12)), 3, idx)
        idx = np.where((xi >= 5) & (xi <= 10) & (yi >= 5) & (yi <= 10), 3, idx)
        idx = np.where((xi >= 6) & (xi <= 9) & (yi >= 6) & (yi <= 9) & ((xi + yi) % 2 == 0), 2, idx)
        idx[7:9, 7:9] = 1
    elif kind == "gate_open":  # a shutter rolled up into its housing along the top
        idx[0:3, 1:15] = [[3] * 14, [1] * 14, [0] * 14]
        idx[3, 1:15] = np.where(idx[3, 1:15] > 1, 1, idx[3, 1:15])
        for gx in (1, 14):
            idx[0:16, gx] = np.where(np.arange(16) % 3 == 0, 0, 1)
    return idx.astype(np.uint8)


# --- egg-crate acoustic foam (FOAM: 0 deep, 1 slab base, 2 mid, 3 light) ---------------------------------------------
def foam(seed: int, n_: int = 3, shift: int = 0) -> np.ndarray:
    """acoustic foam: a grid of rounded pillows, each lit from the upper left, dark in the gaps"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    pitch = 14.0 / n_
    gx, gy = (x - 1 - shift) % 14 / pitch, (y - 1) / pitch
    fx, fy = gx - np.floor(gx), gy - np.floor(gy)  # 0..1 inside a pillow
    d = np.hypot(fx - 0.5, fy - 0.5) * 2  # 0 centre .. 1.4 corner
    lit = (0.5 - fx) + (0.5 - fy)  # upper left bright
    nz = vnoise(16, 3, seed) - 0.5
    v = 2.0 + lit * 1.3 + nz * 0.4 - np.clip(d - 0.8, 0, 1) * 2.2
    idx = dither(np.clip(v, 0.0, 3.0), 0, 3)
    idx = np.where(idx == 1, np.where(v > 1.2, 2, 0), idx)  # the slab's own colour never shows in the foam
    gap = (d > 1.15)
    idx = np.where(gap, 0, idx)
    # the slab rim around the mound
    idx[0, :] = 1
    idx[:, 0] = 1
    idx[15, :] = 0
    idx[:, 15] = 0
    for (px, py) in rng_points(seed + 9, 2, 2, 13):
        if idx[py, px] == 2:
            idx[py, px] = 3
    return idx.astype(np.uint8)


# --- walls (WALL: 0 ink, 1 purple, 2 lilac, 3 pale) ------------------------------------------------------------------
def wall_top_shade(seed: int, h: int = 16) -> np.ndarray:
    x, y = grid()
    n = vnoise(16, 4, seed) - 0.5
    return 0.95 + 0.85 * np.clip(1.0 - (x + y * 16 / h) / 24.0, 0, 1) + n * 0.3


def wall_top(seed: int) -> np.ndarray:
    v = bevel(wall_top_shade(seed), 3.0, 0.0, inner=False)
    idx = dither(v)
    # two screw heads, like the slabs' cousins
    idx[3, 3] = 0
    idx[12, 12] = 0
    idx[3, 12] = 1
    return idx.astype(np.uint8)


def wall_front(seed: int, face: str = "grille") -> np.ndarray:
    """a raised block seen in 3/4: its top (rows 0-6), a lit ledge, then the front face (rows 8-15)"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    top = bevel(wall_top_shade(seed, 7), 3.0, 0.0, inner=False)
    v = np.where(yi <= 6, top, 0.0)
    v[6, :] = 2.0
    v[7, :] = 3.0  # the ledge catches the light
    v[7, 15] = 2.0
    front = yi >= 8
    stripe = np.where(xi % 3 == 0, 0.2, 1.4) - (yi - 8) * 0.08
    v = np.where(front, stripe, v)
    v = np.where(front & (yi == 15), 0.0, v)
    v = np.where(front & (xi == 0), 1.6, v)
    if face == "vent":  # an air vent: a pale frame round dark slats
        box = (xi >= 3) & (xi <= 12) & (yi >= 9) & (yi <= 14)
        v = np.where(box, np.where(yi % 2 == 0, 0.0, 2.0), v)
        v = np.where(box & ((xi == 3) | (xi == 12) | (yi == 9) | (yi == 14)), 3.0, v)
    elif face == "notice":  # a pinned memo, scrawled
        memo = (xi >= 5) & (xi <= 11) & (yi >= 9) & (yi <= 14)
        v = np.where(memo, 3.0, v)
        v = np.where(memo & (yi % 2 == 1) & (xi >= 6) & (xi <= 10) & ((xi + yi) % 4 != 0), 1.0, v)
        v = np.where((xi == 8) & (yi == 9), 0.0, v)  # the pin
        v = np.where(memo & (xi == 11) & (yi == 14), 2.0, v)  # curled corner
    elif face == "switch":  # a light switch and a cable run
        v = np.where((xi >= 10) & (xi <= 12) & (yi >= 9) & (yi <= 12), 3.0, v)
        v = np.where((xi == 11) & (yi == 10), 0.0, v)
        v = np.where((xi == 11) & (yi >= 13) & (yi <= 14), 2.0, v)
    idx = dither(v)
    return idx.astype(np.uint8)


def drywall(seed: int, cracked: bool = False) -> np.ndarray:
    """a free-standing partition panel: a lilac lip on top, a pale board face with a seam and screws"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    v = np.where(yi <= 3, 1.6 + (3 - yi) * 0.25, 2.55 - (x + y) / 40 + (vnoise(16, 4, seed) - 0.5) * 0.5)
    v = np.where(yi == 0, 3.0, v)
    v = np.where(yi == 4, 0.0, v)  # the lip's shadow line
    v = np.where((xi == 0) & (yi > 4), 3.0, v)
    v = np.where((xi == 15) | (yi == 15), 0.0, v)
    v = np.where((xi == 14) & (yi > 4) & (yi < 15), 1.0, v)
    v = np.where((yi == 10) & (xi > 0) & (xi < 14), 1.6, v)  # panel seam
    for sx in (3, 11):  # screws
        v = np.where((xi == sx) & ((yi == 7) | (yi == 13)), 0.0, v)
    if cracked:
        pts = [(12, 5), (10, 7), (11, 9), (8, 10), (9, 12), (6, 14)]
        crack = np.zeros((16, 16), bool)
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            for t in np.linspace(0, 1, 10):
                crack[round(ay + (by - ay) * t), round(ax + (bx - ax) * t)] = True
        crack[11:13, 4:6] = True
        crack[7, 6:8] = True
        v = np.where(crack, 0.0, v)
        v = np.where(np.roll(crack, 1, axis=1) & ~crack & (yi > 4), 3.0, v)
    return dither(v).astype(np.uint8)


def gate(seed: int) -> np.ndarray:
    """a rolling shutter, down: pale-lipped slats between dark posts"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    v = np.where(yi % 3 == 0, 3.0, np.where(yi % 3 == 1, 1.8, 1.1)) - x / 40
    v = np.where((xi <= 1) | (xi >= 14), np.where(xi % 2 == 0, 0.0, 1.0), v)
    v = np.where(yi == 15, 0.0, v)
    v = np.where((xi >= 7) & (xi <= 8) & (yi == 13), 0.0, v)  # the handle
    return dither(v).astype(np.uint8)


# --- the void (VOID: 0 deepest, 1 deep, 2 grid / underside, 3 stars) ------------------------------------------------
def void(seed: int, stars: int = 1, under: bool = False, outline_: bool = False) -> np.ndarray:
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    n = vnoise(16, 8, seed, period=2)
    thr = np.tile(BA.BAYER, (4, 4))
    idx = np.where(thr < 0.12 + n * 0.22, 1, 0)
    # a ceiling-tile grid, far below: lines along each cell's top and left edges, broken by the dither
    gridline = (xi == 0) | (yi == 0)
    idx = np.where(gridline & ((xi + yi) % 2 == 0), 1, idx)
    idx = np.where((xi == 0) & (yi == 0), 2, idx)
    for k, (px, py) in enumerate(rng_points(seed, stars, 2, 13)):
        idx[py, px] = 3 if k == 0 else 2
        if k == 0 and hsh(k, 7, seed) > 0.5:  # a twinkle cross
            for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                idx[py + ddy, px + ddx] = max(idx[py + ddy, px + ddx], 1)
    if under:  # the underside of the ground above: a purple lip dithering away into the dark
        for r in range(7):
            t = 1.0 - r / 7.0
            row = np.where(thr[r] < t * 0.95, 2, np.where(thr[r] < t * 1.4, 1, idx[r]))
            idx[r] = row
        idx[0, :] = 2
        idx[1, :] = np.where(xi[1] % 4 == 1, 1, 2)
    if outline_:  # the landing outline: where a card is about to land
        edge = ((xi == 1) | (xi == 14) | (yi == 1) | (yi == 14)) & (xi >= 1) & (xi <= 14) & (yi >= 1) & (yi <= 14)
        idx = np.where(edge & ((xi + yi) % 3 != 0), 2, idx)
        for cx, cy in ((1, 1), (14, 1), (1, 14), (14, 14)):
            idx[cy, cx] = 3
    return idx.astype(np.uint8)


# B2: the slab's side faces. A void cell beside ground shows the ground's edge going down into the dark on that side:
# a 4-pixel purple lip dithering away (the underside, VOID_UNDER, is 7 rows: the side is seen more obliquely). The
# cards are drawn for ground on the left; ground on the right is the same tiles X-flipped by the map attribute.
#   VOID_SIDE    ground beside the cell
#   VOID_CORNER  ground above and beside (an inside corner): the underside's lip and the side's together
#   VOID_TIP     ground only diagonally above (an outside corner): the side of the underside's lip, so the slab's
#                bottom corner is not notched
# All three are drawn on one void, so their plain quarters are the same tiles: 8 tiles for the three.
SIDE_W = 4
EDGE_SEED = 22


def side_lip(idx: np.ndarray, rows: int = 16) -> np.ndarray:
    thr = np.tile(BA.BAYER, (4, 4))
    for r in range(rows):
        fade = 1.0 if rows == 16 else 1.0 - r / (rows + 1.0)  # the tip fades down with the underside it belongs to
        for c in range(SIDE_W):
            t = (1.0 - c / SIDE_W) * fade
            lip = 2 if thr[r, c] < t * 0.95 else (1 if thr[r, c] < t * 1.4 else 0)
            idx[r, c] = max(idx[r, c], lip)  # over the corner's underside, the brighter of the two
        if fade > 0.5:
            idx[r, 0] = 2
            if idx[r, 1] < 2:
                idx[r, 1] = 1 if r % 4 == 1 else 2
    return idx


def void_edge(kind: str) -> np.ndarray:
    idx = void(EDGE_SEED, 0, under=kind == "corner").copy()
    return side_lip(idx, 7 if kind == "tip" else 16).astype(np.uint8)


# --- the egress door (HUD: 0 ink, 1 pink, 2 accent, 3 white) ---------------------------------------------------------
def door(open_: bool) -> np.ndarray:
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    v = np.zeros((16, 16), np.float32)
    # the EXIT sign: four tiny letters in the accent colour
    letters = ["111.1.1.111.111", "11...1...1...1.", "111.1.1.111..1."]
    for r, row in enumerate(letters):
        for c, ch in enumerate(row):
            if ch == "1":
                v[r + 1, c] = 2.0 if open_ else 1.0
    frame = (xi >= 2) & (xi <= 13) & (yi >= 5)
    inner = (xi >= 3) & (xi <= 12) & (yi >= 6)
    v = np.where(frame & ~inner, 1.0, v)
    v = np.where(frame & ~inner & ((xi == 2) | (yi == 5)), 3.0 if open_ else 1.0, v)
    if open_:  # light pours out of the doorway, brightest low down
        g = np.clip((yi - 5) / 10.0, 0, 1)
        v = np.where(inner, 1.6 + g * 1.5, v)
        v = np.where(inner & (xi >= 6) & (xi <= 9), 3.0, v)
    else:  # dark, with a neon tube humming down the middle
        v = np.where(inner & ((xi == 7) | (xi == 8)) & (yi >= 7) & (yi <= 14), 1.0, v)
        v = np.where(inner & (xi == 7) & (yi >= 8) & (yi <= 13), 3.0, v)
        v = np.where(inner & (yi == 15), 0.0, v)
    return dither(v).astype(np.uint8)


# --- objects (OBJ: 0 shadow / outline, 1 slab base, 2 accent (amber), 3 light) ---------------------------------------
def floor_under(seed: int, light3: bool = True) -> np.ndarray:
    """the slab under an object, in a palette that has the slab's base (1) and highlight (3) but not its light: the
    base with a sparse highlight dither toward the lit corner, and the bevel"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    thr = np.tile(BA.BAYER, (4, 4))
    lit = np.clip(0.55 - (x + y) / 26.0, 0, 1) * 0.35
    idx = np.where(thr < lit, 3 if light3 else 1, 1).astype(np.uint8)
    idx[0, :] = 3 if light3 else 1
    idx[:, 0] = 3 if light3 else 1
    idx[15, :] = 0
    idx[:, 15] = 0
    return idx


CRT = """
................
.00000000000000.
.03333333333310.
.03000000000110.
.03022222220110.
.03022222220110.
.03022222220110.
.03022222220110.
.03022222220110.
.03000000000110.
.01111111112110.
.00000000000000.
.00000000000000.
....00000000....
....00000000....
................"""


def crt(seed: int, screen: str) -> np.ndarray:
    """an amber-phosphor office terminal: a dark slate case (ink dithered with the slab base, lit along the top), a
    glowing amber screen with dark text, a foot"""
    idx = from_template(CRT, seed)
    for yy in range(2, 12):  # the case: slate, ink and base in a checker, darker toward the lower right
        for xx in range(2, 14):
            if idx[yy, xx] == 1 and not (yy == 10 and xx < 12) and (xx + yy) % 2 == 0 and xx + yy > 12:
                idx[yy, xx] = 0
    for xx in range(4, 11):  # one bright scanline rolling down the glass
        if xx % 2 == 0:
            idx[6, xx] = 3
    if screen == "cursor":
        idx[5, 5:10] = [0, 0, 2, 0, 0]
        idx[8, 5:8] = 0
        idx[8, 9] = 0
    else:  # a face in the static
        idx[5, 6] = 0
        idx[5, 8] = 0
        idx[7, 6:9] = 0
    idx[4, 4] = 3  # glare
    idx[4, 5] = 3
    idx[13, 5:11] = 1  # the foot's lit edge
    return idx.astype(np.uint8)


def token(seed: int) -> np.ndarray:
    """a gold arcade token: a disc with a bright rim on the lit side, a white diamond stamped in the middle"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    d = np.hypot(x - 8, y - 7.5)
    idx = floor_under(seed)
    coin = d <= 5.2
    light = (x - 8) + (y - 7.5)
    idx = np.where(coin, 2, idx)
    idx = np.where(coin & (d > 3.8) & (light < -2.0), 3, idx)
    idx = np.where(coin & (d > 3.6) & (light > 3.0) & ((xi + yi) % 2 == 0), 0, idx)
    dia = (np.abs(x - 8) + np.abs(y - 7.5)) <= 2.2
    idx = np.where(dia, 3, idx)
    idx = np.where(outline(coin), 0, idx)
    sh = (np.hypot(x - 9.5, (y - 14.3) * 2.4) < 4.6) & ~coin & ~outline(coin)
    idx = np.where(sh & ((xi + yi) % 2 == 0), 0, idx)
    return idx.astype(np.uint8)


def keycard(seed: int) -> np.ndarray:
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    idx = floor_under(seed)
    card = (xi >= 3) & (xi <= 12) & (yi >= 4) & (yi <= 13)
    card &= ~(((xi == 3) | (xi == 12)) & ((yi == 4) | (yi == 13)))
    idx = np.where(card, 3, idx)
    idx = np.where(card & (yi >= 6) & (yi <= 7), 2, idx)  # the amber stripe
    idx = np.where(card & (xi >= 5) & (xi <= 7) & (yi >= 9) & (yi <= 11), 2, idx)  # chip
    idx = np.where(card & (xi == 6) & (yi == 10), 0, idx)
    idx = np.where(card & (xi >= 9) & (xi <= 11) & ((yi == 9) | (yi == 11)), 1, idx)  # printed lines
    idx = np.where(card & (xi == 12) & (yi > 4), 1, idx)
    idx = np.where(card & (yi == 13) & (xi > 3), 1, idx)
    clip = (xi >= 6) & (xi <= 9) & (yi >= 1) & (yi <= 4)
    idx = np.where(clip, np.where((xi == 7) | (xi == 8), 1, 0), idx)
    idx = np.where(clip & (yi == 1), 0, idx)
    idx = np.where(outline(card) & ~clip, 0, idx)
    return idx.astype(np.uint8)


def from_template(rows: str, seed: int, light3: bool = True) -> np.ndarray:
    """a 16x16 template ('.' = the slab under it, '0'-'3' palette indices) over the object slab"""
    idx = floor_under(seed, light3)
    for yy, r in enumerate(rows.strip("\n").split("\n")):
        for xx, ch in enumerate(r[:16]):
            if ch in "0123":
                idx[yy, xx] = int(ch)
    return idx


BENCH = """
................
...000....000...
...030....030...
..000000000000..
..033333333330..
..032323232320..
..022222222220..
..000000000000..
..020........20.
..020.000000.20.
..020.033330.20.
..020.022220.20.
..020.000000.20.
..020........20.
..000........00.
................"""
CRATE = """
................
.00000000000000.
.03333333333320.
.03222222222220.
.03232323232320.
.03223232323220.
.03232023232320.
.00000000000000.
.03333333333320.
.03222222222220.
.03232323232320.
.03223232323220.
.03232023232320.
.02222222222220.
.00000000000000.
................"""
MUG = """
................
......3...3.....
.....3...3......
......3...3.....
................
...0000000000...
...033333333000.
...022222223..0.
...022222223..0.
...033333333..0.
...03333333300..
...03333333300..
...0333333330...
....00000000....
.....0....0.....
................"""
BATTERY = """
................
......0000......
......0330......
...0000000000...
...0333333110...
...0311111110...
...0111221110...
...0112221110...
...0122222210...
...0111222110...
...0111221110...
...0222222220...
...0222222220...
...0000000000...
....0......0....
................"""
BOTTLE = """
................
......0000......
......0330......
......0220......
....00022000....
...0333332220...
...0322222220...
...0322222220...
...0322232220...
...0322232220...
...0322222220...
...0332222220...
....00000000....
....03333330....
....00000000....
................"""
BURNER = """
................
.......3........
......323.......
.....32223......
....3220223.....
....3200023.....
...320000023....
...320000023....
....32000223....
.....322223.....
...0000000000...
..022222222220..
..033333333330..
...0000000000...
....0......0....
................"""
SPROUT = """
................
.......33.......
......3223......
.....322223.....
......3223......
....3..00..3....
...323.00.323...
....3220022.....
.......00.......
.......00.......
.....000000.....
....02222220....
....02333320....
....02222220....
.....000000.....
................"""


def wet(seed: int) -> np.ndarray:
    """a puddle on the slab (REACT_A: 0 deep, 1 slab base, 2 water, 3 sheen)"""
    x, y = grid()
    idx = floor_under(seed, False)
    n = vnoise(16, 5, seed + 3)
    pud = (np.hypot((x - 8) / 6.5, (y - 8.5) / 5.0) + (n - 0.5) * 0.5) < 1.0
    v = 2.1 + (y - 8) / 16 + (n - 0.5)
    pidx = dither(np.clip(v, 1.4, 3.0), 1, 3)
    pidx = np.where(pidx == 1, 2, pidx)
    idx = np.where(pud, pidx, idx)
    idx = np.where(outline(pud), 0, idx)
    idx[6, 5:8] = 3
    idx[7, 4] = 3
    return idx.astype(np.uint8)


def ice(seed: int) -> np.ndarray:
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    v = 2.3 + 0.6 * np.clip(1 - (x + y) / 24, 0, 1) + (vnoise(16, 4, seed) - 0.5) * 0.4
    idx = dither(np.clip(v, 2, 3), 2, 3)
    streak = ((xi - yi) % 7 == 0) & (xi > 2)
    idx = np.where(streak, 3, idx)
    idx[0, :] = 3
    idx[:, 0] = 3
    idx[15, :] = 0
    idx[:, 15] = 0
    idx[14, 1:15] = 1
    return idx.astype(np.uint8)


def burn(seed: int, phase: int) -> np.ndarray:
    """the slab on fire (REACT_B: 0 ember, 1 slab base, 2 flame, 3 core)"""
    x, y = grid()
    idx = floor_under(seed, False)
    n = vnoise(16, 3, seed + phase * 17)
    flick = 0.5 + 0.5 * np.sin(x * 1.3 + phase * 2.1)
    hgt = 4 + 6 * flick + n * 3
    fire = (y > 16 - hgt) & (x > 1) & (x < 15)
    v = 1.6 + (y - (16 - hgt)) / hgt * 1.6 + (n - 0.5)
    fidx = dither(np.clip(v, 1.2, 3.0), 1, 3)
    fidx = np.where(fidx == 1, 0, fidx)
    idx = np.where(fire, fidx, idx)
    return idx.astype(np.uint8)


# --- fragile floors (the puzzle workstream's rules; docs/redesign.md): worn carpet (3 steps), a ceiling tile laid as
# floor (2 steps), glass over the void (1 step), glitched glass (on a beat: solid, flicker, gone), and the pit a break
# leaves. Each stage reads at 1x: hairline, split, then the fall ----------------------------------------------------
def line_px(pts: list[tuple[int, int]]) -> list[tuple[int, int]]:
    out = []
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        n = max(abs(bx - ax), abs(by - ay), 1)
        for t in range(n + 1):
            q = (round(ax + (bx - ax) * t / n), round(ay + (by - ay) * t / n))
            if q not in out:
                out.append(q)
    return out


def carpet_worn(seed: int, stage: int) -> np.ndarray:
    """a worn carpet tile (FLOOR): sunk into the floor (the bevel turned inward: shadowed top and left, lit bottom and
    right), matte and tufted, a threadbare patch, frayed corners; stage 1 a hairline, stage 2 a split with the halves
    sagging apart"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    tuft = ((xi // 2 + yi // 2) % 2 == 0)
    v = 1.0 + 0.5 * np.clip(1.0 - (x + y) / 26.0, 0, 1) + np.where(tuft, 0.7, 0.0)
    bald = np.hypot(x - 10.0, y - 6.5) < 3.2  # the threadbare patch: the weave gone flat
    v = np.where(bald, 1.2 + (vnoise(16, 4, seed) - 0.5) * 0.3, v)
    v[0, :] = 0.0  # sunk: the rim's shadow falls inside along the top and left
    v[:, 0] = 0.0
    v[1, 1:15] = np.minimum(v[1, 1:15], 1.0)
    v[1:15, 1] = np.minimum(v[1:15, 1], 1.0)
    v[15, :] = 3.0
    v[:, 15] = 3.0
    v[14, 1:15] = np.maximum(v[14, 1:15], 2.0)
    v[1:15, 14] = np.maximum(v[1:15, 14], 2.0)
    idx = dither(v)
    for (px, py) in ((11, 5), (9, 8), (12, 7)):  # loose threads in the bald patch
        idx[py, px] = 0
    for (px, py) in ((14, 14), (13, 15), (15, 2)):  # frayed edges
        idx[py, px] = 1
    if stage >= 1:  # the hairline: one dark pixel wide, a light lip under it
        path = line_px([(2, 9), (5, 8), (7, 9), (10, 7), (13, 8)])
        for (px, py) in path:
            idx[py, px] = 0
            if stage == 1 and py + 1 < 15 and idx[py + 1, px] != 0:
                idx[py + 1, px] = 3
    if stage >= 2:  # the split: the hairline opens into a gap, a branch runs out, the lower half sags into shadow
        path = line_px([(0, 10), (2, 9), (5, 8), (7, 9), (10, 7), (13, 8), (15, 7)])
        for (px, py) in path:
            idx[py, px] = 0
            if py + 1 < 16:
                idx[py + 1, px] = 0
            if py - 1 >= 0 and idx[py - 1, px] != 0:
                idx[py - 1, px] = 3
        for (px, py) in line_px([(7, 10), (6, 12), (8, 14), (7, 15)]):
            idx[py, px] = 0
        lower = np.zeros((16, 16), bool)
        for (px, py) in path:
            lower[py + 2:, px] = True
        shade = lower & ((xi + yi) % 2 == 0) & (idx == 2)
        idx = np.where(shade, 1, idx)
        idx = np.where(lower & (idx == 3) & (yi > 11), 2, idx)
    return idx.astype(np.uint8)


def ceiling_tile(seed: int, stage: int) -> np.ndarray:
    """an acoustic ceiling tile laid as floor (WALL: 0 deep, 1 dark, 2 mid, 3 pale): a pale fissured face in its
    T-bar, an old water stain; stage 1 cracked through with a corner sagging"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    v = 3.0 - np.clip((x + y - 20) / 10.0, 0, 1) * 0.5 + (vnoise(16, 4, seed) - 0.5) * 0.15
    idx = dither(np.clip(v, 2, 3), 2, 3)
    # the acoustic pinholes, in staggered rows, and a few fissures
    pin = ((yi % 3) == 1) & (((xi + (yi // 3) * 2) % 4) == 1) & (xi > 1) & (xi < 14) & (yi < 14)
    idx = np.where(pin, 1, idx)
    for k in range(2):
        px = 3 + int(hsh(k, 3, seed) * 9)
        py = 2 + int(hsh(k, 4, seed) * 10)
        idx[py, px] = 1
        idx[py, px + 1] = 1
    d = np.hypot(x - 5.0, y - 10.5)  # a water stain ring
    ring = (d > 2.4) & (d < 3.3) & ((xi + yi) % 2 == 0)
    idx = np.where(ring, 2, idx)
    # the T-bar it sits in
    idx[0, :] = 3
    idx[:, 0] = 3
    idx[15, :] = 0
    idx[:, 15] = 0
    idx[14, 1:15] = 1
    idx[1:15, 14] = 1
    idx[1, 1:14] = np.where(idx[1, 1:14] == 3, 2, idx[1, 1:14])
    idx[15, 0] = idx[0, 15] = 1
    if stage >= 1:  # cracked through: a corner breaks and drops a little, the void shows in the gap
        path = line_px([(15, 5), (12, 7), (11, 10), (8, 12), (6, 15)])
        corner = np.zeros((16, 16), bool)
        for (px, py) in path:
            corner[py, px + 1:] = True
        for (px, py) in path:
            idx[py, px] = 0
            if px - 1 >= 0 and idx[py, px - 1] >= 2:
                idx[py, px - 1] = 1
        sag = corner & ~np.isin(idx, [0])
        idx = np.where(sag & (idx == 3), 2, idx)
        idx = np.where(sag & (idx == 2) & ((xi + yi) % 2 == 1), 1, idx)
        for (px, py) in path[1:]:  # the gap: the dropped corner shifted down a pixel
            if py + 1 < 16:
                idx[py + 1, px] = 0
        for (px, py) in line_px([(12, 7), (9, 6), (7, 3)]):  # and a hairline running back into the face
            idx[py, px] = 1
    return idx.astype(np.uint8)


def glass(seed: int, kind: str = "plain") -> np.ndarray:
    """a glass floor panel over the void (VOID: 0 deepest, 1 deep, 2 frame / sheen, 3 glint): the void and its
    far grid show through, a thin frame, two diagonal glints and a dithered sheen"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    idx = void(seed, 1)
    thr = np.tile(BA.BAYER, (4, 4))
    sheen = np.clip(1.0 - (x + y) / 22.0, 0, 1)
    idx = np.where((thr < sheen * 0.4) & (idx == 0), 1, idx)  # the pane's faint sheen, dithered over the dark
    dd = x + y
    span = (np.abs(x - y) < 9)  # the glints stop short of the corners
    band = (np.abs(dd - 11) < 2.2) & span
    idx = np.where(band & (thr < 0.6), 2, idx)
    idx = np.where((np.abs(dd - 11) < 0.6) & span, 3, idx)  # the long glint, crisp
    band2 = (np.abs(dd - 16) < 1.0) & (np.abs(x - y) < 5)
    idx = np.where(band2, np.where((xi % 2) == 0, 3, 2), idx)
    idx = np.where((np.abs(dd - 24) < 0.6) & (np.abs(x - y) < 3), 2, idx)  # a glint low right, faint
    idx[0, 1:15] = 3
    idx[1:15, 0] = 3
    idx[1, 1:14] = 2
    idx[1:14, 1] = 2
    idx[15, 1:16] = 2  # the frame's far sides, in shade
    idx[1:16, 15] = 2
    idx[14, 2:14] = np.where(xi[14, 2:14] % 2 == 0, 1, idx[14, 2:14])
    idx[0, 0] = idx[0, 15] = idx[15, 0] = 3
    idx[1, 1] = 3
    if kind == "glitch":  # torn: rows slid sideways, a doubled frame, scan dashes
        for r0, r1, sh in ((3, 5, 3), (9, 11, -4), (12, 13, 2)):
            idx[r0:r1 + 1] = np.roll(idx[r0:r1 + 1], sh, axis=1)
        idx[6, 2:14] = np.where(xi[6, 2:14] % 3 == 0, 3, np.where(xi[6, 2:14] % 3 == 1, 2, 0))
        idx[1:15, 1] = np.where(yi[1:15, 1] % 2 == 0, 3, idx[1:15, 1])
        idx[10, 13:16] = 3
        idx[4, 0:2] = 0
    elif kind == "flicker":  # half there: every other line, the rest dropped to the void
        g = glass(seed, "glitch")
        vd = void(seed + 3, 0)
        idx = np.where(yi % 2 == 0, np.roll(g, 1, axis=1), vd)
        idx[7:9, 3:9] = np.where((xi[7:9, 3:9] + yi[7:9, 3:9]) % 2 == 0, 2, 0)
    elif kind == "gone":  # only the void, and the corners of where it was
        idx = void(seed + 5, 1)
        for cx, cy, sx, sy in ((1, 1, 1, 1), (14, 1, -1, 1), (1, 14, 1, -1), (14, 14, -1, -1)):
            idx[cy, cx] = 2
            idx[cy, cx + sx] = 2
            idx[cy + sy, cx] = 2
        idx[1, 5] = idx[1, 10] = 1
        idx[14, 7] = 1
        idx[8, 6] = 3
    return idx.astype(np.uint8)


def pit(seed: int) -> np.ndarray:
    """the hole a break leaves (VOID): the void deep inside, the torn underside of the floor along the top, ragged
    edges down the sides, threads hanging"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    idx = void(seed, 1)
    idx = np.where(idx == 1, np.where((xi + yi) % 3 == 0, 1, 0), idx)
    thr = np.tile(BA.BAYER, (4, 4))
    depth = np.array([5, 6, 5, 4, 6, 7, 5, 4, 5, 6, 4, 5, 6, 5, 4, 5])  # the torn lip's depth, column by column
    for cx in range(16):
        d = depth[(cx + seed) % 16]
        for r in range(d):
            t = 1.0 - r / d
            idx[r, cx] = 2 if thr[r % 4, cx % 4] < t * 1.05 else (1 if thr[r % 4, cx % 4] < t * 1.5 else idx[r, cx])
        idx[0, cx] = 3 if cx % 5 == 2 else 2
    for cx in (3, 9, 12):  # threads hanging off the torn edge
        d = depth[(cx + seed) % 16]
        idx[d:d + 2, cx] = 2
        idx[d + 2, cx] = 1
    for cy in range(16):  # ragged sides
        w = 1 + int(hsh(cy, 1, seed) * 2)
        idx[cy, :w] = np.where(idx[cy, :w] < 2, 2 if cy % 3 else 1, idx[cy, :w])
        w = 1 + int(hsh(cy, 2, seed) * 2)
        idx[cy, 16 - w:] = np.where(idx[cy, 16 - w:] < 2, 1, idx[cy, 16 - w:])
    idx[15, :] = np.where(idx[15, :] < 2, 0, idx[15, :])
    return idx.astype(np.uint8)


def shard(variant: int) -> np.ndarray:
    """an 8x16 sprite of falling glass (VOID: 0 clear, 1 deep, 2 sheen, 3 glint): slivers, dithered"""
    o = np.zeros((16, 8), np.uint8)
    if variant == 0:
        tris = [((0, 1), (6, 0), (2, 6)), ((4, 9), (7, 8), (5, 14)), ((1, 11), (3, 10), (2, 13))]
    else:
        tris = [((1, 0), (7, 3), (3, 5)), ((0, 8), (4, 7), (1, 12)), ((5, 11), (7, 12), (6, 15))]
    ys, xs = np.mgrid[0:16, 0:8]
    for (ax, ay), (bx, by), (cx, cy) in tris:
        def side(px, py, qx, qy, rx, ry):
            return (px - rx) * (qy - ry) - (qx - rx) * (py - ry)
        d1 = side(xs, ys, ax, ay, bx, by)
        d2 = side(xs, ys, bx, by, cx, cy)
        d3 = side(xs, ys, cx, cy, ax, ay)
        inside = ~(((d1 < 0) | (d2 < 0) | (d3 < 0)) & ((d1 > 0) | (d2 > 0) | (d3 > 0)))
        fill = np.where((xs + ys) % 2 == 0, 2, 1)
        o = np.where(inside, fill, o)
        for (px, py) in line_px([(ax, ay), (bx, by)]):
            if 0 <= py < 16 and 0 <= px < 8:
                o[py, px] = 3
    return o


# --- the cards: (name, palette, builder, what it is) ----------------------------------------------------------------
METAS: list[tuple[str, int, object]] = [
    ("SLAB_A", PAL_FLOOR, lambda: slab(1)),
    ("SLAB_B", PAL_FLOOR, lambda: slab(2, "scuff")),
    ("SLAB_C", PAL_FLOOR, lambda: slab(3, "seam")),
    ("SLAB_D", PAL_FLOOR, lambda: slab(4, "stain")),
    ("SLAB_CRACK", PAL_FLOOR, lambda: slab(1, "crack")),
    ("CRUMBLE", PAL_FLOOR, lambda: slab(5, "crumble")),
    ("PLATE", PAL_FLOOR, lambda: slab(6, "plate")),
    ("GATE_OPEN", PAL_FLOOR, lambda: slab(7, "gate_open")),
    ("FOAM_A", PAL_FOAM, lambda: foam(8, 2, 0)),
    ("FOAM_B", PAL_FOAM, lambda: foam(9, 2, 3)),
    ("WALL_TOP", PAL_WALL, lambda: wall_top(10)),
    ("WALL_FRONT", PAL_WALL, lambda: wall_front(11)),
    ("WALL_VENT", PAL_WALL, lambda: wall_front(12, "vent")),
    ("WALL_NOTE", PAL_WALL, lambda: wall_front(13, "notice")),
    ("WALL_SWITCH", PAL_WALL, lambda: wall_front(14, "switch")),
    ("DRY", PAL_WALL, lambda: drywall(15)),
    ("DRY_CRACK", PAL_WALL, lambda: drywall(15, cracked=True)),
    ("GATE", PAL_WALL, lambda: gate(16)),
    ("VOID_A", PAL_VOID, lambda: void(17, 1)),
    ("VOID_B", PAL_VOID, lambda: void(18, 2)),
    ("VOID_C", PAL_VOID, lambda: void(19, 0)),
    ("VOID_UNDER", PAL_VOID, lambda: void(20, 1, under=True)),
    ("VOID_OUTLINE", PAL_VOID, lambda: void(21, 0, outline_=True)),
    ("DOOR_SHUT", PAL_HUD, lambda: door(False)),
    ("DOOR_OPEN", PAL_HUD, lambda: door(True)),
    ("CRT_A", PAL_OBJ, lambda: crt(22, "cursor")),
    ("CRT_B", PAL_OBJ, lambda: crt(23, "face")),
    ("TOKEN", PAL_OBJ, lambda: token(24)),
    ("KEY", PAL_OBJ, lambda: keycard(25)),
    ("BENCH", PAL_OBJ, lambda: from_template(BENCH, 26)),
    ("CRATE", PAL_OBJ, lambda: from_template(CRATE, 27)),
    ("MUG", PAL_OBJ, lambda: from_template(MUG, 28)),
    ("BATTERY", PAL_OBJ, lambda: from_template(BATTERY, 29)),
    ("BOTTLE", PAL_REACT_A, lambda: from_template(BOTTLE, 30, False)),
    ("BURNER", PAL_REACT_B, lambda: from_template(BURNER, 31, False)),
    ("SPROUT", PAL_FOAM, lambda: from_template(SPROUT, 32, False)),
]
# placeholders in the reserved reaction range (the puzzle workstream replaces and extends them)
REACTS: list[tuple[str, int, object]] = [
    ("WET", PAL_REACT_A, lambda: wet(40)),
    ("ICE", PAL_REACT_A, lambda: ice(41)),
    ("BURN_A", PAL_REACT_B, lambda: burn(42, 0)),
    ("BURN_B", PAL_REACT_B, lambda: burn(42, 1)),
]


# --- pieces lying in a room (src/puzzle.c, src/core/eg_piece.h): one card per piece, an office thing that carries the
# piece's element, lifted off the slab with a soft dithered shadow under it and a glint, so it reads as a pick-up ---
ITEM_ART: list[tuple[str, int, str]] = [
    ("FIRE", PAL_REACT_B, """
................
.......0........
......020.......
......0220......
.....02320......
.....023320.....
....02333320....
....02333320....
.....023320.....
......0000......
....00000000....
...0333333330...
....02222220....
.....000000.....
................
................"""),  # a tealight: the flame, the wax, the cup
    ("WATER", PAL_REACT_A, """
................
......0000......
......0330......
.....002200.....
....03333220....
....03222220....
....03222220....
....03223220....
....03223220....
....03222220....
....03322220....
....00000000....
....02333320....
....00000000....
................
................"""),  # the water cooler's jug
    ("AIR", PAL_FOAM, """
................
.....000000.....
....03333330....
...0322032230...
...0322332230...
...0300303330...
...0333000220...
...0322332230...
...0322302230...
....03333330....
.....000000.....
.......00.......
.....003300.....
....00000000....
................
................"""),  # a desk fan: the cage, the blades, the stand
    ("EARTH", PAL_FOAM, """
................
.......03.......
....0..030..0...
...030.0330030..
...0330323033...
....03332330....
.....033230.....
......0330......
....00000000....
....03333330....
.....022220.....
.....022220.....
.....022220.....
......0000......
................
................"""),  # a potted plant: the leaves, the pot's rim, the soil-dark pot
    ("METAL", PAL_FLOOR, """
................
..........000...
.........0330...
........0320.0..
........0320030.
.......0332230..
......03330000..
.....03330......
....03330.......
...03330........
..03330.........
..0330..........
...00...........
................
................
................"""),  # a wrench, polished
    ("ENERGY", PAL_OBJ, """
................
.....033330.....
....00000000....
....01111110....
....00002000....
....00022000....
....00222000....
....02222220....
....00002200....
....00022000....
....00020000....
....01111110....
....00000000....
.....000000.....
................
................"""),  # an energy-drink can: a gold bolt on the dark can
    ("RAIN", PAL_REACT_A, """
................
................
.0..............
..0.......000...
.3.0.....03330..
..3.0...0333220.
.3...0.03222220.
......032222220.
......032222220.
......033222220.
.......0000000..
................
................
................
................
................"""),  # a watering can, pouring
    ("MUD", PAL_OBJ, """
................
......0..0......
.......0..0.....
......0..0......
....00000000....
....03000030....
....03333330000.
....0333333303.0
....0322222203.0
....0333333300..
....0333333330..
....0333333330..
.....00000000...
................
................
................"""),  # a mug of coffee, steaming: dark to the brim
    ("SNOW", PAL_REACT_A, """
................
.....000000.....
....03332330....
...0323333230...
...0333332330...
...0332333330...
...0333233230...
....03333330....
.....000000.....
....02222220....
....00000000....
....02222220....
....00000000....
................
................
................"""),  # a snow globe
    ("ICE", PAL_REACT_A, """
................
................
.....0000000....
....03333330....
...0333333320...
...0000000320...
...0333320220...
...0322220220...
...0323320220...
...0322220200...
...0322220.00...
...0222220......
...0000000......
................
................
................"""),  # an ice cube
    ("BRICK", PAL_REACT_B, """
................
................
................
................
.....0000000000.
....03333333330.
...0333303333.0.
..00000000000220
..02222222220220
..02200222220220
..02222222002200
..02222222222000
..0000000000000.
................
................
................"""),  # a brick: the lit top, the dark face, its holes
    ("ELECTRIC", PAL_OBJ, """
................
..2.....0.0.....
...2....0.0.....
.2.2..00000000..
......03333330..
..2...03333330..
.2.2..03300330..
......03333330..
.......033330...
........0330....
........0..0....
.......0...0....
......0.....00..
................
................
................"""),  # a plug, sparking
    ("STEAM", PAL_REACT_A, """
..........3.....
.........3.3....
..........3.3...
.........3......
.....00000......
....0333330.000.
...033333330..0.
...03222222000..
...0322222220...
...0322222220...
...0322222220...
...0222222220...
....00000000....
................
................
................"""),  # a kettle, steaming
    ("LAVA", PAL_REACT_B, """
................
......0000......
......0220......
.....022320.....
.....023220.....
....02222220....
....02332220....
....02222320....
....02232220....
.....000000.....
....02222220....
....00000000....
................
................
................
................"""),  # a lava lamp: the blobs in the glow, the base
    ("DUST", PAL_FLOOR, """
................
...........2....
...2............
.......2........
.....0.0.0......
....0202020.....
...022222220....
..02223222220...
...0222222322...
..02322222220...
...0222232220...
....0202020200..
.....0.0.0......
................
................
................"""),  # a dust bunny, and the motes over it
    ("CLOUD", PAL_FOAM, """
................
................
................
.......000......
......03330.....
...0000333300...
..03333333330...
.0333333332330..
.0333233322230..
.0322222222220..
..000000000000..
................
................
................
................
................"""),  # a small grey cloud
]


def item(rows: str, seed: int, light3: bool) -> np.ndarray:
    """an item card: the slab under it, a dithered shadow under the object (it floats a little), the object, a glint"""
    idx = floor_under(seed, light3)
    if not light3:  # a palette without the slab's shadow: the bevel's dark side in the base, no coloured seam
        idx[15, :] = 1
        idx[:, 15] = 1
    obj = np.full((16, 16), -1, np.int16)
    for yy, r in enumerate(rows.strip("\n").split("\n")[:16]):
        for xx, ch in enumerate(r[:16]):
            if ch in "0123":
                obj[yy, xx] = int(ch)
    m = obj >= 0
    ys, xs = np.nonzero(m)
    bot = ys.max()
    cx = (xs.min() + xs.max()) / 2.0
    half = max(2.5, (xs.max() - xs.min()) / 2.0 * 0.75)
    sy = min(bot + 1.6, 13.6)
    x, y = grid()
    d = ((x - 0.5 - cx) / half) ** 2 + ((y - 0.5 - sy) / 0.9) ** 2
    inside = (y < 15) & (x < 15)
    idx = np.where(inside & (d <= 0.45), 0, idx)  # a flat oval: solid in the middle, dithered at its rim
    idx = np.where(inside & (d > 0.45) & (d <= 1.0) & ((x.astype(int) + y.astype(int)) % 2 == 0), 0, idx)
    idx = np.where(m, obj, idx).astype(np.uint8)
    # the glint: a small four-point star off the object's upper right, on the slab
    gx, gy = min(int(xs.max()) + 1, 13), max(int(ys.min()) + 1, 2)
    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
        px, py = gx + dx, gy + dy
        if 1 <= px <= 14 and 1 <= py <= 14 and not m[py, px]:
            idx[py, px] = 3 if (dx, dy) == (0, 0) or light3 else 3
    return idx


def ice_cracked(seed: int) -> np.ndarray:
    """thin ice after a step (REACT_A): a star of cracks from where the foot came down, white along their lips"""
    idx = ice(seed)
    for path in ([(7, 8), (4, 5), (2, 4)], [(7, 8), (11, 4), (13, 2)], [(7, 8), (12, 10), (14, 11)],
                 [(7, 8), (5, 12), (3, 14)], [(7, 8), (8, 13)]):
        for (px, py) in line_px(path):
            if 1 <= px <= 14 and 1 <= py <= 14:
                idx[py, px] = 0
                if py + 1 <= 14 and idx[py + 1, px] != 0:
                    idx[py + 1, px] = 3
    idx[7:10, 6:9] = np.where(idx[7:10, 6:9] == 3, 1, idx[7:10, 6:9])  # the dent: a little dark under the foot
    idx[8, 7] = 0
    return idx.astype(np.uint8)


# fragile floors and the pit (the puzzle workstream owns the rules): ids follow the reaction range, each kind's stages
# consecutive so a cell shows M_<KIND>0 + steps taken. BANK0 cards live in VRAM bank 0 (tiles B0_BASE ..), where a
# sprite can show them too: they wobble and fall as themselves
FRAGILE: list[tuple[str, int, object]] = [
    ("CARPET0", PAL_FLOOR, lambda: carpet_worn(50, 0)),
    ("CARPET1", PAL_FLOOR, lambda: carpet_worn(50, 1)),
    ("CARPET2", PAL_FLOOR, lambda: carpet_worn(50, 2)),
    ("CEIL0", PAL_WALL, lambda: ceiling_tile(52, 0)),
    ("CEIL1", PAL_WALL, lambda: ceiling_tile(52, 1)),
    ("GLASS", PAL_VOID, lambda: glass(54)),
    ("GLITCH0", PAL_VOID, lambda: glass(54, "glitch")),
    ("GLITCH1", PAL_VOID, lambda: glass(54, "flicker")),
    ("GLITCH2", PAL_VOID, lambda: glass(54, "gone")),
    ("PIT", PAL_VOID, lambda: pit(56)),
    ("ICE1", PAL_REACT_A, lambda: ice_cracked(41)),  # thin ice, one step taken (M_ICE is the whole sheet)
]
FRAG_STEPS = {"CARPET": 3, "CEIL": 2, "GLASS": 1, "GLITCH": 1}
# VRAM bank 0 above the sprites (bake_art's sprite set ends at 210): the flying fragile cards, the glass shards, and
# the two cards that also fly during assembly and collapse (a floor slab, a wall top) at SPR_BASE
B0_BASE = 210
BANK0 = {"CARPET0", "CARPET1", "CARPET2", "CEIL0", "CEIL1", "GLASS", "GLITCH0", "GLITCH2"}
FIXED0 = {"SLAB_A": SPR_BASE, "WALL_TOP": SPR_BASE + 4}
# VRAM bank 0, BG-only: the floors address BG tiles signed (LCDC bit 4 clear), so BG indices 0 .. 127 with the bank
# bit clear read 0x9000 .. 0x97FF, which no sprite can see and nothing on a floor uses (only the screens' pictures,
# src/gen/art_pics.c, and they are gone before a floor's look_load). The item cards and the cracked thin ice live
# there; they never fly (an item flies in as a slab).
LOW0_BASE = 0
ICON_BASE = 112  # sixteen 8x8 inventory icons, signed BG bank 0 only

def connected_carpet(mask: int) -> np.ndarray:
    """N/E/S/W edges; sixteen cards share sixteen quadrant tiles.

    Texture repeats across card boundaries instead of beveling every cell.
    Each quadrant depends only on its two adjacent connections.
    """
    y,x=np.mgrid[0:16,0:16]
    im=np.ones((16,16),np.uint8)
    im[(x*3+y*5)%11<3]=2
    im[(x+y)%8==0]=0
    if not mask&1:im[0,:]=3;im[1,:]=2
    if not mask&2:im[:,15]=0;im[:,14]=1
    if not mask&4:im[15,:]=0;im[14,:]=1
    if not mask&8:im[:,0]=3;im[:,1]=2
    return im

def plate_down() -> np.ndarray:
    im=slab(6,'plate');im[5:12,5:12]=1
    im[5,5:12]=im[5:12,5]=0
    im[11,5:12]=im[5:12,11]=3
    im[7:10,7:10]=3
    return im

def portal(state: int) -> np.ndarray:
    """A recessed transport pad: broken outer chevrons, rotating lit aperture.

    The slab retains its bevel. Inactive pads have a closed dark iris; active
    pads open into a dithered well, with a bright sector moving around it.
    """
    im=slab(73)
    y,x=np.mgrid[0:16,0:16];dx=x-7.5;dy=y-7.5
    radius=np.maximum(np.abs(dx),np.abs(dy))
    ring=(radius>=4)&(radius<=6)&((np.abs(dx)<3)|(np.abs(dy)<3))
    im[ring]=2
    im[(radius>=3)&(radius<4)]=0
    im[radius<3]=0 if state else 1
    if state:
        im[(radius<3)&((x+y+state)%3==0)]=2
        sector=[dy<-2,dx>2,dy>2][state-1]
        im[ring&sector]=3
        im[7:9,7:9]=3
    else:
        im[7:9,5:11]=0
    return im

def item_icon(rows: str) -> bytes:
    """Reduce the authored object, excluding the card's slab and shadow."""
    src=np.array([[int(c) if c in '0123' else 1 for c in r] for r in rows.strip().splitlines()],np.uint8)
    dst=np.ones((8,8),np.uint8)
    for y in range(8):
        for x in range(8):
            a=src[y*2:y*2+2,x*2:x*2+2].ravel()
            dst[y,x]=0 if np.count_nonzero(a==0)>=2 else 3 if 3 in a else 2 if 2 in a else 1
    return encode(dst)
# B2: the void's side faces, after the items in the same block, their tiles shared where equal. *_R is the card
# X-flipped: no tiles of its own, the map attribute's X-flip bit (0x20, kept in meta_pal for bank 0 cards) and the
# columns swapped in meta_tile.
EDGES: list[tuple[str, int, object]] = [
    ("VOID_SIDE", PAL_VOID, lambda: void_edge("side")),
    ("VOID_CORNER", PAL_VOID, lambda: void_edge("corner")),
    ("VOID_TIP", PAL_VOID, lambda: void_edge("tip")),
]
EDGE_FLIP = 0x20
EDGE_NAMES = {n for n, _, _ in EDGES}
EXTRAS=[(f'AUTOCARPET{k}',PAL_FLOOR,lambda k=k:connected_carpet(k)) for k in range(16)]
EXTRAS += [('PLATE_DOWN',PAL_FLOOR,plate_down)]
EXTRAS += [(f'PORTAL{k}',PAL_REACT_A,lambda k=k:portal(k)) for k in range(4)]
SHARED_NAMES=EDGE_NAMES | {n for n,_,_ in EXTRAS}
LOW0 = {f"ITEM_{n}" for n, _, _ in ITEM_ART} | {"ICE1"} | SHARED_NAMES


def meta_tiles(m: np.ndarray) -> list[bytes]:
    """8x16 sprite order: TL, BL, TR, BR"""
    return [encode(m[0:8, 0:8]), encode(m[8:16, 0:8]), encode(m[0:8, 8:16]), encode(m[8:16, 8:16])]


def build() -> tuple[list[np.ndarray], list[int], list[np.ndarray], list[int]]:
    cores = [fn() for _, _, fn in METAS]
    reacts = [fn() for _, _, fn in REACTS]
    return cores, [p for _, p, _ in METAS], reacts, [p for _, p, _ in REACTS]


def layout() -> tuple[list, dict[int, bytes], dict[int, bytes], int, int, int, int]:
    """every metatile id in order: (name, palette | 0x80 if in bank 0, image); the bank 1 and bank 0 tiles by index;
    the first reaction tile and the end of the bank 1 range"""
    cores, cpals, reacts, rpals = build()
    ms: list[tuple[str, int, np.ndarray | None]] = [(n, p, im) for (n, _, _), p, im in zip(METAS, cpals, cores)]
    ms += [(n, p, im) for (n, _, _), p, im in zip(REACTS, rpals, reacts)]
    ms += [(f"REACT{k}", PAL_FLOOR, None) for k in range(len(reacts), REACT_METAS)]
    ms += [(n, p, fn()) for n, p, fn in FRAGILE]
    ms += [(f"ITEM_{n}", p, item(rows, 60 + k, p in (PAL_FLOOR, PAL_OBJ))) for k, (n, p, rows) in enumerate(ITEM_ART)]
    ms += [(n, p, fn()) for n, p, fn in EDGES]
    ms += [(n + "_R", p, None) for n, p, _ in EDGES]  # the flips: placed below, on their card's tiles
    ms += [(n,p,fn()) for n,p,fn in EXTRAS]
    b1: dict[int, bytes] = {}
    b0: dict[int, bytes] = {}
    t1, t0, tl = T_BASE, B0_BASE, LOW0_BASE
    out, react_t = [], 0
    edge_t0 = 0
    for k, (n, p, im) in enumerate(ms):
        if k == len(METAS):
            react_t = t1
        if n in EDGE_NAMES and not edge_t0:
            edge_t0 = tl
        if n.endswith("_R") and n[:-2] in LOW0:
            src = next(o for o in out if o[0] == n[:-2])
            out.append((n, src[1] | EDGE_FLIP, src[2], src[3]))
            continue
        if n in SHARED_NAMES:  # shared tiles: an explicit (TL, BL, TR, BR)
            ids = []
            for t in meta_tiles(im):
                hit = next((k for k in range(edge_t0, tl) if b0[k] == t), None)
                if hit is None:
                    b0[tl] = t
                    hit = tl
                    tl += 1
                ids.append(hit)
            out.append((n, p | 0x80, im, tuple(ids)))
        elif n in LOW0:
            for j, t in enumerate(meta_tiles(im)):
                b0[tl + j] = t
            out.append((n, p | 0x80, im, tl))
            tl += 4
        elif n in FIXED0 or n in BANK0:
            base = FIXED0.get(n, t0)
            if n not in FIXED0:
                t0 += 4
            for j, t in enumerate(meta_tiles(im)):
                b0[base + j] = t
            out.append((n, p | 0x80, im, base))
        else:
            if im is not None:
                for j, t in enumerate(meta_tiles(im)):
                    b1[t1 + j] = t
            out.append((n, p, im, t1))
            t1 += 4
    shard_t = t0
    for v in (0, 1):
        sh = shard(v)
        b0[t0] = encode(sh[0:8])
        b0[t0 + 1] = encode(sh[8:16])
        t0 += 2
    if t1 > FIXED_BASE:
        raise SystemExit(f"look tiles overrun the font block: {t1} > {FIXED_BASE}")
    if t0 > SPR_BASE:
        raise SystemExit(f"bank 0 look tiles overrun the flying cards: {t0} > {SPR_BASE}")
    spr, _ = BA.build_sprites()
    n_spr = len(spr.tiles)
    if tl > ICON_BASE:
        raise SystemExit(f"bank 0 BG-only cards ({LOW0_BASE}..{tl - 1}) run into inventory icons at {ICON_BASE}")
    if n_spr > B0_BASE:
        raise SystemExit(f"the sprite set ({n_spr} tiles) runs into the bank 0 look range at {B0_BASE}")
    return out, b1, b0, react_t, t1, shard_t, tl


def mix(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))  # type: ignore[return-value]


def write(out: Path) -> None:
    ms, b1, b0, react_t, t1_end, shard_t, low_end = layout()
    n_core = len(METAS)
    react_m = n_core  # reaction metas follow the core set
    frag_m = n_core + REACT_METAS
    t1_lo, t1_hi = T_BASE, t1_end
    tiles1 = b"".join(b1.get(t, bytes(16)) for t in range(t1_lo, t1_hi))
    t0_lo, t0_hi = B0_BASE, SPR_BASE + 8
    tiles0 = b"".join(b0.get(t, bytes(16)) for t in range(t0_lo, t0_hi))
    tiles0l = b"".join(b0[t] for t in range(LOW0_BASE, low_end))
    pals = [p for _, p, _, _ in ms]
    mtile = []
    for _, p, _, base in ms:  # the 2x2 map order: TL, TR, BL, BR (the tiles are in 8x16 order: TL, BL, TR, BR)
        t = base if isinstance(base, tuple) else (base, base + 1, base + 2, base + 3)
        if p & EDGE_FLIP:  # X-flipped: each tile mirrors, so the columns swap
            mtile += [t[2], t[0], t[3], t[1]]
        else:
            mtile += [t[0], t[2], t[1], t[3]]
    names = list(BIOMES)
    biome_words = []
    for nm in names:
        biome_words += [rgb555(c) for p in BIOMES[nm] for c in p]
    h = [
        "/* Generated by tools/bake_look.py. Do not edit. */",
        "#ifndef EG_LOOK_ART_H",
        "#define EG_LOOK_ART_H",
        "#include <gb/gb.h>",
        "#include <stdint.h>",
        f"#define LOOK_T_BASE {T_BASE}u /* VRAM bank 1: the cards from here, 4 tiles each in 8x16 order (TL, BL, TR, BR) */",
    ]
    for i, (name, _, _) in enumerate(METAS):
        h.append(f"#define M_{name} {i}u")
    h.append(f"#define M_CORE {n_core}u")
    h.append(f"#define M_REACT0 {react_m}u /* reserved reaction range: M_REACT0 .. M_REACT0 + LOOK_REACT_METAS - 1 */")
    h.append(f"#define LOOK_REACT_METAS {REACT_METAS}u")
    for i, (name, _, _) in enumerate(REACTS):
        h.append(f"#define M_{name} {react_m + i}u /* placeholder */")
    h.append(f"#define M_FRAG0 {frag_m}u /* fragile floors and the pit: M_<KIND>0 + steps taken */")
    for i, (name, _, _) in enumerate(FRAGILE):
        h.append(f"#define M_{name} {frag_m + i}u")
    item_m = frag_m + len(FRAGILE)
    h.append(f"#define M_ITEM0 {item_m}u /* a piece lying in a room: M_ITEM0 + piece - 1 (P_FIRE .. P_CLOUD, eg_piece.h) */")
    for i, (name, _, _) in enumerate(ITEM_ART):
        h.append(f"#define M_ITEM_{name} {item_m + i}u")
    h.append(f"#define LOOK_ITEMS {len(ITEM_ART)}u")
    edge_m = item_m + len(ITEM_ART)
    for i, (name, _, _) in enumerate(EDGES):
        h.append(f"#define M_{name} {edge_m + i}u /* B2: ground on the left (VOID_SIDE, VOID_CORNER) or up-left (VOID_TIP) */")
    for i, (name, _, _) in enumerate(EDGES):
        h.append(f"#define M_{name}_R {edge_m + len(EDGES) + i}u /* the same, X-flipped: ground on the right */")
    for i,(name,_,_) in enumerate(EXTRAS):
        h.append(f"#define M_{name} {edge_m+len(EDGES)*2+i}u")
    h.append(f"#define LOOK_ICON_BASE {ICON_BASE}u /* sixteen 8x8 inventory icons in signed BG bank 0 */")
    for k, n in FRAG_STEPS.items():
        h.append(f"#define LOOK_{k}_STEPS {n}u")
    h.append(f"#define M_COUNT {len(ms)}u")
    h.append(f"#define LOOK_SPR_BASE {SPR_BASE}u /* VRAM bank 0: +0 the floor card (M_SLAB_A), +4 the wall card (M_WALL_TOP) */")
    h.append(f"#define LOOK_T_REACT {react_t}u")
    h.append(f"#define LOOK_T_END {react_t + 4 * REACT_METAS}u /* first tile past the reaction range */")
    h.append(f"#define LOOK_T1_END {t1_hi}u /* first bank 1 tile past the look */")
    h.append(f"#define LOOK_B0_BASE {B0_BASE}u /* VRAM bank 0: the flying fragile cards from here */")
    h.append(f"#define LOOK_LOW0_BASE {LOW0_BASE}u /* VRAM bank 0 BG-only (0x9000 + 16n): items, thin ice's crack, the void's edges, to {low_end - 1} */")
    h.append(f"#define LOOK_SHARD {shard_t}u /* VRAM bank 0: two 8x16 glass shard sprites (+0, +2) */")
    h.append("#define LOOK_META_BANK0 0x80u /* meta_pal bit: the card's tiles are in VRAM bank 0 */")
    h.append(f"#define LOOK_META_FLIP 0x{EDGE_FLIP:02X}u /* meta_pal bit, bank 0 cards: the map attribute's X-flip */")
    for i, nm in enumerate(PAL_NAMES):
        h.append(f"#define LOOK_PAL_{nm} {i}u")
    for i, nm in enumerate(names):
        h.append(f"#define BIOME_{nm} {i}u")
    h.append(f"#define BIOME_COUNT {len(names)}u")
    h += [
        "extern const uint8_t look_ambient_kind[];",
        "/* loads the cards into VRAM banks 1 and 0, a biome's 8 BG palettes into pal_out[32], each card's palette into",
        " * meta_pal[M_COUNT] (| LOOK_META_BANK0) and its 2x2 map tiles into meta_tile[M_COUNT * 4] */",
        "void look_art_load(uint8_t biome, uint16_t *pal_out, uint8_t *meta_pal, uint8_t *meta_tile) BANKED;",
        "#endif",
    ]
    (out / "look_art.h").write_text("\n".join(h) + "\n")
    c = [
        "/* Generated by tools/bake_look.py. Do not edit. */",
        "#pragma bank 255",
        "#include <gb/gb.h>",
        "#include <gb/cgb.h>",
        "#include <string.h>",
        '#include "look_art.h"',
        "BANKREF(look_art_bank)",
        BA.c_bytes("look_tiles1", tiles1),
        BA.c_bytes("look_tiles0", tiles0),
        BA.c_bytes("look_tiles0l", tiles0l),
        BA.c_bytes("look_item_icons", b''.join(item_icon(rows) for _,_,rows in ITEM_ART)),
        "static const uint8_t look_meta_pal[] = {" + ", ".join(map(str, pals)) + "};",
        "static const uint8_t look_meta_tile[] = {" + ", ".join(map(str, mtile)) + "};",
        BA.c_words("look_biome_pal", biome_words),
        "const uint8_t look_ambient_kind[] = {" + ", ".join(str(AMBIENT[n]) for n in names) + "};",
        f"#define LOOK_NT1 {len(tiles1) // 16}u",
        f"#define LOOK_NT0 {len(tiles0) // 16}u",
        f"#define LOOK_NT0L {len(tiles0l) // 16}u",
        """
void look_art_load(uint8_t biome, uint16_t *pal_out, uint8_t *meta_pal, uint8_t *meta_tile) BANKED {
  VBK_REG = 1;
  set_bkg_data(LOOK_T_BASE, LOOK_NT1, look_tiles1);
  VBK_REG = 0;
  set_sprite_data(LOOK_B0_BASE, LOOK_NT0, look_tiles0);
  set_bkg_data(LOOK_LOW0_BASE, LOOK_NT0L, look_tiles0l); /* bank 0, signed BG addressing: 0x9000 + 16 * tile */
  set_bkg_data(LOOK_ICON_BASE, LOOK_ITEMS, look_item_icons);
  memcpy(pal_out, &look_biome_pal[(uint16_t)biome * 32u], 64);
  memcpy(meta_pal, look_meta_pal, M_COUNT);
  memcpy(meta_tile, look_meta_tile, (uint16_t)M_COUNT * 4u);
}
""",
    ]
    (out / "look_art.c").write_text("\n".join(c))
    print(f"look: {len(ms)} cards; bank 1 tiles {t1_lo}..{t1_hi - 1} (reaction {react_t}..{react_t + 4 * REACT_METAS - 1}),"
          f" bank 0 BG-only tiles {LOW0_BASE}..{low_end - 1} (items, cracked ice, void edges), bank 0 tiles {t0_lo}..{t0_hi - 1}"
          f" (shards {shard_t}..{shard_t + 3})")


# --- review renders ---------------------------------------------------------------------------------------------------
def paint(idx: np.ndarray, p: list[tuple[int, int, int]]) -> Image.Image:
    a = np.array(p, np.uint8)[idx]
    return Image.fromarray(a, "RGB")


def sheet(path: Path) -> None:
    cores, cpals, reacts, rpals = build()
    office = BIOMES["OFFICE"]
    frs = [fn() for _, _, fn in FRAGILE]
    allm = list(zip(cores + reacts + frs, cpals + rpals + [p for _, p, _ in FRAGILE],
                    [n for n, _, _ in METAS] + [n for n, _, _ in REACTS] + [n for n, _, _ in FRAGILE]))
    cols = 10
    rows = (len(allm) + cols - 1) // cols
    cw = 18
    w = cols * cw + 2
    nb = len(BIOMES)
    h = rows * cw + 2 + 6 + nb * cw + 2
    base = Image.new("RGB", (w, h), (24, 22, 32))
    for i, (m, p, _) in enumerate(allm):
        base.paste(paint(m, office[p]), (2 + (i % cols) * cw, 2 + (i // cols) * cw))
    # every biome: a slab, foam, a wall front, the void, the underside, a CRT, a token, the door
    y0 = rows * cw + 8
    sample = ["SLAB_A", "SLAB_B", "FOAM_A", "WALL_TOP", "WALL_FRONT", "DRY", "VOID_A", "VOID_UNDER", "CRT_A", "TOKEN"]
    idx_of = {n: i for i, (_, _, n) in enumerate(allm)}
    for bi, (bn, bp) in enumerate(BIOMES.items()):
        for k, nm in enumerate(sample):
            m, p, _ = allm[idx_of[nm]]
            base.paste(paint(m, bp[p]), (2 + k * cw, y0 + bi * cw))
    big = base.resize((w * 4, h * 4), Image.NEAREST)
    out = Image.new("RGB", (w + 8 + w * 4, max(h, h * 4)), (24, 22, 32))
    out.paste(base, (0, 0))
    out.paste(big, (w + 8, 0))
    out.save(path)


def fragile_sheet(path: Path) -> None:
    """every fragile stage, alone and set into the floor (a 3x3 of slabs), with the shards: 1x on top, 4x below"""
    from PIL import ImageDraw
    office = BIOMES["OFFICE"]
    rows = [["CARPET0", "CARPET1", "CARPET2", "PIT"], ["CEIL0", "CEIL1", "PIT"], ["GLASS", "PIT"],
            ["GLITCH0", "GLITCH1", "GLITCH2", "GLITCH1"]]
    labels = ["worn carpet: 3 steps", "ceiling tile: 2 steps", "glass: 1 step", "glitched glass: solid, flicker, gone"]
    fr = {n: (fn(), p) for n, p, fn in FRAGILE}
    slabs = [slab(1), slab(2, "scuff"), slab(3, "seam"), slab(4, "stain"), slab(1)]
    cell = 52  # a stage in its 3x3 of floor, plus a gap
    w = 2 + 4 * cell + 8 + 4 * 18 + 2
    h = 2 + len(rows) * (cell + 10) + 20
    base = Image.new("RGB", (w, h), (24, 22, 32))
    for r, names in enumerate(rows):
        y0 = 2 + r * (cell + 10)
        for k, nm in enumerate(names):
            im, p = fr[nm]
            x0 = 2 + k * cell
            for yy in range(3):
                for xx in range(3):
                    if (xx, yy) == (1, 1):
                        base.paste(paint(im, office[p]), (x0 + 16, y0 + 16))
                    else:
                        base.paste(paint(slabs[(xx * 3 + yy + k + r) % 5], office[PAL_FLOOR]), (x0 + xx * 16, y0 + yy * 16))
            base.paste(paint(im, office[p]), (2 + 4 * cell + 8 + k * 18, y0 + 16))  # alone
    y0 = 2 + len(rows) * (cell + 10)
    for v in (0, 1):  # the shards over the void, as they fall
        bg = paint(void(17, 0), office[PAL_VOID])
        sh = shard(v)
        a = np.array(bg)
        col = np.array(office[PAL_VOID], np.uint8)
        a[0:16, 4:12] = np.where(sh[..., None] > 0, col[sh], a[0:16, 4:12])
        base.paste(Image.fromarray(a), (2 + v * 18, y0))
    big = base.resize((w * 4, h * 4), Image.NEAREST)
    d = ImageDraw.Draw(big)
    for r, lab in enumerate(labels):
        d.text((8, (2 + r * (cell + 10) + 49) * 4 + 4), lab, fill=(236, 220, 255))
    d.text((8 + 40 * 4, (y0 + 4) * 4), "glass shards (sprites)", fill=(236, 220, 255))
    out = Image.new("RGB", (w * 4, h + 8 + h * 4), (24, 22, 32))
    out.paste(base, (0, 0))
    out.paste(big, (0, h + 8))
    out.save(path)


def items_sheet(path: Path) -> None:
    """every item card, and thin ice whole and cracked, each set into the floor (a 3x3 of slabs): 1x on top, 4x below"""
    from PIL import ImageDraw
    office = BIOMES["OFFICE"]
    cards = [(n, p, item(rows, 60 + k, p in (PAL_FLOOR, PAL_OBJ))) for k, (n, p, rows) in enumerate(ITEM_ART)]
    cards += [("THIN ICE", PAL_REACT_A, ice(41)), ("CRACKED", PAL_REACT_A, ice_cracked(41))]
    slabs = [slab(1), slab(2, "scuff"), slab(3, "seam"), slab(4, "stain"), slab(1)]
    cols, cell = 6, 52
    rows_n = (len(cards) + cols - 1) // cols
    w, h = 2 + cols * cell, 2 + rows_n * (cell + 8)
    base = Image.new("RGB", (w, h), (24, 22, 32))
    for k, (_, p, im) in enumerate(cards):
        x0, y0 = 2 + (k % cols) * cell, 2 + (k // cols) * (cell + 8)
        for yy in range(3):
            for xx in range(3):
                if (xx, yy) == (1, 1):
                    base.paste(paint(im, office[p]), (x0 + 16, y0 + 16))
                else:
                    base.paste(paint(slabs[(xx * 3 + yy + k) % 5], office[PAL_FLOOR]), (x0 + xx * 16, y0 + yy * 16))
    big = base.resize((w * 4, h * 4), Image.NEAREST)
    d = ImageDraw.Draw(big)
    for k, (n, _, _) in enumerate(cards):
        d.text(((2 + (k % cols) * cell) * 4 + 4, (2 + (k // cols) * (cell + 8) + 48) * 4 + 2), n, fill=(236, 220, 255))
    out = Image.new("RGB", (w * 4, h + 8 + h * 4), (24, 22, 32))
    out.paste(base, (0, 0))
    out.paste(big, (0, h + 8))
    out.save(path)


VOID_RING = True
THIN_FOAM = True


def preview(path: Path, seed: int, tier: int, egtool: Path, cam: tuple[int, int] = (0, 0), absent: set | None = None):
    """the room as src/grid.c draws it (meta_for), 160x128 at the camera, 4x"""
    txt = subprocess.run([str(egtool), "show", str(seed), str(tier)], capture_output=True, text=True).stdout
    rows = [r for r in txt.splitlines() if len(r) == 16][:14]
    ch = {"#": 2, ":": 1, ".": 0, "O": 4, "$": 5, "k": 6, "D": 7, "=": 3, "@": 0, "M": 0, "E": 0}
    cells = [[ch.get(c, 0) for c in r] for r in rows]
    if THIN_FOAM:  # what the puzzle workstream's rooms will look like: foam in a few deliberate patches
        cells = [[(0 if t == 1 and ((x // 3) + (y // 2)) % 4 else t) for x, t in enumerate(r)] for y, r in enumerate(cells)]
    cores, cpals, reacts, rpals = build()
    ms = cores + reacts
    ps = cpals + rpals
    name = {n: i for i, (n, _, _) in enumerate(METAS)}
    office = BIOMES["OFFICE"]
    absent = absent or set()

    def is_void(x: int, y: int) -> bool:
        return cells[y][x] == 2 and (x == 0 or x == 15 or y == 13)

    def here(x: int, y: int) -> bool:
        return (x, y) not in absent

    def meta(x: int, y: int) -> int:
        h = (x * 7 + y * 13 + (x ^ y)) & 0xFF
        if not here(x, y) or is_void(x, y):
            if y > 0 and here(x, y - 1) and not is_void(x, y - 1):
                return name["VOID_UNDER"]
            return name[["VOID_A", "VOID_B", "VOID_C", "VOID_C"][h & 3]]
        t = cells[y][x]
        if t == 0:
            k = h % 20
            return name["SLAB_A" if k < 10 else ("SLAB_B" if k < 14 else ("SLAB_C" if k < 17 else "SLAB_D"))]
        if t == 1:
            return name["FOAM_A"] if (h & 2) else name["FOAM_B"]
        if t == 2:
            if y < 13 and cells[y + 1][x] not in (2, 7):
                return name[["WALL_FRONT", "WALL_FRONT", "WALL_VENT", "WALL_FRONT", "WALL_NOTE", "WALL_FRONT",
                             "WALL_SWITCH"][h % 7]]
            if y < 13 and is_void(x, y + 1) and not is_void(x, y):
                return name["WALL_FRONT"]
            return name["WALL_TOP"]
        if t == 3:
            return name["DRY"]
        if t == 4:
            return name["CRT_A"] if x & 1 else name["CRT_B"]
        if t == 5:
            return name["TOKEN"]
        if t == 6:
            return name["KEY"]
        return name["DOOR_SHUT"]

    im = Image.new("RGB", (256, 224))
    for y in range(14):
        for x in range(16):
            m = meta(x, y)
            im.paste(paint(ms[m], office[ps[m]]), (x * 16, y * 16))
    view = im.crop((cam[0], cam[1], cam[0] + 160, cam[1] + 128))
    view.resize((640, 512), Image.NEAREST).save(path)
    im.resize((512, 448), Image.NEAREST).save(path.with_name(path.stem + "_full.png"))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", type=Path)
    ap.add_argument("--sheet", type=Path)
    ap.add_argument("--preview", type=Path)
    ap.add_argument("--fragile", type=Path, help="the fragile floors' sheet")
    ap.add_argument("--items", type=Path, help="the item cards' sheet")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--tier", type=int, default=0)
    ap.add_argument("--egtool", type=Path, default=Path("build/host/egtool"))
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    write(a.out)
    if a.sheet:
        sheet(a.sheet)
    if a.fragile:
        fragile_sheet(a.fragile)
    if a.items:
        items_sheet(a.items)
    if a.preview:
        preview(a.preview, a.seed, a.tier, a.egtool)


if __name__ == "__main__":
    main()
