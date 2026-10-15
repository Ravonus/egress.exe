"""Heir sprite style options (design mock, not game code).

Renders the small in-room human sprites in three style directions (A, B, C) next to the CURRENT 16x18 sprite, all
from the heir genome (proto/genetics/genes.py) and the same palette the portrait uses (build/host/avatar_dump).

Every frame is built the way the cartridge would build it:
  * a rig paints labelled parts (hair, skin, top, pants, shoes, lanyard, accessory...) for the pose and direction;
  * shading is computed in screen space (key light from the top left) and quantised to 3 tones per material with a
    4x4 Bayer dither band between them (KEEL style), plus coloured inner lines where one part overlaps another;
  * a dark outline on the outer silhouette only, and a rim light in the biome's accent colour;
  * the 6 colours are two CGB OBJ palettes: BODY (hair/ink, skin, skin light) and WEAR (cloth dark, cloth, rim).
    Each palette is its own layer of 8x16 sprites at the same position (the "material overlay"); a pixel can show a
    WEAR colour only where a WEAR sprite covers it, and the same for BODY. `legalize` enforces that and `cost`
    counts sprites, sprites per scan line and palettes.

    .venv/bin/python tools/mock/sprite_styles.py            # writes docs/shots/sprite_styles.png, _room.png (A, B, C)
    .venv/bin/python tools/mock/sprite_styles.py --d        # writes docs/shots/sprite_styles_D.png, _D_room.png
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from egkit import (AP, BAYER, CYAN, INK, PINK, ROOT, WHITE, YEL, hud, obj, panel, put, slab, small, spark,  # noqa: E402
                   tear, text, underside, void, wall, water)

sys.path.insert(0, str(ROOT / "proto/genetics"))
import genes  # noqa: E402

OUT = ROOT / "docs/shots"

# ------------------------------------------------------------------------------------------------ colours
# accent gene (0..7, linked to the strongest affinity by genes.CLOTH_FOR_TAG) -> cloth (dark, base)
CLOTH = [
    ((44, 48, 92), (128, 136, 184)),  # 0 slate (no affinity)
    ((34, 62, 132), (120, 176, 244)),  # 1 COLD ice blue
    ((18, 70, 86), (64, 176, 172)),  # 2 WET sea teal
    ((30, 86, 58), (118, 200, 112)),  # 3 ALIVE leaf
    ((62, 32, 116), (168, 116, 236)),  # 4 MAGIC violet
    ((112, 32, 50), (244, 116, 84)),  # 5 HOT ember
    ((118, 68, 28), (240, 196, 84)),  # 6 SHINY gold
    ((94, 24, 92), (236, 96, 196)),  # 7 GLITCH magenta
]
# biome accent = the rim light (one palette entry, swapped per floor theme)
RIMS = {"NOCLIP": CYAN, "POOLROOMS": (120, 248, 196), "BOILER": (255, 176, 72), "NEON MALL": PINK, "ARCHIVE": YEL}
B1, B2, B3, W1, W2, W3 = range(6)  # colour slots: BODY palette 0..2, WEAR palette 3..5
SLOT_NAMES = ["HAIR/INK", "SKIN", "SKIN LT", "CLOTH DK", "CLOTH", "RIM"]


def rgb555(c):
    return AP.rgb(c)


# ------------------------------------------------------------------------------------------------ heirs
HAIRS = ["BALD", "SHORT", "LONG", "BUN", "SPIKES"]
LOOKS = ["DREAM", "BOY", "GIRL"]
NECKS = ["CREW", "V-COLLAR", "HIGH COLLAR", "HOOD"]
PATS = ["PLAIN", "STRIPES", "PINSTRIPE", "CHECK", "DOTS", "DIAGONAL", "CHEVRON", "TWO-TONE"]
MARKS = ["NONE", "GLASSES", "HEADPHONES", "SCARF", "BEANIE", "EARRING", "BACKPACK", "VISOR"]
CUTS = ["LONG SHIRT", "TROUSERS", "SKIRT+LEGGINGS"]


def eff_hair(g):
    """the hair the portrait actually draws (same mapping as genes.face_genome + avatar_make_genome)"""
    gr = g["hair"]
    if g["look"] == 1:
        gr = 1 if gr in (1, 4) else 0
        return 4 if gr & 1 else 1
    if g["look"] == 2:
        return 3 if gr & 1 else 2
    return gr if gr < 5 else 0


def make_heir(g, name="", rim=CYAN):
    p = genes.express(g)
    hexg = genes.face_genome(g)
    fh, pal = AP.faces(["genome", hexg])[0]
    body = [rgb555(c) for c in pal[1:4]]  # the portrait's own ink/hair, skin, light
    cd, cb = CLOTH[p["accent"]]
    return {
        "name": name, "g": g, "p": p, "genome": hexg, "face": AP.face_image(fh, pal),
        "skin": g["skin"], "hair": eff_hair(g), "look": g["look"], "neck": g["neck"], "pat": g["pat"],
        "dith": g["dith"], "size": p["size"], "accent": p["accent"], "mark": g["mark"], "weapon": p["weapon"],
        "dodge": p["dodge"], "traits": p["traits"], "seed": int(hexg, 16) & 0xFFFF,
        "pal": body + [cd, cb, rim],
    }


def q555(c):
    """round to the CGB's 5 bits per channel (and back, the way AP.rgb expands it)"""
    return tuple(int(round(max(0, min(255, v)) * 31 / 255)) * 255 // 31 for v in c)


def skin_shadow(skin):
    """the D styles' skin shadow: the skin darkened and hue-shifted toward red / plum, never toward grey.

    Light skin keeps more value (a rose-tan shadow), deep skin drops less in red than in green and keeps its blue, so
    the shadow is a warm red-brown, and the result is pulled 12 % toward a crimson-plum (96, 28, 48), which is what
    makes it read as shadow on skin rather than as dirt. It is a BODY palette entry of its own, so it never mixes the
    hair/ink colour into a face (that is what made the earlier deep-skin shadows grey-black and muddy)."""
    L = lum(skin)
    f = 0.70 + 0.11 * min(1.0, L / 190)
    base = (skin[0] * f, skin[1] * f * 0.80, skin[2] * f * 0.88)
    return q555(tuple(base[i] * 0.88 + (96, 28, 48)[i] * 0.12 for i in range(3)))


def style_pal(st, h):
    """the 6 colours a style shows: A/B/C use the portrait's BODY colours; D swaps skin light for the skin shadow"""
    if isinstance(st, str):
        st = STYLES[st]
    pal = h["pal"]
    if st.get("cel"):
        return [pal[0], pal[1], skin_shadow(pal[1])] + list(pal[3:])
    return pal


def pick_four():
    """4 diverse heirs: spread of skin tones, different hair, cuts, accents, accessories"""
    want_skin = [0, 2, 4, 5]
    got = []
    used_hair, used_acc, used_mark, used_look = set(), set(), set(), []
    for seed in range(1, 3000):
        rng = genes.Rng(seed * 7919)
        g = genes.founder(rng)
        g = genes.child(g, rng, [])
        if len(got) >= 4:
            break
        if g["skin"] != want_skin[len(got)]:
            continue
        h = eff_hair(g)
        p = genes.express(g)
        if h in used_hair or p["accent"] in used_acc or g["mark"] in used_mark or g["mark"] == 0:
            continue
        if used_look.count(g["look"]) >= 2:
            continue
        got.append(g)
        used_hair.add(h)
        used_acc.add(p["accent"])
        used_mark.add(g["mark"])
        used_look.append(g["look"])
    return got


NAMES4 = ["VEDA", "OREN", "PIM", "CORA"]


def four_heirs(rim=CYAN):
    return [make_heir(g, n, rim) for g, n in zip(pick_four(), NAMES4)]


# ------------------------------------------------------------------------------------------------ styles
STYLES = {
    "A": dict(name="A", title="A: DITHERED CLASSIC", W=16, H=20, head=(6.3, 5.7, 5.3), ex=2, eh=2, eye_y=0.3,
              sh_y=11.4, hip_y=14.8, foot_y=19, shoe_h=2, sw=3.7, hw=3.3, aw=2.0, lw=2.6, gap=1.0, hand_y=14.2,
              cover={"BODY": [(0, 0), (8, 0)], "WEAR": [(0, 4), (8, 4)]}, rim="soft", detail=1, big_eye=False),
    "B": dict(name="B", title="B: NEON CHIBI", W=16, H=24, head=(7.6, 7.2, 6.6), ex=2, eh=3, eye_y=0.22,
              sh_y=14.4, hip_y=19.0, foot_y=23, shoe_h=2, sw=3.4, hw=3.1, aw=2.0, lw=2.5, gap=1.0, hand_y=18.6,
              cover={"BODY": [(0, 0), (8, 0), (0, 16), (8, 16)], "WEAR": [(0, 8), (8, 8)]}, rim="strong", detail=1,
              big_eye=True, neon_sole=True),
    "C": dict(name="C", title="C: TALL TAILORED", W=16, H=32, head=(6.4, 5.4, 5.4), ex=1, eh=2, eye_y=0.3,
              sh_y=12.8, hip_y=21.6, foot_y=31, shoe_h=2, sw=4.7, hw=3.9, aw=2.4, lw=3.2, gap=1.0, hand_y=21.5,
              cover={"BODY": [(0, 0), (8, 0), (0, 16), (8, 16)], "WEAR": [(0, 0), (8, 0), (0, 16), (8, 16)]},
              rim="inner", detail=2, big_eye=False),
}
# D1-D3: refinements of A (the owner's pick of the four, 2026-10): A's 16x20 size, proportions and read in the room,
# plus hard cel bands (designed shadow shapes, light from the top left), more ordered dither and C's outfit detail.
# They shade with `cel_pass` and use their own BODY palette (`style_pal`): hair/ink, skin, and a hue-shifted skin
# SHADOW instead of the portrait's skin light (see skin_shadow). The cel dict tunes each variant:
#   sw   shadow width as a fraction of the part's width (+1 px the outline eats on the silhouette)
#   term terminator band in px between light and shadow (Bayer 50 %, then 25 %)
#   tex  ordered-dither cloth texture in the lit cloth (0 none, else the Bayer fraction of cloth dark)
#   hmid a second, dithered row under the hair sheen (Bayer fraction)
#   pants Bayer fraction of cloth dark in the lit trousers (they read a step darker than the jacket)
#   rim  "solid" short runs or "dith" (every other pixel) on the shadow-side edge of the cloth, lower body only
#   cast hair fringe shadow on the forehead: 1.0 a solid row, 0.5 dithered
_D = dict(STYLES["A"], rim="cel", detail=1, big_eye=False)
STYLES["D1"] = dict(_D, name="D1", title="D1: HARD CEL", cel=dict(sw=0.34, term=1, tex=0, hmid=0, rim="solid", cast=1.0,
                                                                  pants=0.5))
STYLES["D2"] = dict(_D, name="D2", title="D2: CEL + DITHER",
                    cel=dict(sw=0.30, term=2, tex=0.1875, hmid=0.5, rim="dith", cast=0.5, pants=0.5))
# D3: two more rows of outfit (torso + legs); the BODY cells sit 1 px lower so the hands reach the hips, and the WEAR
# cells start at row 6 so the shoes are covered: still 4 sprites, 4 per line. A visor is drawn no higher than row 6
# (it needs a WEAR cell), so on D3 it sits on the hairline rather than up in the hair
STYLES["D3"] = dict(_D, name="D3", title="D3: BALANCED", H=22, head=(7.1, 5.7, 5.3), sh_y=12.4, hip_y=16.8,
                    foot_y=21, hand_y=15.6, cover={"BODY": [(0, 1), (8, 1)], "WEAR": [(0, 6), (8, 6)]},
                    cel=dict(sw=0.32, term=1, tex=0.0625, hmid=0, rim="solid", cast=0.5, pants=0.5))
D_STYLES = ["D1", "D2", "D3"]
M = 8  # margin around the character box for weapon / FX sprites

# groups (shading volumes)
G_HEAD, G_TORSO, G_ARMN, G_ARMF, G_LEGN, G_LEGF, G_SHOEN, G_SHOEF, G_HAIRB, G_WPN, G_FX, G_SKIRT = range(1, 13)
RIM_GROUPS = {"strong": {1, 2, 3, 4, 5, 6, 9, 12}, "soft": {2, 3, 4, 5, 6, 12}, "inner": {1, 2, 3, 4, 9},
              "cel": {2, 3, 4, 5, 6, 12}}
CYL = {G_TORSO, G_ARMN, G_ARMF, G_LEGN, G_LEGF, G_HAIRB, G_SKIRT}

# materials -> shading bands [(l_max, colour a, colour b, fraction of b)]
MAT = {}


def _m(name, bands, keep=False, wear=False):
    MAT[name] = dict(id=len(MAT) + 1, bands=bands, keep=keep, wear=wear)


_m("skin", [(0.4, B2, B1, 0.25), (0.88, B2, B2, 0), (0.95, B2, B3, 0.25), (9, B2, B3, 0.5)])
_m("hair", [(9, B1, B1, 0)])
_m("hairM", [(9, B1, B2, 0.5)])
_m("hairS", [(0.75, B2, B2, 0), (9, B3, B3, 0)])
_m("eye", [(9, B1, B1, 0)], keep=True)
_m("eyeH", [(9, B3, B3, 0)], keep=True)
_m("mouth", [(9, B1, B2, 0.5)], keep=True)
_m("top", [(0.4, W1, W1, 0), (0.56, W2, W1, 0.5), (0.9, W2, W2, 0), (9, W2, B3, 0.25)], wear=True)
_m("topP", [(0.7, W1, W1, 0), (9, W2, W1, 0.5)], wear=True)
_m("shirt", [(0.35, B3, W1, 0.25), (9, B3, B3, 0)], wear=False)
_m("sleeve", [(0.5, W1, W1, 0), (0.7, W2, W1, 0.5), (9, W2, W2, 0)], wear=True)
_m("pants", [(0.62, W1, W1, 0), (0.86, W1, W2, 0.25), (9, W1, W2, 0.5)], wear=True)
_m("shoe", [(0.7, W1, W1, 0), (9, W1, W2, 0.5)], wear=True)
_m("sole", [(9, W3, W3, 0)], keep=True, wear=True)
_m("lan", [(9, W3, W3, 0)], keep=True, wear=True)
_m("badge", [(9, B3, B3, 0)], keep=True)
_m("belt", [(9, W1, W1, 0)], wear=True)
_m("buckle", [(9, W3, W3, 0)], keep=True, wear=True)
_m("cuff", [(9, B3, W2, 0.25)])
_m("acc", [(0.5, W3, W2, 0.5), (9, W3, W3, 0)], wear=True)
_m("accD", [(0.6, W1, W1, 0), (9, W1, W2, 0.25)], wear=True)
_m("ink", [(9, B1, B1, 0)], keep=True)
_m("hatB", [(0.45, B1, B1, 0), (9, B1, B2, 0.25)])
_m("wpn", [(0.5, W2, W1, 0.25), (9, W2, W3, 0.25)], wear=True)
_m("wpnE", [(9, W3, W3, 0)], keep=True, wear=True)
_m("fxR", [(9, W3, W3, 0)], keep=True, wear=True)
_m("fxL", [(9, W2, W2, 0)], keep=True, wear=True)
_m("fxD", [(9, W2, W3, 0.5)], keep=True, wear=True)
_m("seam", [(9, W1, W1, 0)], keep=True, wear=True)  # D: jacket opening / hem line (a coloured inner line)
MID = {v["id"]: k for k, v in MAT.items()}
FXMATS = {"fxR", "fxL", "fxD", "wpnE", "wpn"}
LIGHT = np.array([-0.55, -0.7, 0.62])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


def bayer(x, y, frac):
    return BAYER[y & 3][x & 3] < frac * 16


class Canvas:
    def __init__(s, w, h):
        s.w, s.h = w, h
        s.mat = np.zeros((h, w), np.int16)
        s.grp = np.zeros((h, w), np.int16)
        s.z = np.zeros((h, w), np.int16)
        s.zc = 0

    def px(s, x, y, m, g):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < s.w and 0 <= y < s.h:
            s.mat[y, x] = MAT[m]["id"]
            s.grp[y, x] = g
            s.z[y, x] = s.zc

    def fill(s, test, m, g, box=None):
        s.zc += 1
        x0, y0, x1, y1 = box or (0, 0, s.w - 1, s.h - 1)
        for y in range(max(0, int(y0)), min(s.h, int(y1) + 2)):
            for x in range(max(0, int(x0)), min(s.w, int(x1) + 2)):
                if test(x, y):
                    s.px(x, y, m, g)

    def ell(s, cx, cy, rx, ry, m, g, cond=None):
        s.fill(lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0 and (cond is None or cond(x, y)),
               m, g, (cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1))

    def rect(s, x0, y0, x1, y1, m, g):
        s.zc += 1
        for y in range(int(round(y0)), int(round(y1)) + 1):
            for x in range(int(round(x0)), int(round(x1)) + 1):
                s.px(x, y, m, g)

    def line(s, x0, y0, x1, y1, wd, m, g, m2=None, split=1.0):
        """thick segment; the part after `split` (0..1 along it) uses m2"""
        s.zc += 1
        n = max(1, int(max(abs(x1 - x0), abs(y1 - y0)) * 2))
        for i in range(n + 1):
            t = i / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            mm = m2 if (m2 and t > split) else m
            r = (wd - 1) / 2
            for dy in np.arange(-r, r + 0.01, 1.0):
                for dx in np.arange(-r, r + 0.01, 1.0):
                    s.px(math.floor(x + dx + 0.5), math.floor(y + dy + 0.5), mm, g)

    def flip(s):
        s.mat, s.grp, s.z = s.mat[:, ::-1].copy(), s.grp[:, ::-1].copy(), s.z[:, ::-1].copy()

    def name(s, x, y):
        return MID.get(int(s.mat[y, x]))


# ------------------------------------------------------------------------------------------------ the rig
FACE = {0: (0, 1), 1: (0.7, 0.7), 2: (1, 0), 3: (0.7, -0.7), 4: (0, -1)}


def rig(st, h, d, pose):
    """paint the labelled parts for direction d (0 S, 1 SE, 2 E, 3 NE, 4 N, 5 NW, 6 W, 7 SW) and a pose dict"""
    d = (d + pose.get("dir", 0)) % 8
    mirror = d in (5, 6, 7)
    dd = 8 - d if mirror else d
    W, H = st["W"], st["H"]
    C = Canvas(W + 2 * M, H + M)
    ox, oy = M, M
    big = st["detail"] >= 2 or bool(st.get("cel"))
    if pose.get("ball") is not None:
        ball(C, st, h, pose, ox, oy)
    else:
        figure(C, st, h, dd, pose, ox, oy, big)
    if mirror:
        C.flip()
    if pose.get("lie"):
        lie(C, st)
    return C, dd, mirror


def figure(C, st, h, dd, pose, ox, oy, big):
    W, H = st["W"], st["H"]
    cel = bool(st.get("cel"))
    btop = min(y for (_, y) in st["cover"]["BODY"])  # first row the BODY layer covers
    wtop = min(y for (_, y) in st["cover"]["WEAR"])
    cx = ox + W / 2 - 0.5
    sq = pose.get("sq", 0)
    cr = pose.get("crouch", 0)
    lean = pose.get("lean", 0)
    up = pose.get("bob", 0) + cr + sq  # upper body drop
    size = h["size"] - 1  # -1 small, 0, +1 broad
    hcy, hrx, hry = st["head"]
    hcy = oy + hcy + up + pose.get("hdy", 0)
    hrx = hrx + 0.25 * sq
    hry = hry - 0.25 * sq
    hcx = cx + lean + pose.get("hdx", 0) + (0.5 if dd in (1, 2, 3) else 0)
    sh_y = oy + st["sh_y"] + up
    hip_y = oy + st["hip_y"] + up - (0 if cr < 2 else 1)
    foot_y = oy + st["foot_y"]
    sw = st["sw"] + 0.5 * sq + 0.5 * size - (0.6 if dd == 2 else 0.3 if dd in (1, 3) else 0)
    hw = st["hw"] + 0.4 * sq + 0.4 * size - (0.6 if dd == 2 else 0.3 if dd in (1, 3) else 0)
    tcx = cx + lean * 0.6
    side = dd in (1, 2, 3)
    back = dd in (3, 4)
    front = dd in (0, 1)
    fx, fy = FACE[dd]
    hair = h["hair"]
    look = h["look"]
    aw, lw = st["aw"], st["lw"]
    stride = pose.get("stride", 0)
    lift = pose.get("lift", 0)
    shoe_h = st["shoe_h"]
    hand_y = oy + st["hand_y"] + up - pose.get("arms_up", 0)
    hairline = hcy - hry * (0.22 if front else 0.0)
    # ---- long hair behind everything
    if hair == 2:
        if dd in (0, 1):
            C.rect(hcx - hrx - 0.4, hcy, hcx + hrx + 0.4, sh_y + 2.5, "hair", G_HAIRB)
        elif dd == 2:
            C.rect(hcx - hrx - 0.2, hcy - 1, hcx + 0.5, sh_y + 3.5, "hair", G_HAIRB)
    # ---- hood behind the head (neck gene 3)
    if h["neck"] == 3 and not back:
        C.ell(tcx - (1 if dd == 2 else 0), sh_y - 0.6, sw + 0.6, 2.0, "top", G_TORSO)
    # ---- far limbs (side views)
    legx = lambda s: tcx + s * (st["gap"] / 2 + lw / 2)  # noqa: E731
    arm_x = lambda s: tcx + s * (sw + aw / 2 - 0.4)  # noqa: E731
    sleeve = 0.55 if big else 0.6
    hands = pose.get("hands", {})

    def arm(sgn, g, swing=0.0):
        key = "n" if sgn > 0 else "f"
        hx, hy = hands.get(key, (0, 0))
        x0, y0 = arm_x(sgn) if not side else tcx + (0.3 if sgn > 0 else -0.8), sh_y + 0.8
        if side:
            x1, y1 = x0 + swing + hx, hand_y + hy
        else:
            x1, y1 = x0 + sgn * 0.3 + hx * sgn, hand_y + hy - abs(swing) * 0.4
        if hy <= -9:  # "over the head": reach above the crown whatever the head size
            x1 = x0 + (sgn * 1.2 if not side else hx * 0.5 + 1)
            y1 = hcy - hry - 0.5 + (hy + 9)
        C.line(x0, y0, x1, y1, aw, "sleeve", g, "skin", sleeve)
        if big:
            t = sleeve + 0.02
            C.px(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, "cuff", g)
        C.ell(x1, y1 + 0.3, 1.0, 1.0, "skin", g)

    sole = "sole" if st.get("neon_sole") else "shoe"

    def leg(sgn, g, sg, off=0.0, lf=0):
        top = hip_y - 0.5
        bot = foot_y - shoe_h + 0.5 - lf - (cr * 0.5)
        x0 = legx(sgn) if not side else tcx + sgn * 0.6
        x1 = x0 + off
        if cr >= 2 and not side:
            x1 += sgn * 1
        C.line(x0, top, x1, bot, lw, "pants", g)
        # shoe
        sy = foot_y - shoe_h + 1 - lf
        if side:
            C.rect(x1 - 1.2, sy, x1 + 1.8, foot_y - lf, "shoe", sg)
            C.rect(x1 - 1.2, foot_y - lf, x1 + 1.8, foot_y - lf, sole, sg)
        else:
            C.rect(x1 - lw / 2 + (0 if sgn < 0 else 0.1) - (0.4 if sgn < 0 else 0),
                   sy, x1 + lw / 2 + (0.4 if sgn > 0 else 0), foot_y - lf, "shoe", sg)
            C.rect(x1 - lw / 2 - (0.4 if sgn < 0 else 0), foot_y - lf, x1 + lw / 2 + (0.4 if sgn > 0 else 0),
                   foot_y - lf, sole, sg)

    if side:
        arm(-1, G_ARMF, -pose.get("swing", 0))
        leg(-1, G_LEGF, G_SHOEF, -stride * (1 if dd == 2 else 0.5), max(0, -lift))
        leg(1, G_LEGN, G_SHOEN, stride * (1 if dd == 2 else 0.5), max(0, lift))
    else:
        leg(-1, G_LEGF, G_SHOEF, -stride * 0.3, max(0, -lift))
        leg(1, G_LEGN, G_SHOEN, stride * 0.3, max(0, lift))
    # ---- skirt / long shirt hem (look gene)
    if look == 2:
        C.fill(lambda x, y: abs(x - tcx) <= hw + 0.4 + (y - hip_y) * 0.5, "top", G_SKIRT,
               (tcx - hw - 3, hip_y - 1, tcx + hw + 3, hip_y + (2.5 if not big else 3.5)))
    # ---- torso
    def tw(y):
        t = (y - sh_y) / max(1, hip_y - sh_y)
        return sw + (hw - sw) * t

    hem = hip_y + (1.0 if look == 0 else 0)
    C.fill(lambda x, y: sh_y - 0.5 <= y <= hem and abs(x - tcx) <= tw(min(y, hip_y)) - (0.5 if y < sh_y + 0.6 else 0),
           "top", G_TORSO, (tcx - sw - 2, sh_y - 1, tcx + sw + 2, hem + 1))
    # fabric pattern (pattern gene) on the top
    pat = h["pat"]
    tz = C.zc

    def pat_on(x, y):
        X, Y = x - ox, y - oy
        return [False, Y % 3 == 0, X % 3 == 0, ((X >> 1) + (Y >> 1)) % 2 == 0 and (X + Y) % 2 == 0,
                X % 3 == 1 and Y % 3 == 1, (X + Y) % 4 == 0, (abs(2 * X - 15) // 2 + Y) % 4 == 0, X < W // 2][pat]

    for y in range(C.h):
        for x in range(C.w):
            if C.z[y, x] == tz and C.name(x, y) == "top" and pat_on(x, y) and y > sh_y + 0.5:
                C.mat[y, x] = MAT["topP"]["id"]
    # belt (detailed style)
    if big and look != 0:
        C.rect(tcx - hw + 0.5, hip_y, tcx + hw - 0.5, hip_y, "belt", G_TORSO)
        C.px(tcx + (1 if side else 0), hip_y, "buckle", G_TORSO)
    # collar (neck gene) + lanyard and badge (front)
    nk = h["neck"]
    if front:
        c0 = tcx + (0.6 if dd == 1 else 0)
        if nk == 0:
            C.rect(c0 - 0.5, sh_y - 0.5, c0 + 0.5, sh_y - 0.5, "skin", G_TORSO)
        elif nk == 1:
            for k in range(3 if big else 2):
                C.rect(c0 - 1 + k * 0.5, sh_y - 0.5 + k, c0 + 1 - k * 0.5, sh_y - 0.5 + k, "skin" if cel else "shirt",
                       G_TORSO)
        elif nk == 2:
            C.rect(c0 - 1.5, sh_y - 1.4, c0 + 1.5, sh_y - 0.6, "top", G_TORSO)
        if big and not cel:  # open jacket: shirt strip down the middle, lapels
            C.rect(c0 - 0.5, sh_y + 0.5, c0 + 0.5, hip_y - 1, "shirt", G_TORSO)
            C.px(c0 - 1.5, sh_y + 0.5, "topP", G_TORSO)
            C.px(c0 + 1.5, sh_y + 0.5, "topP", G_TORSO)
        elif cel:  # jacket: the zip / opening line under the badge, down to the hem
            C.rect(c0 + 0.5, sh_y + (hip_y - sh_y) * 0.55 + 1.5, c0 + 0.5, hip_y + (1 if look == 0 else -0.5), "seam",
                   G_TORSO)
        if not pose.get("badge_up"):
            by = sh_y + (hip_y - sh_y) * 0.55
            C.line(c0 - 1.5, sh_y - 0.3, c0 - 0.5, by - 1, 1, "lan", G_TORSO)
            C.line(c0 + 1.5, sh_y - 0.3, c0 + 0.5, by - 1, 1, "lan", G_TORSO)
            C.rect(c0 - 0.5, by - 0.5, c0 + 0.5, by + (1.5 if big and not cel else 0.5), "badge", G_TORSO)
            if big and not cel:
                C.px(c0, by + 1.5, "ink", G_TORSO)
    elif dd == 2:
        if not pose.get("badge_up"):
            by = sh_y + (hip_y - sh_y) * 0.5
            C.line(tcx + 0.5, sh_y, tcx + sw - 1, by, 1, "lan", G_TORSO)
            C.px(tcx + sw - 0.6, by + 0.5, "badge", G_TORSO)
        if cel:  # side: the hem line of the jacket
            C.rect(tcx - hw + 0.5, hip_y, tcx + hw - 0.5, hip_y, "seam", G_TORSO)
    else:  # back: the lanyard cord behind the neck
        C.rect(tcx - 1.5, sh_y - 0.2, tcx + 1.5, sh_y - 0.2, "lan", G_TORSO)
    if h["mark"] == 6:  # backpack
        if back:
            C.rect(tcx - 2.5, sh_y + 1, tcx + 2.5, hip_y - 0.5, "accD", G_TORSO)
            C.rect(tcx - 1.5, sh_y + 2, tcx + 1.5, sh_y + 2, "acc", G_TORSO)
        elif front:
            C.rect(tcx - sw + 1, sh_y, tcx - sw + 1, hip_y - 1.5, "accD", G_TORSO)
            C.rect(tcx + sw - 1, sh_y, tcx + sw - 1, hip_y - 1.5, "accD", G_TORSO)
        else:
            C.rect(tcx - sw - 1, sh_y + 1, tcx - sw + 0.5, hip_y - 1, "accD", G_TORSO)
    if h["mark"] == 3:  # scarf
        C.rect(tcx - sw + 0.5, sh_y - 0.6, tcx + sw - 0.5, sh_y + 0.4, "acc", G_TORSO)
        if not back:
            C.rect(tcx + (1 if front else -1), sh_y + 1, tcx + (2 if front else 0), sh_y + 2.5, "acc", G_TORSO)
    # ---- arms (an arm raised above the shoulder is painted over the head, later)
    later = []
    raised = lambda k: hands.get(k, (0, 0))[1] <= -5  # noqa: E731
    if side:
        (later if raised("n") else []).append((1, G_ARMN, pose.get("swing", 0)))
        if not raised("n"):
            arm(1, G_ARMN, pose.get("swing", 0))
    else:
        sw_ = pose.get("swing", 0)
        for sg, g, k, swv in ((-1, G_ARMF, "f", sw_), (1, G_ARMN, "n", -sw_)):
            if raised(k) and not back:
                later.append((sg, g, swv))
            else:
                arm(sg, g, swv)
    # ---- head
    top = hcy - hry
    ey = int(math.floor(hcy + hry * st["eye_y"]))
    if hair in (0, 1, 4) and not back:
        if front:
            for sg in (-1, 1):
                C.ell(hcx + sg * (hrx + 0.1), ey + 1, 0.9, 1.3, "skin", G_HEAD)
        elif dd == 2:
            pass
    C.ell(hcx, hcy, hrx, hry, "skin", G_HEAD)
    # hair
    if hair:
        rx2, ry2 = hrx + 0.55, hry + 0.5

        def in_cap(x, y):
            if ((x - hcx) / rx2) ** 2 + ((y - hcy + 0.3) / ry2) ** 2 > 1:
                return False
            X = x - ox
            if dd == 4:
                return y <= hcy + hry * (0.75 if hair != 4 else 0.55)
            if dd == 3:
                return not (x > hcx + hrx * 0.45 and y > hcy - hry * 0.1) and y <= hcy + hry * 0.7
            if dd == 2:
                back_ = x <= hcx - 0.6 and y <= hcy + hry * (0.6 if hair != 4 else 0.35)
                return y <= hcy - hry * 0.35 + (1 if (X % 3 == 0 and x < hcx + hrx - 2) else 0) or back_
            # front / SE: cap + fringe + side locks
            jag = 1 if (X * 5 + h["seed"]) % 3 == 0 else 0
            if y <= hairline + jag - (0 if hair != 4 else 0.5):
                return True
            lock = hrx - (1.6 if hair in (2, 3) else 1.1)
            low = hcy + hry * (0.5 if hair == 2 else 0.15 if hair == 3 else -0.05)
            if dd == 1:
                return (x - hcx) < -lock and y <= low
            return abs(x - hcx) > lock and y <= low

        C.fill(in_cap, "hair", G_HEAD, (hcx - rx2 - 1, hcy - ry2 - 1, hcx + rx2 + 1, hcy + ry2 + 1))
        if hair == 2 and dd in (0, 1):  # front strands over the shoulders
            C.rect(hcx - hrx - 0.4, hcy, hcx - hrx + 0.4, sh_y + 0.5, "hair", G_HAIRB)
            if dd == 0:
                C.rect(hcx + hrx - 0.4, hcy, hcx + hrx + 0.4, sh_y + 0.5, "hair", G_HAIRB)
        if hair == 2 and dd in (3, 4):
            C.rect(hcx - hrx + 0.2, hcy, hcx + hrx - 0.2, sh_y + 2.5, "hair", G_HEAD)
            for x in range(int(hcx - hrx + 1), int(hcx + hrx)):  # dithered ends
                if x % 2 == 0:
                    C.px(x, sh_y + 2.5, "hairM", G_HEAD)
        if hair == 3:  # bun
            bx = hcx - (hrx * 0.55 if side else 0)
            C.ell(bx, top + 1.3, 2.7, 1.9, "hair", G_HEAD)
        if hair == 4:  # spikes, swept back
            for k in range(-2, 3):
                sx = hcx + k * 2.4 - (1 if side else 0)
                C.line(sx, top + 2, sx - 1 - (1 if side else 0), max(oy + btop - 0.4, top - 1.0 + abs(k) * 0.4), 2, "hair",
                       G_HEAD)
        # highlight streak: a short "/" sheen on the upper-left of the hair (2 mid + 1 light pixel)
        if not cel:
            sx0 = int(math.floor(hcx - hrx * (0.62 if dd in (0, 4) else 0.3)))
            sy0 = int(math.floor(top + 2.6))
            for k in range(3 if st["detail"] < 2 else 4):
                X, Y = sx0 + k, sy0 - k
                if 0 <= X < C.w and 0 <= Y < C.h and C.name(X, Y) == "hair":
                    C.mat[Y, X] = MAT["hairS"]["id"]
            for (X, Y) in [(sx0 + k + dx, sy0 - k + dy) for k in range(4) for dx, dy in ((1, 0), (0, 1), (1, 1))]:
                if 0 <= X < C.w and 0 <= Y < C.h and C.name(X, Y) == "hair":  # dithered mid-tone under the sheen
                    C.mat[Y, X] = MAT["hairM"]["id"]
    # ---- face
    eyes = pose.get("eyes", "open")
    gx = pose.get("gaze", 0)
    cxi = int(math.floor(hcx + 0.5))  # pixel just right of the head centre
    eh = st["eh"]
    if h["mark"] == 4:  # beanie
        mat = "acc" if st["detail"] >= 2 else "hatB"
        C.fill(lambda x, y: ((x - hcx) / (hrx + 0.7)) ** 2 + ((y - hcy + 0.4) / (hry + 0.6)) ** 2 <= 1
               and y <= hcy - hry * 0.42, mat, G_HEAD, (hcx - hrx - 2, top - 2, hcx + hrx + 2, hcy))
        C.rect(hcx - hrx + 0.5, hcy - hry * 0.42, hcx + hrx - 0.5, hcy - hry * 0.42, "accD" if st["detail"] >= 2 else "ink", G_HEAD)
        C.ell(hcx - 1, top - 0.8, 1.1, 1.0, mat, G_HEAD)
    if not back:
        if dd == 0:
            cols = [cxi - st["ex"] - 2 + gx, cxi + st["ex"] - 1 + gx + 1]
            if st["ex"] == 1:
                cols = [cxi - 3 + gx, cxi + 1 + gx]
            widths = [2, 2]
        elif dd == 1:
            cols = [cxi - st["ex"] - 1 + gx + 1, cxi + st["ex"] + gx + 1]
            widths = [2, 1]
        else:
            cols = [int(math.floor(hcx + hrx * 0.4)) + gx]
            widths = [1 if not st["big_eye"] else 2]
        for c0, wdt in zip(cols, widths):
            draw_eye(C, c0, ey, wdt, eh, eyes)
        my = ey + eh + (1 if st["big_eye"] else 1)
        mo = pose.get("mouth", "line")
        if dd == 0:
            mx = cxi - 1
            if mo == "o":
                C.rect(mx, my, mx + 1, my + 1, "ink", G_HEAD)
            elif mo == "yawn":
                C.rect(mx, my, mx + 1, my + 2, "ink", G_HEAD)
            elif mo != "none":
                C.rect(mx, my, mx + 1, my, "mouth", G_HEAD)
        elif dd == 1:
            C.px(cxi + 1, my, "ink" if mo in ("o", "yawn") else "mouth", G_HEAD)
        else:
            nx = int(math.floor(hcx + hrx + 0.4))
            if not cel or C.name(nx - 1, ey + 1) == "skin":
                C.px(nx, ey + 1, "skin", G_HEAD)  # nose (D: only where it touches the face, no stray pixel)
            mx = nx - 1
            while cel and mx > hcx and C.name(int(round(mx)), my) != "skin":
                mx -= 1  # D: keep the mouth on the face where the jaw narrows
            C.px(mx, my, "mouth", G_HEAD)
        if dd == 2 and hair in (0, 1, 4):  # ear on the side of the head
            C.ell(hcx - 0.8, ey + 0.8, 0.8, 1.2, "skin", G_HEAD)
            C.px(hcx - 0.8, ey + 1, "mouth", G_HEAD)
    # accessories on the head (mark gene)
    mk = h["mark"]
    if mk == 1 and not back:  # glasses: ink frame, lens glint in the rim colour
        if dd == 0:
            C.rect(cols[0] - 1, ey - 1, cols[1] + 2, ey - 1, "ink", G_HEAD)
            for c0 in cols:
                C.px(c0 + 1, ey, "eyeH", G_HEAD)
        elif dd == 1:
            C.rect(cols[0] - 1, ey - 1, cols[1] + 1, ey - 1, "ink", G_HEAD)
            C.px(cols[0] + 1, ey, "eyeH", G_HEAD)
        else:
            C.rect(cols[0] - 2, ey - 1, cols[0] + 1, ey - 1, "ink", G_HEAD)
            C.px(cols[0] + 1, ey, "lan", G_HEAD)
    if mk == 2:  # headphones: band over the top, cups in the rim colour
        C.fill(lambda x, y: abs(((x - hcx) / (hrx + 0.9)) ** 2 + ((y - hcy) / (hry + 0.9)) ** 2 - 1) < 0.22
               and y < hcy - hry * 0.45, "accD", G_HEAD, (hcx - hrx - 2, top - 2, hcx + hrx + 2, ey))
        if front:
            for sg in (-1, 1):
                C.rect(hcx + sg * hrx - 0.5, ey - 1, hcx + sg * hrx + 0.5, ey + 1, "acc", G_HEAD)
        elif dd == 2:
            C.rect(hcx - 1.5, ey - 1, hcx - 0.5, ey + 1.5, "acc", G_HEAD)
        else:
            for sg in (-1, 1):
                C.rect(hcx + sg * hrx - 0.5, ey - 1, hcx + sg * hrx + 0.5, ey + 1, "acc", G_HEAD)
    if mk == 5 and not back:  # earring
        exx = hcx - hrx - 0.3 if dd == 0 else hcx - 0.8
        C.px(exx, ey + 3, "lan", G_HEAD)
    if mk == 7:  # visor shades pushed up on the hair
        vy = max(int(math.floor(hairline)) - 1, oy + wtop)  # a WEAR colour needs a WEAR cell under it
        if dd in (0, 1):
            C.rect(hcx - hrx + 0.5, vy, hcx + hrx - 0.5, vy, "acc", G_HEAD)
            C.rect(hcx - hrx + 1.5, vy + 1, hcx + hrx - 1.5, vy + 1, "accD", G_HEAD)
        elif dd == 2:
            C.rect(hcx - 1, vy - 1, hcx + hrx + 0.4, vy - 1, "acc", G_HEAD)
            C.rect(hcx, vy, hcx + hrx + 0.4, vy, "accD", G_HEAD)
        else:
            C.rect(hcx - hrx + 0.5, vy - 1, hcx + hrx - 0.5, vy - 1, "accD", G_HEAD)
    for (sg, g, swv) in later:
        arm(sg, g, swv)
    # ---- held item / weapon
    if pose.get("badge_up"):
        hx, hy = arm_x(1) - 1.5, sh_y - 1.5
        C.rect(hx - 1, hy - 1, hx, hy + 0.5, "badge", G_TORSO)
        C.line(hx, hy + 1, tcx, sh_y, 1, "lan", G_TORSO)
    wp = pose.get("weapon")
    if wp:
        weapon(C, h, dd, wp, tcx, sh_y, hand_y, sw, pose)
    if pose.get("item"):
        C.zc += 1
        hy = hcy - hry - 3
        it = pose["item"]
        if it == "card":
            C.rect(hcx - 1.5, hy - 1, hcx + 1.5, hy + 1, "fxL", G_FX)
            C.rect(hcx - 1.5, hy - 1, hcx + 1.5, hy - 1, "fxR", G_FX)
        else:
            C.ell(hcx, hy, 2, 1.6, "fxL", G_FX)
            C.px(hcx - 1, hy - 1, "fxR", G_FX)


def draw_eye(C, c0, ey, wdt, eh, eyes):
    if eyes == "closed":
        for k in range(wdt + (1 if wdt == 1 else 0)):
            C.px(c0 + k - (1 if wdt == 1 else 0) * 0, ey + eh - 1, "ink", G_HEAD)
        return
    if eyes == "x":
        C.px(c0, ey, "ink", G_HEAD)
        C.px(c0 + 1, ey + 1, "ink", G_HEAD)
        C.px(c0 + 1, ey, "ink", G_HEAD) if wdt > 1 else None
        C.px(c0, ey + 1, "ink", G_HEAD)
        return
    if eyes == "half":
        for k in range(wdt):
            C.px(c0 + k, ey + eh - 1, "eye", G_HEAD)
        return
    for yy in range(eh):
        for k in range(wdt):
            C.px(c0 + k, ey + yy, "eye", G_HEAD)
    if eyes != "dull":
        C.px(c0 + (0 if wdt > 1 else 0), ey, "eyeH", G_HEAD)  # the catch-light, top-left (key light)


WPN_LEN = {"BLADE": 6, "HAMMER": 5, "WHIP": 9, "GAUNTLET": 2, "LAUNCHER": 5}


def weapon(C, h, dd, wp, tcx, sh_y, hand_y, sw, pose):
    """the heir's weapon (weapon gene) at the near hand; wp in held / raise / swing / thrust"""
    fx, fy = FACE[dd]
    kind = h["weapon"]
    hx = tcx + (sw + 0.6) * (1 if dd != 4 else -1) if dd in (0, 4) else tcx + fx * 2.5
    hy = hand_y
    if wp == "raise":
        ang = math.atan2(fy, fx) + math.pi * 0.9
    elif wp == "held":
        ang = math.atan2(1, fx * 0.5) if dd != 4 else -math.pi / 2
    else:
        ang = math.atan2(fy, fx)
    n = WPN_LEN[kind]
    ex, ey = hx + math.cos(ang) * n, hy + math.sin(ang) * n
    C.zc += 1
    if kind == "BLADE":
        C.line(hx, hy, hx + math.cos(ang), hy + math.sin(ang), 1, "accD", G_WPN)
        C.line(hx + math.cos(ang) * 1.5, hy + math.sin(ang) * 1.5, ex, ey, 1, "wpn", G_WPN, "wpnE", 0.6)
    elif kind == "HAMMER":
        C.line(hx, hy, ex, ey, 1, "accD", G_WPN)
        C.rect(ex - 1.5, ey - 1.5, ex + 1.5, ey + 1.5, "wpn", G_WPN)
        C.px(ex + math.cos(ang) * 1.5, ey + math.sin(ang) * 1.5, "wpnE", G_WPN)
    elif kind == "WHIP":
        px_ = None
        for i in range(n * 3 + 1):
            t = i / (n * 3)
            a = ang + math.sin(t * 5) * 0.45 * t
            q = (int(round(hx + math.cos(a) * n * t)), int(round(hy + math.sin(a) * n * t)))
            if q != px_:
                C.px(q[0], q[1], "wpnE" if t > 0.25 else "accD", G_WPN)
            px_ = q
    elif kind == "GAUNTLET":
        C.rect(hx - 1, hy - 1, hx + 1.5, hy + 1.5, "wpn", G_WPN)
        C.px(hx + fx * 2, hy + fy * 2, "wpnE", G_WPN)
    else:  # launcher
        C.line(hx - math.cos(ang), hy - math.sin(ang), ex, ey, 2, "wpn", G_WPN)
        C.px(ex, ey, "wpnE", G_WPN)
    if wp in ("swing", "thrust"):  # the smear: a clean 2px arc in front, solid rim outside, dither inside
        r = n + 2
        base = math.atan2(fy, fx)
        span_ = 0.35 if wp == "thrust" else 1.1
        for i in range(40):
            a = base - span_ + 2 * span_ * i / 39
            for rr, m in ((r, "fxR"), (r - 1, "fxD")):
                x, y = int(round(hx + math.cos(a) * rr)), int(round(hy + math.sin(a) * rr * 0.8 - 1))
                if 0 <= x < C.w and 0 <= y < C.h and not C.mat[y, x]:
                    C.px(x, y, m, G_FX)


def ball(C, st, h, pose, ox, oy):
    """the roll: a tucked ball (hair, back, knees, shoes), rotated a quarter per frame"""
    W, H = st["W"], st["H"]
    r = 6.2 if st["H"] < 22 else 7.0 if st["H"] < 30 else 7.4
    cx, cy = ox + W / 2 - 0.5, oy + H - r - 1 + pose.get("bob", 0)
    sub = Canvas(16, 16)
    c = 7.5
    sub.ell(c, c, r, r, "top", G_TORSO)
    sub.ell(c - 1.8, c - 1.8, r * 0.62, r * 0.62, "hair" if h["hair"] else "skin", G_HEAD)
    sub.ell(c + 2.5, c + 2.2, 2.4, 2.0, "pants", G_LEGN)
    sub.rect(c + 3.5, c - 1, c + 5.5, c + 1.5, "shoe", G_SHOEN)
    sub.rect(c - 0.5, c + 1.5, c + 1.5, c + 4.5, "skin", G_ARMN)
    sub.rect(c - 5, c + 1, c - 4, c + 1, "lan", G_TORSO)
    q = pose["ball"] % 4
    for k in ("mat", "grp", "z"):
        setattr(sub, k, np.rot90(getattr(sub, k), -q).copy())
    for y in range(16):
        for x in range(16):
            if sub.mat[y, x]:
                X, Y = int(cx - 7.5 + x), int(cy - 7.5 + y)
                if 0 <= X < C.w and 0 <= Y < C.h:
                    C.mat[Y, X], C.grp[Y, X], C.z[Y, X] = sub.mat[y, x], sub.grp[y, x], sub.z[y, x]


def lie(C, st):
    """lying on the floor: the standing rig rotated a quarter turn, feet to the right (uses a 3-wide layout)"""
    W, H = st["W"], st["H"]
    box = (M, M, M + W, M + H)
    sub = {k: getattr(C, k)[box[1]:box[3], box[0]:box[2]].copy() for k in ("mat", "grp", "z")}
    for k in ("mat", "grp", "z"):
        getattr(C, k)[:] = 0
        r = np.rot90(sub[k], -1)  # head to the left
        rh, rw = r.shape
        y0 = M + H - rh
        x0 = M + W // 2 - rw // 2
        getattr(C, k)[y0:y0 + rh, max(0, x0):x0 + rw] = r[:, max(0, -x0):]


# ------------------------------------------------------------------------------------------------ shading
def shade(C, st, pose, mirror):
    """material + light -> colour slot per pixel (-1 empty); outline, inner lines and rim"""
    hgt, wdt = C.mat.shape
    col = -np.ones((hgt, wdt), np.int16)
    boxes = {}
    for g in np.unique(C.grp):
        if g == 0:
            continue
        ys, xs = np.nonzero(C.grp == g)
        boxes[g] = (xs.min(), ys.min(), xs.max(), ys.max())
    occ = C.mat > 0
    solid = occ.copy()
    for y in range(hgt):
        for x in range(wdt):
            if occ[y, x] and C.name(x, y) in ("fxR", "fxL", "fxD"):
                solid[y, x] = False

    def S(x, y):
        return 0 <= x < wdt and 0 <= y < hgt and solid[y, x]

    dbias = [0, 0.04, -0.04, 0.08, -0.08, 0.0][min(5, st.get("_dith", 0))]
    if st.get("cel"):
        col = cel_pass(C, st, occ, solid, S)
    for y in range(hgt):
        for x in range(wdt):
            if not occ[y, x] or st.get("cel"):
                continue
            m = MAT[C.name(x, y)]
            g = C.grp[y, x]
            x0, y0, x1, y1 = boxes[g]
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            rx, ry = max(1, (x1 - x0) / 2 + 0.8), max(1, (y1 - y0) / 2 + 0.8)
            nx, ny = (x - cx) / rx, (y - cy) / ry
            if g in CYL:
                ny *= 0.45
            nz = math.sqrt(max(0.08, 1 - nx * nx - ny * ny))
            n = np.array([nx, ny, nz])
            L = 0.5 + 0.5 * float(n @ LIGHT) / float(np.linalg.norm(n)) + dbias
            if g in CYL:
                L -= (y - y0) / max(1, y1 - y0) * 0.12
            # coloured inner lines: a part in front, above or left, shades this pixel (light from the top left)
            for (dx, dy) in ((-1, 0), (0, -1), (1, 0)):
                X, Y = x + dx, y + dy
                if S(X, Y) and C.grp[Y, X] != g and C.z[Y, X] > C.z[y, x] and C.grp[Y, X] not in (G_FX,) \
                        and C.name(X, Y) not in ("lan", "badge", "eye", "eyeH", "mouth", "ink"):
                    L -= 0.32
                    break
            if not S(x - 1, y) or not S(x, y - 1):
                L += 0.08
            for (lmax, a, b, fr) in m["bands"]:
                if L < lmax:
                    col[y, x] = b if (fr and bayer(x, y, fr)) else a
                    break
    # outline: outer silhouette only (fx and kept details are not outlined)
    out = col.copy()
    W, H = st["W"], st["H"]
    for y in range(hgt):
        for x in range(wdt):
            if not solid[y, x]:
                continue
            hx_ = not (S(x - 1, y) and S(x + 1, y))
            vy_ = not (S(x, y - 1) and S(x, y + 1))
            run_h = 1 + sum(1 for _ in iter(lambda k=[x]: (k.__setitem__(0, k[0] + 1) or S(k[0], y)), False))
            run_h += sum(1 for _ in iter(lambda k=[x]: (k.__setitem__(0, k[0] - 1) or S(k[0], y)), False))
            run_v = 1 + sum(1 for _ in iter(lambda k=[y]: (k.__setitem__(0, k[0] + 1) or S(x, k[0])), False))
            run_v += sum(1 for _ in iter(lambda k=[y]: (k.__setitem__(0, k[0] - 1) or S(x, k[0])), False))
            edge = (hx_ and run_h >= 3) or (vy_ and run_v >= 3)  # thin (1-2 px) parts keep their shading
            if not edge:
                continue
            m = MAT[C.name(x, y)]
            if m["keep"] and C.name(x, y) not in ("lan", "badge"):
                continue
            out[y, x] = W1 if m["wear"] else B1
    col = out
    # rim light in the biome accent, on the side away from the key light (right / lower right)
    mode = st["rim"]
    if mode:
        rim = col.copy()
        wear_top = min(y for (_, y) in st["cover"]["WEAR"])
        ytop = M + max(wear_top, int(st["H"] * 0.45) if mode in ("soft", "cel") else 0)

        def cand(x, y):  # the column just inside (or on) the right edge of the silhouette
            if not S(x, y) or y >= M + H - 1 or y < ytop:
                return False
            if C.name(x, y) in ("eye", "eyeH", "sole", "badge"):
                return False
            if C.grp[y, x] == G_WPN or any(S(k, y) for k in range(x + 2, min(wdt, M + st["W"] + 1))):
                return False  # only the outermost right edge of the whole figure
            if C.grp[y, x] not in RIM_GROUPS[mode]:
                return False
            if mode == "cel" and (not MAT[C.name(x, y)]["wear"] or C.name(x, y) in ("lan", "buckle", "seam")):
                return False  # cel: a thin biome edge on the cloth only, never on skin
            if mode == "strong":
                return not S(x + 1, y)
            return S(x + 1, y) and not S(x + 2, y)

        mark = np.zeros_like(solid)
        for y in range(hgt):
            for x in range(wdt):
                mark[y, x] = cand(x, y)
        for y in range(hgt):
            for x in range(wdt):
                if not mark[y, x]:
                    continue
                run = (y > 0 and (mark[y - 1, x] or mark[y - 1, max(0, x - 1)] or mark[y - 1, min(wdt - 1, x + 1)])) or \
                      (y + 1 < hgt and (mark[y + 1, x] or mark[y + 1, max(0, x - 1)] or mark[y + 1, min(wdt - 1, x + 1)]))
                if run and mode == "cel" and st["cel"]["rim"] == "dith" and not bayer(x, y, 0.5):
                    continue
                if run:
                    rim[y, x] = W3
                    if mode == "strong" and S(x - 1, y) and y > M + st["H"] * 0.4 and bayer(x - 1, y, 0.25):
                        rim[y, x - 1] = W3
        col = rim
    # status / FX effects that work on colour slots
    dis = pose.get("dissolve", 0)
    if dis:
        for y in range(hgt):
            for x in range(wdt):
                if col[y, x] >= 0:
                    t = (BAYER[y & 3][(x + y // 4) & 3] + 0.5) / 16
                    if t < dis:
                        col[y, x] = -1
                    elif t < dis + 0.12:
                        col[y, x] = W3
    if pose.get("assemble") is not None:
        k = pose["assemble"]
        new = -np.ones_like(col)
        for y in range(hgt):
            band = (y - M) // 4
            off = int(round((1 - k) * (band + 1) * 3))
            for x in range(wdt):
                if col[y, x] >= 0 and 0 <= y - off < hgt:
                    if (1 - k) > 0.05 and (x + band) % 3 == 0 and k < 0.7:
                        continue
                    new[y - off, x] = col[y, x] if k > 0.3 or (x + y) % 2 else W3
        col = new
    if pose.get("glitch"):
        sh = pose["glitch"]
        for y in range(hgt):
            if (y // 2 + sh) % 5 == 0:
                col[y] = np.roll(col[y], 1 if sh % 2 else -1)
                for x in range(wdt):
                    if col[y, x] >= 0 and (x + y) % 3 == 0:
                        col[y, x] = W3
    if pose.get("charge"):
        lv = pose["charge"]
        cc = [W3, B3, W2][(lv - 1) % 3]
        for y in range(hgt):
            for x in range(wdt):
                if col[y, x] >= 0:
                    edge = any(not (0 <= x + dx < wdt and 0 <= y + dy < hgt) or col[y + dy, x + dx] < 0
                               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                    if edge and (lv >= 2 or bayer(x, y, 0.5)):
                        col[y, x] = cc
    for f in pose.get("fx", []):
        fx_paint(col, st, f)
    return col


# ------------------------------------------------------------------------------------------------ cel shading (D)
# per material: (shadow slot, lit slot); special cases below. Everything resolves to one of the 6 slots.
CEL_TONES = {
    "skin": (B3, B2), "top": (W1, W2), "sleeve": (W1, W2), "topP": (W1, W1), "pants": (W1, W2), "shoe": (W1, W1),
    "hair": (B1, B1), "hairM": (B1, B1), "hairS": (B3, B3), "hatB": (B1, B3), "acc": (W1, W2), "wpn": (W1, W2),
    "eye": (B1, B1), "eyeH": (W3, W3), "mouth": (B3, B3), "ink": (B1, B1), "lan": (W1, W1), "badge": (W3, W3),
    "belt": (W1, W1), "seam": (W1, W1), "cuff": (W1, W1), "sole": (W1, W1), "accD": (W1, W1), "buckle": (W3, W3),
    "shirt": (B3, B3), "wpnE": (W3, W3),
}
CEL_DETAIL = {"lan", "badge", "eye", "eyeH", "mouth", "ink", "seam", "buckle", "belt", "cuff"}  # cast no shadow
SHINY = {"acc", "wpn"}  # hard top-left highlight in the rim colour


def _run(mask, x, y, sx, k):
    """steps from (x, y) along (sx, k*sx-per-step) until leaving the mask (1 = the next pixel is already outside)"""
    hgt, wdt = mask.shape
    o = 1
    while o < 16:
        X, Y = x + sx * o, y + int(round(o * k)) * (1 if sx > 0 else -1)
        if not (0 <= X < wdt and 0 <= Y < hgt and mask[Y, X]):
            break
        o += 1
    return o


def cel_pass(C, st, occ, solid, S):
    """hard cel bands with designed shadow shapes (styles D1-D3).

    Each shading volume (head, torso, each arm, leg, shoe...) gets its shadow from its own silhouette: a pixel is in
    shadow when it is within `w` px of the volume's edge measured toward the bottom right (away from the key light), so
    the shadow follows the shape (a crescent on the head, a strip down the right of the torso and limbs, the bottom
    rows of the head). Between light and shadow sits a terminator band of `term` px in ordered Bayer dither (50 %, then
    25 %). On top of that: cast shadows where a part in front sits directly above a pixel (coloured inner lines),
    the hair fringe shadow on the forehead, a hard highlight band on the hair (one pixel in from its lit edge), a
    one-pixel glint on shiny gear, and (D2/D3) an ordered-dither cloth texture and hair midtone in the lit areas."""
    P = st["cel"]
    hgt, wdt = C.mat.shape
    col = -np.ones((hgt, wdt), np.int16)
    t = np.zeros((hgt, wdt))  # shadow fraction: 0 lit .. 1 shadow
    hi = np.zeros((hgt, wdt), bool)
    for g in np.unique(C.grp):
        if g == 0 or g == G_FX:
            continue
        mask = (C.grp == g) & solid
        ys, xs = np.nonzero(mask)
        if not len(xs):
            continue
        w = xs.max() - xs.min() + 1
        k = 0.0 if g in CYL else 0.5
        frac = P["sw"] * (0.3 if g == G_HEAD else 1.0)
        if g == G_HEAD:
            k = 0.35
        sw = max(1, int(round(w * frac))) + (1 if w >= 5 else 0)
        for y, x in zip(ys, xs):
            dn = _run(mask, x, y, 1, k)
            if dn <= sw:
                t[y, x] = 1.0
            elif w >= 5 and P["term"] >= 1 and dn == sw + 1:
                t[y, x] = 0.5
            elif w >= 7 and P["term"] >= 2 and dn == sw + 2:
                t[y, x] = 0.25
            if g == G_HEAD and C.name(x, y) in ("hair", "hairM"):  # the sheen: a band 1 px in from the lit edge
                up = _run(mask, x, y, -1, 0.5)
                if t[y, x] == 0 and y < ys.min() + (ys.max() - ys.min()) * 0.45 and x <= xs.min() + w * 0.6:
                    hi[y, x] = up == 2 or (P["hmid"] and up == 3 and bayer(x, y, P["hmid"]))
            elif C.name(x, y) in SHINY:
                hi[y, x] = _run(mask, x, y, -1, 1.0) == 1 and t[y, x] == 0
    for y in range(hgt):
        for x in range(wdt):
            if not occ[y, x]:
                continue
            nm = C.name(x, y)
            if nm not in CEL_TONES:  # FX: their own bands
                for (lmax, a, b, fr) in MAT[nm]["bands"]:
                    col[y, x] = b if (fr and bayer(x, y, fr)) else a
                    break
                continue
            g = C.grp[y, x]
            tt = t[y, x]
            if nm not in CEL_DETAIL:
                for (dx, dy) in ((0, -1),):  # cast shadow from a part in front, directly above (head on collar...)
                    X, Y = x + dx, y + dy
                    if S(X, Y) and C.grp[Y, X] not in (g, G_FX) and C.z[Y, X] > C.z[y, x] \
                            and C.name(X, Y) not in CEL_DETAIL:
                        tt = 1.0
                        break
                if nm == "skin" and y > 0 and C.name(x, y - 1) in ("hair", "hatB", "acc") and C.grp[y - 1, x] == g:
                    tt = max(tt, P["cast"])  # the fringe shades the forehead
            sh, lt = CEL_TONES[nm]
            if nm == "lan" and g == G_HEAD:
                sh = lt = W3  # earring, glasses lens: shiny
            c = sh if (tt >= 1 or (tt > 0 and bayer(x, y, tt))) else lt
            if c == lt:
                if nm in ("top", "sleeve") and P["tex"] and bayer(x + 2, y, P["tex"]):
                    c = W1  # ordered-dither cloth texture
                elif nm == "pants" and bayer(x, y, P["pants"]):
                    c = W1  # trousers a step darker than the jacket: an ordered-dither midtone
                elif nm == "hatB" and x % 3 == 0:
                    c = B1  # knit ribs
                elif nm == "shoe" and _run((C.grp == g) & solid, x, y, -1, 1.0) == 1:
                    c = W2  # toe cap catch-light
            if hi[y, x]:
                c = B3 if nm in ("hair", "hairM") else W3
            col[y, x] = c
    return col


def fx_paint(col, st, f):
    """FX sprites drawn straight in colour slots (they live on the WEAR palette)"""
    W, H = st["W"], st["H"]
    hy = M + 1
    cxp = M + W // 2

    def P_(x, y, c):
        if 0 <= x < col.shape[1] and 0 <= y < col.shape[0]:
            col[y, x] = c

    name, _, arg = f.partition(":")
    k = int(arg) if arg else 0
    if name == "zzz":
        for i, (zx, zy, s) in enumerate([(cxp + 5, hy - 2, 3), (cxp + 8, hy - 6, 2)]):
            if (i + k) % 2 == 0 or i == 0:
                for t in range(s):
                    P_(zx + t, zy, W3)
                    P_(zx + t, zy + s - 1, W3)
                    P_(zx + s - 1 - t, zy + t, W3)
    elif name == "stars":
        for i in range(3):
            a = k * 0.9 + i * 2.1
            x, y = int(cxp + math.cos(a) * 7), int(hy - 2 + math.sin(a) * 2)
            for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
                P_(x + dx, y + dy, W3 if (dx, dy) != (0, 0) else W2)
    elif name == "sweat":
        P_(cxp + 6, hy + 2 + k % 2, W3)
        P_(cxp + 6, hy + 3 + k % 2, W2)
    elif name == "dust":
        for i in range(3):
            x = M - 1 - i * 2 - k
            y = M + H - 2 - (i % 2)
            P_(x, y, W2)
            if i == 0:
                P_(x + 1, y, W2)
    elif name == "spark":
        x, y = M + W + 1, M + H // 2 - 2
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (2, 0), (-2, 0), (0, 2), (0, -2)):
            P_(x + dx, y + dy, W3 if abs(dx) + abs(dy) < 2 else W2)
    elif name == "ice":
        for i in range(4):
            x = M + 1 + i * 4
            y = M + H - 3 - (i % 2) * 2
            P_(x, y, W3)
            P_(x, y - 1, W2)
            P_(x + 1, y, W2)
    elif name == "wave":
        for i in range(3):
            P_(M + W + 1 + i, M + 2 - i + (k % 2), W3)
    elif name == "breath":
        for i in range(2):
            P_(M + W + i + k, M + 9 - i, W2)
    elif name == "warm":
        P_(M + W // 2 + (k % 3) - 1, M + 6, W3)
        P_(M + W // 2 + 1 - (k % 3), M + 4, W2)
    elif name == "shiver":
        for y in range(M, M + H):
            if (y + k) % 4 == 0:
                col[y] = np.roll(col[y], 1)
    elif name == "ring":
        for i in range(32):
            a = i / 32 * math.tau + k
            x, y = int(cxp + math.cos(a) * 11), int(M + H - 4 + math.sin(a) * 4)
            if i % 2 == 0:
                P_(x, y, W3)


# ------------------------------------------------------------------------------------------------ legality + cost
def cover_map(st, pose):
    """which layers cover which pixels (character box coordinates + margin)"""
    W, H = st["W"], st["H"]
    cov = {}
    cells = dict(st["cover"])
    if pose.get("lie"):  # lying needs a 3-wide arrangement (the only layout change)
        cells = {k: [(x, H - 16) for x in (-4, 4, 12)] for k in ("BODY", "WEAR")}
    for layer, cl in cells.items():
        m = np.zeros((H + M, W + 2 * M), bool)
        for (x, y) in cl:
            m[M + y:M + y + 16, M + x:M + x + 8] = True
        cov[layer] = m
    return cov, cells


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def legalize(col, st, pal, pose):
    cov, cells = cover_map(st, pose)
    fixes = 0
    out = col.copy()
    hgt, wdt = col.shape
    for y in range(hgt):
        for x in range(wdt):
            c = col[y, x]
            if c < 0:
                continue
            layer = "BODY" if c < 3 else "WEAR"
            if cov[layer][y, x]:
                continue
            inbox = M <= x < M + st["W"] and M <= y < M + st["H"]
            other = "WEAR" if layer == "BODY" else "BODY"
            if not inbox:  # FX/weapon sprites ride on the WEAR palette
                if layer == "WEAR":
                    continue
                other = "WEAR"
            elif not cov[other][y, x]:
                out[y, x] = -1
                fixes += 1
                continue
            rng = range(3, 6) if other == "WEAR" else range(0, 3)
            out[y, x] = min(rng, key=lambda k: abs(lum(pal[k]) - lum(pal[c])))
            fixes += 1
    return out, fixes, cells


def cost(col, st, cells):
    """sprites and sprites per line, counting only non-empty 8x16 cells (+ FX cells outside the box)"""
    W, H = st["W"], st["H"]
    used = []
    for layer, cl in cells.items():
        for (x, y) in cl:
            blk = col[M + y:M + y + 16, M + x:M + x + 8]
            sel = (blk >= 0) & ((blk < 3) if layer == "BODY" else (blk >= 3))
            if sel.any():
                used.append((layer, x, y))
    fx = 0
    box = np.zeros(col.shape, bool)
    for layer, cl in cells.items():
        for (x, y) in cl:
            box[M + y:M + y + 16, M + x:M + x + 8] = True
    fxpix = (col >= 0) & ~box
    fx_cells = set()
    for y, x in zip(*np.nonzero(fxpix)):
        fx_cells.add(((x - M) // 8, (y - M) // 16))
    fx = len(fx_cells)
    lines = [sum(1 for (_, x, y) in used if y <= r < y + 16) for r in range(H)]
    return {"sprites": len(used), "per_line": max(lines) if lines else 0, "fx": fx, "used": used}


def to_image(col, pal, flash=None):
    hgt, wdt = col.shape
    im = Image.new("RGBA", (wdt, hgt), (0, 0, 0, 0))
    pp = list(pal)
    if flash == "white":
        pp = [(150, 130, 200), (250, 246, 255), (255, 255, 255), (150, 130, 200), (250, 246, 255), (255, 255, 255)]
    elif flash == "red":
        pp = [(110, 20, 50), (255, 90, 110), (255, 200, 210), (110, 20, 50), (255, 90, 110), (255, 200, 210)]
    elif flash == "ice":
        pp = [(30, 60, 120), (150, 200, 240), (230, 248, 255), (40, 80, 150), (120, 180, 240), (240, 255, 255)]
    elif flash == "dim":
        pp = [tuple(int(v * 0.6) for v in c) for c in pal]
    px = im.load()
    for y in range(hgt):
        for x in range(wdt):
            c = col[y, x]
            if c >= 0:
                px[x, y] = (*pp[c], 255)
    return im


def render(st, h, d, pose=None, info=False):
    """one frame: RGBA (W+16)x(H+8) at 1x; with info=True also the cost record"""
    pose = pose or {}
    if isinstance(st, str):
        st = STYLES[st]
    st = dict(st, _dith=h["dith"])
    C, dd, mirror = rig(st, h, d, pose)
    pal = style_pal(st, h)
    col = shade(C, st, pose, mirror)
    col, fixes, cells = legalize(col, st, pal, pose)
    im = to_image(col, pal, pose.get("flash"))
    if info:
        return im, dict(cost(col, st, cells), fixes=fixes)
    return im


def layers(st, h, d, pose=None):
    """the two OBJ layers separately (BODY, WEAR) as 1x images, for the legality strip"""
    pose = pose or {}
    st = dict(STYLES[st] if isinstance(st, str) else st, _dith=h["dith"])
    C, dd, mirror = rig(st, h, d, pose)
    pal = style_pal(st, h)
    col, _, _ = legalize(shade(C, st, pose, mirror), st, pal, pose)
    b = np.where(col < 3, col, -1)
    w = np.where(col >= 3, col, -1)
    return to_image(b, pal), to_image(w, pal)


# ------------------------------------------------------------------------------------------------ CURRENT sprite
def current(h, which="S", ph=None, attack=False):
    import anim  # the existing 16x18 templates (tools/mock/anim.py)
    pal = [None, h["pal"][0], h["pal"][1], h["pal"][4]]
    im = anim.lib.sprite(anim.T[which], pal)
    if ph is not None:
        im = anim.walk(im, ph)
    if attack:
        im = anim.squash(im)
    return im


# ------------------------------------------------------------------------------------------------ poses used by the sheets
WALK = [dict(stride=2, lift=0, swing=2, sq=1), dict(stride=0, lift=1, swing=0, sq=-1),
        dict(stride=-2, lift=0, swing=-2, sq=1), dict(stride=0, lift=-1, swing=0, sq=-1)]
ATTACK = dict(weapon="swing", lean=1, sq=-1, hands={"n": (2, -2)})
SHEET_POSES = [("IDLE S", 0, {}), ("WALK E", 2, WALK[0]), ("ATTACK SE", 1, ATTACK)]


def big_text(im, x, y, s, col):
    text(im, x, y, s, col)


def crop_char(im, st):
    return im


SUBS = {"A": "DITHERED", "B": "NEON CHIBI", "C": "TAILORED", "D1": "HARD CEL", "D2": "CEL+DITHER", "D3": "BALANCED"}
RIM_NAMES = {"soft": "SOFT RIM", "strong": "NEON EDGE", "inner": "INNER RIM", "cel": "THIN RIM"}


def style_sheet(heirs, path, cols=("CURRENT", "A", "B", "C")):
    """CURRENT vs the styles on the same 4 heirs: idle front, walk side, attack; 1x strip and 4x"""
    cols = list(cols)
    cw = 3 * 34 + 6  # per style block at 1x: 3 frames of 32 wide
    rowh = 48
    head = 22
    W1x = 64 + len(cols) * cw
    H1x = head + len(heirs) * rowh + 70
    im = Image.new("RGB", (W1x, H1x), (20, 14, 46))
    for i, c in enumerate(cols):
        x0 = 64 + i * cw
        st = STYLES.get(c)
        label = "CURRENT 16X18" if c == "CURRENT" else f"{c}: {st['W']}X{st['H']}"
        text(im, x0, 2, label, PINK if c != "CURRENT" else (180, 160, 220))
        sub = "3 COL 1 PAL" if c == "CURRENT" else SUBS[c]
        text(im, x0, 11, sub, CYAN)
    costs = {}
    for r, h in enumerate(heirs):
        y0 = head + r * rowh
        panel(im, 0, y0, W1x - 1, y0 + rowh - 3, top=(46, 30, 92), bot=(28, 18, 60), rim=(70, 44, 130),
              under=(46, 30, 92))
        fc = h["face"].resize((24, 24), Image.NEAREST)
        sm = Image.new("RGBA", (24, 24), (0, 0, 0, 0))
        from egkit import small
        put(im, small(h["face"], 2), 2, y0 + 3)
        text(im, 2, y0 + 28, h["name"], WHITE)
        text(im, 2, y0 + 37, HAIRS[h["hair"]][:5], (150, 116, 210))
        for i, c in enumerate(cols):
            x0 = 64 + i * cw
            for k, (lab, d, pose) in enumerate(SHEET_POSES):
                fx = x0 + k * 34
                if c == "CURRENT":
                    spr = [current(h, "S"), current(h, "E", 1), current(h, "SE", attack=True)][k]
                    put(im, spr, fx + 8, y0 + rowh - 4 - spr.height)
                else:
                    spr, inf = render(c, h, d, pose, info=True)
                    st = STYLES[c]
                    put(im, spr, fx, y0 + rowh - 4 - (st["H"] + M))
                    costs.setdefault(c, []).append(inf)
                if r == 0:
                    pass
    y = head + len(heirs) * rowh + 2
    for i, c in enumerate(cols):
        x0 = 64 + i * cw
        for k, (lab, d, pose) in enumerate(SHEET_POSES):
            text(im, x0 + k * 34, y, lab.split()[0][:4], (150, 116, 210))
    y += 12
    lines = {"CURRENT": ["4 SPR 2/LINE", "1 PAL 3 COL", "NO SHADING"]}
    for c in cols[1:]:
        st = STYLES[c]
        sp = max(x["sprites"] for x in costs[c])
        pl = max(x["per_line"] for x in costs[c])
        lines[c] = [f"{sp} SPR {pl}/LINE", "2 PAL 6 COL", RIM_NAMES[st["rim"]]]
    for i, c in enumerate(cols):
        for j, s in enumerate(lines[c]):
            text(im, 64 + i * cw, y + j * 10, s, WHITE if j == 0 else (214, 190, 246))
    text(im, 2, y, "COST", YEL)
    text(im, 2, y + 30, "RIM = BIOME ACCENT (NOCLIP: CYAN). KEY LIGHT TOP LEFT.", (150, 116, 210))
    text(im, 2, y + 40, "6 COLOURS = BODY PAL + WEAR PAL, 2 SPRITE LAYERS.", (150, 116, 210))
    return im, costs


def state_strip(h, st="B"):
    """the heir state set (row per state, front + side), shown on the options sheet"""
    sys.path.insert(0, str(ROOT / "proto/anim"))
    import heirs as HM
    v = HM.variant_from_heir(h)
    sts = HM.states(v)
    names = HM.REQUIRED
    S = STYLES[st]
    fw, fh = S["W"] + 2 * M, S["H"] + M
    colsN = 2
    per = 6
    W_ = colsN * (60 + per * (fw + 1)) + 8
    rows = math.ceil(len(names) / colsN)
    im = Image.new("RGB", (W_, rows * (fh + 2) + 14), (20, 14, 46))
    text(im, 2, 2, f"HEIR STATES ({S['name']}), DIR SE: {len(names)} STATES, 8 DIRS EACH", PINK)
    for i, n in enumerate(names):
        cx = (i % colsN) * (60 + per * (fw + 1)) + 2
        cy = 14 + (i // colsN) * (fh + 2)
        text(im, cx, cy + fh // 2 - 4, n.upper().replace("FIDGET_", "F.").replace("CTX_", "C.").replace("LONG_", "L.")[:7], CYAN)
        fr = sts[n]["frames"]
        for k in range(min(per, len(fr))):
            put(im, HM.render(v, n, k, 1), cx + 58 + k * (fw + 1), cy)
    return im


def layer_strip(h, styles=("A", "B", "C")):
    """per style: BODY layer, WEAR layer and the composite, with the 8x16 OBJ grid (proves every pixel is legal)"""
    im = Image.new("RGB", (len(styles) * (3 * 34 + 12) + 4, 64), (20, 14, 46))
    text(im, 2, 2, "OBJ LAYERS (8X16 GRID): BODY PAL + WEAR PAL = 6 COLOURS", PINK)
    for i, c in enumerate(styles):
        st = STYLES[c]
        b, w = layers(c, h, 0)
        comp = render(c, h, 0)
        x0 = 2 + i * (3 * 34 + 12)
        text(im, x0, 12, c, YEL)
        for k, (lab, img) in enumerate((("BODY", b), ("WEAR", w), ("=", comp))):
            x = x0 + 10 + k * 33
            sub = Image.new("RGB", img.size, (46, 30, 92))
            sub.paste(img, (0, 0), img)
            d = ImageDraw.Draw(sub)
            if lab != "=":
                for (cx, cy) in st["cover"][lab]:
                    d.rectangle((M + cx, M + cy, M + cx + 7, M + cy + 15), outline=(120, 90, 180))
                sub.paste(img, (0, 0), img)
            im.paste(sub.crop((M - 2, M - 2, M + st["W"] + 2, M + st["H"] + 2)), (x, 22))
    return im


def check_legal(st, h, d, pose):
    """re-derive the layers from the final pixels: every pixel's colour must belong to a layer covering it"""
    st_ = dict(STYLES[st], _dith=h["dith"])
    C, dd, mirror = rig(st_, h, d, pose)
    col, _, cells = legalize(shade(C, st_, pose, mirror), st_, style_pal(st_, h), pose)
    cov, _ = cover_map(st_, pose)
    bad = 0
    for y in range(col.shape[0]):
        for x in range(col.shape[1]):
            c = col[y, x]
            if c < 0:
                continue
            inbox = M <= x < M + st_["W"] and M <= y < M + st_["H"]
            if inbox and not cov["BODY" if c < 3 else "WEAR"][y, x]:
                bad += 1
            if not inbox and c < 3:
                bad += 1
    return bad


def room_panel(h, st, other=None, label=""):
    """mock_room (tools/mock/mocks.py room) with this style's sprite placed in it"""
    im = void(5)
    text(im, 104, 4, "NOCLIP", (70, 40, 120), None)
    Mp = [" " * 10, " ###D##   ", " .F.V.... ", " .~~..... ", " .~~..... ", " ...P.... ", " ........ "]
    OFF = {6: 2, 7: 7, 8: 14}
    cells = []
    for r, row in enumerate(Mp):
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
        if ch == "V":
            dr = ImageDraw.Draw(im)
            dr.rectangle((x + 3, y + 3, x + 12, y + 12), fill=INK)
            for yy in range(y + 5, y + 12, 2):
                dr.line((x + 4, yy, x + 11, yy), fill=CYAN)
    for (r, c, ch, x, y, dy) in cells:
        if ch in "#D":
            wall(im, x, y)
        if ch == "D":
            dr = ImageDraw.Draw(im)
            dr.rectangle((x + 3, y - 12, x + 12, y + 8), fill=INK)
            dr.rectangle((x + 5, y - 10, x + 10, y + 8), fill=(40, 26, 86))
            dr.line((x + 7, y - 8, x + 7, y + 6), fill=PINK)
            dr.line((x + 8, y - 8, x + 8, y + 6), fill=WHITE)
    put(im, obj("FIRE"), 2 * 16, 2 * 16 - 6)
    put(im, obj("CRATE"), 6 * 16, 3 * 16 - 4 - 2)
    put(im, obj("MUG"), 7 * 16, 5 * 16 - 4 - 7)
    for (x, y) in [(18, 6), (31, 10), (150, 14), (128, 66)]:
        spark(im, x, y)
    feet = 5 * 16 - 6 + 18  # the current sprite's feet line
    if st == "CURRENT":
        spr = current(h, "S")
        put(im, spr, 64, feet - spr.height)
        hy = feet - spr.height
        if other:
            o = current(other, "E", 1)
            put(im, o, 22, feet + 12 - o.height)
    else:
        S = STYLES[st]
        spr = render(st, h, 0, {"hands": {"n": (0, -3), "f": (0, -3)}, "arms_up": 0})
        put(im, spr, 64 - M, feet - S["H"] - M)
        hy = feet - S["H"]
        if other:
            o = render(st, other, 2, WALK[0])
            put(im, o, 22 - M, feet + 12 - S["H"] - M)
    put(im, obj("WATER"), 64, hy - 14)
    tear(im, 58, 60, 3, 96, 160)
    hud(im, 2, 4, ("WATER",), 2, "2/5")
    text(im, 2, 120, label, YEL)
    return im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    hs = four_heirs()
    for h in hs:
        print(h["name"], "skin", h["skin"], HAIRS[h["hair"]], LOOKS[h["look"]], NECKS[h["neck"]], PATS[h["pat"]],
              "acc", h["accent"], MARKS[h["mark"]], h["weapon"], h["dodge"], h["traits"], "size", h["size"])
    sheet, costs = style_sheet(hs, OUT)
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(OUT / "sprite_styles_4x.png")
    # one combined file: 1x strip on top, 4x below is too wide; the deliverable: 1x + 2x + 4x crop
    S1 = sheet
    S4 = sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST)
    try:
        strip = state_strip(hs[1])
    except Exception as e:  # heirs.py not ready
        print("state strip skipped:", e)
        strip = None
    lay = layer_strip(hs[2])
    Wt = S4.width
    Ht = S1.height + 8 + S4.height + lay.height * 4 + 8 + (strip.height * 2 + 8 if strip else 0) + 10
    out = Image.new("RGB", (Wt, Ht), (20, 14, 46))
    out.paste(S1, (0, 0))
    text(out, S1.width + 8, 4, "<- 1X (REAL SIZE)", YEL)
    out.paste(S4, (0, S1.height + 8))
    y = S1.height + 8 + S4.height + 4
    out.paste(lay.resize((lay.width * 4, lay.height * 4), Image.NEAREST), (0, y))
    y += lay.height * 4 + 8
    if strip:
        s2 = strip.resize((strip.width * 2, strip.height * 2), Image.NEAREST)
        out.paste(s2, (0, y))
    out.save(OUT / "sprite_styles.png")
    (OUT / "sprite_styles_4x.png").unlink()
    # room
    panels = [room_panel(hs[1], s, hs[2], lab) for s, lab in
              (("CURRENT", "CURRENT"), ("A", "A"), ("B", "B"), ("C", "C"))]
    R = Image.new("RGB", (2 * 160 + 4, 2 * 144 + 4), (0, 0, 0))
    for i, p in enumerate(panels):
        R.paste(p, ((i % 2) * 164, (i // 2) * 148))
    R.resize((R.width * 3, R.height * 3), Image.NEAREST).save(OUT / "sprite_styles_room.png")
    bad = sum(check_legal(c, h, d, p) for c in "ABC" for h in hs for (_, d, p) in SHEET_POSES + [("", 0, {})])
    print("illegal pixels after layering:", bad)
    for c in "ABC":
        print(c, "sprites", max(x["sprites"] for x in costs[c]), "per line", max(x["per_line"] for x in costs[c]),
              "fx cells", max(x["fx"] for x in costs[c]), "fixes", sum(x["fixes"] for x in costs[c]))
    return hs


def skin_panel(heirs):
    """the BODY palette per heir: A (portrait ink, skin, skin light) vs D (ink, skin, hue-shifted skin shadow), each
    with the 50 % terminator dither the cel bands use between skin and its shadow"""
    im = Image.new("RGB", (160, 144), (20, 14, 46))
    text(im, 2, 2, "SKIN UNDER CEL", PINK)
    text(im, 2, 12, "A: INK SKIN LIGHT", (150, 116, 210))
    text(im, 2, 21, "D: INK SKIN SHADOW", CYAN)
    for i, h in enumerate(heirs):
        y = 34 + i * 27
        text(im, 2, y + 6, h["name"], WHITE)
        for row, st in enumerate(("A", "D1")):
            pal = style_pal(st, h)
            for k, c in enumerate(pal[:3]):
                ImageDraw.Draw(im).rectangle((44 + k * 14, y + row * 12, 44 + k * 14 + 12, y + row * 12 + 10), fill=c)
            if row:  # skin -> shadow terminator: lit | 50 % Bayer | shadow
                for yy in range(11):
                    for xx in range(36):
                        f = 0 if xx < 12 else 0.5 if xx < 24 else 1
                        im.putpixel((90 + xx, y + 12 + yy), pal[2] if bayer(xx, yy, f) else pal[1])
                # a few shadow pixels next to ink, as on a jaw line
                ImageDraw.Draw(im).rectangle((128, y + 12, 131, y + 22), fill=pal[0])
    return im


def main_d():
    """the D round (refinements of A): sprite_styles_D.png (1x sheet, 4x sheet, real-size rooms, layers) and
    sprite_styles_D_room.png (the room views at 3x). A stays as it is, next to D1-D3, so the gain is visible."""
    OUT.mkdir(parents=True, exist_ok=True)
    hs = four_heirs()
    by = {h["name"]: h for h in hs}
    cols = ("CURRENT", "A") + tuple(D_STYLES)
    sheet, costs = style_sheet(hs, OUT, cols)
    S4 = sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST)
    panels = [room_panel(by["OREN"], c, by["CORA"], c) for c in cols] + [skin_panel(hs)]
    R = Image.new("RGB", (3 * 164 - 4, 2 * 148 - 4), (0, 0, 0))
    for i, p_ in enumerate(panels):
        R.paste(p_, ((i % 3) * 164, (i // 3) * 148))
    R.resize((R.width * 3, R.height * 3), Image.NEAREST).save(OUT / "sprite_styles_D_room.png")
    lay = layer_strip(by["CORA"], ("A",) + tuple(D_STYLES))
    Wt = S4.width
    Ht = sheet.height + 8 + S4.height + 8 + 12 + R.height * 2 + 8 + lay.height * 4
    out = Image.new("RGB", (Wt, Ht), (20, 14, 46))
    out.paste(sheet, (0, 0))
    text(out, sheet.width + 8, 4, "<- 1X (REAL SIZE)", YEL)
    text(out, sheet.width + 8, 14, "D1-D3 = A + CEL BANDS + DITHER + C DETAIL", CYAN)
    out.paste(S4, (0, sheet.height + 8))
    y = sheet.height + 8 + S4.height + 8
    text(out, 2, y, "IN THE ROOM AT 1X (REAL SIZE) AND 2X: CURRENT, A, D1 / D2, D3, SKIN PALETTES", YEL)
    out.paste(R, (0, y + 12))
    out.paste(R.resize((R.width * 2, R.height * 2), Image.NEAREST), (R.width + 12, y + 12))
    y += 12 + R.height * 2 + 8
    out.paste(lay.resize((lay.width * 4, lay.height * 4), Image.NEAREST), (0, y))
    out.save(OUT / "sprite_styles_D.png")
    poses = SHEET_POSES + [("", 0, {})] + [(f"WALK {d}", d, WALK[k % 4]) for k, d in enumerate(range(8))]
    for c in ("A",) + tuple(D_STYLES):
        bad = sum(check_legal(c, h, d, p_) for h in hs for (_, d, p_) in poses)
        print(c, "sprites", max(x["sprites"] for x in costs[c]), "per line", max(x["per_line"] for x in costs[c]),
              "fx cells", max(x["fx"] for x in costs[c]), "fixes", sum(x["fixes"] for x in costs[c]),
              "illegal after layering", bad)
    for h in hs:
        print(h["name"], "BODY D", style_pal("D1", h)[:3], "A", h["pal"][:3])
    return hs


if __name__ == "__main__":
    main_d() if "--d" in sys.argv else main()
