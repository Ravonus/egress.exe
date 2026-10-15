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
        [s, b, hx(accent_hi), l],  # objects: the CRTs' screens, tokens, keycards, props
        [hx(react_b[0]), b, hx(react_b[1]), hx(react_b[2])],
    ]


WET = ["#18245c", "#5094ec", "#d6f0ff"]
FIRE = ["#601630", "#ff7830", "#ffe468"]
BIOMES = {
    # name: palettes, ambient animation (src/look.c look_ambient)
    "OFFICE": biome(["#46224a", "#e4968c", "#f6c4ae", "#ffeedc"], ["#14404f", "#48a0a8", "#b4ecd2"],
                    ["#1c123c", "#583c96", "#9676d0", "#e8daff"], ["#0c081e", "#1e1442", "#50348c", "#e6c4ff"],
                    "#ffde5c", "#f4ac38", WET, FIRE),
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
    for k, (px, py) in enumerate(rng_points(seed, 4, 3, 12)):
        idx[py, px] = 0 if k % 2 == 0 else 3
    if kind == "scuff":  # a dragged-chair scuff
        for t in range(6):
            idx[9 + t // 3, 4 + t] = 0 if t % 2 == 0 else 1
            idx[10 + t // 3, 4 + t] = 1
    elif kind == "seam":  # carpet tiles meet: a faint seam with a lifted corner
        idx[2:14, 8] = np.where(idx[2:14, 8] == 2, 1, idx[2:14, 8])
        idx[10:13, 10:13] = [[1, 1, 0], [1, 2, 1], [0, 1, 1]]
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
    return 1.2 + 0.9 * np.clip(1.0 - (x + y * 16 / h) / 24.0, 0, 1) + n * 0.3


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
    """the slab under an object, in the object palette: base with the light's dither and the bevel"""
    x, y = grid()
    v = 1.0 + 1.6 * np.clip(0.85 - (x + y) / 26.0, 0, 1) + (vnoise(16, 4, seed) - 0.5) * 0.3
    v = np.clip(v, 1.0, 1.9)  # base and light only (index 3 is the slab light here)
    idx = dither(v - 0.0, 1, 2)
    idx = np.where(idx == 2, 3 if light3 else 1, idx)
    idx[0, :] = 3 if light3 else 1
    idx[:, 0] = 3 if light3 else 1
    idx[15, :] = 0
    idx[:, 15] = 0
    return idx.astype(np.uint8)


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
.03333333332110.
.01111111111110.
.00000000000000.
....01111110....
....00000000....
................"""


def crt(seed: int, screen: str) -> np.ndarray:
    """an amber-phosphor office terminal: a beige case lit from the upper left, a dark bezel, a glowing screen"""
    idx = from_template(CRT, seed)
    # the case's dithered light: light peach fading to base toward the lower right
    for yy in range(2, 11):
        for xx in range(2, 13):
            if idx[yy, xx] == 3 and (xx + yy) > 14 and (xx + yy) % 2 == 0:
                idx[yy, xx] = 1
    for yy in range(4, 9):  # scanlines
        for xx in range(4, 11):
            if yy % 2 == 1 and (xx + yy) % 4 == 0:
                idx[yy, xx] = 0
    if screen == "cursor":
        idx[5, 5:9] = 3
        idx[7, 5:7] = 3
        idx[7, 8] = 3
    else:  # a face in the static
        idx[5, 6] = 0
        idx[5, 8] = 0
        idx[7, 6:9] = 0
        idx[6, 5] = 3
    idx[4, 4] = 3  # glare
    return idx.astype(np.uint8)


def token(seed: int) -> np.ndarray:
    """a brass arcade token: a lit rim, a darker face with a stamped slot, a glint"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    d = np.hypot(x - 8, y - 7.5)
    idx = floor_under(seed)
    coin = d <= 5.4
    rim = coin & (d > 3.9)
    light = (x - 8) + (y - 7.5)  # < 0 toward the upper left
    idx = np.where(coin, 2, idx)
    idx = np.where(rim & (light < -1.5), 3, idx)
    idx = np.where(rim & (light > 2.5) & ((xi + yi) % 2 == 0), 0, idx)
    idx = np.where(~rim & coin & (xi >= 7) & (xi <= 8) & (yi >= 5) & (yi <= 10), 0, idx)  # the slot
    idx = np.where(~rim & coin & (xi == 6) & (yi >= 6) & (yi <= 9), 3, idx)
    idx = np.where(outline(coin), 0, idx)
    sh = (np.hypot(x - 9.5, (y - 14.3) * 2.4) < 4.8) & ~coin & ~outline(coin)
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
    ("FOAM_A", PAL_FOAM, lambda: foam(8)),
    ("FOAM_B", PAL_FOAM, lambda: foam(9, 2, 0)),
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


def meta_tiles(m: np.ndarray) -> list[bytes]:
    """8x16 sprite order: TL, BL, TR, BR"""
    return [encode(m[0:8, 0:8]), encode(m[8:16, 0:8]), encode(m[0:8, 8:16]), encode(m[8:16, 8:16])]


def build() -> tuple[list[np.ndarray], list[int], list[np.ndarray], list[int]]:
    cores = [fn() for _, _, fn in METAS]
    reacts = [fn() for _, _, fn in REACTS]
    return cores, [p for _, p, _ in METAS], reacts, [p for _, p, _ in REACTS]


def mix(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))  # type: ignore[return-value]


def write(out: Path) -> None:
    cores, cpals, reacts, rpals = build()
    n_core = len(cores)
    react_m = n_core  # reaction metas follow the core set
    if T_BASE + 4 * (n_core + REACT_METAS) > FIXED_BASE:
        raise SystemExit(f"look tiles overrun the font block: {T_BASE + 4 * (n_core + REACT_METAS)} > {FIXED_BASE}")
    tiles = b"".join(b"".join(meta_tiles(m)) for m in cores + reacts)
    pals = cpals + rpals + [PAL_FLOOR] * (REACT_METAS - len(reacts))
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
        f"#define LOOK_T_BASE {T_BASE}u /* VRAM bank 1: metatile m = tiles LOOK_T_BASE + 4m .. +3 (TL, BL, TR, BR) */",
    ]
    for i, (name, _, _) in enumerate(METAS):
        h.append(f"#define M_{name} {i}u")
    h.append(f"#define M_CORE {n_core}u")
    h.append(f"#define M_REACT0 {react_m}u /* reserved reaction range: M_REACT0 .. M_REACT0 + LOOK_REACT_METAS - 1 */")
    h.append(f"#define LOOK_REACT_METAS {REACT_METAS}u")
    for i, (name, _, _) in enumerate(REACTS):
        h.append(f"#define M_{name} {react_m + i}u /* placeholder */")
    h.append(f"#define M_COUNT {n_core + REACT_METAS}u")
    h.append(f"#define LOOK_SPR_BASE {SPR_BASE}u /* VRAM bank 0: +0 the floor card, +4 the wall card (8x16 pairs) */")
    h.append(f"#define LOOK_T_REACT {T_BASE + 4 * react_m}u")
    h.append(f"#define LOOK_T_END {T_BASE + 4 * (n_core + REACT_METAS)}u /* first tile past the reaction range */")
    for i, nm in enumerate(PAL_NAMES):
        h.append(f"#define LOOK_PAL_{nm} {i}u")
    for i, nm in enumerate(names):
        h.append(f"#define BIOME_{nm} {i}u")
    h.append(f"#define BIOME_COUNT {len(names)}u")
    h += [
        "extern const uint8_t look_ambient_kind[];",
        "/* loads the cards into VRAM bank 1, a biome's 8 BG palettes into pal_out[32], each metatile's palette into",
        " * meta_pal[M_COUNT] */",
        "void look_art_load(uint8_t biome, uint16_t *pal_out, uint8_t *meta_pal) BANKED;",
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
        BA.c_bytes("look_tiles", tiles),
        "static const uint8_t look_meta_pal[] = {" + ", ".join(map(str, pals)) + "};",
        BA.c_words("look_biome_pal", biome_words),
        "const uint8_t look_ambient_kind[] = {" + ", ".join(str(AMBIENT[n]) for n in names) + "};",
        f"#define LOOK_NTILES {len(tiles) // 16}u",
        """
void look_art_load(uint8_t biome, uint16_t *pal_out, uint8_t *meta_pal) BANKED {
  VBK_REG = 1;
  set_bkg_data(LOOK_T_BASE, LOOK_NTILES, look_tiles);
  VBK_REG = 0;
  set_sprite_data(LOOK_SPR_BASE, 4, look_tiles + (uint16_t)M_SLAB_A * 64u);
  set_sprite_data(LOOK_SPR_BASE + 4u, 4, look_tiles + (uint16_t)M_WALL_TOP * 64u);
  memcpy(pal_out, &look_biome_pal[(uint16_t)biome * 32u], 64);
  memcpy(meta_pal, look_meta_pal, M_COUNT);
}
""",
    ]
    (out / "look_art.c").write_text("\n".join(c))
    print(f"look: {n_core} core cards + {len(reacts)}/{REACT_METAS} reaction, tiles {T_BASE}..{T_BASE + 4 * (n_core + REACT_METAS) - 1}")


# --- review renders ---------------------------------------------------------------------------------------------------
def paint(idx: np.ndarray, p: list[tuple[int, int, int]]) -> Image.Image:
    a = np.array(p, np.uint8)[idx]
    return Image.fromarray(a, "RGB")


def sheet(path: Path) -> None:
    cores, cpals, reacts, rpals = build()
    office = BIOMES["OFFICE"]
    allm = list(zip(cores + reacts, cpals + rpals, [n for n, _, _ in METAS] + [n for n, _, _ in REACTS]))
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


VOID_RING = True


def preview(path: Path, seed: int, tier: int, egtool: Path, cam: tuple[int, int] = (0, 0), absent: set | None = None):
    """the room as src/grid.c draws it (meta_for), 160x128 at the camera, 4x"""
    txt = subprocess.run([str(egtool), "show", str(seed), str(tier)], capture_output=True, text=True).stdout
    rows = [r for r in txt.splitlines() if len(r) == 16][:14]
    ch = {"#": 2, ":": 1, ".": 0, "O": 4, "$": 5, "k": 6, "D": 7, "=": 3, "@": 0, "M": 0, "E": 0}
    cells = [[ch.get(c, 0) for c in r] for r in rows]
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
            return name["SLAB_A"] if h % 5 < 2 else name[["SLAB_B", "SLAB_C", "SLAB_D"][h % 3]]
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
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--tier", type=int, default=0)
    ap.add_argument("--egtool", type=Path, default=Path("build/host/egtool"))
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    write(a.out)
    if a.sheet:
        sheet(a.sheet)
    if a.preview:
        preview(a.preview, a.seed, a.tier, a.egtool)


if __name__ == "__main__":
    main()
