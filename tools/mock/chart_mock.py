"""EGRESS birth chart screens mock (docs/redesign.md 9.7). Design-stage only: nothing here is game code.

    .venv/bin/python tools/mock/chart_mock.py   # -> docs/shots/chart_wheel_{1x,4x}.png, chart_step_{1x,4x}.png,
                                                #    chart_step_strip.png, chart_transit_{1x,4x}.png,
                                                #    sky_today_{1x,4x}.png, chart_report.json

Every screen is composed the way the cartridge would hold it, then checked:
- the BG (wheel, ring, aspect lines, text) is drawn in RGB and every 8x8 tile must fit one of at most 8 declared BG
  palettes of 4 colours (the CGB attribute map);
- planet glyphs, the Rising mark, R marks and the cursor are 8x8 sprites (OBJ 8x8 mode), 3 colours + transparent,
  at most 8 OBJ palettes, at most 10 per scan line, at most 40;
- unique BG tiles are counted against the 2 x 384 tile VRAM.
The chart data are real: the positions come from the sky engine's own C (proto/sky/out/host_dump, the same code as
the cartridge), the events from the baked tables (proto/sky/out/events.json).
"""
from __future__ import annotations

import datetime as dt
import json
import math
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from egkit import FONT, ROOT  # noqa: E402
import tree_mock  # noqa: E402  (the sign glyphs the TREE screen already uses)

OUT = ROOT / "docs/shots"
SKY = ROOT / "proto/sky/out"
EPOCH = dt.datetime(2000, 1, 1)
UTC_Q = -20  # the mock player's offset: UTC-5, in quarter hours

# ---- colours (EGRESS's pastel/synthwave set) ----------------------------------------------------------------
VOID, VMID, INK = (14, 10, 34), (44, 26, 86), (20, 14, 46)
WHITE, CYAN, PINK, YEL, LILAC = (250, 246, 255), (64, 224, 248), (255, 112, 200), (255, 224, 96), (178, 140, 222)
STAR, MINT, RIM = (236, 220, 255), (124, 206, 168), (150, 116, 210)
ELEM = [(255, 150, 128), (132, 214, 160), (150, 214, 250), (110, 136, 236)]  # fire earth air water
ELEM_NAME = ["FIRE", "EARTH", "AIR", "WATER"]
ELEM_TAG = ["HOT", "STONE", "AIRY", "WET"]
SIGNS = ["ARIES", "TAURUS", "GEMINI", "CANCER", "LEO", "VIRGO", "LIBRA", "SCORPIO", "SAGITTARIUS", "CAPRICORN",
         "AQUARIUS", "PISCES"]
SHORT = ["ARI", "TAU", "GEM", "CAN", "LEO", "VIR", "LIB", "SCO", "SAG", "CAP", "AQU", "PIS"]
BGPAL = {
    "hud": [VOID, INK, WHITE, CYAN],
    "hud2": [VOID, INK, YEL, PINK],
    "stars": [VOID, VMID, STAR, LILAC],
    "inner": [VOID, VMID, MINT, PINK],
}
for i in range(4):
    BGPAL[f"ring{i}"] = [VOID, RIM, ELEM[i], ELEM[(i + 1) % 4]]
# planets: name, sky.c index, OBJ palette (ink, body, highlight), 7x7 glyph (1 ink, 2 body, 3 highlight)
PLANETS = [  # 7x7 glyph shapes ('2' body, '3' highlight); a 1 px ink drop shadow is added (colour 1)
    ("SUN", 0, (INK, (255, 200, 80), (255, 248, 200)),
     ["..222..", ".2...2.", "2..3..2", "2.333.2", "2..3..2", ".2...2.", "..222.."]),
    ("MOON", 1, (INK, (200, 200, 236), WHITE),
     ["..332..", ".32....", "32.....", "22.....", "22.....", ".22....", "..222.."]),
    ("MERCURY", 2, (INK, CYAN, (220, 255, 255)),
     [".3...2.", "..222..", ".3...2.", ".2...2.", "..222..", "...2...", "..222.."]),
    ("VENUS", 3, (INK, (255, 140, 200), (255, 230, 245)),
     ["..332..", ".3...2.", ".2...2.", "..222..", "...2...", "..222..", "...2..."]),
    ("MARS", 4, (INK, (255, 104, 72), (255, 214, 170)),
     ["...3322", ".....22", "..332.2", ".3...2.", ".2...2.", ".2...2.", "..222.."]),
    ("JUPITER", 5, (INK, (255, 190, 100), (255, 244, 210)),
     [".3..2..", "3.2.2..", "...2...", "..2....", ".322222", "....2..", "....2.."]),
    ("SATURN", 6, (INK, (214, 182, 124), (250, 236, 200)),
     [".332...", "..2....", "..2.33.", "..22..2", "..2...2", "..2..2.", ".....2."]),
]


def shadowed(rows: list[str]) -> list[str]:
    """7x7 glyph -> 8x8 sprite rows with a 1 px ink drop shadow (down-right) where it falls on nothing"""
    g = [list(r + ".") for r in rows] + [list("........")]
    for y in range(6, -1, -1):
        for x in range(6, -1, -1):
            if rows[y][x] in "23" and g[y + 1][x + 1] == ".":
                g[y + 1][x + 1] = "1"
    return ["".join(r) for r in g]


GLYPH_R = ["...", "11.", "1.."]  # a small r beside a retrograde planet (in the planet's own palette)


def text(im, x, y, s, col=WHITE, shadow=INK):
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


def bay(x, y):
    return BAYER[y & 3][x & 3]


# ---- the sky, from the cartridge's own C ----------------------------------------------------------------------
def minutes(t: dt.datetime) -> int:
    return int((t - EPOCH).total_seconds() // 60)


def positions(t_utc: dt.datetime) -> list[float]:
    m = minutes(t_utc)
    r = subprocess.run([str(SKY / "host_dump"), "pos", str(m // 1440), str(m // 1440 + 1), "1440", str(m % 1440)],
                       capture_output=True, text=True, check=True).stdout.split()
    return [int(v) * 360.0 / 65536.0 for v in r[2:12]]


def sky_read(t_utc: dt.datetime) -> list[int]:
    m = minutes(t_utc)
    r = subprocess.run([str(SKY / "host_dump"), "read", str(m // 1440), str(m // 1440 + 1), "1440", str(m % 1440)],
                       capture_output=True, text=True, check=True).stdout.split()
    return [int(v) for v in r]


def aspect(a: float, b: float, orb: float):
    d = abs((a - b + 180.0) % 360.0 - 180.0)
    best = None
    for k, ang in enumerate((0.0, 90.0, 120.0, 180.0)):
        if abs(d - ang) <= orb and (best is None or abs(d - ang) < best[1]):
            best = (k, abs(d - ang))
    return best


ASP_NAME = ["CONJ", "SQUARE", "TRINE", "OPPOSE"]
ASP_VERB = ["MEETS", "SQUARES", "TRINES", "OPPOSES"]


def chart(t_utc: dt.datetime) -> dict:
    p = positions(t_utc)
    pts = {n: p[i] for n, i, _, _ in PLANETS}
    pts["ASC"] = p[8]
    pts["MC"] = p[9]
    asp = []
    names = ["SUN", "MOON", "ASC", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"]
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            orb = 8.0 if a in ("SUN", "MOON") else 6.0
            k = aspect(pts[a], pts[b], orb)
            if k:
                asp.append((a, b, k[0], k[1]))
    return dict(t=t_utc, pts=pts, asp=asp, sign={n: int(v // 30) for n, v in pts.items()})


# ---- BG and sprites, with the CGB checks ---------------------------------------------------------------------
class Screen:
    def __init__(self):
        self.im = Image.new("RGB", (160, 144), VOID)
        self.spr: list[tuple[int, int, Image.Image, str]] = []

    def sprite(self, x, y, rows, pal, name):
        s = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
        for yy, r in enumerate(rows):
            for xx, c in enumerate(r):
                if c in "123":
                    s.putpixel((xx, yy), (*pal[int(c) - 1], 255))
        self.spr.append((int(x), int(y), s, name))

    def render(self) -> Image.Image:
        im = self.im.copy()
        for x, y, s, _ in self.spr:
            im.paste(s, (x, y), s)
        return im

    def check(self, name: str, objpals: dict) -> dict:
        px = self.im.load()
        bad, used, tiles = [], set(), set()
        for ty in range(18):
            for tx in range(20):
                cs = {px[tx * 8 + x, ty * 8 + y] for y in range(8) for x in range(8)}
                fit = [k for k, pal in BGPAL.items() if cs <= set(pal)]
                if not fit:
                    bad.append((tx, ty, len(cs)))
                    continue
                used.add(fit[0])
                pal = BGPAL[fit[0]]
                tiles.add(tuple(pal.index(px[tx * 8 + x, ty * 8 + y]) for y in range(8) for x in range(8)))
        per_line: dict[int, int] = {}
        spal, sbad = set(), 0
        for x, y, s, n in self.spr:
            cs = {s.getpixel((a, b))[:3] for b in range(8) for a in range(8) if s.getpixel((a, b))[3]}
            pal = objpals[n]
            if not cs <= set(pal):
                sbad += 1
            spal.add(tuple(pal))
            for yy in range(max(0, y), min(144, y + 8)):
                per_line[yy] = per_line.get(yy, 0) + 1
        rep = dict(screen=name, bg_tile_violations=len(bad), bg_palettes=len(BGPAL), bg_palettes_used=len(used),
                   unique_bg_tiles=len(tiles), sprites=len(self.spr), obj_palettes=len(spal), obj_violations=sbad,
                   max_sprites_per_line=max(per_line.values()) if per_line else 0)
        if bad:
            rep["bad"] = bad[:6]
        assert len(BGPAL) <= 8 and len(spal) <= 8 and rep["max_sprites_per_line"] <= 10 and len(self.spr) <= 40, rep
        return rep


OBJPAL = {n: list(p) for n, _, p, _ in PLANETS}
OBJPAL["CURSOR"] = OBJPAL["MOON"]  # the cursor and the Rising mark share planet palettes: 7 OBJ palettes in all
OBJPAL["ASC"] = OBJPAL["SUN"]


def screen_angle(lon: float, asc: float) -> float:
    """the wheel turns so the Rising sits at 9 o'clock; longitude runs counter-clockwise"""
    return math.radians(180.0 + (lon - asc))


def wheel(sc: Screen, ch: dict, cx: int, cy: int, R: int, sel: str | None = None, band: int = 9,
          lines_r: int | None = None, stars: bool = True, glow: bool = True):
    """the 12 signs as an element-coloured band with cut-out glyphs, the inner glow, aspect chords"""
    px = sc.im.load()
    asc = ch["pts"]["ASC"]
    r_out, r_in = R, R - band
    lines_r = lines_r or r_in - 18
    sel_sign = ch["sign"].get(sel) if sel else None
    for y in range(144):
        for x in range(160):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            r = math.hypot(dx, dy)
            if r_in <= r <= r_out:
                phi = math.degrees(math.atan2(-dy, dx))
                lon = (asc + phi - 180.0) % 360.0
                s = int(lon // 30)
                edge = min(lon % 30, 30 - lon % 30) * math.pi / 180 * r
                if r > r_out - 1 or r < r_in + 1 or edge < 0.6:
                    px[x, y] = RIM
                else:
                    px[x, y] = ELEM[s % 4]
            elif r < lines_r + 2 and glow:
                t = max(0.0, 1.0 - r / (lines_r + 2)) ** 1.3 * 0.9
                px[x, y] = VMID if bay(x, y) < t * 16 else VOID
    # sign glyphs, cut out of the band (VOID on the element colour; the selected planet's sign shows lit)
    for s in range(12):
        lon = s * 30 + 15
        a = screen_angle(lon, asc)
        gx, gy = cx + (r_in + band / 2) * math.cos(a), cy - (r_in + band / 2) * math.sin(a)
        g = tree_mock.SIGN.get(SIGNS[s])
        if not g:
            continue
        col = RIM if s == sel_sign else VOID
        for yy, row in enumerate(g[:7]):
            for xx, c in enumerate(row[:7]):
                if c == "#":
                    X, Y = int(gx - 3 + xx), int(gy - 3 + yy)
                    if 0 <= X < 160 and 0 <= Y < 144:
                        px[X, Y] = col
    # aspect chords between the natal points, inside the glow
    order = ["SUN", "MOON", "ASC", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"]
    for a, b, k, miss in ch["asp"]:
        hard = k in (1, 3)
        col = PINK if hard else MINT
        if sel and sel not in (a, b):
            col = VMID
        if k == 0:
            continue  # a conjunction is two glyphs side by side: no chord
        pa = screen_angle(ch["pts"][a], asc)
        pb = screen_angle(ch["pts"][b], asc)
        x0, y0 = cx + lines_r * math.cos(pa), cy - lines_r * math.sin(pa)
        x1, y1 = cx + lines_r * math.cos(pb), cy - lines_r * math.sin(pb)
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / n
            X, Y = int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t)
            if (k == 1 or k == 3) and (i // 2) % 2 and not (sel and sel in (a, b)):
                continue  # hard aspects dashed until selected
            px[X, Y] = col
    del order
    # the inner rim and the horizon tick
    for i in range(720):
        a = math.radians(i / 2)
        X, Y = int(cx + (lines_r + 1.5) * math.cos(a)), int(cy - (lines_r + 1.5) * math.sin(a))
        if i % 6 < 3:
            px[X, Y] = VMID
    if stars:
        import random
        rnd = random.Random(11)
        for _ in range(70):
            x, y = rnd.randrange(160), rnd.randrange(16, 136)
            tx, ty = x // 8, y // 8
            far = min(math.hypot(tx * 8 + ox - cx, ty * 8 + oy - cy) for ox in (0, 8) for oy in (0, 8))
            if far > R + 3 and px[x, y] == VOID:
                px[x, y] = STAR if rnd.random() < 0.4 else LILAC if rnd.random() < 0.5 else VMID


def place_planets(sc: Screen, ch: dict, cx: int, cy: int, r: float, names=None, retro=(), tag="", sel=None):
    asc = ch["pts"]["ASC"]
    names = names or [p[0] for p in PLANETS]
    placed = []
    for n, _, pal, rows in PLANETS:
        if n not in names:
            continue
        lon = ch["pts"][n]
        rr = r
        for (l2, r2) in placed:  # crowding: step inward
            if abs((lon - l2 + 180) % 360 - 180) < 11 and abs(rr - r2) < 4:
                rr -= 8
        placed.append((lon, rr))
        a = screen_angle(lon, asc)
        x, y = cx + rr * math.cos(a) - 3.5, cy - rr * math.sin(a) - 3.5
        rows2 = shadowed(rows)
        if n in retro:  # a small r in the bottom-right corner, in the planet's highlight
            rows2[5] = rows2[5][:6] + "33"
            rows2[6] = rows2[6][:6] + "3."
            rows2[7] = rows2[7][:6] + "3."
        sc.sprite(x, y, rows2, pal, n)
        if sel == n:  # four corner brackets around the selected glyph (4 sprites, 2 per line)
            e = "........"
            tl = ["333.....", "3.......", "3......."] + [e] * 5
            tr = [".....333", ".......3", ".......3"] + [e] * 5
            bl = [e] * 5 + ["3.......", "3.......", "333....."]
            br = [e] * 5 + [".......3", ".......3", ".....333"]
            for rows_, dx, dy in ((tl, -2, -2), (tr, 2, -2), (bl, -2, 2), (br, 2, 2)):
                sc.sprite(x + dx, y + dy, rows_, OBJPAL["CURSOR"], "CURSOR")


def asc_mark(sc: Screen, cx: int, cy: int, R: int):
    """the Rising: a small yellow arrow just outside the band at 9 o'clock"""
    rows = ["........", "..2.....", ".222....", "22222223", ".222....", "..2.....", "........", "........"]
    sc.sprite(cx - R - 9, cy - 4, rows, OBJPAL["ASC"], "ASC")


def fmt_local(t_utc: dt.datetime, year: bool = True) -> str:
    t = t_utc + dt.timedelta(minutes=15 * UTC_Q)
    return t.strftime("%b %d %Y %H:%M" if year else "%b %d %H:%M").upper()


def utc_label() -> str:
    h, q = divmod(abs(UTC_Q), 4)
    return ("UTC-" if UTC_Q < 0 else "UTC+") + str(h) + (":%02d" % (q * 15) if q else "")


# ---- readings: oblique, a little wrong, never horoscope cheese ----------------------------------------------
READ = {
    ("SUN", 0): "YOU RUN WARM. DOORS STICK WHEN YOU LEAVE.", ("SUN", 1): "HEAVY IN A GOOD WAY. CHAIRS TRUST YOU.",
    ("SUN", 2): "YOU ARRIVE BEFORE THE DUST DOES.", ("SUN", 3): "SOMETHING IN YOU IS ALWAYS SLIGHTLY DAMP.",
    ("MOON", 0): "AT REST YOU TAP. THE FLOOR TAPS BACK.", ("MOON", 1): "YOU SIT LIKE A ROCK SITS: COMPLETELY.",
    ("MOON", 2): "YOUR QUIET FACE KEEPS LOOKING UP.", ("MOON", 3): "WHEN IDLE YOU SWAY. NOBODY MENTIONS IT.",
    ("ASC", 0): "YOU WAKE UP HOLDING A SPARK. FINE.", ("ASC", 1): "YOU WAKE UP HOLDING A STONE. WHOSE?",
    ("ASC", 2): "YOU WAKE UP HOLDING A BREEZE. SOMEHOW.", ("ASC", 3): "YOU WAKE UP HOLDING A PUDDLE. CAREFULLY.",
    ("MERCURY", 0): "SOMETIMES YOU SAY THE THIRD THING.", ("MERCURY", 1): "YOU SAY IT SLOWER. IT LANDS ANYWAY.",
    ("MERCURY", 2): "A SPARE SENTENCE LIVES IN YOUR MOUTH.", ("MERCURY", 3): "YOU ASK THE QUESTION UNDER THE QUESTION.",
    ("VENUS", 0): "SHOPKEEPERS FEEL A SMALL SUNBURN.", ("VENUS", 1): "TRADERS LIKE YOUR SHOES. NO ONE KNOWS WHY.",
    ("VENUS", 2): "YOU HAGGLE BY FORGETTING TO.", ("VENUS", 3): "SELLERS GO SOFT, LIKE BREAD.",
    ("MARS", 0): "YOUR CHARGE COMES OUT LIT.", ("MARS", 1): "YOUR CHARGE LANDS LIKE FURNITURE.",
    ("MARS", 2): "YOUR CHARGE ARRIVES A BIT EARLY.", ("MARS", 3): "YOUR CHARGE COMES IN SIDEWAYS, LIKE A TIDE.",
}
EFFECT = {
    "SUN": lambda e: f"HITS +{ELEM_TAG[e]} 3", "MOON": lambda e: "IDLE AND WHISPERS",
    "ASC": lambda e: f"START WITH {ELEM_TAG[e]}", "MERCURY": lambda e: "1 TALK IN 6 +1 LINE",
    "VENUS": lambda e: f"-3 PRICE: {ELEM_NAME[e]}", "MARS": lambda e: f"CHARGE +{ELEM_TAG[e]} 2",
}
LABEL = {"SUN": "SUN", "MOON": "MOON", "ASC": "RISING", "MERCURY": "MERCURY", "VENUS": "VENUS", "MARS": "MARS"}


def wrap(s: str, n: int = 19) -> list[str]:
    out, cur = [], ""
    for w in s.split():
        if len(cur) + len(w) + (1 if cur else 0) > n:
            out.append(cur)
            cur = w
        else:
            cur = (cur + " " + w).strip()
    return out + [cur] if cur else out


# ---- screens --------------------------------------------------------------------------------------------------
def scr_wheel(ch: dict, name: str) -> tuple[Screen, dict]:
    sc = Screen()
    wheel(sc, ch, 80, 78, 58)
    place_planets(sc, ch, 80, 78, 41)
    asc_mark(sc, 80, 78, 58)
    text(sc.im, 0, 0, name, YEL)
    text(sc.im, 0, 8, fmt_local(ch["t"]), WHITE)
    s = ch["sign"]
    text(sc.im, 0, 137, f"{SHORT[s['SUN']]} {SHORT[s['MOON']]} {SHORT[s['ASC']]} A:READ", CYAN)
    return sc, sc.check("chart_wheel", OBJPAL)


def scr_step(ch: dict, name: str, sel: str) -> tuple[Screen, dict]:
    sc = Screen()
    cx, cy, R = 80, 56, 46
    wheel(sc, ch, cx, cy, R, sel=sel, band=8, lines_r=22)
    place_planets(sc, ch, cx, cy, 31, sel=sel if sel != "ASC" else None)
    asc_mark(sc, cx, cy, R)
    px = sc.im.load()
    for y in range(104, 144):  # the reading box (window layer, rows 13..17): one text colour pair per tile row
        for x in range(160):
            px[x, y] = INK if y == 106 else VOID
    e = ch["sign"][sel] % 4
    deg = int(ch["pts"][sel] % 30)
    head = f"{LABEL[sel]} {SIGNS[ch['sign'][sel]]} {deg}"
    if len(head) > 20:
        head = f"{LABEL[sel]} {SHORT[ch['sign'][sel]]} {deg}"
    text(sc.im, 0, 112, head, YEL)
    lines = wrap(READ[(sel, e)], 20)
    for i, ln in enumerate(lines[:2]):
        text(sc.im, 0, 120 + 8 * i, ln, WHITE)
    text(sc.im, 0, 136, EFFECT[sel](e), CYAN)
    text(sc.im, 0, 0, name, YEL)
    text(sc.im, 120, 0, str(["SUN", "MOON", "ASC", "MERCURY", "VENUS", "MARS"].index(sel) + 1) + "/6>", WHITE)
    return sc, sc.check("chart_step_" + sel.lower(), OBJPAL)


def scr_transit(ch: dict, now_t: dt.datetime, name: str, events: list[tuple[str, str]]) -> tuple[Screen, dict, list]:
    sc = Screen()
    cx, cy = 80, 58
    now = chart(now_t)
    rd = sky_read(now_t)
    retro = {n for n, i, _, _ in PLANETS if rd[5] & (1 << i)}
    # inner natal wheel, transit planets outside the band
    wheel(sc, ch, cx, cy, 40, band=8, lines_r=17, stars=False)
    place_planets(sc, ch, cx, cy, 25)
    px = sc.im.load()
    for i in range(1440):  # the transit track: a dotted ring at r 52
        a = math.radians(i / 4)
        X, Y = int(cx + 52 * math.cos(a)), int(cy - 52 * math.sin(a))
        if 0 <= Y < 144 and i % 10 < 4:
            px[X, Y] = VMID
    now2 = dict(now)
    now2["pts"] = dict(now["pts"])
    now2["pts"]["ASC"] = ch["pts"]["ASC"]  # the outer ring turns with the natal wheel
    place_planets(sc, now2, cx, cy, 51, retro=retro)
    asc_mark(sc, cx, cy, 40)
    # transit hits: now's planets against the natal six
    hits = []
    for n, _, _, _ in PLANETS:
        orb = 4 if n == "MOON" else 1.5 if n in ("JUPITER", "SATURN") else 2
        for nn in ("SUN", "MOON", "MERCURY", "VENUS", "MARS", "ASC"):
            k = aspect(now["pts"][n], ch["pts"][nn], orb)
            if k:
                hits.append((k[1], n, nn, k[0]))
    hits.sort()
    for y in range(112, 144):
        for x in range(160):
            px[x, y] = INK if y == 114 else VOID
    text(sc.im, 0, 0, "NOW", YEL)
    text(sc.im, 104, 0, now_t.strftime("%b %d").upper(), WHITE)
    fx = {"SATURN": ("+1 GUARD -SPEED", 1), "JUPITER": ("+1 HEART", 0), "MARS": ("+DMG -DODGE", 1), "SUN": ("+STATUS", 0),
          "VENUS": ("+1 STATUS -3 PRICE", 0), "MOON": ("+STATUS", 0), "MERCURY": ("ONE MORE LINE", 0)}
    for i, (miss, n, nn, k) in enumerate(hits[:1]):
        text(sc.im, 0, 120, f"{n} {ASP_VERB[k]} {LABEL[nn]}"[:20], WHITE)
        text(sc.im, 0, 128, fx[n][0][:20], CYAN)
    if not hits:
        text(sc.im, 0, 120, "NOTHING TOUCHES YOU", WHITE)
    text(sc.im, 0, 136, ("NEXT " + events[0][0])[:20] if events else "", PINK, INK)
    return sc, sc.check("chart_transit", OBJPAL), hits


def moon_icon(phase8: int) -> list[str]:
    """8x8 moon phase in the MOON palette: 2 lit body, 1 ink (dark side outline), 3 highlight"""
    rows = []
    k = [0, 0.25, 0.5, 0.75, 1.0, 0.75, 0.5, 0.25][phase8]
    waxing = phase8 <= 4
    for y in range(8):
        r = ""
        for x in range(8):
            dx, dy = x - 3.5, y - 3.5
            if dx * dx + dy * dy > 12.5:
                r += "."
                continue
            w = math.sqrt(max(0.0, 12.5 - dy * dy))
            edge = w * (1 - 2 * k)  # terminator x from the lit limb
            lit = (dx > edge) if waxing else (dx < -edge)
            r += "2" if lit else "1"
        rows.append(r)
    return rows


def scr_today(now_t: dt.datetime, natal: dict, rows: list[tuple[str, str, str, str]]) -> tuple[Screen, dict]:
    sc = Screen()
    px = sc.im.load()
    rd = sky_read(now_t)
    for y in range(16, 20):  # a dithered rule under the header (its own tile row)
        for x in range(160):
            if bay(x, y) < (4 - (y - 16)) * 3:
                px[x, y] = VMID
    phase8 = rd[3]
    text(sc.im, 0, 0, "SKY TODAY", YEL)
    text(sc.im, 0, 8, fmt_local(now_t, year=False) + " " + utc_label(), WHITE)
    sc.sprite(148, 2, moon_icon(phase8), OBJPAL["MOON"], "MOON")
    y = 24
    for icon, title, eff, ends in rows:
        if icon in OBJPAL:
            pn = next(p for p in PLANETS if p[0] == icon)
            sc.sprite(0, y, shadowed(pn[3]), pn[2], icon)
        text(sc.im, 8, y, title[:19], WHITE)
        text(sc.im, 8, y + 8, eff[:14], CYAN)
        text(sc.im, 160 - 8 * len(ends), y + 8, ends, YEL)
        y += 24
    return sc, sc.check("sky_today", OBJPAL)


def save(sc: Screen, base: str):
    im = sc.render()
    im.save(OUT / f"{base}_1x.png")
    im.resize((640, 576), Image.NEAREST).save(OUT / f"{base}_4x.png")
    return im


def main() -> int:
    ev = json.loads((SKY / "events.json").read_text())
    # the founder's line: the parent died on 2026-10-15 at 00:48 local (UTC-5): the heir is born then
    birth = dt.datetime(2026, 10, 15, 5, 48)
    ch = chart(birth)
    rep = {"birth_utc": birth.isoformat(), "chart": {k: round(v, 2) for k, v in ch["pts"].items()},
           "signs": {k: SIGNS[v] for k, v in ch["sign"].items()},
           "aspects": [(a, b, ASP_NAME[k], round(m, 1)) for a, b, k, m in ch["asp"]], "screens": []}
    name = "ODA VALE G4"
    sc, r = scr_wheel(ch, name)
    save(sc, "chart_wheel")
    rep["screens"].append(r)
    frames = []
    for sel in ("SUN", "MOON", "ASC", "MERCURY", "VENUS", "MARS"):
        sc, r = scr_step(ch, name, sel)
        rep["screens"].append(r)
        frames.append(sc.render())
        if sel == "MARS":
            save(sc, "chart_step")
    strip = Image.new("RGB", (3 * 160 * 2 + 2 * 4, 2 * 144 * 2 + 4), INK)
    for i, f in enumerate(frames):
        strip.paste(f.resize((320, 288), Image.NEAREST), ((i % 3) * 324, (i // 3) * 292))
    strip.save(OUT / "chart_step_strip.png")
    # the transit view, eighteen days on (a run started on 2026-11-02 21:10 local)
    now = dt.datetime(2026, 11, 3, 2, 10)
    up = upcoming(ev, now, 30)
    rep["next_30_days"] = up
    sc, r, hits = scr_transit(ch, now, name, up)
    save(sc, "chart_transit")
    rep["screens"].append(r)
    rep["transit_hits"] = [(n, ASP_NAME[k], nn, round(m, 2)) for m, n, nn, k in hits]
    rd = sky_read(now)
    rows = today_rows(ev, now, rd, ch)
    rep["sky_today"] = rows
    sc, r = scr_today(now, ch, rows)
    save(sc, "sky_today")
    rep["screens"].append(r)
    rep["sky_read_now"] = dict(zip(["date", "mod", "flags", "phase8", "eclipse", "retro", "station", "moon_sign", "voc_end"], rd))
    (OUT / "chart_report.json").write_text(json.dumps(rep, indent=1, default=str))
    for s in rep["screens"]:
        print(s)
    print(json.dumps({k: rep[k] for k in ("signs", "aspects", "transit_hits", "next_30_days", "sky_today")}, default=str))
    return 0


def stamp(m: int) -> dt.datetime:
    return EPOCH + dt.timedelta(minutes=m)


def upcoming(ev: dict, now: dt.datetime, days: int) -> list[tuple[str, str]]:
    """the next 30 days from the baked tables (what the chart screen's list shows)"""
    m0, m1 = minutes(now), minutes(now) + days * 1440
    out = []
    names = ["NEW MOON", "FIRST QTR", "FULL MOON", "LAST QTR"]
    for m, q in ev["phases"]:
        if m0 <= m < m1 and q in (0, 2):
            out.append((m, names[q]))
    for p, st in ev["stations"].items():
        if p in ("URANUS", "NEPTUNE", "PLUTO"):
            continue
        for m, k in st:
            if m0 <= m < m1:
                out.append((m, f"{p[:4]} {'RX' if k == 'R' else 'DIRECT'}"))
    for p in ("SUN", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"):
        for m, s, f in ev["ingress"][p]:
            if m0 <= m < m1:
                out.append((m, f"{p[:4]} TO {SHORT[s]}"))
    for m, who, k in ev["eclipses"]:
        if m0 <= m < m1:
            out.append((m, f"{who.upper()} ECLIPSE"))
    out.sort()
    res = []
    for m, s in out:
        t = stamp(m) + dt.timedelta(minutes=15 * UTC_Q)
        res.append((f"{t.strftime('%b %d').upper()} {s}", t.isoformat()))
    return res


def today_rows(ev: dict, now: dt.datetime, rd: list[int], natal: dict) -> list[tuple[str, str, str, str]]:
    """SKY TODAY: what holds now, its effect, and when it ends (from the baked tables)"""
    m = minutes(now)
    rows = []

    def ends(mm):
        d = (mm - m) / 1440.0
        return f"{int(d)}D" if d >= 1 else f"{int(d * 24)}H"

    for b, p in ((2, "MERCURY"), (3, "VENUS"), (4, "MARS"), (5, "JUPITER"), (6, "SATURN")):
        if rd[5] & (1 << b):
            nxt = next(x for x, k in ev["stations"][p] if x > m)
            eff = {"MERCURY": "RECIPES SLIP", "VENUS": "PRICES SWAY", "MARS": "FIRE SOFTER",
                   "JUPITER": "", "SATURN": ""}[p]
            rows.append((p, f"{p} BACKWARD", eff or "SLOW TURN", ends(nxt)))
    ph = rd[3]
    pn = ["NEW MOON", "YOUNG MOON", "HALF MOON", "FAT MOON", "FULL MOON", "FAT MOON", "HALF MOON", "OLD MOON"][ph]
    nxt_q = next(x for x, q in ev["phases"] if x > m)
    eff = {0: "DARKER ROOMS", 4: "MOBS BOLDER"}.get(ph, "QUIET")
    rows.append(("MOON", f"{pn} IN {SHORT[rd[7]]}", eff, ends(nxt_q)))
    if rd[2] & 2:
        rows.append(("MOON", "MOON UNTETHERED", "NO ALIGNING", ends(rd[8] + 9497 * 1440)))
    # the tightest natal transit, with its nudge (astro.c's table)
    nowc = chart(now)
    best = None
    for n, _, _, _ in PLANETS:
        orb = 4 if n == "MOON" else 1.5 if n in ("JUPITER", "SATURN") else 2
        for nn in ("SUN", "MOON", "MERCURY", "VENUS", "MARS", "ASC"):
            k = aspect(nowc["pts"][n], natal["pts"][nn], orb)
            if k and (best is None or k[1] < best[0]):
                best = (k[1], n, nn, k[0])
    if best:
        _, n, nn, k = best
        soft = k in (0, 2)
        eff = {"SUN": "+1 STATUS", "MOON": "+2 STATUS", "MERCURY": "ONE MORE LINE", "VENUS": "+1 STAT -3 PRICE",
               "MARS": "+2 DMG" if soft else "+2 DMG -2 DODGE", "JUPITER": "+4 HP" if soft else "+3 HP -1 GUARD",
               "SATURN": "+2 GUARD" if soft else "+2 GUARD -SPEED"}[n]
        speed = {"SUN": 1.0, "MOON": 13.2, "MERCURY": 1.2, "VENUS": 1.1, "MARS": 0.6, "JUPITER": 0.1, "SATURN": 0.05}[n]
        orb = 4 if n == "MOON" else 1.5 if n in ("JUPITER", "SATURN") else 2
        left = (orb + best[0]) / speed  # days until it passes out of orb (as if still applying: the longest case)
        rows.append((n, f"{n[:7]} {ASP_VERB[k][:7]} {LABEL[nn][:4]}", eff, f"{max(1, int(left))}D" if left >= 1 else f"{int(left * 24)}H"))
    return rows[:5]


if __name__ == "__main__":
    raise SystemExit(main())
