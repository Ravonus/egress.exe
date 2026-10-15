"""EGRESS living obstacles (redesign.md 7.7): the look sheet and the office puzzle-room strip.

Run from tools/mock:   python3 obstacle_mock.py
writes docs/shots/mock_obstacles.png and docs/shots/mock_obstacle_room.png (both at 3x), and prints the content
review numbers (hue spread of the sheet) and the solver numbers of the room (obstacle_room.py).

The obstacle look is mobgen's body (same seeded parts) with a fixed language on top:
- body: CHROME (banded metal, biome-tinted, a white specular line) or DARK (near-ink, a chrome rim at the key light);
- outline: 1 px solid INK cut-out outside the silhouette (an elite's is a pulsing colour line in palette 7);
- halo: a still Bayer checker ring 1 px outside the outline, one colour, never pulses (an elite's ring pulses);
- glyph: an infinity sign (mobile obstacles) or a lock (anchored ones) 2 px over the head; it flickers like a bad
  neon (on 6 frames, off 2), it never bobs (an elite's crown bobs 1 px);
- no crown, no orbiting particles, no size bump, no health flash; a hit draws a spark and plays a clink.
"""
from __future__ import annotations

import colorsys
import math
from pathlib import Path

from PIL import Image, ImageDraw

import egkit as E
import mobgen as M
import obstacle_room as R

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/shots"
INK = E.INK
STEEL = (196, 206, 230)
HALO = (214, 226, 246)

# biome-tinted chrome ramps (dark, mid, light); white is the overlay's specular colour
CHROME = {
    "OFFICE": [(66, 44, 70), (196, 160, 170), (246, 226, 228)],    # warm rose steel
    "SERVER": [(18, 56, 74), (120, 200, 214), (214, 248, 250)],    # cyan steel
    "MALL": [(84, 56, 22), (226, 186, 96), (255, 238, 184)],       # brass
    "ATRIUM": [(22, 48, 96), (118, 164, 236), (214, 230, 255)],    # blue steel
    "GLITCH": [(70, 20, 74), (220, 120, 214), (255, 214, 250)],    # magenta chrome
    "GARDEN": [(30, 58, 40), (128, 186, 132), (214, 244, 206)],    # verdigris
    "VENTS": [(48, 40, 52), (170, 160, 170), (232, 226, 232)],
    "ROOFTOP": [(40, 40, 96), (150, 160, 236), (226, 228, 255)],
}
CHROME_ALT = {"OFFICE": [(30, 66, 62), (130, 200, 184), (218, 250, 240)]}  # mint steel (the filing-cabinet ramp)
DARKBODY = [(10, 8, 22), (34, 26, 52), (70, 60, 100)]  # the DARK body: near-ink, the rim takes the chrome light

# behaviour cue colours (overlay B, eyes and accents); the same in every biome, like the tag tells
CUE = {"PATROLLER": (255, 196, 64), "WANDERER": (255, 120, 96), "PUSHER": (120, 236, 140), "EATER": (110, 200, 255),
       "MIRROR": (230, 140, 255), "SENTRY": (255, 70, 90), "LIGHT-EATER": (150, 120, 255),
       "DRIFTER": (64, 224, 248), "FOLLOWER": (255, 160, 200), "SLEEPER": (180, 220, 255)}

INF = [".11.11.", "1..1..1", ".11.11."]
LOCK = [".111.", "1...1", "1...1", "11111", "11.11", "11111"]


def solid_mask(im):
    px = im.load()
    return [[px[x, y][3] > 0 for x in range(im.width)] for y in range(im.height)]


def ring(mask, w, h, d8=True):
    out = []
    nb = [(1, 0), (-1, 0), (0, 1), (0, -1)] + ([(1, 1), (1, -1), (-1, 1), (-1, -1)] if d8 else [])
    for y in range(h):
        for x in range(w):
            if mask[y][x]:
                continue
            if any(0 <= x + dx < w and 0 <= y + dy < h and mask[y + dy][x + dx] for dx, dy in nb):
                out.append((x, y))
    return out


def glyph_stamp(im, tpl, cx, top, col, shadow=INK):
    """a glyph with a full 1 px ink ring (so it reads on the light floor and the dark void alike)"""
    px = im.load()
    w = len(tpl[0])
    x0 = cx - w // 2
    on = {(x0 + xx, top + yy) for yy, row in enumerate(tpl) for xx, c in enumerate(row) if c == "1"}
    if shadow:
        for (X, Y) in on:
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    P = (X + dx, Y + dy)
                    if P not in on and 0 <= P[0] < im.width and 0 <= P[1] < im.height and not px[P][3]:
                        px[P] = (*shadow, 255)
    for (X, Y) in on:
        if 0 <= X < im.width and 0 <= Y < im.height:
            px[X, Y] = (*col, 255)


def obstacle_image(lk, beh, dark=False, glyph="INF", glyph_on=True, pose=None, W=48, H=56, halo=True):
    """RGBA of one obstacle look; codes from mobgen.render, recoloured in the obstacle language"""
    cv = M.render(lk, pose, W, H)
    x0, y0, x1, y1 = cv.bbox()
    span = max(1, y1 - y0)
    ramp = CHROME_ALT[lk["fam"]] if lk.get("alt") and lk["fam"] in CHROME_ALT else CHROME[lk["fam"]]
    d, m, l = DARKBODY if dark else ramp
    cue = CUE[beh]
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = im.load()
    rim_l = CHROME[lk["fam"]][2]
    for y in range(H):
        for x in range(W):
            v = cv.c[y][x]
            if not v:
                continue
            t = (y - y0) / span
            if v == 1:
                c = d if not dark else (4, 2, 12)
            elif v in (2, 3):
                if dark:
                    c = d if (x + y) & 1 or v == 2 else m
                else:  # chrome: sky band, specular line, mid, a dark horizon, ground reflection
                    if t < 0.16:
                        c = l
                    elif t < 0.24:
                        c = E.WHITE
                    elif t < 0.46:
                        c = m
                    elif t < 0.54:
                        c = d if (x + y) & 1 else m
                    elif t < 0.80:
                        c = m
                    else:
                        c = E.dith(m, l, 0.5, x, y)
            elif v == 4:
                c = INK
            elif v == 5:
                c = cue
            else:
                c = E.WHITE
            px[x, y] = (*c, 255)
    im.info["body_px"] = [px[x, y][:3] for y in range(H) for x in range(W) if cv.c[y][x] in (2, 3)]
    if dark:  # the chrome rim at the key light (top-left), as on the heirs
        mask = solid_mask(im)
        for y in range(H):
            for x in range(W):
                if mask[y][x] and (y == 0 or not mask[y - 1][x] or x == 0 or not mask[y][x - 1]):
                    if cv.c[y][x] in (1, 2, 3):
                        px[x, y] = (*rim_l, 255)
    mask = solid_mask(im)
    for (x, y) in ring(mask, W, H, d8=True):  # the ink cut-out
        px[x, y] = (*INK, 255)
    if halo:
        mask2 = solid_mask(im)
        for (x, y) in ring(mask2, W, H, d8=False):  # a still checker, one phase, every frame
            if (x + y) & 1 == 0:
                px[x, y] = (*HALO, 255)
    if glyph_on:
        tpl = INF if glyph == "INF" else LOCK
        top = y0 - 3 - 2 - len(tpl)
        glyph_stamp(im, tpl, (x0 + x1 + 1) // 2, top, E.WHITE)
    else:  # the off frame: a dim ghost of the glyph (the halo colour at half density)
        tpl = INF if glyph == "INF" else LOCK
        top = y0 - 3 - 2 - len(tpl)
        g = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        glyph_stamp(g, tpl, (x0 + x1 + 1) // 2, top, (90, 80, 140), None)
        gp = g.load()
        for y in range(H):
            for x in range(W):
                if gp[x, y][3] and (x + y) & 1:
                    px[x, y] = gp[x, y]
    return im


# ---------------------------------------------------------------------------------------------------- the six
SIX = [  # behaviour, name, verb line, look fields, dark body?, glyph
    ("PATROLLER", "THE COMMUTER", "WALKS ITS ROUTE",
     dict(fam="OFFICE", body=0, size=1, limb="LEGS2", topper="NONE", mouth="LINE", eyes=2, style="DOT", alt=1), False, "INF"),
    ("WANDERER", "THE MOVER", "CRACKS ITS PATH",
     dict(fam="OFFICE", body=1, size=2, limb="WHEELS", topper="NONE", mouth="GRILLE", eyes=2, style="SLIT"), False, "INF"),
    ("PUSHER", "THE TRAY CART", "SHOVES WHAT IT FACES",
     dict(fam="MALL", body=2, size=1, limb="ARMS", topper="NONE", mouth="LINE", eyes=2, style="DOT"), False, "INF"),
    ("EATER", "THE DRAIN", "EATS WHAT IT REACHES",
     dict(fam="ATRIUM", body=2, size=1, limb="LEGS2", topper="NONE", mouth="O", eyes=2, style="RING"), False, "INF"),
    ("MIRROR", "THE DUPLICATE", "COPIES YOU, FLIPPED",
     dict(fam="GLITCH", body=3, size=1, limb="LEGS2", topper="ANTENNAE", mouth="LINE", eyes=2, style="SCREEN"), False,
     "INF"),
    ("SENTRY", "THE TOPIARY", "MOVES WHEN UNSEEN",
     dict(fam="GARDEN", body=3, size=1, limb="NONE", topper="NONE", mouth="NONE", eyes=2, style="SLIT"), True, "LOCK"),
]


def look(fields, seed):
    lk = M.gen(seed, fam=fields["fam"], size=fields["size"], tag="NONE", defence="NONE", body=fields["body"])
    lk.update({k: v for k, v in fields.items() if k not in ("fam", "size", "body")})
    if not M.face_layout(lk["fam"], lk["body"], lk["size"], lk["limb"], lk["eyes"], lk["style"], lk["mouth"]):
        n, st, mo = M.valid_faces(lk["fam"], lk["body"], lk["size"], lk["limb"])[0]
        lk.update(eyes=n, style=st, mouth=mo)
    lk["pattern"] = "NONE"
    return lk


def up(im, k):
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def tag_box(im, x, y, w, name, verb, glyph):
    """the first-sight name tag: an ink panel with a steel rim (an elite's tag is the pink ELITE chip)"""
    E.panel(im, x, y, x + w - 1, y + 23, top=(40, 34, 70), bot=(16, 12, 36), rim=STEEL, under=(110, 104, 150))
    g = Image.new("RGBA", (12, 10), (0, 0, 0, 0))
    glyph_stamp(g, INF if glyph == "INF" else LOCK, 5, 1, E.WHITE)
    im.paste(g, (x + 2, y + 4), g)
    E.text(im, x + 14, y + 4, name, E.WHITE)
    E.text(im, x + 4, y + 14, verb, STEEL)


def mob_cell(kind, w, h, seed):
    return M.bg_cell(kind, w, h, seed).convert("RGBA")


def sheet():
    imgs = []
    for i, (beh, name, verb, f, dark, gl) in enumerate(SIX):
        lk = look(f, M.mix_seed(0x0B57, i))
        imgs.append((beh, name, verb, lk, dark, gl))
    CW = 232
    W = CW * 3
    Hh = 1100
    im = Image.new("RGB", (W, Hh), INK)
    st = M.Stats()
    E.panel(im, 0, 0, W - 1, 17)
    E.text(im, 4, 6, "EGRESS.EXE  LIVING OBSTACLES  THEY CAN'T DIE: THEY ARE THE PUZZLE", E.WHITE)
    y = 22
    E.text(im, 2, y, "GAME SCALE  GLYPH ON (FLOOR) / GLYPH OFF FRAME (VOID)  + FIRST-SIGHT TAG", E.YEL)
    y += 12
    for i, (beh, name, verb, lk, dark, gl) in enumerate(imgs):
        cx, cy = (i % 3) * CW, y + (i // 3) * 96
        for k, kind in enumerate(("floor", "void")):
            cell = mob_cell(kind, 48, 56, i * 2 + k + 7)
            m = obstacle_image(lk, beh, dark, gl, glyph_on=(k == 0))
            st.add(m, lk)
            cell.alpha_composite(m)
            im.paste(cell.convert("RGB"), (cx + 4 + k * 50, cy))
        E.text(im, cx + 108, cy + 4, beh, CUE[beh])
        E.text(im, cx + 108, cy + 14, lk["fam"], (150, 116, 210))
        E.text(im, cx + 108, cy + 24, "DARK BODY" if dark else "CHROME", (150, 116, 210))
        E.text(im, cx + 108, cy + 34, "LOCK GLYPH" if gl == "LOCK" else "INF GLYPH", (150, 116, 210))
        E.text(im, cx + 108, cy + 44, ["M", "M", "L", "XL"][lk["size"]] + " " + str(M.SIZES[lk["size"]]) + " PX",
               (150, 116, 210))
        tag_box(im, cx + 4, cy + 60, CW - 8, name, verb, gl)
    y += 2 * 96 + 4
    # ---- 4x zoom ----------------------------------------------------------------------------------------------
    E.text(im, 2, y, "ZOOM X4  INK CUT-OUT OUTLINE + STILL DITHER HALO + FLICKERING GLYPH, NO CROWN", E.YEL)
    y += 12
    zx = 0
    for i, (beh, name, verb, lk, dark, gl) in enumerate(imgs):
        m = obstacle_image(lk, beh, dark, gl, glyph_on=True)
        bx0, by0, bx1, by1 = m.getbbox()
        wcrop = 28 if lk["size"] < 2 else 36
        cx0 = (bx0 + bx1) // 2 - wcrop // 2
        cell = mob_cell("floor" if i % 2 == 0 else "void", wcrop, 40, 60 + i)
        cell.alpha_composite(m.crop((cx0, 56 - 40, cx0 + wcrop, 56)))
        z = up(cell.convert("RGB"), 4)
        im.paste(z, (zx, y))
        zx += z.width
    zoom_w = zx
    y += 160 + 4
    # ---- never an elite ---------------------------------------------------------------------------------------
    E.text(im, 2, y, "NEVER AN ELITE  SAME BODY, TWO LANGUAGES  (ZOOM X3)", E.YEL)
    y += 12
    base = look(SIX[0][3], M.mix_seed(0x0B57, 0))
    el = M.make_elite(dict(base), 5, affixes=["VAMPIRIC", "HASTED"])
    E.chip(im, 4, y, "ELITE", E.PINK)
    E.text(im, 4, y + 12, "VAMPIRIC HASTED", M.pal_elite(el["affixes"], el["aura"])[1])
    E.text(im, 4, y + 21, M.mob_name(base), E.WHITE)
    tag_box(im, W // 2 + 4, y, 220, "THE COMMUTER", "WALKS ITS ROUTE", "INF")
    y += 32
    panels = []
    for ph in (0, 1):
        c = mob_cell("floor" if ph == 0 else "void", 40, 52, 90 + ph)
        mm = M.mob_image(el, None, ph, 40, 52)

        c.alpha_composite(mm)
        panels.append(up(c.convert("RGB"), 3))
    for ph in (0, 1):
        c = mob_cell("floor" if ph == 0 else "void", 40, 52, 92 + ph)
        c.alpha_composite(obstacle_image(base, "PATROLLER", False, "INF", glyph_on=(ph == 0), W=40, H=52))
        panels.append(up(c.convert("RGB"), 3))
    for k, p in enumerate(panels):
        im.paste(p, (4 + k * 124 + (16 if k >= 2 else 0), y))
    E.text(im, 4, y + 158, "PULSE PHASE 1 / 2", E.CYAN)
    E.text(im, W // 2 + 20, y + 158, "GLYPH ON / OFF FRAME", E.CYAN)
    y += 172
    rows = [("CROWN, BOBS 1 PX", "INF OR LOCK GLYPH, FLICKERS"),
            ("COLOUR OUTLINE, PULSES", "INK OUTLINE + STILL HALO"),
            ("ORBITING SPARKS", "NONE"),
            ("SIZE BUMP", "TRUE SIZE"),
            ("HIT: WHITE FLASH", "HIT: SPARK + CLINK, NO FLASH"),
            ("DIES, DROPS 2X TOKENS", "NEVER DIES, NEVER A HUNT"),
            ("PALETTE 7", "BODY A CHROME/DARK + OVERLAY B")]
    E.text(im, 4, y, "ELITE", E.PINK)
    E.text(im, W // 2 + 20, y, "OBSTACLE", STEEL)
    y += 11
    for a, b in rows:
        E.text(im, 4, y, a, (214, 190, 246))
        E.text(im, W // 2 + 20, y, b, (214, 226, 246))
        y += 10
    y += 4
    # ---- the hit: spark + clink -------------------------------------------------------------------------------
    E.text(im, 2, y, "A HIT ALWAYS BOUNCES  (GAME SCALE, 3 FRAMES, X3)", E.YEL)
    y += 12
    mv = imgs[1]
    hx = 4
    for f in range(3):
        c = mob_cell("floor", 56, 52, 120 + f)
        ob = obstacle_image(mv[3], mv[0], mv[4], mv[5], glyph_on=True, W=56, H=52)
        c.alpha_composite(ob)
        d = ImageDraw.Draw(c)
        bx0, by0, bx1, by1 = ob.getbbox()
        sx, sy = bx0 - 1, (by0 + by1) // 2 + 2
        if f == 0:  # the blade arrives
            d.line((sx - 12, sy - 6, sx - 2, sy), fill=(*E.WHITE, 255))
            d.line((sx - 12, sy - 5, sx - 3, sy + 1), fill=(*E.CYAN, 255))
        elif f == 1:  # spark at the contact, no flash on the body
            for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (2, -2), (-2, 2), (2, 2), (-2, -2), (3, 0)):
                c.putpixel((sx + dx, sy + dy), (*E.WHITE, 255) if abs(dx) + abs(dy) < 2 else (*E.YEL, 255))
        else:  # the blade recoils 3 px; a ring of 4 dither specks
            d.line((sx - 16, sy - 8, sx - 7, sy - 3), fill=(*E.WHITE, 255))
            for dx, dy in ((-3, -3), (-4, 2), (1, -5), (2, 4)):
                c.putpixel((sx + dx, sy + dy), (*HALO, 255))
        z = up(c.convert("RGB"), 3)
        im.paste(z, (hx, y))
        E.text(im, hx + 4, y + z.height + 2, ["CONTACT", "SPARK  CLINK", "RECOIL 3 PX"][f], E.CYAN)
        hx += z.width + 8
    for k, line in enumerate(["NO HP, NO FLASH,", "NO NUMBER.", "HITSTOP 2 FRAMES", "YOURS ONLY.", "",
                              "CLINK: 12.5 PULSE,", "A FALLING 5TH, 6 F.", "DARK BODY: A SHORT", "STATIC HISS."]):
        E.text(im, hx + 2, y + 6 + k * 10, line, (214, 226, 246))
    y += 52 * 3 + 16
    rep = st.report()
    E.text(im, 2, y, "OBJ: M = 2 BODY + 2 OVERLAY + 1 GLYPH  L = 6 + 3 + 1  CAP 3 PER ROOM, ONE L", E.WHITE)
    E.text(im, 2, y + 10, f"SHEET HUE BINS {rep['hue_bins_3pct']}  BODY HUE BINS {rep['body_hue_bins_3pct']}  "
           f"LARGEST BIN {round(rep['body_max_bin'] * 100)} PCT", (150, 116, 210))
    im = im.crop((0, 0, W, y + 20))
    p = OUT / "mock_obstacles.png"
    up(im, 3).save(p)
    return p, rep, zoom_w


# ---------------------------------------------------------------------------------------------------- the room
OY = 8
PAL_GLASS = [(30, 40, 90), (120, 190, 240), (210, 240, 255)]


def cellxy(x, y):
    return x * 16, y * 16 + OY


def is_wall_drawn(x, y):
    return R.MAP[y][x] == "#" and (y == 0 or (x == 5 and 1 <= y <= 5) or (x, y) in ((6, 1), (8, 1)))


def floor_cell(x, y):
    return R.base(x, y) in ".gG" or (x, y) == R.LANDING


def draw_glass_floor(im, X, Y):
    v = E.void(9).crop((X % 140, Y % 120, X % 140 + 16, Y % 120 + 16))
    im.paste(v, (X, Y))
    d = ImageDraw.Draw(im)
    d.rectangle((X, Y, X + 15, Y + 15), outline=PAL_GLASS[1])
    d.line((X + 1, Y + 1, X + 14, Y + 1), fill=PAL_GLASS[2])
    for k in range(4):
        im.putpixel((X + 3 + k, Y + 9 - k), PAL_GLASS[2])
    im.putpixel((X + 11, Y + 4), E.WHITE)
    im.putpixel((X + 12, Y + 12), PAL_GLASS[2])


def draw_pit(im, X, Y, seed=0):
    v = E.void(11 + seed).crop((X % 140, Y % 120, X % 140 + 16, Y % 120 + 16))
    v = E.darken(v, 0.7)
    im.paste(v, (X, Y))
    d = ImageDraw.Draw(im)
    for (a, b) in ((1, 0), (4, 1), (9, 0), (13, 1), (0, 5), (15, 7), (0, 12), (15, 11)):
        d.point((X + a, Y + b), fill=PAL_GLASS[2])
    for (a, b) in ((3, 4), (11, 9), (6, 12)):
        d.point((X + a, Y + b), fill=PAL_GLASS[1])


def draw_partition(im, X, Y, broken=False, cracked=0):
    """the glass partition: a wall-height pane in a steel frame; broken: shards on the floor"""
    if broken:
        put_slab(im, X, Y, 5 * 9 + 2)
        for (a, b, c) in ((3, 4, 2), (6, 9, 1), (10, 5, 2), (12, 11, 1), (4, 13, 2), (8, 2, 1), (13, 7, 2)):
            im.putpixel((X + a, Y + b), PAL_GLASS[c])
            im.putpixel((X + a + 1, Y + b), PAL_GLASS[1])
        return
    h = 7
    d = ImageDraw.Draw(im)
    top = Y - h + 1
    for yy in range(16 + h - 1):
        for xx in range(16):
            c = E.dith(PAL_GLASS[0], PAL_GLASS[1], 0.35 + 0.3 * (yy / (16 + h)), X + xx, top + yy)
            im.putpixel((X + xx, top + yy), c)
    d.rectangle((X, top, X + 15, Y + 15), outline=(120, 110, 150))
    d.line((X + 1, top + 1, X + 14, top + 1), fill=E.WHITE)
    for k in range(7):
        if top + 4 + k < Y + 15:
            im.putpixel((X + 3 + k, top + 12 - k), PAL_GLASS[2])
            im.putpixel((X + 6 + k, top + 18 - k), PAL_GLASS[2])
    if cracked:
        for (a, b) in ((7, 6), (8, 7), (8, 8), (9, 9), (7, 9), (6, 10), (10, 8)):
            im.putpixel((X + a, top + b + 4), E.WHITE)


def put_slab(im, X, Y, seed):
    E.put(im, E.slab("lilac", seed), X, Y)


def wanderer_look():
    beh, name, verb, f, dark, gl = SIX[1]
    return look(f, M.mix_seed(0x0B57, 1))


def heir():
    import sprite_styles as S
    return S, S.four_heirs()[1]


_S, _H = None, None


def player_sprite(facing):
    global _S, _H
    if _S is None:
        _S, _H = heir()
    d = {"N": 4, "E": 2, "S": 0, "W": 6}[facing]
    return _S.render("D3", _H, d, {})


def room_frame(s, facing="N", glyph_on=True, notes=(), crack_fx=False, dead=False, tag=False, path=None):
    im = E.void(5)
    E.text(im, 4, 120 - 8, "", E.WHITE)
    # floors (row-major), then walls on top
    for y in range(R.H):
        for x in range(R.W):
            if not floor_cell(x, y):
                continue
            X, Y = cellxy(x, y)
            if (x, y) == R.LANDING:
                if s["landing_broken"]:
                    draw_pit(im, X, Y)
                else:
                    draw_glass_floor(im, X, Y)
            elif (x, y) == R.GLASS_G:
                if s["g_broken"]:
                    draw_partition(im, X, Y, broken=True)
                else:
                    put_slab(im, X, Y, y * 9 + x)
            else:
                put_slab(im, X, Y, y * 9 + x)
            if not floor_cell(x, y + 1) and not is_wall_drawn(x, y + 1):
                E.underside(im, X, Y + 16)
    for y in range(R.H):
        for x in range(R.W):
            X, Y = cellxy(x, y)
            if is_wall_drawn(x, y):
                E.wall(im, X, Y)
            if (x, y) == R.EXIT:
                d = ImageDraw.Draw(im)
                d.rectangle((X + 3, Y - 12, X + 12, Y + 8), fill=INK)
                d.rectangle((X + 5, Y - 10, X + 10, Y + 8), fill=(40, 26, 86))
                d.line((X + 7, Y - 8, X + 7, Y + 6), fill=E.PINK)
                d.line((X + 8, Y - 8, X + 8, Y + 6), fill=E.WHITE)
    # the planned path (light annotation): dotted cyan
    if path:
        for (x, y) in path:
            X, Y = cellxy(x, y)
            for k in (5, 10):
                im.putpixel((X + k, Y + 8), E.CYAN)
                im.putpixel((X + 8, Y + k), E.CYAN)
    for (kind, x, y) in notes:
        X, Y = cellxy(x, y)
        if kind == "box":
            d = ImageDraw.Draw(im)
            for i in range(0, 16, 2):
                for p in ((X + i, Y), (X + i, Y + 15), (X, Y + i), (X + 15, Y + i)):
                    d.point(p, fill=E.YEL)
        if kind == "boxpink":
            d = ImageDraw.Draw(im)
            for i in range(0, 16, 2):
                for p in ((X + i, Y), (X + i, Y + 15), (X, Y + i), (X + 15, Y + i)):
                    d.point(p, fill=E.PINK)
    # depth order: things in lower rows draw later
    actors = []
    for i, b in enumerate(s["blocks"]):
        if b != (-1, -1):
            actors.append((b[1], 0, ("block", i, b)))
    mx, my, md, _ = s["mob"]
    actors.append((my, 1, ("mob", mx, my, md)))
    if not s["won"]:
        actors.append((s["player"][1], 2, ("player",) + s["player"]))
    else:
        actors.append((0, 2, ("player",) + s["player"]))
    if not s["g_broken"]:
        actors.append((R.GLASS_G[1], 0.5, ("partition",)))
    for _, _, a in sorted(actors, key=lambda t: (t[0], t[1])):
        if a[0] == "block":
            X, Y = cellxy(*a[2])
            E.put(im, E.obj("CRATE" if a[1] == 0 else "CRT"), X, Y - 3)
        elif a[0] == "partition":
            X, Y = cellxy(*R.GLASS_G)
            draw_partition(im, X, Y, cracked=crack_fx)
        elif a[0] == "mob":
            ob = obstacle_image(wanderer_look(), "WANDERER", False, "INF", glyph_on=glyph_on, W=48, H=56)
            bx0, by0, bx1, by1 = ob.getbbox()
            X, Y = cellxy(a[1], a[2])
            ox = X + 8 - (bx0 + bx1 + 1) // 2
            oy = Y + 14 - by1
            E.put(im, ob, ox, oy)
            # facing: a small steel chevron at its front
            dx, dy = R.DIRS[a[3]]
            fx, fy = X + 8 + dx * 11, Y + 8 + dy * 10
            for k in range(-2, 3):
                px_, py_ = (fx + k, fy - abs(k) * (dy if dy else 0)) if dy else (fx - abs(k) * dx, fy + k)
                if 0 <= px_ < 160 and 0 <= py_ < 128:
                    im.putpixel((px_, py_), E.YEL)
        elif a[0] == "player":
            spr = player_sprite(facing)
            X, Y = cellxy(a[1], a[2])
            if s["won"]:
                Y += 6
            E.put(im, spr, X - 8, Y + 15 - spr.height + 1)
    if crack_fx:
        X, Y = cellxy(*R.GLASS_G)
        for (a, b) in ((-4, -6), (18, -2), (20, 6), (-6, 4), (8, -12), (14, 14), (2, 16)):
            E.spark(im, X + a, Y + b, E.WHITE if (a + b) % 2 else (210, 240, 255))
    if tag:
        tag_box(im, 2, 102, 156, "THE MOVER", "CRACKS ITS PATH", "INF")
    if dead:
        E.tear(im, 30, 34, 3)
        E.tear(im, 70, 72, -2)
        E.tear(im, 96, 99, 2)
        E.panel(im, 12, 82, 147, 111, top=(40, 20, 70), bot=(16, 10, 34), rim=E.PINK, under=E.CYAN)
        E.text(im, 16, 88, "THE ROOM FORGETS", E.PINK)
        E.text(im, 24, 99, "HOLD SELECT", (214, 226, 246))
    E.hud(im, 3 - s["hurt"], 4, (), 2)
    return im


def room_strip(rep, chk):
    sol = R.run(R.SOLUTION)
    bad = R.run(R.MISTAKE)
    face = {"N": "N", "E": "E", "S": "S", "W": "W"}

    def facing_at(acts, t):
        for a in reversed(acts[:t]):
            if a in face:
                return face[a]
        return "N"

    steer_t, crack_t, win_t = 7, 15, len(R.SOLUTION)
    mist_t, dead_t = chk["mistake_push_tick"], len(R.MISTAKE)
    # the wanderer's coming route after the steer, from the sim: its next cells
    def route(states, t0, n):
        out = []
        for s in states[t0 + 1:t0 + 1 + n]:
            c = s["mob"][:2]
            if not out or out[-1] != c:
                out.append(c)
        return out

    frames = [
        (room_frame(sol[0], "N", tag=True, notes=[("box", 1, 1)]),
         ["1 START  T0", "IT LOOPS THE WALLS.", "EXIT IS BEHIND GLASS"]),
        (room_frame(sol[steer_t], facing_at(R.SOLUTION, steer_t), glyph_on=False, path=route(sol, steer_t, 8)),
         [f"2 STEER  T{steer_t}", "CRATE INTO CORNER:", "IT TURNS EAST."]),
        (room_frame(sol[crack_t - 1], facing_at(R.SOLUTION, crack_t - 1), crack_fx=True),
         [f"3 CRACK  T{crack_t}", "IT WALKS THROUGH", "THE PANE: SHARDS."]),
        (room_frame(sol[win_t - 1], facing_at(R.SOLUTION, win_t - 1)),
         [f"4 SOLVED  T{win_t}", "SLIP IN BEHIND IT,", "OUT VIA THE LANDING."]),
        (room_frame(bad[mist_t], facing_at(R.MISTAKE, mist_t), notes=[("boxpink", 6, 2)]),
         [f"MISTAKE  T{mist_t}", "CRT PUSHED IN BEHIND", "IT: IT TURNS NORTH."]),
        (room_frame(bad[dead_t], facing_at(R.MISTAKE, dead_t), dead=True),
         [f"DEAD  T{chk['landing_broken_at_tick']}", "IT STEPS OFF THE", "LANDING: A PIT."]),
    ]
    PW, PH = 160, 144
    LAB = 32
    G = 6
    W = 3 * PW + 4 * G
    Hh = 22 + 2 * (PH + LAB + G) + 54
    out = Image.new("RGB", (W, Hh), INK)
    E.panel(out, 0, 0, W - 1, 17)
    E.text(out, 4, 6, "EGRESS.EXE  OFFICE  THE MOVER + GLASS  SOLVER-CHECKED", E.WHITE)
    for i, (fr, lab) in enumerate(frames):
        cx = G + (i % 3) * (PW + G)
        cy = 22 + (i // 3) * (PH + LAB + G)
        out.paste(fr, (cx, cy))
        for k, line in enumerate(lab):
            E.text(out, cx, cy + PH + 2 + k * 10, line, E.YEL if k == 0 else (214, 226, 246))
    y = 22 + 2 * (PH + LAB + G)
    lines = [f"SOLVER: {rep['states']:,} STATES, {round(rep['dead_ratio'] * 100)} PCT DEAD.".replace(",", " ", 1),
             f"BEST PLAN {rep['best_steps']} TICKS. FIRST FATAL MOVE AT T{rep['earliest_fatal_tick']}.",
             "IDLE ROLLOUT AND SOLUTION: PASSIVE-SAFE.",
             f"MISTAKE DEAD FROM T{chk['mistake_dead_from_tick']}; HINT SHOWS AT T{chk['flood_fill_flags_at_tick']}.",
             "THE MOVER ACTS EVERY 2ND TICK. BLOCKED: TURNS RIGHT."]
    for k, line in enumerate(lines):
        E.text(out, 4, y + k * 10, line, E.CYAN if k < 4 else (150, 116, 210))
    p = OUT / "mock_obstacle_room.png"
    up(out, 3).save(p)
    return p


def main():
    p, rep, _ = sheet()
    print(p.relative_to(ROOT), rep)
    r, seen, live = R.report()
    chk = R.check_paths(live)
    print({**r, **chk})
    p2 = room_strip(r, chk)
    print(p2.relative_to(ROOT))


if __name__ == "__main__":
    main()
