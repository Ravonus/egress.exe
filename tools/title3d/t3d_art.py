"""EGRESS 3D renderer art (from proto/raycast/v2/art.py): wall textures, floor/ceiling sets, biome palettes (all data, seeded, per biome).

Wall textures are 16x16 texels in colour indices 1 (light), 2 (mid), 3 (dark); 0 is the row's haze colour and is
only used for real holes. Floor/ceiling sets are 16x16 texels covering a 2x2-cell block (8 texels per cell): one
ceiling light per block (cell 0,0), whose dithered pool lands on the floor under it.
"""

from __future__ import annotations

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def bayer(x: int, y: int) -> int:
    return BAYER[y & 3][x & 3]


def blank(c: int = 2) -> list[list[int]]:
    return [[c] * 16 for _ in range(16)]


def rect(t, x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 <= x < 16 and 0 <= y < 16:
                t[y][x] = c


def box(t, x0, y0, x1, y1, c):
    for x in range(x0, x1 + 1):
        t[y0][x] = c
        t[y1][x] = c
    for y in range(y0, y1 + 1):
        t[y][x0] = c
        t[y][x1] = c


def stamp(t, x0, y0, rows, cmap=None):
    cmap = cmap or {"1": 1, "2": 2, "3": 3}
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in cmap and 0 <= x0 + x < 16 and 0 <= y0 + y < 16:
                t[y0 + y][x0 + x] = cmap[ch]


# ---------------------------------------------------------------------------------------------------------------
# OFFICE (the backrooms floor: wallpaper, seams, baseboards, outlets, posters, doors, exit signs, CRTs, damage)
# ---------------------------------------------------------------------------------------------------------------
def office_base(pat: int) -> list[list[int]]:
    t = blank(2)
    # wallpaper field rows 1..12
    for y in range(1, 13):
        for x in range(16):
            if pat == 0:  # wide stripes: a light rule every 8 texels, a dotted one between
                if x % 8 == 4 or (x % 8 == 0 and y % 2 == 0 and x):
                    t[y][x] = 1
            elif pat == 1:  # small diamond lattice
                if (x + y) % 8 == 0 or (x - y) % 8 == 0:
                    t[y][x] = 1
            elif pat == 2:  # dotted damask: a 4-dot flower every 8
                xx, yy = x % 8, (y + 1) % 6
                if (xx, yy) in ((3, 1), (2, 2), (4, 2), (3, 3)):
                    t[y][x] = 1
                elif (xx, yy) == (3, 2):
                    t[y][x] = 3
    # crown trim and baseboard
    for x in range(16):
        t[0][x] = 3
        t[13][x] = 1
        t[14][x] = 3
        t[15][x] = 3
    # panel seam at the cell edge (one per cell) with a light bevel next to it
    for y in range(1, 13):
        t[y][0] = 3
        t[y][1] = 1 if t[y][1] != 3 else 3
    return t


def office_decor(kind: str, pat: int) -> list[list[int]]:
    t = office_base(pat)
    if kind == "outlet":
        rect(t, 9, 9, 11, 12, 1)
        box(t, 9, 9, 11, 12, 3)
        t[10][10] = 3
        t[11][10] = 3
    elif kind == "poster":  # an abstract print: mountain and sun, thick frame
        rect(t, 4, 2, 12, 10, 1)
        box(t, 4, 2, 12, 10, 3)
        stamp(t, 5, 3, ["...22..", "..2..2.", "...22..", ".......", "...3...", "..333..", ".33333."])
    elif kind == "vent":  # wall grille near the ceiling
        rect(t, 5, 2, 11, 6, 3)
        for y in (3, 5):
            for x in range(6, 11):
                t[y][x] = 1
        box(t, 5, 2, 11, 6, 3)
    elif kind == "exit":  # exit sign plate (EXIT in 3x3 glyphs)
        rect(t, 1, 2, 14, 7, 1)
        box(t, 1, 2, 14, 7, 3)
        stamp(t, 2, 3, ["333.3.3.3.333", "33...3..3..3.", "333.3.3.3..3."])
        for x in range(4, 12):
            t[8][x] = 2 if x % 2 else 1  # glow spill under the sign
    elif kind == "crt":  # a wall-mounted CRT: dark bezel, lit screen, dithered glow around it
        for y in range(1, 12):
            for x in range(2, 14):
                d = max(abs(x - 7.5) - 4, abs(y - 6) - 3.5)
                if 0 < d <= 1.5 and bayer(x, y) < 8:
                    t[y][x] = 1
        rect(t, 4, 3, 11, 9, 3)
        rect(t, 5, 4, 10, 8, 1)
        for y in (5, 7):
            for x in range(5, 11):
                if (x + y) % 2:
                    t[y][x] = 2
        t[10][7] = 3
        t[10][8] = 3
    elif kind == "stain":  # a damp blob, dithered at its edge
        for y in range(5, 13):
            for x in range(3, 13):
                r = ((x - 7.5) / 4.5) ** 2 + ((y - 9.5) / 3.2) ** 2
                if r < 0.45:
                    t[y][x] = 3 if bayer(x, y) < 10 else 2
                elif r < 1.0 and bayer(x, y) < 6:
                    t[y][x] = 3
    elif kind == "water":  # water damage: tide line from the top and drip streaks
        for x in range(16):
            for y in range(1, 6):
                if y < 3 + (x * 7 % 3):
                    t[y][x] = 3 if bayer(x, y) < 9 else t[y][x]
        for x, ln in ((4, 10), (9, 7), (12, 11)):
            for y in range(4, 4 + ln):
                if y < 13:
                    t[y][x] = 3 if y % 3 else 2
    elif kind == "tear":  # a glitch tear: a shifted, inverted band
        src = [row[:] for row in t]
        for y in range(5, 9):
            for x in range(16):
                c = src[y][(x + 5) % 16]
                t[y][x] = {1: 3, 3: 1, 2: 2}[c] if (x // 3 + y) % 2 else c
        for x in range(16):
            if x % 5 != 2:
                t[4][x] = 1
                t[9][x] = 3
    elif kind == "clock":  # wall clock
        for y in range(2, 9):
            for x in range(4, 12):
                r = (x - 7.5) ** 2 + (y - 5.5) ** 2
                if r < 10:
                    t[y][x] = 1
                elif r < 14:
                    t[y][x] = 3
        t[5][7] = t[4][7] = t[3][7] = 3
        t[5][8] = t[5][9] = 3
    return t


def office_door(exit_sign: bool = False) -> list[list[int]]:
    t = office_base(0)
    rect(t, 2, 1, 13, 15, 1)  # frame
    rect(t, 4, 3, 11, 15, 3)  # slab
    rect(t, 5, 4, 10, 8, 2)  # upper panel
    rect(t, 5, 10, 10, 14, 2)  # lower panel
    t[9][10] = 1  # knob
    t[9][9] = 1
    if exit_sign:
        rect(t, 4, 0, 11, 1, 1)
        stamp(t, 5, 0, ["3.3.3.", ".3.3.3"])
    else:
        rect(t, 7, 1, 8, 2, 3)  # number plate
    return t


def office_burn(frame: int) -> list[list[int]]:
    """a poster wall burning (the puzzle reaction): flames climb, embers, then char"""
    t = office_decor("poster", 1)
    for y in range(16):
        for x in range(16):
            h = 13 - frame * 4 + ((x * 5 + frame * 3) % 4)
            if y > h:
                t[y][x] = 3 if (x + y + frame) % 3 else 1
            elif y == h:
                t[y][x] = 1
            elif y == h - 1 and (x + frame) % 3 == 0:
                t[y][x] = 1
    return t


def office_char() -> list[list[int]]:
    t = blank(3)
    for y in range(16):
        for x in range(16):
            if bayer(x, y) < 3:
                t[y][x] = 2
    for x in range(16):
        t[15][x] = 3
    return t


# ---------------------------------------------------------------------------------------------------------------
# VENTS (duct metal: riveted plates, grates, fans, pipes, hazard bands, hatches)
# ---------------------------------------------------------------------------------------------------------------
def vents_base(pat: int) -> list[list[int]]:
    t = blank(2)
    for x in range(16):
        t[0][x] = 3
        t[8][x] = 3
        t[1][x] = 1
        t[9][x] = 1
    for y in range(16):
        t[y][0] = 3
        if pat == 1:
            t[y][8] = 3
    for (x, y) in ((2, 3), (13, 3), (2, 6), (13, 6), (2, 11), (13, 11), (2, 14), (13, 14)):
        t[y][x] = 1
        t[y][x + 1 if x < 15 else x] = 3 if pat == 0 else t[y][x]
    if pat == 2:  # corrugated ribs
        for y in range(2, 16):
            if y % 3 == 0 and y not in (8, 9):
                for x in range(1, 16):
                    t[y][x] = 3
            elif y % 3 == 1 and y not in (8, 9):
                for x in range(1, 16):
                    t[y][x] = 1
    return t


def vents_decor(kind: str, pat: int) -> list[list[int]]:
    t = vents_base(pat)
    if kind == "grate":
        rect(t, 3, 3, 12, 12, 3)
        for y in range(4, 12):
            for x in range(4, 12):
                if (x + y) % 3 == 0 or (x - y) % 3 == 0:
                    t[y][x] = 2
        box(t, 3, 3, 12, 12, 1)
    elif kind == "fan":
        for y in range(2, 15):
            for x in range(2, 15):
                r = (x - 8) ** 2 + (y - 8) ** 2
                if r <= 30:
                    t[y][x] = 3
                    a = (x - 8) * (y - 8)
                    if r > 3 and ((a > 0 and abs(x - y) <= 1) or (a < 0 and abs(x + y - 16) <= 1)):
                        t[y][x] = 2
                elif r <= 42:
                    t[y][x] = 1
        t[8][8] = 1
    elif kind == "pipes":
        for x0 in (4, 10):
            for y in range(16):
                t[y][x0] = 1
                t[y][x0 + 1] = 2
                t[y][x0 + 2] = 3
            for y in (5, 12):
                rect(t, x0 - 1, y, x0 + 3, y, 3)
    elif kind == "hazard":
        for y in range(11, 15):
            for x in range(16):
                t[y][x] = 1 if ((x + y) // 2) % 2 else 3
    elif kind == "sign":  # warning triangle
        stamp(t, 4, 2, ["...11...", "..1331..", "..1331..", ".113311.", ".111111.", "11133111", "11111111"])
    elif kind == "steam":  # a vent shaft cover the bench can steam up
        rect(t, 4, 4, 11, 11, 3)
        for y in range(5, 11, 2):
            rect(t, 5, y, 10, y, 1)
        box(t, 4, 4, 11, 11, 1)
    elif kind == "ladder":
        for y in range(16):
            t[y][4] = 3
            t[y][11] = 3
        for y in range(2, 16, 3):
            rect(t, 5, y, 10, y, 1)
    return t


def vents_hatch() -> list[list[int]]:
    t = vents_base(0)
    rect(t, 2, 2, 13, 15, 3)
    rect(t, 3, 3, 12, 15, 2)
    for y in range(5, 12):
        for x in range(4, 12):
            r = (x - 7.5) ** 2 + (y - 8) ** 2
            if 6 <= r <= 12:
                t[y][x] = 1
    t[8][7] = t[8][8] = 3
    return t


def blend(a, b, frac16: int):
    """texture-space ordered-dither mix: b where bayer < frac16"""
    return [[b[y][x] if bayer(x, y) < frac16 else a[y][x] for x in range(16)] for y in range(16)]


# ---------------------------------------------------------------------------------------------------------------
# floor + ceiling sets (16x16 texels over a 2x2-cell block; light at cell (0,0), centre texel (4,4))
# ---------------------------------------------------------------------------------------------------------------
def pool(x, y, lit):
    if not lit:
        return 0.0
    dx, dy = (x + 0.5) - 12, (y + 0.5) - 12  # light over cell (1,1) of the block: maze rooms sit on odd cells
    r = (dx * dx + dy * dy) ** 0.5
    return max(0.0, 1.0 - r / 5.2)


def office_floor(lit=True):
    t = blank(0)
    for y in range(16):
        for x in range(16):
            n = (x * 7 + y * 13 + (x * y) % 5) % 11
            c = 0
            if n == 0 and (x * 3 + y) % 4 == 0:
                c = 3  # carpet fleck
            elif n in (2, 7):
                c = 2
            p = pool(x, y, lit)
            if p > 0:
                v = p * 20
                if v > 10 and bayer(x, y) < (v - 10) * 1.6:
                    c = 1
                elif bayer(x, y) < v * 1.2:
                    c = 2 if c != 3 else 3
            t[y][x] = c
    return t


def office_ceiling(lit=True):
    t = blank(0)
    for y in range(16):
        for x in range(16):
            if x % 4 == 0 or y % 4 == 0:
                t[y][x] = 2  # acoustic tile grid, 2x2 tiles per cell
    # fluorescent panel in cell (0,0): 4x2 texels with a dark frame
    rect(t, 9, 10, 15, 14, 3)
    rect(t, 10, 11, 14, 13, 1 if lit else 2)
    return t


def vents_floor(lit=True):
    t = blank(0)
    for y in range(16):
        for x in range(16):
            c = 0
            if x % 4 == 0 or y % 4 == 0:
                c = 3
            elif (x + y) % 4 == 2:
                c = 2
            p = pool(x, y, lit)
            if p > 0.35 and bayer(x, y) < (p - 0.35) * 30 and c != 3:
                c = 1
            t[y][x] = c
    return t


def vents_ceiling(lit=True):
    t = blank(0)
    for y in range(16):
        for x in range(16):
            if y % 4 == 0:
                t[y][x] = 3
            elif y % 4 == 1:
                t[y][x] = 2
    if lit:  # a caged lamp
        rect(t, 11, 11, 13, 13, 1)
        t[11][11] = t[13][13] = t[11][13] = t[13][11] = 3
    return t


def tex_blend_sets(a, b, frac16):
    return [[b[y][x] if bayer(x, y) < frac16 else a[y][x] for x in range(16)] for y in range(16)]


# ---------------------------------------------------------------------------------------------------------------
# palettes: 8 BG palettes, one per tile row of the 64-px view (rows 0-3 ceiling half, 4-7 floor half)
#   colour 0 = that row's ceiling/floor base, fading to the haze at the horizon; 1/2/3 = wall light/mid/dark with
#   a gentle top-lit falloff
# ---------------------------------------------------------------------------------------------------------------
def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


BIOMES = {
    "office": {
        "ceil": [(196, 178, 230), (172, 152, 214), (150, 128, 196), (126, 104, 176)],
        "floor": [(118, 92, 158), (138, 106, 166), (156, 118, 170), (174, 128, 172)],
        "light": (252, 222, 236), "mid": (184, 166, 234), "dark": (70, 62, 140),
    },
    "vents": {
        "ceil": [(150, 206, 196), (110, 170, 168), (72, 130, 136), (40, 88, 104)],
        "floor": [(34, 74, 92), (54, 104, 116), (80, 140, 140), (110, 176, 164)],
        "light": (206, 248, 220), "mid": (92, 174, 178), "dark": (22, 58, 82),
    },
    "dark": {  # office with its lights out (a GLOWS item lights a cone): same textures, low palette
        "ceil": [(98, 80, 140), (78, 62, 120), (60, 46, 100), (42, 30, 78)],
        "floor": [(40, 28, 74), (56, 40, 92), (72, 52, 108), (90, 66, 124)],
        "light": (170, 140, 200), "mid": (96, 82, 150), "dark": (34, 26, 70),
    },
}


def biome_palettes(name: str, nrows: int = 9) -> list[list[tuple]]:
    """one palette per tile row of the view; with 9 rows (72 px) the middle row straddles the horizon and takes the
    mean of the last ceiling and the first floor colour"""
    b = BIOMES[name]
    if nrows == 9:
        mid = lerp(b["ceil"][3], b["floor"][0], 0.5)
        rows = b["ceil"] + [mid] + b["floor"]
    else:
        rows = b["ceil"] + b["floor"]
    out = []
    last = nrows - 1
    for r in range(nrows):
        k = abs(r - last / 2) / (last / 2)  # 0 at the horizon rows, 1 at top/bottom
        shade = 0.88 + 0.12 * (1 - r / last)  # top-lit: walls a touch brighter near the lights
        out.append([rows[r], lerp((0, 0, 0), b["light"], shade), lerp((0, 0, 0), b["mid"], shade),
                    lerp((0, 0, 0), b["dark"], 0.9 + 0.1 * k)])
    return out


def q555(c):
    return tuple((v >> 3) << 3 for v in c)


# ---------------------------------------------------------------------------------------------------------------
# THE TITLE'S OWN SET (docs/redesign.md 15.1, mock_title.png): flat panels that read at 24 rays (each texel column
# is 4 px wide on screen, so vertical detail aliases and only bold shapes survive). Horizontal bands carry the look
# (crown, chair rail, a deep baseboard); one dark seam per cell; features are big and outlined in the dark colour so
# they survive the y-face remap (light -> mid). No speckle, no texture dither: the dither is the fog's alone.
# ---------------------------------------------------------------------------------------------------------------
def title_wall(rail: bool = False) -> list[list[int]]:
    t = blank(1)
    for x in range(16):
        t[0][x] = 3                       # crown
        t[12][x] = 2                      # baseboard cap
        for y in (13, 14, 15):
            t[y][x] = 3                   # baseboard
        if rail:
            t[7][x] = 3                   # chair rail over a mid wainscot
            for y in range(8, 12):
                t[y][x] = 2
    for y in range(1, 12):                # the panel seam, two texels across the cell edge
        t[y][0] = 3
        t[y][15] = 3
    return t


def title_feature(kind: str) -> list[list[int]]:
    t = title_wall(False)
    if kind == "door":
        rect(t, 3, 1, 12, 15, 3)          # frame
        rect(t, 5, 3, 10, 15, 2)          # slab
        rect(t, 6, 5, 9, 8, 3)            # window
        t[10][9] = 1
        t[10][8] = 1                      # handle
    elif kind == "exitdoor":
        t = title_feature("door")
        rect(t, 4, 0, 11, 2, 3)           # a lit EXIT plate over the frame
        rect(t, 5, 1, 10, 1, 1)
    elif kind == "poster":                # sun over hills, thick frame
        rect(t, 4, 2, 11, 10, 3)
        rect(t, 5, 3, 10, 9, 2)
        rect(t, 7, 4, 8, 5, 1)
        rect(t, 5, 8, 10, 9, 3)
        t[7][6] = t[7][9] = 3
    elif kind == "poster2":               # a tall notice: three dark text bars
        rect(t, 5, 2, 10, 11, 3)
        rect(t, 6, 3, 9, 10, 1)
        for y in (4, 6, 8):
            rect(t, 6, y, 9 if y != 8 else 8, y, 3)
    elif kind == "exit":                  # a wall EXIT plate with an arrow under it
        rect(t, 3, 2, 12, 6, 3)
        rect(t, 4, 3, 11, 5, 1)
        rect(t, 5, 4, 10, 4, 3)
        stamp(t, 6, 8, ["..3.", "3333", "..3."])
    elif kind == "vent":
        rect(t, 4, 2, 11, 6, 3)
        for y in (3, 5):
            rect(t, 5, y, 10, y, 2)
    elif kind == "clock":
        for y in range(2, 9):
            for x in range(4, 12):
                r = (x - 7.5) ** 2 + (y - 5.5) ** 2
                if r < 9:
                    t[y][x] = 1
                elif r < 15:
                    t[y][x] = 3
        t[3][7] = t[4][7] = t[5][7] = 3
        t[5][8] = t[5][9] = 3
    elif kind == "board":                 # a long cork board: wide dark frame, mid field, light notes
        rect(t, 1, 3, 14, 10, 3)
        rect(t, 2, 4, 13, 9, 2)
        rect(t, 3, 5, 5, 7, 1)
        rect(t, 8, 5, 10, 8, 1)
        rect(t, 12, 6, 13, 7, 1)
    return t


def title_floor(lit: bool = True) -> list[list[int]]:
    return blank(0)                       # the rows' palette gradient is the carpet (mock_title.png)


def title_ceiling(lit: bool = True) -> list[list[int]]:
    t = blank(0)
    rect(t, 9, 10, 15, 14, 2)             # one fluorescent panel per 2x2 cells: a mid rim, a light tube
    rect(t, 10, 11, 14, 13, 1 if lit else 2)
    return t
