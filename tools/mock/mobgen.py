"""EGRESS MOBGEN: procedural mob LOOKS (design-stage mock, structured to port to C).

Run from tools/mock:   python3 mobgen.py          (all sheets + counts + review stats)
                       python3 mobgen.py count    (counts only)

C-port shape:
- a look is a struct of small ints (LOOK_FIELDS); generation draws them from an xorshift32 in a fixed order, each from
  a list of choices that the same compatibility rules leave valid (allowed_*), so C walks the same tables;
- every part is data: body primitives in a 16-unit box, pixel templates ('1'..'9' codes, '.' clear), anchors;
- drawing is integer only (doubled-coordinate ellipses/polygons, Bayer dither), into an indexed canvas:
    0 clear | 1 2 3 = body palette A (OBJ pal n: dark/outline, mid, light)
            | 4 5 6 = overlay palette B (accessories + tag tells: ink, biome accent, tag cue)
            | 7 8 9 = elite palette E (OBJ pal 7: ink, affix 1, affix 2), cycled for the pulse
  so a mob is at most 3 visible colours of A, plus an overlay sprite of 3 colours of B (+ E when elite);
- palettes are biome ramp x element ramp, hue rotated by an integer channel mix, quantised to RGB555, then nudged
  until the outline reads on the light floor and the body reads on the dark void.
"""
from __future__ import annotations

import colorsys
import math
import sys
from itertools import combinations
from pathlib import Path

from PIL import Image

import egkit as E

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/shots"

# =====================================================================================================================
# RNG (xorshift32, as on the cart)
# =====================================================================================================================


class Rng:
    def __init__(self, seed):
        self.s = (seed & 0xFFFFFFFF) or 0x9E3779B9
        for _ in range(3):
            self.next()

    def next(self):
        s = self.s
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        self.s = s
        return s

    def pick(self, n):
        return self.next() % n

    def choice(self, seq):
        return seq[self.pick(len(seq))]


def mix_seed(*k):
    h = 2166136261
    for v in k:
        h = ((h ^ (v & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return h


# =====================================================================================================================
# part lists
# =====================================================================================================================
FAMILIES = ["OFFICE", "VENTS", "ATRIUM", "SERVER", "MALL", "GARDEN", "ROOFTOP", "GLITCH"]
FAM_LONG = ["OFFICE", "VENTS", "FLOODED ATRIUM", "SERVER FARM", "MALL FOOD COURT", "DREAM GARDEN", "ROOFTOP SKY",
            "GLITCH CORE"]
SIZES = [12, 16, 24, 32]
SIZE_NAME = ["S", "M", "L", "XL"]
LIMBS = ["NONE", "LEGS2", "LEGS4", "LEGS6", "ARMS", "TENTACLES", "WINGS", "WHEELS", "HOVER", "MOUNT"]
LIFT_U = {"NONE": 0, "LEGS2": 3, "LEGS4": 3, "LEGS6": 2, "ARMS": 0, "TENTACLES": 4, "WINGS": 0, "WHEELS": 3,
          "HOVER": 3, "MOUNT": 4}
TOPPERS = ["NONE", "ANTENNAE", "HORNS", "TUFT", "CAP", "BUN", "HALO", "SPROUT", "CABLE", "FIN"]
EYE_STYLES = ["DOT", "RING", "SLIT", "SCREEN", "SLEEPY"]
MOUTHS = ["NONE", "LINE", "TEETH", "O", "SMILE", "GRILLE"]
PATTERNS = ["NONE", "STRIPES", "SPOTS", "CIRCUIT", "CRACKS", "BAND"]
QUIRKS = ["TWITCH", "HOVER BOB", "FLICKER", "BREATHE", "EYE ROLL"]
TAGS = ["NONE", "HOT", "COLD", "WET", "SHOCK", "SHINY", "ALIVE", "MAGIC", "GLITCH", "STONE", "AIRY"]
DEFENCES = ["NONE", "ARMOURED", "SHIELDED"]
HUE_SHIFTS = [-2, -1, 0, 1, 2]
AFFIXES = ["SHIELDED", "VAMPIRIC", "SPLITTING", "HASTED", "AURA", "TELEPORT", "COLLAPSE"]
AFFIX_WORD = {"SHIELDED": "SHIELDED", "VAMPIRIC": "VAMPIRIC", "SPLITTING": "SPLITTING", "HASTED": "HASTED",
              "AURA": "AURA", "TELEPORT": "BLINKING", "COLLAPSE": "CRUMBLING"}
ACC = {  # biome accessories; first list = family cue set (a look always carries at least one of these)
    "OFFICE": (["LANYARD", "TIE", "STICKY NOTE", "COFFEE STAIN"], ["LANYARD", "TIE", "STICKY NOTE"]),
    "VENTS": (["DUST", "CABLE"], ["DUST", "CABLE"]),
    "ATRIUM": (["WATER LINE", "BUBBLES"], ["WATER LINE", "BUBBLES"]),
    "SERVER": (["CABLES", "LEDS"], ["CABLES", "LEDS"]),
    "MALL": (["PRICE TAG", "BAG"], ["PRICE TAG", "BAG"]),
    "GARDEN": (["VINES", "FLOWER"], ["VINES", "FLOWER"]),
    "ROOFTOP": (["SCARF", "ANTENNA"], ["SCARF", "ANTENNA"]),
    "GLITCH": (["ROW TEAR", "DEAD PIXELS"], ["ROW TEAR", "DEAD PIXELS"]),
}

# ---- bodies: primitives in a 16x16 unit box -------------------------------------------------------------------------
# ('E',x0,y0,x1,y1) ellipse | ('C',x0,y0,x1,y1,clipy) ellipse kept above clipy | ('R',x0,y0,x1,y1,r) rounded rect
# ('P',[(x,y)..]) polygon | ('X',x0,y0,x1,y1) cut rect | ('XE',..) cut ellipse
# details (interior only): ('h',x0,x1,y,code) ('v',x,y0,y1,code) ('f',x0,y0,x1,y1,code) ('d',x0,y0,x1,y1,code)=dither
ALL_GROUND = ["NONE", "LEGS2", "LEGS4", "ARMS", "WHEELS", "HOVER", "TENTACLES", "WINGS", "MOUNT"]
BODIES = {
    "OFFICE": [
        ("CLERK BLOB", [("E", 1, 2, 15, 16)], [], (8, 7), ALL_GROUND, []),
        ("FILING CAB", [("R", 2, 0, 14, 16, 1)], [("h", 3, 13, 6, 1), ("h", 3, 13, 11, 1), ("h", 7, 9, 8, 1),
                                                   ("h", 7, 9, 13, 1)], (8, 2),
         ["NONE", "LEGS2", "LEGS4", "WHEELS", "HOVER", "ARMS", "MOUNT"], []),
        ("DESK LAMP", [("C", 2, 0, 14, 14, 7), ("R", 6, 6, 10, 10, 0), ("E", 1, 9, 15, 16)],
         [("h", 3, 13, 6, 3)], (8, 12), ["NONE", "LEGS2", "WHEELS", "HOVER", "ARMS", "MOUNT"], ["CAP", "BUN"]),
        ("FAX THING", [("R", 3, 1, 13, 7, 0), ("R", 0, 6, 16, 16, 2)], [("f", 4, 1, 12, 6, 3), ("h", 2, 14, 8, 1)],
         (8, 11), ["NONE", "LEGS2", "LEGS4", "LEGS6", "WHEELS", "ARMS", "HOVER", "MOUNT"], []),
    ],
    "VENTS": [
        ("MOTH", [("E", 5, 1, 11, 16)], [], (8, 5), ["WINGS"], []),
        ("DUST WORM", [("E", 0, 8, 6, 16), ("E", 3, 7, 10, 16), ("E", 7, 6, 13, 16), ("E", 10, 3, 16, 13)],
         [("v", 4, 9, 15, 1), ("v", 7, 8, 15, 1), ("v", 10, 8, 15, 1)], (13, 7),
         ["NONE", "LEGS6", "HOVER"], ["CAP"]),
        ("GRILLE CRAB", [("R", 0, 4, 16, 15, 5)], [("h", 3, 13, 11, 1), ("h", 4, 12, 13, 1)], (8, 7),
         ["ARMS", "LEGS4", "LEGS6", "NONE"], []),
        ("DUST GHOST", [("R", 2, 0, 14, 16, 6), ("X", 5, 14, 7, 16), ("X", 10, 14, 12, 16)], [], (8, 6),
         ["NONE", "HOVER", "ARMS", "TENTACLES"], ["CAP"]),
    ],
    "ATRIUM": [
        ("DROPLET", [("E", 2, 4, 14, 16), ("P", [(8, 1), (3, 9), (13, 9)])], [], (8, 10),
         ["NONE", "HOVER", "TENTACLES", "ARMS", "LEGS2"], ["CAP", "HALO"]),
        ("JELLY", [("C", 1, 1, 15, 19, 11)], [("h", 2, 14, 9, 3)], (8, 6), ["TENTACLES", "NONE", "HOVER"], []),
        ("PUDDLE FROG", [("E", 0, 6, 16, 16), ("E", 1, 2, 7, 9), ("E", 9, 2, 15, 9)], [], (8, 5),
         ["LEGS2", "LEGS4", "NONE", "ARMS"], ["HALO"]),
        ("CLOUD PUFF", [("E", 0, 6, 8, 14), ("E", 4, 2, 12, 12), ("E", 8, 5, 16, 14), ("R", 1, 9, 15, 15, 3)], [],
         (8, 8), ["NONE", "HOVER", "WINGS", "TENTACLES", "ARMS"], []),
    ],
    "SERVER": [
        ("RACK", [("R", 2, 0, 14, 16, 0)], [("h", 3, 13, 8, 1), ("h", 3, 13, 12, 1)], (8, 4),
         ["NONE", "WHEELS", "LEGS2", "LEGS4", "MOUNT", "ARMS", "HOVER"], []),
        ("EYE ORB", [("E", 1, 1, 15, 15)], [], (8, 7),
         ["NONE", "HOVER", "WINGS", "MOUNT", "TENTACLES", "LEGS2", "LEGS6", "ARMS"], []),
        ("BLOCK BUG", [("R", 3, 3, 13, 13, 0), ("R", 1, 6, 3, 11, 0), ("R", 13, 6, 15, 11, 0), ("R", 5, 1, 7, 3, 0),
                       ("R", 9, 1, 11, 3, 0)], [], (8, 7),
         ["LEGS2", "LEGS4", "LEGS6", "NONE", "HOVER", "ARMS", "MOUNT"], ["HORNS"]),
        ("CABLE SNAKE", [("E", 0, 9, 16, 16), ("E", 3, 2, 12, 11)], [("h", 2, 14, 13, 1)], (7, 6),
         ["NONE", "HOVER", "ARMS"], []),
    ],
    "MALL": [
        ("SODA CUP", [("P", [(2, 4), (14, 4), (12, 16), (4, 16)]), ("R", 1, 2, 15, 5, 1), ("R", 9, 0, 11, 3, 0)],
         [("h", 3, 13, 12, 3)], (8, 8), ["NONE", "LEGS2", "WHEELS", "HOVER", "ARMS", "MOUNT"],
         ["HORNS", "TUFT", "CAP", "BUN", "SPROUT", "FIN"]),
        ("BURGER", [("C", 1, 1, 15, 15, 8), ("R", 0, 8, 16, 11, 1), ("R", 2, 11, 14, 16, 2)],
         [("d", 0, 8, 16, 11, 1)], (8, 5), ["NONE", "LEGS2", "LEGS4", "ARMS", "HOVER", "WINGS", "WHEELS"], []),
        ("SHOP BAG", [("E", 4, 0, 12, 8), ("XE", 6, 2, 10, 7), ("R", 2, 5, 14, 16, 1)], [], (8, 10),
         ["NONE", "LEGS2", "HOVER", "ARMS", "WHEELS"], ["CAP", "BUN", "HALO"]),
        ("DUMPLING", [("C", 0, 3, 16, 29, 16)], [("v", 5, 4, 7, 1), ("v", 8, 3, 6, 1), ("v", 11, 4, 7, 1)], (8, 10),
         ["NONE", "LEGS2", "LEGS4", "ARMS", "HOVER", "WHEELS"], ["CAP"]),
    ],
    "GARDEN": [
        ("BULB", [("E", 1, 4, 15, 16), ("R", 7, 1, 9, 6, 0)], [], (8, 10),
         ["NONE", "LEGS2", "TENTACLES", "ARMS", "HOVER"], ["CAP"]),
        ("MUSHROOM", [("C", 0, 0, 16, 16, 8), ("R", 4, 7, 12, 16, 2)], [("d", 4, 7, 12, 9, 1)], (8, 11),
         ["NONE", "LEGS2", "TENTACLES", "ARMS", "HOVER"], ["CAP", "BUN"]),
        ("FLOWER HEAD", [("E", 0, 4, 6, 11), ("E", 10, 4, 16, 11), ("E", 5, 0, 11, 5), ("E", 1, 1, 7, 7),
                         ("E", 9, 1, 15, 7), ("E", 1, 8, 7, 14), ("E", 9, 8, 15, 14), ("E", 5, 10, 11, 15),
                         ("R", 7, 13, 9, 16, 0)], [("f", 4, 3, 12, 12, 3)], (8, 6),
         ["NONE", "TENTACLES", "ARMS", "HOVER", "WINGS", "MOUNT"], ["CAP", "HORNS"]),
        ("TOPIARY", [("E", 1, 0, 15, 12), ("R", 4, 11, 12, 16, 1)], [("d", 4, 11, 12, 16, 1)], (8, 5),
         ["NONE", "WHEELS", "LEGS2", "ARMS", "HOVER"], []),
    ],
    "ROOFTOP": [
        ("PIGEON", [("E", 0, 6, 13, 16), ("E", 7, 1, 15, 9), ("P", [(14, 4), (16, 6), (14, 7)])], [], (11, 4),
         ["LEGS2", "WINGS", "HOVER", "NONE"], []),
        ("KITE", [("P", [(8, 0), (16, 7), (8, 16), (0, 7)])], [], (8, 6),
         ["NONE", "WINGS", "HOVER", "ARMS", "TENTACLES"], ["CAP", "BUN"]),
        ("WATER TOWER", [("R", 2, 3, 14, 12, 2), ("P", [(1, 4), (8, 0), (15, 4)])], [("h", 3, 13, 9, 1)], (8, 7),
         ["LEGS4", "LEGS2", "LEGS6", "MOUNT", "WHEELS"], ["TUFT", "BUN", "CAP"]),
        ("SKY DISH", [("E", 1, 0, 15, 10), ("R", 6, 9, 10, 14, 0), ("R", 3, 13, 13, 16, 1)], [("f", 4, 2, 12, 7, 3)],
         (8, 5), ["MOUNT", "NONE", "LEGS2", "LEGS4", "HOVER", "WHEELS"], ["TUFT", "BUN"]),
    ],
    "GLITCH": [
        ("NULL CUBE", [("R", 2, 3, 14, 16, 0), ("P", [(2, 3), (5, 0), (16, 0), (14, 3)]),
                       ("P", [(14, 3), (16, 0), (16, 13), (14, 16)])], [("f", 3, 0, 16, 3, 3), ("d", 14, 1, 16, 16, 1)],
         (8, 8), ["NONE", "LEGS2", "LEGS4", "LEGS6", "HOVER", "ARMS", "WHEELS", "MOUNT", "WINGS"], []),
        ("SHARD", [("P", [(6, 0), (14, 5), (11, 16), (2, 11)])], [], (8, 7),
         ["NONE", "HOVER", "WINGS", "ARMS", "TENTACLES"], ["CAP", "BUN"]),
        ("STATIC BLOB", [("E", 1, 2, 15, 16)], [("noise",)], (8, 8),
         ["NONE", "HOVER", "TENTACLES", "LEGS2", "LEGS6", "ARMS"], []),
        ("HEX FACE", [("P", [(4, 1), (12, 1), (16, 8), (12, 15), (4, 15), (0, 8)])], [], (8, 7),
         ["NONE", "HOVER", "LEGS2", "LEGS4", "WHEELS", "MOUNT", "WINGS", "ARMS"], []),
    ],
}

NOUNS = {
    "OFFICE": ["INTERN", "CLERK", "MEMO", "STAPLER", "TEMP", "FAX", "MINUTES", "LAMP"],
    "VENTS": ["MOTH", "DRAFT", "DUSTER", "HUM", "FLUE", "LINT", "SPARE FAN", "ECHO"],
    "ATRIUM": ["PUDDLE", "KOI", "SPLASH", "FOUNTAIN", "RIPPLE", "DRIP", "MIST", "TIDE"],
    "SERVER": ["PING", "CACHE", "DAEMON", "BYTE", "UPTIME", "PACKET", "BACKUP", "HOLD MUSIC"],
    "MALL": ["SAMPLE", "COUPON", "REFILL", "COMBO", "RECEIPT", "TRAY", "PRETZEL", "SALE"],
    "GARDEN": ["BUD", "ROOT", "POSY", "SEEDLING", "BLOOM", "MOSS", "PETAL", "HEDGE"],
    "ROOFTOP": ["GULL", "GUST", "PIGEON", "LAST BUS", "SKYLINE", "BEACON", "SIGNAL", "KITE"],
    "GLITCH": ["NULL", "LOOP", "TEAR", "STATIC", "GHOST", "BUFFER", "CHORUS", "ERROR"],
}
ADJ = ["DAMP", "SORRY", "TINY", "SPARE", "PILOT", "LATE", "SOFT", "QUIET", "LOST", "FADED", "SLEEPY", "STATIC",
       "HOLLOW", "DREAMY", "WARM", "LOOSE", "HUMMING", "PALE", "ODD", "SPECIAL"]

# =====================================================================================================================
# colour
# =====================================================================================================================
BIOME = {  # (dark, mid, light): pastel synthwave ramps, EGRESS's own
    "OFFICE": [(78, 40, 72), (232, 168, 150), (255, 226, 200)],
    "VENTS": [(46, 32, 52), (186, 156, 164), (234, 216, 214)],
    "ATRIUM": [(20, 52, 96), (88, 186, 204), (186, 240, 236)],
    "SERVER": [(18, 48, 64), (96, 196, 176), (200, 248, 224)],
    "MALL": [(92, 28, 80), (246, 140, 186), (255, 214, 232)],
    "GARDEN": [(36, 70, 52), (166, 214, 132), (230, 250, 196)],
    "ROOFTOP": [(42, 38, 104), (132, 160, 240), (218, 228, 255)],
    "GLITCH": [(40, 12, 56), (255, 112, 196), (255, 212, 236)],
}
RAMPS = {  # extra body ramps per family (index 0 = BIOME); the look picks one, so a family sheet isn't one hue
    "OFFICE": [[(60, 104, 92), (140, 222, 190), (214, 252, 234)],     # mint filing cabinet
               [(110, 40, 90), (255, 150, 196), (255, 220, 236)],     # sticky-note pink
               [(100, 70, 30), (250, 214, 96), (255, 244, 190)],      # sticky-note yellow
               [(24, 60, 100), (80, 170, 214), (180, 232, 248)],      # carpet teal-blue
               [(60, 40, 110), (168, 150, 248), (226, 218, 255)],     # lavender CRT
               [(110, 40, 50), (255, 140, 120), (255, 212, 196)],     # coral
               [(90, 20, 80), (232, 92, 200), (255, 196, 240)]],      # toner magenta
    "VENTS": [[(30, 44, 80), (120, 156, 204), (206, 224, 244)], [(90, 56, 30), (222, 170, 110), (250, 226, 186)],
              [(40, 60, 50), (150, 190, 160), (220, 240, 224)]],
    "ATRIUM": [[(20, 40, 110), (90, 140, 240), (190, 214, 255)], [(20, 80, 70), (110, 220, 180), (210, 255, 236)],
               [(110, 50, 30), (255, 160, 100), (255, 226, 196)]],
    "SERVER": [[(10, 50, 80), (70, 200, 230), (190, 244, 255)], [(40, 30, 80), (150, 130, 230), (220, 212, 255)],
               [(90, 60, 10), (240, 190, 80), (255, 236, 180)]],
    "MALL": [[(100, 80, 20), (250, 220, 100), (255, 246, 200)], [(20, 80, 70), (120, 230, 200), (220, 255, 240)],
             [(110, 50, 30), (255, 160, 90), (255, 222, 190)], [(30, 50, 110), (120, 170, 250), (210, 230, 255)]],
    "GARDEN": [[(100, 40, 80), (250, 160, 210), (255, 226, 240)], [(100, 80, 30), (250, 224, 120), (255, 248, 210)],
               [(110, 60, 50), (255, 176, 140), (255, 230, 210)]],
    "ROOFTOP": [[(110, 50, 40), (255, 150, 100), (255, 220, 190)], [(100, 40, 90), (240, 130, 190), (255, 214, 236)],
                [(20, 60, 100), (110, 200, 240), (210, 240, 255)]],
    "GLITCH": [[(10, 60, 80), (64, 220, 240), (200, 250, 255)], [(40, 80, 20), (180, 240, 90), (236, 255, 200)],
               [(110, 30, 40), (255, 100, 110), (255, 200, 206)]],
}


def ramps(fam):
    return [BIOME[fam]] + RAMPS[fam]


ACCENT = {
    "OFFICE": [(80, 120, 240), (255, 120, 110)],
    "VENTS": [(196, 180, 214), (214, 190, 160)],
    "ATRIUM": [(214, 250, 255), (164, 226, 255)],
    "SERVER": [(90, 255, 150), (64, 224, 248)],
    "MALL": [(255, 214, 90), (120, 150, 255)],
    "GARDEN": [(255, 140, 200), (255, 180, 120)],
    "ROOFTOP": [(240, 80, 110), (255, 150, 70)],
    "GLITCH": [(64, 224, 248), (190, 255, 80)],
}
ELEMENT = {  # element ramp target (mid) per tag; cue colour = the fixed tell colour players learn
    "HOT": ((255, 140, 90), (255, 120, 40)),
    "COLD": ((156, 210, 255), (120, 208, 255)),
    "WET": ((90, 140, 240), (70, 150, 255)),
    "SHOCK": ((250, 230, 110), (255, 236, 64)),
    "SHINY": ((240, 236, 250), (255, 255, 255)),
    "ALIVE": ((130, 220, 120), (80, 224, 96)),
    "MAGIC": ((180, 120, 255), (196, 110, 255)),
    "GLITCH": ((255, 100, 200), (255, 56, 200)),
    "STONE": ((176, 166, 172), (156, 146, 150)),
    "AIRY": ((206, 240, 250), (214, 252, 255)),
}
AFFIX_COL = {"SHIELDED": (90, 230, 255), "VAMPIRIC": (255, 64, 104), "SPLITTING": (255, 230, 80),
             "HASTED": (255, 150, 40), "TELEPORT": (190, 120, 255), "COLLAPSE": (226, 184, 120)}
INK = (20, 14, 46)
WHITE = (250, 246, 255)
FLOOR_LIGHT = E.P["lilac"][2]  # the lightest floor tone a mob stands on
FLOOR_BASE = E.P["lilac"][1]
VOID_TONE = E.P["void"][1]


def q555(c):
    return tuple((v >> 3) * 255 // 31 for v in c)


def mix(a, b, n, d=16):
    return tuple((a[i] * (d - n) + b[i] * n) // d for i in range(3))


def hue_rot(c, k):
    """integer hue rotation by channel mixing: k steps of 2/16 toward the next (k>0) or previous (k<0) channel"""
    a = abs(k) * 2
    r, g, b = c
    if k > 0:
        return ((r * (16 - a) + b * a) // 16, (g * (16 - a) + r * a) // 16, (b * (16 - a) + g * a) // 16)
    if k < 0:
        return ((r * (16 - a) + g * a) // 16, (g * (16 - a) + b * a) // 16, (b * (16 - a) + r * a) // 16)
    return c


def lum(c):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * ch(c[0]) + 0.7152 * ch(c[1]) + 0.0722 * ch(c[2])


def contrast(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def pal_body(fam, tag, shift, ramp=0):
    d, m, l = ramps(fam)[ramp]
    if tag != "NONE":
        el = ELEMENT[tag][0]
        m = mix(m, el, 6)
        l = mix(l, mix(el, WHITE, 8), 4)
        d = mix(d, tuple(v * 5 // 16 for v in el), 6)
    d, m, l = (hue_rot(c, shift) for c in (d, m, l))
    d, m, l = q555(d), q555(m), q555(l)
    for _ in range(16):  # outline must read on the light floor
        if min(contrast(d, FLOOR_LIGHT), contrast(d, FLOOR_BASE)) >= 4.5:
            break
        d = q555(tuple(v * 13 // 16 for v in d))
    for _ in range(16):  # body must read on the dark void
        if contrast(m, VOID_TONE) >= 4.0:
            break
        m = q555(mix(m, WHITE, 2))
    for k in range(8):  # keep the body off the floor's own lilac: rotate, then lift (bounded; C runs the same loop)
        if sum((m[i] - FLOOR_BASE[i]) ** 2 for i in range(3)) >= 48 * 48:
            break
        m = q555(hue_rot(m, 1)) if k < 3 else q555(mix(m, WHITE, 3))
        l = q555(hue_rot(l, 1)) if k < 3 else l
    for _ in range(12):  # light must separate from mid: lift light, and if it's already white, deepen mid
        if contrast(l, m) >= 1.3:
            break
        l = q555(mix(l, WHITE, 4))
        if contrast(l, m) < 1.3 and l == q555(mix(l, WHITE, 4)):
            m = q555(tuple(v * 15 // 16 for v in m))
    return [d, m, l]


def pal_overlay(fam, tag, accent):
    cue = ELEMENT[tag][1] if tag != "NONE" else WHITE
    return [INK, q555(ACCENT[fam][accent]), q555(cue)]


def pal_elite(affixes, aura_tag):
    cols = [ELEMENT[aura_tag][1] if a == "AURA" else AFFIX_COL[a] for a in affixes]
    if len(cols) == 1:
        cols.append(mix(cols[0], WHITE, 9))
    return [INK, q555(cols[0]), q555(cols[1])]


# =====================================================================================================================
# templates ('.' clear; codes as in the module docstring). small = S/M, big = L/XL. anchor: bottom-centre
# =====================================================================================================================
def T(*rows):
    return [r for r in rows]


TOP = {
    "ANTENNAE": (T("3...3", "1...1", ".1.1."), T("33.....33", "31.....13", ".1.....1.", "..1...1..", "...1.1...")),
    "HORNS": (T("1...1", "31.13"), T("1.......1", "31.....13", "331...133", ".3.....3.")),
    "TUFT": (T(".1.1.", "13131"), T("..1.1.1..", ".131313..", "1333333.1")),
    "CAP": (T(".444..", "455544", "444444"), T("..4444....", ".455554...", "45555554..", "4444444444")),
    "BUN": (T(".11.", "1331", "1221"), T("..111..", ".13331.", "1333221", "1322221", ".11111.")),
    "HALO": (T(".111.", "13331", ".111.", "....."), T("..11111..", ".1333331.", "..11111..", ".........")),
    "SPROUT": (T(".11.", "1331", ".11.", "..1."), T("..111.", ".13331", ".13321", "..111.", "...1..", "...1..")),
    "CABLE": (T("55.", "44.", ".4.", "4.."), T(".55..", ".55..", ".44..", "..4..", "..4..", ".4...", "4....")),
    "FIN": (T("1..", "31.", "321"), T("1....", "31...", "331..", "3321.", "32221")),
}
EYE = {
    "DOT": [T("1"), T("1", "1"), T("31", "11"), T("311", "111", "111")],
    "RING": [None, T("111", "131", "111"), T(".11.", "1331", "1311", ".11."),
             T(".111.", "13331", "13311", "13331", ".111.")],
    "SLIT": [T("11"), T("111"), T("1111"), T("11111", ".111.")],
    "SCREEN": [T("5"), T("55", "55"), T("555", "565", "555"), T("5555", "5665", "5555")],
    "SLEEPY": [None, T("111", ".1."), T("1111", ".11."), T("11111", "11311", ".111.")],
}
MOUTH = {
    "NONE": [T(), T(), T(), T()],
    "LINE": [T("11"), T("111"), T("1111"), T("111111")],
    "TEETH": [None, T("1.1.1", ".1.1."), T("1111111", "1313131"), T("111111111", "131313131", "1.1.1.1.1")],
    "O": [T("1"), T("11", "11"), T(".11.", "1111", ".11."), T(".111.", "11111", "11111", ".111.")],
    "SMILE": [T("1.1", ".1."), T("1..1", ".11."), T("1...1", ".111."), T("1.....1", ".1...1.", "..111..")],
    "GRILLE": [None, T("1111", "1313", "1111"), T("11111", "13131", "11111"),
               T("1111111", "1313131", "1313131", "1111111")],
}
CUE = {
    "HOT": T("..4.", ".464", "4664", "4664", ".44."),
    "ALIVE": T("..44", ".466", "4664", "464.", "4..."),
    "MAGIC": T("..4..", ".464.", "46664", ".464.", "..4.."),
    "SHOCK": T("..44", ".464", "4664", "..64", ".64.", ".4.."),
    "SHINY": T(".6.", "666", ".6."),
    "STONE": T(".44.", "4664", "4444"),
    "AIRY": T(".666", "6...", "6.66", ".6.."),
}
AFFIX_ICON = {
    "SHIELDED": T("7777777", "7aaaaa7", "7aaaaa7", ".7aaa7.", "..7a7..", "...7..."),
    "VAMPIRIC": T("777777777", "7aaaaaaa7", "77a777a77", ".7a7.7a7.", "..7...7.."),
    "SPLITTING": T(".777..777.", "7aaa77aaa7", "7aaa77aaa7", ".777..777."),
    "HASTED": T("77..77..", "7a7.7a7.", ".7a7.7a7", "7a7.7a7.", "77..77.."),
    "AURA": T("..7...", ".7a7..", ".7aa7.", "7aaaa7", "7aaaa7", ".7777."),
    "TELEPORT": T("aa...aa", "a.....a", "...a...", "a.....a", "aa...aa"),
    "COLLAPSE": T("777......", "7a7......", "77777....", "...7a7...", "...77777.", "......7a7", "......777"),
}
ACC_T = {
    "CARD": (T("444", "456", "444"), T("4444", "4556", "4554", "4444")),
    "TIE": (T(".4.", "454", "454", ".4."), T(".44.", "4554", ".55.", "4554", "4554", ".44.")),
    "STICKY": (T("555", "554"), T("5555", "5555", "5544")),
    "STAIN": (T("4.4", ".44"), T(".4.4", "4.4.", ".44.")),
    "PRICE": (T("444", "465", "455", "444"), T(".444.", "44654", "45554", "45554", "44444")),
    "BAG": (T(".44.", "4..4", "4444", "4554", "4444"), T("..44..", ".4..4.", "444444", "455554", "455554", "444444")),
    "FLOWER": (T(".55.", "5665", ".55."), T(".55.", "5665", "5665", ".55.")),
    "BUBBLE": (T("55", "5."), T(".55.", "5..5", "5..5", ".55.")),
}
SIN16 = [0, 6, 11, 15, 16, 15, 11, 6, 0, -6, -11, -15, -16, -15, -11, -6]  # 16 * sin(k * 22.5 deg)


# =====================================================================================================================
# geometry
# =====================================================================================================================
class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.c = [[0] * w for _ in range(h)]

    def get(self, x, y):
        return self.c[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def set(self, x, y, v):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.c[y][x] = v

    def stamp(self, tpl, x, y, empty_only=False, remap=None, allow=None):
        """top-left stamp; returns pixels written"""
        n = 0
        for yy, row in enumerate(tpl):
            for xx, ch in enumerate(row):
                if ch == ".":
                    continue
                v = int(remap.get(ch, ch)) if remap else int(ch)
                X, Y = x + xx, y + yy
                if empty_only and self.get(X, Y):
                    continue
                if allow is not None and not allow(X, Y):
                    continue
                self.set(X, Y, v)
                n += 1
        return n

    def bbox(self):
        xs = [x for y in range(self.h) for x in range(self.w) if self.c[y][x]]
        ys = [y for y in range(self.h) for x in range(self.w) if self.c[y][x]]
        return (min(xs), min(ys), max(xs), max(ys)) if xs else (0, 0, 0, 0)


def tw(t):
    return len(t[0]) if t else 0


def u2p(o, u, span):
    return o + (u * span + 8) // 16


def raster_prims(mask, prims, box, mat):
    bx, by, bw, bh = box
    H, W = len(mask), len(mask[0])
    for p in prims:
        kind = p[0]
        if kind in ("E", "C", "XE"):
            X0, Y0, X1, Y1 = u2p(bx, p[1], bw), u2p(by, p[2], bh), u2p(bx, p[3], bw), u2p(by, p[4], bh)
            clip = u2p(by, p[5], bh) if kind == "C" else 10 ** 6
            rx, ry = X1 - X0, Y1 - Y0
            for y in range(max(0, Y0), min(H, Y1, clip)):
                for x in range(max(0, X0), min(W, X1)):
                    dx, dy = 2 * x + 1 - (X0 + X1), 2 * y + 1 - (Y0 + Y1)
                    if dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry:
                        mask[y][x] = 0 if kind == "XE" else mat
        elif kind in ("R", "X"):
            X0, Y0, X1, Y1 = u2p(bx, p[1], bw), u2p(by, p[2], bh), u2p(bx, p[3], bw), u2p(by, p[4], bh)
            rp = (p[5] * bw + 8) // 16 if kind == "R" else 0
            rp = min(rp, (X1 - X0) // 2, (Y1 - Y0) // 2)
            for y in range(max(0, Y0), min(H, Y1)):
                for x in range(max(0, X0), min(W, X1)):
                    ok = True
                    if rp:
                        cx2 = 2 * (X0 + rp) if 2 * x + 1 < 2 * (X0 + rp) else (2 * (X1 - rp) if 2 * x + 1 > 2 * (X1 - rp) else None)
                        cy2 = 2 * (Y0 + rp) if 2 * y + 1 < 2 * (Y0 + rp) else (2 * (Y1 - rp) if 2 * y + 1 > 2 * (Y1 - rp) else None)
                        if cx2 is not None and cy2 is not None:
                            ok = (2 * x + 1 - cx2) ** 2 + (2 * y + 1 - cy2) ** 2 <= (2 * rp) ** 2
                    if ok:
                        mask[y][x] = 0 if kind == "X" else mat
        elif kind == "P":
            pts = [(2 * bx + (u * bw * 2 + 8) // 16, 2 * by + (v * bh * 2 + 8) // 16) for u, v in p[1]]
            ys = [q[1] for q in pts]
            for y in range(max(0, min(ys) // 2 - 1), min(H, max(ys) // 2 + 1)):
                for x in range(W):
                    px_, py_ = 2 * x + 1, 2 * y + 1
                    inside = False
                    j = len(pts) - 1
                    for i in range(len(pts)):
                        (xi, yi), (xj, yj) = pts[i], pts[j]
                        if (yi > py_) != (yj > py_):
                            # px < xi + (py-yi)*(xj-xi)/(yj-yi), cross-multiplied
                            lhs = (px_ - xi) * (yj - yi)
                            rhs = (py_ - yi) * (xj - xi)
                            if (lhs < rhs) if yj > yi else (lhs > rhs):
                                inside = not inside
                        j = i
                    if inside:
                        mask[y][x] = mat


def body_box(spr, limb):
    sx, sy, S = spr
    lift = (LIFT_U[limb] * S + 8) // 16
    inset = (3 * S + 8) // 16 if limb == "WINGS" else 0
    return (sx + inset, sy, S - 2 * inset, S - lift)


# =====================================================================================================================
# the look
# =====================================================================================================================
LOOK_FIELDS = ["fam", "body", "size", "limb", "topper", "eyes", "style", "mouth", "pattern", "acc", "accent", "ramp", "hue",
               "quirk", "tag", "defence", "adj", "noun", "pseed"]


def allowed_sizes(mobility):
    return [0] if mobility == "SWARM" else ([1, 2, 3] if mobility in ("TURRET",) else [0, 1, 1, 2, 2, 1, 3, 2])


def allowed_limbs(fam, body, sc, mobility=None):
    ls = list(BODIES[fam][body][4])
    if sc == 0:
        ls = [l for l in ls if l not in ("LEGS4", "LEGS6", "ARMS", "WHEELS")] or ["NONE"]
    if mobility == "TURRET":
        ls = ["MOUNT"]
    elif mobility == "FLYER":
        ls = [l for l in ls if l in ("WINGS", "HOVER")] or ["HOVER"]
    elif mobility == "WALKER":
        ls = [l for l in ls if l in ("LEGS2", "LEGS4", "LEGS6", "WHEELS", "TENTACLES", "ARMS")] or ls
    return ls


def allowed_toppers(fam, body, sc):
    ban = BODIES[fam][body][5]
    ts = [t for t in TOPPERS if t not in ban]
    if sc == 0:
        ts = [t for t in ts if t in ("NONE", "ANTENNAE", "TUFT", "SPROUT", "HALO", "HORNS")]
    return ts


def allowed_eye_styles(fam):
    return ["DOT", "SLIT", "SCREEN"] if fam == "SERVER" else EYE_STYLES


def allowed_patterns(sc):
    return PATTERNS[:3] if sc == 0 else PATTERNS


def allowed_acc(fam, sc):
    items, cue = ACC[fam]
    mx = [1, 2, 3, 3][sc]
    out = []
    for m in range(1, 1 << len(items)):
        sel = [items[i] for i in range(len(items)) if m >> i & 1]
        if len(sel) <= mx and any(s in cue for s in sel):
            out.append(m)
    return out


def allowed_defences(sc):
    return ["NONE", "SHIELDED"] if sc == 0 else DEFENCES


_BODY_CACHE = {}


def body_mask(fam, body, sc, limb, W=64, H=64):
    """material grid for body + limbs (1 body, 2 limb, 3 wing) and anchors; cached (shape is deterministic)"""
    key = (fam, body, sc, limb, W, H)
    if key in _BODY_CACHE:
        return _BODY_CACHE[key]
    S = SIZES[sc]
    spr = ((W - S) // 2, H - 8 - S, S)
    sx, sy, _ = spr
    bx, by, bw, bh = body_box(spr, limb)
    name, prims, det, (fx, fy), _, _ = BODIES[fam][body]
    mat = [[0] * W for _ in range(H)]
    ground = sy + S - 1
    if limb == "WINGS":
        wy0, wy1 = sy + S // 16, sy + (S * 11) // 16
        raster_prims(mat, [("E", 0, 0, 8, 16)], (sx, wy0, S // 2 + 1, wy1 - wy0), 3)
        raster_prims(mat, [("E", 8, 0, 16, 16)], (sx + S // 2 - 1, wy0, S // 2 + 1, wy1 - wy0), 3)
    body = [[0] * W for _ in range(H)]
    raster_prims(body, prims, (bx, by, bw, bh), 1)
    for y in range(H):
        for x in range(W):
            if body[y][x]:
                mat[y][x] = 1
    rows = [y for y in range(H) if any(body[y])]
    by0, by1 = rows[0], rows[-1]
    cols = [x for x in range(W) if any(body[y][x] for y in range(H))]
    bx0, bx1 = cols[0], cols[-1]
    cx = u2p(bx, fx, bw)
    cxm = (bx0 + bx1 + 1) // 2

    def put_limb(x, y):
        if 0 <= x < W and 0 <= y < H and not mat[y][x]:
            mat[y][x] = 2

    lw = max(1, S // 8)
    if limb == "LEGS2":
        for lx in (cxm - S // 5 - lw // 2 - 1, cxm + S // 5 - lw // 2):
            for y in range(by1 - 1, ground + 1):
                for k in range(lw):
                    put_limb(lx + k, y)
            put_limb(lx - 1 if lx < cxm else lx + lw, ground)
    elif limb == "LEGS4":
        n, lw4 = 4, max(1, S // 12)
        for i in range(n):
            lx = bx0 + 1 + (i * (bx1 - bx0 - 1 - lw4)) // (n - 1)
            for y in range(by1 - 1, ground + 1):
                for k in range(lw4):
                    put_limb(lx + k, y)
    elif limb == "TENTACLES":
        n = 3 if sc < 2 else 4
        tw_ = max(1, S // 10)
        for i in range(n):
            lx = bx0 + 2 + (i * (bx1 - bx0 - 3 - tw_)) // (n - 1)
            for y in range(by1 - 1, ground + 1):
                wob = ((y - by1) // 2 + i) % 2
                for k in range(tw_):
                    put_limb(lx + k + wob, y)
    elif limb == "ARMS":
        my = by0 + (by1 - by0) * 5 // 8
        aw, ah = max(2, S // 6), max(1, S // 10)
        for side in (-1, 1):
            # stub arm: an ellipse sticking out of the side at mid-height
            edge = next(x for x in range(W) if body[my][x]) if side < 0 else max(x for x in range(W) if body[my][x])
            x0 = edge - aw if side < 0 else edge - 1
            for y in range(my - ah, my + ah + 1):
                for x in range(x0, x0 + aw + 2):
                    dx2, dy2 = 2 * x + 1 - (2 * x0 + aw + 2), 2 * (y - my)
                    if dx2 * dx2 * (ah + 1) ** 2 + dy2 * dy2 * ((aw + 2) // 2 + 1) ** 2 <= ((aw + 2) // 2 + 1) ** 2 * (ah + 1) ** 2 * 4:
                        put_limb(x, y)
    elif limb == "WHEELS":
        d = max(4, S // 4 + 1)
        for wx in (bx0 + 1, bx1 - d + 1):
            raster_prims(mat, [("E", 0, 0, 16, 16)], (wx, ground - d + 1, d, d), 2)
    elif limb == "MOUNT":
        h = (LIFT_U["MOUNT"] * S + 8) // 16
        raster_prims(mat, [("P", [(3, 0), (13, 0), (16, 16), (0, 16)])], (cxm - S * 3 // 8, ground - h + 1, S * 3 // 4, h + 1), 2)
    info = {"spr": spr, "bbox": (bx0, by0, bx1, by1), "cx": cx, "cxm": cxm, "ground": ground,
            "fy": u2p(by, fy, bh), "box": (bx, by, bw, bh)}
    _BODY_CACHE[key] = (mat, body, info)
    return _BODY_CACHE[key]


def row_span(body, y, cx):
    """contiguous body run on row y through column cx (or nearest)"""
    W = len(body[0])
    if not (0 <= y < len(body)) or not any(body[y]):
        return None
    if not body[y][cx]:
        xs = [x for x in range(W) if body[y][x]]
        cx = min(xs, key=lambda x: abs(x - cx))
    a = cx
    while a > 0 and body[y][a - 1]:
        a -= 1
    b = cx
    while b < W - 1 and body[y][b + 1]:
        b += 1
    return a, b


def face_layout(fam, body, sc, limb, n, style, mouth):
    """eye + mouth placement or None when it doesn't fit (the same check the C composer runs)"""
    et = EYE[style][sc]
    mt = MOUTH[mouth][sc]
    if et is None or mt is None:
        return None
    if sc == 0 and n > 2 or sc == 1 and n > 3 or (sc == 1 and style == "RING" and n > 2):
        return None
    mat, bm, info = body_mask(fam, body, sc, limb)
    ew, eh = tw(et), len(et)
    ey = info["fy"] - eh // 2
    cx = info["cx"]

    def fits(y, need):
        for yy in (y, y + max(0, eh - 1)):
            sp = row_span(bm, yy, cx)
            if sp is None or sp[1] - sp[0] + 1 < need + 2:
                return None
        sp = row_span(bm, y, cx)
        return sp

    rows = [n]
    sp = fits(ey, n * ew + (n - 1))
    if sp is None and n >= 3:
        rows = [2, n - 2]
        sp = fits(ey, 2 * ew + 1)
        if sp is None or fits(ey + eh + 1, (n - 2) * ew + (n - 3)) is None:
            return None
    elif sp is None:
        return None
    eyes = []
    y = ey
    for k in rows:
        sp = row_span(bm, y, cx)
        fw = sp[1] - sp[0] + 1
        gap = max(1, min(1 + sc * 2, (fw - 2 - k * ew) // (k + 1)))
        tot = k * ew + (k - 1) * gap
        mid = (sp[0] + sp[1] + 1) // 2 if abs((sp[0] + sp[1] + 1) // 2 - cx) > 2 else cx
        x0 = mid - tot // 2
        for i in range(k):
            eyes.append((x0 + i * (ew + gap), y))
        y += eh + 1
    my = y + (1 if sc >= 2 else 0)
    if mt:
        mw = tw(mt)
        for yy in range(my, my + len(mt)):
            sp = row_span(bm, yy, cx)
            if sp is None or sp[1] - sp[0] + 1 < mw + 2 or yy >= info["bbox"][3]:
                return None
        sp = row_span(bm, my, cx)
        mx = min(max(cx - mw // 2, sp[0] + 1), sp[1] - mw)
    else:
        mx = cx
    return {"eyes": eyes, "et": et, "mt": mt, "mouth_xy": (mx, my), "mouth_bot": my + len(mt), "eye_top": ey}


def valid_faces(fam, body, sc, limb):
    out = []
    for n in range(1, 5):
        for st in allowed_eye_styles(fam):
            for m in MOUTHS:
                if face_layout(fam, body, sc, limb, n, st, m):
                    out.append((n, st, m))
    return out


def gen(seed, fam=None, size=None, tag=None, defence=None, mobility=None, body=None):
    """seeded look: draws in a fixed order (mirrors the C composer)"""
    r = Rng(seed)
    f = FAMILIES[r.pick(8)] if fam is None else fam
    b = r.pick(len(BODIES[f])) if body is None else body
    sz = allowed_sizes(mobility)
    sc = sz[r.pick(len(sz))] if size is None else size
    limbs = allowed_limbs(f, b, sc, mobility)
    limb = limbs[r.pick(len(limbs))]
    tops = allowed_toppers(f, b, sc)
    topper = tops[r.pick(len(tops))]
    faces = valid_faces(f, b, sc, limb)
    n, st, mo = faces[r.pick(len(faces))]
    pats = allowed_patterns(sc)
    pat = pats[r.pick(len(pats))]
    accs = allowed_acc(f, sc)
    acc = accs[r.pick(len(accs))]
    accent = r.pick(2)
    ramp = r.pick(len(ramps(f)))
    hue = HUE_SHIFTS[r.pick(5)]
    quirk = QUIRKS[r.pick(5)]
    tg = (TAGS[r.pick(len(TAGS))] if r.pick(3) else "NONE") if tag is None else tag
    defs = allowed_defences(sc)
    df = (defs[r.pick(len(defs))] if r.pick(4) == 0 else "NONE") if defence is None else defence
    return {"fam": f, "body": b, "size": sc, "limb": limb, "topper": topper, "eyes": n, "style": st, "mouth": mo,
            "pattern": pat, "acc": acc, "accent": accent, "ramp": ramp, "hue": hue, "quirk": quirk, "tag": tg, "defence": df,
            "adj": r.pick(len(ADJ)), "noun": r.pick(len(NOUNS[f])), "pseed": r.next(), "affixes": [],
            "aura": "NONE", "mobility": mobility}


def mob_name(lk):
    return f"{ADJ[lk['adj']]} {NOUNS[lk['fam']][lk['noun']]}"


def affix_ok(lk, a):
    if a == "SPLITTING":
        return lk["size"] != 0 and lk.get("mobility") != "SWARM"
    if a == "TELEPORT":
        return lk["defence"] != "ARMOURED" and lk["limb"] != "MOUNT"
    if a == "COLLAPSE":
        return lk["limb"] not in ("HOVER", "WINGS", "MOUNT")
    if a == "SHIELDED":
        return lk["defence"] != "SHIELDED"
    return True


def make_elite(lk, seed, n=None, affixes=None, aura=None):
    r = Rng(seed ^ 0xE11E)
    if affixes is None:
        pool = [a for a in AFFIXES if affix_ok(lk, a)]
        k = (1 + r.pick(2)) if n is None else n
        affixes = []
        while len(affixes) < k:
            a = pool[r.pick(len(pool))]
            if a not in affixes:
                affixes.append(a)
    lk = dict(lk)
    lk["affixes"] = list(affixes)
    lk["aura"] = aura or (lk["tag"] if lk["tag"] != "NONE" else ["HOT", "COLD", "SHOCK", "MAGIC"][r.pick(4)])
    return lk


# =====================================================================================================================
# render
# =====================================================================================================================
POSE0 = {"dx": 0, "dy": 0, "squash": 0, "leg": 0, "wing": 0, "eyes": "open", "mouth": None, "pal": "normal",
         "dissolve": 0, "twitch": 0}


def render(lk, pose=None, W=64, H=64):
    """indexed canvas of one look in one pose (no elite parts)"""
    ps = dict(POSE0, **(pose or {}))
    f, b, sc, limb = lk["fam"], lk["body"], lk["size"], lk["limb"]
    S = SIZES[sc]
    mat0, bm0, info = body_mask(f, b, sc, limb, W, H)
    r = Rng(lk["pseed"])
    # pose: squash moves the body top down (body rows only), leg phase lifts one leg
    mat = [row[:] for row in mat0]
    bm = [row[:] for row in bm0]
    bx0, by0, bx1, by1 = info["bbox"]
    if ps["squash"]:
        for _ in range(ps["squash"]):
            for y in range(by1, by0, -1):
                for x in range(W):
                    if y < by0 + (by1 - by0) // 2:
                        bm[y][x] = bm[y - 1][x]
                        if mat[y][x] in (0, 1):
                            mat[y][x] = 1 if bm[y][x] else (mat[y][x] if mat[y][x] != 1 else 0)
            for x in range(W):
                if bm[by0][x]:
                    bm[by0][x] = 0
                    if mat[by0][x] == 1:
                        mat[by0][x] = 0
            by0 += 1
    if ps["leg"] and limb in ("LEGS2", "LEGS4", "TENTACLES"):
        g = info["ground"]
        for x in range(info["cxm"], W):
            if mat[g][x] == 2:
                mat[g][x] = 0
    if ps["wing"] and limb == "WINGS":
        for y in range(H):
            for x in range(W):
                if mat[y][x] == 3 and (y > by0 + (by1 - by0) * 5 // 8):
                    mat[y][x] = 0
    if "STATIC BLOB" == BODIES[f][b][0]:  # noisy edge: seeded nibbles
        for y in range(H):
            for x in range(W):
                if bm[y][x] and (not bm[y][x - 1] or not bm[y][x + 1]) and (mix_seed(x, y, lk["pseed"]) & 3) == 0:
                    bm[y][x] = 0
                    mat[y][x] = 0
    cv = Canvas(W, H)
    # ---- shade by material: Bayer dither mid->light from the top-left, a dark rim at the bottom-right
    w, h = max(1, bx1 - bx0), max(1, by1 - by0)
    for y in range(H):
        for x in range(W):
            m = mat[y][x]
            if not m:
                continue
            if m == 1:
                t = 1 - ((x - bx0) / w * 0.55 + (y - by0) / h * 0.75)
                v = 3 if E.BAYER[y & 3][x & 3] < max(0, t) * 22 else 2
                if E.BAYER[y & 3][x & 3] < max(0, (y - by0) / h - 0.72) * 30:
                    v = 1 if (x + y) & 1 else 2
            elif m == 2:
                v = 2 if E.BAYER[y & 3][x & 3] < 11 else 1
            else:
                v = 3 if E.BAYER[y & 3][x & 3] < 9 else 2
            cv.c[y][x] = v
    # ---- outline (inside the silhouette): edge to clear, or a back part (limb/wing) meeting the body
    outl = [[False] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            m = mat[y][x]
            if not m:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                X, Y = x + dx, y + dy
                n = mat[Y][X] if 0 <= X < W and 0 <= Y < H else 0
                if n == 0 or (m in (2, 3) and n == 1) or (m == 3 and n == 2):
                    outl[y][x] = True
                    break
    for y in range(H):
        for x in range(W):
            if outl[y][x]:
                cv.c[y][x] = 1
    interior = lambda x, y: 0 <= x < W and 0 <= y < H and bm[y][x] and not outl[y][x]  # noqa: E731
    # specular
    for y in range(by0, by1):
        xs = [x for x in range(bx0, bx1 + 1) if interior(x, y)]
        if xs:
            hx = xs[0] + 1 + sc // 2
            if interior(hx, y + 1):
                cv.set(hx, y + 1, 3)
                if sc >= 2:
                    cv.set(hx + 1, y + 1, 3)
                    cv.set(hx, y + 2, 3)
            break
    # ---- body details
    bx, by, bw, bh = info["box"]
    by_sh = by + (by0 - info["bbox"][1])
    for d in BODIES[f][b][2]:
        if d[0] == "v":
            x = u2p(bx, d[1], bw)
            for y in range(u2p(by_sh, d[2], bh), u2p(by_sh, d[3], bh)):
                if interior(x, y):
                    cv.c[y][x] = d[4]
        elif d[0] == "h":
            y = u2p(by_sh, d[3], bh)
            for x in range(u2p(bx, d[1], bw), u2p(bx, d[2], bw)):
                if interior(x, y):
                    cv.c[y][x] = d[4]
        elif d[0] in ("f", "d"):
            for y in range(u2p(by_sh, d[2], bh), u2p(by_sh, d[4], bh)):
                for x in range(u2p(bx, d[1], bw), u2p(bx, d[3], bw)):
                    if interior(x, y) and (d[0] == "f" or (x + y) & 1):
                        cv.c[y][x] = d[5] if d[0] == "f" and E.BAYER[y & 3][x & 3] < 12 else (d[5] if d[0] == "d" else cv.c[y][x])
    # ---- pattern
    pat = lk["pattern"]
    ys_ = [y for y in range(H) if any(interior(x, y) for x in range(W))]
    if pat == "STRIPES":
        P = [3, 4, 5, 6][sc]
        for y in ys_:
            if (y - by0) % P == P - 1:
                for x in range(W):
                    if interior(x, y) and (x + y) & 1 == 0 or (interior(x, y) and sc >= 2 and (y - by0) % P == P - 1 and x % 3 == 0):
                        cv.c[y][x] = 1
    elif pat == "SPOTS":
        k = 2 + sc * 2
        cells = [(x, y) for y in ys_ for x in range(W) if interior(x, y) and interior(x + 1, y + 1)]
        for _ in range(k):
            if not cells:
                break
            x, y = cells[r.pick(len(cells))]
            cv.c[y][x] = 1
            if sc >= 2:
                for dx, dy in ((1, 0), (0, 1), (1, 1)):
                    cv.c[y + dy][x + dx] = 1
    elif pat == "CIRCUIT":
        # horizontal traces with one downward stub and a pad: one orientation only (no rotational motifs)
        for _ in range(1 + sc):
            cells = [(x, y) for y in ys_ for x in range(W) if interior(x, y)]
            x, y = cells[r.pick(len(cells))]
            L = 3 + r.pick(2 + sc)
            xs = [xx for xx in range(x, x + L) if interior(xx, y)]
            for xx in xs:
                cv.c[y][xx] = 1
            if xs:
                ex = xs[-1]
                for k in (1, 2):
                    if interior(ex, y + k):
                        cv.c[y + k][ex] = 1
                if interior(xs[0], y):
                    cv.c[y][xs[0]] = 3
    elif pat == "CRACKS":
        tops = [(x, y) for y in ys_ for x in range(W) if interior(x, y) and not interior(x, y - 1)]
        x, y = tops[r.pick(len(tops))]
        for _ in range(3 + sc * 2):
            if interior(x, y):
                cv.c[y][x] = 1
            y += 1
            x += (-1, 0, 1)[r.pick(3)]
    elif pat == "BAND":
        y0 = by0 + (by1 - by0) * (3 + r.pick(3)) // 10
        for y in range(y0, y0 + max(2, S // 6)):
            for x in range(W):
                if interior(x, y) and (x + y) & 1:
                    cv.c[y][x] = 1
    # ---- face
    lay = face_layout(f, b, sc, limb, lk["eyes"], lk["style"], ps["mouth"] or lk["mouth"])
    if lay is None:
        lay = face_layout(f, b, sc, limb, lk["eyes"], lk["style"], lk["mouth"])
    remap = {"1": "5", "3": "6"} if f == "SERVER" else None
    lit = f == "SERVER" or lk["style"] == "SCREEN"
    sq = ps["squash"]
    for (ex, ey) in lay["eyes"]:
        ey2 = ey + sq // 2 - (1 if ps["eyes"] == "roll" else 0)
        et = lay["et"] if ps["eyes"] != "closed" else [("1" if not lit else "4") * tw(lay["et"])]
        if ps["eyes"] == "closed":
            ey2 += len(lay["et"]) // 2
        cv.stamp(et, ex, ey2, remap=remap if ps["eyes"] != "closed" else None)
        if lit and ps["eyes"] != "closed":  # lit eyes get an ink frame on the body
            for yy in range(ey2 - 1, ey2 + len(et) + 1):
                for xx in range(ex - 1, ex + tw(et) + 1):
                    if interior(xx, yy) and cv.get(xx, yy) in (2, 3):
                        inside = ex <= xx < ex + tw(et) and ey2 <= yy < ey2 + len(et)
                        if not inside:
                            cv.set(xx, yy, 4)
    mx, my = lay["mouth_xy"]
    cv.stamp(lay["mt"], mx, my + sq // 2)
    # ---- toppers (sit on the body top over the face column)
    tcol = info["cx"]
    top_y = next(y for y in range(H) if cv.get(tcol, y))
    tp = lk["topper"]
    if tp != "NONE":
        tt = TOP[tp][0 if sc < 2 else 1]
        tx = tcol - tw(tt) // 2 + (ps["twitch"] if tp in ("ANTENNAE", "CABLE", "SPROUT", "TUFT") else 0)
        ty = top_y - len(tt) + (1 if tp in ("CAP", "BUN", "FIN", "TUFT") else 0)
        if tp == "FIN":  # a fin sits off-centre toward the back, never a centred peak
            tx = tcol + max(1, S // 6)
            col = tx + 1
            ty = next((y for y in range(H) if cv.get(col, y)), top_y) - len(tt) + 1
        cv.stamp(tt, tx, ty)
    # ---- accessories (overlay palette B)
    items = ACC[f][0]
    sel = [items[i] for i in range(len(items)) if lk["acc"] >> i & 1]
    big = 1 if sc >= 2 else 0
    mb = lay["mouth_bot"]
    on_body = lambda x, y: interior(x, y)  # noqa: E731
    cxm = info["cxm"]
    bx0, by0, bx1, by1 = info["bbox"]
    for it in sel:
        if it == "LANYARD":
            card = ACC_T["CARD"][big]
            cyy = min(mb + 2 + sc, by1 - len(card) - 1)
            for y in range(mb, cyy):
                d = (cyy - y) // 2
                for x in (cxm - 1 - d, cxm + d):
                    if on_body(x, y):
                        cv.set(x, y, 5)
            cv.stamp(card, cxm - tw(card) // 2, cyy)
        elif it == "TIE":
            t_ = ACC_T["TIE"][big]
            cv.stamp(t_, cxm - tw(t_) // 2, mb + 1, allow=lambda x, y: bm[y][x] if 0 <= y < H else False)
        elif it == "STICKY NOTE":
            t_ = ACC_T["STICKY"][big]
            cv.stamp(t_, bx1 - tw(t_) - (0 if sc else -1), by0 + (by1 - by0) // 3)
        elif it == "COFFEE STAIN":
            t_ = ACC_T["STAIN"][big]
            cv.stamp(t_, bx0 + 2 + sc, by1 - len(t_) - 1, allow=on_body)
        elif it == "DUST":
            for k in range(3 + sc):
                a = r.pick(16)
                rx, ry = (bx1 - bx0) // 2 + 3, (by1 - by0) // 2 + 2
                x = (bx0 + bx1) // 2 + SIN16[(a + 4) % 16] * rx // 16
                y = (by0 + by1) // 2 + SIN16[a] * ry // 16
                cv.set(x, y, 5)
                if sc >= 2 and k % 2:
                    cv.set(x + 1, y, 5)
        elif it in ("CABLE", "CABLES"):
            for side in ((1,) if it == "CABLE" else (-1, 1)):
                sx_ = bx1 if side > 0 else bx0
                y0 = by0 + (by1 - by0) // 2
                x = sx_
                for y in range(y0, info["ground"] + 1):
                    if not cv.get(x + side, y) or cv.get(x + side, y) == 1:
                        x += side if (y - y0) < 2 else 0
                    cv.set(x, y, 4)
                cv.set(x + side, info["ground"], 5)
                cv.set(x, info["ground"], 5)
        elif it == "WATER LINE":
            y = by0 + (by1 - by0) * 5 // 8
            for x in range(W):
                if on_body(x, y):
                    cv.set(x, y, 5)
                if on_body(x, y + 1) and (x & 1):
                    cv.set(x, y + 1, 5)
        elif it == "BUBBLES":
            t_ = ACC_T["BUBBLE"][big]
            cv.stamp(t_, bx1 + 1, by0 - 1, empty_only=True)
            cv.stamp(ACC_T["BUBBLE"][0], bx1 + 3 + big * 2, by0 - 5 - big * 2, empty_only=True)
        elif it == "LEDS":
            y = by1 - 2 - sc // 2
            n = 0
            for x in range(bx0, bx1):
                if on_body(x, y) and on_body(x - 1, y) and x % 2 == 0 and n < 2 + sc:
                    cv.set(x, y, 5 if n % 2 == 0 else 6)
                    n += 1
        elif it == "PRICE TAG":
            t_ = ACC_T["PRICE"][big]
            x0, y0 = bx0 - 1, by0 + 1
            cv.set(x0 + 1, y0, 4)
            cv.stamp(t_, x0 - tw(t_) + 1, y0 + 1)
        elif it == "BAG":
            t_ = ACC_T["BAG"][big]
            cv.stamp(t_, bx1 - 1, info["ground"] - len(t_) + 1)
        elif it == "VINES":
            for x in range(bx0, bx1 + 1):
                y = by0 + (by1 - by0) // 2 + SIN16[(x * 3) % 16] * max(2, S // 6) // 16
                if on_body(x, y):
                    cv.set(x, y, 5)
                    if (x % 5) == 0 and on_body(x, y - 1):
                        cv.set(x, y - 1, 5)
        elif it == "FLOWER":
            t_ = ACC_T["FLOWER"][big]
            cv.stamp(t_, bx0 - 1, by0 + (by1 - by0) // 4 - 1)
        elif it == "SCARF":
            y = mb + (1 if sc else 0)
            for yy in (y, y + 1) if sc >= 2 else (y,):
                for x in range(W):
                    if bm[yy][x] if 0 <= yy < H else False:
                        cv.set(x, yy, 5 if (yy == y or x & 1) else 4)
            xs = [x for x in range(W) if 0 <= y < H and bm[y][x]]
            if xs:
                for k in range(2 + sc):
                    cv.set(xs[-1] + 1 + k // 2, y + k, 5)
        elif it == "ANTENNA":
            x = bx0 + (bx1 - bx0) // 4
            yt = next((y for y in range(H) if cv.get(x, y)), by0)
            for k in range(1, 4 + sc * 2):
                cv.set(x, yt - k, 4)
            cv.set(x, yt - 4 - sc * 2, 6)
            cv.set(x - 1, yt - 3 - sc * 2, 4)
        elif it == "ROW TEAR":
            y = by0 + (by1 - by0) * (4 + r.pick(3)) // 10
            for x in range(W):
                if cv.get(x, y) and mat[y][x] == 1:
                    cv.set(x, y, 0)
        elif it == "DEAD PIXELS":
            cells = [(x, y) for y in range(by0, by1) for x in range(bx0, bx1) if on_body(x, y) and on_body(x + 1, y)]
            for _ in range(2 + sc):
                x, y = cells[r.pick(len(cells))]
                cv.set(x, y, 5)
                cv.set(x + 1, y, 5)
    # ---- defence tells (body palette, same shape on every mob)
    if lk["defence"] == "ARMOURED":
        pw = max(4, (bx1 - bx0) // 2)
        ph = max(3, S // 5)
        px0 = cxm - pw // 2
        py0 = min(mb + 1, by1 - ph)
        for y in range(py0, py0 + ph):
            for x in range(px0, px0 + pw):
                if not bm[y][x]:
                    continue
                edge = y in (py0, py0 + ph - 1) or x in (px0, px0 + pw - 1)
                corner = (y in (py0, py0 + ph - 1)) and (x in (px0, px0 + pw - 1))
                if corner:
                    continue
                cv.set(x, y, 1 if edge else 3)
        cv.set(px0 + 1 + (sc >= 2), py0 + 1, 2 if sc < 2 else 1)
        cv.set(px0 + pw - 2 - (sc >= 2), py0 + 1, 2 if sc < 2 else 1)
    # ---- tag tells (overlay palette B, colour B3 = the tag's fixed cue colour)
    tg = lk["tag"]
    x0, y0_, x1, y1 = cv.bbox()
    if tg == "COLD":
        for x in range(x0, x1 + 1):
            y = next((y for y in range(H) if cv.get(x, y)), None)
            if y is not None and (x % 2 == 0 or sc == 0):
                cv.set(x, y, 6)
        bots = [x for x in range(bx0 + 1, bx1) if mat[by1][x] == 1 or (bm[by1 - 1][x] and not bm[by1][x])]
        for k, x in enumerate(bots[1::max(2, len(bots) // 3)][:3]):
            yb = max(y for y in range(H) if bm[y][x])
            cv.set(x, yb + 1, 6)
            if k != 1:
                cv.set(x, yb + 2, 6)
    elif tg == "WET":
        for k, fx_ in enumerate((bx0 + (bx1 - bx0) // 5, bx1 - (bx1 - bx0) // 4)):
            yb = max((y for y in range(H) if bm[y][fx_]), default=by1)
            cv.set(fx_, yb + 1, 6)
            cv.set(fx_, yb + 2, 6)
            cv.set(fx_ + (k * 2 - 1) * 2, yb + 5 + k, 6)
            cv.set(fx_ + (k * 2 - 1) * 2, yb + 6 + k, 6)
    elif tg == "HOT":
        t_ = CUE["HOT"]
        cv.stamp(t_, tcol - 2, y0_ - len(t_) - 1)
    elif tg == "ALIVE":
        t_ = CUE["ALIVE"]
        lx = bx0 + (bx1 - bx0) // 4
        ly = next((y for y in range(H) if cv.get(lx, y)), by0)
        cv.stamp(t_, lx - 3, ly - len(t_) + 1, empty_only=True)
    elif tg == "MAGIC":
        cv.stamp(CUE["MAGIC"], x1 + 1, by0 - 1)
    elif tg == "SHOCK":
        cv.stamp(CUE["SHOCK"], x1 + 1, by0 + (by1 - by0) // 3)
    elif tg == "SHINY":
        cv.stamp(CUE["SHINY"], bx0 + 1 + sc // 2, by0 + 1 + sc // 2)
    elif tg == "STONE":
        g = info["ground"]
        cv.stamp(CUE["STONE"], x0 - 3, g - 2, empty_only=True)
        cv.stamp(CUE["STONE"], x1, g - 2, empty_only=True)
    elif tg == "AIRY":
        cv.stamp(CUE["AIRY"], x0 - 4, by0 + (by1 - by0) // 2)
        cv.stamp(CUE["AIRY"], x1 + 1, by1 - 4)
    elif tg == "GLITCH":
        # scanline offset: two body bands slide right by 2, the vacated edge lit in the cue colour
        for yb in (by0 + (by1 - by0) // 3, by0 + (by1 - by0) * 2 // 3):
            for y in (yb, yb + (1 if sc >= 2 else 0)):
                row = cv.c[y][:]
                xs = [x for x in range(W) if row[x]]
                if not xs:
                    continue
                for x in range(W):
                    cv.c[y][x] = row[x - 2] if x >= 2 else 0
                cv.set(xs[0], y, 6)
                cv.set(xs[0] + 1, y, 6)
    if lk["defence"] == "SHIELDED":
        x0, y0_, x1, y1 = cv.bbox()
        cx2, cy2 = x0 + x1 + 1, y0_ + y1 + 1
        rx, ry = (x1 - x0) // 2 + 3, (y1 - y0_) // 2 + 3
        for k in range(64):
            a = k * 2 * math.pi / 64  # (C: a 64-entry ellipse table)
            x = (cx2 + int(round(2 * rx * math.cos(a)))) // 2
            y = (cy2 + int(round(2 * ry * math.sin(a)))) // 2
            if (k // 2) % 3 != 2 and not cv.get(x, y):
                cv.set(x, y, 3 if (k // 2) % 3 == 0 else 1)
    # ---- hover shadow (after everything, stays on the ground)
    if limb == "HOVER":
        g = info["ground"]
        sw = S * 3 // 4
        for x in range(cxm - sw // 2, cxm + sw // 2):
            for y in (g - 1, g):
                d2 = abs(x - cxm) * 2
                if (x + y) & 1 and d2 < sw - (2 if y == g else 0) and not cv.get(x, y):
                    cv.set(x, y, 1)
    # ---- whole-sprite offsets: bob / lunge
    if ps["dx"] or ps["dy"]:
        out = Canvas(W, H)
        for y in range(H):
            for x in range(W):
                v = cv.c[y][x]
                if v:
                    keep_shadow = limb == "HOVER" and y >= info["ground"] - 1 and v == 1 and not bm[y][x]
                    out.set(x if keep_shadow else x + ps["dx"], y if keep_shadow else y + ps["dy"], v)
        cv = out
    if ps["dissolve"]:
        for y in range(H):
            for x in range(W):
                if cv.c[y][x] and E.BAYER[y & 3][x & 3] < ps["dissolve"] * 16:
                    cv.c[y][x] = 0
    return cv


def elite_parts(cv, lk, phase):
    """elite overlay in palette E: pulsing outline, affix crown, orbiting particles"""
    W, H = cv.w, cv.h
    sc = lk["size"]
    solid = [[cv.c[y][x] != 0 for x in range(W)] for y in range(H)]
    ring = []
    for y in range(H):
        for x in range(W):
            if solid[y][x]:
                continue
            if any(0 <= x + dx < W and 0 <= y + dy < H and solid[y + dy][x + dx]
                   for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                ring.append((x, y))
    col = 8 if phase == 0 else 9
    for (x, y) in ring:
        cv.c[y][x] = col
    if sc == 3 or "AURA" in lk["affixes"]:  # XL can't grow: a second, dithered glow ring (also the element aura)
        solid2 = [[cv.c[y][x] != 0 for x in range(W)] for y in range(H)]
        for y in range(H):
            for x in range(W):
                if not solid2[y][x] and (x + y + phase) & 1 and any(
                        0 <= x + dx < W and 0 <= y + dy < H and solid2[y + dy][x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    cv.c[y][x] = 9 if phase == 0 else 8
    x0, y0, x1, y1 = cv.bbox()
    swarm = lk.get("mobility") == "SWARM"
    if not swarm:
        icons = []
        for i, a in enumerate(lk["affixes"]):
            t_ = [row.replace("a", "8" if i == 0 else "9") for row in AFFIX_ICON[a]]
            icons.append(t_)
        tot = sum(tw(t_) for t_ in icons) + 2 * (len(icons) - 1)
        x = (x0 + x1 + 1) // 2 - tot // 2
        for t_ in icons:
            cv.stamp(t_, x, y0 - len(t_) - 2 - phase)
            x += tw(t_) + 2
    cx2, cy2 = x0 + x1, y0 + y1
    rx, ry = (x1 - x0) // 2 + 4, (y1 - y0) // 2 + 2
    n = 4 if sc < 2 else 6
    for k in range(n):
        a = (k * 16 // n + phase * 2) % 16
        x = (cx2 + 2 * SIN16[(a + 4) % 16] * rx // 16) // 2
        y = (cy2 + 2 * SIN16[a] * ry // 16) // 2
        c = 8 if k % 2 == 0 or len(lk["affixes"]) == 1 else 9
        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            if not cv.get(x + dx, y + dy):
                cv.set(x + dx, y + dy, c)
    return cv


def palettes(lk, mode="normal"):
    A = pal_body(lk["fam"], lk["tag"], lk["hue"], lk.get("ramp", 0))
    B = pal_overlay(lk["fam"], lk["tag"], lk["accent"])
    Ep = pal_elite(lk["affixes"], lk["aura"]) if lk["affixes"] else [INK, WHITE, WHITE]
    if mode == "tell":
        A = [A[0], q555(mix(A[2], (255, 236, 140), 10)), WHITE]
    elif mode == "hurt":
        A = [q555(mix(A[0], (255, 80, 140), 6)), WHITE, WHITE]
        B = [B[0], WHITE, WHITE]
    elif mode == "death":
        A = [A[0], q555(mix(A[1], VOID_TONE, 6)), q555(mix(A[2], VOID_TONE, 6))]
    return [None] + A + B + Ep


def to_img(cv, pal):
    im = Image.new("RGBA", (cv.w, cv.h), (0, 0, 0, 0))
    px = im.load()
    for y in range(cv.h):
        for x in range(cv.w):
            v = cv.c[y][x]
            if v:
                px[x, y] = (*pal[v], 255)
    return im


def mob_image(lk, pose=None, phase=0, W=64, H=64):
    if lk.get("mobility") == "MIMIC":
        return mimic_image(lk, phase, W, H)
    work = dict(lk)
    if lk["affixes"] and lk["size"] < 3:  # elite size bump: S->M, M->L, L->XL
        work["size"] = lk["size"] + 1
        if not face_layout(work["fam"], work["body"], work["size"], work["limb"], work["eyes"], work["style"], work["mouth"]):
            work["mouth"] = next(m for m in MOUTHS if face_layout(work["fam"], work["body"], work["size"], work["limb"], work["eyes"], work["style"], m))
    cv = render(work, pose, W, H)
    if lk["affixes"]:
        cv = elite_parts(cv, work, phase)
    im = to_img(cv, palettes(work, (pose or {}).get("pal", "normal")))
    im.info["body_px"] = [im.getpixel((x, y))[:3] for y in range(cv.h) for x in range(cv.w) if cv.c[y][x] in (1, 2, 3)]
    return im


def mimic_image(lk, phase, W=64, H=64):
    """an elite mimic stays an object: only a travelling 3-pixel shimmer on its rim gives it away"""
    o = E.obj(lk.get("mimic_obj", "MUG"))
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    im.paste(o, ((W - 16) // 2, H - 8 - 16 + 2), o)
    if lk["affixes"]:
        pal = pal_elite(lk["affixes"], lk["aura"])
        px = im.load()
        ring = []
        for y in range(H):
            for x in range(W):
                if px[x, y][3]:
                    continue
                if any(0 <= x + dx < W and 0 <= y + dy < H and px[x + dx, y + dy][3] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    ring.append((x, y))
        cx, cy = W // 2, H - 16
        ring.sort(key=lambda p: math.atan2(p[1] - cy, p[0] - cx))
        st = (phase * len(ring) // 2 + len(ring) // 8) % len(ring)
        for k in range(3):
            x, y = ring[(st + k * 2) % len(ring)]
            px[x, y] = (*pal[1 + (k == 1)], 255)
    return im


# =====================================================================================================================
# counting
# =====================================================================================================================
def count():
    res = {}
    for f in FAMILIES:
        tot_look, tot_full = 0, 0
        for b in range(len(BODIES[f])):
            for sc in range(4):
                for limb in allowed_limbs(f, b, sc):
                    faces = len(valid_faces(f, b, sc, limb))
                    n = (len(allowed_toppers(f, b, sc)) * faces * len(allowed_patterns(sc)) *
                         len(allowed_acc(f, sc)) * 2 * len(ramps(f)) * len(HUE_SHIFTS))
                    tot_look += n
                    tot_full += n * len(TAGS) * len(allowed_defences(sc))
        res[f] = (tot_look, tot_full)
    return res


def elite_sets():
    singles = len(AFFIXES) - 1 + 10  # AURA comes in 10 elements
    pairs = 0
    for a, b in combinations(AFFIXES, 2):
        pairs += (10 if "AURA" in (a, b) else 1)
    return singles, pairs


# =====================================================================================================================
# sheets
# =====================================================================================================================
def bg_cell(kind, w, h, seed):
    if kind == "floor":
        t = Image.new("RGB", (w, h))
        for y in range(0, h, 16):
            for x in range(0, w, 16):
                t.paste(E.slab("lilac", seed + x * 3 + y * 7), (x, y))
        return t
    v = E.void(seed + 3)
    return v.crop((seed * 7 % 90, seed * 5 % 70, seed * 7 % 90 + w, seed * 5 % 70 + h)) if w <= 70 else v.resize((w, h))


class Stats:
    def __init__(self):
        self.hues = [0] * 12
        self.npx = 0
        self.bhues = [0] * 12
        self.bpx = 0
        self.min_outline = 99.0
        self.min_body = 99.0
        self.min_acc = 99.0
        self.min_floor_dist = 999
        self.worst = None

    def add(self, im, lk):
        for (r, g, b, a) in im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata():
            if not a:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if s > 0.18 and v > 0.2:
                self.hues[int(h * 12) % 12] += 1
                self.npx += 1
        for (r, g, b) in im.info.get("body_px", []):
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if s > 0.12 and v > 0.2:
                self.bhues[int(h * 12) % 12] += 1
                self.bpx += 1
        if lk.get("mobility") == "MIMIC":
            return
        A = pal_body(lk["fam"], lk["tag"], lk["hue"], lk.get("ramp", 0))
        B = pal_overlay(lk["fam"], lk["tag"], lk["accent"])
        co = min(contrast(A[0], FLOOR_LIGHT), contrast(A[0], FLOOR_BASE))
        cb = contrast(A[1], VOID_TONE)
        ca = min(max(contrast(B[1], FLOOR_LIGHT), contrast(B[1], VOID_TONE)), 99)
        if co < self.min_outline:
            self.min_outline, self.worst = co, mob_name(lk)
        self.min_body = min(self.min_body, cb)
        self.min_floor_dist = min(self.min_floor_dist, int(math.dist(A[1], FLOOR_BASE)))
        self.min_acc = min(self.min_acc, ca)

    def report(self):
        tot = max(1, self.npx)
        fr = [h / tot for h in self.hues]
        bins = sum(1 for v in fr if v >= 0.03)
        bf = [h / max(1, self.bpx) for h in self.bhues]  # body pixels (palette A) with a hue
        bb, bmax = sum(1 for v in bf if v >= 0.03), max(bf)
        # circular spread (deg) of saturated pixels
        sx = sum(fr[i] * math.cos(i * math.pi / 6 + math.pi / 12) for i in range(12))
        sy = sum(fr[i] * math.sin(i * math.pi / 6 + math.pi / 12) for i in range(12))
        R = math.hypot(sx, sy)
        spread = math.degrees(math.sqrt(max(0, -2 * math.log(max(R, 1e-6)))))
        return {"hue_bins_3pct": bins, "hue_circ_std_deg": round(spread, 1), "min_outline_vs_floor": round(self.min_outline, 2),
                "min_body_vs_void": round(self.min_body, 2), "min_accent_best_bg": round(self.min_acc, 2), "min_body_rgb_dist_floor": self.min_floor_dist,
                "body_hue_bins_3pct": bb, "body_max_bin": round(bmax, 3),
                "body_hue_rule_ok": bb >= 5 and bmax <= 0.35, "monotone": bins < 4}


def up(im, k):
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def family_sheet(fi):
    f = FAMILIES[fi]
    C = 64
    head = 20
    sheet = Image.new("RGB", (8 * C, head + 8 * C), E.INK)
    E.panel(sheet, 0, 0, 8 * C - 1, head - 3)
    E.text(sheet, 4, 6, f"MOBGEN {FAM_LONG[fi]}  64 LOOKS", E.WHITE)
    E.text(sheet, 8 * C - 8 * 9 - 4, 6, f"SEED 0X{0xE6 + fi:02X}", E.CYAN)
    st = Stats()
    for i in range(64):
        cx, cy = i % 8, i // 8
        seed = mix_seed(0xE6 + fi, i)
        mobility = None
        if i % 16 == 5:
            mobility = "SWARM"
        elif i % 16 == 11:
            mobility = "TURRET"
        lk = gen(seed, fam=f, mobility=mobility)
        if i % 8 == 7:
            lk = make_elite(lk, seed)
        kind = "floor" if (cx + cy) % 2 == 0 else "void"
        cell = bg_cell(kind, C, C, i).convert("RGBA")
        im = mob_image(lk, None, i // 8 % 2)
        st.add(im, lk)
        cell.alpha_composite(im)
        sheet.paste(cell.convert("RGB"), (cx * C, head + cy * C))
    p = OUT / "mobs" / f"family_{f.lower()}.png"
    up(sheet, 3).save(p)
    return p, st.report()


def label(im, x, y, s, col=E.WHITE):
    E.text(im, x, y, s, col)


def main_sheet():
    W = 13 * 52
    Hh = 560
    im = Image.new("RGB", (W, Hh), E.INK)
    st = Stats()
    E.panel(im, 0, 0, W - 1, 17)
    label(im, 4, 6, "EGRESS.EXE  MOBGEN  SAME SEED RULES, DIFFERENT LOOKS", E.WHITE)
    y = 22
    # ---- row A: 8 looks of one family, same tag (WET) -----------------------------------------------------------
    label(im, 2, y, "8 LOOKS  OFFICE FAMILY  TAG COLD  (FROST RIM + OFFICE PROP STAY)", E.YEL)
    y += 11
    cw = W // 8
    for i in range(8):
        lk = gen(mix_seed(0xA0, i), fam="OFFICE", tag="COLD", defence="NONE", body=i % 4, size=[1, 2, 1, 3, 2, 1, 2, 0][i])
        cell = bg_cell("floor" if i % 2 == 0 else "void", cw, 64, i + 40).convert("RGBA")
        m = mob_image(lk)
        st.add(m, lk)
        cell.alpha_composite(m, ((cw - 64) // 2, 0))
        im.paste(cell.convert("RGB"), (i * cw, y))
        label(im, i * cw + 2, y + 66, mob_name(lk)[:9], E.CYAN)
    y += 78
    # ---- row B: tag tells on one mob ----------------------------------------------------------------------------
    label(im, 2, y, "TAG TELLS  SAME MOB, EACH TAG  (PLAYERS LEARN TAGS, NOT LOOKS)", E.YEL)
    y += 11
    base = gen(mix_seed(0xB0, 2), fam="OFFICE", body=0, size=1, tag="NONE", defence="NONE")
    base.update(limb="LEGS2", topper="NONE", pattern="NONE", eyes=2, style="DOT", mouth="SMILE", acc=2)
    cols = [("NONE", "NONE"), ("COLD", "NONE"), ("WET", "NONE"), ("HOT", "NONE"), ("ALIVE", "NONE"), ("MAGIC", "NONE"),
            ("GLITCH", "NONE"), ("SHOCK", "NONE"), ("SHINY", "NONE"), ("STONE", "NONE"), ("AIRY", "NONE"),
            ("NONE", "ARMOURED"), ("NONE", "SHIELDED")]
    for i, (tg, df) in enumerate(cols):
        lk = dict(base, tag=tg, defence=df)
        cell = bg_cell("floor" if i % 2 == 0 else "void", 52, 52, i + 70).convert("RGBA")
        m = mob_image(lk).crop((6, 10, 58, 62))
        st.add(m, lk)
        cell.alpha_composite(m)
        im.paste(cell.convert("RGB"), (i * 52, y))
        lab = {"NONE": "PLAIN", "ARMOURED": "ARMOUR", "SHIELDED": "SHIELD"}.get(df if df != "NONE" else tg, tg)
        label(im, i * 52 + max(0, (52 - 8 * len(lab)) // 2) + 1, y + 54, lab, ELEMENT[tg][1] if tg != "NONE" else E.WHITE)
    y += 66
    # ---- row C: elites ------------------------------------------------------------------------------------------
    label(im, 2, y, "ELITES  PULSE PHASE 1 / PHASE 2  ZOOM X2  ANY MOB, ANY SIZE", E.YEL)
    y += 11
    pw = W // 4
    sw = gen(mix_seed(0xC0, 1), fam="ATRIUM", mobility="SWARM", tag="WET", defence="NONE", body=0)
    sw = make_elite(sw, 1, affixes=["HASTED"])
    tu = gen(mix_seed(0xC0, 2), fam="SERVER", mobility="TURRET", size=1, tag="COLD", defence="NONE", body=1)
    tu = make_elite(tu, 2, affixes=["AURA"], aura="COLD")
    mi = dict(gen(mix_seed(0xC0, 3), fam="MALL", size=1, tag="NONE", defence="NONE"), mobility="MIMIC", mimic_obj="MUG")
    mi = make_elite(mi, 3, affixes=["VAMPIRIC"])
    lg = gen(mix_seed(0xC0, 4), fam="GARDEN", size=2, tag="ALIVE", defence="ARMOURED", body=1)
    lg = make_elite(lg, 4, affixes=["VAMPIRIC", "HASTED"])
    elites = [(sw, "SWARM S>M", "GLOW+PARTICLES ONLY"), (tu, "TURRET M>L", None), (mi, "MIMIC (HIDDEN)", None),
              (lg, "LARGE L>XL 2 AFFIX", None)]
    for k, (lk, cap, note) in enumerate(elites):
        x0 = k * pw
        tagy = y
        if lk.get("mobility") not in ("SWARM", "MIMIC"):
            E.chip(im, x0 + 4, tagy, "ELITE", E.PINK)
            words = " ".join(AFFIX_WORD[a] if a != "AURA" else f"{lk['aura']} AURA" for a in lk["affixes"])
            label(im, x0 + 4, tagy + 12, words[:19], pal_elite(lk["affixes"], lk["aura"])[1])
            label(im, x0 + 4, tagy + 21, mob_name(lk)[:19], E.WHITE)
        elif lk.get("mobility") == "SWARM":
            label(im, x0 + 4, tagy + 12, "NO TAG/CROWN", (150, 116, 210))
            label(im, x0 + 4, tagy + 21, "ON SWARMS", (150, 116, 210))
        else:
            label(im, x0 + 4, tagy + 12, "NO TAG: IT IS", (150, 116, 210))
            label(im, x0 + 4, tagy + 21, "STILL A MUG", (150, 116, 210))
        for ph in (0, 1):  # shown at x2 (a zoom inset: the sprite itself is 1x like everything else)
            cell = bg_cell("void" if ph else "floor", 40, 56, k * 2 + ph + 90).convert("RGBA")
            m = mob_image(lk, None, ph, 40, 56)
            st.add(m, lk)
            cell.alpha_composite(m)
            im.paste(up(cell.convert("RGB"), 2), (x0 + 4 + ph * 82, y + 32))
        label(im, x0 + 4, y + 146, cap[:19], E.CYAN)
    y += 160
    # ---- row D: state strip -------------------------------------------------------------------------------------
    label(im, 2, y, "STATES FROM PARTS  POSE OFFSETS + PART SWAPS + PALETTE SWAPS", E.YEL)
    y += 11
    am = gen(mix_seed(0xD0, 7), fam="ROOFTOP", body=0, size=2, tag="HOT", defence="NONE")
    am.update(limb="LEGS2", quirk="BREATHE", topper="TUFT", mouth="LINE")
    frames = [("IDLE 1", {}), ("IDLE 2", {"squash": 1}), ("MOVE 1", {"leg": 1, "dy": -1}), ("MOVE 2", {"dx": 1}),
              ("TELL", {"squash": 2, "dx": -1, "pal": "tell", "eyes": "roll"}), ("ATTACK", {"dx": 4, "mouth": "O"}),
              ("HURT", {"pal": "hurt", "eyes": "closed", "dx": -2}), ("DEATH 1", {"pal": "death", "dissolve": 0.4, "eyes": "closed"}),
              ("DEATH 2", {"pal": "death", "dissolve": 0.8, "eyes": "closed", "dy": 2})]
    fw = W // len(frames)
    for i, (nm, pose) in enumerate(frames):
        cell = bg_cell("floor" if i % 2 == 0 else "void", fw, 56, i + 120).convert("RGBA")
        m = mob_image(am, pose).crop((32 - fw // 2, 10, 32 - fw // 2 + fw, 66))
        if nm == "TELL":
            mp = m.load()
            for (dx, dy) in ((0, -1), (0, 0), (0, 1), (0, 3)):  # the '!' flash over the head
                mp[fw // 2 + 6, 12 + dy] = (*E.YEL, 255)
        if nm.startswith("DEATH"):
            dd = Image.new("RGB", (160, 128), (0, 0, 0))
            E.puff(dd, 80, 64, 5 if nm == "DEATH 1" else 8, seed=i)
            pf = dd.crop((80 - fw // 2, 64 - 34, 80 - fw // 2 + fw, 64 + 22)).convert("RGBA")
            pd = pf.load()
            for yy in range(pf.height):
                for xx in range(pf.width):
                    r_, g_, b_, _ = pd[xx, yy]
                    pd[xx, yy] = (r_, g_, b_, 0 if (r_, g_, b_) == (0, 0, 0) or (xx + yy) % 3 else 255)
            m.alpha_composite(pf)
        if i < 7:
            st.add(m, am)
        cell.alpha_composite(m)
        im.paste(cell.convert("RGB"), (i * fw, y))
        label(im, i * fw + max(0, (fw - 8 * len(nm)) // 2), y + 58, nm, E.CYAN)
    y += 70
    res = count()
    tl = sum(v[0] for v in res.values())
    tf = sum(v[1] for v in res.values())
    label(im, 2, y, f"LOOKS {tl:,}  WITH TAG+DEFENCE CUES {tf:,}".replace(",", " "), E.WHITE)
    label(im, 2, y + 10, "BODY PAL A 3 COL + OVERLAY PAL B 3 COL + ELITE PAL 7", (150, 116, 210))
    im = im.crop((0, 0, W, y + 20))
    p = OUT / "mock_mobs.png"
    up(im, 3).save(p)
    return p, st.report()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "count":
        res = count()
        for f, (a, b) in res.items():
            print(f"{f:8s} looks {a:>16,}  with tag+defence {b:>18,}")
        print("TOTAL    looks", f"{sum(v[0] for v in res.values()):,}", " with tag+defence",
              f"{sum(v[1] for v in res.values()):,}")
        s, p = elite_sets()
        print("elite affix sets: singles", s, "pairs", p)
        return
    (OUT / "mobs").mkdir(parents=True, exist_ok=True)
    for fi in range(8):
        p, rep = family_sheet(fi)
        print(p.relative_to(ROOT), rep)
    p, rep = main_sheet()
    print(p.relative_to(ROOT), rep)


if __name__ == "__main__":
    main()
