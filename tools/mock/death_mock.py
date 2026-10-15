"""EGRESS death sequence mock (docs/redesign.md, section 12b). Design-stage only: nothing here is game code.

    .venv/bin/python tools/mock/death_mock.py            # writes docs/shots/death_card.png, death_collapse.png,
                                                         # death_sequence.gif, death_variants.png
    .venv/bin/python tools/mock/death_mock.py --grammar  # prints cause-of-death and epitaph examples (for the doc)

Every frame is composed the way the cartridge would draw it, then checked:
- the BG/window layer is legalised to 8x8 tiles against the phase's palette set (at most 8 BG palettes x 4 colours,
  shared by BG and window, so the scene's and the card's sets together never pass 8);
- the palette drain, the pastel void and the light are palette writes (the tile maps stay put);
- sprites (the heir, falling quarters, motes, icons) are 3 colours + transparent, and the per-line count is reported.
"""
from __future__ import annotations

import math
import random
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for _p in (HERE, ROOT / "proto/genetics", ROOT / "proto/anim"):
    sys.path.insert(0, str(_p))
import egkit as E  # noqa: E402
from egkit import CYAN, INK, PINK, WHITE, YEL, P, obj, small, text  # noqa: E402
import genes  # noqa: E402
import sprite_styles as SS  # noqa: E402

OUT = ROOT / "docs/shots"
HZ = 60
LILAC = (214, 190, 246)
GREEN = (140, 230, 150)

# =====================================================================================================================
# 1. the cause-of-death grammar and the epitaph pools (data the cartridge would carry; ~1.1 KB of strings)
# =====================================================================================================================
# A death record is filled at the killing blow (it is what the lineage ring stores, 3 bytes, see the doc):
#   killer: kind + id (mob look, object, boss, hazard cell, reaction, void)
#   how:    the reaction or damage that landed (picks the VERB)
#   owner:  who set the killer in motion: SELF (pushed, made, reflected, planted, poured), MOB, BOSS, ROOM
#   ctx:    flags at the moment: mid-dodge, carrying, one hit from a kill, seconds on the floor, boss phase
VERB = {  # damage/reaction -> past participle (<= 11 chars)
    "CRUSH": "CRUSHED", "BURN": "COOKED", "STEAM": "STEAMED", "FREEZE": "FROZEN", "SHORT": "SHORTED OUT",
    "OVERGROW": "OVERGROWN", "GLITCH": "UNRENDERED", "SHATTER": "SHATTERED", "DROWN": "DROWNED", "MUD": "STUCK",
    "BEAM": "ERASED", "LAVA": "MELTED",
}
BIOME_HIT = {  # a plain hit takes the biome's verb, so the line sounds like where it happened
    "OFFICE": "FILED AWAY", "VENTS": "VENTED", "ATRIUM": "DUNKED", "SERVER": "DEFRAGGED", "MALL": "RUNG UP",
    "GARDEN": "COMPOSTED", "ROOFTOP": "BLOWN OFF", "GLITCH": "DELETED",
}
PRON = {0: ("THEY", "THEIR", "THEMSELF"), 1: ("HE", "HIS", "HIMSELF"), 2: ("SHE", "HER", "HERSELF")}
OWN_VERB = {"PUSHED": "PUSHED", "MADE": "MADE", "BREWED": "BREWED", "POURED": "POURED", "PLANTED": "PLANTED",
            "REFLECTED": "REFLECTED", "DUG": "DUG", "THREW": "THREW", "LIT": "LIT"}
CTX = [  # (flag, short clause) in priority order; the first that fits wins
    ("one_hit", "ONE HIT SHORT"), ("mid_dodge", "MID-{DODGE}"), ("carrying", "{ITEM} IN HAND"),
    ("fast", "{SECS}S IN"), ("phase", "PHASE {PHASE}"), ("last_heart", "LAST HEART"),
]


def wrap(s, n=20):
    out, cur = [], ""
    for w in s.split():
        if len(cur) + len(w) + (1 if cur else 0) > n:
            out.append(cur)
            cur = w
        else:
            cur = (cur + " " + w).strip()
    if cur:
        out.append(cur)
    return out


def split2(sent, n=18):
    """the sentence on 1 line, or broken at the word boundary that keeps both lines <= n (most balanced)"""
    if len(sent) <= n:
        return [sent]
    best = None
    for i, ch in enumerate(sent):
        if ch == " " and len(sent[:i]) <= n and len(sent[i + 1:]) <= n:
            k = abs(len(sent[:i]) - len(sent[i + 1:]))
            if best is None or k < best[0]:
                best = (k, [sent[:i], sent[i + 1:]])
    return best[1] if best else None


def a_an(noun):
    return ("AN " if noun[0] in "AEIOU" and not noun.startswith("U") else "A ") + noun


def cause(d):
    """d: the death record (dict). Returns 1..2 lines of <= 18 chars. Fixed grammar, tried longest-first:
       CAUSE  := VERB BY KILLER [ / TWIST ]
       KILLER := POSS OWN NOUN        when owner is SELF and the thing is a reaction or projectile ("HER OWN STEAM")
               | A/AN NOUN | THE BOSS
       TWIST  := PRON OWN_VERB REFLEX ("SHE PUSHED HERSELF")  when owner is SELF and the thing is an object
               | PRON OWN_VERB IT | CTX clause | ''
       special: void fall = "LET GO OF / THE FLOOR ON F{n}"; boss = boss line replaces the twist (variation)."""
    he, his, self_ = PRON[d.get("look", 0)]
    if d["how"] == "VOID":
        return ["LET GO OF", f"THE FLOOR ON F{d['floor']}"]
    verb = VERB.get(d["how"]) or BIOME_HIT[d["biome"]]
    noun = d["killer"]
    own = d.get("owner") == "SELF"
    if own and d.get("thing") == "reaction":
        kps = [f"{his} OWN {noun}"]
    else:
        kps = [noun if d.get("thing") == "boss" else a_an(noun)]
    twists = []
    if own and d.get("thing") != "reaction":
        ov = OWN_VERB[d["own_verb"]]
        twists += [f"{he} {ov} {self_}", f"{he} {ov}"]  # a relative clause: "A CRT SHE PUSHED HERSELF"
    for flag, cl in CTX:
        if d.get(flag):
            twists.append(", " + cl.format(DODGE=d.get("dodge", "ROLL"), ITEM=d.get("item", "MUG"),
                                           SECS=d.get("secs", 8), PHASE=d.get("phase", 3)))
    twists.append("")
    for kp in kps:
        for tw in twists:
            sent = f"{verb} BY {kp}" + (tw if tw.startswith(",") or not tw else " " + tw)
            lines = split2(sent)
            if lines:
                return lines
            l1, l2 = f"{verb} BY {kp}", tw.lstrip(", ")
            if tw and len(l1) <= 18 and len(l2) <= 18:
                return [l1, l2]
    return [verb, noun[:18]]


EPITAPH = {  # the strongest-leaning truth axis and its sign pick the pool; the seed picks the line. Never a reveal.
    ("SELF", +1): ["THE CHAIR WAS WARM", "SAME DESK, AGAIN", "THE BADGE FIT", "SHE KNEW THE WAY"],
    ("SELF", -1): ["A STRANGER'S SHOES", "NOBODY KNEW HER", "THE NAME WAS NEW"],
    ("CAUSE", +1): ["SOMEONE MIXED THIS", "IT WAS PUT HERE", "THE BENCH WAS WARM"],
    ("CAUSE", -1): ["THE FLOOR GAVE UP", "ALWAYS LOOSE", "THE BUILDING SAGS"],
    ("CULPRIT", +1): ["SHE HAD THE KEY", "HER HANDS, AGAIN", "SHE OPENED IT"],
    ("CULPRIT", -1): ["THEY KNEW HER NAME", "SOMEONE WATCHED", "THE DOOR WAS DRAWN"],
    ("REVERSIBLE", +1): ["NOT YET. NOT HERE", "IT CAN BE PUT BACK", "THE STAIRS WAIT"],
    ("REVERSIBLE", -1): ["SOME DOORS STAY", "THE STAIRS WENT", "NOTHING CAME BACK"],
}


def epitaph(lean, seed):
    """lean: {axis: signed byte}. The sex of the pronoun in a line is rewritten for the heir (SHE/HE/THEY)."""
    ax = max(lean, key=lambda a: abs(lean[a]))
    pool = EPITAPH[(ax, 1 if lean[ax] >= 0 else -1)]
    return pool[seed % len(pool)]


GRAMMAR_EXAMPLES = [
    dict(how="CRUSH", killer="CRT", thing="object", owner="SELF", own_verb="PUSHED", look=2, biome="OFFICE"),
    dict(how="FREEZE", killer="STEAM", thing="reaction", owner="SELF", look=2, biome="VENTS"),
    dict(how="SHORT", killer="PUDDLE", thing="object", owner="SELF", own_verb="POURED", look=1, biome="SERVER"),
    dict(how="HIT", killer="DAMP INTERN", thing="mob", owner="MOB", mid_dodge=True, dodge="ROLL", look=0,
         biome="VENTS"),
    dict(how="BURN", killer="ORB", thing="reaction", owner="SELF", look=1, biome="MALL"),
    dict(how="OVERGROW", killer="SPROUT", thing="object", owner="SELF", own_verb="PLANTED", look=2, biome="GARDEN"),
    dict(how="BURN", killer="SORRY LAMP", thing="mob", owner="MOB", last_heart=True, look=2, biome="SERVER"),
    dict(how="BEAM", killer="OLD MONITOR", thing="boss", owner="BOSS", phase=3, look=0, biome="SERVER"),
    dict(how="DROWN", killer="LEAK", thing="object", owner="ROOM", carrying=True, item="MUG", look=1,
         biome="ATRIUM"),
    dict(how="VOID", floor=2, look=2, biome="ROOFTOP"),
    dict(how="HIT", killer="RIME TOASTER", thing="mob", owner="MOB", fast=True, secs=4, look=0, biome="MALL"),
    dict(how="GLITCH", killer="NOCLIP MOTH", thing="mob", owner="MOB", look=1, biome="GLITCH"),
]

# =====================================================================================================================
# 2. GBC legality: 8x8 tiles mapped to <= 8 palettes x 4 colours; sprite lines
# =====================================================================================================================


def legalize(img, pals):
    """img RGB (160x144, multiple-of-8 sizes); pals: list of <= 8 lists of 4 RGB. Returns (palid[ty,tx], idx[y,x])"""
    a = np.asarray(img.convert("RGB"), dtype=np.int32)
    h, w, _ = a.shape
    pl = np.array([p + [p[-1]] * (4 - len(p)) for p in pals], dtype=np.int32)  # (n,4,3)
    t = a.reshape(h // 8, 8, w // 8, 8, 3).transpose(0, 2, 1, 3, 4).reshape(h // 8, w // 8, 64, 1, 1, 3)
    d = ((t - pl[None, None, None]) ** 2).sum(-1)  # (ty,tx,64,n,4)
    best = d.min(-1)  # (ty,tx,64,n)
    pid = best.sum(2).argmin(-1)  # (ty,tx)
    idx = np.take_along_axis(d.argmin(-1), pid[:, :, None, None], 3)[..., 0]  # (ty,tx,64)
    idx = idx.reshape(h // 8, w // 8, 8, 8).transpose(0, 2, 1, 3).reshape(h, w)
    return pid, idx


def paint(pid, idx, pals):
    h, w = idx.shape
    pl = np.array([p + [p[-1]] * (4 - len(p)) for p in pals], dtype=np.uint8)
    pp = np.repeat(np.repeat(pid, 8, 0), 8, 1)
    return Image.fromarray(pl[pp, idx], "RGB")


def tile_stats(pid, idx):
    h, w = idx.shape
    seen = set()
    for ty in range(h // 8):
        for tx in range(w // 8):
            b = idx[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            keys = [b.tobytes(), b[:, ::-1].tobytes(), b[::-1].tobytes(), b[::-1, ::-1].tobytes()]
            if not any(k in seen for k in keys):
                seen.add(keys[0])
    return len(seen), len(set(pid.flatten().tolist()))


def obj_lines(objs):
    """objs: (x, y, w, h) boxes of sprite art; 8x16 OBJ mode. Returns the worst scanline's sprite count."""
    worst = 0
    for line in range(144):
        n = 0
        for (x, y, w, h) in objs:
            if y <= line < y + h and x < 160 and x + w > 0:
                n += (w + 7) // 8
        worst = max(worst, n)
    return worst


# =====================================================================================================================
# 3. palettes
# =====================================================================================================================
def lum(c):
    return 0.3 * c[0] + 0.55 * c[1] + 0.15 * c[2]


def greyof(c):  # the drained grey: a cool lilac-grey at the colour's luminance (not dead neutral grey)
    v = lum(c)
    return (int(v * 0.92 + 8), int(v * 0.9 + 6), int(min(255, v * 0.96 + 22)))


def lerp(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def drain(pal, k):  # k = 0..4 palette steps (one write per step, 64 bytes)
    return [lerp(c, greyof(c), k / 4) for c in pal]


VOID = P["void"]
PASTEL = [(196, 172, 236), (238, 186, 222), (255, 222, 230), (255, 250, 244)]  # the pastel void (same tiles)
UNDER = [P["wall"][0], P["wall"][1], VOID[0], VOID[1]]
FLOOR = P["peach"]
WALL = P["wall"]
HUDP = [P["hud"][0], P["hud"][1], (40, 26, 86), CYAN]
HUDH = [P["hud"][0], (40, 26, 86), PINK, WHITE]
LIGHT = [VOID[0], VOID[1], (255, 236, 214), (255, 252, 240)]
OBJP = [P["peach"][1], (30, 22, 60), (170, 150, 210), (90, 230, 240)]  # objects at rest are BG (the CRT)
SCENE = [FLOOR, WALL, VOID, UNDER, HUDP, HUDH, LIGHT, OBJP]  # authoring set for the room (BG palettes 0..7)

# card / carry / tree (window) palette sets: 5 slots; the scene keeps 3 (void, light, the last tile)
CARD_BG = (36, 26, 70)
CARD_HI = (58, 40, 104)
CARD = [  # BG palettes 3..6 while the card is up (the scene keeps 0..2, the portrait takes 7)
    [INK, CARD_HI, LILAC, CYAN],  # 3 header band: MEMORIAL, GEN, the rim line
    [INK, CARD_BG, CARD_HI, WHITE],  # 4 body text, dividers
    [INK, CARD_BG, PINK, CYAN],  # 5 name, dodge + trait
    [INK, CARD_BG, LILAC, YEL],  # 6 clock labels, barcode, epitaph, stat numbers
]
SCENE_LATE = [VOID, LIGHT, [P["wall"][0], P["peach"][0], P["peach"][1], P["peach"][2]]]


# =====================================================================================================================
# 4. the family, the heir, faces
# =====================================================================================================================
DUMP = ROOT / "build/host/avatar_dump"


def face_of(g, closed=False):
    hexg = genes.face_genome(g)
    out = subprocess.run([str(DUMP), "genome", hexg, "talk"], capture_output=True, text=True, check=True).stdout
    lines = [ln.split() for ln in out.strip().split("\n")]
    ln = lines[1] if closed and len(lines) > 1 else lines[0]
    return E.AP.face_image(ln[0], [int(x, 16) for x in ln[1:5]]), [E.AP.rgb(int(x, 16)) for x in ln[1:5]]


def pick_family():
    for seed in range(1, 600):
        hist = genes.run("random", seed * 7919)
        for gi in range(3, 10):
            dead = hist[gi - 1]["pick"]
            pd = genes.express(dead)
            sibs = hist[gi]["sibs"]
            ps = [genes.express(x) for x in sibs]
            if dead["look"] != 2 or pd["dodge"] != "SLIDE" or "SMALL" not in pd["traits"]:
                continue
            if len({q["dodge"] for q in ps}) == 3 and sum("SMALL" in q["traits"] for q in ps) == 2:
                return hist[gi - 2]["pick"], dead, sibs, gi
    raise SystemExit("no family")


GRAND, DEAD, SIBS, GEN = pick_family()
HEIR = SS.make_heir(DEAD, "CORA", rim=YEL)
HP = genes.express(DEAD)
SIBN = ["VEDA", "OREN", "PIM"]
DEATH = dict(how="CRUSH", killer="CRT", thing="object", owner="SELF", own_verb="PUSHED", look=DEAD["look"],
             biome="OFFICE")
CAUSE = cause(DEATH)
LEAN = {"SELF": 52, "CAUSE": -20, "CULPRIT": 31, "REVERSIBLE": -8}
EPI = epitaph(LEAN, 0)


def heir_frame(pose, d=0, drain_k=0, lit=0):
    v = dict(HEIR)
    pal = list(HEIR["pal"])
    if drain_k:
        pal = drain(pal, drain_k)
    if lit:
        pal = [lerp(c, (255, 244, 226), 0.22 * lit) for c in pal]
    v["pal"] = pal
    return SS.render("B", v, d, pose)


# death animation per dodge style: (ticks, pose, dx) -- the heir's own dodge gene picks the fall
DEATH_ANIM = {
    "ROLL": [(6, dict(flash="white", sq=2), 0), (8, dict(crouch=2, eyes="closed", sq=1), 0),
             (5, dict(ball=1), 2), (5, dict(ball=2), 4), (16, dict(lie=True, eyes="closed"), 5)],
    "SLIDE": [(6, dict(flash="white", sq=2), 0), (8, dict(crouch=4, lean=3, stride=3, fx=["dust:1"]), 3),
              (8, dict(crouch=3, lean=2, stride=2, fx=["dust:2"]), 6), (18, dict(crouch=3, eyes="closed", hdy=2, lean=1), 7)],
    "BLINK": [(6, dict(flash="white", sq=2), 0), (6, dict(dissolve=0.5, glitch=2), 0),
              (6, dict(dissolve=0.9, glitch=3), 3), (6, dict(dissolve=0.5, glitch=1, flash="dim"), -2),
              (16, dict(crouch=3, eyes="closed", sq=1, glitch=1), 0)],
}


# =====================================================================================================================
# 5. the room (office, F3), drawn in authoring colours, cell by cell so it can come apart
# =====================================================================================================================
COLS, ROWS, OX, OY = 8, 5, 16, 24  # 8x5 cells of 16 px from (16,24): row 0 wall, 1..4 floor
BODY = (4, 3)  # the cell CORA dies on
CELLS = [(c, r) for r in range(ROWS) for c in range(COLS)]


def cell_xy(c, r):
    return OX + c * 16, OY + r * 16


def void_layer(seed=7):
    return E.void(seed, grid=True)


def draw_room(present, cracked=(), crt_broken=False):
    im = void_layer()
    pos = set(present)
    for (c, r) in sorted(present, key=lambda q: q[1]):
        x, y = cell_xy(c, r)
        if r == 0:
            continue
        E.put(im, E.slab("peach", c * 9 + r * 5), x, y)
        if (c, r + 1) not in pos:
            E.underside(im, x, y + 16, depth=6)
    for (c, r) in present:  # the wall row: slab top over a vent-grille face
        if r == 0:
            x, y = cell_xy(c, r)
            E.wall(im, x, y + 8, h=8)
            if (c, 1) not in pos:
                E.underside(im, x, y + 16, depth=6)
            if c == 6:  # the door (drawn, shut)
                d = ImageDraw.Draw(im)
                d.rectangle((x + 3, y - 2, x + 12, y + 15), fill=INK)
                d.rectangle((x + 5, y, x + 10, y + 15), fill=WALL[0])
                d.line((x + 7, y + 2, x + 7, y + 13), fill=PINK)
    if (5, 3) in pos:  # the CRT she pushed: at rest it is committed to the BG (object palette), cracked
        x, y = cell_xy(5, 3)
        E.put(im, crt_sprite(crt_broken), x, y)
    d = ImageDraw.Draw(im)
    for (c, r) in cracked:  # one frame of crack tiles
        x, y = cell_xy(c, r)
        if r == 0:
            y += 1
        pts = [(x + 2, y + 5), (x + 6, y + 8), (x + 9, y + 6), (x + 13, y + 10)]
        d.line(pts, fill=FLOOR[0])
        d.line([(x + 6, y + 8), (x + 7, y + 13)], fill=FLOOR[0])
    return im


def hud_layer(im, hearts=0, show=True):
    if show:
        E.hud(im, hearts, 4, ("MUG",), 23, None)


# =====================================================================================================================
# 6. the sequence timeline (ticks at 60 Hz) -- the doc's timing table is printed from this
# =====================================================================================================================
def beat_at(t):
    for b in BEATS:
        if b[0] <= t < b[1]:
            return b
    return BEATS[-1]


def collapse_order():
    """outside in: cells farthest from the body go first (the wall row and the HUD with them)"""
    bx, by = BODY
    cs = [q for q in CELLS if q != BODY]
    rr = random.Random(5)
    cs.sort(key=lambda q: -(abs(q[0] - bx) * 1.1 + abs(q[1] - by) + rr.random() * 0.8))
    return cs


ORDER = collapse_order()
C0 = 60  # the collapse starts on the beat after STILL; a cell = crack CRACK ticks + fall FALL ticks
CRACK, FALL = 3, 8
MAX_FLY = 8  # cells in flight at once (2 OBJ of 8x16 each; 16 of the 40 OAM slots)


def death_pose(t, style=None):
    style = style or HP["dodge"]
    seq = DEATH_ANIM[style]
    if t < 10:
        return dict(flash="white" if t < 5 else None, sq=2, weapon="held"), 0
    tt = t - 10
    for (dur, pose, dx) in seq:
        if tt < dur:
            return pose, dx
        tt -= dur
    return seq[-1][1], seq[-1][2]


def fall_boxes(q, u):
    x, y = cell_xy(*q)
    dy = int(u * u * 40)
    return [(x - int(u * 3), y + dy, 8, 16), (x + 8 + int(u * 3), y + dy + int(u * 6), 8, 16)]


def schedule():
    """launch cells in ORDER while the OBJ budget allows: <= MAX_FLY in flight and <= 10 sprites on any line over
    the cell's whole fall, counting the heir (2 layers x 2 columns) and the CRT (the cartridge keeps the same
    free-slot and per-band counts)"""
    bx, by = cell_xy(*BODY)
    pose, dx = death_pose(C0)
    bb = heir_frame(pose).getbbox()
    static = [(bx - 8 + dx + bb[0], by - 18 + bb[1], bb[2] - bb[0], bb[3] - bb[1])] * 2  # the heir: 2 OBJ layers
    start, i, t = {}, 0, C0
    while i < len(ORDER):
        q = ORDER[i]
        ok = sum(1 for s_ in start.values() if s_ + CRACK + FALL > t) < MAX_FLY
        if ok:
            for dt in range(CRACK, CRACK + FALL):
                tt = t + dt
                boxes = list(static)
                for r, s_ in list(start.items()) + [(q, t)]:
                    if s_ + CRACK <= tt < s_ + CRACK + FALL:
                        boxes += fall_boxes(r, (tt - s_ - CRACK) / FALL)
                if obj_lines(boxes) > 10:
                    ok = False
                    break
        if ok:
            start[q] = t
            i += 1
            continue
        t += 1
        assert t < 400, "collapse cannot be scheduled"
    return start


START = schedule()
C1 = max(START.values()) + CRACK + FALL


BEATS = [  # (start, end, name, what)
    (0, 10, "HITSTOP", "freeze, white flash, 2 px shake"),
    (10, 50, "FALL", "death anim by dodge; palette drains 4 steps; music cuts on the beat to a drone"),
    (50, 60, "STILL", "nothing moves; the drone settles"),
    (60, C1, "LET GO", "room collapses outside-in; void turns pastel; glitch tears"),
    (C1, 160, "LIGHT", "dithered light falls on the last tile"),
    (160, 172, "CARD IN", "the memorial badge slides up (window)"),
    (172, 330, "CARD", "lines type in; cause; epitaph; stats count up"),
    (330, 342, "SWAP", "card slides out left, the carry panel in"),
    (342, 462, "CARRIES", "kept vs lost rows; trait teaser"),
    (462, 474, "OUT", "panel out; back to the lit tile"),
    (474, 504, "DROP", "the body tile cracks and drops into the void"),
    (504, 566, "THE LINE", "tree slides in; CORA added, faded; 3 kids slide up"),
]
END = BEATS[-1][1]


def cell_state(q, t):
    start = START[q]
    if t < start:
        return "solid", 0
    if t < start + CRACK:
        return "crack", 0
    if t < start + CRACK + FALL:
        return "fall", (t - start - CRACK) / FALL
    return "gone", 1


def ease(u):
    u = max(0.0, min(1.0, u))
    return 1 - (1 - u) ** 3


# ---------------------------------------------------------------------------------------- sprites helpers
class Objs:
    def __init__(self):
        self.items = []  # (img RGBA, x, y, layers): layers = OBJ layers drawn at that spot (the heir has 2)

    def add(self, img, x, y, layers=1):
        self.items.append((img, int(x), int(y), layers))

    def boxes(self):
        out = []
        for (img, x, y, n) in self.items:
            bb = img.getbbox()
            if bb:
                out += [(x + bb[0], y + bb[1], bb[2] - bb[0], bb[3] - bb[1])] * n
        return out

    def draw(self, im):
        for (img, x, y, n) in self.items:
            im.paste(img, (x, y), img)


def half_sprite(c, r, k, u):
    """one falling 8x16 half of a cell (k=0 left, 1 right): 3 greys + transparent (one OBJ palette); as it drops
    it is absorbed by the pastel void: a palette step toward the void, then a Bayer dissolve"""
    t = E.slab("peach", c * 9 + r * 5).crop((k * 8, 0, k * 8 + 8, 16))
    g = [greyof(x) for x in FLOOR[:3]]
    if u > 0.4:
        g = [lerp(x, PASTEL[0], 0.5) for x in g]
    out = Image.new("RGBA", (8, 16), (0, 0, 0, 0))
    src = t.load()
    gone = max(0.0, (u - 0.55) / 0.45)
    for y in range(16):
        for x in range(8):
            cc = src[x, y]
            if E.BAYER[y & 3][(x + k * 2) & 3] < gone * 16:
                continue
            j = min(range(4), key=lambda i: sum((a - b) ** 2 for a, b in zip(cc, FLOOR[i])))
            out.putpixel((x, y), (*g[min(j, 2)], 255))
    return out


def mote(col=(255, 250, 240)):
    m = Image.new("RGBA", (3, 3), (0, 0, 0, 0))
    m.putpixel((1, 1), (*col, 255))
    m.putpixel((1, 0), (*lerp(col, LIGHT[1], 0.5), 255))
    return m


def crt_sprite(broken=False, drain_k=0):
    o = obj("CRT")
    if broken:
        d = ImageDraw.Draw(o)
        d.line((5, 5, 9, 9), fill=(90, 230, 240, 255))
        d.line((9, 5, 6, 10), fill=(30, 22, 60, 255))
    if drain_k:
        px = o.load()
        for y in range(16):
            for x in range(16):
                p = px[x, y]
                if p[3]:
                    px[x, y] = (*lerp(p[:3], greyof(p[:3]), drain_k / 4), 255)
    return o


# =====================================================================================================================
# 7. screens: the scene, the card, the carry panel, the tree
# =====================================================================================================================
def scene(t, show_heir=True):
    """BG (legalised, palette-driven) + sprites for tick t in the room phases"""
    objs = Objs()
    dk = 0 if t < 14 else min(4, 1 + (t - 14) // 8)  # drain: 4 palette steps, 8 ticks apart
    vk = 0 if t < 70 else min(4, (t - 70) // 12 + 1)  # void palette: indigo grey -> pastel in 4 steps
    present, cracked = [], []
    for q in CELLS:
        if q == BODY:
            present.append(q)
            continue
        s, u = cell_state(q, t)
        if s in ("solid", "crack"):
            present.append(q)
        if s == "crack":
            cracked.append(q)
        if s == "fall":
            x, y = cell_xy(*q)
            for k in range(2):
                dx = k * 8 + (k * 2 - 1) * int(u * 3)
                dy = int(u * u * 40) + k * int(u * 6)
                objs.add(half_sprite(q[0], q[1], k, u), x + dx, y + dy)
    # body tile dropping (beat DROP)
    body_dy = 0
    if t >= 474:
        u = (t - 478) / 26
        if t < 478:
            cracked.append(BODY)
        else:
            present.remove(BODY)
            for k in range(2):
                x, y = cell_xy(*BODY)
                objs.add(half_sprite(BODY[0], BODY[1], k, min(1.0, u * 0.8)), x + k * 8 + (k * 2 - 1) * int(u * 2),
                         y + int(u * u * 90))
            body_dy = int(u * u * 90)
    bg = draw_room(present, cracked, t >= 6)
    hud_on = t < 100
    if hud_on:
        hud_layer(bg, 0)
    # the light: a dithered shaft over the void, falling from the top (BG tiles in its own palette)
    if t >= C1:
        bx, by = cell_xy(*BODY)
        reach = by * ease((t - C1) / 18) if t < C1 + 18 else by
        px = bg.load()
        for y in range(0, int(reach)):
            fr = y / max(1, by)
            half = 5 + fr * 7
            cx = bx + 8
            for x in range(int(cx - half - 2), int(cx + half + 3)):
                if 0 <= x < 160:
                    edge = abs(x - cx) / (half + 2)
                    dens = (0.25 + 0.4 * fr) * (1 - edge ** 3)
                    if E.BAYER[y & 3][x & 3] < dens * 16:
                        px[x, y] = LIGHT[2] if E.BAYER[y & 3][x & 3] < dens * 7 else LIGHT[3]
    vd = [lerp(a, b, vk / 4) for a, b in zip(drain(VOID, dk), PASTEL)]
    if t >= C1:  # after the collapse only 3 BG palettes stay: void, light, the last tile (5 left for the window)
        pals = SCENE_LATE
        pid, idx = legalize(bg, pals)
        disp = [vd, [vd[0], vd[1], LIGHT[2], LIGHT[3]], drain(SCENE_LATE[2], dk)]
    else:
        pals = SCENE
        pid, idx = legalize(bg, pals)
        disp = [drain(p, dk) for p in pals]
        disp[2] = vd
        disp[3] = [disp[3][0], disp[3][1], vd[0], vd[1]]
        disp[6] = [vd[0], vd[1], LIGHT[2], LIGHT[3]]

    out = paint(pid, idx, disp)
    # glitch tears: SCX per row band (STAT handler), BG only
    for (a, b, y0, y1, dx) in [(72, 74, 40, 43, 4), (95, 98, 88, 90, -3), (96, 98, 20, 22, 5),
                               (118, 120, 60, 64, -4), (138, 140, 100, 102, 3)]:
        if a <= t < b:
            E.tear(out, y0, y1, dx)
    # hitstop shake
    shake = (2 if t % 4 < 2 else -2) if t < 10 else 0
    if shake:
        out = out.transform(out.size, Image.AFFINE, (1, 0, -shake, 0, 1, 0), fillcolor=paint(pid, idx, disp).getpixel((0, 0)))
    # sprites: the CRT she pushed (broken, beside her), the heir, motes in the light
    bx, by = cell_xy(*BODY)
    if show_heir:
        pose, dx = death_pose(t)
        lit = 0 if t < C1 + 14 else min(2, (t - C1 - 14) // 6 + 1)
        hk = min(dk, 2)  # the heir drains only half way: the last thing with colour
        f = heir_frame(pose, 0, hk, lit)
        objs.add(f, bx - 8 + dx, by - 18 + body_dy, 2)
    if max(140, C1) <= t < 474:
        rr = random.Random(3)
        for i in range(5):
            mx = bx + 8 + rr.randrange(-8, 9)
            my = (rr.randrange(0, by - 24) + (t - 140) * (0.4 + 0.1 * i)) % max(1, by - 24)
            objs.add(mote(), mx, my)
    if t < 10:
        objs.items = [(im, x - shake, y, n) for (im, x, y, n) in objs.items]
    stats = tile_stats(pid, idx)
    final = out.copy()
    objs.draw(final)
    return final, out, objs, stats


# ---------------------------------------------------------------------------------------- the card
def barcode(im, x, y, w, h, hexs):
    d = ImageDraw.Draw(im)
    bits = "".join(f"{int(c, 16):04b}" for c in hexs)
    xx = x
    for i, b in enumerate(bits):
        if xx >= x + w:
            break
        if b == "1":
            d.line((xx, y, xx, y + h - 1), fill=LILAC)
        xx += 1 + (i % 3 == 0)


def memorial_face(glitch_row=None):
    f, pal = face_of(DEAD, closed=True)
    # a memorial print: the face's own 3 colours pulled toward the card's lilac (one palette, legal)
    tone = [(240, 226, 250), (176, 150, 214), (92, 70, 140)]
    order = sorted(range(1, 4), key=lambda i: -lum(pal[i]))
    mp = {pal[order[k]]: tone[k] for k in range(3)}
    px = f.load()
    for y in range(48):
        for x in range(48):
            p = px[x, y]
            if p[3]:
                px[x, y] = (*mp.get(p[:3], tone[1]), 255)
    if glitch_row is not None:
        seg = f.crop((0, glitch_row, 48, glitch_row + 3))
        f.paste((0, 0, 0, 0), (0, glitch_row, 48, glitch_row + 3))
        f.paste(seg, (4, glitch_row), seg)
    return f, tone


def card(u=1.0, show=None):
    """the epitaph badge, 160x144; show: dict of how much of each part is revealed (for the animation).
    Laid out on the 8x8 grid so every tile needs one of 5 palettes (CARD + the portrait's)."""
    show = show or {}
    im = Image.new("RGB", (160, 144), CARD_BG)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 159, 14), fill=CARD_HI)  # header band (rows 0-1)
    d.rectangle((72, 2, 87, 5), fill=INK)  # the lanyard slot
    d.line((0, 15, 159, 15), fill=CYAN)
    text(im, 2, 4, "MEMORIAL", LILAC)
    text(im, 106, 4, f"GEN {GEN:02d}", CYAN)
    # portrait: eyes shut, memorial tone, a glitch row; a frame on the tile grid (7x7 tiles)
    f, tone = memorial_face(glitch_row=30 if show.get("glitch", True) else None)
    d.rectangle((0, 16, 55, 71), fill=tone[2])
    d.rectangle((2, 18, 53, 69), fill=CARD_HI)
    im.paste(f, (4, 20), f)
    n = show.get("id", 99)
    rows = [(58, 18, HEIR["name"], PINK), (58, 28, f"{HP['dodge']} {HP['traits'][0]}"[:12], CYAN),
            (58, 40, "F3 OFFICE", WHITE)]
    for i, (x, y, s_, c) in enumerate(rows):
        if i < n:
            text(im, x, y, s_, c)
    if n >= 3:
        barcode(im, 58, 56, 98, 10, HEIR["genome"] * 2)
    # the time clock: IN = birth (the real moment the parent died), OUT = now
    k = show.get("clock", 2)
    if k >= 1:
        text(im, 2, 73, "IN", LILAC)
        text(im, 34, 73, "03.11 14:02", WHITE)
    if k >= 2:
        text(im, 2, 82, "OUT", LILAC)
        text(im, 34, 82, "03.15 21:47", WHITE)
    d.line((2, 96, 157, 96), fill=CARD_HI)
    c = show.get("cause", 99)
    for i, ln in enumerate(CAUSE):
        s_ = ln[:max(0, c - sum(len(x) for x in CAUSE[:i]))]
        text(im, 2, 99 + i * 9, s_, WHITE)
    e = show.get("epi", 4)
    if e >= 2:  # fades in: dark lilac (palette step) then lilac
        text(im, 2, 120, f"'{EPI}'" if len(EPI) <= 18 else EPI, CARD_HI if e < 3 else LILAC, INK)
    return im, show.get("stats", 1.0)


STATS = dict(tokens=23, treasure=(2, 5), kills=14, mixes=6)


def stat_icons(s):
    """the stats row is sprites over the card: token, star, mark, flask (OBJ palettes 4..7 of the card phase)"""
    objs = Objs()
    tok = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    m = ["00111100", "01222210", "12233221", "12333321", "12233221", "12222221", "01222210", "00111100"]
    for yy, r in enumerate(m):
        for xx, c in enumerate(r):
            if c != "0":
                tok.putpixel((xx, yy), (*[(140, 90, 30), YEL, WHITE][int(c) - 1], 255))
    star = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    sm = ["00010000", "00121000", "11222110", "01222100", "00222000", "01212100", "11000110", "00000000"]
    for yy, r in enumerate(sm):
        for xx, c in enumerate(r):
            if c != "0":
                star.putpixel((xx, yy), (*[(150, 90, 30), YEL][int(c) - 1], 255))
    kill = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    km = ["01111100", "12222210", "12122120", "12222210", "01212100", "01222100", "00111000", "00000000"]
    for yy, r in enumerate(km):
        for xx, c in enumerate(r):
            if c != "0":
                kill.putpixel((xx, yy), (*[(110, 60, 150), (240, 230, 255)][int(c) - 1], 255))
    flask = small(obj("STEAM"), 2)
    v = lambda n: int(round(n * s))  # noqa: E731
    out = [(tok, 3, 133, f"{v(STATS['tokens']):02d}", YEL), (star, 38, 133, f"{v(STATS['treasure'][0])}/5", YEL),
           (kill, 80, 133, f"{v(STATS['kills']):02d}", YEL), (flask, 116, 133, f"{v(STATS['mixes'])}", YEL)]
    return out


def card_frame(t):
    """card phase: reveal by tick"""
    lt = t - 172
    show = dict(id=min(3, max(0, lt // 4 + 1)), clock=0 if lt < 14 else (1 if lt < 22 else 2),
                cause=max(0, (lt - 26)), epi=0 if lt < 74 else min(4, (lt - 74) // 5 + 1),
                stats=0 if lt < 100 else min(1.0, (lt - 100) / 30), glitch=(lt // 3) % 9 == 0 or lt < 0)
    if lt < 0:
        show = dict(id=3, clock=2, cause=99, epi=4, stats=0, glitch=True)
    return card(show=show)


def card_legal(im=None):
    _, tone = memorial_face()
    return CARD + [[CARD_HI, tone[2], tone[1], tone[0]]]


# ---------------------------------------------------------------------------------------- carries
RECIPES = ["STEAM", "SPARK", "SEED", "FIRE"]


def carry(lt):
    """what the line keeps vs loses. lt = ticks into the panel"""
    im = Image.new("RGB", (160, 144), CARD_BG)
    d = ImageDraw.Draw(im)
    objs = Objs()
    d.rectangle((0, 0, 159, 14), fill=CARD_HI)
    d.line((0, 15, 159, 15), fill=CYAN)
    text(im, 2, 4, "THE LINE KEEPS", LILAC)
    rows = [18, 34, 50, 66, 82]
    k = lt // 18
    # 1 mods: KEEP
    if k >= 0:
        objs.add(small(obj("FIRE"), 2), 4, rows[0] + 2)
        text(im, 16, rows[0] + 2, "EMBER MOD", WHITE)
        if lt > 8:
            text(im, 120, rows[0] + 2, "KEEP", CYAN)
    # 2 recipes: 3 of 4, one forgotten (dissolves)
    if k >= 1:
        for i, r in enumerate(RECIPES):
            ic = small(obj(r), 2)
            if r == "SPARK":
                u = min(1.0, max(0.0, (lt - 26) / 14))
                px = ic.load()
                for y in range(8):
                    for x in range(8):
                        if px[x, y][3] and E.BAYER[y & 3][x & 3] < u * 16:
                            px[x, y] = (0, 0, 0, 0)
            objs.add(ic, 4 + i * 10, rows[1] + 2)
        text(im, 46, rows[1] + 2, "RECIPES", WHITE)
        if lt > 40:
            text(im, 120, rows[1] + 2, "3/4", CYAN)
    # 3 faction trace: halved
    if k >= 2:
        text(im, 4, rows[2] + 2, "WATCH", LILAC)
        full = 6
        now = 6 if lt < 50 else 3
        for i in range(full):
            c = YEL if i < now else INK
            d.rectangle((52 + i * 8, rows[2] + 3, 57 + i * 8, rows[2] + 8), fill=c)
        if lt > 50:
            text(im, 120, rows[2] + 2, "HALF", CYAN)
    # 4 tokens: lost (drain to 0)
    if k >= 3:
        u = min(1.0, max(0.0, (lt - 56) / 16))
        n = int(round(23 * (1 - u)))
        tok = stat_icons(1.0)[0][0]
        objs.add(tok, 4, rows[3] + 2)
        text(im, 16, rows[3] + 2, f"{n:02d} TOKENS", WHITE)
        if lt > 70:
            text(im, 120, rows[3] + 2, "LOST", PINK)
    # 5 pocket + weapon pieces: lost
    if k >= 4:
        objs.add(small(obj("MUG"), 2), 4, rows[4] + 2)
        text(im, 16, rows[4] + 2, "POCKET", WHITE)
        if lt > 80:
            text(im, 120, rows[4] + 2, "LOST", PINK)
    # teaser: the three kids as unlit silhouettes, two with SMALL lit
    if lt > 88:
        d.line((2, 100, 157, 100), fill=CARD_HI)
        text(im, 4, 104, "SMALL RUNS IN IT", YEL)
        for i, g in enumerate(SIBS):
            p = genes.express(g)
            sil = SS.render("B", dict(HEIR, pal=[CARD_HI] * 6), 0, {})
            if "SMALL" in p["traits"] and lt > 96 + i * 4:
                sil = SS.render("B", dict(HEIR, pal=[CARD_HI, CARD_HI, CARD_HI, CARD_HI, CARD_HI, YEL]), 0, {})
            objs.add(sil, 42 + i * 26, 112)
    return im, objs


def carry_pals():
    return list(CARD)


# ---------------------------------------------------------------------------------------- the line (tree slide-in)
def tree(lt):
    """lt ticks into THE LINE: ancestors slide in from the right, CORA drops in faded, the kids slide up"""
    vv = E.void(7, grid=True)
    pid, idx = legalize(vv, [VOID])
    bgv = paint(pid, idx, [PASTEL])
    im = bgv.copy()
    d = ImageDraw.Draw(im)
    objs = Objs()
    sx = 0
    slide = int(160 * (1 - ease(lt / 16)))
    # header strip
    E.panel(im, 0 + sx, 0, 159 + sx, 13, top=CARD_HI, bot=CARD_BG)
    text(im, 4 + sx, 3, "THE LINE", LILAC)
    text(im, 104 + sx, 3, f"GEN {GEN + 1:02d}", YEL)
    # ancestor row: ... grandparent, CORA (faded, eyes shut)
    gf, _ = face_of(GRAND)
    cf, _ = face_of(DEAD, closed=True)
    y0 = 18
    d.line((4 + sx, y0 + 12, 150 + sx, y0 + 12), fill=(150, 116, 210))
    for i in range(3):  # elders: dots
        d.point((8 + i * 4 + sx, y0 + 12), fill=WHITE)
    E.put(im, small(gf, 2), 24 + sx, y0)
    text(im, 50 + sx, y0 + 8, f"{GEN - 1:02d}", LILAC)
    if lt >= 16:
        u = ease((lt - 16) / 10)
        f = small(cf, 2)
        px = f.load()
        for y in range(24):
            for x in range(24):
                p = px[x, y]
                if p[3]:
                    px[x, y] = (*lerp(greyof(p[:3]), PASTEL[0], 0.25), 255)
        cy = int(y0 - 30 + 30 * u)
        objs.add(f, 76, cy)  # dropping in: sprites until it lands, then committed to the BG
        if lt % 6 < 2 and lt < 30:
            E.tear(im, y0 + 4, y0 + 7, 3)
        if lt >= 26:
            text(im, 102, y0 + 8, HEIR["name"], WHITE)
            text(im, 102, y0 + 0, f"{GEN:02d}", LILAC)
    # the kids slide up
    if lt >= 32:
        u = ease((lt - 32) / 20)
        d.line((88, y0 + 26, 88, y0 + 34), fill=(150, 116, 210))
        d.line((26, y0 + 34, 140, y0 + 34), fill=(150, 116, 210))
        for i, g in enumerate(SIBS):
            x = 4 + i * 52
            y = int(144 - (144 - 58) * u)
            k = Image.new("RGB", (50, 83), CARD_BG)
            E.panel(k, 0, 0, 49, 82, top=(70, 44, 130) if i == 1 else (46, 30, 92), bot=CARD_BG,
                    rim=PINK if i == 1 else (110, 80, 170), under=CYAN if i == 1 else (70, 40, 120))
            ff, _ = face_of(g)
            E.put(k, ff, 1, 3)
            p = genes.express(g)
            text(k, 2, 52, SIBN[i], PINK if i == 1 else WHITE)
            text(k, 2, 61, p["dodge"], CYAN)
            text(k, 2, 70, (p["traits"] or ["-"])[0][:6], YEL)
            m = Image.new("L", (50, 83), 255)
            md = ImageDraw.Draw(m)
            for (cx, cy) in ((0, 0), (49, 0), (0, 82), (49, 82)):
                md.point((cx, cy), 0)
            im.paste(k, (x, y), m)
    if slide:
        out = bgv.copy()
        out.paste(im.crop((0, 0, 160 - slide, 144)), (slide, 0))
        im = out
    return im, objs


def tree_pals():
    out = [PASTEL, [CARD_BG, CARD_HI, LILAC, WHITE], [CARD_BG, CARD_HI, PINK, CYAN], [CARD_BG, (46, 30, 92), YEL, WHITE]]
    for g in SIBS + [GRAND]:
        _, pal = face_of(g)
        out.append([(46, 30, 92), pal[1], pal[2], pal[3]])
    return out[:8]


# =====================================================================================================================
# 8. the full sequence
# =====================================================================================================================
def window_y(t):
    if 160 <= t < 172:
        return int(144 * (1 - ease((t - 160) / 12)))
    if 172 <= t < 462:
        return 0
    return 144


def frame(t):
    """the composed 160x144 frame at tick t, plus a legality record"""
    rec = {}
    if t < 504:
        final, bgonly, objs, st = scene(t, show_heir=not (172 <= t < 462))
        rec["scene_tiles"], rec["scene_pals"] = st
        # window (card / carry) over the scene
        if 160 <= t < 474:
            if t < 330:
                w, s = card_frame(t)
                pals = card_legal(w)
                wo = Objs()
                for (ic, x, y, lab, col) in stat_icons(s if t >= 172 else 0):
                    if s > 0:
                        wo.add(ic, x, y)
                        text(w, x + 10, y, lab, col)
            else:
                lt = t - 342
                w, wo = carry(max(0, lt))
                pals = carry_pals()
            pid, idx = legalize(w, pals)
            w = paint(pid, idx, pals)
            rec["win_tiles"], rec["win_pals"] = tile_stats(pid, idx)
            # slide: in from the bottom, swap = out left / in right, out = down
            if t < 172:
                ox, oy = 0, window_y(t)
            elif 330 <= t < 342:
                u = ease((t - 330) / 12)
                ox, oy = 0, 0
                old, _ = card_frame(329)
                pid2, idx2 = legalize(old, card_legal(old))
                old = paint(pid2, idx2, card_legal(old))
                comp = Image.new("RGB", (160, 144))
                comp.paste(old, (int(-160 * u), 0))
                comp.paste(w, (int(160 * (1 - u)), 0))
                w = comp
                wo = Objs()
            elif t >= 462:
                ox, oy = 0, int(144 * ease((t - 462) / 12))
            else:
                ox, oy = 0, 0
            if oy < 144:
                final = final.copy()
                final.paste(w.crop((0, 0, 160, 144 - oy)), (ox, oy))
                if oy == 0:
                    wo.draw(final)
                rec["objs"] = obj_lines(wo.boxes())
        else:
            rec["objs"] = obj_lines(objs.boxes())
        return final, rec
    lt = t - 504
    im, objs = tree(lt)
    pals = tree_pals()
    pid, idx = legalize(im, pals)
    out = paint(pid, idx, pals)
    objs.draw(out)
    rec["win_tiles"], rec["win_pals"] = tile_stats(pid, idx)
    rec["objs"] = obj_lines(objs.boxes())
    return out, rec


def composite(im, label=None, sub=None):
    """1x and 4x side by side on the mock backdrop"""
    W = 4 + 160 + 8 + 640 + 4
    H = 4 + 576 + 4
    c = Image.new("RGB", (W, H), (12, 8, 26))
    c.paste(im, (4, 4))
    c.paste(im.resize((640, 576), Image.NEAREST), (172, 4))
    text(c, 4, 152, "1X", (110, 80, 170))
    text(c, 4, 162, "4X >", (110, 80, 170))
    if label:
        text(c, 4, 180, label[:20], PINK)
    if sub:
        for i, ln in enumerate(sub):
            text(c, 4, 192 + i * 10, ln[:20], LILAC)
    return c


# =====================================================================================================================
# 9. variations sheet
# =====================================================================================================================
def talkbox(im, face_img, lines, rim=CYAN):
    E.panel(im, 0, 96, 159, 143, rim=rim)
    if face_img is not None:
        E.put(im, small(face_img, 2), 4, 102)
    for i, ln in enumerate(lines):
        text(im, 32, 102 + i * 10, ln, WHITE)


def variants():
    tiles = []
    # (a) death by dodge style: 3 strips (ROLL/SLIDE/BLINK) + trait overlays
    a = Image.new("RGB", (160, 144), CARD_BG)
    text(a, 2, 2, "FALL BY DODGE", PINK)
    for j, sty in enumerate(["ROLL", "SLIDE", "BLINK"]):
        y = 14 + j * 40
        text(a, 2, y + 12, sty[:5], CYAN)
        x = 44
        for (dur, pose, dx) in DEATH_ANIM[sty]:
            f = heir_frame(pose, 0, 0)
            E.put(a, f, x - 8 + dx // 2, y)
            x += 23
    text(a, 2, 134, "+TRAIT ACCENTS", LILAC)
    tiles.append((a, "A. FALL BY DODGE", ["ROLL TUMBLES FLAT,", "SLIDE SKIDS, BLINK", "FLICKERS OUT"]))
    # (b) first death: the card's header and a stamp; unskippable
    b, _ = card()
    E.panel(b, 20, 40, 139, 70, rim=YEL, under=PINK)
    text(b, 28, 46, "FIRST OF THE", YEL)
    text(b, 28, 56, "LINE TO STOP", YEL)
    tiles.append((b, "B. FIRST DEATH", ["A STAMP; THE LINE", "SCREEN TEACHES WHAT", "CARRIES; NO SKIP"]))
    # (c) no heirs left: the tile does not drop; the light goes out; no kids slide up
    c = Image.new("RGB", (160, 144), (0, 0, 0))
    fr, _, _, _ = scene(160)
    px = fr.load()
    for y in range(144):
        for x in range(160):
            p = px[x, y]
            px[x, y] = lerp(p, (14, 10, 34), 0.55) if abs(x - 80) > 18 else p
    c.paste(fr, (0, 0))
    E.panel(c, 0, 108, 159, 143, rim=(110, 80, 170), under=(70, 40, 120))
    text(c, 4, 114, "NO ONE CAME AFTER", LILAC)
    text(c, 4, 125, "A:BEGIN AGAIN", WHITE)
    tiles.append((c, "C. NO HEIRS LEFT", ["NO DROP: THE LIGHT", "NARROWS AND GOES", "OUT. NEW FOUNDER"]))
    # (d) boss kill: the boss speaks over the card's cause line
    dd, _ = card(show=dict(cause=0))
    bf, _ = E.face(3, 41)
    talkbox(dd, bf, ["OLD MONITOR:", "YOU LEFT IT ON.", "SO DID SHE."], rim=PINK)
    tiles.append((dd, "D. BOSS KILL", ["THE BOSS'S LINE", "FROM THE MATRIX,", "BEFORE THE CAUSE"]))
    # (e) a faction talker comments (rare, standing-gated)
    ee, _ = card()
    ff, _ = E.face(4, 77)
    talkbox(ee, ff, ["WATCH:", "WE SAW HER PUSH", "IT. WE WROTE IT."])
    tiles.append((ee, "E. FACTION NOTE", ["1 IN 6, WHEN A", "FACTION CARES;", "SLANTED BY STANDING"]))
    # (f) the ghost later: on F3 again, CORA's echo stands where she stopped
    g = E.void(5, grid=True)
    E.floor(g, [(x, y) for x in range(24, 136, 16) for y in range(32, 96, 16)], "peach")
    ghost = heir_frame(dict(eyes="closed"), 0, 4)
    px = ghost.load()
    for y in range(ghost.height):
        for x in range(ghost.width):
            p = px[x, y]
            if p[3]:
                px[x, y] = (*lerp(p[:3], (150, 236, 255), 0.6), 255) if y % 3 else (0, 0, 0, 0)
    E.put(g, ghost, 64, 30)
    E.put(g, heir_frame({}, 4, 0), 64, 58)
    talkbox(g, None, ["", "", ""])
    text(g, 4, 102, "SHE STOPPED HERE.", LILAC)
    text(g, 4, 112, "MIND THE SCREEN.", LILAC)
    text(g, 4, 126, f"-CORA, GEN {GEN:02d}", (150, 116, 210))
    tiles.append((g, "F. HER GHOST, LATER", ["ON F3 IN A LATER", "RUN; HER LINE IS", "BUILT FROM HER DEATH"]))
    W, H = 3 * 336 + 16, 2 * (288 + 40) + 8
    sheet = Image.new("RGB", (W, H), (12, 8, 26))
    for i, (im, lab, sub) in enumerate(tiles):
        x = 8 + (i % 3) * 336
        y = 8 + (i // 3) * 328
        sheet.paste(im.resize((320, 288), Image.NEAREST), (x, y))
        big = Image.new("RGB", (160, 18), (12, 8, 26))
        text(big, 0, 0, lab, PINK)
        sheet.paste(big.resize((320, 36), Image.NEAREST), (x, y + 290))
    return sheet


# =====================================================================================================================
def main():
    if "--grammar" in sys.argv:
        for d in GRAMMAR_EXAMPLES:
            print(" / ".join(cause(d)))
        for (ax, sg), pool in EPITAPH.items():
            assert all(len(x) <= 18 for x in pool), pool
        print("epitaph:", EPI, "| cause:", CAUSE, "| heir:", HEIR["name"], HP["dodge"], HP["traits"], "gen", GEN)
        return
    OUT.mkdir(parents=True, exist_ok=True)
    recs = {}
    # stills
    col, rc = frame(100)
    composite(col, "LET GO  1.67S", ["ROOM COLLAPSES", "OUTSIDE-IN; VOID", "TURNS PASTEL"]).save(OUT / "death_collapse.png")
    recs["collapse"] = rc
    cd, rd = frame(310)
    composite(cd, "CARD  5.17S", ["MEMORIAL BADGE:", "WINDOW LAYER,", "STATS AS SPRITES"]).save(OUT / "death_card.png")
    recs["card"] = rd
    variants().save(OUT / "death_variants.png")
    # the GIF: every 3 ticks (50 ms), merging identical frames
    frames, durs, last = [], [], None
    worst = {"scene_pals": 0, "win_pals": 0, "objs": 0, "scene_tiles": 0, "win_tiles": 0}
    for t in range(0, END + 40, 3):
        im, rec = frame(min(t, END - 1))
        for k in worst:
            if k in rec:
                worst[k] = max(worst[k], rec[k])
        b = beat_at(min(t, END - 1))
        c = composite(im, f"{b[2]} {t / HZ:4.2f}S", wrap(b[3]))
        if last is not None and c.tobytes() == last:
            durs[-1] += 50
            continue
        frames.append(c.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE))
        durs.append(50)
        last = c.tobytes()
    durs[-1] += 1500
    frames[0].save(OUT / "death_sequence.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0,
                   optimize=True)
    print("stills:", recs)
    print("worst over the sequence:", worst, "frames:", len(frames))
    for b in BEATS:
        print(f"{b[0] / HZ:5.2f}-{b[1] / HZ:5.2f}s {b[2]:9s} {b[3]}")


if __name__ == "__main__":
    main()
