"""Heir sprite styles animated side by side (design mock, not game code).

The same two heirs (CORA, OREN) in every style, on the same room floor, playing one synced timeline from the heir
state table (proto/anim/heirs.py): idle with a fidget, a walk east then back west, the heir's own dodge, and a
3-hit attack (swing, thrust, spin). Every frame is rendered by sprite_styles.render, so it is the same rig, layers
and colour rules as the stills.

    .venv/bin/python tools/mock/sprite_styles_anim.py       # A, B, C -> docs/shots/sprite_styles_anim_compare.gif
    .venv/bin/python tools/mock/sprite_styles_anim.py --d   # A, D1, D2, D3 -> docs/shots/sprite_styles_D_anim.gif

The D round puts A, unchanged, next to its three refinements so the owner sees exactly what each one adds.
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sprite_styles as SS  # noqa: E402
from egkit import CYAN, INK, PINK, ROOT, YEL, put, slab, text  # noqa: E402

sys.path.insert(0, str(ROOT / "proto/anim"))
import heirs as HM  # noqa: E402

D_MODE = "--d" in sys.argv
OUT = ROOT / ("docs/shots/sprite_styles_D_anim.gif" if D_MODE else "docs/shots/sprite_styles_anim_compare.gif")
STYLES = ["A"] + SS.D_STYLES if D_MODE else ["A", "B", "C"]
CW, CH = 56, 48  # one floor cell per style and heir
STEP = 3  # ticks (60 Hz) per GIF frame -> 20 fps
SCALE = 3


def timeline(v):
    """[(state, frame index, direction, x offset)] per tick for one heir"""
    T = HM.states(v)
    seq = []

    def play(name, d, dx_per_tick=0.0, ticks=None, loop=False):
        fr = T[name]["frames"]
        total = ticks or sum(f["dur"] for f in fr)
        t = i = 0
        x0 = seq[-1][3] if seq else 0.0
        while t < total:
            f = fr[i % len(fr)]
            for _ in range(f["dur"]):
                if t >= total:
                    break
                seq.append((name, i % len(fr), d, x0 + dx_per_tick * (t + 1)))
                t += 1
            i += 1
            if not loop and i >= len(fr):
                break

    play("idle", 0, ticks=60, loop=True)
    play("idle_fidget", 0)
    play("walk", 2, 0.3, ticks=48, loop=True)  # east
    play("walk", 6, -0.3, ticks=48, loop=True)  # back west
    play("idle", 1, ticks=12, loop=True)
    play("dodge", 2, 0.0)  # the heir's own dodge (dodge gene)
    play("idle", 1, ticks=10, loop=True)
    play("windup", 1)
    play("attack", 1)  # hit 1: swing
    play("lunge", 1)  # hit 2: thrust
    play("spin", 1)  # hit 3: spin
    play("recover", 1)
    play("idle", 0, ticks=20, loop=True)
    return seq


def floor(seed):
    im = Image.new("RGB", (CW, CH), INK)
    for r in range(0, CH, 16):
        for c in range(0, CW, 16):
            put(im, slab("lilac", seed + r * 7 + c), c, r)
    return im


def main() -> None:
    hs = {h["name"]: h for h in SS.four_heirs()}
    heirs = [HM.variant_from_heir(hs["CORA"]), HM.variant_from_heir(hs["OREN"])]
    seqs = [timeline(v) for v in heirs]
    n = max(len(s) for s in seqs)
    seqs = [s + [s[-1]] * (n - len(s)) for s in seqs]
    floors = [[floor(17 * i + 3 * j) for j in range(len(STYLES))] for i in range(len(heirs))]
    W = len(STYLES) * (CW + 2) + 2
    TOP = 12  # header: style name + size (at most 7 glyphs fit a cell)
    H = TOP + len(heirs) * (CH + 2) + 10
    cache = {}
    frames = []
    for t in range(0, n, STEP):
        im = Image.new("RGB", (W, H), (20, 14, 46))
        for j, st in enumerate(STYLES):
            text(im, 2 + j * (CW + 2) + 2, 2, st + (" X%d" % SS.STYLES[st]["H"]),
                 PINK if st.startswith("D") else CYAN)
        for i, v in enumerate(heirs):
            name, k, d, x = seqs[i][t]
            for j, st in enumerate(STYLES):
                ox, oy = 2 + j * (CW + 2), TOP + i * (CH + 2)
                im.paste(floors[i][j], (ox, oy))
                key = (i, st, name, k, d)
                if key not in cache:
                    pose = dict(HM.states(v)[name]["frames"][k]["pose"])
                    if pose.pop("face_cam", False):
                        pose["dir"] = (-d) % 8
                    cache[key] = SS.render(st, v, d, pose)
                spr = cache[key]
                S = SS.STYLES[st]
                feet = oy + CH - 8
                put(im, spr, ox + CW // 2 - 8 - SS.M - 7 + int(round(x)), feet - S["H"] - SS.M)
            text(im, 2, TOP + i * (CH + 2) + 2, v["name"], YEL)
        lab = {"idle": "IDLE", "idle_fidget": "FIDGET", "walk": "WALK", "dodge": "DODGE", "windup": "WINDUP",
               "attack": "HIT 1", "lunge": "HIT 2", "spin": "HIT 3", "recover": "RECOVER"}.get(seqs[0][t][0], "")
        text(im, 2, H - 9, lab, YEL)
        frames.append(im.resize((W * SCALE, H * SCALE), Image.NEAREST))
    frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=round(1000 * STEP / 60), loop=0,
                   optimize=True)
    print(OUT, len(frames), "frames,", round(n / 60, 2), "s,", frames[0].size)
    for st in STYLES:
        _, c = SS.render(st, heirs[0], 1, dict(HM.states(heirs[0])["attack"]["frames"][1]["pose"]), info=True)
        print(st, "sprites", c["sprites"], "per line", c["per_line"])


if __name__ == "__main__":
    main()
