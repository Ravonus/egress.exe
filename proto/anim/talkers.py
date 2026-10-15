"""Talkers and traders: 16x18 chibi people from parts, with the full state set plus talk and reactions.

Base silhouettes are the player templates already in tools/mock/anim.py (S, E, N; W = E mirrored), so a talker is
the same body plan as the heirs. Each variant composes: hair style (part edits: short, bald, bun, long, spikes),
seeded skin and hair ramps (a diverse set), faction cloth, and a role part (TALKER scarf, TRADER bag). Faces in talk
boxes stay the shared face generator; this is the in-world sprite.

Interpretations (talkers do not fight):
    windup/attack/recover  a "shoo": hands up, a shove (event "push", no damage), settle
    death                  the talker is driven off or the room collapses under them: flash, sink, puff out
    stun / frozen / status reactions to stray room effects (a reaction cloud, a freeze, rain)
    walk / run             shuffle / hurry (talkers wander the landing; traders run when the floor collapses)
Extras: talk (mouth flap + head bob, a blip per syllable), react_happy (hop, smile, heart), react_annoyed
(stamp, brows, scribble, sweat).
"""
from __future__ import annotations

import importlib.util

from PIL import Image

import core as C
from core import F, M, S

TYPE = "talker"
DIRS = 4
EXTRA = ["talk", "react_happy", "react_annoyed"]
REQUIRED = list(C.MIN_STATES) + EXTRA
W = H = 32
OX, OY = 6, 12  # grid origin in the canvas (2 spare rows above for buns/spikes)
GW, PX = 20, 2  # grid width; the 16-wide template sits at x=2, so a mirrored grid keeps its centre
GROUND = OY + 17

_spec = importlib.util.spec_from_file_location("mock_anim", C.ROOT / "tools/mock/anim.py")
_mock_anim = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mock_anim)
TEMPLATES = {k: [list(r) for r in _mock_anim.T[k].strip("\n").split("\n")] for k in ("S", "E", "N")}

SKIN = [(252, 220, 196), (240, 190, 156), (214, 156, 116), (178, 118, 80), (138, 88, 58), (100, 62, 44)]
HAIR = [(34, 22, 40), (72, 42, 30), (124, 70, 42), (210, 170, 96), (54, 40, 96), (150, 60, 120), (36, 100, 108),
        (200, 200, 214)]
HAIRSTYLES = ["SHORT", "BALD", "BUN", "LONG", "SPIKES"]
FACTIONS = {"COMPILE": ((64, 120, 232), (160, 210, 255)), "CHARGE": ((236, 140, 48), (255, 224, 120)),
            "SKY": ((96, 180, 236), (236, 248, 255)), "WATCH": ((176, 80, 176), (255, 170, 230)),
            "ROOT": ((70, 160, 96), (190, 240, 160)), "LOCK": ((120, 108, 150), (226, 214, 246))}
ROLES = ["TALKER", "TRADER"]
EYES = {"S": [(4, 6), (11, 6)], "E": [(10, 6)], "N": []}
MOUTH = {"S": [(7, 8), (8, 8)], "E": [(11, 8), (12, 8)], "N": []}


def variants(n, seed=0):
    out = []
    for i in range(n):
        r = M.Rng(M.mix_seed(0x7A1C, seed, i))
        fac = list(FACTIONS)[r.pick(6)]
        sk = r.pick(len(SKIN))
        hr = r.pick(len(HAIR))
        while abs(C.lum(HAIR[hr]) - C.lum(SKIN[sk])) < 56:  # hair must read against the skin
            hr = (hr + 1) % len(HAIR)
        out.append({"type": TYPE, "id": f"TALKER-{i}-{seed}", "role": ROLES[i % 2], "faction": fac,
                    "skin": sk, "hair": hr, "style": HAIRSTYLES[(i + r.pick(5)) % 5],
                    "quirk": ["BREATHE", "TWITCH", "EYE ROLL", "BREATHE", "HOVER BOB"][r.pick(5)],
                    "status_tag": ["WET", "HOT", "SHOCK", "COLD"][r.pick(4)]})
    return out


def talker_extras(st):
    st["talk"] = S([F(6, mouth="open", dy=-1), F(5, mouth="half"), F(6, mouth="open", dy=-1, look=1), F(8)], True,
                   [(0, "talk_blip"), (2, "talk_blip")])
    st["react_happy"] = S([F(4, squash=2), F(6, dy=-4, eyes="happy", mouth="smile", fx=("heart0",)),
                           F(6, dy=-2, eyes="happy", mouth="smile", fx=("heart1",)), F(10, eyes="happy", mouth="smile")],
                          False, [(1, "sfx:happy")])
    st["react_annoyed"] = S([F(4, eyes="angry", squash=1, fx=("anger",)), F(4, dx=1, eyes="angry", fx=("anger",)),
                             F(4, dx=-1, eyes="angry"), F(12, eyes="angry", squash=1, fx=("sweat0",))], False,
                            [(0, "sfx:huff"), (0, "shake:1")])
    return st


_ST = {}


def states(v):
    if v["id"] not in _ST:
        st = C.creature_states("shuffle", v.get("quirk", "BREATHE"), "drop", 12)
        st["attack"]["events"] = [(0, "sfx:shoo"), (1, "push")]
        st["windup"]["frames"] = [F(6, squash=1, eyes="angry"), F(6, dy=-1, eyes="angry", fx=("anger",)),
                                  F(6, dy=-1, fwd=-1, eyes="angry", fx=("anger",))]
        _ST[v["id"]] = talker_extras(st)
    return _ST[v["id"]]


# =====================================================================================================================
# composing the chibi from parts
# =====================================================================================================================
def build_grid(v, view):
    """the template for a view (0 S, 1 E, 2 N, 3 W) with the variant's hair and role parts, as a code grid"""
    key = "SENE"[view] if view != 2 else "N"
    g = [["."] * GW for _ in range(2)] + [["."] * PX + r[:] + ["."] * (GW - 16 - PX) for r in TEMPLATES[key]]
    off = 2
    st = v["style"]
    put = lambda x, y, c: g[y].__setitem__(x + PX, c)  # noqa: E731
    get = lambda x, y: g[y][x + PX]  # noqa: E731
    if st == "BALD":
        src = [r[:] for r in g]
        for y in range(off, off + 10):
            for x in range(GW):
                if src[y][x] == "1":
                    edge = any(not (0 <= x + dx < GW) or src[y + dy][x + dx] == "." for dx, dy in ((1, 0), (-1, 0), (0, -1), (0, 1)))
                    g[y][x] = "o" if edge else "2"
    elif st == "BUN":
        for (x, y) in ((7, 0), (8, 0), (6, 1), (7, 1), (8, 1), (9, 1)):
            put(x + (1 if key == "E" else 0), y, "1")
    elif st == "SPIKES":
        for x in (4, 7, 10):
            put(x, 1, "1")
            put(x + 1, 1, "1")
    elif st == "LONG":
        for y in range(off + 9, off + 12):
            for x in ((3, 12) if key != "E" else (3, 4)):
                if get(x, y) in ".23":
                    put(x, y, "1")
    for (x, y) in EYES.get(key, []):
        put(x, y + off, "k")
    if v.get("role") == "TALKER":  # scarf
        y = off + 10
        for x in range(4, 12):
            if get(x, y) == "3":
                put(x, y, "a")
    elif v.get("role") == "TRADER":  # a satchel at the hip, strap across
        bx = 13 if key != "E" else 0
        for i in range(4):
            for j in range(4):
                x, y = bx + i, off + 12 + j
                edge = i in (0, 3) or j in (0, 3)
                if edge and get(x, y) == ".":
                    put(x, y, "1")
                elif not edge:
                    put(x, y, "a")
        for k in range(3):
            x = (10 - k * 2) if key != "E" else (5 + k * 2)
            if get(x, off + 10 + k) == "3":
                put(x, off + 10 + k, "a")
    if view == 3:
        g = [r[::-1] for r in g]
    return g


def colours(v):
    skin = SKIN[v["skin"]]
    cloth, acc = FACTIONS[v["faction"]]
    return {"1": HAIR[v["hair"]], "2": skin, "3": cloth, "a": acc, "k": C.INK, "o": C.mix(skin, C.INK, 7),
            "m": C.mix(skin, C.INK, 9)}


def apply_face(g, v, view, pose):
    """part swaps for eyes and mouth"""
    key = "SENE"[view] if view != 2 else "N"
    off = 2
    flip = view == 3
    fx_ = lambda x: (GW - 1 - (x + PX)) if flip else x + PX  # noqa: E731
    eyes = pose.get("eyes", "open")
    look = pose.get("look", 0) * (-1 if flip else 1)
    for (x, y) in EYES.get(key, []):
        X, Y = fx_(x), y + off
        g[Y][X] = "2"
        if eyes in ("open", "angry"):
            g[Y][X + (1 if look > 0 else (-1 if look < 0 else 0))] = "k"
        elif eyes == "roll":
            g[Y - 1][X] = "k"
        elif eyes in ("closed", "happy"):
            g[Y][X] = "m"
        if eyes == "angry":
            g[Y - 1][X + (1 if X < GW // 2 else -1)] = "k"
    mouth = pose.get("mouth")
    pts = MOUTH.get(key, [])
    if mouth and pts:
        ps = [(fx_(x), y + off) for (x, y) in pts]
        if mouth == "half":
            ps = ps[:1]
        if mouth == "smile":
            ps = [(ps[0][0] - 1, ps[0][1] - 1)] + ps + [(ps[-1][0] + 1, ps[-1][1] - 1)] if key == "S" else ps
        for (x, y) in ps:
            g[y][x] = "k"
        if mouth in ("O", "open"):
            for (x, y) in ps:
                g[y + 1][x] = "k" if g[y + 1][x] == "2" else g[y + 1][x]
    if pose.get("leg"):  # legs: the last 3 rows step
        n = len(g)
        legs = [r[:] for r in g[n - 3:]]
        for j in range(3):
            row = ["."] + legs[j][:-1] if j % 2 == 0 else legs[j][1:] + ["."]
            g[n - 3 + j] = row
    if pose.get("twitch") and key != "N":
        g[off][fx_(8)] = "1"
        g[off - 1][fx_(9)] = "1"
    return g


def grid_image(g, cols):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = im.load()
    for y, r in enumerate(g):
        for x, ch in enumerate(r):
            if ch in cols:
                X, Y = OX + x, OY - 2 + y
                if 0 <= X < W and 0 <= Y < H:
                    px[X, Y] = (*cols[ch], 255)
    return im


def chibi(v, pose, direction, cols):
    t = pose.get("turn", 0)
    view = (direction + 1) % 4 if t == 2 else direction
    g = apply_face(build_grid(v, view), v, view, pose)
    return grid_image(g, cols)


def render(v, state, frame_index, direction):
    pose = states(v)[state]["frames"][frame_index]["pose"]
    img = chibi(v, pose, direction, colours(v))
    return C.post(img, pose, direction, GROUND, tint=M.ELEMENT[v["status_tag"]][1])
