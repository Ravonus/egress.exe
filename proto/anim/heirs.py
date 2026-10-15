"""EGRESS heirs: the full animation state set + the varied idle scheduler (design-stage prototype).

Shared interface (see core.py): TYPE, DIRS, REQUIRED, variants(n, seed), states(variant), render(variant, state, i, d).
Heirs use 8 directions: 0 S, 1 SE, 2 E, 3 NE, 4 N, 5 NW, 6 W, 7 SW.

Rendering is the recommended style direction B ("neon chibi", 16x24, BODY + WEAR OBJ layers, 6 colours) from
tools/mock/sprite_styles.py. A state is pure data: frames of {dur (ticks @ 60 Hz), pose} where a pose is a few small
ints / flags the rig reads (squash, bob, crouch, lean, stride, lift, swing, hand offsets, eyes, mouth, weapon, fx,
palette flash, dissolve...). Every generated heir therefore gets every state: the parts come from its genome and the
poses are shared.

    .venv/bin/python proto/anim/heirs.py            # writes docs/shots/anim_heir_states.png, anim_heir_idle30s.gif,
                                                    # sprite_styles_anim_B.png (+ .gif)
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
for _p in (ROOT / "tools", ROOT / "tools/mock", ROOT / "proto/genetics"):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))
import genes  # noqa: E402
import sprite_styles as SS  # noqa: E402

try:
    import core as _core  # the shared harness core (COUNT_MODE)
except Exception:  # noqa: BLE001
    _core = None

TYPE = "heir"
DIRS = 8
STYLE = "B"
HZ = 60


def F(dur, **pose):
    return {"dur": int(dur), "pose": pose}


def S(frames, loop=False, events=()):
    return {"frames": frames, "loop": loop, "events": list(events)}


BOTH = lambda dx, dy: {"n": (dx, dy), "f": (dx, dy)}  # noqa: E731
UP = {"n": (0, -9), "f": (0, -9)}

# ------------------------------------------------------------------------------------------------ the state table
WALK = [F(8, stride=2, swing=2, sq=1), F(8, lift=1, sq=-1), F(8, stride=-2, swing=-2, sq=1), F(8, lift=-1, sq=-1)]
RUN = [F(6, stride=3, swing=3, lean=1, sq=1, fx=["dust:0"]), F(5, lift=2, lean=1, sq=-1, bob=-1),
       F(6, stride=-3, swing=-3, lean=1, sq=1, fx=["dust:1"]), F(5, lift=-2, lean=1, sq=-1, bob=-1)]
SIT = dict(crouch=3, stride=0)


def _table():
    T = {
        # ---- minimum set
        "idle": S([F(36), F(24, sq=-1), F(36), F(24, sq=1)], True),  # breathe: chest up (stretch), settle (squash)
        "idle_blink": S([F(90), F(4, eyes="half", sq=1), F(5, eyes="closed", sq=1, hdy=1), F(4, eyes="half")], True),
        "idle_fidget": S([F(14, dir=1, hdx=1), F(20, dir=1, gaze=1, sq=-1), F(14), F(20, dir=-1, gaze=-1, sq=-1),
                          F(14)]),  # look around (the generic fidget; the pool is below)
        "walk": S(WALK, True, [(0, "step"), (2, "step")]),
        "run": S(RUN, True, [(0, "step"), (2, "step")]),
        "turn": S([F(4, dir=2, sq=1), F(4, dir=1), F(5, sq=-1)], False),  # from 90 deg away into this direction
        "windup": S([F(10, weapon="raise", lean=-1, sq=1, hands={"n": (-1, -4)}),
                     F(12, weapon="raise", lean=-1, sq=2, eyes="wide", hands={"n": (-1, -5)}, charge=1)], False,
                    [(0, "tell")]),
        "attack": S([F(4, weapon="swing", lean=1, sq=-1, hands={"n": (2, -1)}),
                     F(6, weapon="swing", lean=2, sq=-2, stride=1, hands={"n": (3, 0)})], False,
                    [(0, "hit"), (0, "sfx:swing")]),
        "recover": S([F(8, weapon="held", lean=1, sq=1), F(10, weapon="held", sq=0)], False),
        "hurt": S([F(3, flash="white", sq=2, lean=-1, eyes="closed", mouth="o"),
                   F(4, flash="red", sq=-1, lean=-1, mouth="o"), F(8, sq=1, eyes="closed")], False,
                  [(0, "iframes:30"), (0, "shake:2")]),
        "stagger": S([F(8, lean=-2, lift=1, swing=-2, sq=1, eyes="closed"), F(8, lean=-1, lift=-1, swing=2),
                      F(8, lean=-2, sq=1)], False),
        "stun": S([F(10, eyes="x", fx=["stars:0"], lean=-1, hdx=-1), F(10, eyes="x", fx=["stars:1"], lean=1, hdx=1),
                   F(10, eyes="x", fx=["stars:2"])], True, [(0, "status:stun")]),
        "frozen": S([F(20, flash="ice", fx=["ice"], sq=1, eyes="dull"), F(4, flash="ice", fx=["ice", "shiver:1"], sq=1,
                                                                            eyes="dull")], True,
                    [(0, "status:frozen")]),
        "status": S([F(12, flash="dim", fx=["sweat:0"], crouch=1, eyes="half"),
                     F(12, fx=["sweat:1"], crouch=1, sq=1, eyes="half")], True, [(0, "status:poison")]),
        "death": S([F(4, flash="white", sq=2), F(10, crouch=2, eyes="closed", sq=1),
                    F(8, crouch=3, eyes="closed", dissolve=0.3), F(8, crouch=3, eyes="closed", dissolve=0.6),
                    F(12, crouch=3, dissolve=0.9)], False, [(4, "despawn")]),
        "spawn": S([F(6, assemble=0.0), F(6, assemble=0.35), F(6, assemble=0.7), F(4, assemble=1.0, sq=2),
                    F(4, sq=-1), F(6)], False, [(0, "spawn_fx:assemble"), (5, "commit")]),
        # ---- heir extras
        "dodge_roll": S([F(3, crouch=2, sq=2), F(4, ball=0), F(4, ball=1), F(4, ball=2), F(4, ball=3), F(3, sq=-2),
                         F(4, sq=1)], False, [(1, "iframes:16"), (6, "commit")]),
        "dodge_blink": S([F(3, sq=-1, dissolve=0.3), F(3, dissolve=0.7, glitch=1), F(4, dissolve=0.95),
                          F(3, dissolve=0.6, glitch=2), F(3, dissolve=0.2), F(4, sq=1)], False,
                         [(1, "iframes:12"), (5, "commit")]),
        "dodge_slide": S([F(3, crouch=3, lean=2, stride=2, sq=1, fx=["dust:0"]),
                          F(6, crouch=4, lean=3, stride=3, fx=["dust:1"]),
                          F(5, crouch=3, lean=2, stride=2, fx=["dust:2"]), F(4, crouch=1, sq=-1)], False,
                         [(0, "iframes:14"), (3, "commit")]),
        "sprint": S([F(5, stride=3, swing=3, lean=2, sq=2, fx=["dust:0"]), F(4, lift=2, lean=2, sq=-2, bob=-1),
                     F(5, stride=-3, swing=-3, lean=2, sq=2, fx=["dust:1"]), F(4, lift=-2, lean=2, sq=-2, bob=-1)],
                    True, [(0, "step"), (2, "step")]),
        "lunge": S([F(6, crouch=2, sq=2, lean=-1), F(5, lean=3, stride=3, sq=-2, weapon="thrust"),
                    F(8, lean=2, stride=2, weapon="held", sq=1)], False, [(1, "hit")]),
        "charge": S([F(10, weapon="held", charge=1, sq=1), F(10, weapon="raise", charge=2, sq=1),
                     F(10, weapon="raise", charge=3, sq=2)], True, [(2, "charged")]),
        "spin": S([F(3, dir=0, weapon="swing", fx=["ring:0"]), F(3, dir=2, weapon="swing", fx=["ring:1"]),
                   F(3, dir=4, weapon="swing", fx=["ring:2"]), F(3, dir=6, weapon="swing", fx=["ring:3"])], False,
                  [(0, "hit"), (2, "hit")]),
        "parry": S([F(3, hands=BOTH(1, -5), sq=1, lean=-1), F(4, hands=BOTH(1, -5), fx=["spark"], sq=-1),
                    F(8, hands=BOTH(1, -3))], False, [(0, "parry_window"), (1, "sfx:parry")]),
        "push": S([F(10, lean=2, hands=BOTH(3, -2), stride=1, sq=1), F(10, lean=2, hands=BOTH(3, -2), lift=1),
                   F(10, lean=2, hands=BOTH(3, -2), stride=-1, sq=1), F(10, lean=2, hands=BOTH(3, -2), lift=-1)], True,
                  [(0, "step"), (2, "step")]),
        "dig": S([F(8, crouch=2, hands=BOTH(1, 2), lean=1), F(6, crouch=3, hands=BOTH(2, 3), sq=1, fx=["dust:0"]),
                  F(8, crouch=1, hands=BOTH(0, -1))], True, [(1, "dig")]),
        "pickup": S([F(6, crouch=3, hands=BOTH(1, 3)), F(6, crouch=1, hands=BOTH(0, -2)),
                     F(14, hands=UP, item="box", sq=-1)], False, [(2, "pickup")]),
        "bench": S([F(16, **SIT, hands=BOTH(2, -2), eyes="half"), F(8, **SIT, hands=BOTH(1, -3), fx=["spark"]),
                    F(16, **SIT, hands=BOTH(2, -2), gaze=1)], True, [(1, "sfx:craft")]),
        # ---- fidget pool
        "fidget_look": S([F(14, dir=1, hdx=1), F(20, dir=1, gaze=1, sq=-1), F(14), F(20, dir=-1, gaze=-1, sq=-1),
                          F(14)]),
        "fidget_stretch": S([F(10, hands=UP, sq=-2, eyes="closed"), F(24, hands=UP, sq=-2, mouth="yawn",
                                                                      eyes="closed"), F(10, sq=1)]),
        "fidget_lanyard": S([F(10, badge_up=True, hands={"n": (-2, -4)}, hdy=1), F(30, badge_up=True,
                                                                                   hands={"n": (-2, -4)}, hdy=1,
                                                                                   eyes="half"), F(10)]),
        "fidget_yawn": S([F(10, mouth="yawn", eyes="closed", sq=-1, hands={"n": (-1, -6)}),
                          F(24, mouth="yawn", eyes="closed", sq=-2, hands={"n": (-1, -6)}), F(10, sq=1)]),
        "fidget_scratch": S([F(8, hands={"n": (-1, -9)}), F(6, hands={"n": (-2, -9)}, hdx=1),
                             F(6, hands={"n": (-1, -9)}), F(6, hands={"n": (-2, -9)}, hdx=1), F(8)]),
        "fidget_shift": S([F(20, lean=1, lift=-1, sq=1), F(20, lean=-1, lift=1, sq=1), F(10)]),
        "fidget_tap": S([F(6, lift=1), F(6, sq=1), F(6, lift=1), F(6, sq=1), F(6, lift=1), F(10)]),
        "fidget_glance": S([F(10, face_cam=True, sq=-1), F(30, face_cam=True, eyes="wide"), F(10, sq=1)]),
        "fidget_glitch": S([F(3, glitch=1), F(3, glitch=2, flash="dim"), F(3, glitch=3), F(8)]),
        "fidget_twitch": S([F(3, hdx=1, sq=1), F(3, hdx=-1), F(3, hdx=1, sq=-1), F(6)]),  # INSOMNIAC
        "fidget_bounce": S([F(6, bob=-2, sq=-1), F(5, sq=2), F(6, bob=-1, sq=-1), F(5, sq=1)]),  # SMALL
        # ---- long idles (about 10 s and up)
        "long_sit": S([F(40, **SIT, hands=BOTH(1, 0)), F(40, **SIT, hands=BOTH(1, 0), sq=1)], True),
        "long_sit_twitch": S([F(3, **SIT, hdx=1, sq=1, eyes="wide"), F(3, **SIT, hdx=-1), F(6, **SIT, sq=1)]),
        "long_lie": S([F(50, lie=True, eyes="closed"), F(50, lie=True, eyes="closed", sq=1)], True),
        "long_doze": S([F(40, **SIT, eyes="closed", hdy=1, fx=["zzz:0"]),
                        F(40, **SIT, eyes="closed", hdy=1, sq=1, fx=["zzz:1"])], True),
        # ---- context idles
        "ctx_ready": S([F(20, weapon="held", crouch=1, lean=1), F(20, weapon="held", crouch=1, sq=1)], True),
        "ctx_cool": S([F(24, weapon="held", mouth="o", fx=["breath:0"]), F(24, weapon="held", sq=-1,
                                                                            fx=["breath:1"])], True),
        "ctx_lowhp": S([F(16, crouch=2, lean=1, hdy=1, eyes="half", mouth="o", fx=["sweat:0"]),
                        F(16, crouch=2, hdy=2, sq=1, mouth="o", fx=["sweat:1"])], True),
        "ctx_shiver": S([F(4, hands=BOTH(1, -3), sq=1, fx=["shiver:0"]), F(4, hands=BOTH(1, -3),
                                                                           fx=["shiver:1"])], True),
        "ctx_warm": S([F(14, hands=BOTH(2, -3), lean=1, fx=["warm:0"]), F(14, hands=BOTH(2, -2), lean=1, sq=1,
                                                                         fx=["warm:1"])], True),
        "ctx_peer": S([F(20, lean=1, hands={"n": (0, -8)}, gaze=1), F(20, lean=2, gaze=1, hands={"n": (0, -8)},
                                                                     sq=1)], True),
        "ctx_wave": S([F(8, hands={"n": (2, -9)}, fx=["wave:0"]), F(8, hands={"n": (3, -8)}, fx=["wave:1"]),
                       F(8, hands={"n": (2, -9)}, fx=["wave:0"]), F(12)], False, [(0, "sfx:hey")]),
    }
    return T


TABLE = _table()
FIDGETS = ["fidget_look", "fidget_stretch", "fidget_lanyard", "fidget_yawn", "fidget_scratch", "fidget_shift",
           "fidget_tap", "fidget_glance", "fidget_glitch"]
LONG = ["long_sit", "long_sit_twitch", "long_lie", "long_doze"]
CTX = ["ctx_ready", "ctx_cool", "ctx_lowhp", "ctx_shiver", "ctx_warm", "ctx_peer", "ctx_wave"]
REQUIRED = ["idle", "idle_blink", "idle_fidget", "walk", "run", "turn", "windup", "attack", "recover", "hurt",
            "stagger", "stun", "frozen", "status", "death", "spawn",
            "dodge", "dodge_roll", "dodge_blink", "dodge_slide", "sprint", "lunge", "charge", "spin", "parry", "push",
            "dig", "pickup", "bench"] + FIDGETS + ["fidget_twitch", "fidget_bounce"] + LONG + CTX


def states(variant):
    T = dict(TABLE)
    T["dodge"] = T["dodge_" + variant["dodge"].lower()]  # the heir's own dodge (dodge gene)
    return T


def state_len(name):
    return sum(f["dur"] for f in TABLE[name]["frames"])


# ------------------------------------------------------------------------------------------------ variants
def variants(n, seed):
    """n generated heirs: a founder line from genes.py, n siblings/descendants (diverse skin across lines)"""
    out = []
    rng = genes.Rng((seed + 1) * 7919)
    for i in range(n):
        g = genes.founder(rng)
        g = genes.child(g, rng, [])
        h = SS.make_heir(g, f"H{i}")
        h["id"] = f"heir{i}-{h['genome']}-{SS.HAIRS[h['hair']].lower()}-{h['dodge'].lower()}"
        out.append(h)
    return out


def variant_from_heir(h):
    h = dict(h)
    h.setdefault("id", h.get("name", "heir"))
    return h


def render(variant, state, frame_index, direction):
    st = states(variant)[state]
    fr = st["frames"][frame_index % len(st["frames"])]
    pose = dict(fr["pose"])
    if pose.pop("face_cam", False):
        pose["dir"] = (-direction) % 8  # glance at the camera: the head + body turn to S for the frame
    if _core is not None and getattr(_core, "COUNT_MODE", False):
        pose.pop("flash", None)
        pose.pop("fx", None)
    return SS.render(STYLE, variant, direction, pose)


# ------------------------------------------------------------------------------------------------ idle scheduler
class _Lcg:
    def __init__(self, seed):
        self.s = (seed * 2654435761 + 1) & 0xFFFFFFFF or 1

    def u8(self):
        self.s = (self.s * 1103515245 + 12345) & 0xFFFFFFFF
        return (self.s >> 16) & 0xFF

    def below(self, n):
        return self.u8() * n >> 8


LONG_AFTER = 600  # 10 s of no input -> long idle
FIDGET_GAP = (150, 210)  # base idle between fidgets: 2.5 s + up to 3.5 s (seeded)


def fidget_pool(traits, context=None):
    """weighted pool [(state, weight)]: traits and context change it"""
    w = {f: 4 for f in FIDGETS}
    w["fidget_glitch"] = 1
    t = set(traits or ())
    if "INSOMNIAC" in t:
        w["fidget_twitch"] = 10
        w["fidget_yawn"] = 0  # never sleepy-looking
    if "HEAVY" in t:
        w["fidget_shift"] = 16
    if "GLITCHED" in t:
        w["fidget_glitch"] = 14
    if "SMALL" in t:
        w["fidget_bounce"] = 10
    if context in ("water", "cold"):
        w["ctx_shiver"] = 12
    if context == "fire":
        w["ctx_warm"] = 12
    if context == "dark":
        w["ctx_peer"] = 12
    return [(k, v) for k, v in w.items() if v > 0]


def base_for(context, hp_low):
    if hp_low:
        return "ctx_lowhp"
    if context == "fight":
        return "ctx_ready"
    return "idle"


def idle_plan(seed, traits=(), until=1800, inputs=(), context=None, hp_low=False):
    """Pure: the idle timeline from idle start (tick 0) to `until`, as [(start, end, state)].

    Rules: a base loop (breathing; weapon-ready after a fight, hunched when HP is low) with a seeded fidget every
    2.5-6 s; never the same fidget twice in a row; after 10 s a long idle (sit -> doze, or lie down; INSOMNIAC never
    dozes and stays sitting; low HP never lies down); a talker nearby gets one wave first; the first input tick
    cancels instantly (the timeline ends there). Deterministic from the seed.
    """
    rng = _Lcg(seed)
    cut = min([until] + [t for t in inputs if t >= 0])
    t, out, last = 0, [], None
    traits = tuple(traits or ())
    base = base_for(context, hp_low)

    def add(name, dur):
        nonlocal t
        end = min(cut, t + dur)
        if end > t:
            out.append((t, end, name))
        t = end

    if context == "talker":
        add("ctx_wave", state_len("ctx_wave"))
        last = "ctx_wave"
    if context == "fight" and not hp_low:
        add("ctx_ready", 180)
        add("ctx_cool", 120)
        base = "idle"
    while t < cut:
        if t >= LONG_AFTER:
            if hp_low:
                add("ctx_lowhp", cut - t)
                break
            add("long_sit", 240)
            if "INSOMNIAC" in traits:
                while t < cut:  # sits, twitches, never dozes
                    add("long_sit", 120 + rng.below(120))
                    add("long_sit_twitch", state_len("long_sit_twitch"))
                break
            add("long_doze" if rng.below(2) == 0 else "long_lie", cut - t)
            break
        add(base, FIDGET_GAP[0] + rng.below(FIDGET_GAP[1]))
        if t >= cut or t >= LONG_AFTER:
            continue
        pool = [(k, v) for k, v in fidget_pool(traits, context) if k != last]
        tot = sum(v for _, v in pool)
        r = rng.below(tot)
        for k, v in pool:
            if r < v:
                break
            r -= v
        add(k, state_len(k))
        last = k
    return out


# ------------------------------------------------------------------------------------------------ proof images
def _sheets():
    from PIL import Image, ImageDraw
    from egkit import CYAN, PINK, WHITE, YEL, text, slab
    out = ROOT / "docs/shots"
    hs = SS.four_heirs()
    v = variant_from_heir(hs[1])
    st = states(v)
    S_ = SS.STYLES[STYLE]
    fw, fh = S_["W"] + 2 * SS.M, S_["H"] + SS.M
    dirs = [0, 1, 2, 4]
    names = REQUIRED
    maxf = max(len(st[n]["frames"]) for n in names)
    W = 70 + len(dirs) * (maxf * (fw + 1) + 6)
    H = 24 + len(names) * (fh + 3) + 12
    im = Image.new("RGB", (W, H), (20, 14, 46))
    text(im, 2, 2, f"HEIR STATES, STYLE {STYLE}, {v['name']}: {len(names)} STATES X 8 DIRS. SHOWN S SE E N;"
                   " SW W NW = MIRRORED RIGS, NE DRAWN", PINK)
    for j, d in enumerate(dirs):
        text(im, 70 + j * (maxf * (fw + 1) + 6), 12, ["S", "SE", "E", "NE", "N"][d], CYAN)
    for i, n in enumerate(names):
        y = 24 + i * (fh + 3)
        text(im, 2, y + fh // 2 - 4, n.upper().replace("FIDGET_", "F.").replace("CTX_", "C.")[:8], CYAN)
        fr = st[n]["frames"]
        for j, d in enumerate(dirs):
            x0 = 70 + j * (maxf * (fw + 1) + 6)
            for k in range(len(fr)):
                f = render(v, n, k, d)
                im.paste(f, (x0 + k * (fw + 1), y), f)
    im.resize((W * 2, H * 2), Image.NEAREST).save(out / "anim_heir_states.png")
    # ---- 30 s of idling (GIF): fidgets, then the long idle after 10 s
    plan = idle_plan(seed=int(v["genome"], 16) & 0xFFFF, traits=v["traits"], until=1800)
    frames, durs = [], []
    bg = Image.new("RGB", (64, 64), (20, 14, 46))
    for yy in range(0, 64, 16):
        for xx in range(0, 64, 16):
            bg.paste(slab("lilac", xx + yy * 3), (xx, yy))
    for (a, b, name) in plan:
        fr = TABLE[name]["frames"]
        t, k = a, 0
        while t < b:
            dd = min(fr[k % len(fr)]["dur"], b - t)
            img = bg.copy()
            f = render(v, name, k % len(fr), 1)
            img.paste(f, (32 - fw // 2, 58 - fh), f)
            text(img, 1, 1, name.replace("fidget_", "").replace("long_", "").upper()[:7], WHITE)
            text(img, 1, 10, f"{t / 60:4.1f}S", YEL)
            frames.append(img.resize((256, 256), Image.NEAREST))
            durs.append(max(20, int(dd * 1000 / 60)))
            t += dd
            k += 1
    frames[0].save(out / "anim_heir_idle30s.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0,
                   disposal=1)
    print("idle plan:", [(round(a / 60, 1), n) for a, b, n in plan])
    # ---- recommended direction: all 8 directions x core states (squash/stretch visible)
    core = [("IDLE", "idle", [0, 1, 2, 3]), ("WALK", "walk", [0, 1, 2, 3]), ("ROLL", "dodge_roll", range(7)),
            ("WIND", "windup", [0, 1]), ("ATK", "attack", [0, 1]), ("HIT", "hurt", [0, 1, 2])]
    ncol = sum(len(list(c[2])) for c in core)
    W2 = 30 + ncol * (fw + 1) + len(core) * 4
    H2 = 22 + 8 * (fh + 2)
    sh = Image.new("RGB", (W2, H2), (20, 14, 46))
    text(sh, 2, 2, "STYLE B: 8 DIRECTIONS X IDLE, WALK, ROLL, WINDUP, ATTACK, HIT", PINK)
    x = 30
    for lab, n, ks in core:
        text(sh, x, 12, lab, CYAN)
        x += len(list(ks)) * (fw + 1) + 4
    for d in range(8):
        y = 22 + d * (fh + 2)
        text(sh, 2, y + fh // 2 - 4, ["S", "SE", "E", "NE", "N", "NW", "W", "SW"][d], YEL)
        x = 30
        for lab, n, ks in core:
            for k in ks:
                f = render(v, n, k, d)
                sh.paste(f, (x, y), f)
                x += fw + 1
            x += 4
    sh.resize((W2 * 3, H2 * 3), Image.NEAREST).save(out / "sprite_styles_anim_B.png")
    # GIF: walk around the 8 directions, then roll, attack, hurt
    gf, gd = [], []
    seq = [("walk", d, 2) for d in range(8)] + [("dodge_roll", 2, 1), ("windup", 1, 1), ("attack", 1, 1),
                                               ("recover", 1, 1), ("hurt", 0, 1), ("spin", 0, 2)]
    for n, d, reps in seq:
        fr = TABLE[n]["frames"] if n in TABLE else states(v)[n]["frames"]
        for _ in range(reps):
            for k in range(len(fr)):
                img = Image.new("RGB", (fw + 8, fh + 4), (40, 26, 86))
                f = render(v, n, k, d)
                img.paste(f, (4, 2), f)
                gf.append(img.resize((img.width * 6, img.height * 6), Image.NEAREST))
                gd.append(max(20, int(fr[k]["dur"] * 1000 / 60)))
    gf[0].save(out / "sprite_styles_anim_B.gif", save_all=True, append_images=gf[1:], duration=gd, loop=0)


if __name__ == "__main__":
    _sheets()
