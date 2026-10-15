"""Bosses: a boss template (family + body plan + 3 environmental beats) composed from a MOBGEN XL core and two
generated S satellites ("hands"). Every phase has the full state set (pN.<state>), plus phase-change animations.

Phases (redesign section 7.5):
    p1  dodge-heavy: the hands rise and SLAM (24-frame tell, shake, a tile tears)
    p2  parry-heavy: the hands pull in and fire ORBS/BEAM (24-frame tell, reflectable orb event); cracks, angrier ramp
    p3  arena half gone: one hand is lost, the core is exposed (a pulsing weak band), SWEEP attack with a punish
        window event after it; faster walk/run
Phase changes: phase_1to2 (roar, flash, shake, cracks appear, arena beat), phase_2to3 (a hand breaks off and falls
through the floor, the core opens, arena beat). spawn = the arena assembles and the boss drops in; death = the
final collapse from phase 3.
Views: 4 (the core uses the mob view ops; in side views the far hand draws behind the core).
"""
from __future__ import annotations

from PIL import Image

import core as C
from core import F, M, S
import mobs

TYPE = "boss"
DIRS = 4
PHASES = 3
PER_PHASE = [s for s in C.MIN_STATES if s not in ("spawn", "death")]
REQUIRED = [f"p{p}.{s}" for p in range(1, PHASES + 1) for s in PER_PHASE] + ["spawn", "death", "phase_1to2", "phase_2to3"]
BW, BH = 112, 96
GROUND = BH - 12
TEMPLATES = [  # name, family, body, environmental beat per phase change
    ("OLD MONITOR", "SERVER", 0, ["tiles_pull", "crt_drop", "lights_off"]),
    ("THE TENANT", "OFFICE", 1, ["lights_off", "drywall_shift", "ring_collapse"]),
    ("LIFEGUARD", "ATRIUM", 1, ["flood", "freeze", "ring_collapse"]),
    ("THE GARDENER", "GARDEN", 3, ["overgrowth", "ring_collapse", "floor_drop"]),
]
ATTACK = {1: "SLAM", 2: "ORBS", 3: "SWEEP"}
HANDS = {  # (outward px, vertical px) per frame: the satellites' choreography per phase attack
    (1, "windup"): [(0, -4), (0, -8), (0, -12)], (1, "attack"): [(0, 2), (-2, 6), (0, 4)],
    (2, "windup"): [(-4, 0), (-6, -2), (-8, -2)], (2, "attack"): [(4, 0), (8, 0), (4, 0)],
    (3, "windup"): [(2, -6), (4, -6), (6, -8)], (3, "attack"): [(-10, 0), (-14, 2), (-6, 0)],
}


def variants(n, seed=0):
    out = []
    for i in range(n):
        name, fam, body, beats = TEMPLATES[(i + seed) % len(TEMPLATES)]
        s = M.mix_seed(0xB055, seed, i)
        core = M.gen(s, fam=fam, size=3, body=body, defence="NONE")
        hand = M.gen(s ^ 0x77, fam=fam, size=0, body=(body + 1) % 4, mobility="FLYER", tag=core["tag"], defence="NONE")
        hand["ramp"], hand["hue"] = core["ramp"], core["hue"]
        out.append({"type": TYPE, "id": f"BOSS-{name.replace(' ', '_')}-{i}-{seed}", "name": name, "fam": fam,
                    "core": core, "hand": hand, "beats": beats, "quirk": core["quirk"],
                    "status_tag": core["tag"] if core["tag"] != "NONE" else "SHOCK"})
    return out


def _hands(p, state, fi, pose):
    t = HANDS.get((p, state))
    if t:
        return t[min(fi, len(t) - 1)]
    if state in ("idle", "idle_fidget", "walk", "run", "status"):
        return (0, -(fi % 2) * 2)
    if state == "hurt":
        return (3, 0)
    if state == "stagger":
        return (4, -2 + fi)
    if state == "stun":
        return (-2, 3 + (fi % 2))
    if state == "recover":
        return (0, 2 - fi)
    if state == "turn":
        return (-fi * 2, 0)
    return (0, 0)


_ST = {}


def states(v):
    if v["id"] in _ST:
        return _ST[v["id"]]
    st = {}
    for p in range(1, PHASES + 1):
        base = C.creature_states("heavy", v["quirk"], "drop", 24, speed=[1.0, 1.15, 1.35][p - 1])
        for s in PER_PHASE:
            d = base[s]
            frames = []
            for fi, fr in enumerate(d["frames"]):
                pose = dict(fr["pose"], phase=p, hand=_hands(p, s, fi, fr["pose"]))
                if s == "attack":
                    pose["fx"] = ("arc",) if p != 2 else ("speed",)
                frames.append({"dur": fr["dur"], "pose": pose})
            ev = list(d["events"])
            if s == "attack":
                ev += {1: [(1, "shake:3"), (1, "arena:tile_tear")], 2: [(0, "spawn_fx:orb"), (1, "reflectable")],
                       3: [(2, "window:20")]}[p]
            st[f"p{p}.{s}"] = S(frames, d["loop"], ev)
        if p == 1:
            sp = base["spawn"]
            st["spawn"] = S([dict(f, pose=dict(f["pose"], phase=1, hand=(0, -6 if i < 3 else 0))) for i, f in enumerate(sp["frames"])],
                            False, sp["events"] + [(0, "arena:assemble"), (3, "shake:3"), (5, "name_card")])
    b = v["beats"]
    st["phase_1to2"] = S([F(6, phase=1, pal="flash", dx=2, hand=(2, 0)), F(6, phase=1, dx=-2, tilt=2, hand=(4, -4)),
                          F(8, phase=1, pal="flash", mouth="O", sy=112, hand=(6, -10), fx=("bang",)),
                          F(8, phase=2, mouth="O", hand=(4, -6), fx=("puff0",)), F(6, phase=2, squash=2, hand=(0, 0)),
                          F(10, phase=2, hand=(0, -2))], False,
                         [(0, "sfx:roar"), (2, "shake:3"), (3, "phase:2"), (3, f"arena:{b[0]}"), (5, "commit")])
    st["phase_2to3"] = S([F(6, phase=2, pal="flash", dx=2, hand=(2, 0)), F(6, phase=2, dx=-2, tilt=-2, hand=(4, -2)),
                          F(8, phase=2, pal="hurt", mouth="O", sy=110, hand=(6, -8), hand_drop=1, fx=("spark",)),
                          F(8, phase=3, mouth="O", hand=(2, -4), hand_drop=2, fx=("puff1",)),
                          F(6, phase=3, squash=2, hand=(0, 0), hand_drop=3), F(10, phase=3, hand=(0, -2))], False,
                         [(0, "sfx:roar"), (2, "spawn_fx:debris"), (3, "phase:3"), (3, f"arena:{b[1]}"),
                          (4, f"arena:{b[2]}"), (5, "commit")])
    st["death"] = S([F(6, phase=3, pal="flash", dx=2), F(6, phase=3, pal="hurt", dx=-2, tilt=2, fx=("spark",)),
                     F(8, phase=3, pal="death", dissolve=0.3, squash=2, hand=(0, 6), fx=("puff0",)),
                     F(8, phase=3, pal="death", dissolve=0.6, squash=4, hand=(0, 10), fx=("puff1",)),
                     F(10, phase=3, pal="death", dissolve=0.85, dy=3, hand=(0, 14), fx=("puff2",)),
                     F(12, phase=3, hide=1, fx=("puff2",))], False,
                    [(0, "sfx:boss_death"), (0, "shake:4"), (2, "arena:settle"), (5, "despawn"), (5, "drop:reward")])
    _ST[v["id"]] = st
    return st


def _damage(img, v, phase, fi):
    """phase 2: cracks (dark seams) and an angrier ramp; phase 3: plus a pulsing exposed band (the weak point)"""
    if phase < 2:
        return img
    out = img.copy()
    px = out.load()
    x0, y0, x1, y1 = C.bbox(img)
    r = M.Rng(v["core"]["pseed"] ^ 0xC4AC)
    if not C.COUNT_MODE:
        for y in range(y0, y1):
            for x in range(x0, x1):
                c = px[x, y]
                if c[3] and C.lum(c[:3]) > 70:
                    px[x, y] = (*C.mix(c[:3], (255, 90, 140), 3), 255)
    for _ in range(3):
        x, y = x0 + 3 + r.pick(max(1, x1 - x0 - 6)), y0 + 2 + r.pick(max(1, (y1 - y0) // 2))
        for _ in range(6 + r.pick(4)):
            if 0 <= x < out.width and 0 <= y < out.height and px[x, y][3]:
                px[x, y] = (*C.INK, 255)
            y += 1
            x += r.pick(3) - 1
    if phase >= 3:
        yb = (y0 + y1) // 2
        for y in (yb, yb + 1):
            for x in range(max(1, x0), min(out.width - 1, x1)):
                if px[x, y][3] and px[x - 1, y][3] and px[x + 1, y][3]:
                    on = (x + fi) % 2 == 0
                    px[x, y] = (*((C.YEL if on else C.WHITE) if not C.COUNT_MODE else C.WHITE), 255)
    return out


def render(v, state, frame_index, direction):
    pose = states(v)[state]["frames"][frame_index]["pose"]
    ph = pose.get("phase", 1)
    tint = M.ELEMENT[v["status_tag"]][1]
    core_pose = {k: val for k, val in pose.items() if k not in ("fx", "hand", "hand_drop", "phase")}
    core = mobs.render_single(v["core"], core_pose, direction, frame_index % 2, tint, fx=False)
    core = _damage(core, v, ph, frame_index)
    hand_pose = {k: val for k, val in core_pose.items() if k not in ("squash", "sy", "sx", "tilt", "mouth", "trail", "clip")}
    out = Image.new("RGBA", (BW, BH), (0, 0, 0, 0))
    ox, oy = (BW - 64) // 2, GROUND - mobs.GROUND
    hx, hy = pose.get("hand", (0, 0))
    view = (direction + 1) % 4 if pose.get("turn") == 2 else direction
    side_view = view in (1, 3)
    drop = pose.get("hand_drop", 0)
    layers = []
    for s in (-1, 1):
        lost = ph >= 3 and s == 1 and not drop
        if lost:
            continue
        hp = dict(hand_pose)
        if s == 1 and drop:
            hp["dissolve"] = min(0.9, 0.3 * drop)
            hp["pal"] = "hurt" if drop == 1 else "death"
        him = mobs.render_single(v["hand"], hp, direction, (frame_index + (s > 0)) % 2, tint, fx=False)
        spread = 16 if side_view else 30
        fwdx = (6 if view == 1 else -6) if side_view else 0
        x = ox + s * (spread + hx) + fwdx
        y = oy - 18 + hy + (6 * drop * drop if (s == 1 and drop) else 0)
        far = side_view and ((s == -1) == (view == 1))
        layers.append((far, him, x, y))
    for far, him, x, y in layers:
        if far:
            dim = C.pal_img(him, "dim")
            out.paste(dim, (x, y), dim)
    out.paste(core, (ox, oy), core)
    for far, him, x, y in layers:
        if not far:
            out.paste(him, (x, y), him)
    return C.fx(out, pose.get("fx", ()), direction, GROUND)
