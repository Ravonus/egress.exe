"""Faces from the ported CRUCIBLE generator (build/host/avatar_dump) as PIL images, for sheets and the art bake."""

from __future__ import annotations

import subprocess
from pathlib import Path

from PIL import Image

DUMP = Path("build/host/avatar_dump")


def rgb(c: int) -> tuple[int, int, int]:
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31)


def face_image(hexbytes: str, pal: list[int], bg: tuple[int, int, int] | None = None) -> Image.Image:
    data = bytes.fromhex(hexbytes)
    img = Image.new("RGBA", (48, 48), (0, 0, 0, 0) if bg is None else (*bg, 255))
    px = img.load()
    for t in range(36):
        tx, ty = (t % 6) * 8, (t // 6) * 8
        for y in range(8):
            lo, hi = data[t * 16 + y * 2], data[t * 16 + y * 2 + 1]
            for x in range(8):
                c = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
                if c:
                    px[tx + x, ty + y] = (*rgb(pal[c]), 255)
    return img


def faces(args: list[str]) -> list[tuple[str, list[int]]]:
    out = subprocess.run([str(DUMP), *args], capture_output=True, text=True, check=True).stdout.split("\n")
    res = []
    for line in out:
        if not line.strip():
            continue
        parts = line.split()
        res.append((parts[0], [int(x, 16) for x in parts[1:5]]))
    return res
