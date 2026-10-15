"""EGRESS animation core: the shared state schema, the generic state builder and the image-level part ops.

Design-stage prototype (Python). Every type module in proto/anim/ uses the same interface:

    TYPE: str          DIRS: int          REQUIRED: list[str]
    variants(n, seed) -> list[dict]                                  (generated, never hand-drawn per variant)
    states(variant)   -> {name: {"frames": [{"dur": ticks@60Hz, "pose": {...}}], "loop": bool,
                                 "events": [(frame_index, "event")]}}
    render(variant, state, frame_index, direction) -> PIL RGBA at 1x

Directions: 0 = S (front, toward the camera), 1 = E (side, facing right), 2 = N (back), 3 = W (= E mirrored, free
on the GBC with the OAM X-flip). A type with DIRS = 1 renders direction 0 only.

Pose keys (all optional; a pose is a small struct of ints on the cart):
    dx dy        whole-sprite offset in px (free: OAM position)
    fwd          offset along the facing (resolved per direction into dx/dy)
    squash       px of top-half compression (squash), sx / sy percent scale about the feet (stretch)
    tilt         px of shear at the top row (lean / stagger)
    leg wing     limb phase 0/1;  twitch: topper px
    eyes         open | closed | roll | happy | angry;   mouth: None | O | TEETH | open | half
    look         face offset px toward the facing (glance, turret tracking)
    pal          normal | tell | hurt | flash | death | frozen | status | dim     (palette swaps, free on the GBC)
    dissolve     0..1 Bayer dissolve;  dph: dissolve phase (flicker);  clip: px sunk below the ground (burrow)
    trail        afterimages behind the movement;  tear: glitch row shift in px
    turn         0/1/2 turn interpolation (from this direction to the next clockwise)
    hide         1 = only the FX are drawn (spawn: the landing outline before the body arrives)
    fx           tuple of overlay names: bang stars<k> ice<k> puff<k> land dust<k> arc spark heart anger speed
                 sweat motes<k> zz (drawn from the shared FX OBJ palette)
Type-specific keys: reveal/open (mimic), hand/hands (boss satellites), cracks/core (boss phase damage).

Events: "hit", "tell", "iframes:N", "spawn_fx:NAME", "sfx:NAME", "shake:N", "commit", "despawn", "phase:N",
"step", "status:NAME", "arena:NAME", "talk_blip".
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
for _p in (ROOT / "tools", ROOT / "tools/mock"):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))
import egkit as E  # noqa: E402
import mobgen as M  # noqa: E402

HZ = 60
MIN_STATES = ["idle", "idle_fidget", "walk", "run", "turn", "windup", "attack", "recover", "hurt", "stagger",
              "stun", "frozen", "status", "death", "spawn"]
DIR_NAMES = ["S", "E", "N", "W"]
BAYER = E.BAYER
INK, WHITE, YEL, CYAN, PINK = E.INK, E.WHITE, E.YEL, E.CYAN, E.PINK
ICE = [(36, 70, 150), (120, 186, 246), (226, 248, 255)]
VOID = E.P["void"][1]
# the shared FX OBJ palette (palette 6 on the cart): ink, white, yellow, plus one per-room cue slot
FXC = {"ink": INK, "white": WHITE, "yel": YEL, "cyan": CYAN, "pink": PINK, "ice": ICE[1], "lilac": (214, 190, 246),
       "dust": (176, 160, 200)}

COUNT_MODE = False  # when True, renders skip palette swaps and FX (used to count unique OBJ tiles)


def F(dur, **pose):
    return {"dur": int(dur), "pose": pose}


def S(frames, loop=False, events=()):
    return {"frames": frames, "loop": loop, "events": list(events)}


def fwd_vec(direction):
    return [(0, 1), (1, 0), (0, -1), (-1, 0)][direction % 4]


def offsets(pose, direction):
    vx, vy = fwd_vec(direction)
    f = pose.get("fwd", 0)
    return pose.get("dx", 0) + vx * f, pose.get("dy", 0) + vy * f


# =====================================================================================================================
# the generic state builder (creature-like types: mobs, elites, bosses' bodies, ghosts, talkers)
# =====================================================================================================================
QUIRK_POSE = {"TWITCH": {"twitch": 1, "dx": 1}, "HOVER BOB": {"dy": -2}, "FLICKER": {"pal": "dim", "dy": -1},
              "BREATHE": {"squash": 2}, "EYE ROLL": {"eyes": "roll", "tilt": 1}}


def creature_states(move="legs", quirk="BREATHE", entrance="drop", tell=24, speed=1.0):
    """The minimum state set, interpreted per movement style.

    move: legs | fly | track (turret) | scoot (mimic) | heavy | blink | burrow | phase | glide (ghost) | shuffle (talker)
    speed: a HASTED or heavy mob scales its frame durations."""
    sp = lambda t: max(1, int(round(t / speed)))  # noqa: E731
    q = QUIRK_POSE.get(quirk, QUIRK_POSE["BREATHE"])
    st = {}
    # ---- idle: breathe/bob + fidget/blink -------------------------------------------------------------------------
    if move in ("fly", "glide"):
        st["idle"] = S([F(16), F(16, dy=-1), F(16, dy=-2), F(16, dy=-1)], True)
    elif move == "track":
        st["idle"] = S([F(24), F(24, squash=1, pal="dim")], True)
    elif move == "scoot":  # disguised: a 1px settle every so often (only a keen eye sees it breathe)
        st["idle"] = S([F(90), F(6, sy=94)], True)
    else:
        st["idle"] = S([F(24), F(24, squash=1)], True)
    st["idle_fidget"] = S([F(40), F(8, **q), F(5, eyes="closed", dy=1 if move not in ("fly", "glide") else -1),
                           F(8, **q)], True, [(2, "blink")])
    if move == "scoot":
        st["idle_fidget"] = S([F(60), F(4, tilt=1), F(4, tilt=-1), F(6, open=1)], True, [(3, "sfx:rattle")])
    # ---- walk / run ------------------------------------------------------------------------------------------------
    if move == "legs" or move == "shuffle":
        st["walk"] = S([F(sp(8)), F(sp(8), leg=1, dy=-1, tilt=1), F(sp(8), squash=1), F(sp(8), leg=1, dy=-1, dx=1, tilt=-1)],
                       True, [(0, "step"), (2, "step")])
        st["run"] = S([F(sp(5), tilt=2, fwd=1, fx=("dust0",)), F(sp(5), tilt=2, leg=1, dy=-2, fwd=1),
                       F(sp(5), tilt=2, squash=1, fwd=1, fx=("dust1",)), F(sp(5), tilt=2, leg=1, dy=-2, fwd=2)], True,
                      [(0, "step"), (2, "step"), (0, "spawn_fx:dust")])
    elif move in ("fly", "glide"):
        st["walk"] = S([F(sp(8), wing=1), F(sp(8), dy=-1), F(sp(8), wing=1, dy=-2), F(sp(8), dy=-1)], True,
                       [(0, "sfx:flap")])
        st["run"] = S([F(sp(4), tilt=3, wing=1, fwd=1, trail=1), F(sp(4), tilt=3, dy=-1, fwd=2, trail=2, fx=("speed",)),
                       F(sp(4), tilt=3, wing=1, fwd=1, trail=1), F(sp(4), tilt=3, dy=1, fwd=2, trail=2, fx=("speed",))],
                      True, [(0, "sfx:flap")])
    elif move == "track":  # a turret cannot walk: "walk" is the barrel/eye tracking, "run" a fast slew + spin-up
        st["walk"] = S([F(10, look=-2), F(10, look=-1), F(10, look=1), F(10, look=2)], True, [(0, "sfx:servo")])
        st["run"] = S([F(4, look=-2, squash=1), F(4, look=0, pal="tell"), F(4, look=2, squash=1), F(4, look=0)], True,
                      [(0, "sfx:servo_fast")])
    elif move == "scoot":  # disguised mimic: a subtle scoot; "run" = hop
        st["walk"] = S([F(sp(12)), F(sp(4), fwd=1, sy=96, fx=("dust0",)), F(sp(12), fwd=1), F(sp(4), fwd=2, sy=96)],
                       True, [(1, "sfx:scrape")])
        st["run"] = S([F(sp(4), squash=2), F(sp(5), dy=-5, sy=110, fwd=1), F(sp(5), dy=-3, fwd=2),
                       F(sp(4), squash=2, fwd=2, fx=("dust1",))], True, [(3, "sfx:clunk")])
    elif move == "heavy":
        st["walk"] = S([F(sp(12)), F(sp(10), leg=1, dy=-1, tilt=1), F(sp(6), squash=2, fx=("dust1",)),
                        F(sp(10), leg=1, dy=-1, tilt=-1)], True, [(2, "shake:1"), (2, "sfx:stomp")])
        st["run"] = S([F(sp(8), tilt=2, fwd=1), F(sp(8), leg=1, dy=-2, tilt=2, fwd=2),
                       F(sp(5), squash=3, fwd=2, fx=("dust1",)), F(sp(8), leg=1, dy=-1, tilt=2, fwd=1)], True,
                      [(2, "shake:2"), (2, "sfx:stomp")])
    elif move == "blink":  # walks normally, "runs" by blinking: dissolve out, reappear ahead
        st["walk"] = S([F(sp(8)), F(sp(8), leg=1, dy=-1), F(sp(8), squash=1), F(sp(8), leg=1, dy=-1, dx=1)], True,
                       [(0, "step"), (2, "step")])
        st["run"] = S([F(sp(4), squash=1), F(sp(4), dissolve=0.5, pal="flash"), F(sp(4), dissolve=0.85, fwd=4),
                       F(sp(4), dissolve=0.4, fwd=8, pal="flash"), F(sp(6), fwd=8)], False,
                      [(1, "sfx:blink"), (3, "spawn_fx:blink")])
    elif move == "burrow":  # walk = half sunk, a mound travels; run = fully under, only the mound shows
        st["walk"] = S([F(sp(8), clip=4), F(sp(8), clip=5, dx=1, fx=("dust0",)), F(sp(8), clip=4, dx=1),
                        F(sp(8), clip=5, fx=("dust1",))], True, [(1, "sfx:dig")])
        st["run"] = S([F(sp(5), clip=20, fwd=1, fx=("dust0",)), F(sp(5), clip=20, fwd=2, fx=("dust1",)),
                       F(sp(5), clip=20, fwd=3, fx=("dust0",)), F(sp(5), clip=20, fwd=4, fx=("dust1",))], True,
                      [(0, "sfx:dig")])
    elif move == "phase":  # walks through walls: dither flicker
        st["walk"] = S([F(sp(8), dissolve=0.3), F(sp(8), leg=1, dy=-1, dissolve=0.3, dph=1), F(sp(8), squash=1, dissolve=0.3),
                        F(sp(8), leg=1, dy=-1, dissolve=0.3, dph=1)], True, [(0, "step")])
        st["run"] = S([F(sp(5), dissolve=0.55, fwd=1, trail=1), F(sp(5), dissolve=0.55, dph=1, fwd=2, trail=2),
                       F(sp(5), dissolve=0.55, fwd=3, trail=1), F(sp(5), dissolve=0.55, dph=1, fwd=4, trail=2)], True,
                      [(0, "sfx:phase")])
    # ---- turn -----------------------------------------------------------------------------------------------------
    if move == "track":
        st["turn"] = S([F(4, look=-2), F(4, look=-1, squash=1), F(4, look=0), F(4, look=1, squash=1), F(4, look=2)],
                       False, [(0, "sfx:servo")])
    else:
        st["turn"] = S([F(4, turn=0, squash=1), F(4, turn=1, sx=86), F(6, turn=2)], False, [(1, "turned")])
    # ---- attack: tell (>= 12 frames; 24 for beams/slams), strike, recovery -------------------------------------------
    t3 = max(4, tell - 12)
    st["windup"] = S([F(6, squash=1, pal="tell", fx=("bang",)), F(6, squash=2, fwd=-1, eyes="roll", fx=("bang",)),
                      F(t3, squash=2, fwd=-2, pal="tell", eyes="roll", fx=("bang",))], False,
                     [(0, "tell"), (0, "sfx:tell")])
    st["attack"] = S([F(3, fwd=2, sy=112, mouth="O"), F(4, fwd=4, mouth="O", fx=("arc",)), F(6, fwd=3, mouth="O")],
                     False, [(0, "sfx:swing"), (1, "hit")])
    st["recover"] = S([F(8, fwd=1, squash=1), F(10, fwd=0, eyes="closed"), F(6)], False, [(2, "recovered")])
    if move == "track":
        st["attack"] = S([F(3, squash=2, pal="flash"), F(4, sy=108, mouth="O", fx=("arc",)), F(6, squash=1)], False,
                         [(0, "sfx:shot"), (1, "hit"), (1, "spawn_fx:muzzle")])
        st["recover"] = S([F(8, squash=1, pal="dim"), F(10, look=0), F(6)], False, [(2, "recovered")])
    if move == "scoot":  # the disguise opens: eyes, then teeth
        st["windup"] = S([F(6, reveal=1, tilt=1), F(6, reveal=1, open=1, pal="tell", fx=("bang",)),
                          F(t3, reveal=2, open=2, pal="tell", squash=1, fx=("bang",))], False,
                         [(0, "tell"), (1, "sfx:creak")])
        st["attack"] = S([F(3, reveal=2, open=3, fwd=3, sy=110), F(4, reveal=2, open=0, fwd=6, fx=("arc",)),
                          F(6, reveal=1, open=1, fwd=4)], False, [(1, "hit"), (1, "sfx:chomp")])
        st["recover"] = S([F(8, reveal=1, fwd=2), F(10, fwd=1, squash=1), F(6)], False, [(2, "recovered"), (2, "disguise")])
    # ---- hurt / stagger / stun / frozen / status -------------------------------------------------------------------
    st["hurt"] = S([F(4, pal="hurt", eyes="closed", fwd=-2, fx=("spark",)), F(6, fwd=-1, eyes="closed")], False,
                   [(0, "sfx:hurt"), (0, "iframes:30"), (0, "spawn_fx:spark")])
    st["stagger"] = S([F(4, tilt=-3, fwd=-3, eyes="closed", pal="flash"), F(5, tilt=2, fwd=-2),
                       F(5, tilt=-1, squash=1, fwd=-1), F(6, squash=1)], False, [(0, "stagger"), (0, "sfx:clank")])
    st["stun"] = S([F(8, tilt=1, eyes="closed", fx=("stars0",)), F(8, fx=("stars1",), squash=1),
                    F(8, tilt=-1, eyes="closed", fx=("stars2",)), F(8, fx=("stars3",), squash=1)], True,
                   [(0, "status:stun")])
    st["frozen"] = S([F(30, pal="frozen", fx=("ice0",)), F(8, pal="frozen", fx=("ice1",))], True,
                     [(0, "status:frozen"), (0, "sfx:freeze")])
    st["status"] = S([F(8, pal="status", dy=-1), F(8, twitch=1, dx=1), F(8, pal="status", dx=-1, squash=1)], True,
                     [(0, "status:tag")])
    # ---- death / spawn ----------------------------------------------------------------------------------------------
    st["death"] = S([F(4, pal="hurt", eyes="closed", sy=108), F(6, pal="death", dissolve=0.25, squash=1, eyes="closed"),
                     F(6, pal="death", dissolve=0.5, squash=2, eyes="closed", fx=("puff0",)),
                     F(8, pal="death", dissolve=0.8, dy=2, eyes="closed", fx=("puff1",)), F(8, hide=1, fx=("puff2",))],
                    False, [(0, "sfx:death"), (2, "spawn_fx:puff"), (4, "despawn")])
    if entrance == "glitch":
        st["spawn"] = S([F(6, hide=1, fx=("land",)), F(2, tear=3, dissolve=0.6, pal="flash"), F(2, tear=-2, dissolve=0.3),
                         F(2, tear=2, pal="flash"), F(4, squash=1), F(6)], False,
                        [(0, "spawn_fx:landing_outline"), (1, "sfx:glitch"), (5, "commit")])
    elif entrance == "rise":
        st["spawn"] = S([F(6, hide=1, fx=("land", "dust0")), F(4, clip=16, fx=("dust1",)), F(4, clip=8, sy=110),
                         F(3, dy=-2, sy=108), F(4, squash=1), F(6)], False,
                        [(0, "spawn_fx:landing_outline"), (1, "sfx:rise"), (5, "commit")])
    else:  # drop from above like the room cards: landing outline, fall, 2px overshoot, 1-frame squash, commit
        st["spawn"] = S([F(6, hide=1, fx=("land",)), F(4, dy=-18, dissolve=0.5, fx=("land",)), F(3, dy=-7, sy=115),
                         F(3, dy=2, squash=2), F(4, squash=1), F(6)], False,
                        [(0, "spawn_fx:landing_outline"), (3, "sfx:land"), (3, "shake:1"), (5, "commit")])
    return st


# =====================================================================================================================
# image-level part ops (shared by every type)
# =====================================================================================================================
def mix(a, b, n, d=16):
    return tuple((a[i] * (d - n) + b[i] * n) // d for i in range(3))


def lum(c):
    return (c[0] * 3 + c[1] * 6 + c[2]) // 10


def pal_img(img, mode, tint=None):
    """palette swap at image level (on the cart: a palette write, no tile change)"""
    if COUNT_MODE or mode in (None, "normal"):
        return img
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if not a:
                continue
            c = (r, g, b)
            L = lum(c)
            if mode == "hurt":
                c = c if L < 70 else mix(c, WHITE, 13)
            elif mode == "flash":
                c = mix(c, WHITE, 8) if L < 70 else WHITE
            elif mode == "tell":
                c = c if L < 70 else mix(c, (255, 236, 140), 9)
            elif mode == "death":
                c = mix(c, VOID, 8)
            elif mode == "dim":
                c = mix(c, VOID, 5)
            elif mode == "frozen":
                c = ICE[0] if L < 80 else (ICE[1] if L < 175 else ICE[2])
            elif mode == "status":
                c = c if L < 70 else mix(c, tint or (255, 120, 40), 7)
            px[x, y] = (*c, a)
    return out


def bbox(img):
    b = img.getbbox()
    return b if b else (img.width // 2, img.height - 2, img.width // 2 + 1, img.height - 1)


def scale_feet(img, sx, sy):
    """scale about the feet (bottom centre of the opaque box); nearest neighbour keeps the palette"""
    if (sx, sy) == (100, 100):
        return img
    x0, y0, x1, y1 = bbox(img)
    w, h = x1 - x0, y1 - y0
    nw, nh = max(1, (w * sx + 50) // 100), max(1, (h * sy + 50) // 100)
    part = img.crop((x0, y0, x1, y1)).resize((nw, nh), Image.NEAREST)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(part, ((x0 + x1) // 2 - nw // 2, y1 - nh), part)
    return out


def shear(img, tilt):
    """lean: the top row moves `tilt` px, the feet stay"""
    if not tilt:
        return img
    x0, y0, x1, y1 = bbox(img)
    h = max(1, y1 - y0)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    for y in range(img.height):
        s = round(tilt * (y1 - 1 - y) / h) if y < y1 else 0
        row = img.crop((0, y, img.width, y + 1))
        out.paste(row, (s, y))
    return out


def tear(img, k):
    """glitch: two bands slide sideways"""
    if not k:
        return img
    x0, y0, x1, y1 = bbox(img)
    out = img.copy()
    h = y1 - y0
    for (a, b, d) in ((y0 + h // 4, y0 + h // 4 + max(1, h // 6), k), (y0 + h * 3 // 5, y0 + h * 3 // 5 + max(1, h // 8), -k)):
        band = img.crop((0, a, img.width, b))
        out.paste((0, 0, 0, 0), (0, a, img.width, b))
        out.paste(band, (d, a))
    return out


def shift(img, dx, dy):
    if not dx and not dy:
        return img
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(img, (dx, dy))
    return out


def dissolve(img, t, ph=0):
    if not t:
        return img
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            if px[x, y][3] and BAYER[(y + ph * 2) & 3][(x + ph) & 3] < t * 16:
                px[x, y] = (0, 0, 0, 0)
    return out


def clip_ground(img, ground, k):
    """burrow: the sprite sinks k px; whatever is below the ground row is hidden"""
    if not k:
        return img
    out = shift(img, 0, k)
    out.paste((0, 0, 0, 0), (0, ground + 1, img.width, img.height))
    return out


def trail(img, n, direction):
    """afterimages: dithered copies behind the movement"""
    if not n:
        return img
    vx, vy = fwd_vec(direction)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    for k in range(n, 0, -1):
        ghost = dissolve(shift(img, -vx * 3 * k, -vy * 3 * k), 0.5 + 0.2 * k, k)
        out.alpha_composite(ghost)
    out.alpha_composite(img)
    return out


def _dot(px, w, h, x, y, c):
    if 0 <= x < w and 0 <= y < h:
        px[x, y] = (*c, 255)


def _stamp(img, rows, x, y, cols):
    px = img.load()
    for j, r in enumerate(rows):
        for i, ch in enumerate(r):
            if ch != ".":
                _dot(px, img.width, img.height, x + i, y + j, cols[ch])


FX_T = {
    "bang": ["k", "y", "y", "y", ".", "y"],
    "star": [".w.", "wyw", ".w."],
    "heart": ["pp.pp", "ppppp", ".ppp.", "..p.."],
    "anger": ["p.p.p", ".p.p.", "p.p.p"],  # a scribble (no crosses)
    "sweat": [".c", "cc", "cc"],
    "spark": ["..w..", ".wyw.", "wy.yw", ".wyw.", "..w.."],
    "zz": ["www", "..w", ".w.", "www"],
}
FXCOL = {"k": INK, "y": YEL, "w": WHITE, "p": PINK, "c": CYAN}


def fx(img, names, direction=0, ground=None, anchor=None):
    """draw overlays; anchor = the body bbox to place them around (the body before FX)"""
    if COUNT_MODE or not names:
        return img
    out = img.copy()
    W, H = out.size
    x0, y0, x1, y1 = anchor or bbox(img)
    cx = (x0 + x1) // 2
    g = ground if ground is not None else y1 - 1
    px = out.load()
    vx, vy = fwd_vec(direction)
    for nm in names:
        k = int(nm[-1]) if nm[-1].isdigit() else 0
        base = nm.rstrip("0123456789")
        if base == "bang":
            _stamp(out, FX_T["bang"], x1 + 1, max(0, y0 - 7), FXCOL)
        elif base == "stars":
            for i in range(3):
                a = (i * 2 * math.pi / 3) + k * math.pi / 6
                sx_ = cx + int(round(math.cos(a) * max(5, (x1 - x0) // 2))) - 1
                sy_ = y0 - 3 + int(round(math.sin(a) * 2)) - 1
                _stamp(out, FX_T["star"], sx_, sy_, FXCOL)
        elif base == "ice":  # an ice block: dotted rim around the body, a glint that travels
            for x in range(x0 - 1, x1 + 1):
                for y in (y0 - 1, y1):
                    if (x + y) & 1:
                        _dot(px, W, H, x, y, ICE[2])
            for y in range(y0 - 1, y1 + 1):
                for x in (x0 - 1, x1):
                    if (x + y) & 1:
                        _dot(px, W, H, x, y, ICE[2])
            gx = x0 + (x1 - x0) * (1 + 2 * k) // 4
            for d in range(3):
                _dot(px, W, H, gx + d, y0 + 1 + d, WHITE)
        elif base == "puff":
            r = 3 + 3 * k
            for y in range(g - r * 2, g + 1):
                for x in range(cx - r - 2, cx + r + 3):
                    d2 = (x - cx) ** 2 + ((y - (g - r)) * 1.2) ** 2
                    if r * r * 0.45 <= d2 <= r * r and (x * 3 + y * 5 + k) % 4 == 0:
                        _dot(px, W, H, x, y, WHITE if d2 < r * r * 0.7 else FXC["lilac"])
        elif base == "land":  # dotted landing outline on the ground (as the room cards)
            rw = max(4, (x1 - x0) // 2 + 1)
            for i in range(-rw, rw + 1, 2):
                _dot(px, W, H, cx + i, g + 1, CYAN)
            for i in range(-rw + 2, rw - 1, 2):
                _dot(px, W, H, cx + i + 1, g - 1, CYAN)
        elif base == "dust":
            bx = cx - vx * ((x1 - x0) // 2 + 2)
            for i, (ddx, ddy) in enumerate(((0, 0), (-2, -1), (2, -1), (-1, -2))[: 2 + k]):
                _dot(px, W, H, bx + ddx - vx * k, g + ddy, FXC["dust"])
        elif base == "arc":  # a swoosh in front of the facing
            if vy:
                ay = (y1 + 1) if vy > 0 else (y0 - 2)
                for i in range(-(x1 - x0) // 2 - 1, (x1 - x0) // 2 + 2):
                    _dot(px, W, H, cx + i, ay + (abs(i) * abs(i) // 24) * (-vy), WHITE if abs(i) < 3 else YEL)
            else:
                ax = (x1 + 1) if vx > 0 else (x0 - 2)
                for j in range(-(y1 - y0) // 2 - 1, (y1 - y0) // 2 + 2):
                    _dot(px, W, H, ax + (abs(j) * abs(j) // 24) * (-vx), (y0 + y1) // 2 + j, WHITE if abs(j) < 3 else YEL)
        elif base == "spark":
            _stamp(out, FX_T["spark"], cx - 2 + (vx * (x1 - x0) // 3), (y0 + y1) // 2 - 3, FXCOL)
        elif base == "heart":
            _stamp(out, FX_T["heart"], x1, max(0, y0 - 6 - k), FXCOL)
        elif base == "anger":
            _stamp(out, FX_T["anger"], x1, max(0, y0 - 4), FXCOL)
        elif base == "sweat":
            _stamp(out, FX_T["sweat"], x0 - 3, y0 + 1 + k, FXCOL)
        elif base == "zz":
            _stamp(out, FX_T["zz"], x1 + 1, max(0, y0 - 5 - k), FXCOL)
        elif base == "speed":
            for j in (y0 + (y1 - y0) // 4, (y0 + y1) // 2, y1 - (y1 - y0) // 4):
                for i in range(3, 7):
                    if vx:
                        _dot(px, W, H, (x0 - i if vx > 0 else x1 + i), j, WHITE)
                    else:
                        _dot(px, W, H, cx + (j - (y0 + y1) // 2), (y0 - i if vy > 0 else y1 + i), WHITE)
        elif base == "motes":  # ghost: rising motes
            for i in range(4):
                _dot(px, W, H, x0 + (i * 5 + k * 3) % max(1, x1 - x0 + 4) - 2, y0 - 2 - ((i * 3 + k * 4) % 8), FXC["lilac"])
    return out


def post(img, pose, direction, ground, native=False, tint=None, pal_done=False):
    """the shared image-level chain: palette, squash/stretch, lean, tear, offset, sink, dissolve, trail, FX.
    native=True: the type already applied squash and the dx/dy/fwd offsets on its own canvas."""
    anchor0 = bbox(img)
    if not pal_done:
        img = pal_img(img, pose.get("pal"), tint)
    sq = pose.get("squash", 0)
    sy = pose.get("sy", 100)
    if sq and not native:  # types without a native squash: scale the height
        h = max(1, anchor0[3] - anchor0[1])
        sy = sy * max(50, 100 - sq * 100 // max(6, h)) // 100
    img = scale_feet(img, pose.get("sx", 100), sy)
    img = shear(img, pose.get("tilt", 0))
    img = tear(img, pose.get("tear", 0))
    if not native:
        dx, dy = offsets(pose, direction)
        img = shift(img, dx, dy)
    img = clip_ground(img, ground, pose.get("clip", 0))
    img = dissolve(img, pose.get("dissolve", 0), pose.get("dph", 0))
    img = trail(img, pose.get("trail", 0), direction)
    if pose.get("hide"):
        img = Image.new("RGBA", img.size, (0, 0, 0, 0))
    anchor = bbox(img) if not pose.get("hide") else anchor0
    return fx(img, pose.get("fx", ()), direction, ground, anchor)
