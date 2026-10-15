"""Bake EGRESS's art into CGB tiles: `python tools/bake_art.py OUT_DIR [--sheet PNG]`.

Every object is a small procedural construction (shapes, value noise, a light direction) rendered at its native
16x16 and ordered-dithered (4x4 Bayer) into its palette's four colours: KEEL-style dithered pixel art, never smooth.
People are hand-shaped templates whose colours are seeded at run time from diverse sets (skin, hair, hoodie, trousers).

Writes OUT_DIR/art.c (banked data and the loaders) and OUT_DIR/art.h. --sheet writes a contact sheet to review.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import numpy as np
from PIL import Image

BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0 + 1 / 32


def hx(c: str) -> tuple[int, int, int]:
    c = c.lstrip("#")
    return int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16)


def rgb555(c: tuple[int, int, int]) -> int:
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def pal(*cs: str) -> list[tuple[int, int, int]]:
    return [hx(c) for c in cs]


# --- palettes (index 0 is the darkest, 3 the lightest, unless noted) ---------------------------------------------
BG_PAL = [
    pal("#2a2410", "#6b5a24", "#a8904a", "#d2bc72"),  # 0 carpet: backrooms ochre
    pal("#2e1822", "#6e3c4c", "#a86c78", "#d8a8a4"),  # 1 insulation foam: plum, dusty rose, pale pink
    pal("#1c1a12", "#6a6040", "#c8b860", "#f0e4a0"),  # 2 wall: concrete umber + mono-yellow wallpaper
    pal("#202838", "#6878a0", "#b0bcd0", "#f4f4ec"),  # 3 drywall: blue-grey board, chalk
    pal("#141420", "#7c7064", "#38c8b8", "#e8f0d8"),  # 4 CRT: case taupe, cyan screen (also HUD text)
    pal("#2a2410", "#6b5a24", "#e0a830", "#fff0b0"),  # 5 token: brass on the carpet's own browns
    pal("#101830", "#d03068", "#40a0f0", "#f8f0f8"),  # 6 keycard: navy, red-magenta stripe, blue
    pal("#120c08", "#5c3820", "#30e060", "#c8ffc0"),  # 7 egress door: dark wood, EXIT green
]
SPR_PAL = [
    pal("#000000", "#1a1018", "#c08860", "#3a2418"),  # 0 head: outline, skin, hair (seeded at run time)
    pal("#000000", "#14101c", "#e06030", "#304878"),  # 1 body: outline, hoodie, trousers (seeded)
    pal("#000000", "#1e0c2c", "#9c90b8", "#ff9020"),  # 2 static mite: violet, static lilac, orange eyes
    pal("#000000", "#101838", "#e8e4f4", "#e02838"),  # 3 WATCH eye: navy, sclera, red iris
    pal("#000000", "#103048", "#40d8f0", "#ffffff"),  # 4 the lead (cable) arcs
    pal("#000000", "#601040", "#ff58b0", "#fff0f8"),  # 5 orb
    pal("#000000", "#ffffff", "#ffffff", "#ffffff"),  # 6 hit flash
    pal("#000000", "#601000", "#ff4020", "#ffe040"),  # 7 tell flash
]

SKINS = ["#4a2a1c", "#5c3a24", "#7a4b2f", "#8d5a3b", "#a66b45", "#c48a62", "#dba680", "#f0c8a4"]
HAIRS = ["#140c0c", "#2c1810", "#5a3018", "#8c5a28", "#d8b060", "#b03018", "#3c3c48", "#204868"]
HOODIES = ["#e06030", "#2c9c70", "#c03860", "#3070c8", "#d8a020", "#7848b0", "#209aa8", "#88a030"]
TROUSERS = ["#304878", "#3c3028", "#283828", "#584060", "#20303c", "#5c2c20"]


# --- small render helpers ---------------------------------------------------------------------------------------
def dither(v: np.ndarray, lo: int = 0, hi: int = 3) -> np.ndarray:
    """v holds continuous shades; returns palette indices with a 4x4 ordered dither between neighbouring shades."""
    h, w = v.shape
    th = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w]
    base = np.floor(v)
    idx = base + ((v - base) > th)
    return np.clip(idx, lo, hi).astype(np.uint8)


def hash2(x: np.ndarray, y: np.ndarray, seed: int) -> np.ndarray:
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def vnoise(size: int, cell: int, seed: int, period: int | None = None) -> np.ndarray:
    """Tileable value noise: lattice every `cell` px, wrapping at `period` lattice points."""
    period = period or size // cell
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    gx, gy = xs / cell, ys / cell
    x0, y0 = np.floor(gx).astype(int), np.floor(gy).astype(int)
    fx, fy = gx - x0, gy - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

    def h(a: np.ndarray, b: np.ndarray) -> np.ndarray:
        return hash2(a % period, b % period, seed)

    a = h(x0, y0) * (1 - fx) + h(x0 + 1, y0) * fx
    b = h(x0, y0 + 1) * (1 - fx) + h(x0 + 1, y0 + 1) * fx
    return a * (1 - fy) + b * fy


def grid(n: int = 16) -> tuple[np.ndarray, np.ndarray]:
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float32)
    return xs + 0.5, ys + 0.5


def outline(mask: np.ndarray) -> np.ndarray:
    """cells outside `mask` that touch it (4-neighbour)"""
    m = mask
    o = np.zeros_like(m)
    o[1:, :] |= m[:-1, :]
    o[:-1, :] |= m[1:, :]
    o[:, 1:] |= m[:, :-1]
    o[:, :-1] |= m[:, 1:]
    return o & ~m


# --- background metatiles (16x16, one palette each) ---------------------------------------------------------------
def carpet_shade(seed: int) -> np.ndarray:
    x, y = grid()
    n = vnoise(16, 4, seed) * 0.6 + vnoise(16, 2, seed + 7) * 0.4
    return 0.2 + n * 1.1 + 0.25 * (((x.astype(int) + y.astype(int)) % 4) == 0)


def carpet(seed: int, stain: bool = False) -> np.ndarray:
    x, y = grid()
    n = vnoise(16, 4, seed) * 0.6 + vnoise(16, 2, seed + 7) * 0.4
    v = 1.15 + n * 1.3
    # the loop pile: a faint diagonal weave
    v += 0.25 * (((x.astype(int) + y.astype(int)) % 4) == 0)
    if stain:
        d = np.hypot(x - 10, y - 6) / 4.5
        v -= np.clip(1.2 - d, 0, 1) * 0.9
    return dither(v)


def foam(seed: int) -> np.ndarray:
    x, y = grid()
    n = vnoise(16, 5, seed) * 0.55 + vnoise(16, 2, seed + 3) * 0.45
    v = 0.6 + n * 2.6
    # light from the upper left: puffs read as soft lumps
    v += (vnoise(16, 8, seed + 11) - 0.5) * 0.8
    return dither(v)


def wall_cap(seed: int) -> np.ndarray:
    x, y = grid()
    n = vnoise(16, 4, seed)
    v = 0.4 + n * 0.9
    v = np.where((x < 1) | (y < 1), 1.4, v)
    v = np.where((x > 15) | (y > 15), 0.2, v)
    return dither(v, 0, 1)


def wall_front(seed: int, vent: bool = False) -> np.ndarray:
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    stripe = (xi % 4 == 1).astype(np.float32)
    v = 2.35 - stripe * 0.7 + (vnoise(16, 4, seed) - 0.5) * 0.5
    # damp stain bleeding down from the top
    v -= np.clip(vnoise(16, 8, seed + 5) - 0.55, 0, 1) * 3 * np.clip(1 - y / 12, 0, 1)
    v = np.where(yi <= 2, 0.6 + (vnoise(16, 4, seed + 1)) * 0.6, v)  # concrete lip
    v = np.where(yi == 3, 0.0, v)  # shadow under the lip
    v = np.where(yi >= 13, 1.2 - (yi == 13) * 1.2, v)  # baseboard
    if vent:
        inside = (xi >= 5) & (xi <= 10) & (yi >= 6) & (yi <= 10)
        v = np.where(inside, np.where(yi % 2 == 0, 0.0, 1.0), v)
        v = np.where(((xi == 4) | (xi == 11)) & (yi >= 5) & (yi <= 11), 3.0, v)
        v = np.where(((yi == 5) | (yi == 11)) & (xi >= 4) & (xi <= 11), 3.0, v)
    return dither(v)


def drywall(seed: int, cracked: bool = False) -> np.ndarray:
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    v = 1.9 + (vnoise(16, 4, seed) - 0.5) * 0.6 + (1 - (x + y) / 32) * 0.6
    v = np.where((xi == 0) | (yi == 0), 3.0, v)
    v = np.where((xi == 15) | (yi == 15), 0.0, v)
    v = np.where((yi == 7) & (xi > 0) & (xi < 15), 1.0, v)  # panel seam
    for sx in (4, 11):  # screw heads
        v = np.where((xi == sx) & ((yi == 3) | (yi == 11)), 0.0, v)
    if cracked:
        # a branching crack from the upper right
        pts = [(13, 1), (11, 4), (12, 6), (9, 8), (10, 11), (7, 13), (5, 14)]
        crack = np.zeros((16, 16), bool)
        for (ax, ay), (bx, by) in zip(pts, pts[1:], strict=False):
            for t in np.linspace(0, 1, 12):
                crack[round(ay + (by - ay) * t), round(ax + (bx - ax) * t)] = True
        crack[9:11, 6:8] = True
        v = np.where(crack, 0.0, v)
    return dither(v)


def crt(seed: int, angle: int) -> np.ndarray:
    """an old CRT set seen from the front: a taupe case, a bulging cyan screen, a vent and a knob"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    case = (xi >= 1) & (xi <= 14) & (yi >= 1) & (yi <= 14)
    v = np.full((16, 16), 0.0, np.float32)
    shade = 1.0 + (vnoise(16, 4, seed) - 0.5) * 0.3 + np.where(xi < 3, 0.25, 0) - np.where(xi > 12, 0.5, 0)
    v = np.where(case, shade, 0.0)
    v = np.where(case & (yi == 1), 1.6, v)  # top edge catches the light
    v = np.where(case & (yi >= 13), 0.4, v)  # base in shadow
    # screen: rounded rect with a highlight
    sx0, sx1 = (2, 11) if angle == 0 else (3, 12)
    scr = (xi >= sx0) & (xi <= sx1) & (yi >= 3) & (yi <= 11)
    corner = ((xi == sx0) | (xi == sx1)) & ((yi == 3) | (yi == 11))
    scr &= ~corner
    glow = 2.0 + 0.5 * np.cos((x - (sx0 + sx1) / 2) / 3.5) * np.cos((y - 7) / 3.2)
    glow += 0.25 * ((yi % 2) == 0)  # scanlines
    v = np.where(scr, glow, v)
    v = np.where(scr & (yi == 4) & (xi >= sx0 + 1) & (xi <= sx0 + 3), 3.0, v)
    v = np.where(scr & (yi == 5) & (xi == sx0 + 1), 3.0, v)
    # bezel
    v = np.where(outline(scr) & case, 0.0, v)
    # knob and vent on the side panel
    kx = 13 if angle == 0 else 2
    v = np.where((xi == kx) & ((yi == 4) | (yi == 7)), 3.0, v)
    v = np.where((xi == kx) & (yi >= 9) & (yi <= 11) & (yi % 2 == 1), 0.0, v)
    idx = dither(v)
    # the screen keeps index 2/3 only (cyan, white); the case keeps 0/1 (charcoal, taupe)
    idx = np.where(scr, np.clip(idx, 2, 3), np.where(case, np.clip(idx, 0, 1), idx))
    idx = np.where(case & (yi == 1) & (xi > 1) & (xi < 14) & (idx == 1) & ((xi % 3) == 0), 3, idx)
    return idx.astype(np.uint8)


def token() -> np.ndarray:
    """a brass arcade token with a star cut through its middle, on carpet-dark ground"""
    x, y = grid()
    d = np.hypot(x - 8, y - 8)
    coin = d <= 6.2
    rim = coin & (d > 4.6)
    light = (8 - x + 8 - y) / 16  # upper-left light
    v = np.where(coin, 1.6 + light * 1.2, 0.0)
    v = np.where(rim, 2.2 + light * 0.9, v)
    # five-point star in the face
    a = np.arctan2(y - 8, x - 8) + math.pi / 2
    star_r = 2.9 * (0.55 + 0.45 * np.cos(5 * a) ** 2 * (np.cos(5 * a) > 0))
    star = d <= star_r
    v = np.where(star, 3.0, v)
    v = np.where(outline(coin), 0.0, v)
    ground = dither(np.clip(carpet_shade(12), 0, 1.0))
    coin_idx = dither(v)
    # the coin uses brass (2) and its glint (3); its rim shadow is the carpet's dark
    coin_idx = np.where(coin, np.clip(coin_idx, 2, 3), coin_idx)
    coin_idx = np.where(coin & rim & (light < 0.35), 1, coin_idx)
    return np.where(coin | outline(coin), coin_idx, ground).astype(np.uint8)


def keycard() -> np.ndarray:
    """a WATCH keycard on a lanyard clip: blue card, magenta stripe, chip"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    card = (xi >= 2) & (xi <= 13) & (yi >= 4) & (yi <= 13)
    card &= ~(((xi == 2) | (xi == 13)) & ((yi == 4) | (yi == 13)))
    v = np.where(card, 2.0, 0.0)
    v = np.where(card & (yi >= 6) & (yi <= 7), 1.0, v)  # magenta stripe
    v = np.where(card & (xi >= 4) & (xi <= 6) & (yi >= 9) & (yi <= 11), 3.0, v)  # chip
    v = np.where(card & (xi >= 8) & (xi <= 12) & ((yi == 10) | (yi == 12)), 3.0, v)  # printed lines
    v = np.where(card & (xi == 3) & (yi >= 5) & (yi <= 12), 3.0, v)  # edge highlight
    clip = (xi >= 6) & (xi <= 9) & (yi >= 1) & (yi <= 4)
    v = np.where(clip, np.where((xi == 7) | (xi == 8), 0.0, 3.0), v)
    v = np.where(clip & (yi == 1), 3.0, v)
    v = np.where(outline(card) & ~clip, 0.0, v)
    return np.clip(np.round(v), 0, 3).astype(np.uint8)


def door(open_: bool) -> np.ndarray:
    """the egress door set into the north wall, an EXIT sign above it"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    v = np.full((16, 16), 0.0, np.float32)
    frame = (xi >= 1) & (xi <= 14) & (yi >= 4)
    leaf = (xi >= 3) & (xi <= 12) & (yi >= 6)
    v = np.where(frame, 1.0, v)
    if open_:
        v = np.where(leaf, 0.0, v)
        # light spilling out of the doorway
        spill = leaf & (yi >= 6)
        g = np.clip((yi - 6) / 10, 0, 1)
        v = np.where(spill, 2.0 + g * 1.0 - (xi % 2 == yi % 2) * 0.6, v)
    else:
        grain = (vnoise(16, 3, 41) - 0.5) * 0.6
        v = np.where(leaf, 1.3 + grain + (xi == 3) * 0.6, v)
        v = np.where(leaf & (xi == 8), 0.0, v)  # the leaves meet
        v = np.where((xi == 10) & (yi == 11), 3.0, v)  # handle
        v = np.where((xi == 6) & (yi == 11), 3.0, v)
        v = np.where(leaf, np.clip(v, 0.0, 1.0), v)
    # the EXIT sign: green box with dark letters
    sign = (xi >= 3) & (xi <= 12) & (yi >= 0) & (yi <= 3)
    v = np.where(sign, 2.0, v)
    letters = {(4, 1), (4, 2), (5, 1), (5, 2), (7, 1), (8, 2), (8, 1), (7, 2), (10, 1), (10, 2), (11, 1)}
    for lx, ly in letters:
        v = np.where((xi == lx) & (yi == ly), 0.0 if not open_ else 3.0, v)
    return dither(v)


# --- people (seeded colours at run time) --------------------------------------------------------------------------
# two layers per 16x16 frame: head (o outline, s skin, h hair) and body (O outline, c hoodie, d trousers)
PLAYER_DOWN = [
    "................",
    ".....oooooo.....",
    "....ohhhhhho....",
    "...ohhhhhhhho...",
    "...ohhhhhhhho...",
    "...ohssssssho...",
    "...ossossosso...",
    "...ossssssssо...",
    "....oossssoo....",
    "...OccccccccO...",
    "..OccOccccOccO..",
    "..OccOccccOccO..",
    "..OsOccccccOsO..",
    "...OOddddddOO...",
    "....OddOOddO....",
    "....OOO..OOO....",
]
PLAYER_UP = [
    "................",
    ".....oooooo.....",
    "....ohhhhhho....",
    "...ohhhhhhhho...",
    "...ohhhhhhhho...",
    "...ohhhhhhhho...",
    "...ohhhhhhhho...",
    "...oshhhhhhso...",
    "....oohhhhoo....",
    "...OccccccccO...",
    "..OccOcddcOccO..",
    "..OccOccccOccO..",
    "..OsOccccccOsO..",
    "...OOddddddOO...",
    "....OddOOddO....",
    "....OOO..OOO....",
]
PLAYER_SIDE = [
    "................",
    ".....ooooo......",
    "....ohhhhho.....",
    "...ohhhhhhho....",
    "...ohhhhhhsso...",
    "...ohhhhsssso...",
    "...ohhhhssoso...",
    "...ohhhsssssso..",
    "....oohssssoo...",
    "....OccccccO....",
    "....OcOccccO....",
    "....OcOccccO....",
    "....OsOccccO....",
    "....OOddddOO....",
    ".....OddOdO.....",
    ".....OOO.OOO....",
]
ROLL = [
    "................",
    "................",
    "................",
    "................",
    ".....oooooo.....",
    "....ohhhhhhO....",
    "...ohhhhhhccO...",
    "...ohhhhhcccO...",
    "..OccchhhcccdO..",
    "..OcccchccccdO..",
    "..OcccccccccdO..",
    "...OcccccccddO..",
    "...OddcccddddO..",
    "....OOddddOOO...",
    "......OOOO......",
    "................",
]

HEAD_CH = {"o": 1, "s": 2, "h": 3, "о": 1}
BODY_CH = {"O": 1, "c": 2, "d": 3}


def fix(rows: list[str]) -> list[str]:
    out = []
    for r in rows:
        r = r.replace("о", "o")
        r = (r + "." * 16)[:16]
        out.append(r)
    return out


def walk(rows: list[str], phase: int, side: bool) -> list[str]:
    r = list(rows)
    if side:
        legs = [[".....OddOdO.....", ".....OOO.OOO...."], ["....OdO.OddO....", "...OOO...OOO...."]][phase]
    else:
        legs = [["....OddOOddO....", "....OOO...OO...."], ["....OddOOddO....", "....OO...OOO...."]][phase]
    r[14], r[15] = legs
    if not side:
        # the arms swing opposite to the legs
        if phase == 0:
            r[12] = "..OsOccccccOcO.."
        else:
            r[12] = "..OcOccccccOsO.."
    return r


def attack(rows: list[str], side: bool) -> list[str]:
    r = list(rows)
    if side:
        r[10] = "....OccccccOsO.."
        r[11] = "....OccccccOO..."
        r[12] = "....OccccccO...."
    else:
        r[12] = "..OsOccccccO...."
        r[13] = "...OOddddddOOsO."
    return r


def layers(rows: list[str]) -> tuple[np.ndarray, np.ndarray]:
    rows = fix(rows)
    a = np.zeros((16, 16), np.uint8)
    b = np.zeros((16, 16), np.uint8)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in HEAD_CH:
                a[y, x] = HEAD_CH[ch]
            elif ch in BODY_CH:
                b[y, x] = BODY_CH[ch]
    return a, b


# --- enemies and effects ------------------------------------------------------------------------------------------
def mite(frame: int) -> np.ndarray:
    """a static mite: a crawling scrap of TV static with six legs and two orange eyes (top-down, facing down)"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    lift = 1 if frame == 2 else 0
    body = ((x - 8) / 5.6) ** 2 + ((y - 8.5 + lift) / 4.4) ** 2 <= 1
    noise = hash2(xi, yi, 17 + frame * 5) > 0.45
    v = np.where(body, np.where(noise, 2, 1), 0)
    legs = np.zeros((16, 16), bool)
    sw = 1 if frame == 1 else 0
    for k, ly in enumerate((6, 9, 12)):
        off = (sw if k % 2 == 0 else -sw) - lift
        for lx in (1, 2):
            legs[min(15, max(0, ly + off - (lx == 1))), lx] = True
            legs[min(15, max(0, ly - off - (lx == 1))), 15 - lx] = True
    v = np.where(legs & ~body, 1, v)
    for ex in (6, 9):
        v = np.where((xi == ex) & (yi == 11 - lift), 3, v)
        v = np.where((xi == ex) & (yi == 12 - lift), 3 if frame == 2 else 2, v)
    # antennae
    v = np.where(((xi == 5) & (yi == 14 - lift)) | ((xi == 10) & (yi == 14 - lift)), 1, v)
    v = np.where(outline(body) & (v == 0), 1, v)
    return v.astype(np.uint8)


def eye(frame: int) -> np.ndarray:
    """a WATCH camera eye: a floating sclera with fins; frame 1 has the iris dilated (the tell)"""
    x, y = grid()
    xi, yi = x.astype(int), y.astype(int)
    d = np.hypot(x - 8, y - 8)
    ball = d <= 5.6
    v = np.where(ball, 2.0, 0.0)
    v = np.where(ball & (x + y < 11), 2.0, v)
    ir = 3.2 if frame == 1 else 2.2
    di = np.hypot(x - 8, y - 9)
    v = np.where(di <= ir, 3.0, v)
    v = np.where(di <= (1.6 if frame == 1 else 0.9), 1.0, v)
    v = np.where((xi == 6) & (yi == 5), 2.0, v)  # glint stays white
    fins = ((xi <= 1) | (xi >= 14)) & (yi >= 6) & (yi <= 9)
    fins |= ((xi == 2) | (xi == 13)) & (yi >= 7) & (yi <= 8)
    v = np.where(fins, 1.0, v)
    v = np.where(outline(ball) & ~fins, 1.0, v)
    # lashes: shutter blades top and bottom
    v = np.where(((yi == 1) & (xi >= 6) & (xi <= 9)) | ((yi == 14) & (xi >= 6) & (xi <= 9)), 1.0, v)
    return v.astype(np.uint8)


def orb() -> np.ndarray:
    x, y = grid(8)
    d = np.hypot(x - 4, y - 4)
    v = np.where(d <= 3.3, 2.0, 0.0)
    v = np.where(d <= 1.8, 3.0, v)
    v = np.where((d > 2.6) & (d <= 3.3), 1.0, v)
    o = np.zeros((16, 8), np.uint8)
    o[:8] = v.astype(np.uint8)
    return o


def arc(direction: str, frame: int) -> np.ndarray:
    """the lead's lash: a cable arc in front of the player (drawn for 'down'; rotated for the others)"""
    x, y = grid()
    cx, cy = 8.0, -2.0
    d = np.hypot(x - cx, y - cy)
    a = np.degrees(np.arctan2(y - cy, x - cx))
    span = (30, 150) if frame == 0 else (15, 165)
    band = (d >= 8.8) & (d <= 13.2) & (a >= span[0]) & (a <= span[1])
    core = (d >= 9.8) & (d <= 12.2) & band
    v = np.where(band, 1, 0)
    v = np.where(core, 2, v)
    tip = (a >= span[1] - 14) & core
    v = np.where(tip, 3, v)
    sparks = band & (hash2(x.astype(int), y.astype(int), 99 + frame) > 0.85)
    v = np.where(sparks, 3, v).astype(np.uint8)
    if direction == "down":
        return v
    if direction == "up":
        return v[::-1, :]
    if direction == "right":
        return v.T
    raise ValueError(direction)


def spark() -> np.ndarray:
    o = np.zeros((16, 8), np.uint8)
    pts = {(3, 0): 3, (3, 1): 2, (3, 2): 3, (3, 3): 2, (3, 4): 3, (1, 2): 2, (2, 2): 3, (4, 2): 3, (5, 2): 2}
    for (px, py), c in pts.items():
        o[py + 5, px] = c
    o[8, 3] = 1
    return o


def puff() -> np.ndarray:
    x, y = grid(8)
    d = np.hypot(x - 4, y - 5)
    v = np.where(d <= 3.4, 1.0, 0.0)
    v = np.where((d <= 2.2) & (x + y < 9), 2.0, v)
    v = np.where((d > 2.8) & (d <= 3.4) & (hash2(x.astype(int), y.astype(int), 3) > 0.5), 0.0, v)
    o = np.zeros((16, 8), np.uint8)
    o[:8] = v.astype(np.uint8)
    return o


# --- the font and HUD icons (8x8) ---------------------------------------------------------------------------------
FONT = {
    "0": ["0111", "1001", "1011", "1101", "1001", "1001", "0110"],
    "1": ["0010", "0110", "0010", "0010", "0010", "0010", "0111"],
    "2": ["0110", "1001", "0001", "0010", "0100", "1000", "1111"],
    "3": ["1110", "0001", "0001", "0110", "0001", "0001", "1110"],
    "4": ["1001", "1001", "1001", "1111", "0001", "0001", "0001"],
    "5": ["1111", "1000", "1110", "0001", "0001", "1001", "0110"],
    "6": ["0110", "1000", "1000", "1110", "1001", "1001", "0110"],
    "7": ["1111", "0001", "0010", "0010", "0100", "0100", "0100"],
    "8": ["0110", "1001", "1001", "0110", "1001", "1001", "0110"],
    "9": ["0110", "1001", "1001", "0111", "0001", "0001", "0110"],
    "A": ["0110", "1001", "1001", "1111", "1001", "1001", "1001"],
    "B": ["1110", "1001", "1001", "1110", "1001", "1001", "1110"],
    "C": ["0111", "1000", "1000", "1000", "1000", "1000", "0111"],
    "D": ["1110", "1001", "1001", "1001", "1001", "1001", "1110"],
    "E": ["1111", "1000", "1000", "1110", "1000", "1000", "1111"],
    "F": ["1111", "1000", "1000", "1110", "1000", "1000", "1000"],
    "G": ["0111", "1000", "1000", "1011", "1001", "1001", "0111"],
    "H": ["1001", "1001", "1001", "1111", "1001", "1001", "1001"],
    "I": ["111", "010", "010", "010", "010", "010", "111"],
    "J": ["0001", "0001", "0001", "0001", "1001", "1001", "0110"],
    "K": ["1001", "1010", "1100", "1000", "1100", "1010", "1001"],
    "L": ["1000", "1000", "1000", "1000", "1000", "1000", "1111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["1001", "1101", "1101", "1011", "1011", "1001", "1001"],
    "O": ["0110", "1001", "1001", "1001", "1001", "1001", "0110"],
    "P": ["1110", "1001", "1001", "1110", "1000", "1000", "1000"],
    "Q": ["0110", "1001", "1001", "1001", "1011", "1001", "0111"],
    "R": ["1110", "1001", "1001", "1110", "1100", "1010", "1001"],
    "S": ["0111", "1000", "1000", "0110", "0001", "0001", "1110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["1001", "1001", "1001", "1001", "1001", "1001", "0110"],
    "V": ["1001", "1001", "1001", "1001", "1001", "0110", "0110"],
    "W": ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    "X": ["1001", "1001", "0110", "0110", "0110", "1001", "1001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["1111", "0001", "0010", "0110", "0100", "1000", "1111"],
    ".": ["0", "0", "0", "0", "0", "0", "1"],
    ",": ["00", "00", "00", "00", "00", "01", "10"],
    ":": ["0", "1", "0", "0", "0", "1", "0"],
    "!": ["1", "1", "1", "1", "1", "0", "1"],
    "?": ["0110", "1001", "0001", "0010", "0100", "0000", "0100"],
    "-": ["000", "000", "000", "111", "000", "000", "000"],
    "+": ["000", "000", "010", "111", "010", "000", "000"],
    "/": ["0001", "0001", "0010", "0110", "0100", "1000", "1000"],
    "'": ["1", "1", "0", "0", "0", "0", "0"],
    ">": ["100", "010", "001", "001", "001", "010", "100"],
    "x": ["000", "000", "101", "010", "101", "000", "000"],
}
FONT_ORDER = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,:!?-+/'>x"


def glyph(ch: str) -> np.ndarray:
    t = np.zeros((8, 8), np.uint8)
    if ch == " ":
        return t
    rows = FONT[ch]
    w = len(rows[0])
    ox = (8 - w) // 2 if w < 4 else 1
    for y, row in enumerate(rows):
        for x, bit in enumerate(row):
            if bit == "1":
                t[y, ox + x] = 3
                if y + 1 < 8 and ox + x + 1 < 8 and t[y + 1, ox + x + 1] == 0:
                    t[y + 1, ox + x + 1] = 1  # drop shadow
    for y in range(8):
        for x in range(8):
            if t[y, x] == 1 and any(
                (rows[y - 1][x - ox - 1] == "1" if 0 <= y - 1 < 7 and 0 <= x - ox - 1 < w else False) for _ in [0]
            ):
                pass
    return t


def heart(fill: int) -> np.ndarray:
    """HUD heart: 2 full, 1 half, 0 empty (the floor's HUD palette: ink, pink, accent, white). An empty heart is a
    dithered pink outline, so it reads as a socket on the ink bar"""
    m = ["01100110", "11111111", "11111111", "11111111", "01111110", "00111100", "00011000", "00000000"]
    mask = np.array([[c == "1" for c in r] for r in m])
    t = np.zeros((8, 8), np.uint8)
    edge = mask & ~(np.roll(mask, 1, 0) & np.roll(mask, -1, 0) & np.roll(mask, 1, 1) & np.roll(mask, -1, 1))
    for y in range(8):
        for x in range(8):
            if not mask[y, x]:
                continue
            filled = fill == 2 or (fill == 1 and x < 4)
            if filled:
                t[y, x] = 1
            elif edge[y, x] and (x + y) % 2 == 0:
                t[y, x] = 1
    if fill:
        t[1, 1] = 3
    return t


def small_token() -> np.ndarray:
    m = ["00111100", "01222210", "12233221", "12333321", "12233221", "12222221", "01222210", "00111100"]
    return np.array([[int(c) for c in r] for r in m], np.uint8)


def small_key() -> np.ndarray:
    m = ["00033000", "00300300", "00033000", "02222220", "02111120", "02233220", "02222220", "00000000"]
    return np.array([[int(c) for c in r] for r in m], np.uint8)


def rim() -> np.ndarray:
    """the HUD's rim line (the floor's HUD palette): a dotted white line over a pink one, level with the charge bar"""
    t = np.zeros((8, 8), np.uint8)
    t[3, ::2] = 3
    t[4, :] = 1
    return t


def bar(n: int) -> np.ndarray:
    """charge bar segment, n of 8 columns lit (CRT palette: cyan = lit)"""
    t = np.zeros((8, 8), np.uint8)
    t[2:6, :] = 1
    t[3:5, :n] = 2
    return t


# --- talkers' faces (32x32 background tiles, 4x4; rows 0-2 take palette A, row 3 palette B) -----------------------
# palette A: 0 the landing's dark, 1 skin (or main), 2 hair (or second), 3 light (eyes, glints)
# palette B: 0 the dark, 1 cloth, 2 cloth shade, 3 accent
def face_human(hair: str, accessory: str, seed: int) -> np.ndarray:
    x, y = grid(32)
    xi, yi = x.astype(int), y.astype(int)
    v = np.zeros((32, 32), np.uint8)
    head = ((x - 16) / 7.6) ** 2 + ((y - 13) / 9.0) ** 2 <= 1
    ears = (((x - 7.8) / 1.6) ** 2 + ((y - 14) / 2.4) ** 2 <= 1) | (((x - 24.2) / 1.6) ** 2 + ((y - 14) / 2.4) ** 2 <= 1)
    neck = (xi >= 13) & (xi <= 18) & (yi >= 20) & (yi <= 24)
    skin = head | ears | neck
    v[skin] = 1
    # cheek shadow: dither skin toward the dark on the right side
    shade = skin & (x > 19) & ((xi + yi) % 2 == 0) & (y > 10)
    v[shade & ~ears] = 1
    # hair
    if hair == "short":
        h = head & (y < 9 + 2 * np.abs(np.sin((x - 16) / 3)))
    elif hair == "long":
        h = (head & (y < 9)) | ((((x - 16) / 9.2) ** 2 + ((y - 15) / 11) ** 2 <= 1) & ~head & (y < 25))
    elif hair == "mohawk":
        h = head & (np.abs(x - 16) < 2.5) & (y < 11)
        h |= (np.abs(x - 16) < 2.2) & (y >= 1) & (y < 6)
    elif hair == "cap":
        h = head & (y < 8)
        h |= (yi == 8) & (xi >= 8) & (xi <= 27)  # brim
        h |= (yi == 9) & (xi >= 18) & (xi <= 27)
    else:  # curls
        h = np.zeros((32, 32), bool)
        for cx, cy in ((10, 7), (13, 4), (17, 3), (21, 5), (23, 9), (9, 11), (24, 12)):
            h |= ((x - cx) ** 2 + (y - cy) ** 2) <= 9.5
        h &= ~(head & (y > 9))
    v[h] = 2
    # eyes, brows, nose, mouth
    for ex in (12, 20):
        v[12:14, ex - 1 : ex + 1] = 3
        v[13, ex] = 0 if True else 3
        v[10, ex - 2 : ex + 1] = 2 if hair != "cap" else 0
    v[15:17, 16] = 0
    v[18, 14:19] = 0
    v[19, 15:18] = 1
    if accessory == "headset":
        band = (np.abs(np.hypot(x - 16, y - 12) - 10.2) < 0.8) & (y < 13)
        v[band] = 3
        v[12:16, 6:8] = 3
        v[16, 8:13] = 3
        v[17, 12:14] = 3
    elif accessory == "glasses":
        for ex in (12, 20):
            ring = (np.abs(np.hypot(x - ex, y - 13) - 3.0) < 0.6)
            v[ring] = 0
        v[13, 15:18] = 0
    elif accessory == "scar":
        for k in range(5):
            v[9 + k, 21 + (k % 2)] = 0
    # shoulders (palette B)
    body = (((x - 16) / 14) ** 2 + ((y - 34) / 10) ** 2 <= 1) & (y >= 22)
    collar = body & (np.abs(x - 16) < 4) & (y < 26)
    v[body & (yi >= 24)] = 1
    v[body & (yi >= 24) & (x > 20) & ((xi + yi) % 2 == 0)] = 2
    v[collar & (yi >= 24)] = 3
    if accessory == "keys":
        for k, (kx, ky) in enumerate(((9, 27), (11, 28), (10, 30))):
            v[ky, kx] = 3
    return v


def face_ghost() -> np.ndarray:
    x, y = grid(32)
    xi, yi = x.astype(int), y.astype(int)
    v = np.zeros((32, 32), np.uint8)
    top = ((x - 16) / 10) ** 2 + ((y - 13) / 10) ** 2 <= 1
    skirt = (y >= 13) & (np.abs(x - 16) <= 10 + (y - 13) * 0.35) & (y < 31 - 2 * (np.sin(x / 2.2) > 0))
    sheet = top | skirt
    v[sheet] = 1
    v[sheet & (x + y > 34) & ((xi + yi) % 2 == 0)] = 2  # folds in shadow
    v[sheet & ((xi == 12) | (xi == 21)) & (yi > 18)] = 2
    for ex in (12, 20):
        hole = ((x - ex) / 2.2) ** 2 + ((y - 12) / 3.0) ** 2 <= 1
        v[hole] = 0
    v[8, 10:13] = 3  # light on the sheet's crown
    v[7, 13:18] = 3
    return v


def face_camera() -> np.ndarray:
    x, y = grid(32)
    xi, yi = x.astype(int), y.astype(int)
    v = np.zeros((32, 32), np.uint8)
    housing = (xi >= 5) & (xi <= 26) & (yi >= 4) & (yi <= 19)
    v[housing] = 1
    v[housing & (yi == 4)] = 3
    v[housing & (xi >= 22) & ((xi + yi) % 2 == 0)] = 2
    lens = np.hypot(x - 14, y - 12) <= 5.5
    v[lens] = 2
    v[np.hypot(x - 14, y - 12) <= 3.2] = 0
    v[np.hypot(x - 13, y - 11) <= 1.0] = 3
    v[(yi >= 6) & (yi <= 7) & (xi >= 22) & (xi <= 24)] = 3  # a tally light
    neck = (xi >= 13) & (xi <= 18) & (yi >= 20) & (yi <= 24)
    v[neck] = 2
    body = (((x - 16) / 14) ** 2 + ((y - 34) / 10) ** 2 <= 1) & (y >= 24)
    v[body] = 1
    v[body & (np.abs(x - 16) < 2)] = 3  # tie
    v[body & (x > 20) & ((xi + yi) % 2 == 0)] = 2
    return v


def face_mold() -> np.ndarray:
    x, y = grid(32)
    xi, yi = x.astype(int), y.astype(int)
    v = np.zeros((32, 32), np.uint8)
    n = vnoise(32, 6, 77)
    blob = ((x - 16) / 12) ** 2 + ((y - 15) / 11) ** 2 <= 0.8 + n * 0.5
    v[blob] = 1
    v[blob & (hash2(xi, yi, 5) > 0.6)] = 2
    v[blob & (vnoise(32, 3, 9) > 0.62)] = 3
    for rx in (9, 14, 19, 24):  # roots dripping
        for ry in range(20, 31):
            if hash2(np.array(rx), np.array(ry), 3) > 0.25 and ry < 22 + rx % 9:
                v[ry, rx + (ry % 3 == 0)] = 2
    for ex in (12, 19):
        v[13:15, ex : ex + 2] = 0
    v[19, 13:19] = 0
    v[20, 14:18] = 0
    return v


FACES = [
    ("COMPILE", lambda: face_human("short", "headset", 1), pal("#0c0c14", "#c08860", "#3a2418", "#f0f0f8"),
     pal("#0c0c14", "#3a6ab0", "#20406c", "#f0f0f8")),
    ("CHARGE", lambda: face_human("mohawk", "scar", 2), pal("#0c0c14", "#c08860", "#e04020", "#f8f0e0"),
     pal("#0c0c14", "#303030", "#181818", "#f8c030")),
    ("SKY", face_ghost, pal("#0c0c14", "#d8dcf0", "#8890b8", "#ffffff"),
     pal("#0c0c14", "#d8dcf0", "#8890b8", "#ffffff")),
    ("WATCH", face_camera, pal("#0c0c14", "#a8b0c0", "#384058", "#ff4040"),
     pal("#0c0c14", "#283048", "#141828", "#d8d8e0")),
    ("ROOT", face_mold, pal("#0c0c14", "#a86c78", "#5a3040", "#e8c8b8"),
     pal("#0c0c14", "#a86c78", "#5a3040", "#e8c8b8")),
    ("LOCK", lambda: face_human("cap", "keys", 6), pal("#0c0c14", "#c08860", "#405838", "#f0f0e0"),
     pal("#0c0c14", "#5a6048", "#383c2c", "#e0c040")),
]
HUMAN_FACES = {"COMPILE", "CHARGE", "LOCK"}


# --- THE WATCHMAN (32x32 sprite, eye palette: navy, housing light, red lens) ------------------------------------
def watchman(frame: int) -> np.ndarray:
    """0 watching, 1 the tell (lens flared), 2 stunned (shutters closed)"""
    x, y = grid(32)
    xi, yi = x.astype(int), y.astype(int)
    v = np.zeros((32, 32), np.uint8)
    arm = (xi >= 14) & (xi <= 17) & (yi <= 5)
    v[arm] = 1
    v[arm & (xi == 15)] = 2
    dome = (((x - 16) / 13) ** 2 + ((y - 17) / 12) ** 2 <= 1) & (y >= 5)
    v[dome] = 2
    v[dome & (x + y > 36) & ((xi + yi) % 2 == 0)] = 1  # shade, dithered
    v[outline(dome)] = 1
    v[(yi == 12) & dome] = 1  # a seam
    lens_r = 7.2 if frame == 1 else 6.0
    d = np.hypot(x - 16, y - 19)
    v[(d <= lens_r) & dome] = 1
    v[(d <= lens_r - 1.4) & dome] = 3
    v[(d <= (3.2 if frame == 1 else 2.0)) & dome] = 1
    v[(np.hypot(x - 14, y - 17) <= 1.0)] = 2  # glint
    if frame == 2:  # shutters closed across the lens
        v[(np.abs(y - 19) <= lens_r) & (d <= lens_r) & (yi % 2 == 0)] = 1
        v[(np.abs(y - 19) <= lens_r) & (d <= lens_r) & (yi % 2 == 1)] = 2
    for fx in (2, 3, 28, 29):  # side fins
        v[14:22, fx] = 1
    v[15:21, 1] = 1
    v[15:21, 30] = 1
    if frame == 1:
        for (sx, sy) in ((4, 4), (27, 5), (6, 29), (26, 28)):
            v[sy, sx] = 3
    return v


# --- tiles, encoding, output ----------------------------------------------------------------------------------------
def encode(tile: np.ndarray) -> bytes:
    out = bytearray()
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = int(tile[y, x])
            lo |= (v & 1) << (7 - x)
            hi |= ((v >> 1) & 1) << (7 - x)
        out += bytes((lo, hi))
    return bytes(out)


class TileSet:
    def __init__(self, dedupe: bool = True) -> None:
        self.tiles: list[bytes] = []
        self.index: dict[bytes, int] = {}
        self.dedupe = dedupe

    def add(self, tile: np.ndarray) -> int:
        b = encode(tile)
        if self.dedupe and b in self.index:
            return self.index[b]
        self.tiles.append(b)
        self.index.setdefault(b, len(self.tiles) - 1)
        return len(self.tiles) - 1

    def add_meta(self, m: np.ndarray) -> list[int]:
        """16x16 -> [TL, TR, BL, BR] background tiles"""
        return [self.add(m[0:8, 0:8]), self.add(m[0:8, 8:16]), self.add(m[8:16, 0:8]), self.add(m[8:16, 8:16])]

    def add_sprite16(self, m: np.ndarray) -> int:
        """16x16 -> four tiles in 8x16 order (TL, BL, TR, BR); returns the first"""
        first = len(self.tiles)
        for t in (m[0:8, 0:8], m[8:16, 0:8], m[0:8, 8:16], m[8:16, 8:16]):
            self.tiles.append(encode(t))
        return first

    def add_sprite8(self, m: np.ndarray) -> int:
        first = len(self.tiles)
        self.tiles.append(encode(m[0:8]))
        self.tiles.append(encode(m[8:16]))
        return first


def c_bytes(name: str, data: bytes, kind: str = "uint8_t") -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i : i + 16]) for i in range(0, len(data), 16)]
    return f"const {kind} {name}[{len(data)}] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def c_words(name: str, data: list[int]) -> str:
    rows = [", ".join(f"0x{w:04x}" for w in data[i : i + 8]) for i in range(0, len(data), 8)]
    return f"const uint16_t {name}[{len(data)}] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


METAS = [
    ("CARPET_A", 0, lambda: carpet(1)),
    ("CARPET_B", 0, lambda: carpet(2)),
    ("CARPET_C", 0, lambda: carpet(3, stain=True)),
    ("FOAM_A", 1, lambda: foam(4)),
    ("FOAM_B", 1, lambda: foam(5)),
    ("WALL_CAP", 2, lambda: wall_cap(6)),
    ("WALL_FRONT", 2, lambda: wall_front(7)),
    ("WALL_VENT", 2, lambda: wall_front(8, vent=True)),
    ("BRICK", 3, lambda: drywall(9)),
    ("BRICK_CRACK", 3, lambda: drywall(9, cracked=True)),
    ("CRT_A", 4, lambda: crt(10, 0)),
    ("CRT_B", 4, lambda: crt(11, 1)),
    ("TOKEN", 5, token),
    ("KEY", 6, keycard),
    ("DOOR_SHUT", 7, lambda: door(False)),
    ("DOOR_OPEN", 7, lambda: door(True)),
]


def build_bg() -> tuple[TileSet, list[list[int]], list[int], dict[str, int]]:
    ts = TileSet()
    ts.add(np.zeros((8, 8), np.uint8))  # tile 0: blank
    metas, pals = [], []
    for _, p, fn in METAS:
        metas.append(ts.add_meta(fn()))
        pals.append(p)
    named: dict[str, int] = {}
    named["FONT"] = len(ts.tiles)
    for ch in FONT_ORDER:
        ts.tiles.append(encode(glyph(ch)))
    for i, f in enumerate((0, 1, 2)):
        named[f"HEART{f}"] = ts.add(heart(f)) if i else len(ts.tiles)
        if not i:
            ts.tiles.append(encode(heart(f)))
    named["ICON_TOKEN"] = len(ts.tiles)
    ts.tiles.append(encode(small_token()))
    named["ICON_KEY"] = len(ts.tiles)
    ts.tiles.append(encode(small_key()))
    named["BAR"] = len(ts.tiles)
    for n in range(9):
        ts.tiles.append(encode(bar(n)))
    named["FACE"] = len(ts.tiles)
    for _, fn, _, _ in FACES:
        f = fn()
        for ty in range(4):
            for tx in range(4):
                ts.tiles.append(encode(f[ty * 8 : ty * 8 + 8, tx * 8 : tx * 8 + 8]))
    return ts, metas, pals, named


# VRAM bank 1 during floors (src/look.h): 0 blank, 2.. the talkers' faces (screens only; the floor's cards from
# tools/bake_look.py load over them), BG_FIXED_BASE.. the font and the HUD, which every screen keeps.
BG_FIXED_BASE = 190


def build_vram() -> tuple[list[bytes], list[bytes], dict[str, int]]:
    low = [encode(np.zeros((8, 8), np.uint8))] * 2
    named: dict[str, int] = {"FACE": len(low)}
    for _, fn, _, _ in FACES:
        f = fn()
        for ty in range(4):
            for tx in range(4):
                low.append(encode(f[ty * 8 : ty * 8 + 8, tx * 8 : tx * 8 + 8]))
    fixed: list[bytes] = []
    named["FONT"] = BG_FIXED_BASE
    fixed += [encode(glyph(ch)) for ch in FONT_ORDER]
    for f in (0, 1, 2):
        named[f"HEART{f}"] = BG_FIXED_BASE + len(fixed)
        fixed.append(encode(heart(f)))
    named["ICON_TOKEN"] = BG_FIXED_BASE + len(fixed)
    fixed.append(encode(small_token()))
    named["ICON_KEY"] = BG_FIXED_BASE + len(fixed)
    fixed.append(encode(small_key()))
    named["BAR"] = BG_FIXED_BASE + len(fixed)
    fixed += [encode(bar(n)) for n in range(9)]
    named["RIM"] = BG_FIXED_BASE + len(fixed)
    fixed.append(encode(rim()))
    if len(low) > BG_FIXED_BASE or BG_FIXED_BASE + len(fixed) > 256:
        raise SystemExit(f"bank 1 layout: faces end {len(low)}, fixed block ends {BG_FIXED_BASE + len(fixed)}")
    return low, fixed, named


def build_sprites() -> tuple[TileSet, dict[str, int]]:
    ts = TileSet(dedupe=False)
    named: dict[str, int] = {}
    poses = {
        "DOWN": PLAYER_DOWN,
        "UP": PLAYER_UP,
        "SIDE": PLAYER_SIDE,
    }
    # per direction: idle, walk0, walk1, attack; each frame is head (4 tiles) then body (4 tiles)
    for name, rows in poses.items():
        side = name == "SIDE"
        frames = [fix(rows), walk(fix(rows), 0, side), walk(fix(rows), 1, side), attack(fix(rows), side)]
        named[f"P_{name}"] = len(ts.tiles)
        for fr in frames:
            a, b = layers(fr)
            ts.add_sprite16(a)
            ts.add_sprite16(b)
    named["P_ROLL"] = len(ts.tiles)
    a, b = layers(ROLL)
    ts.add_sprite16(a)
    ts.add_sprite16(b)
    a2, b2 = a[:, ::-1].copy(), b[::-1, :].copy()
    ts.add_sprite16(a2)
    ts.add_sprite16(b2)
    named["MITE"] = len(ts.tiles)
    for f in range(3):
        ts.add_sprite16(mite(f))
    named["EYE"] = len(ts.tiles)
    for f in range(2):
        ts.add_sprite16(eye(f))
    named["ORB"] = ts.add_sprite8(orb())
    named["ARC_DOWN"] = len(ts.tiles)
    for f in range(2):
        ts.add_sprite16(arc("down", f))
    named["ARC_UP"] = len(ts.tiles)
    for f in range(2):
        ts.add_sprite16(arc("up", f))
    named["ARC_SIDE"] = len(ts.tiles)
    for f in range(2):
        ts.add_sprite16(arc("right", f))
    named["BOSS"] = len(ts.tiles)
    for f in range(3):
        m = watchman(f)
        for r in range(2):
            for c in range(4):
                blk = m[r * 16 : r * 16 + 16, c * 8 : c * 8 + 8]
                ts.tiles.append(encode(blk[0:8]))
                ts.tiles.append(encode(blk[8:16]))
    named["SPARK"] = ts.add_sprite8(spark())
    named["PUFF"] = ts.add_sprite8(puff())
    return ts, named


# --- full-screen pictures (boot logo, title) ----------------------------------------------------------------------
class Picture:
    """a 160x144 picture as tiles + a 20x18 map + attributes (palette, VRAM bank)"""

    def __init__(self, palettes: list[list[tuple[int, int, int]]]) -> None:
        self.palettes = palettes
        self.tiles = TileSet()
        self.map: list[int] = []
        self.attr: list[int] = []

    def from_shades(self, shade: np.ndarray, region: np.ndarray) -> None:
        """shade: 144x160 continuous 0..3; region: 144x160 palette ids. A tile takes its majority palette."""
        for ty in range(18):
            for tx in range(20):
                sl = (slice(ty * 8, ty * 8 + 8), slice(tx * 8, tx * 8 + 8))
                reg = np.bincount(region[sl].ravel(), minlength=8).argmax()
                idx = dither(shade[sl])
                t = self.tiles.add(idx)
                self.map.append(t & 0xFF)
                self.attr.append(int(reg) | (8 if t >= 256 else 0))
        if len(self.tiles.tiles) > 512:
            raise SystemExit(f"picture needs {len(self.tiles.tiles)} tiles (max 512)")

    def render(self) -> Image.Image:
        img = Image.new("RGB", (160, 144))
        px = img.load()
        for i, (t, a) in enumerate(zip(self.map, self.attr, strict=True)):
            tile = self.tiles.tiles[t + (256 if a & 8 else 0)]
            p = self.palettes[a & 7]
            tx, ty = i % 20, i // 20
            for y in range(8):
                lo, hi = tile[y * 2], tile[y * 2 + 1]
                for x in range(8):
                    v = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
                    px[tx * 8 + x, ty * 8 + y] = p[v]
        return img


BIG = {  # 5x7 letters for logos, drawn as dithered blocks
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "G": ["01111", "10000", "10000", "10011", "10001", "10001", "01111"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
}


def stamp_word(mask: np.ndarray, word: str, x0: int, y0: int, cell: int, gap: int) -> None:
    x = x0
    for ch in word:
        rows = BIG[ch]
        for ry, row in enumerate(rows):
            for rx, b in enumerate(row):
                if b == "1":
                    mask[y0 + ry * cell : y0 + (ry + 1) * cell, x + rx * cell : x + (rx + 1) * cell] = True
        x += 5 * cell + gap


def boot_picture() -> Picture:
    pals = [
        pal("#06080c", "#0c2a30", "#1c6a68", "#30b0a0"),  # 0 ground: deep teal glow
        pal("#06080c", "#2a8c80", "#7ce0c8", "#f0fff4"),  # 1 letters
        pal("#06080c", "#202830", "#586470", "#a8b4bc"),  # 2 small text
    ] + [pal("#000000", "#000000", "#000000", "#000000")] * 5
    ys, xs = np.mgrid[0:144, 0:160].astype(np.float32)
    shade = np.clip(1.6 - np.hypot((xs - 80) / 70, (ys - 64) / 40) * 1.5, 0, 2.2)
    shade += (hash2(xs.astype(int), ys.astype(int), 5) - 0.5) * 0.4
    region = np.zeros((144, 160), np.uint8)
    mask = np.zeros((144, 160), bool)
    stamp_word(mask, "KEEL", 11, 40, 6, 6)  # 4 letters: 4*30 + 3*6 = 138 wide
    yfrac = (ys - 40) / 42
    letter = 3.0 - np.clip(yfrac, 0, 1) * 1.6 + (xs % 2 == ys % 2) * 0.2
    shadow = np.zeros_like(mask)
    shadow[2:, 2:] = mask[:-2, :-2]
    shadow &= ~mask
    shade = np.where(mask, letter, np.where(shadow, 0.0, shade))
    ty = (ys // 8).astype(int)
    tx = (xs // 8).astype(int)
    region = np.where((ty >= 5) & (ty <= 10) & (tx >= 2) & (tx <= 17), 1, 0).astype(np.uint8)
    # within the letter band, keep the glow behind the letters in ramp 1 too (index 0..1)
    shade = np.where((region == 1) & ~mask & ~shadow, np.clip(shade * 0.5, 0, 1.0), shade)
    p = Picture(pals)
    p.from_shades(shade, region)
    return p


def title_picture() -> Picture:
    """a backrooms corridor in one-point perspective: fluorescent panels, yellow wallpaper, wet carpet, and the EXIT"""
    pals = [
        pal("#1c1a10", "#5a5434", "#a8a070", "#fff8d0"),  # 0 ceiling and lights
        pal("#20180a", "#70602c", "#c0aa58", "#ecdc98"),  # 1 walls
        pal("#140e06", "#4c3818", "#8c6c34", "#b89858"),  # 2 carpet
        pal("#040404", "#0c3010", "#28c050", "#c0ffb0"),  # 3 the far door and its sign
        pal("#1c1a10", "#5a5434", "#ff5aa0", "#fff4f8"),  # 4 the logo over the ceiling
        pal("#06040c", "#303044", "#9090a8", "#f0f0f8"),  # 5 small text
    ] + [pal("#000000", "#000000", "#000000", "#000000")] * 2
    H, W = 144, 160
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    cx, cy = 80.0, 70.0
    # far rectangle of the corridor
    fx0, fx1, fy0, fy1 = 66.0, 94.0, 58.0, 84.0
    dx, dy = xs - cx, ys - cy
    region = np.full((H, W), 1, np.uint8)
    shade = np.zeros((H, W), np.float32)
    # depth (0 near, 1 far) by how close a pixel sits to the vanishing rectangle
    sx = np.where(dx < 0, (xs - 0) / (fx0 - 0), (W - xs) / (W - fx1))
    sy = np.where(dy < 0, ys / fy0, (H - ys) / (H - fy1))
    ceil = (dy < 0) & (np.abs(dy) * (cx - 0) >= np.abs(dx) * (cy - 0) * 0.98) & (ys < fy0)
    floor = (dy > 0) & (np.abs(dy) * (cx) >= np.abs(dx) * (H - cy) * 0.98) & (ys > fy1)
    far = (xs >= fx0) & (xs < fx1) & (ys >= fy0) & (ys < fy1)
    walls = ~ceil & ~floor & ~far
    # ceiling: tiles with fluorescent panels receding
    depth_c = np.clip(ys / fy0, 0, 1)
    z = 1 / np.maximum(1 - depth_c * 0.92, 0.05)
    shade = np.where(ceil, 1.1 + 0.3 * np.sin(z * 2.2) , shade)
    panel = ceil & (np.sin(z * 1.4) > 0.55) & (np.abs(dx) < (1 - depth_c) * 22 + 4)
    shade = np.where(panel, 3.0, shade)
    region = np.where(ceil, 0, region)
    # walls: vertical stripes in perspective, darker far away, a baseboard line
    depth_w = np.clip(sx, 0, 1)
    zw = 1 / np.maximum(1 - depth_w * 0.92, 0.05)
    stripe = (np.floor(zw * 3.0) % 2 == 0)
    shade = np.where(walls, 2.3 - depth_w * 1.2 - stripe * 0.35, shade)
    # wall light pools under the panels
    shade = np.where(walls, shade + 0.4 * (np.sin(zw * 1.4) > 0.55), shade)
    base = walls & (dy > 0) & (np.abs(dy) * cx >= np.abs(dx) * (H - cy) * 0.86)
    shade = np.where(base, 0.6, shade)
    # floor: carpet, wet patch catching the light
    depth_f = np.clip((H - ys) / (H - fy1), 0, 1)
    zf = 1 / np.maximum(1 - depth_f * 0.92, 0.05)
    shade = np.where(floor, 1.0 + (hash2(xs.astype(int), ys.astype(int), 8) - 0.5) * 0.9 + 0.5 * depth_f, shade)
    wet = floor & (np.hypot((xs - 92) / 30, (ys - 122) / 9) < 1)
    shade = np.where(wet, 2.2 + 0.6 * (np.sin(zf * 1.4) > 0.55), shade)
    region = np.where(floor, 2, region)
    # the far wall and the door with its EXIT sign
    shade = np.where(far, 1.0, shade)
    door_m = far & (xs >= 74) & (xs < 86) & (ys >= 66)
    shade = np.where(door_m, 2.2 - (xs == 80) * 2.0, shade)
    sign = (xs >= 72) & (xs < 88) & (ys >= 59) & (ys < 64)
    shade = np.where(sign, 3.0, shade)
    region = np.where(far, 3, region)
    region = np.where(
        (xs >= 64) & (xs < 96) & (ys >= 56) & (ys < 88), np.where(far | sign, 3, region), region
    ).astype(np.uint8)
    # the logo
    mask = np.zeros((H, W), bool)
    stamp_word(mask, "EGRESS", 8, 10, 4, 3)  # 6*20 + 5*3 = 135
    shadow = np.zeros_like(mask)
    shadow[2:, 1:] = mask[:-2, :-1]
    shadow &= ~mask
    lg = 3.0 - np.clip((ys - 10) / 28, 0, 1) * 0.95
    shade = np.where(mask, lg, np.where(shadow, 0.0, shade))
    tyi, txi = (ys // 8).astype(int), (xs // 8).astype(int)
    band = (tyi >= 1) & (tyi <= 4) & (txi >= 1) & (txi <= 18)
    region = np.where(band, 4, region).astype(np.uint8)
    shade = np.where(band & ~mask & ~shadow, np.clip(shade, 0, 1.0), shade)
    p = Picture(pals)
    p.from_shades(shade, region)
    return p


def write(out: Path, sheet: Path | None) -> None:
    bg, metas, mpals, _ = build_bg()  # the old cards: the contact sheet only
    bg_low, bg_fixed, bg_named = build_vram()
    sp, sp_named = build_sprites()
    boot = boot_picture()
    title = title_picture()
    if len(title.tiles.tiles) > 256:
        raise SystemExit("the title must fit VRAM bank 0 (the font stays in bank 1)")
    if len(sp.tiles) > 256:
        raise SystemExit(f"sprite tiles: {len(sp.tiles)} > 256")
    h = [
        "/* Generated by tools/bake_art.py. Do not edit. */",
        "#ifndef EG_ART_H",
        "#define EG_ART_H",
        "#include <gb/gb.h>",
        "#include <stdint.h>",
    ]
    h.append('#include "look_art.h" /* the floor\'s cards (M_*, M_COUNT): tools/bake_look.py */')
    for k, v in bg_named.items():
        h.append(f"#define BT_{k} {v}u")
    h.append(f"#define BT_FONT_ORDER \"{FONT_ORDER}\"")
    for k, v in sp_named.items():
        h.append(f"#define ST_{k} {v}u")
    h.append(f"#define ART_SKINS {len(SKINS)}u")
    h.append(f"#define ART_HAIRS {len(HAIRS)}u")
    h.append(f"#define ART_HOODIES {len(HOODIES)}u")
    h.append(f"#define ART_TROUSERS {len(TROUSERS)}u")
    h += [
        f"#define ART_FACES {len(FACES)}u",
        "extern const uint16_t art_face_pal[][8]; /* palette A then B per talker */",
        "extern const uint8_t art_face_human[];",
        "extern const uint16_t art_bg_pal[32];",
        "extern const uint16_t art_spr_pal[32];",
        "extern const uint16_t art_skins[], art_hairs[], art_hoodies[], art_trousers[];",
        "/* loaders: they live in the art bank */",
        "void art_load_game(void) BANKED;   /* background tiles to VRAM bank 1, sprites to VRAM bank 0 */",
        "void art_show_picture(uint8_t which, uint16_t *pal_out) BANKED; /* 0 boot logo, 1 title: map, attributes; palettes to pal_out */",
        "uint8_t art_face_palettes(uint8_t face, uint16_t *out16) BANKED; /* 8 colours: palette A, B; 1: a human (skin, hair seeded) */",
        "void art_copy_tables(uint8_t *meta_tile, uint8_t *meta_pal, uint16_t *bg_pal, uint16_t *spr_pal, "
        "uint16_t *people) BANKED;",
        "#endif",
    ]
    (out / "art.h").write_text("\n".join(h) + "\n")

    def pic_c(name: str, p: Picture) -> str:
        tiles = b"".join(p.tiles.tiles)
        s = c_bytes(f"{name}_tiles", tiles)
        s += c_bytes(f"{name}_map", bytes(p.map))
        s += c_bytes(f"{name}_attr", bytes(p.attr))
        s += c_words(f"{name}_pal", [rgb555(c) for pp in p.palettes for c in pp])
        s += f"#define {name.upper()}_NTILES {len(p.tiles.tiles)}u\n"
        return s

    c = [
        "/* Generated by tools/bake_art.py. Do not edit. */",
        "#pragma bank 255",
        "#include <gb/gb.h>",
        "#include <gb/cgb.h>",
        "#include <string.h>",
        '#include "art.h"',
        "BANKREF(art_bank)",
        c_bytes("bg_tiles", b"".join(bg_low)),
        c_bytes("bg_fixed", b"".join(bg_fixed)),
        c_bytes("spr_tiles", b"".join(sp.tiles)),
        c_words("art_bg_pal", [rgb555(col) for p in BG_PAL for col in p]),
        c_words("art_spr_pal", [rgb555(col) for p in SPR_PAL for col in p]),
        "const uint16_t art_face_pal[][8] = {" + ", ".join(
            "{" + ", ".join(f"0x{rgb555(c):04x}" for c in a + b) + "}" for _, _, a, b in FACES) + "};",
        "const uint8_t art_face_human[] = {" + ", ".join("1" if n in HUMAN_FACES else "0" for n, *_ in FACES) + "};",
        c_words("art_skins", [rgb555(hx(x)) for x in SKINS]),
        c_words("art_hairs", [rgb555(hx(x)) for x in HAIRS]),
        c_words("art_hoodies", [rgb555(hx(x)) for x in HOODIES]),
        c_words("art_trousers", [rgb555(hx(x)) for x in TROUSERS]),
        f"#define BG_NTILES {len(bg_low)}u",
        f"#define BG_FIXED_BASE {BG_FIXED_BASE}u",
        f"#define BG_NFIXED {len(bg_fixed)}u",
        f"#define SPR_NTILES {len(sp.tiles)}u",
        """
void art_load_game(void) BANKED {
  VBK_REG = 1;
  set_bkg_data(0, BG_NTILES, bg_tiles);
  set_bkg_data(BG_FIXED_BASE, BG_NFIXED, bg_fixed);
  VBK_REG = 0;
  set_sprite_data(0, SPR_NTILES, spr_tiles);
}

uint8_t art_face_palettes(uint8_t face, uint16_t *out16) BANKED {
  memcpy(out16, art_face_pal[face], 16);
  return art_face_human[face];
}

void art_copy_tables(uint8_t *meta_tile, uint8_t *meta_pal, uint16_t *bg_pal, uint16_t *spr_pal,
                     uint16_t *people) BANKED {
  (void)meta_tile; /* the floor's cards come from src/look.c now (look_load) */
  (void)meta_pal;
  memcpy(bg_pal, art_bg_pal, sizeof art_bg_pal);
  memcpy(spr_pal, art_spr_pal, sizeof art_spr_pal);
  memcpy(people, art_skins, sizeof art_skins);
  memcpy(people + ART_SKINS, art_hairs, sizeof art_hairs);
  memcpy(people + ART_SKINS + ART_HAIRS, art_hoodies, sizeof art_hoodies);
  memcpy(people + ART_SKINS + ART_HAIRS + ART_HOODIES, art_trousers, sizeof art_trousers);
}
""",
    ]
    (out / "art.c").write_text("\n".join(c))
    # the full-screen pictures in a bank of their own (art.c alone is near a bank's 16 KiB)
    pics = [
        "/* Generated by tools/bake_art.py. Do not edit. */",
        "#pragma bank 255",
        "#include <gb/gb.h>",
        "#include <gb/cgb.h>",
        "#include <string.h>",
        '#include "art.h"',
        pic_c("boot", boot),
        pic_c("title", title),
        "\n" + """static void load_picture(const uint8_t *tiles, uint16_t n, const uint8_t *map, const uint8_t *attr,
                         const uint16_t *pals, uint16_t *pal_out) {
  uint16_t first = n > 256u ? 256u : n;
  VBK_REG = 0;
  set_bkg_data(0, (uint8_t)first, tiles);
  if (n > 256u) {
    VBK_REG = 1;
    set_bkg_data(0, (uint8_t)(n - 256u), tiles + 4096u);
  }
  VBK_REG = 1;
  set_bkg_tiles(0, 0, 20, 18, attr);
  VBK_REG = 0;
  set_bkg_tiles(0, 0, 20, 18, map);
  memcpy(pal_out, pals, 64);
}

void art_show_picture(uint8_t which, uint16_t *pal_out) BANKED {
  if (which == 0u)
    load_picture(boot_tiles, BOOT_NTILES, boot_map, boot_attr, boot_pal, pal_out);
  else
    load_picture(title_tiles, TITLE_NTILES, title_map, title_attr, title_pal, pal_out);
}

""",
    ]
    (out / "art_pics.c").write_text("\n".join(pics))
    print(f"bg tiles {len(bg.tiles)}, sprite tiles {len(sp.tiles)}, boot {len(boot.tiles.tiles)}, "
          f"title {len(title.tiles.tiles)}")
    if sheet:
        contact_sheet(sheet, metas, mpals, bg, sp, sp_named, boot, title)
        faces_sheet(sheet.with_name("art_faces.png"))


def tile_img(tile: bytes, p: list[tuple[int, int, int]], transparent: bool = False) -> Image.Image:
    img = Image.new("RGBA", (8, 8))
    px = img.load()
    for y in range(8):
        lo, hi = tile[y * 2], tile[y * 2 + 1]
        for x in range(8):
            v = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
            px[x, y] = (0, 0, 0, 0) if transparent and v == 0 else (*p[v], 255)
    return img


def sprite16(sp: TileSet, first: int, p: list[tuple[int, int, int]]) -> Image.Image:
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for k, (ox, oy) in enumerate(((0, 0), (0, 8), (8, 0), (8, 8))):
        t = tile_img(sp.tiles[first + k], p, True)
        img.alpha_composite(t, (ox, oy))
    return img


def contact_sheet(path: Path, metas, mpals, bg, sp, sp_named, boot, title) -> None:  # noqa: ANN001
    scale = 4
    sheet = Image.new("RGBA", (16 * 18 + 160 * 2 + 24, 300), (40, 40, 48, 255))
    for i, m in enumerate(metas):
        img = Image.new("RGBA", (16, 16))
        for k, (ox, oy) in enumerate(((0, 0), (8, 0), (0, 8), (8, 8))):
            img.paste(tile_img(bg.tiles[m[k]], BG_PAL[mpals[i]]), (ox, oy))
        sheet.paste(img, ((i % 8) * 18, (i // 8) * 18))
    # a little room: carpet with objects on it
    people = [
        (SKINS[s], HAIRS[h], HOODIES[c], TROUSERS[t])
        for s, h, c, t in [(0, 0, 0, 0), (3, 4, 1, 1), (5, 5, 2, 2), (7, 2, 3, 3), (1, 6, 4, 4), (6, 7, 5, 5)]
    ]
    y0 = 40
    for j, (sk, ha, ho, tr) in enumerate(people):
        hp = [(0, 0, 0), hx("#1a1018"), hx(sk), hx(ha)]
        bp = [(0, 0, 0), hx("#14101c"), hx(ho), hx(tr)]
        for f in range(14):
            base = sp_named["P_DOWN"] + f * 8 if f < 12 else sp_named["P_ROLL"] + (f - 12) * 8
            img = Image.new("RGBA", (16, 16), (*BG_PAL[0][2], 255))
            img.alpha_composite(sprite16(sp, base + 4, bp))
            img.alpha_composite(sprite16(sp, base, hp))
            sheet.paste(img, (f * 18, y0 + j * 18))
    y1 = y0 + 6 * 18 + 4
    row = []
    for f in range(3):
        row.append(sprite16(sp, sp_named["MITE"] + f * 4, SPR_PAL[2]))
    for f in range(2):
        row.append(sprite16(sp, sp_named["EYE"] + f * 4, SPR_PAL[3]))
    for nm in ("ARC_DOWN", "ARC_UP", "ARC_SIDE"):
        for f in range(2):
            row.append(sprite16(sp, sp_named[nm] + f * 4, SPR_PAL[4]))
    for i, img in enumerate(row):
        bgc = Image.new("RGBA", (16, 16), (*BG_PAL[0][2], 255))
        bgc.alpha_composite(img)
        sheet.paste(bgc, (i * 18, y1))
    font = Image.new("RGBA", (8 * 25, 16))
    for i, _ch in enumerate(FONT_ORDER):
        t = tile_img(bg.tiles[sp_named.get("x", 0) * 0 + i + 0 + _font_base(bg)], BG_PAL[4])
        font.paste(t, ((i % 25) * 8, (i // 25) * 8))
    sheet.paste(font, (0, y1 + 20))
    sheet.paste(boot.render(), (16 * 18 + 8, 0))
    sheet.paste(title.render(), (16 * 18 + 8 + 168, 0))
    sheet = sheet.resize((sheet.width * scale // 2, sheet.height * scale // 2), Image.NEAREST)
    sheet.save(path)


def paint(arr: np.ndarray, p: list[tuple[int, int, int]], transparent: bool = False) -> Image.Image:
    img = Image.new("RGBA", (arr.shape[1], arr.shape[0]))
    px = img.load()
    for yy in range(arr.shape[0]):
        for xx in range(arr.shape[1]):
            v = int(arr[yy, xx])
            px[xx, yy] = (0, 0, 0, 0) if transparent and v == 0 else (*p[v], 255)
    return img


def faces_sheet(path: Path) -> None:
    """every talker (humans in three seeded skin/hair pairs) and the Watchman's three frames"""
    tiles = []
    for name, fn, pa, pb in FACES:
        f = fn()
        variants = [(SKINS[1], HAIRS[0]), (SKINS[4], HAIRS[4]), (SKINS[7], HAIRS[5])] if name in HUMAN_FACES else [None]
        for var in variants:
            a = list(pa)
            if var:
                a[1], a[2] = hx(var[0]), hx(var[1])
                if name == "CHARGE":
                    a[2] = pa[2]
            img = Image.new("RGBA", (32, 32))
            img.paste(paint(f[:24], a), (0, 0))
            img.paste(paint(f[24:], pb), (0, 24))
            tiles.append(img)
    for fr in range(3):
        bgc = Image.new("RGBA", (32, 32), (*BG_PAL[0][2], 255))
        bgc.alpha_composite(paint(watchman(fr), SPR_PAL[3], True))
        tiles.append(bgc)
    sheet = Image.new("RGBA", (len(tiles) * 34, 32), (40, 40, 48, 255))
    for i, t in enumerate(tiles):
        sheet.paste(t, (i * 34, 0))
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(path)


_FONT_BASE = [0]


def _font_base(bg: TileSet) -> int:
    return _FONT_BASE[0]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", type=Path)
    ap.add_argument("--sheet", type=Path)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    bg, _, _, named = build_bg()
    _FONT_BASE[0] = named["FONT"]
    write(a.out, a.sheet)


if __name__ == "__main__":
    main()
