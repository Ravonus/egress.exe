"""The whole 256x224 room as the cartridge drew it: the BG map, its CGB attributes and tiles from both VRAM banks,
coloured with the game's BG palettes (pal_bg, the colours the fade ends on). The screen shows a 160x144 window of
this; the room picture shows every cell at once (B2 shots: obstacles, wall structures)."""
from __future__ import annotations

from PIL import Image


def rgb555(c: int) -> tuple[int, int, int]:
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31)


def room_image(g, w: int = 32, h: int = 28, sprites: bool = True) -> Image.Image:
    """g: a test/harness/play.py Game. w x h tiles of the BG map from its top left (the room is 32x28 tiles)."""
    mem = g.pb.memory
    lcdc = mem[0xFF40]
    base = 0x9C00 if lcdc & 0x08 else 0x9800
    signed = not (lcdc & 0x10)
    pa = g.addr("pal_bg")
    pal = [[rgb555(mem[pa + (p * 4 + k) * 2] | (mem[pa + (p * 4 + k) * 2 + 1] << 8)) for k in range(4)]
           for p in range(8)]
    img = Image.new("RGB", (w * 8, h * 8))
    px = img.load()
    for ty in range(h):
        for tx in range(w):
            a = base + ty * 32 + tx
            t, at = mem[0, a], mem[1, a]
            bank = 1 if at & 0x08 else 0
            if signed:
                ta = 0x9000 + (t - 256 if t > 127 else t) * 16
            else:
                ta = 0x8000 + t * 16
            p = pal[at & 7]
            for y in range(8):
                yy = 7 - y if at & 0x40 else y
                lo, hi = mem[bank, ta + yy * 2], mem[bank, ta + yy * 2 + 1]
                for x in range(8):
                    bit = x if at & 0x20 else 7 - x
                    px[tx * 8 + x, ty * 8 + y] = p[((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)]
    if sprites:
        draw_sprites(g, img)
    return img


def draw_sprites(g, img: Image.Image) -> None:
    """the OAM sprites over the room picture, at their room positions (screen + scroll)"""
    mem = g.pb.memory
    scx, scy = mem[0xFF43], mem[0xFF42]
    tall = bool(mem[0xFF40] & 0x04)
    pa = g.addr("pal_sp")
    pal = [[rgb555(mem[pa + (p * 4 + k) * 2] | (mem[pa + (p * 4 + k) * 2 + 1] << 8)) for k in range(4)]
           for p in range(8)]
    px = img.load()
    W, H = img.size
    for s in reversed(range(40)):
        y, x, t, at = (mem[0xFE00 + s * 4 + k] for k in range(4))
        if y == 0 or y >= 160 or x == 0 or x >= 168:
            continue
        hgt = 16 if tall else 8
        if tall:
            t &= 0xFE
        bank = 1 if at & 0x08 else 0
        p = pal[at & 7]
        for yy in range(hgt):
            row = hgt - 1 - yy if at & 0x40 else yy
            ta = 0x8000 + t * 16 + row * 2
            lo, hi = mem[bank, ta], mem[bank, ta + 1]
            for xx in range(8):
                bit = xx if at & 0x20 else 7 - xx
                c = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)
                if not c:
                    continue
                X, Y = (x - 8 + xx + scx) % 256, (y - 16 + yy + scy) % 256
                if X < W and Y < H:
                    px[X, Y] = p[c]
