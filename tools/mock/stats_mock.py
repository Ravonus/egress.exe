"""STATS screen mock (design only, not game code). See docs/redesign.md 9.8.

Draws two STATS pages (RUNS and COMBAT) the way the cartridge would hold them, from a packed STATS record
(stats_save.py: pack -> unpack, so every number on screen came through the real byte layout), and checks them
against the CGB limits:
  * every 8x8 BG tile fits one of 8 BG palettes x 4 colours; sprites fit an OBJ palette (3 colours + clear);
  * <= 10 sprites per line, <= 8 palettes of each kind; unique BG tiles are counted for the VRAM budget.
The big numbers are a VCR clock face: 7-segment digits, 8x16 (two tiles each), cyan over pink.

    .venv/bin/python tools/mock/stats_mock.py     # docs/shots/mock_stats.png, stats_mock_report.json

The in-game toast strip draws over a real B1 capture (docs/shots/b1_look_floor2.png) and uses the game's own
toast: hud_msg raises the window 8 px for one text row (src/hud.c), plus a star OBJ from the particle palette.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from egkit import FONT, ROOT  # noqa: E402
import stats_save as SS  # noqa: E402

OUT = ROOT / "docs/shots"
W, H = 160, 144

# ------------------------------------------------------------------------------------------------ colours
V0, V1, V2, STAR = (14, 10, 34), (34, 20, 70), (70, 40, 120), (236, 200, 255)
CARD, INK = (40, 24, 82), (20, 14, 46)
CYAN, PINK, WHITE, YEL = (64, 224, 248), (255, 112, 200), (250, 246, 255), (255, 224, 96)
LILAC, MINT, CORAL = (178, 150, 230), (120, 248, 196), (255, 136, 112)

BG = {
    0: ("VOID", [V0, V1, V2, STAR]),        # the background, the flavour line, the controls
    1: ("FRAME", [V0, CARD, CYAN, PINK]),   # card rims: cyan over pink (the synthwave edge), rounded corners
    2: ("LABEL", [CARD, INK, LILAC, WHITE]),  # small labels and sub-values on the cards
    3: ("DIGIT", [CARD, INK, CYAN, PINK]),  # the big numbers: cyan over pink, ink shadow
    4: ("ICON-A", [CARD, INK, YEL, WHITE]),  # tokens, stars, crowns, the NEW tag
    5: ("ICON-B", [CARD, INK, PINK, MINT]),  # skulls, hearts, family bars
    6: ("NEW", [CARD, INK, CYAN, PINK]),    # a NEW record's digits: this palette blinks (one write per toggle)
    7: ("HEAD", [V1, INK, PINK, CYAN]),     # the header bar
}
NEW_BLINK = [CARD, INK, YEL, WHITE]  # palette 6's other state
OBJ_ARROW = ("OBJ0 ARROWS", [WHITE, CYAN, PINK])
OBJ_SPARK = ("OBJ1 SPARKLE", [WHITE, YEL, PINK])

# ------------------------------------------------------------------------------------------------ the record
BIOMES = ["OFFICE", "VENTS", "ATRIUM", "SERVERS", "FOOD CT", "GARDEN", "ROOFTOP", "CORE"]
NAMES = {17: "ISA", 40: "CORA", 52: "PIM"}


def sample_record():
    """a believable save after ~90 hours: 347 runs over 3 lines. Every value goes through pack/unpack."""
    r = SS.blank()
    L = r["life"]
    L.update(runs=347, deaths=312, egresses=35, deep=47 | (6 << 6), deep_gen=19, heirs=296, lines=3,
             long_life=4 * 1440 + 7 * 60 + 12, long_name=17, long_line=141, play_s=88 * 3600 + 12 * 60 + 40,
             fast_biome=252, fast_biome_id=0, fast_egress=58 * 60 + 9,
             kills=12408, elites=388, dmg_out=88112, dmg_in=2210, parries=1204, lunges=3511, k_crush=310,
             k_void=122, k_react=905, nohit=47, solved=1630, forgets=212, glass=877, collapsed=40125, pushed=9050,
             outwit=301, mixes=6240, steps=1902211, dozes=58, crt_self=9, self_deaths=41, falls=77)
    L["kills_fam"] = [2410, 1980, 1702, 1655, 1490, 1410, 1102, 659]
    L["boss_n"] = [31, 22, 17, 12, 9, 6, 0, 0]
    L["boss_gen"] = [4, 9, 15, 22, 40, 77, 0, 0]
    r["seq"] = 212
    return SS.unpack(SS.pack(r))


# ------------------------------------------------------------------------------------------------ drawing
def text(im, x, y, s, col, shadow=INK):
    """the M2 font: 4x7 glyphs plus a 1 px drop shadow, so a tile-aligned glyph stays in one tile"""
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


# 7-segment VCR digits, 7 x 14 plus the shadow. Segments: a top, b upper right, c lower right, d bottom, e lower
# left, f upper left, g middle.
SEG = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc", "7": "abc",
       "8": "abcdefg", "9": "abcdfg", "F": "aefg", "-": "g"}


def seg_mask(ch):
    m = [[0] * 7 for _ in range(14)]
    def hseg(r):
        for y in (r, r + 1):
            for x in range(1 + (y != r), 6 - (y != r)):
                m[y][x] = 1
    def vseg(c, r0, r1):
        for y in range(r0, r1 + 1):
            for x in (c, c + 1):
                m[y][x] = 1
    if ch == ":":
        for (x, y) in ((3, 4), (3, 9)):
            for dy in (0, 1):
                m[y + dy][x] = m[y + dy][x - 1] = 1
        return m
    if ch == ".":
        for y in (12, 13):
            m[y][2] = m[y][3] = 1
        return m
    if ch == "1":  # centred, with a small flag, so 12408 and 3511 don't read as two numbers
        vseg(3, 0, 13)
        m[1][2] = m[2][1] = m[1][1] = 1
        return m
    s = SEG.get(ch, "")
    if "a" in s: hseg(0)
    if "g" in s: hseg(6)
    if "d" in s: hseg(12)
    if "f" in s: vseg(0, 1, 6)
    if "b" in s: vseg(5, 1, 6)
    if "e" in s: vseg(0, 7, 12)
    if "c" in s: vseg(5, 7, 12)
    return m


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def big(im, x, y, s, pal=BG[3][1]):
    """big digits, tile-aligned (8x16 each): rows 0-5 colour 2 (cyan), a 3-row dithered band, then colour 3"""
    px = im.load()
    for ch in s:
        m = seg_mask(ch)
        for yy in range(14):
            for xx in range(7):
                if m[yy][xx]:
                    t = 0 if yy < 5 else 1 if yy > 7 else (yy - 4) / 4
                    hi = BAYER[yy & 3][xx & 3] < t * 16
                    sx, sy = x + xx + 1, y + yy + 1
                    if px[sx, sy] == pal[0]:
                        px[sx, sy] = pal[1]
                    px[x + xx, y + yy] = pal[3] if hi else pal[2]
        # the shadow must not paint over lit pixels of this glyph: redo the glyph on top
        for yy in range(14):
            for xx in range(7):
                if m[yy][xx]:
                    t = 0 if yy < 5 else 1 if yy > 7 else (yy - 4) / 4
                    px[x + xx, y + yy] = pal[3] if BAYER[yy & 3][xx & 3] < t * 16 else pal[2]
        x += 8


ICON = {  # 8x8: '1' ink, '2' colour 2, '3' colour 3 of the tile's palette
    "DOOR": ["..1111..", ".122221.", ".123321.", ".122221.", ".122231.", ".122221.", ".122221.", "11111111"],
    "SKULL": ["..1111..", ".133331.", "13133131", "13133131", "13333331", ".133131.", ".131311.", "..1111.."],
    "EXIT": ["...11...", "..1331..", ".133331.", "11133111", "..1331..", "..1331..", "..1221..", "..1111.."],
    "DEEP": ["..1111..", "..1221..", "..1221..", "11122111", ".122221.", "..1221..", "...11...", "........"],
    "HEIR": ["..1111..", ".133331.", ".131131.", ".133331.", "..1331..", ".122221.", "12222221", "11111111"],
    "CLOCK": ["..1111..", ".133331.", "13313331", "13312231", "13333331", ".133331.", "..1111..", "........"],
    "SWORD": [".....111", "....1331", "...1331.", "1.1331..", "11331...", ".121....", "1211....", "11......"],
    "SHIELD": ["11111111", "13333331", "13322331", "13322331", "13333331", ".133331.", "..1331..", "...11..."],
    "PARRY": ["...1....", "..131...", "1113111.", "13333331", "1113111.", "..131...", "...1....", "........"],
    "LUNGE": ["........", "1.......", "131..1..", "1331.31.", "13333331", "1331.31.", "131..1..", "1......."],
    "CLAW": ["1..1..1.", "21.21.21", "21.21.21", "12112112", ".1221221", "..122221", "...11111", "........"],
    "CROWN": ["........", "2..2..2.", "22.2.22.", "2222222.", "2323232.", "2222222.", "1111111.", "........"],
    "CROWN0": ["........", "1..1..1.", "11.1.11.", "1.1.1.1.", "1.....1.", "1.....1.", "1111111.", "........"],
    "STAR": ["...11...", "...31...", "11333311", "13333331", ".133331.", ".131131.", "11.11.11", "........"],
    "HOUR": ["11111111", ".133331.", "..1331..", "...11...", "..1221..", ".122221.", "11111111", "........"],
}


def icon(im, x, y, name, pal):
    for yy, r in enumerate(ICON[name]):
        for xx, c in enumerate(r):
            if c != ".":
                im.putpixel((x + xx, y + yy), pal[int(c)])


class Screen:
    def __init__(self):
        self.im = Image.new("RGB", (W, H), V0)
        self.sprites = []  # (x, y, 8x16 RGBA, (name, palette))

    def sprite(self, x, y, rows, pal):
        t = Image.new("RGBA", (8, 16), (0, 0, 0, 0))
        for yy, r in enumerate(rows):
            for xx, c in enumerate(r):
                if c != ".":
                    t.putpixel((xx, yy), (*pal[1][int(c) - 1], 255))
        self.sprites.append((x, y, t, pal))

    def compose(self):
        im = self.im.copy()
        for (x, y, t, _) in self.sprites:
            im.paste(t, (x, y), t)
        return im


ARROW_L = ["...1....", "..12....", ".1222...", "12222...", ".1222...", "..12....", "...1...."]
ARROW_R = ["....1...", "....21..", "...2221.", "...22221", "...2221.", "....21..", "....1..."]
SPARK = ["...1....", "...2....", ".12321..", "...2....", "...1....", "........", "......1.", ".....121", "......1."]
SPARK2 = ["........", "..1.....", ".121....", "..1.....", "........", "...2....", "..232...", "...2...."]


def void_bg(im, rows):
    """sparse stars on the void (VOID palette only) in the given tile rows"""
    px = im.load()
    for ty in rows:
        for tx in range(20):
            h = (tx * 73 + ty * 151) % 17
            if h < 3:
                px[tx * 8 + (h * 3) % 7, ty * 8 + (h * 5) % 7] = STAR if h == 0 else V2


def rim(im, x0, x1, ty):
    """a card's top edge: the bottom 2 px of tile row ty (FRAME palette), cyan over pink, corners cut"""
    d = ImageDraw.Draw(im)
    y = ty * 8 + 6
    d.line((x0 + 2, y, x1 - 2, y), fill=CYAN)
    d.line((x0 + 1, y + 1, x1 - 1, y + 1), fill=PINK)


def card(im, tx0, tx1, ty0, ty1):
    d = ImageDraw.Draw(im)
    x0, x1 = tx0 * 8, tx1 * 8 + 7
    rim(im, x0, x1, ty0 - 1)
    d.rectangle((x0, ty0 * 8, x1, ty1 * 8 + 7), fill=CARD)
    d.line((x0, ty0 * 8, x0, ty1 * 8 + 7), fill=INK)
    d.line((x1, ty0 * 8, x1, ty1 * 8 + 7), fill=INK)


PAGES = ["RUNS", "COMBAT", "PUZZLES", "BENCH", "TREASURE", "LINEAGE", "SKY", "ODDITIES"]
PAGE_ICON = {"RUNS": "DOOR", "COMBAT": "SWORD"}
FLAVOUR = {"RUNS": "SO MANY DOORS.", "COMBAT": "THEY STARTED IT.", "PUZZLES": "THE ROOM REMEMBERS.",
           "BENCH": "STEAM, AGAIN?", "TREASURE": "FINDERS, KEEPERS.", "LINEAGE": "SAME HANDS?",
           "SKY": "THE SKY WATCHED.", "ODDITIES": "YOU WERE HERE."}


def header(S, page, bounce=0):
    im = S.im
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 159, 7), fill=V1)
    hp = BG[7][1]
    icon(im, 12, 0, PAGE_ICON[page], [None, INK, PINK, CYAN])
    text(im, 24, 0, page, PINK)
    i = PAGES.index(page)
    for k in range(8):  # page pips: the current one a cyan dash, the rest pink dots
        x = 96 + k * 7
        if k == i:
            d.line((x, 3, x + 4, 3), fill=CYAN)
            d.line((x, 4, x + 4, 4), fill=CYAN)
        else:
            d.rectangle((x + 1, 3, x + 2, 4), fill=PINK)
    # the arrows are OBJ (they bob 1 px every 16 frames)
    S.sprite(1 - bounce, 0, ARROW_L + ["........"] * 9, OBJ_ARROW)
    S.sprite(151 + bounce, 0, ARROW_R + ["........"] * 9, OBJ_ARROW)
    _ = hp


def footer(S, page):
    """row 16 stays void; row 17 is the page's flavour line, centred (VOID palette). The header's bobbing arrows
    are the paging cue; B or START closes, as on every EGRESS screen."""
    im = S.im
    line = FLAVOUR[page]
    assert len(line) <= 18, line  # 12's 18-glyph rule
    text(im, (160 - 8 * len(line)) // 2, 136, line, STAR, V1)


def stat_card(S, tx0, ty, ico, ico_pal, label, value, sub=None, new=False, phase=0):
    """a 10-tile card: row ty = icon + label, rows ty+1..ty+2 = the big number (+ a small sub-value)"""
    im = S.im
    card(im, tx0, tx0 + 9, ty, ty + 2)
    icon(im, tx0 * 8 + 8, ty * 8, ico, ico_pal)
    text(im, tx0 * 8 + 16, ty * 8, label, LILAC)
    pal = (NEW_BLINK if phase else BG[6][1]) if new else BG[3][1]
    nx = tx0 * 8 + 8
    big(im, nx, (ty + 1) * 8, value, pal)
    sx = nx + 8 * len(value) + 8 * (1 if len(value) < 4 else 0)
    if sub:
        text(im, sx, (ty + 2) * 8, sub, WHITE)
    if new:  # the NEW tag shares palette 6 with the digits, so one palette write blinks both; a sparkle OBJ
        text(im, sx, (ty + 2) * 8, "NEW", pal[3])
        S.sprite(nx + 8 * len(value) - 1, (ty + 1) * 8 + 1 + (phase & 1), SPARK + ["........"] * 7, OBJ_SPARK)


def fmt_time_ms(sec):
    return f"{sec // 60}:{sec % 60:02d}"


def page_runs(R, phase=0, bounce=0):
    S = Screen()
    L = R["life"]
    header(S, "RUNS", bounce)
    a = [None, INK, YEL, WHITE]
    b = [None, INK, PINK, MINT]
    stat_card(S, 0, 2, "DOOR", a, "RUNS", f"{L['runs']}")
    stat_card(S, 10, 2, "SKULL", b, "DEATHS", f"{L['deaths']}")
    stat_card(S, 0, 6, "EXIT", a, "EGRESS", f"{L['egresses']}", sub=f"/{L['runs']}")
    floor, biome = L["deep"] & 63, (L["deep"] >> 6) & 7
    stat_card(S, 10, 6, "DEEP", b, "DEEPEST", f"F{floor}", sub=f"G{L['deep_gen']}")
    stat_card(S, 0, 10, "HEIR", b, "HEIRS", f"{L['heirs']}")
    h, m = L["play_s"] // 3600, L["play_s"] // 60 % 60
    stat_card(S, 10, 10, "CLOCK", a, "PLAYED", f"{h}", sub="HRS")
    # the record strip: two rows of small text under one wide card
    im = S.im
    card(im, 0, 19, 14, 15)
    icon(im, 8, 112, "HOUR", a)
    life = L["long_life"]
    text(im, 16, 112, "OLDEST", LILAC)
    text(im, 72, 112, f"{NAMES.get(L['long_name'], '?')} {life // 1440}D {life // 60 % 24:02d}H", WHITE)
    icon(im, 8, 120, "STAR", a)
    text(im, 16, 120, "FASTEST", LILAC)
    # a NEW record in small text: its tiles use palette 6 too (cyan <-> yellow), so it blinks with the same write
    text(im, 72, 120, fmt_time_ms(L["fast_biome"]), (NEW_BLINK if phase else BG[6][1])[2])
    text(im, 112, 120, BIOMES[L["fast_biome_id"] & 7][:6], WHITE)
    S.sprite(101, 120 + (phase & 1), SPARK + ["........"] * 7, OBJ_SPARK)
    footer(S, "RUNS")
    return S


def page_combat(R, phase=0, bounce=0):
    S = Screen()
    L = R["life"]
    header(S, "COMBAT", bounce)
    a = [None, INK, YEL, WHITE]
    b = [None, INK, PINK, MINT]
    stat_card(S, 0, 2, "SWORD", a, "KILLS", f"{L['kills']}")
    stat_card(S, 10, 2, "SHIELD", b, "NO-HIT", f"{L['nohit']}", new=True, phase=phase)
    stat_card(S, 0, 6, "PARRY", a, "PARRIES", f"{L['parries']}")
    stat_card(S, 10, 6, "LUNGE", b, "LUNGES", f"{L['lunges']}")
    # families and bosses: one wide card, rows 10-12
    im = S.im
    card(im, 0, 19, 10, 12)
    icon(im, 8, 80, "CLAW", b)
    text(im, 16, 80, "FAMILY", LILAC)
    fam = L["kills_fam"]
    top = max(fam)
    d = ImageDraw.Draw(im)
    for i, n in enumerate(fam):  # 8 bars, 6 px wide, up to 15 px tall, in ICON-B (pink, the biggest mint)
        hgt = max(2, round(15 * n / top))
        x = 8 + i * 8
        col = MINT if n == top else PINK
        d.rectangle((x + 1, 103 - hgt, x + 6, 103), fill=col)
        d.line((x + 1, 103 - hgt, x + 6, 103 - hgt), fill=INK if n != top else MINT)
    beaten = sum(1 for n in L["boss_n"] if n)
    text(im, 88, 80, f"BOSS {beaten}/8", LILAC)
    for i, n in enumerate(L["boss_n"]):  # beaten: ICON-A crowns; not yet: LABEL-palette outlines
        icon(im, 88 + i * 8, 88, "CROWN" if n else "CROWN0",
             [None, INK, YEL, WHITE] if n else [None, LILAC, CARD, CARD])
    first = min(g for g in L["boss_gen"] if g)
    text(im, 88, 96, f"FIRST G{first}", WHITE)
    # the record strip
    card(im, 0, 19, 14, 15)
    text(im, 8, 112, "DMG", LILAC)
    text(im, 40, 112, f"{L['dmg_out']}", WHITE)
    text(im, 96, 112, "IN", LILAC)
    text(im, 120, 112, f"{L['dmg_in']}", WHITE)
    text(im, 8, 120, "CRUSH", LILAC)
    text(im, 56, 120, f"{L['k_crush']}", WHITE)
    text(im, 88, 120, "VOID", LILAC)
    text(im, 128, 120, f"{L['k_void']}", WHITE)
    footer(S, "COMBAT")
    return S


# ------------------------------------------------------------------------------------------------ CGB checks
def check(S, bgpals, name):
    im = S.im
    px = im.load()
    bad, used, tiles = [], set(), set()
    for ty in range(H // 8):
        for tx in range(W // 8):
            cs = {px[tx * 8 + x, ty * 8 + y] for y in range(8) for x in range(8)}
            fit = [k for k, (_, pal) in bgpals.items() if cs <= set(pal)]
            if not fit:
                bad.append((tx, ty, sorted(cs)))
                continue
            used.add(fit[0])
            pal = bgpals[fit[0]][1]
            tiles.add(tuple(pal.index(px[tx * 8 + x, ty * 8 + y]) for y in range(8) for x in range(8)))
    sp_bad, per_line, objpals = [], {}, set()
    for (x, y, t, (pname, pal)) in S.sprites:
        cs = {t.getpixel((a, b))[:3] for b in range(16) for a in range(8) if t.getpixel((a, b))[3]}
        if not cs <= set(pal):
            sp_bad.append(pname)
        objpals.add(pname)
        for yy in range(max(0, y), min(H, y + 16)):
            per_line[yy] = per_line.get(yy, 0) + 1
    rep = {"screen": name, "bg_tile_violations": len(bad), "bg_palettes_used": len(used),
           "unique_bg_tiles": len(tiles), "obj_palettes_used": len(objpals), "obj_violations": len(sp_bad),
           "sprites": len(S.sprites), "max_sprites_per_line": max(per_line.values()) if per_line else 0}
    if bad:
        rep["bad"] = bad[:6]
    return rep


def big_font_tiles():
    """the big-number font as the cartridge would hold it: each glyph two tiles, palette indices"""
    tiles = set()
    for ch in "0123456789F:.":
        im = Image.new("RGB", (8, 16), CARD)
        big(im, 0, 0, ch)
        pal = BG[3][1]
        for half in (0, 1):
            tiles.add(tuple(pal.index(im.getpixel((x, half * 8 + y))) for y in range(8) for x in range(8)))
    return len(tiles)


# ------------------------------------------------------------------------------------------------ the toast
STAR16 = ["......11........", ".....1331.......", ".....1331.......", "11111333311111..", "13333333333331..",
          ".133333333331...", "..1333333331....", "...13333331.....", "...13311331.....", "..1331..1331....",
          "..131....131....", "..11......11....", "................", "................", "................",
          "................"]


def toast_frame(base, k):
    """k = 0 the HUD as captured; 1 the toast opens (the window rises 8 px, the row glitches in);
    2 the star pops; 3 settled; 4 closing (the star drifts up and blinks)"""
    im = base.copy()
    HUDBG, HPINK, HYEL, HWHITE = (16, 8, 40), (248, 104, 192), (248, 224, 96), (248, 240, 248)
    sprites = []
    if k == 0:
        return im, sprites
    d = ImageDraw.Draw(im)
    d.rectangle((0, 120, 159, 127), fill=HUDBG)  # the toast row (window at TOAST_Y = 120): hud_msg replaces the text
    msg = "NO-HIT STREAK 48"
    x0 = (20 - (len(msg) + 1)) // 2 * 8
    # the star glyph tile (1 extra HUD tile, accent yellow) then the text in the HUD's white over pink
    for yy, r in enumerate(["...2....", "..222...", "2222222.", ".22222..", "..2.2...", ".2...2..", "........", "........"]):
        for xx, c in enumerate(r):
            if c != ".":
                im.putpixel((x0 + xx, 120 + yy), HYEL)
    tx = x0 + 8
    for ch in msg:
        if ch in FONT:
            rows = FONT[ch]
            w = len(rows[0])
            ox = (8 - w) // 2 if w < 4 else 1
            for yy, row in enumerate(rows):
                for xx, b in enumerate(row):
                    if b == "1":
                        im.putpixel((tx + ox + xx + 1, 120 + yy + 1), HPINK)
                for yy, row in enumerate(rows):
                    for xx, b in enumerate(row):
                        if b == "1":
                            im.putpixel((tx + ox + xx, 120 + yy), HWHITE)
        tx += 8
    if k == 1:  # glitch-in: the row band torn sideways for 2 frames (SCX per line, no VRAM)
        band = im.crop((0, 120, 160, 124))
        im.paste(band, (5, 120))
        band = im.crop((0, 124, 160, 128))
        im.paste(band, (-3, 124))
    pal = ("OBJ5 PARTICLES", [WHITE, YEL, PINK])  # the shared particle palette (5.5)
    if k >= 2:
        sy = {2: 106, 3: 102, 4: 94}[k]
        sx = x0 - 4
        for half in (0, 1):
            t = Image.new("RGBA", (8, 16), (0, 0, 0, 0))
            for yy in range(16):
                for xx in range(8):
                    c = STAR16[yy][half * 8 + xx]
                    if c != ".":
                        t.putpixel((xx, yy), (*(PINK if c == "1" else YEL), 255))
            if k == 4 and half == 1:
                continue  # closing: it blinks out a half at a time
            sprites.append((sx + half * 8, sy, t, pal))
        if k in (2, 3):
            for (dx, dy) in ((-6, 6), (16, 2)):
                t = Image.new("RGBA", (8, 16), (0, 0, 0, 0))
                for yy, r in enumerate(SPARK):
                    for xx, c in enumerate(r):
                        if c != ".":
                            t.putpixel((xx, yy), (*(WHITE if c != "1" else PINK), 255))
                sprites.append((sx + dx, sy + dy, t, pal))
    for (x, y, t, _) in sprites:
        im.paste(t, (x, y), t)
    return im, sprites


# ------------------------------------------------------------------------------------------------ sheet
def label(sheet, x, y, s, col=STAR, scale=2):
    tmp = Image.new("RGB", (len(s) * 8 + 2, 9), (10, 8, 24))
    text(tmp, 0, 0, s, col, (10, 8, 24))
    tmp = tmp.resize((tmp.width * scale, tmp.height * scale), Image.NEAREST)
    sheet.paste(tmp, (x, y))


def main():
    R = sample_record()
    reports = []
    runs = page_runs(R, phase=0)
    combat = page_combat(R, phase=0)
    pals_a = dict(BG)
    pals_b = dict(BG)
    pals_b[6] = ("NEW (blink)", NEW_BLINK)
    for nm, S, pals in (("RUNS", runs, pals_a), ("COMBAT", combat, pals_a),
                        ("RUNS blink", page_runs(R, phase=1, bounce=1), pals_b),
                        ("COMBAT blink", page_combat(R, phase=1, bounce=1), pals_b)):
        reports.append(check(S, pals, nm))
    reports.append({"big_font_tiles": big_font_tiles(), "glyphs": "0-9 F : ."})
    # blink strip: the NO-HIT card over 4 beats (16 frames each), the palette write only
    blink = []
    for ph in (0, 1, 0, 1):
        S = page_combat(R, phase=ph, bounce=ph)
        blink.append(S.compose().crop((80, 8, 160, 48)))
    # toast strip over a real capture
    base = Image.open(OUT / "b1_look_floor2.png").convert("RGB")
    toasts = [toast_frame(base, k)[0] for k in range(5)]
    hud_pal = {0: ("HUD", [(16, 8, 40), (248, 104, 192), (248, 224, 96), (248, 240, 248)])}
    tf = Image.new("RGB", (W, H))
    tf.paste(toast_frame(base, 3)[0])
    hud_rows = tf.crop((0, 120, 160, 128))
    cs = {hud_rows.getpixel((x, y)) for y in range(8) for x in range(160)}
    reports.append({"screen": "toast row", "colours_fit_HUD_palette": cs <= set(hud_pal[0][1]),
                    "obj": "star 2 x 8x16 + 2 sparkles, OBJ5 (particles)", "new_bg_tiles": 1})
    one_runs, one_combat = runs.compose(), combat.compose()
    one_runs.save(OUT / "mock_stats_runs_1x.png")
    one_combat.save(OUT / "mock_stats_combat_1x.png")
    # the sheet: 1x pages | 3x RUNS | 3x COMBAT ; then the blink strip and the toast strip
    bgc = (10, 8, 24)
    SW = 8 + 160 + 16 + 480 + 16 + 480 + 8
    SH = 8 + 24 + 432 + 24 + 24 + 132 + 24 + 24 + 168 + 8
    sheet = Image.new("RGB", (SW, SH), bgc)
    y = 8
    label(sheet, 8, y, "1X", STAR)
    label(sheet, 8 + 160 + 16, y, "RUNS 3X", PINK)
    label(sheet, 8 + 160 + 16 + 480 + 16, y, "COMBAT 3X", PINK)
    y += 24
    sheet.paste(one_runs, (8, y))
    sheet.paste(one_combat, (8, y + 160))
    sheet.paste(one_runs.resize((480, 432), Image.NEAREST), (8 + 160 + 16, y))
    sheet.paste(one_combat.resize((480, 432), Image.NEAREST), (8 + 160 + 16 + 480 + 16, y))
    y += 432 + 24
    label(sheet, 8, y, "A NEW RECORD BLINKS: ONE PALETTE WRITE EVERY 16 FRAMES (3X)", STAR)
    y += 24
    for i, c in enumerate(blink):
        sheet.paste(c.resize((240, 120), Image.NEAREST), (8 + i * 256, y))
        label(sheet, 8 + i * 256, y + 123, f"FRAME {i * 16}", (150, 128, 200), 1)
    y += 132 + 24
    label(sheet, 8, y, "IN-GAME TOAST: HUD MSG + A STAR OBJ (OVER A REAL B1 CAPTURE)", STAR)
    y += 24
    names = ["BEFORE", "F0 GLITCH-IN", "F4 STAR POPS", "F30 HOLD", "F140 CLOSING"]
    for i, t in enumerate(toasts):
        sheet.paste(t, (8 + i * 168, y))
        label(sheet, 8 + i * 168, y + 147, names[i], WHITE, 1)
    zoom = toasts[3].crop((0, 88, 160, 144)).resize((320, 112), Image.NEAREST)
    zx = 8 + 5 * 168 + 8
    sheet.paste(zoom, (zx, y))
    label(sheet, zx, y + 116, "F30 HOLD 2X", WHITE, 1)
    sheet.save(OUT / "mock_stats.png")
    (OUT / "stats_mock_report.json").write_text(json.dumps(reports, indent=1, default=str))
    for r in reports:
        print(r)


if __name__ == "__main__":
    main()
