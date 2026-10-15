"""Ancestors' ghosts (ECHO): the dead heir's own chibi (same hair parts), recoloured into a ghost ramp, with the
legs swapped for a dithered tail. Always hovering.

Interpretations:
    walk     drift: hover bob, the tail sways (leg phase moves the tail)
    run      streak: lean, afterimages, speed lines (the ECHO trace along the last heir's path)
    windup/attack/recover   a wail: rises, mouth O, reaches forward (event "hit" = a chill), sinks back
    hurt     a white scatter; stagger = glitch tear rows; stun = stars; frozen = ice ramp; status = tinted
    death    lays to rest: rises and fades into motes;  spawn: motes coalesce into the ghost
Extra: talk (an ancestor's two-line talk; mouth flap with a bob).
"""
from __future__ import annotations

import core as C
from core import F, M, S
import talkers as T

TYPE = "ghost"
DIRS = 4
REQUIRED = list(C.MIN_STATES) + ["talk"]
W = H = T.W
GROUND = T.GROUND
RAMPS = {"LILAC": [(70, 44, 122), (150, 116, 210), (232, 218, 255)], "CYAN": [(28, 82, 118), (86, 186, 220), (218, 250, 255)],
         "MINT": [(32, 96, 84), (110, 200, 168), (226, 255, 240)], "ROSE": [(110, 44, 92), (220, 128, 186), (255, 226, 244)]}


def variants(n, seed=0):
    base = T.variants(n, seed + 99)
    out = []
    for i, b in enumerate(base):
        v = dict(b, type=TYPE, id=f"GHOST-{i}-{seed}", role=None, ramp=list(RAMPS)[(i + seed) % 4],
                 generation=1 + (i * 7 + seed) % 30)
        out.append(v)
    return out


_ST = {}


def states(v):
    if v["id"] in _ST:
        return _ST[v["id"]]
    st = C.creature_states("glide", v.get("quirk", "HOVER BOB"), "drop", 24)
    st["walk"] = S([F(8, leg=1), F(8, dy=-1), F(8, leg=1, dy=-2, tilt=1), F(8, dy=-1)], True, [(0, "sfx:hum")])
    st["windup"] = S([F(6, dy=-2, pal="tell", mouth="O", fx=("bang",)), F(6, dy=-3, sy=110, mouth="O", fx=("bang",)),
                      F(12, dy=-4, sy=112, pal="tell", mouth="O", fx=("bang",))], False, [(0, "tell"), (0, "sfx:wail")])
    st["attack"] = S([F(3, fwd=3, mouth="O", sy=108), F(4, fwd=6, mouth="O", trail=1, fx=("arc",)), F(6, fwd=4, mouth="O")],
                     False, [(1, "hit"), (1, "status:chill")])
    st["stagger"] = S([F(4, tear=2, fwd=-2, pal="flash"), F(4, tear=-2, fwd=-1), F(5, tear=1, tilt=1), F(6)], False,
                      [(0, "stagger"), (0, "sfx:glitch")])
    st["death"] = S([F(4, pal="flash"), F(6, dissolve=0.3, dy=-2), F(6, dissolve=0.6, dy=-4, fx=("motes0",)),
                     F(8, dissolve=0.85, dy=-7, fx=("motes1",)), F(8, hide=1, fx=("motes2",))], False,
                    [(0, "sfx:rest"), (2, "spawn_fx:motes"), (4, "despawn"), (4, "drop:relic")])
    st["spawn"] = S([F(6, hide=1, fx=("motes0",)), F(4, dissolve=0.8, dy=-3, fx=("motes1",)),
                     F(4, dissolve=0.5, dy=-2, fx=("motes2",)), F(4, dissolve=0.2, dy=-1), F(6)], False,
                    [(0, "spawn_fx:motes"), (0, "sfx:echo"), (4, "commit")])
    st["talk"] = S([F(6, mouth="open", dy=-1), F(5, mouth="half", dy=-2), F(6, mouth="open", dy=-1), F(8)], True,
                   [(0, "talk_blip"), (2, "talk_blip")])
    _ST[v["id"]] = st
    return st


def ghost_grid(v, view, pose):
    g = T.apply_face(T.build_grid(v, view), v, view, pose)
    n = len(g)
    sway = 1 if pose.get("leg") else 0
    for j in range(3):  # the legs become a dithered tail that tapers
        y = n - 3 + j
        row = ["."] * len(g[y])
        lo, hi = 7 + j * 2 + sway * j, 12 - j + sway * j
        for x in range(lo, hi + 1):
            if j == 0 or (j == 1 and (x + y) & 1 == 0) or (j == 2 and x == lo + 1):
                row[x] = "3"
        g[y] = row
    return g


def render(v, state, frame_index, direction):
    pose = states(v)[state]["frames"][frame_index]["pose"]
    t = pose.get("turn", 0)
    view = (direction + 1) % 4 if t == 2 else direction
    r = RAMPS[v["ramp"]]
    cols = {"1": r[0], "o": r[0], "k": C.INK, "m": r[0], "2": r[2], "3": r[1], "a": r[1]}
    img = T.grid_image(ghost_grid(v, view, pose), cols)
    img = C.shift(img, 0, -2)  # always hovering
    return C.post(img, pose, direction, GROUND - 2, tint=M.ELEMENT[v["status_tag"]][1])
