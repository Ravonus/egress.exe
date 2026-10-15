"""The title bake's worlds: a seeded backrooms maze (the same rules as the old live title's generator in
src/title3d/title3d.c: an odd-cell maze, extra loops, doors, flat panels with spaced features) and a closed walk
through it (a corridor cycle, walked at a constant speed with a look-ahead heading, so the last frame leads straight
back into the first).
"""

from __future__ import annotations

import math
import random
from collections import deque

MAPW = 32
# texture / set ids (tools/title3d/gen_t3d.py WALLS / SETS order, 1-based walls)
T_WALL, T_RAIL, T_DOOR, T_EXITDOOR, T_POSTER, T_NOTICE, T_EXIT, T_VENT, T_CLOCK, T_BOARD = range(1, 11)
S_OFFICE, S_OFFDARK = 0, 1
FEATURES = [T_POSTER, T_NOTICE, T_EXIT, T_BOARD, T_POSTER, T_VENT, T_CLOCK, T_NOTICE]
DX = [1, 0, -1, 0]
DY = [0, 1, 0, -1]


def gen_maze(rng: random.Random) -> list[list[int]]:
    """1 wall, 0 open; rooms on odd cells 1..29, a carved spanning tree plus ~1 in 6 extra openings (loops)"""
    m = [[1] * MAPW for _ in range(MAPW)]
    sx, sy = 1 + 2 * rng.randrange(15), 1 + 2 * rng.randrange(15)
    m[sy][sx] = 0
    st = [(sx, sy)]
    while st:
        x, y = st[-1]
        opts = [d for d in range(4) if 1 <= x + 2 * DX[d] <= 29 and 1 <= y + 2 * DY[d] <= 29
                and m[y + 2 * DY[d]][x + 2 * DX[d]] == 1]
        if not opts:
            st.pop()
            continue
        d = rng.choice(opts)
        m[y + DY[d]][x + DX[d]] = 0
        m[y + 2 * DY[d]][x + 2 * DX[d]] = 0
        st.append((x + 2 * DX[d], y + 2 * DY[d]))
    for y in range(1, 30):
        for x in range(1, 30):
            if m[y][x] == 1 and (x + y) % 2 == 1 and rng.random() < 0.17:
                m[y][x] = 0
    return m


def find_cycle(m, rng: random.Random, lo: int, hi: int) -> list[tuple[int, int]]:
    """a simple corridor cycle with lo <= length <= hi (cells, odd-cell rooms and the links between them)"""
    open_cells = [(x, y) for y in range(MAPW) for x in range(MAPW) if m[y][x] == 0]
    best = None
    for _ in range(400):
        root = rng.choice(open_cells)
        par = {root: None}
        dep = {root: 0}
        q = deque([root])
        found = None
        while q and not found:
            u = q.popleft()
            for d in rng.sample(range(4), 4):
                v = (u[0] + DX[d], u[1] + DY[d])
                if m[v[1]][v[0]] != 0:
                    continue
                if v not in par:
                    par[v] = u
                    dep[v] = dep[u] + 1
                    q.append(v)
                elif par[u] != v and dep[v] >= dep[u]:
                    pu, pv = [u], [v]
                    while pu[-1] != pv[-1]:
                        if dep[pu[-1]] >= dep[pv[-1]]:
                            pu.append(par[pu[-1]])
                        else:
                            pv.append(par[pv[-1]])
                    cyc = pu + pv[-2::-1]
                    if lo <= len(cyc) <= hi and len(set(cyc)) == len(cyc):
                        found = cyc
                        break
        if found and (best is None or turns(found) * len(best) < turns(best) * len(found)):
            best = found
    assert best, "no cycle"
    return best


def turns(c) -> int:
    n = len(c)
    t = 0
    for i in range(n):
        a, b, d = c[i - 1], c[i], c[(i + 1) % n]
        if (b[0] - a[0], b[1] - a[1]) != (d[0] - b[0], d[1] - b[1]):
            t += 1
    return t


def dress(m, rng: random.Random, path_cells) -> list[list[int]]:
    """texture ids: doors in dead-end walls facing the walk, features every 3rd wall cell ((x + 2y + ph) mod 3),
    8x8 zones of plain panels or a chair rail, 4x4 patches with the lights out (never on the walk: it stays lit)"""
    ph = rng.randrange(3)
    zone = [[rng.randrange(2) for _ in range(4)] for _ in range(4)]
    dark = [[rng.random() < 0.12 for _ in range(8)] for _ in range(8)]
    on = set(path_cells)
    out = [[0] * MAPW for _ in range(MAPW)]
    for y in range(MAPW):
        for x in range(MAPW):
            if m[y][x] == 0:
                dk = dark[y >> 2][x >> 2] and (x, y) not in on
                out[y][x] = 0x80 | (S_OFFDARK if dk else S_OFFICE)
                continue
            h = rng.randrange(256)
            border = x in (0, MAPW - 1) or y in (0, MAPW - 1)
            # a wall between two open cells in line (a blocked corridor end) becomes a door now and then
            doorish = not border and ((m[y][x - 1] == 0 and m[y][x + 1] == 0) or (m[y - 1][x] == 0 and m[y + 1][x] == 0))
            if doorish and h < 96:
                out[y][x] = T_EXITDOOR if h < 16 else T_DOOR
            elif (x + 2 * y + ph) % 3 == 0 and h < 216:
                out[y][x] = FEATURES[h & 7]
            else:
                out[y][x] = T_WALL + zone[y >> 3][x >> 3]
    return out


def walk(cyc, frames: int, look: float = 1.2, trail: float = 0.35):
    """n cameras around the cycle at constant speed: the camera trails `trail` cells behind the walk point (so it is
    still in the corridor while it turns) and heads for the point `look` cells ahead (smooth, early corners); the
    polyline runs through the cell centres and is cyclic, so the last frame leads straight back into the first.
    (cx, cy, ca) in the renderer's units (8.8 cells, angle 0..1023)"""
    pts = [(x + 0.5, y + 0.5) for x, y in cyc]
    n = len(pts)

    def at(s):
        s %= n
        i = int(s)
        f = s - i
        a, b = pts[i], pts[(i + 1) % n]
        return a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f

    cams = []
    for k in range(frames):
        s = k * n / frames
        # the camera: the mean of a short window behind the walk point (rounds the corner instead of touching it)
        ws = [at(s - trail - 0.15 * j) for j in range(-2, 3)]
        px, py = sum(w[0] for w in ws) / 5, sum(w[1] for w in ws) / 5
        q = at(s + look)
        ang = math.atan2(q[1] - py, q[0] - px)
        ca = round(ang / (2 * math.pi) * 1024) & 1023
        cams.append((round(px * 256), round(py * 256), ca))
    return cams
