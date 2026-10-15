"""Family tree screen mock (design only, not game code). See docs/redesign.md section 9.6.

Draws the TREE screen the way the cartridge would hold it, and checks it against the CGB limits:
  * the tree is one tall BG tilemap (streamed a generation at a time) using 8 fixed BG palettes x 4 colours;
  * unpicked siblings ("roads not taken") are BG tiles in one shared faded palette;
  * played heirs are 16x16 OBJ (two 8x16 sprites) in their own 3-colour palette: ink/hair, skin, cloth;
  * mini portraits come from a small 16x16 bust generator that reads the same look genes as the face generator
    (skin, face shape, hair, neck, size, accent, mark), not from a reduction of the 48x48 face;
  * every 8x8 BG tile must fit one BG palette, every sprite one OBJ palette, <= 10 sprites per scan line,
    <= 8 palettes of each kind on screen. `check()` enforces that and counts unique tiles for the VRAM budget.

    .venv/bin/python tools/mock/tree_mock.py          # writes docs/shots/tree_*.png and tree_scroll.gif

The lineage comes from proto/genetics/genes.py (the cartridge's integer rules), bred for HEAVY over 22 generations.
Deaths, epitaphs, feats, factions and birth stamps are seeded mock data with the field sizes of the SRAM record.
"""
from __future__ import annotations

import datetime as dt
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from egkit import AP, FONT, ROOT  # noqa: E402

sys.path.insert(0, str(ROOT / "proto/genetics"))
import genes  # noqa: E402
import death_text as DT  # noqa: E402  (the 12b grammar and epitaph pools: the node prints what the card printed)

OUT = ROOT / "docs/shots"
W, H = 160, 144

# ------------------------------------------------------------------------------------------------ colours
V0, V1, V2, STAR = (14, 10, 34), (34, 20, 70), (70, 40, 120), (236, 200, 255)
LILAC, CYAN, MAGENTA, PINK = (150, 120, 206), (64, 224, 248), (236, 80, 196), (255, 112, 200)
WHITE, INK, PANEL, YEL = (250, 246, 255), (20, 14, 46), (52, 32, 100), (255, 224, 96)
GDIM, GHOT = (54, 24, 92), (255, 112, 200)
G1, G2, G3 = (58, 42, 104), (98, 78, 150), (150, 128, 200)
AMBER, ICE, EMBER, MINT, PEACH = (255, 176, 72), (120, 200, 255), (244, 96, 84), (120, 248, 196), (248, 196, 170)
GREEN, RED, CARD = (110, 230, 140), (244, 84, 96), (30, 18, 62)

# BG palettes of the tree screen. Slot 7 is swapped by SELECT (one palette write + the right-hand map columns).
BG_TREE = {
    0: ("VOID", [V0, V1, V2, STAR]),
    1: ("LINES", [V0, LILAC, CYAN, MAGENTA]),
    2: ("GLOW", [V0, GDIM, GHOT, CYAN]),  # entry 2 is palette-cycled: pink > lilac > cyan > lilac
    3: ("GHOST", [V0, G1, G2, G3]),  # every unpicked sibling shares this one
    4: ("TEXT", [V0, V1, PINK, WHITE]),
    5: ("UI-A", [INK, PANEL, CYAN, WHITE]),
    6: ("UI-B", [INK, PANEL, CYAN, PINK]),
}
VIEW_PAL = {
    "FACES": ("SPARE", [V0, V1, YEL, WHITE]),
    "LOOKS": ("BANDS", [V0, AMBER, ICE, EMBER]),
    "STATS": ("BARS", [V0, YEL, GREEN, PINK]),
    "TRUTH": ("LEANS", [V0, PEACH, MINT, PINK]),
}
OBJ_CURSOR = ("OBJ7 CURSOR/GLYPHS", [WHITE, YEL, PINK])  # cursor, sun-sign glyphs and glitch sparkles share OBJ7
OBJ_SPARK = OBJ_CURSOR

# ------------------------------------------------------------------------------------------------ text
def text(im, x, y, s, col=WHITE, shadow=INK):
    """the M2 font (tools/bake_art.py); glyphs are 4x7 + a 1 px drop shadow, so a tile-aligned glyph stays in one tile"""
    px = im.load()
    for ch in s.upper():
        if ch in FONT:
            rows = FONT[ch]
            w = len(rows[0])
            ox = (8 - w) // 2 if w < 4 else 1
            for yy, row in enumerate(rows):
                for xx, b in enumerate(row):
                    if b == "1":
                        for (dx, dy, c) in ((1, 1, shadow), (0, 0, col)):
                            X, Y = x + ox + xx + dx, y + yy + dy
                            if c and 0 <= X < im.width and 0 <= Y < im.height:
                                px[X, Y] = c
        x += 8


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def bayer(x, y, t):
    return BAYER[y & 3][x & 3] < t * 16


def bits(im, x, y, rows, col):
    for yy, r in enumerate(rows):
        for xx, c in enumerate(r):
            if c != ".":
                im.putpixel((x + xx, y + yy), col[c] if isinstance(col, dict) else col)


# 8x8 icons (1 colour unless a dict is passed). Zodiac glyphs are drawn for the signs the mock line touches.
SIGN = {
    "VIRGO": ["#.#.#...", "###.#...", "#.#.#.#.", "#.#.###.", "#.#.#.#.", "#.#.##..", "....##..", "...#.#.."],
    "LIBRA": ["........", "..###...", ".#...#..", ".#...#..", "##...##.", "........", "#######.", "........"],
    "SCORPIO": ["#.#.#...", "###.#...", "#.#.#...", "#.#.#...", "#.#.#...", "#.#.#.#.", "....###.", "......#."],
}
SIGN.update({
    "ARIES": ["#.....#.", ".#...#..", ".#.#.#..", "..#.#...", "...#....", "...#....", "...#....", "........"],
    "TAURUS": ["#.....#.", ".#####..", "..###...", ".#...#..", ".#...#..", ".#...#..", "..###...", "........"],
    "GEMINI": ["#######.", ".#...#..", ".#...#..", ".#...#..", ".#...#..", ".#...#..", "#######.", "........"],
    "CANCER": [".####...", "#....#..", ".##..#..", "........", "..#..##.", "..#....#", "...####.", "........"],
    "LEO": ["..###...", ".#...#..", ".#...#..", "..#..#..", "##...#..", "##...#.#", "......#.", "........"],
    "SAGITTARIUS": ["...####.", ".....##.", "....#.#.", ".#.#....", "..#.....", ".#.#....", "#.......", "........"],
    "CAPRICORN": ["#.#.....", ".#.#....", ".#.#.##.", ".#.##..#", ".#.#.##.", "....#...", "...#....", "........"],
    "AQUARIUS": [".#.#.#..", "#.#.#.#.", "........", ".#.#.#..", "#.#.#.#.", "........", "........", "........"],
    "PISCES": ["#.....#.", ".#...#..", ".#...#..", "#######.", ".#...#..", ".#...#..", "#.....#.", "........"],
})
SIGN_NAMES = [("CAPRICORN", 120), ("AQUARIUS", 219), ("PISCES", 320), ("ARIES", 420), ("TAURUS", 521),
              ("GEMINI", 621), ("CANCER", 723), ("LEO", 823), ("VIRGO", 923), ("LIBRA", 1023), ("SCORPIO", 1122),
              ("SAGITTARIUS", 1222), ("CAPRICORN", 1300)]
ICON = {
    "ECHO": ["...##...", "..#..#..", ".#.##.#.", ".#.#..#.", ".#.##.#.", "..#..#..", "...##...", "........"],
    "REMEMBERS": [".####...", ".#..#...", ".####...", "...#....", "..##....", "...#....", "..##....", "........"],
    "COLOURBLIND": ["..###...", ".####.#.", "#####..#", "#####..#", "#####..#", ".####.#.", "..###...", "........"],
    "GLITCHED": ["#.......", ".#...#..", "..#.#.#.", "...#...#", "..#.....", ".#.#....", "#...#...", "........"],
    "HEART": [".##.##..", "#######.", "#######.", ".#####..", "..###...", "...#....", "........", "........"],
    "CRACK": [".##.##..", "##.####.", "###.###.", ".##.##..", "..#.#...", "...#....", "........", "........"],
    "CROWN": ["........", "#..#..#.", "##.#.##.", "#######.", "#######.", ".#####..", "........", "........"],
    "FLASK": ["..###...", "...#....", "...#....", "..#.#...", ".#...#..", "#.###.#.", "#######.", "........"],
    "WATCH": ["........", "..####..", ".#....#.", "#..##..#", "#..##..#", ".#....#.", "..####..", "........"],
    "ROOT": ["...#....", ".#.#.#..", "..###...", "...#....", "...#....", "..#.#...", ".#...#..", "........"],
    "COMPILE": [".#....#.", "#......#", "#..##..#", "#..##..#", "#......#", ".#....#.", "........", "........"],
    "CHARGE": ["....##..", "...##...", "..##....", ".#####..", "...##...", "..##....", ".##.....", "........"],
    "SKY": ["........", "...##...", ".##..##.", "#......#", "########", "........", "........", "........"],
    "LOCK": ["..###...", ".#...#..", ".#...#..", "#######.", "###.###.", "###.###.", "#######.", "........"],
}


def sun_sign(t):
    md = t.month * 100 + t.day
    for name, edge in SIGN_NAMES:
        if md < edge:
            return name
    return "CAPRICORN"


# ------------------------------------------------------------------------------------------------ mini busts
# 16x16, three colours: 1 ink (hair, eyes, outline), 2 skin, 3 cloth. Built per node from the look genes in well
# under a millisecond (part masks + a few loops), so nothing heavier than the face generator's tables is needed.
def bust(g):
    p = genes.express(g)
    hair = eff_hair(g)
    shape, size, mark, neck = g["shape"], p["size"], g["mark"], g["neck"]
    m = [[0] * 16 for _ in range(16)]
    cx = 7.5
    rx = 3.3 + 0.45 * (shape % 3) + (0.8 if size == 2 else (-0.5 if size == 0 else 0))
    ry = 4.6
    cy = 7.6
    ex = 2.0 if shape < 4 else 3.2  # round or square jaw
    for y in range(16):
        for x in range(16):
            dx, dy = abs(x - cx) / rx, abs(y - cy) / ry
            if dx ** ex + dy ** 2 <= 1.0 and y >= 3:
                m[y][x] = 2
    # shoulders and neck (cloth), heavy lines get wide shoulders
    sw = 6 + (1 if size == 2 else 0) - (1 if size == 0 else 0)
    for y in range(12, 16):
        half = sw - max(0, 14 - y)  # rounded shoulders
        for x in range(int(cx - half + 0.5), int(cx + half + 0.5) + 1):
            if 0 <= x < 16 and m[y][x] == 0:
                m[y][x] = 3
    for y in (12,):
        for x in (7, 8):
            m[y][x] = 2
    if neck == 1:  # V collar
        m[13][7] = m[13][8] = 2
    if neck == 2:  # high collar
        m[12][6] = m[12][9] = 3
    # hair (ink)
    def cap(rows_to):
        for y in range(1, rows_to + 1):
            for x in range(16):
                dx, dy = abs(x - cx) / (rx + 0.7), abs(y - cy) / (ry + 0.9)
                if dx ** 2 + dy ** 2 <= 1.0:
                    m[y][x] = 1
    if hair == 1:  # short
        cap(5)
        m[6][int(cx - rx + 0.5)] = m[6][int(cx + rx + 0.5)] = 1
    elif hair == 2:  # long
        cap(5)
        for y in range(5, 14):
            for x in (int(cx - rx - 0.2), int(cx - rx + 0.8), int(cx + rx + 0.2), int(cx + rx - 0.8)):
                if 0 <= x < 16 and (m[y][x] != 2 or y < 9):
                    m[y][x] = 1
    elif hair == 3:  # bun
        cap(4)
        for y in range(0, 3):
            for x in (7, 8) if y == 0 else (6, 7, 8, 9):
                m[y][x] = 1
    elif hair == 4:  # spikes
        cap(5)
        for x in range(int(cx - rx), int(cx + rx) + 2, 2):
            if 0 <= x < 16:
                m[1][x] = 1
                m[2][x] = 1
    # face: eyes, mouth
    ey = 8
    m[ey][6] = m[ey][9] = 1
    m[11][7] = m[11][8] = 1
    if g["mouth"]:
        m[10][6] = m[10][9] = 1 if m[10][6] == 2 else m[10][6]
    # marks
    if mark == 1:  # glasses: a frame line over the eyes and the rims beside them, so the eyes still show
        for x in range(5, 11):
            m[ey - 1][x] = 1
        m[ey][5] = m[ey][10] = 1
    elif mark == 2:  # headphones
        for x in range(4, 12):
            if m[2][x] == 0:
                m[2][x] = 1
        for y in (7, 8, 9):
            for x in (int(cx - rx - 0.5), int(cx + rx + 1.5)):
                if 0 <= x < 16:
                    m[y][x] = 3
    elif mark == 3:  # scarf
        for x in range(4, 12):
            m[12][x] = 3
    elif mark == 4:  # beanie
        for y in range(1, 5):
            for x in range(16):
                if m[y][x] == 1 or (y >= 3 and m[y][x] == 2):
                    m[y][x] = 3
    elif mark == 7:  # visor
        for x in range(5, 11):
            m[ey][x] = 3
    # outline: ink around the silhouette
    o = [r[:] for r in m]
    for y in range(16):
        for x in range(16):
            if m[y][x] == 0 and any(
                0 <= x + dx < 16 and 0 <= y + dy < 16 and m[y + dy][x + dx] in (2, 3)
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
            ) and y < 15:
                o[y][x] = 1
    return o


def eff_hair(g):
    gr = g["hair"]
    if g["look"] == 1:
        gr = 1 if gr in (1, 4) else 0
        return 4 if gr & 1 else 1
    if g["look"] == 2:
        return 3 if gr & 1 else 2
    return gr if gr < 5 else 0


# the accent gene's 8 cloth colours: saturated and spread round the wheel, none near a skin tone (the old orange
# read as skin on warm lines), so a family's busts differ in what they wear (9.6, "Tree colour")
CLOTH = [(88, 112, 236), (120, 192, 252), (40, 184, 168), (112, 208, 96), (168, 108, 244), (228, 56, 92),
         (244, 200, 64), (244, 100, 204)]


def obj_pal(g):
    """the node's OBJ palette: the portrait's own ink and skin (face generator) + the cloth of the accent gene"""
    _, pal = AP.faces(["genome", genes.face_genome(g)])[0]
    return [AP.rgb(pal[1]), AP.rgb(pal[2]), CLOTH[genes.express(g)["accent"]]]


# ------------------------------------------------------------------------------------------------ the line (data)
NAMES = ["VEDA", "OREN", "PIM", "CORA", "MIRA", "JUNO", "TAVI", "ODA", "REN", "ISA", "BEX", "NIKO", "SOL", "LUZ",
         "KAI", "ADA", "IVO", "NELL", "RUMI", "TEO", "ZIA", "OTTO", "MAE", "ELIO", "WREN", "AMAR", "SIV", "LEO",
         "NOOR", "HUGO", "IKA", "DESI"]
BIOMES = ["OFFICE", "VENTS", "ATRIUM", "SERVER", "MALL", "GARDEN", "ROOF", "CORE"]
# Death records as the node stores them: 3 cause bytes (12b.4 grammar), the epitaph id, the best feat's type.
DEATHS = [
    DT.rec("HIT", "MOB", "mob", "INTERN", "DAMP", "MID-{DODGE}"), DT.rec("CRUSH", "SELF", "object", "CRT"),
    DT.rec("FREEZE", "SELF", "reaction", "STEAM"), DT.rec("SHORT", "SELF", "object", "PUDDLE"),
    DT.rec("BURN", "MOB", "mob", "LAMP", "SORRY", "LAST HEART"), DT.rec("HIT", "MOB", "mob", "TOASTER", "RIME",
                                                                        "{SECS}S IN", 4),
    DT.rec("BEAM", "BOSS", "boss", "OLD MONITOR", 0, "PHASE {PHASE}", 2), DT.rec("VOID", "ROOM", "object", 0),
    DT.rec("DROWN", "ROOM", "object", "LEAK", 0, "{ITEM} IN HAND", 0), DT.rec("GLITCH", "MOB", "mob", "MOTH", "NOCLIP"),
    DT.rec("CRUSH", "SELF", "object", "FILING CABINET"), DT.rec("HIT", "MOB", "mob", "STAPLER", "OVERDUE", "ONE HIT SHORT"),
    DT.rec("BURN", "SELF", "reaction", "ORB"), DT.rec("OVERGROW", "SELF", "object", "SPROUT"),
]
FEATS = [("CROWN", "FIRST BOSS", "BOSS"), ("FLASK", "RAREST MIX", "RECIPE"), ("CROWN", "SLEW AN ELITE", "ELITE"),
         ("FLASK", "MADE GLASS", "RECIPE")]
FACTIONS = ["COMPILE", "CHARGE", "SKY", "WATCH", "ROOT", "LOCK"]
RING = genes.RING  # 24 heirs in the save's ring (9.5 / 9.6); the newest 16 are the mate pool
FOUNDERS_TO = 6  # G1..G6 compressed into the founders node; the ring holds G7..G30
PLAYED = FOUNDERS_TO + RING  # 30 played generations in the mock line; the kids waiting are G31
NEXT = PLAYED + 1


def build_line(seed=None):
    """30 played generations bred for HEAVY, plus the 3 kids waiting at the heir pick (G31)"""
    best = None
    for s in ([seed] if seed else range(1, 300)):
        hist = genes.run("heavy", s * 7919, gens=PLAYED)
        r = genes.Rng(s * 7919)
        fnd = genes.founder(r)
        picks = [h["pick"] for h in hist[:PLAYED - 1]]
        heavy = ["HEAVY" in genes.express(p)["traits"] for p in picks[5:]]
        mut_spine = [i for i, p in enumerate(picks[5:], 7) if p["flags"]["mutated"]]
        mut_sib = [i for i, h in enumerate(hist[5:PLAYED - 1], 7) if any(x["flags"]["mutated"] for x in h["sibs"] if x is not h["pick"])]
        skins = {p["skin"] for p in picks}
        ok = heavy[:3].count(True) <= 1 and heavy[-6:].count(True) >= 5 and mut_spine and mut_sib and len(skins) >= 2
        if ok:
            best = (s, hist)
            break
    s, hist = best
    rng = genes.Rng(s * 31 + 5)
    t = dt.datetime(2026, 9, 14, 21, 3)
    line = []
    for gi in range(PLAYED):  # G1..G30
        if gi == 0:
            g, sibs = None, []
        else:
            h = hist[gi - 1]
            g, sibs = h["pick"], [x for x in h["sibs"] if x is not h["pick"]]
        line.append({"gen": gi + 1, "g": g, "sibs": sibs, "born": t})
        u = rng.u8()
        life = dt.timedelta(minutes=25 + (u * u * 28 >> 8) + rng.u8())  # real time between deaths: 25 min..5 days
        t = t + life
    # the founder: the parent the sim started from (re-seeded identically)
    line[0]["g"] = genes.founder(genes.Rng(s * 7919))
    kids = hist[PLAYED - 1]["sibs"]
    for i, n in enumerate(line):
        p = genes.express(n["g"])
        n["name"] = NAMES[(i * 11 + s) % len(NAMES)]
        n["floor"] = min(8, 2 + (rng.u8() * 7 >> 8) + (1 if "HEAVY" in p["traits"] else 0))
        n["biome"] = BIOMES[(n["floor"] - 1) % 8]
        n["cause_b"] = DEATHS[rng.pick(len(DEATHS))]
        n["feat"] = FEATS[rng.pick(len(FEATS))] if rng.u8() < 150 else None
        li = rng.pick(6)
        n["liked"], n["hated"] = FACTIONS[li], FACTIONS[(li + 1 + rng.pick(5)) % 6]
        n["ghosts"] = rng.pick(4)  # times met as a ghost / body in later runs
        n["died"] = line[i + 1]["born"] if i + 1 < len(line) else t
        n["truth"] = [((rng.u8() >> 4) - 8 + i // 2) for _ in range(4)]  # 4 axes, signed nibble lean
        lean = dict(zip(("CAUSE", "CULPRIT", "REVERSIBLE", "SELF"), n["truth"]))  # TRUTH thread order
        ft = n["feat"][2] if n["feat"] else None
        n["epi_id"] = DT.choose_epitaph(lean, p["traits"], ft, rng.u8())
        n["epitaph"] = DT.epitaph_text(n["epi_id"], n["g"]["look"], p["traits"], ft)
        n["cause"] = DT.cause_lines(n["cause_b"], n["g"]["look"], n["biome"], n["floor"], p["dodge"])
        n["sibnames"] = [NAMES[(i * 7 + k * 13 + s + 3) % len(NAMES)] for k in range(2)]
    return line, kids, t


# ------------------------------------------------------------------------------------------------ tall tree layout
PITCH = 24  # one generation = 3 tile rows: 2 for the 16x16 nodes, 1 for the connector under them
TOP = 8
SPINE_X, SIB_X = 72, (40, 104)  # tile columns 9-10 (the live branch), 5-6 and 13-14 (roads not taken)
MID = SPINE_X + 7
WIN_Y, WIN_H = 16, 104  # the scrolling window between the fixed bars (top 2 tile rows, bottom 3)
KIDS_ROW = RING + 1


def row_y(r):
    return TOP + PITCH * r


class Layer:
    """a BG layer (RGB; every 8x8 tile must fit one BG palette) plus a list of 8x16 OBJ sprites"""

    def __init__(self, w, h):
        self.im = Image.new("RGB", (w, h), V0)
        self.sprites = []  # (x, y, 8x16 RGBA, (palette name, 3 colours))

    def sprite16(self, x, y, m, pal, name):
        """a 16x16 portrait as two 8x16 OBJ in one palette"""
        for half in (0, 1):
            t = Image.new("RGBA", (8, 16), (0, 0, 0, 0))
            for yy in range(16):
                for xx in range(8):
                    c = m[yy][half * 8 + xx]
                    if c:
                        t.putpixel((xx, yy), (*pal[c - 1], 255))
            self.sprites.append((x + half * 8, y, t, (name, pal)))

    def sprite_bits(self, x, y, rows, col, pal):
        t = Image.new("RGBA", (8, 16), (0, 0, 0, 0))
        for yy, r in enumerate(rows):
            for xx, c in enumerate(r):
                if c != ".":
                    t.putpixel((xx, yy), (*(col[c] if isinstance(col, dict) else col), 255))
        self.sprites.append((x, y, t, pal))


def void_tiles(im, seed=5):
    """the void, only in tiles nothing else uses: indigo, sparse ordered-dither drift lines and stars (VOID)"""
    px = im.load()
    r = genes.Rng(seed)
    for ty in range(im.height // 8):
        for tx in range(im.width // 8):
            if any(px[tx * 8 + x, ty * 8 + y] != V0 for y in range(8) for x in range(8)):
                continue
            for y in range(ty * 8, ty * 8 + 8):
                band = (y // 2) % 11 == 0
                for x in range(tx * 8, tx * 8 + 8):
                    if band and bayer(x, y, 0.22 + 0.2 * (((x // 32) + (y // 22)) % 2)):
                        px[x, y] = V1
            if r.u8() < 34 and 4 <= tx <= 14:
                x, y = tx * 8 + r.pick(7), ty * 8 + r.pick(7)
                px[x, y] = STAR if r.u8() < 110 else V2
                if r.u8() < 60:
                    px[x + 1, y] = V2


def aura(im, x, y, strength, x0=64, x1=95, y0=None, y1=None):
    """dithered glow round a played node (GLOW palette), clipped to tile columns 8-11"""
    px = im.load()
    cx, cy = x + 7.5, y + 7.5
    for Y in range(y if y0 is None else y0, (y + 16) if y1 is None else y1):
        for X in range(x0, x1 + 1):
            d = (((X - cx) / 1.2) ** 2 + (Y - cy) ** 2) ** 0.5
            if d > 14 or px[X, Y] != V0:
                continue
            t = min(1.0, (1 - d / 14) * 1.8) * strength
            if bayer(X, Y, t):  # nested ordered dither: the bright dots are a subset of the dim ones
                px[X, Y] = GHOT if bayer(X, Y, t * 0.45) else GDIM


def ghost(im, x, y, m, cols=(G1, G3, G2)):
    """an unpicked sibling: the same bust in the shared faded palette (ink > G1, skin > G3, cloth > G2)"""
    px = im.load()
    for yy in range(16):
        for xx in range(16):
            c = m[yy][xx]
            if c:
                px[x + xx, y + yy] = cols[c - 1]


def vline(im, x, y0, y1, col, dotted=False):
    for y in range(y0, y1 + 1):
        if not dotted or y % 2 == 0:
            im.putpixel((x, y), col)


def hline(im, x0, x1, y, col, dotted=False):
    for x in range(x0, x1 + 1):
        if not dotted or x % 2 == 0:
            im.putpixel((x, y), col)


def glitch_v(im, x, y0, y1, seed):
    """a glitch line: magenta, broken into sideways-displaced runs (mutation / glitch birth)"""
    r = genes.Rng(seed)
    y = y0
    while y <= y1:
        n = 1 + r.pick(3)
        dx = (r.pick(3) - 1) * 2
        for k in range(n):
            if y + k <= y1:
                im.putpixel((x + dx, y + k), MAGENTA)
        y += n


SPARK = ["..#.....", ".#.#....", "..#....."]


def build_tree(line, kids, view="FACES"):
    """the whole tall tree: founders node, the 24-heir ring (G7-G30) with siblings, the 3 kids (G31)"""
    tall = row_y(KIDS_ROW) + 24
    L = Layer(160, tall)
    im = L.im
    ring = line[FOUNDERS_TO:]
    nodes = []  # (row, x, kind, data)
    # ---- founders (row 0): G1's own bust, the five compressed elders as faded silhouettes either side
    y = row_y(0)
    fb = bust(line[0]["g"])
    sil = [[(1 if c else 0) for c in r] for r in fb]
    ghost(im, SPINE_X - 16, y, [r[::-1] for r in sil], cols=(G2,))
    ghost(im, SPINE_X + 16, y, sil, cols=(G2,))
    aura(im, SPINE_X, y, 0.55, x0=72, x1=87)
    nodes.append((0, SPINE_X, "founder", line[0]))
    # ---- the ring
    for r, n in enumerate(ring, 1):
        y = row_y(r)
        aura(im, SPINE_X, y, 0.5 + 0.5 * r / RING)
        nodes.append((r, SPINE_X, "played", n))
        for k, sx in enumerate(SIB_X):
            if k < len(n["sibs"]):
                ghost(im, sx, y, bust(n["sibs"][k]))
                nodes.append((r, sx, "sib", (n, k)))
    for k, kx in enumerate((SIB_X[0], SPINE_X, SIB_X[1])):
        nodes.append((KIDS_ROW, kx, "kid", kids[k]))
    # ---- connectors: the tile row under each parent
    for r in range(1, KIDS_ROW + 1):
        yc = row_y(r) - 8
        live = r == KIDS_ROW
        child = None if live else line[FOUNDERS_TO + r - 1]
        lx, rx = SIB_X[0] + 7, SIB_X[1] + 8
        hline(im, lx, 71, yc + 3, CYAN if live else LILAC, dotted=not live)  # bar to the left sibling
        hline(im, 88, rx, yc + 3, CYAN if live else LILAC, dotted=not live)
        hline(im, 72, 87, yc + 3, CYAN)  # the junction on the live branch
        for k, sx in enumerate((lx, rx)):
            if child and k < len(child["sibs"]) and child["sibs"][k]["flags"]["mutated"]:
                glitch_v(im, sx, yc + 4, yc + 7, r * 7 + k)
            else:
                vline(im, sx, yc + 4, yc + 7, CYAN if live else LILAC, dotted=not live)
        if child and child["g"]["flags"]["mutated"]:
            glitch_v(im, MID, yc, yc + 7, r * 3)  # a mutated heir: the spine breaks into a glitch line
            glitch_v(im, MID + 1, yc + 1, yc + 7, r * 5)
        else:
            vline(im, MID, yc, yc + 7, CYAN, dotted=live)
            vline(im, MID + 1, yc, yc + 7, CYAN, dotted=live)
            if not live:  # the glow runs down the spine between nodes
                aura(im, SPINE_X, yc - 4, 0.35 + 0.4 * r / RING, x0=72, x1=87, y0=yc, y1=yc + 8)
    # ---- labels (TEXT): generation on the left; floor + biome on the right in the FACES view
    for (r, x, kind, data) in nodes:
        if x != SPINE_X:
            continue
        y = row_y(r)
        if kind == "founder":
            text(im, 0, y, "G1", WHITE, V1)
            text(im, 0, y + 8, "-G6", PINK, V1)
        elif kind == "played":
            text(im, 0, y, f"G{data['gen']}", WHITE, V1)
        else:
            text(im, 0, y, f"G{NEXT}", PINK, V1)
            text(im, 0, y + 8, "NEXT", PINK, V1)
        if view == "FACES" and kind != "kid":
            if kind == "founder":
                text(im, 120, y, "FIRST", PINK, V1)
                text(im, 120, y + 8, "6 GEN", WHITE, V1)
            else:
                text(im, 128, y, f"F{data['floor']}", WHITE, V1)
                text(im, 128, y + 8, data["biome"][:3], PINK, V1)
    if view != "FACES":
        view_columns(im, line, view)
    void_tiles(im)
    # ---- sprites: played heirs (own palettes), kids, sign glyphs, glitch sparkles
    for (r, x, kind, data) in nodes:
        y = row_y(r)
        if kind in ("founder", "played"):
            L.sprite16(x, y, bust(data["g"]), obj_pal(data["g"]), f"G{data['gen']}")
            if view == "FACES" and kind == "played":
                L.sprite_bits(152, y, SIGN[sun_sign(data["born"])], PINK, OBJ_CURSOR)
            if data["g"]["flags"]["mutated"]:
                L.sprite_bits(x + 14, y - 4, SPARK, {"#": WHITE}, OBJ_SPARK)
        elif kind == "kid":
            L.sprite16(x, y, bust(data), obj_pal(data), f"KID@{x}")
        else:
            n, k = data
            if n["sibs"][k]["flags"]["mutated"]:
                L.sprite_bits(x + 14, y - 4, SPARK, {"#": PINK}, OBJ_SPARK)
    return L, nodes


def lane_rows(line):
    return [line[0]] + line[FOUNDERS_TO:]


LOOK_LANES = [("HEAVY", AMBER), ("BLINK", ICE), ("HOT", EMBER), ("GLITCHED", MAGENTA)]
TRUTH_COLS = [PEACH, PINK, MINT, LILAC]  # CAUSE, CULPRIT, REVERSIBLE, SELF (never labelled on screen)


def view_columns(im, line, view):
    """SELECT views draw into the right-hand 4 tile columns (x 128-159), one lane per tile column"""
    px = im.load()
    for r, n in enumerate(lane_rows(line)):
        y0, y1 = row_y(r) - (8 if r else 0), row_y(r) + 15
        g, p = n["g"], genes.express(n["g"])
        if view == "LOOKS":  # solid = expressed, dithered = carried, dotted guide = absent
            for li, (name, col) in enumerate(LOOK_LANES):
                x0 = 128 + li * 8 + 2
                if name == "BLINK":
                    e, c = p["dodge"] == "BLINK", 2 in g["dodge"]
                elif name == "HOT":
                    e, c = p["aff"].get("HOT", 0) >= 2, g["aff"][0] >= 1
                else:
                    e, c = name in p["traits"], sum(g["traits"][name]) >= 1
                for y in range(y0, y1 + 1):
                    for x in range(x0, x0 + 4):
                        if e or (c and bayer(x, y, 0.3)):
                            px[x, y] = col
                    if not e and not c and y % 4 == 0:
                        px[x0 + 1, y] = col
        elif view == "STATS":
            y = row_y(r)
            for k in range(n["floor"]):  # floor reached, 8 steps
                for yy in range(y + 2, y + 6):
                    for xx in range(128 + k * 4, 131 + k * 4):
                        px[xx, yy] = PINK if n["floor"] == 8 else (GREEN if k < 4 else YEL)
            text(im, 128, y + 8, f"{n['kills']}", WHITE, V1)
        elif view == "TRUTH":  # four leaning threads; position = lean, never a label
            for a in range(4):
                lean = max(-3, min(3, n["truth"][a] // 2))
                for y in range(y0, y1 + 1):
                    wob = ((y * 3 + a * 5 + r) % 7 == 0) - ((y * 5 + a) % 9 == 0)
                    xx = max(128 + a * 8, min(128 + a * 8 + 7, 128 + a * 8 + 3 + lean + wob))
                    if (y + a) % 3:
                        px[xx, y] = TRUTH_COLS[a]


# ------------------------------------------------------------------------------------------------ the screen
def cursor(L, x, y):
    lb = ["##......", "#.......", "#.......", "........", "........", "........", "........", "........",
          "........", "........", "........", "........", "........", "#.......", "#.......", "##......"]
    L.sprite_bits(x - 3, y, lb, {"#": YEL}, OBJ_CURSOR)
    L.sprite_bits(x + 11, y, [r[::-1] for r in lb], {"#": YEL}, OBJ_CURSOR)


def records(line):
    rows = lane_rows(line)
    deep = max(rows, key=lambda n: (n["floor"], -n["gen"]))
    longest = max(rows, key=lambda n: n["died"] - n["born"])
    days = int((longest["died"] - longest["born"]).total_seconds() // 86400)
    return f"DEEP F{deep['floor']} G{deep['gen']}", f"LONG {longest['name']} {days}D"


def top_bar(view, line):
    """window layer, 2 tile rows: UI-A / UI-B text on the panel, a cyan rim; lane heads on the void in BG7"""
    im = Image.new("RGB", (W, 16), PANEL)
    lanes = view in ("LOOKS", "TRUTH")
    if view == "FACES":
        text(im, 0, 0, "THE LINE", WHITE)
        text(im, 104, 0, f"G1-G{NEXT}", CYAN)
        text(im, 0, 8, "FACES", PINK)
        text(im, 56, 8, "SEL:LOOKS", CYAN)
    elif view == "LOOKS":
        text(im, 0, 0, "LOOKS", PINK)
        text(im, 56, 0, "DRIFT", WHITE)
        text(im, 0, 8, "HVY BLNK HOT GLT", WHITE)
    elif view == "STATS":
        a, b = records(line)
        text(im, 0, 0, "STATS", PINK)
        text(im, 56, 0, a, WHITE)
        text(im, 0, 8, b, CYAN)
    else:
        text(im, 0, 0, "TRUTH", PINK)
        text(im, 56, 0, "IT LEANED", WHITE)
        text(im, 0, 8, "NOT A FACT", CYAN)
    px = im.load()
    for x in range(160):
        if lanes and x >= 128:
            continue
        if px[x, 15] == PANEL:
            px[x, 15] = CYAN
    if lanes:  # lane heads: the lane colours on the void, directly above their lanes
        cols = [c for _, c in LOOK_LANES] if view == "LOOKS" else TRUTH_COLS
        for li, col in enumerate(cols):
            for y in range(8, 16):
                for x in range(128 + li * 8, 136 + li * 8):
                    px[x, y] = V0
            for y in range(10, 16):
                for x in range(130 + li * 8, 134 + li * 8):
                    px[x, y] = col
        for x in range(128, 160):
            for y in range(0, 8):
                if px[x, y] == PANEL and y == 7:
                    px[x, y] = CYAN
    return im


WHISPER = ["WAS IT MADE?", "WAS IT YOU?", "CAN IT CLOSE?", "SAME HANDS?"]


def node_caption(n, view="FACES"):
    r, x, kind, data = n
    if view in ("STATS", "TRUTH") and kind == "played":
        if view == "STATS":
            hrs = int((data["died"] - data["born"]).total_seconds() // 3600)
            l2 = [(f"KILLS {data['kills']} ", CYAN), (f"LIVED {hrs // 24}D{hrs % 24}H", WHITE)]
        else:
            ax = max(range(4), key=lambda a: abs(data["truth"][a]))
            l2 = [(WHISPER[ax], CYAN)]
        return [(data["name"] + " ", PINK), (f"G{data['gen']} F{data['floor']}", WHITE)], l2
    if kind == "founder":
        return [("FOUNDERS ", PINK), ("G1-G6", WHITE)], [("A:THE ROOT", CYAN)]
    if kind == "played":
        return [(data["name"] + " ", PINK), (f"G{data['gen']} F{data['floor']}", WHITE)], [("A:LOOK  ", CYAN), ("LR:KIN", WHITE)]
    if kind == "sib":
        par, k = data
        return [(par["sibnames"][k] + " ", WHITE), ("NOT TAKEN", PINK)], [("A:LOOK  ", CYAN), ("LR:KIN", WHITE)]
    return [("WAITING ", PINK), (f"G{NEXT}", WHITE)], [("A:PICK  ", CYAN), ("B:BACK", WHITE)]


def bottom_bar(sel_node, view="FACES"):
    """window layer, 3 tile rows: a pink-free cyan rim row, then the selected node's caption"""
    im = Image.new("RGB", (W, 24), PANEL)
    px = im.load()
    for x in range(160):
        px[x, 0] = CYAN
        for y in range(1, 8):
            if bayer(x, y, 0.75 - y * 0.1):
                px[x, y] = INK
    l1, l2 = node_caption(sel_node, view) if sel_node else ([("", WHITE)], [("", WHITE)])
    for row, segs in ((8, l1), (16, l2)):
        x = 0
        for s, col in segs:
            text(im, x, row, s, col)
            x += 8 * len(s)
    return im


def tree_screen(tall, line, scroll, sel, view="FACES", glow_phase=0):
    """sel: (row, x); scroll: window top in tall-tree pixels"""
    T, nodes = tall
    bg = Image.new("RGB", (W, H), V0)
    win = T.im.crop((0, scroll, 160, scroll + WIN_H))
    if glow_phase:  # the glow's bright entry is palette-cycled (one CGB palette write per step)
        cyc = [GHOT, LILAC, CYAN, LILAC][glow_phase % 4]
        win = win.copy()
        p = win.load()
        for y in range(WIN_H):
            for x in range(64, 96):  # GLOW tiles only live in columns 8-11 (TEXT's pink is another palette)
                if p[x, y] == GHOT:
                    p[x, y] = cyc
    bg.paste(win, (0, WIN_Y))
    sel_node = next((n for n in nodes if n[0] == sel[0] and n[1] == sel[1]), None)
    bg.paste(top_bar(view, line), (0, 0))
    bg.paste(bottom_bar(sel_node, view), (0, WIN_Y + WIN_H))
    C = Layer(W, H)
    C.im = bg
    for (x, y, t, pal) in T.sprites:  # OBJ hidden under the bars (the cartridge drops them from OAM)
        sy = y - scroll + WIN_Y
        if sy > WIN_Y - 16 and sy < WIN_Y + WIN_H:
            C.sprites.append((x, sy, t, pal))
    if sel_node:
        cursor(C, sel[1], row_y(sel[0]) - scroll + WIN_Y)
    return C


def compose(L):
    im = L.im.copy()
    for (x, y, t, pal) in L.sprites:
        top = max(0, WIN_Y - y) if L.im.height == H and pal is not None and y < WIN_Y else 0
        bot = 16
        if L.im.height == H and y + 16 > WIN_Y + WIN_H:
            bot = max(0, WIN_Y + WIN_H - y)
        if L.im.height == H and y < WIN_Y:
            top = WIN_Y - y
        if bot > top:
            part = t.crop((0, top, 8, bot))
            im.paste(part, (x, y + top), part)
    return im


# ------------------------------------------------------------------------------------------------ CGB checks
def check(L, bgpals, name):
    """every BG tile fits one BG palette; every sprite fits its OBJ palette; sprites per line; palettes on screen"""
    im = L.im
    px = im.load()
    bad, used_bg, tiles = [], set(), set()
    for ty in range(im.height // 8):
        for tx in range(im.width // 8):
            cs = {px[tx * 8 + x, ty * 8 + y] for y in range(8) for x in range(8)}
            fit = [k for k, (_, pal) in bgpals.items() if cs <= set(pal)]
            if not fit:
                bad.append((tx, ty, sorted(cs)))
                continue
            used_bg.add(fit[0])
            pal = bgpals[fit[0]][1]
            tiles.add(tuple(pal.index(px[tx * 8 + x, ty * 8 + y]) for y in range(8) for x in range(8)))
    sp_bad, per_line, objpals, obj_tiles = [], {}, set(), set()
    for (x, y, t, (pname, pal)) in L.sprites:
        cs = {t.getpixel((a, b))[:3] for b in range(16) for a in range(8) if t.getpixel((a, b))[3]}
        if not cs <= set(pal):
            sp_bad.append(pname)
        objpals.add(tuple(pal))
        obj_tiles.add(tuple((pal.index(t.getpixel((a, b))[:3]) + 1) if t.getpixel((a, b))[3] else 0
                            for b in range(16) for a in range(8)))
        for yy in range(max(0, y), min(im.height, y + 16)):
            per_line[yy] = per_line.get(yy, 0) + 1
    rep = {"screen": name, "bg_tile_violations": len(bad), "bg_palettes_used": len(used_bg),
           "unique_bg_tiles": len(tiles), "obj_palettes_used": len(objpals), "obj_violations": len(sp_bad),
           "sprites": len(L.sprites), "max_sprites_per_line": max(per_line.values()) if per_line else 0,
           "unique_obj_8x16": len(obj_tiles)}
    if bad:
        rep["bad"] = bad[:4]
    return rep


# ------------------------------------------------------------------------------------------------ node detail
SHORT = {"COLOURBLIND": "CBLND", "INSOMNIAC": "NOSLP", "REMEMBERS": "MEMRY", "GLITCHED": "GLTCH", "ALLERGIC": "ALRGY"}
BLINK_ICON = ["........", ".#...#..", "..#.#...", "...#....", "..#.#...", ".#...#..", "........", "........"]


def detail(n):
    """A on a node: full portrait, name, birth + sun sign (the chart entry), traits, carried recessives, where and how
    they died, the epitaph, best feat, faction liked/hated, and how often they came back as a ghost or body"""
    g = n["g"]
    p = genes.express(g)
    fh, fpal = AP.faces(["genome", genes.face_genome(g)])[0]
    face = AP.face_image(fh, fpal)
    fp = [AP.rgb(c) for c in fpal]
    LIL = (214, 190, 246)
    pals = {
        0: ("UI-A", [INK, PANEL, CYAN, WHITE]),
        1: ("UI-B", [INK, PANEL, CYAN, PINK]),
        2: ("PORTRAIT", [CARD, fp[1], fp[2], fp[3]]),
        3: ("CHIPS", [INK, PANEL, CYAN, YEL]),
        4: ("FAINT", [INK, PANEL, G2, G3]),
        5: ("FACTION", [INK, PANEL, GREEN, RED]),
        6: ("SIGN", [INK, PANEL, PINK, CARD]),
        7: ("WORDS", [INK, PANEL, MINT, LIL]),
    }
    im = Image.new("RGB", (W, H), PANEL)
    px = im.load()
    for y in range(H):
        for x in range(W):
            if bayer(x, y, max(0.0, (y - 60) / 150)):
                px[x, y] = INK
    ImageDraw.Draw(im).rectangle((8, 8, 55, 55), fill=CARD)
    im.paste(face, (8, 8), face)
    born = n["born"]
    sign = sun_sign(born)
    text(im, 64, 8, n["name"], PINK)
    text(im, 128, 8, f"G{n['gen']}", WHITE)
    text(im, 64, 16, born.strftime("%d %b %y").upper(), WHITE)
    text(im, 64, 24, born.strftime("%H:%M"), WHITE)
    text(im, 64, 32, sign[:7], PINK)
    hrs = int((n["died"] - born).total_seconds() // 3600)
    text(im, 64, 40, f"LIVED {hrs // 24}D {hrs % 24}H" if hrs >= 24 else f"LIVED {hrs}H", LIL)
    text(im, 64, 48, "SEL:CHART", CYAN)
    ImageDraw.Draw(im).rectangle((136, 24, 151, 39), fill=CARD)  # the sign glyph = the chart's door
    bits(im, 140, 28, SIGN[sign], PINK)
    text(im, 0, 56, f"SEEN AS GHOST {n['ghosts']}X" if n["ghosts"] else "NEVER CAME BACK", LIL)
    x = 0
    for s, col in [(p["dodge"], CYAN)] + [(SHORT.get(t, t)[:5], YEL) for t in p["traits"]]:
        w = len(s) * 8 + 8
        ImageDraw.Draw(im).rectangle((x + 1, 64, x + w - 2, 71), fill=col)
        for i, ch in enumerate(s):
            text(im, x + 4 + i * 8, 64, ch, INK, None)
        x += w
    text(im, 0, 72, "CARRIES", G3)
    carried = sorted(t for t in genes.RECESSIVE if sum(g["traits"][t]) == 1)
    if 2 in g["dodge"] and p["dodge"] != "BLINK":
        carried.append("BLINK")
    for i, t in enumerate(carried[:4]):
        icon = BLINK_ICON if t == "BLINK" else ICON[t]
        for yy, r in enumerate(icon):
            for xx, c in enumerate(r):
                if c != ".":
                    px[64 + i * 16 + xx, 72 + yy] = G3 if (xx + yy) % 2 else G2
    bits(im, 0, 80, ICON["HEART"], GREEN)
    text(im, 8, 80, n["liked"], GREEN)
    bits(im, 80, 80, ICON["CRACK"], RED)
    text(im, 88, 80, n["hated"], RED)
    text(im, 0, 88, f"DIED F{n['floor']} {n['biome']}", WHITE)
    for i, ln in enumerate(n["cause"]):  # the card's own cause lines, from the node's 3 cause bytes
        text(im, 0, 96 + i * 8, ln, PINK)
    text(im, 0, 112, f"'{n['epitaph']}'", MINT)  # the card's epitaph line, from the node's epitaph id
    ic, fs, _ = n["feat"] or ("CROWN", "FIRST BOSS", "BOSS")
    bits(im, 0, 120, ICON[ic], YEL)
    text(im, 8, 120, fs, YEL)
    ImageDraw.Draw(im).rectangle((0, 128, 159, 143), fill=PANEL)
    for x in range(160):
        px[x, 128] = CYAN
        if bayer(x, 129, 0.5):
            px[x, 129] = INK
    text(im, 0, 136, "B:TREE", WHITE)
    text(im, 64, 136, "UP/DN:GEN", CYAN)
    L = Layer(W, H)
    L.im = im
    return L, pals


# ------------------------------------------------------------------------------------------------ the save (9.5 / 9.6)
NODE = [  # (offset, size, field): one ring heir
    (0, 14, "genome"), (14, 1, "name id"), (15, 1, "death floor + stage"), (16, 1, "biome + stage type + rift"),
    (17, 3, "cause: verb, killer, twist"), (20, 1, "epitaph id"), (21, 1, "factions"), (22, 5, "birth stamp"),
    (27, 2, "truth lean"), (29, 1, "kills"), (30, 1, "rooms"), (31, 1, "world ties"), (32, 12, "two siblings"),
    (44, 3, "sky"), (47, 2, "best feat"),
]
NODE_BYTES = sum(n for _, n, _ in NODE)
FOUNDERS_BYTES = 32 + 2 + 16  # G1's node bytes 0..31 + its best feat (47..48) + the elders' aggregates
KIDS_BYTES, HEADER_BYTES, CRC_BYTES = 3 * 14, 6, 2


def block_bytes(ring):
    return HEADER_BYTES + ring * NODE_BYTES + FOUNDERS_BYTES + KIDS_BYTES + CRC_BYTES


SRAM = [  # (start, size, content); the whole 8 KiB bank, 0xA000..0xBFFF
    (0xA000, 32, "meta record A"), (0xA020, 32, "meta record B"),
    (0xA040, 256, "mid-run continue A"), (0xA140, 256, "mid-run continue B"),
    (0xA240, 128, "sky event scenes (section 19)"), (0xA2C0, 64, "line progress A (3.6.10)"),
    (0xA300, 64, "line progress B (3.6.10)"), (0xA340, 112, "free"),
    (0xA3B0, 64, "inheritance"), (0xA3F0, 128, "seen history"), (0xA470, 256, "book"),
    (0xA570, block_bytes(RING), "tree copy A"), (0xA570 + block_bytes(RING), block_bytes(RING), "tree copy B"),
    (0xA570 + 2 * block_bytes(RING), 0xB4C0 - 0xA570 - 2 * block_bytes(RING), "free"),
    (0xB4C0, 32, "kept clock A/B (9.7.1)"), (0xB4E0, 16, "host clock block (9.7.1)"), (0xB4F0, 16, "RTC block (16b)"),
    (0xB500, 576, "face generator base layer"), (0xB740, 0xBC00 - 0xB740, "free"),
    (0xBC00, 384, "STATS record A (9.8)"), (0xBD80, 384, "STATS record B (9.8)"), (0xBF00, 255, "free"),
    (0xBFFF, 1, "RTC access byte (16b)"),
]


def save_report():
    """the tree record and the SRAM map, checked: contiguous, inside 8 KiB, both copies fit"""
    assert [o for o, _, _ in NODE] == [sum(n for _, n, _ in NODE[:i]) for i in range(len(NODE))]
    at = 0xA000
    for start, size, _ in SRAM:
        assert start == at and size >= 0, (hex(start), hex(at))
        at += size
    assert at == 0xC000
    room = 0xB4C0 - 0xA570  # the tree's span, between the stats and the kept clock
    largest = max(n for n in range(1, 64) if 2 * block_bytes(n) <= room)
    return {"screen": "save", "ring": RING, "node_bytes": NODE_BYTES, "founders_bytes": FOUNDERS_BYTES,
            "block_bytes": block_bytes(RING), "both_copies": 2 * block_bytes(RING),
            "free_bytes": sum(sz for _, sz, c in SRAM if c == "free"),
            "largest_ring_that_fits": largest,
            "map": [f"{s:04X}-{s + sz - 1:04X} {sz:5d} {c}" for s, sz, c in SRAM]}


# ------------------------------------------------------------------------------------------------ output
def save(im, name):
    im.save(OUT / f"{name}_1x.png")
    im.resize((im.width * 4, im.height * 4), Image.NEAREST).save(OUT / f"{name}.png")


def main():
    import json
    OUT.mkdir(parents=True, exist_ok=True)
    line, kids, _ = build_line()
    for i, n in enumerate(line):
        n["kills"] = 20 + (sum(n["g"]["stats"]) * 7 + i * 5) % 70
    talls = {v: build_tree(line, kids, v) for v in ("FACES", "LOOKS", "STATS", "TRUTH")}
    tall_h = talls["FACES"][0].im.height
    bottom = tall_h - WIN_H
    reports, shots = [], {}
    for v, (T, _) in talls.items():
        pals = dict(BG_TREE)
        pals[7] = VIEW_PAL[v]
        reports.append(check(T, pals, f"whole tree, {v} (tall BG)"))
    cfg = [
        ("tree_faces", "FACES", bottom, (KIDS_ROW, SPINE_X)),
        ("tree_founders", "FACES", 0, (0, SPINE_X)),
        ("tree_sibling", "FACES", row_y(9) - 32, (10, SIB_X[1])),
        ("tree_looks", "LOOKS", row_y(1) - 8, (3, SPINE_X)),
        ("tree_stats", "STATS", row_y(5), (7, SPINE_X)),
        ("tree_truth", "TRUTH", row_y(1), (2, SPINE_X)),
    ]
    for name, v, sc, sel in cfg:
        S = tree_screen(talls[v], line, sc, sel, v)
        pals = dict(BG_TREE)
        pals[7] = VIEW_PAL[v]
        reports.append(check(S, pals, name))
        shots[name] = compose(S)
        save(shots[name], name)
    focus = next(n for n in reversed(line) if n["feat"] and len(genes.express(n["g"])["traits"]) >= 1)
    D, dp = detail(focus)
    reports.append(check(D, dp, "tree_detail"))
    shots["tree_detail"] = compose(D)
    save(shots["tree_detail"], "tree_detail")
    T, _ = talls["FACES"]  # the unrolled tree, whole
    strip = T.im.copy()
    for (x, y, t, _) in T.sprites:
        strip.paste(t, (x, y), t)
    strip.save(OUT / "tree_strip_1x.png")
    strip.resize((strip.width * 2, strip.height * 2), Image.NEAREST).save(OUT / "tree_strip.png")
    frames, n = [], 90
    for i in range(n + 20):  # founders -> the live end, eased; the cursor walks the spine; the glow cycles
        t = min(1.0, i / n)
        sc = int(round(t * t * (3 - 2 * t) * bottom))
        row = max(0, min(KIDS_ROW, round((sc + WIN_H // 2 - TOP - 8) / PITCH)))
        S = tree_screen(talls["FACES"], line, sc, (row, SPINE_X), "FACES", glow_phase=(i // 8) % 4)
        frames.append(compose(S).resize((W * 3, H * 3), Image.NEAREST))
    frames[0].save(OUT / "tree_scroll.gif", save_all=True, append_images=frames[1:], duration=40, loop=0)
    order = ["tree_founders", "tree_sibling", "tree_faces", "tree_looks", "tree_stats", "tree_truth", "tree_detail"]
    sheet = Image.new("RGB", (8 + 640 + 8 + 640 + 8, 8 + 144 + 12 + 576 + 8), (10, 8, 24))
    for i, k in enumerate(order):
        sheet.paste(shots[k], (8 + i * 184, 8))
    sheet.paste(shots["tree_faces"].resize((640, 576), Image.NEAREST), (8, 164))
    sheet.paste(shots["tree_detail"].resize((640, 576), Image.NEAREST), (656, 164))
    sheet.save(OUT / "tree_contact.png")
    reports.append(save_report())
    (OUT / "tree_report.json").write_text(json.dumps(reports, indent=1, default=str))
    for r in reports:
        print(r)


if __name__ == "__main__":
    main()
