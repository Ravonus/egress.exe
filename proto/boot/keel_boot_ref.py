"""CRUCIBLE's KEEL start-up, as a frame-exact reference.

Ported from the crucible.onkeel.io handheld (rictus ops/crucible-live-service-20261006/site/src/lib/boot.ts, the
boot chime in src/lib/sound.ts and the power light in src/lib/package-scene.ts; the live site's bundle was checked
to carry the same constants). CRUCIBLE's start-up is not inside crucible.gbc: the site plays it on the handheld's
screen when it powers on, over the emulator while the cartridge boots, and dithers it away into the game. This module
evaluates that code at Game Boy frame times (t = k * 70224 / 4194304 s) so it can be compared frame by frame, and
bakes the tables src/boot/keel_boot_data.h is made of.

    python proto/boot/keel_boot_ref.py bake    # write src/boot/keel_boot_data.h
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

# ---- boot.ts constants (verbatim) ----
W, H = 160, 144
BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
BG, INK, GLINT = (244, 240, 255), (69, 83, 230), (150, 160, 255)
SCROLL, HOLD, FADE = 1.5, 0.7, 0.3
LOGO_W, LOGO_H = 46, 36 + 6 + 14


def js_round(x: float) -> int:
    return math.floor(x + 0.5)


LAND_Y, LOGO_X = js_round((H - LOGO_H) / 2), js_round((W - LOGO_W) / 2)

# The cartridge's 5x7 letters (site src/lib/pixel-font/glyphs.ts), only the four the mark uses.
GLYPHS = {
    "K": "#...#/#..#./#.#../##.../#.#../#..#./#...#",
    "E": "#####/#..../#..../####./#..../#..../#####",
    "L": "#..../#..../#..../#..../#..../#..../#####",
}

# ---- the power light (package-scene.ts): on at power-on, flickers [1,0,1,1,0,1] in 25 ms steps, steady after 150 ms
LED_FLICKER, LED_STEP_MS, LED_STEADY_MS = [1, 0, 1, 1, 0, 1], 25, 150

# ---- the boot chime (sound.ts SFX.boot): [frames, NRx2 envelope, NOTES index, duty]
NOTES = [1046, 1102, 1155, 1205, 1253, 1297, 1339, 1379, 1417, 1452, 1486, 1517, 1547, 1575, 1602, 1627, 1650, 1673,
         1694, 1714, 1732, 1750, 1767, 1783, 1798, 1812, 1825, 1837, 1849, 1860, 1871, 1881, 1890, 1899, 1907, 1915,
         1923, 1930, 1936, 1943, 1949, 1954, 1959, 1964, 1969, 1974, 1978, 1982]
CHIME = [(5, 0x81, 36, 2), (44, 0xA3, 47, 2)]
SITE_FRAME = 1 / 59.73  # sound.ts FRAME

GB_FRAME = 70224 / 4194304  # seconds per Game Boy frame


def logo_rgba() -> np.ndarray:
    """buildLogo(): the KEEL glyph (24x36, drawn at x 11) over "KEEL" in INK at 2x from y 42."""
    out = np.zeros((LOGO_H, LOGO_W, 4), np.uint8)
    mark = np.array(Image.open(HERE / "keel-mark.png").convert("RGBA"))
    out[0:36, (LOGO_W - 24) // 2:(LOGO_W - 24) // 2 + 24] = mark
    x = 0
    for ch in "KEEL":
        rows = GLYPHS[ch].split("/")
        for j, row in enumerate(rows):
            for i, b in enumerate(row):
                if b == "#":
                    out[42 + j * 2:44 + j * 2, x + i * 2:x + i * 2 + 2] = (*INK, 255)
        x += (len(rows[0]) + 1) * 2
    return out


def timeline(k: int) -> dict:
    """boot.tick at frame k after begin(), with the cartridge ready (EGRESS's title is loaded with the boot)."""
    t = k * GB_FRAME
    hold = SCROLL + HOLD
    k_fade = math.ceil(hold / GB_FRAME)  # the first frame with t >= hold sets fadeAt (ready is already true)
    fade_at = k_fade * GB_FRAME
    if k >= k_fade and t - fade_at >= FADE:
        return {"phase": "done", "t": t}
    phase = "scroll" if t < SCROLL else "fade" if k >= k_fade else "hold" if t < hold else "wait"
    y = js_round(-LOGO_H + (LAND_Y + LOGO_H) * (t / SCROLL)) if t < SCROLL else LAND_Y
    fade = min(1.0, (t - fade_at) / FADE) if k >= k_fade else 0.0
    return {"phase": phase, "t": t, "y": y, "cut": math.floor(fade * 16), "glint": -1}


def chime_frame() -> int:
    """The first frame with t >= SCROLL (the chime fires in that tick)."""
    return math.ceil(SCROLL / GB_FRAME - 1e-12)


def render(k: int, under: np.ndarray | None = None, quantize: bool = True) -> np.ndarray | None:
    """paint(): one 160x144 RGB frame, or None once the boot is done. `under` is what shows through cut cells (the
    game's frame); quantize maps every colour to the CGB's 15-bit colour as PyBoy shows it (c >> 3 << 3)."""
    s = timeline(k)
    if s["phase"] == "done":
        return None
    art = logo_rgba()
    img = np.empty((H, W, 3), np.uint8)
    img[:] = BG
    y = s["y"]
    top, bot = max(0, y), min(H, y + LOGO_H)
    if bot > top:
        a = art[top - y:bot - y, :, :]
        m = a[:, :, 3] > 127
        region = img[top:bot, LOGO_X:LOGO_X + LOGO_W]
        region[m] = a[:, :, :3][m]
    if s["cut"] > 0:
        under = np.zeros((H, W, 3), np.uint8) if under is None else under
        bay = np.array(BAYER).reshape(4, 4)
        mask = np.tile(bay, (H // 4, W // 4)) < s["cut"]
        img[mask] = under[mask]
    if quantize:
        img = (img >> 3) << 3
    return img


def frames_total() -> int:
    k = 0
    while timeline(k)["phase"] != "done":
        k += 1
    return k


def chime_wave(rate: int = 48000) -> np.ndarray:
    """sound.ts note() for SFX.boot rendered offline: a WebAudio PeriodicWave pulse (32 harmonics, normalised to peak
    1) at 131072 / (2048 - register) Hz, gain (env >> 4) / 15 * 0.07 with the linear ramp sound.ts uses for NRx2."""
    out = []
    for frames, env, n, duty in CHIME:
        hz = 131072 / (2048 - NOTES[n])
        d = [0.125, 0.25, 0.5, 0.75][duty]
        length = frames * SITE_FRAME
        ts = np.arange(int(round(length * rate))) / rate
        wave = np.zeros_like(ts)
        for kk in range(1, 32):
            re = (2 / (kk * math.pi)) * math.sin(2 * math.pi * kk * d)
            im = (2 / (kk * math.pi)) * (1 - math.cos(2 * math.pi * kk * d))
            wave += re * np.cos(2 * math.pi * kk * hz * ts) + im * np.sin(2 * math.pi * kk * hz * ts)
        wave /= np.abs(wave).max()
        start, period = (env >> 4) / 15, (env & 7) / 64
        if period > 0 and not env & 8:
            end = max(0.0, start - length / period / 15)
            gain = start + (end - start) * ts / length
        else:
            gain = np.full_like(ts, start)
        out.append(wave * gain * 0.07)
    return np.concatenate(out)


def bake(path: Path) -> None:
    """Tiles, scroll table and fade-cut table for src/boot/keel_boot.c."""
    art = logo_rgba()
    grey = INK
    colours = {}
    for yy in range(LOGO_H):
        for xx in range(LOGO_W):
            p = art[yy, xx]
            if p[3] > 127:
                colours.setdefault(tuple(int(v) for v in p[:3]), 0)
    assert len(colours) <= 2, colours
    index = {INK: 2}
    for c in colours:
        if c != INK:
            index[c] = 1
            grey = c
    # Logo cells: screen x 57..102 sit in tile columns 7..12 (56..103), so the art is drawn 1 px into the strip.
    x0 = (LOGO_X // 8) * 8
    cols = (LOGO_X + LOGO_W - x0 + 7) // 8
    rows = LOGO_H // 8
    assert LOGO_H % 8 == 0
    strip = np.zeros((rows * 8, cols * 8), np.uint8)
    for yy in range(LOGO_H):
        for xx in range(LOGO_W):
            p = art[yy, xx]
            if p[3] > 127:
                strip[yy, LOGO_X - x0 + xx] = index[tuple(int(v) for v in p[:3])]
    tiles = []
    for r in range(rows):
        for c in range(cols):
            t = strip[r * 8:r * 8 + 8, c * 8:c * 8 + 8]
            for yy in range(8):
                lo = hi = 0
                for xx in range(8):
                    v = int(t[yy, xx])
                    lo |= (v & 1) << (7 - xx)
                    hi |= (v >> 1) << (7 - xx)
                tiles += [lo, hi]
    n = 0
    ys, cuts = [], []
    while True:
        s = timeline(n)
        if s["phase"] == "done":
            break
        ys.append(s["y"])
        cuts.append(s["cut"])
        n += 1
    k_fade = next(i for i in range(n) if timeline(i)["phase"] == "fade")
    k_land = next(i for i in range(n) if timeline(i)["phase"] != "scroll")

    def rgb555(c: tuple[int, int, int]) -> str:
        return f"0x{(c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10):04x}u"

    def arr(v: list[int]) -> str:
        return ",".join(str(x) for x in v)

    chime = []
    for frames, env, nn, duty in CHIME:
        reg = NOTES[nn]
        chime.append(f"{{{frames}u, 0x{env:02x}u, 0x{duty << 6:02x}u, 0x{reg & 255:02x}u, 0x{reg >> 8:02x}u}}")
    text = f"""/* Generated by proto/boot/keel_boot_ref.py from CRUCIBLE's start-up (crucible.onkeel.io src/lib/boot.ts,
 * src/lib/sound.ts, public/ui/keel-mark.png). Do not edit. */
#define KB_FRAMES {n}u   /* frames from power-on until the game shows */
#define KB_LAND {k_land}u     /* first frame on the landing row (scroll ends) */
#define KB_LAND_Y {LAND_Y}   /* the landing row */
#define KB_CHIME {chime_frame()}u    /* the frame the chime starts */
#define KB_FADE {k_fade}u    /* first frame of the dither-away */
#define KB_COLS {cols}u
#define KB_ROWS {rows}u
#define KB_COL0 {x0 // 8}u
#define KB_TILES {rows * cols}u
static const uint16_t kb_palette[4] = {{{rgb555(BG)}, {rgb555(grey)}, {rgb555(INK)}, 0x0000u}};
/* logo top row per frame while it scrolls in (whole pixels, -{LOGO_H}..{LAND_Y}) */
static const int8_t kb_y[KB_LAND] = {{{arr(ys[:k_land])}}};
/* Bayer cut (0..16) per frame of the dither-away */
static const uint8_t kb_cut[KB_FRAMES - KB_FADE] = {{{arr(cuts[k_fade:])}}};
/* chime notes: frames, NRx2, NRx1 duty, NRx3, NRx4 high bits */
static const uint8_t kb_chime[{len(CHIME)}][5] = {{{", ".join(chime)}}};
static const uint8_t kb_tiles[KB_TILES * 16u] = {{{arr(tiles)}}};
"""
    path.write_text(text)
    print(f"wrote {path}: {n} frames, land {k_land}, chime {chime_frame()}, fade {k_fade}, {rows * cols} tiles")


if __name__ == "__main__":
    if sys.argv[1:] == ["bake"]:
        bake(ROOT / "src/boot/keel_boot_data.h")
    else:
        print(__doc__)
