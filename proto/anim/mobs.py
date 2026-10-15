"""Mobs: every MOBGEN family x size x mobility gets the full state set from its parts.

Looks come from tools/mock/mobgen.py (unchanged). This module adds the state machine, the four views and the
mobility interpretations:

    WALKER   legs/wheels/tentacles: walk = leg phase + bob, run = lean + leg phase + dust
    FLYER    wings/hover: idle = hover bob, walk = flap + bob, run = dive lean + afterimage + speed lines
    SWARM    3-5 S members; every state is a group formation (per-member offsets and phase), see formation()
    TURRET   mount, 1 direction (symmetric): walk = eye/barrel tracking sweep, run = fast slew + spin-up flash,
             turn = a full aim sweep, attack = recoil + muzzle
    MIMIC    the room object: idle = still + a 1px settle, fidget = rattle/lid lift, walk = subtle scoot,
             run = hop, windup = the disguise opens (eyes, then teeth), recover = closes back into the object
    HEAVY    L/XL ground mob: stomp walk (shake event), run = charge
    BLINKER  walks; run = dissolve out, reappear 8px ahead
    BURROWER walk = half sunk with a travelling mound, run = fully under; spawns by rising
    PHASER   dither-flicker walk (through walls), run = flicker + afterimages

Views: 0 S front | 1 E side (face slides toward the facing) | 2 N back (face removed, mirrored shading) |
3 W = E mirrored. Every view is derived from the one front look by part ops, so there is no per-view art.
"""
from __future__ import annotations

import core as C
from core import F, M, S

TYPE = "mob"
DIRS = 4
REQUIRED = list(C.MIN_STATES)
MOBILITIES = ["WALKER", "FLYER", "SWARM", "TURRET", "MIMIC", "HEAVY", "BLINKER", "BURROWER", "PHASER"]
MOVE = {"WALKER": "legs", "FLYER": "fly", "SWARM": "legs", "TURRET": "track", "MIMIC": "scoot", "HEAVY": "heavy",
        "BLINKER": "blink", "BURROWER": "burrow", "PHASER": "phase"}
GEN_MOB = {"WALKER": "WALKER", "FLYER": "FLYER", "SWARM": "SWARM", "TURRET": "TURRET", "MIMIC": None,
           "HEAVY": "WALKER", "BLINKER": None, "BURROWER": "WALKER", "PHASER": None}
SIZES_OK = {"SWARM": [0], "TURRET": [1, 2, 3], "HEAVY": [2, 3], "MIMIC": [1]}
MIMIC_OBJ = {"OFFICE": "MUG", "VENTS": "CRATE", "ATRIUM": "CRATE", "SERVER": "CRT", "MALL": "MUG",
             "GARDEN": "SEED", "ROOFTOP": "CRATE", "GLITCH": "CRT"}
W = H = 64
GROUND = H - 9
MIMIC_GROUND = H - 8 - 16 + 2 + 15


def sizes_for(m):
    return SIZES_OK.get(m, [0, 1, 2, 3])


def make(fam, size, mobility, seed, elite=False, affixes=None):
    lk = M.gen(seed, fam=fam, size=size, mobility=GEN_MOB[mobility])
    if mobility == "MIMIC":
        lk["mobility"] = "MIMIC"
        lk["mimic_obj"] = MIMIC_OBJ[fam]
    if mobility == "HEAVY" and lk["limb"] in ("WINGS", "HOVER", "MOUNT", "NONE"):
        lk["limb"] = next((l for l in M.allowed_limbs(fam, lk["body"], size, "WALKER") if l in ("LEGS2", "LEGS4", "LEGS6", "WHEELS")), lk["limb"])
    if elite:
        lk = M.make_elite(lk, seed, affixes=affixes)
    move = MOVE[mobility]
    if mobility == "SWARM" and lk["limb"] in ("WINGS", "HOVER"):
        move = "fly"
    r = M.Rng(seed ^ 0x5A4D)
    v = {"type": TYPE, "id": f"{mobility}-{fam}-{M.SIZE_NAME[size]}{'-ELITE' if elite else ''}-{seed & 0xffff:04x}",
         "look": lk, "mobility": mobility, "move": move, "dirs": 1 if mobility == "TURRET" else 4,
         "elite": bool(elite), "quirk": lk["quirk"],
         "entrance": "glitch" if fam == "GLITCH" else ("rise" if mobility == "BURROWER" else "drop"),
         "status_tag": lk["tag"] if lk["tag"] != "NONE" else ["HOT", "WET", "SHOCK", "COLD"][r.pick(4)],
         "tell": 24 if r.pick(3) == 0 else 12}
    if mobility == "SWARM":
        base = [(-9, -3), (8, -5), (0, 3), (-6, 7), (10, 5)]
        n = 3 + r.pick(3)
        v["members"] = [(bx + r.pick(3) - 1, by + r.pick(3) - 1, i) for i, (bx, by) in enumerate(base[:n])]
    return v


def coverage_variants(seed=0):
    """every family x every allowed size x every mobility"""
    out = []
    for fi, fam in enumerate(M.FAMILIES):
        for mi, m in enumerate(MOBILITIES):
            for sc in sizes_for(m):
                out.append(make(fam, sc, m, M.mix_seed(0xA417 + seed, fi, mi, sc)))
    return out


def variants(n, seed=0):
    out = []
    for i in range(n):
        m = MOBILITIES[i % len(MOBILITIES)]
        fam = M.FAMILIES[(seed + i // len(MOBILITIES) + i) % 8]
        szs = sizes_for(m)
        out.append(make(fam, szs[(seed + i // 3) % len(szs)], m, M.mix_seed(0xB0B + seed, i)))
    return out


_ST = {}


def states(v):
    if v["id"] in _ST:
        return _ST[v["id"]]
    lk = v["look"]
    speed = 1.5 if "HASTED" in lk["affixes"] else (0.75 if v["mobility"] == "HEAVY" else 1.0)
    st = C.creature_states(v["move"], v["quirk"], v["entrance"], v["tell"], speed)
    if v["mobility"] == "HEAVY":
        st["attack"]["events"].append((1, "shake:2"))
    if v["elite"]:  # entrance: name tag, a 12-frame pause, camera nudge, its own sting; the pulse runs throughout
        st["intro"] = S([F(12, sy=110), F(6, pal="flash"), F(6), F(12, squash=1)], False,
                        [(0, "elite_sting"), (0, "pause:12"), (0, "camera:nudge"), (0, "name_tag")])
    _ST[v["id"]] = st
    return st


# =====================================================================================================================
# canvas ops on mobgen's indexed canvas (codes 1-3 body A, 4-6 overlay B, 7-9 elite E)
# =====================================================================================================================
_RC = {}


def _work(lk):
    work = dict(lk)
    if lk["affixes"] and lk["size"] < 3:  # elite size bump, as mobgen.mob_image
        work["size"] = lk["size"] + 1
        if not M.face_layout(work["fam"], work["body"], work["size"], work["limb"], work["eyes"], work["style"], work["mouth"]):
            work["mouth"] = next(m for m in M.MOUTHS if M.face_layout(work["fam"], work["body"], work["size"], work["limb"],
                                                                       work["eyes"], work["style"], m))
    return work


def _render(work, mp):
    key = (repr(sorted((k, v) for k, v in work.items() if not isinstance(v, list))), tuple(work["affixes"]),
           tuple(sorted(mp.items())))
    if key not in _RC:
        if len(_RC) > 20000:
            _RC.clear()
        _RC[key] = M.render(work, mp, W, H).c
    return [row[:] for row in _RC[key]]


def _face_rect(work, mp):
    f, b, sc, limb = work["fam"], work["body"], work["size"], work["limb"]
    lay = M.face_layout(f, b, sc, limb, work["eyes"], work["style"], mp.get("mouth") or work["mouth"]) or \
        M.face_layout(f, b, sc, limb, work["eyes"], work["style"], work["mouth"])
    sq = mp.get("squash", 0)
    et, mt = lay["et"], lay["mt"]
    xs, ys = [], []
    for ex, ey in lay["eyes"]:
        xs += [ex, ex + M.tw(et) - 1]
        ys += [ey + sq // 2 - 1, ey + sq // 2 + len(et)]
    mx, my = lay["mouth_xy"]
    if M.tw(mt):
        xs += [mx, mx + M.tw(mt) - 1]
        ys += [my + sq // 2, my + sq // 2 + len(mt) - 1]
    return min(xs) - 1, min(ys) - 1, max(xs) + 1, max(ys) + 1


def _interior(c, x, y):
    return 0 < x < W - 1 and 0 < y < H - 1 and c[y][x] and c[y][x - 1] and c[y][x + 1] and c[y - 1][x] and c[y + 1][x]


def _back(cv, rect):
    x0, y0, x1, y1 = rect
    out = [row[:] for row in cv]
    for y in range(max(1, y0), min(H - 1, y1 + 1)):
        for x in range(max(1, x0), min(W - 1, x1 + 1)):
            if cv[y][x] in (1, 2, 3, 4, 5, 6) and _interior(cv, x, y):
                out[y][x] = 3 if C.BAYER[y & 3][x & 3] < 3 else 2
    return out


def _slide_face(front, back, rect, k):
    """the face slides k px across the body (side view / glance / turret aim)"""
    x0, y0, x1, y1 = rect
    out = [row[:] for row in back]
    for y in range(max(0, y0), min(H, y1 + 1)):
        for x in range(max(0, x0), min(W, x1 + 1)):
            if front[y][x] != back[y][x]:
                nx = x + k
                if _interior(back, nx, y):
                    out[y][nx] = front[y][x]
    return out


def _mirror(cv):
    return [row[::-1] for row in cv]


def view_canvas(work, mp, view, look=0):
    cv = _render(work, mp)
    if view == 0 and not look:
        return cv
    rect = _face_rect(work, mp)
    back = _back(cv, rect)
    if view == 2:
        return _mirror(back)
    if view == 0:
        return _slide_face(cv, back, rect, look)
    k = max(1, M.SIZES[work["size"]] // 6) + max(0, look)
    side = _slide_face(cv, back, rect, k)
    return side if view == 1 else _mirror(side)


def _shift(cv, dx, dy, keep_shadow):
    if not dx and not dy:
        return cv
    out = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            v = cv[y][x]
            if v:
                if keep_shadow and y >= GROUND - 1 and v == 1:
                    out[y][x] = v
                    continue
                X, Y = x + dx, y + dy
                if 0 <= X < W and 0 <= Y < H:
                    out[Y][X] = v
    return out


def _pal(work, mode, tint):
    if C.COUNT_MODE:
        mode = "normal"
    if mode in ("normal", "tell", "hurt", "death"):
        return M.palettes(work, mode)
    p = M.palettes(work, "normal")
    if mode == "flash":
        p = M.palettes(work, "hurt")
        p[1] = C.mix(p[1], C.WHITE, 6)
    elif mode == "frozen":
        p[1:4] = C.ICE
        p[5] = C.mix(p[5], C.ICE[2], 8)
    elif mode == "status":
        p[2] = M.q555(C.mix(p[2], tint, 8))
        p[3] = M.q555(C.mix(p[3], tint, 6))
    elif mode == "dim":
        p[2] = M.q555(C.mix(p[2], C.VOID, 5))
        p[3] = M.q555(C.mix(p[3], C.VOID, 5))
    return p


def _eyes(e):
    return {"happy": "closed", "angry": "open"}.get(e, e)


def render_single(lk, pose, direction, phase=0, tint=(255, 120, 40), fx=True):
    work = _work(lk)
    t = pose.get("turn", 0)
    view = (direction + 1) % 4 if t == 2 else direction
    mp = {k: pose[k] for k in ("squash", "leg", "wing", "twitch", "mouth") if pose.get(k)}
    if pose.get("eyes"):
        mp["eyes"] = _eyes(pose["eyes"])
    cv = view_canvas(work, mp, view, pose.get("look", 0))
    dx, dy = C.offsets(pose, direction)
    cv = _shift(cv, dx, dy, work["limb"] == "HOVER")
    if lk["affixes"] and not C.COUNT_MODE:
        cvo = M.Canvas(W, H)
        cvo.c = cv
        cv = M.elite_parts(cvo, work, phase).c
    cvo = M.Canvas(W, H)
    cvo.c = cv
    img = M.to_img(cvo, _pal(work, pose.get("pal", "normal"), tint))
    p2 = pose if fx else {k: v for k, v in pose.items() if k != "fx"}
    return C.post(img, p2, direction, GROUND, native=True, pal_done=True)


# ---- mimic ----------------------------------------------------------------------------------------------------------
def render_mimic(v, pose, direction, phase):
    from PIL import Image
    lk = v["look"]
    img = M.mimic_image(lk, phase if not C.COUNT_MODE else 0, W, H)
    ox, oy = (W - 16) // 2, H - 8 - 16 + 2
    op = pose.get("open", 0)
    if op:  # the lid lifts
        lid = img.crop((ox, oy, ox + 16, oy + 5))
        img.paste((0, 0, 0, 0), (ox, oy, ox + 16, oy + 5))
        img.paste(lid, (ox, oy - op), lid)
    rv = pose.get("reveal", 0)
    view = (direction + 1) % 4 if pose.get("turn") == 2 else direction
    if rv and view != 2:  # the look's own eyes (and teeth) show through the disguise
        B = M.pal_overlay(lk["fam"], lk["tag"], lk["accent"])
        px = img.load()
        k = 2 if view == 1 else (-2 if view == 3 else 0)
        for ex in ((ox + 4 + k, ox + 10 + k) if lk["eyes"] > 1 else (ox + 7 + k,)):
            for (ddx, ddy, c) in ((0, 0, C.WHITE), (1, 0, C.WHITE), (0, 1, B[2]), (1, 1, C.INK)):
                px[ex + ddx, oy + 6 + ddy] = (*c, 255)
        if rv >= 2:
            for i in range(3, 13):
                px[ox + i + k // 2, oy + 10 + (i & 1)] = (*(C.WHITE if i & 1 else C.INK), 255)
    if view in (1, 3):
        img = C.scale_feet(img, 86, 100)
    if view in (2, 3):
        img = img.transpose(Image.FLIP_LEFT_RIGHT)
    return C.post(img, pose, direction, MIMIC_GROUND)


# ---- swarm ------------------------------------------------------------------------------------------------------------
def formation(state, fi, i, n, direction):
    """per-member offset for a swarm member i of n in frame fi of a state (the group's choreography)"""
    import math
    vx, vy = C.fwd_vec(direction)
    sc, rot, jit, lag = 1.0, 0.0, 0, 0
    if state == "run":
        sc, lag = 0.6, 2
    elif state == "walk":
        lag = 1
    elif state == "turn":
        rot = fi * math.pi / 6
    elif state in ("windup",):
        sc = 0.8 - 0.15 * fi
    elif state == "attack":
        sc = 0.5
    elif state == "recover":
        sc = 0.6 + 0.2 * fi
    elif state == "hurt":
        sc = 1.4
    elif state == "stagger":
        sc, jit = 1.2, 1
    elif state == "stun":
        rot = fi * math.pi / 4
    elif state == "status":
        jit = 1
    elif state == "idle_fidget":
        jit = 1 if fi % 2 else 0
    elif state == "spawn":
        sc = 1.6 - 0.12 * fi
    elif state == "death":
        sc = 1.0 + 0.15 * fi
    return sc, rot, jit, lag, vx, vy


def render_swarm(v, state, fi, direction):
    import math
    from PIL import Image
    st = states(v)[state]
    frames = st["frames"]
    nfr = len(frames)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    mem = v["members"]
    tint = M.ELEMENT[v["status_tag"]][1]
    for (ox, oy, i) in sorted(mem, key=lambda m: m[1]):
        if state == "death":
            mf = min(nfr - 1, fi + (i % 2))
        elif state == "spawn":
            mf = nfr - 1 if fi == nfr - 1 else max(0, fi - (i % 2))
        elif st["loop"]:
            mf = (fi + i) % nfr
        else:
            mf = fi
        sc, rot, jit, lag, vx, vy = formation(state, fi, i, len(mem), direction)
        x = ox * math.cos(rot) - oy * math.sin(rot)
        y = ox * math.sin(rot) + oy * math.cos(rot)
        x, y = int(round(x * sc)) - vx * lag * i, int(round(y * sc)) - vy * lag * i
        if jit:
            x += ((fi * 3 + i * 5) % 3) - 1
            y += ((fi * 5 + i * 3) % 3) - 1
        im = render_single(v["look"], frames[mf]["pose"], direction, (fi + i) % 2, tint, fx=False)
        out.alpha_composite(C.shift(im, x, y))
    return C.fx(out, frames[fi]["pose"].get("fx", ()), direction, GROUND)


def render(v, state, frame_index, direction):
    st = states(v)[state]
    pose = st["frames"][frame_index]["pose"]
    if v["dirs"] == 1:
        direction = 0
    if v["mobility"] == "MIMIC":
        return render_mimic(v, pose, direction, frame_index % 2)
    if v["mobility"] == "SWARM":
        return render_swarm(v, state, frame_index, direction)
    return render_single(v["look"], pose, direction, frame_index % 2, M.ELEMENT[v["status_tag"]][1])
