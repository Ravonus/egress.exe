"""Redesign mock-ups, drawn with EGRESS's own kit (egkit). Usage: python3 tools/mock/mocks.py OUTDIR"""
import math
import sys
from pathlib import Path

from egkit import *  # noqa: F403
from egkit import AP, ROOT  # noqa: E402
import subprocess  # noqa: E402

OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "docs/shots")
OUT.mkdir(parents=True, exist_ok=True)


def save(im, name, scale=4):
    im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(OUT / name)


def grid_cells(c0, c1, r0, r1, skip=(), yo=-4):
    return [(c * 16, r * 16 + yo) for r in range(r0, r1 + 1) for c in range(c0, c1 + 1) if (c, r) not in skip]


def dotted(im, x, y, col=CYAN):
    d = ImageDraw.Draw(im)
    for i in range(0, 16, 2):
        for p in ((x + i, y), (x + i, y + 15), (x, y + i), (x + 15, y + i)):
            d.point(p, fill=col)


def star(im, x, y):
    tok = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    m = ["00111100", "01222210", "12233221", "12333321", "12233221", "12222221", "01222210", "00111100"]
    for yy, r in enumerate(m):
        for xx, c in enumerate(r):
            if c != "0":
                tok.putpixel((xx, yy), (*[(140, 90, 30), YEL, WHITE][int(c) - 1], 255))
    put(im, tok, x, y)


# ---------------------------------------------------------------- 1. puzzle room mid-assembly, with treasure
def room():
    im = void(5)
    text(im, 104, 4, "NOCLIP", (70, 40, 120), None)
    M = ["          ",
         " ###D##   ",
         " .F.V.... ",
         " .~~..... ",
         " .~~..... ",
         " ...P.... ",
         " ........ "]
    OFF = {6: 2, 7: 7, 8: 14}
    cells = []
    for r, row in enumerate(M):
        for c, ch in enumerate(row):
            if ch == " " or (c == 8 and r == 6):
                continue
            dy = OFF.get(c, 0) + (8 if (c == 8 and r == 5) else 0)
            cells.append((r, c, ch, c * 16, r * 16 - 4 - dy, dy))
    pos = {(x, y) for (r, c, ch, x, y, dy) in cells if ch not in "#D"}
    for (r, c, ch, x, y, dy) in cells:
        if ch in "#D":
            continue
        put(im, water(c) if ch == "~" else slab("lilac", r * 9 + c), x, y)
        if (x, y + 16) not in pos:
            underside(im, x, y + 16, depth=6 if not dy else 3)
        if dy:
            d = ImageDraw.Draw(im)
            for k in range(3, min(dy, 12), 3):
                d.line((x + 4, y + 18 + k, x + 11, y + 18 + k), fill=(70, 40, 120))
        if ch == "V":  # vent grate
            d = ImageDraw.Draw(im)
            d.rectangle((x + 3, y + 3, x + 12, y + 12), fill=INK)
            for yy in range(y + 5, y + 12, 2):
                d.line((x + 4, yy, x + 11, yy), fill=CYAN)
    for (r, c, ch, x, y, dy) in cells:
        if ch in "#D":
            wall(im, x, y)
        if ch == "D":  # the egress: a humming slit door
            d = ImageDraw.Draw(im)
            d.rectangle((x + 3, y - 12, x + 12, y + 8), fill=INK)
            d.rectangle((x + 5, y - 10, x + 10, y + 8), fill=(40, 26, 86))
            d.line((x + 7, y - 8, x + 7, y + 6), fill=PINK)
            d.line((x + 8, y - 8, x + 8, y + 6), fill=WHITE)
    # objects: FIRE in the corner, a wooden crate on the incoming column, a MUG red herring
    put(im, obj("FIRE"), 2 * 16, 2 * 16 - 6)
    put(im, obj("CRATE"), 6 * 16, 3 * 16 - 4 - 2)
    put(im, obj("MUG"), 7 * 16, 5 * 16 - 4 - 7)
    # cards still in the air over their landing outlines
    d = ImageDraw.Draw(im)
    for (cx, cy, h) in [(8 * 16, 6 * 16 - 4, 30), (9 * 16, 4 * 16 - 4, 44)]:
        dotted(im, cx, cy)
        put(im, slab("lilac", cx), cx, cy - h)
        underside(im, cx, cy - h + 16, depth=3)
    # treasure: visible on a wall top, on the dropping block, on the airborne card; one hinted card (warm + shimmer)
    star(im, 1 * 16 + 4, 1 * 16 - 4 - 10)
    star(im, 8 * 16 + 4, 6 * 16 - 4 - 30 + 4)
    star(im, 9 * 16 + 4, 4 * 16 - 4 - 44 + 4)
    for (x, y) in [(18, 6), (31, 10), (150, 14), (128, 66)]:
        spark(im, x, y)
    hx, hy = 2 * 16, 6 * 16 - 4
    px = im.load()
    for y in range(hy + 2, hy + 14):
        for x in range(hx + 2, hx + 14):
            r_, g_, b_ = px[x, y]
            px[x, y] = (min(255, r_ + 14), g_, max(0, b_ - 18))
    px[hx + 5, hy + 5] = WHITE
    px[hx + 10, hy + 9] = WHITE
    # player holding WATER overhead
    put(im, player(), 4 * 16, 5 * 16 - 6)
    put(im, obj("WATER"), 4 * 16, 5 * 16 - 6 - 14)
    tear(im, 58, 60, 3, 96, 160)
    tear(im, 90, 91, -2, 112, 160)
    hud(im, 2, 4, ("WATER",), 2, "2/5")
    return im


# ---------------------------------------------------------------- 2. the portable bench (an EGRESS overlay)
def bench():
    im = room()
    im = im.point(lambda v: int(v * 0.45))  # the room dims behind the bench (palette fade)
    panel(im, 4, 54, 155, 143)
    text(im, 10, 58, "BENCH", PINK)
    text(im, 98, 58, "3/6", (150, 116, 210))
    # sockets: FIRE + WATER -> STEAM
    for i, (n, x) in enumerate([("FIRE", 12), ("WATER", 52), ("STEAM", 108)]):
        socket(im, x, 70, 24, glow=(i == 2))
        o = obj(n).resize((16, 16))
        put(im, o, x + 4, 74)
    text(im, 40, 78, "+", WHITE)
    d = ImageDraw.Draw(im)
    d.line((82, 81, 100, 81), fill=CYAN)
    d.line((97, 78, 100, 81), fill=CYAN)
    d.line((97, 84, 100, 81), fill=CYAN)
    for (x, y) in [(106, 68), (134, 70), (120, 96)]:
        spark(im, x, y)
    text(im, 10, 98, "STEAM", WHITE)
    chip(im, 58, 97, "HOT", (255, 150, 96))
    chip(im, 88, 97, "WET", (110, 170, 250))
    chip(im, 118, 97, "AIR", (214, 200, 250))
    text(im, 10, 110, "RISES. FINDS VENTS.", (150, 218, 212))
    # pocket row
    for i, n in enumerate(["FIRE", "WATER", "MUG", "SEED", None, None]):
        x = 12 + i * 24
        socket(im, x, 122, 18, glow=(i < 2))
        if n:
            put(im, obj(n), x + 1, 123)
    return im


# ---------------------------------------------------------------- 3. combat, far row collapsing, STEAM reaction
def mob(fac, seed, f=2):
    img, _ = face(fac, seed)
    return small(img, f)


def combat(t=7, gif=False):
    im = void(9)
    text(im, 4, 2, "WAKE UP", (120, 70, 150), None)
    cells = grid_cells(1, 9, 2, 7, yo=-4)
    cells = [(x - 8, y) for (x, y) in cells]
    pos = set(cells)
    for (x, y) in cells:
        r, c = (y + 4) // 16, (x + 8) // 16
        put(im, slab("peach", x * 3 + y), x, y)
        if (x, y + 16) not in pos:
            underside(im, x, y + 16)
    # the collapsing row: a wave left->right over time
    d = ImageDraw.Draw(im)
    for c in range(1, 10):
        x, y = c * 16 - 8, 1 * 16 - 4
        k = t - (c - 1) * 2
        if k < 0:
            put(im, slab("peach", c), x, y)
            continue
        if k > 10:
            continue
        if k <= 1:  # crack
            put(im, slab("peach", c), x, y)
            d.line((x + 3, y + 9, x + 12, y + 6), fill=INK)
            d.line((x + 7, y + 3, x + 9, y + 12), fill=INK)
            continue
        q = quarters(darken(slab("peach", c), 0.75 if k < 4 else 0.5))
        for i, (qx, qy) in enumerate(((0, 0), (8, 0), (0, 8), (8, 8))):
            fall = max(0, k - 1 - (i % 2)) * (2 + i)
            put(im, q[i], x + qx, y + qy + fall)
    dae = mob(1, 6)
    eye = mob(3, 3)
    plate = mob(4, 5)
    put(im, eye, 118, 40)
    put(im, plate, 28, 86)
    d.line((28, 110, 51, 110), fill=WHITE)  # armour glint tell
    hit = t >= 6
    put(im, dae if not (gif and t in (6, 7, 14)) else dae.point(lambda v: 255), 78 + (int(2 * math.sin(t)) if t > 9 else 0), 56)
    put(im, player(), 96 + (1 if t in (6, 7) else 0), 70)
    if t in (5, 6, 7, 13, 14):
        for i in range(9):
            a = i / 8 * 2.2
            d.point((100 - int(14 * math.cos(a)), 80 - int(14 * math.sin(a))), fill=WHITE)
    if hit and t < 22:
        k = t - 6
        r = min(4 + k, 11)
        puff(im, 88, 64 - k // 3, r, t)
        if t >= 8:
            puff(im, 80, 56 - k // 4, max(3, r - 4), t + 1)
        for j in range(5):
            ang = j * 1.26 + k * 0.3
            rr = 6 + k * 2
            if k < 10:
                spark(im, 88 + int(rr * math.cos(ang)), 62 + int(rr * math.sin(ang)), [(255, 150, 96), PINK, WHITE, CYAN, YEL][j])
        if t >= 8:
            text(im, 76, 42 - (t % 2), "?", WHITE)
    if 10 <= t < 18:
        ox, oy = 112 - (t - 10) * 2, 60 + (t - 10)
        d.ellipse((ox - 2, oy - 2, ox + 2, oy + 2), fill=CYAN)
    if t >= 18:
        ox, oy = 96 + (t - 18) * 4, 68 - (t - 18) * 3
        d.ellipse((ox - 2, oy - 2, ox + 2, oy + 2), fill=YEL)
    if gif and t in (7, 18):
        text(im, 64, 30, "STEAM" if t == 7 else "PARRY", YEL)
    hud(im, 3, 4, ("FIRE",), 7, "BLIND" if hit else None)
    return im


# ---------------------------------------------------------------- 4. heir pick: three siblings, their parent shown
def genome_face(gen):
    sys.path.insert(0, str(ROOT / "proto/genetics"))
    import genes
    hexg = genes.face_genome(gen)
    line = subprocess.run([str(AP.DUMP), "genome", hexg], capture_output=True, text=True, check=True).stdout.split()
    return AP.face_image(line[0], [int(x, 16) for x in line[1:5]])


def pick_family():
    sys.path.insert(0, str(ROOT / "proto/genetics"))
    import genes
    for seed in range(1, 400):
        hist = genes.run("random", seed * 7919)
        for gi in range(3, 10):
            sibs = hist[gi]["sibs"]
            ps = [genes.express(x) for x in sibs]
            dodges = {q["dodge"] for q in ps}
            ts = [tuple(q["traits"]) for q in ps]
            if len(dodges) == 3 and len(set(ts)) == 3 and all(ts) and len({x["skin"] for x in sibs}) <= 2:
                return genes, hist[gi - 1]["pick"], sibs, gi + 1
    raise SystemExit("no family found")


NAMES = ["VEDA", "OREN", "PIM", "CORA"]
UPS = {"HEAVY": "+PUSH2", "SMALL": "+VENTS", "ECHO": "+TRACE", "GLITCHED": "+PHASE", "INSOMNIAC": "+TALKS",
       "REMEMBERS": "+KEEP1", "DAMP": "+STEAM", "LOUD": "+LURE", "COLOURBLIND": "+SHINY", "ALLERGIC": "+IMMUNE"}
SHORT = {"COLOURBLIND": "C-BLIND", "INSOMNIAC": "INSOMNI", "REMEMBERS": "REMEMBR"}


def heir():
    genes, parent, sibs, gen = pick_family()
    im = void(4, grid=True)
    panel(im, 0, 0, 159, 27)
    pf = small(genome_face(parent), 2)
    put(im, pf, 3, 2)
    text(im, 30, 3, "CORA FELL ON F3", (214, 190, 246))
    text(im, 30, 13, f"GEN {gen}", PINK)
    text(im, 78, 13, "HER KIDS", WHITE)
    for i, g in enumerate(sibs):
        p = genes.express(g)
        x = 3 + i * 52
        sel = i == 1
        panel(im, x, 30, x + 49, 120, top=(70, 44, 130) if sel else (46, 30, 92), bot=(20, 14, 46),
              rim=PINK if sel else (110, 80, 170), under=CYAN if sel else (70, 40, 120))
        put(im, genome_face(g), x + 1, 33)
        tr = p["traits"][0]
        text(im, x + 2, 82, NAMES[i], PINK if sel else WHITE)
        text(im, x + 2, 91, p["dodge"], CYAN)
        text(im, x + 2, 100, SHORT.get(tr, tr)[:6], YEL)
        text(im, x + 2, 109, UPS[tr][:6], (140, 230, 150))
    panel(im, 0, 123, 159, 143)
    text(im, 4, 126, "SAME EYES NEW HANDS", (214, 190, 246))
    text(im, 4, 135, "A:GO ON  SEL:TREE", WHITE)
    return im


# ---------------------------------------------------------------- 5. holdout
def holdout():
    im = void(2)
    cells = [(c * 16 - 8, r * 16 - 4) for r in range(1, 8) for c in range(1, 10)]
    floor(im, cells, "teal")
    d = ImageDraw.Draw(im)
    # the egress being assembled in the centre: frame pieces converging, a charge ring
    cx, cy = 80, 56
    for a in range(0, 360, 12):
        rr = 20
        if a < 0.62 * 360:
            d.point((cx + int(rr * math.cos(math.radians(a - 90))), cy + int(rr * math.sin(math.radians(a - 90)))), fill=CYAN)
    d.rectangle((cx - 8, cy - 14, cx + 7, cy + 10), fill=INK)
    d.rectangle((cx - 6, cy - 12, cx + 5, cy + 10), fill=(40, 26, 86))
    d.line((cx - 1, cy - 10, cx - 1, cy + 8), fill=PINK)
    d.line((cx, cy - 10, cx, cy + 8), fill=WHITE)
    for (px_, py_) in [(cx - 16, cy - 22), (cx + 14, cy - 20), (cx + 18, cy + 6)]:  # pieces flying in
        d.rectangle((px_, py_, px_ + 3, py_ + 3), fill=(150, 218, 212))
    put(im, player(), 54, 70)
    for (fa, se, x, y) in [(4, 6, 6, 18), (0, 2, 130, 10), (4, 6, 132, 92), (5, 6, 8, 98), (4, 14, 100, 100)]:
        put(im, mob(fa, se), x, y)
    for k in range(6):
        d.point((4 + k * 5, 44), fill=(255, 90, 110))  # lunge tell
    for yy in range(108, 124, 2):  # the next wave glitching in at the edge
        d.line((136, yy, 159, yy), fill=CYAN if yy % 4 else PINK)
    panel(im, 0, 0, 159, 10)
    text(im, 4, 2, "HOLD", PINK)
    text(im, 48, 2, "WAVE 2/3", WHITE)
    text(im, 120, 2, "62%", CYAN)
    hud(im, 3, 4, (), 4)
    return im


# ---------------------------------------------------------------- 6. conditional kill
def cond():
    im = void(11)
    cells = [(c * 16 - 8, r * 16 - 4) for r in range(1, 8) for c in range(1, 10)]
    floor(im, cells, "lilac")
    put(im, water(1), 40, 92)
    put(im, water(3), 56, 92)
    d = ImageDraw.Draw(im)
    put(im, mob(3, 4), 92, 40)
    for (x, y) in [(94, 66), (110, 64), (100, 70), (116, 58)]:
        d.line((x, y, x, y + 2), fill=(110, 170, 250))
    for a in range(0, 360, 15):
        if a % 60 < 40:
            d.point((104 + int(17 * math.cos(math.radians(a))), 52 + int(17 * math.sin(math.radians(a)))), fill=CYAN)
    put(im, player(), 60, 62)
    for pts in ([(76, 66), (84, 58), (80, 56), (92, 50), (88, 48), (98, 46)], [(110, 44), (118, 36), (114, 34), (124, 28)]):
        d.line(pts, fill=YEL)
        d.line([(x + 1, y) for x, y in pts], fill=WHITE)
    for (x, y, c) in [(100, 44, WHITE), (96, 58, YEL), (112, 50, YEL), (122, 40, WHITE)]:
        spark(im, x, y, c)
    put(im, obj("SPARK"), 124, 86)
    text(im, 6, 4, "IT HATES RAIN", (214, 190, 246), None)
    panel(im, 0, 112, 159, 127, top=(70, 44, 130))
    text(im, 4, 116, "WET+SHOCK: SHORTED", YEL)
    hud(im, 3, 4, ("SPARK",), 9)
    return im


# ---------------------------------------------------------------- 7. boss arena
def boss():
    im = void(7)
    holes = {(2, 3), (3, 3), (7, 4), (8, 4), (8, 5), (6, 7)}
    cells = [(c * 16 - 8, r * 16 - 4) for r in range(1, 8) for c in range(1, 10) if (c, r) not in holes]
    floor(im, cells, "teal")
    for (c, r) in [(1, 6), (2, 6), (1, 7), (2, 7), (3, 7)]:
        put(im, water(c), c * 16 - 8, r * 16 - 4)
    d = ImageDraw.Draw(im)
    b, _ = face(0, 12)
    put(im, b, 56, 4)
    for (x, y) in [(28, 18), (116, 14), (124, 44), (20, 40)]:
        put(im, darken(slab("teal", x), 0.8), x, y)
        underside(im, x, y + 16, depth=2)
        for k in range(1, 4):
            d.point((x + 8 + (80 - x) * k // 5, y + 18 + (28 - y) * k // 5), fill=CYAN)
    for a in range(0, 360, 20):
        d.point((100 + int(10 * math.cos(math.radians(a))), 92 + int(6 * math.sin(math.radians(a)))), fill=(255, 90, 110))
    put(im, player(), 60, 84)
    panel(im, 0, 112, 159, 127, top=(70, 44, 130))
    text(im, 4, 115, "OLD MONITOR", WHITE)
    for i in range(3):
        d.rectangle((100 + i * 18, 117, 115 + i * 18, 121), fill=(255, 90, 110) if i < 2 else (60, 40, 90))
    hud(im, 3, 4, ("WATER",), 11, "IT PULLS")
    return im


if __name__ == "__main__":
    save(room(), "mock_room.png")
    save(bench(), "mock_bench.png")
    save(combat(), "mock_combat.png")
    save(heir(), "mock_heir.png")
    save(holdout(), "mock_holdout.png")
    save(cond(), "mock_cond.png")
    save(boss(), "mock_boss.png")
    frames = [combat(t, gif=True).resize((480, 432), Image.NEAREST) for t in range(24)]
    frames[0].save(OUT / "mock_reaction.gif", save_all=True, append_images=frames[1:], duration=100, loop=0)


# ---------------------------------------------------------------- 8. title: the backrooms walk behind the logo
def big_text(im, x, y, s, col, shadow=INK, scale=2):
    tmp = Image.new("RGB", (len(s) * 8 + 2, 10), (1, 2, 3))
    text(tmp, 0, 0, s, col, shadow)
    tmp = tmp.resize((tmp.width * scale, tmp.height * scale), Image.NEAREST)
    mask = Image.eval(tmp.convert("L"), lambda v: 0 if v == 2 else 255)
    px = tmp.load()
    m = mask.load()
    for yy in range(tmp.height):
        for xx in range(tmp.width):
            if px[xx, yy] == (1, 2, 3):
                m[xx, yy] = 0
    im.paste(tmp, (x, y), mask)


def title(frame_png):
    im = void(12)
    ray = Image.open(frame_png).convert("RGB").crop((16, 32, 144, 112))  # the real raycaster render (128x80)
    panel(im, 12, 28, 147, 117, top=(46, 30, 92), bot=(20, 14, 46))
    im.paste(ray, (16, 33))
    d = ImageDraw.Draw(im)
    # fluorescent strip on the ceiling (buzzing: palette flicker) and a poster decal on the left wall (planned art)
    d.rectangle((70, 36, 90, 37), fill=WHITE)
    d.line((72, 38, 88, 38), fill=(250, 250, 200))
    d.rectangle((24, 56, 31, 67), fill=(255, 224, 96))
    d.rectangle((25, 57, 30, 61), fill=(255, 112, 200))
    d.line((25, 64, 30, 64), fill=INK)
    big_text(im, 1, 4, "EGRESS.EXE", CYAN)
    big_text(im, -1, 3, "EGRESS.EXE", PINK, None)
    tear(im, 9, 11, 3, 0, 160)
    text(im, 44, 122, "PRESS START", WHITE)
    text(im, 4, 134, "SEED 4F2A", (110, 80, 170), None)
    text(im, 112, 134, "V0.3", (110, 80, 170), None)
    return im
