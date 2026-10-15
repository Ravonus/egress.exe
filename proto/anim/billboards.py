"""Billboards for the first-person raycaster (redesign section 15): every mob state, pre-scaled to a pixel height.

Views are camera-relative and VIEW-FACING: 0 front (the mob faces the camera), 1 side (facing the camera's right),
2 back, 3 side mirrored (free: the raycaster draws the column order reversed). So: front + side + back drawn,
the other side mirrored. Turrets are symmetric and use the front for every view.

Frames are cut from the 1x mob render with a fixed window per variant (bottom-anchored on the ground row, so a frame
never jitters when its content changes), then scaled to `height_px` square:
  - down-scaling keeps the palette (the most common opaque colour per source block, no blending), so a pre-scaled
    frame still uses the mob's 3+3 colours;
  - offsets (bob, lunge, recoil, spawn drop) are applied AFTER scaling, at least 1 px each, so small far-away
    billboards still animate.

API for the raycaster prototype:
    billboard_frame(mob_variant, state, frame_index, view, height_px) -> PIL RGBA, height_px x height_px
    prescale(mob_variant, heights=HEIGHTS) -> {height: {state: [[frame images] per view]}}
    view_for(mob_facing_deg, mob_to_camera_deg) -> view index 0..3
"""
from __future__ import annotations

import math
from collections import Counter

from PIL import Image

import core as C
from core import M
import mobs

TYPE = "billboard"
DIRS = 4
REQUIRED = list(C.MIN_STATES)
HEIGHTS = (8, 12, 16, 24, 32, 48)


def variants(n, seed=0):
    base = mobs.variants(n, seed + 7)
    out = []
    for i, mv in enumerate(base):
        if i % 4 == 3:
            mv = mobs.make(mv["look"]["fam"], mv["look"]["size"], mv["mobility"], M.mix_seed(0xB111, seed, i), elite=True)
        h = HEIGHTS[(i + seed) % len(HEIGHTS)]
        out.append({"type": TYPE, "id": f"BB{h}-{mv['id']}", "mob": mv, "height": h, "dirs": 4})
    return out


def coverage_variants(seed=0):
    """every mobility at every pre-scale height"""
    out = []
    for mi, m in enumerate(mobs.MOBILITIES):
        szs = mobs.sizes_for(m)
        for hi, h in enumerate(HEIGHTS):
            mv = mobs.make(M.FAMILIES[(mi + hi) % 8], szs[hi % len(szs)], m, M.mix_seed(0xB112, seed, mi, hi),
                           elite=(hi == 3))
            out.append({"type": TYPE, "id": f"BB{h}-{mv['id']}", "mob": mv, "height": h, "dirs": 4})
    return out


def states(v):
    return mobs.states(v["mob"])


def window(mv):
    """the fixed cut window (x0, y0, size) in the 64x64 mob canvas"""
    lk = mv["look"]
    s = M.SIZES[min(3, lk["size"] + (1 if lk["affixes"] else 0))]
    if mv["mobility"] == "MIMIC":
        s = 16
    w = max(16, (s * 3 + 1) // 2)
    if mv["mobility"] == "SWARM":
        w = 44
    bottom = mobs.GROUND + 4
    return 32 - w // 2, bottom - w, w


def _down(img, size):
    """palette-keeping down/up scale: the mode of each source block's opaque pixels (alpha if under half)"""
    w = img.width
    if size >= w:
        return img.resize((size, size), Image.NEAREST)
    px = img.load()
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    po = out.load()
    for ty in range(size):
        for tx in range(size):
            xa, xb = tx * w // size, max(tx * w // size + 1, (tx + 1) * w // size)
            ya, yb = ty * w // size, max(ty * w // size + 1, (ty + 1) * w // size)
            cs = [px[x, y] for y in range(ya, yb) for x in range(xa, xb)]
            op = [c for c in cs if c[3]]
            if len(op) * 2 >= len(cs):
                po[tx, ty] = Counter(op).most_common(1)[0][0]
    return out


def _squash1(img, d):
    """d = 1: the top half moves down 1 px (squash); d = -1: it moves up 1 px (stretch)"""
    b = img.getbbox()
    if not b:
        return img
    mid = (b[1] + b[3]) // 2
    top = img.crop((0, 0, img.width, mid))
    out = img.copy()
    out.paste((0, 0, 0, 0), (0, 0, img.width, mid))
    if d < 0:
        out.paste(top, (0, -1), top)
        out.paste(img.crop((0, mid, img.width, mid + 1)), (0, mid - 1))
    else:
        out.paste(top, (0, 1), top)
    return out


def _min1(o, k):
    """scaled offset, never below half scale and at least 1 px, so far billboards keep their motion"""
    if not o:
        return 0
    return int(math.copysign(max(1, math.ceil(abs(o) * max(k, 0.5))), o))


def billboard_frame(mv, state, frame_index, view, height_px):
    st = mobs.states(mv)[state]
    pose = st["frames"][frame_index]["pose"]
    dx, dy = C.offsets(pose, view)
    x0, y0, w = window(mv)
    k = height_px / w
    stripped = {key: val for key, val in pose.items() if key not in ("dx", "dy", "fwd", "fx")}
    img = _render_pose(mv, state, frame_index, view, stripped)
    cut = img.crop((x0, y0, x0 + w, y0 + w))
    out = _down(cut, height_px)
    sq = pose.get("squash", 0)
    sy = pose.get("sy", 100)
    if k < 0.75 and (sq or sy != 100):  # a far billboard still squashes/stretches by a whole pixel
        out = _squash1(out, 1 if (sq or sy < 100) else -1)
    oy = _min1(dy, k)
    ox = _min1(dx, k)
    if ox or oy:
        out = C.shift(out, ox, oy)
    # FX are separate sprites in the raycaster: drawn at the billboard's own resolution, never scaled away
    b = out.getbbox()
    anchor = b if b and not pose.get("hide") else (height_px // 4, height_px // 2, height_px * 3 // 4, height_px - 1)
    return C.fx(out, pose.get("fx", ()), view, min(height_px - 1, int((mobs.GROUND - y0) * k)), anchor)


def _render_pose(mv, state, frame_index, view, pose):
    if mv["dirs"] == 1:
        view = 0
    if mv["mobility"] == "MIMIC":
        return mobs.render_mimic(mv, pose, view, frame_index % 2)
    if mv["mobility"] == "SWARM":
        return mobs.render_swarm(mv, state, frame_index, view)
    return mobs.render_single(mv["look"], pose, view, frame_index % 2, M.ELEMENT[mv["status_tag"]][1])


def prescale(mv, heights=HEIGHTS):
    st = mobs.states(mv)
    return {h: {s: [[billboard_frame(mv, s, i, d, h) for i in range(len(st[s]["frames"]))] for d in range(4)]
                for s in st} for h in heights}


def view_for(mob_facing_deg, mob_to_camera_deg):
    """0 front, 1 side (right), 2 back, 3 side mirrored"""
    rel = (mob_to_camera_deg - mob_facing_deg) % 360
    return [0, 1, 2, 3][int(((rel + 45) % 360) // 90)]


def render(v, state, frame_index, direction):
    return billboard_frame(v["mob"], state, frame_index, direction, v["height"])
