"""Proof images: docs/shots/anim_states_contact.png (every state, every frame, one of each type, labelled) and
docs/shots/anim_states.gif (the types cycling through their states at their real 60 Hz durations).

Run from the repo root: .venv/bin/python proto/anim/sheet.py
"""
from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from PIL import Image  # noqa: E402

import core as C  # noqa: E402
from core import E, M  # noqa: E402
import registry  # noqa: E402

OUT = C.ROOT / "docs/shots"
BG = (34, 22, 66)
BG2 = (46, 30, 88)
LAB = 104


def pick():
    """one of each type: talker, ghost, a mob per mobility class, an elite, a boss (all phases), a billboard, heir"""
    mods, _ = registry.load()
    by = {m.TYPE: m for m in mods}
    out = []
    t = by["talker"].variants(2, 3)
    out.append(("TALKER (TRADER)", by["talker"], t[1]))
    out.append(("ANCESTOR GHOST", by["ghost"], by["ghost"].variants(1, 2)[0]))
    mobs = by["mob"]
    fams = {"WALKER": "OFFICE", "FLYER": "VENTS", "SWARM": "ATRIUM", "TURRET": "SERVER", "MIMIC": "MALL",
            "HEAVY": "GARDEN", "BLINKER": "GLITCH", "BURROWER": "GARDEN", "PHASER": "ROOFTOP"}
    sz = {"WALKER": 2, "FLYER": 1, "SWARM": 0, "TURRET": 2, "MIMIC": 1, "HEAVY": 3, "BLINKER": 1, "BURROWER": 1, "PHASER": 2}
    for m in mobs.MOBILITIES:
        out.append((f"MOB {m}", mobs, mobs.make(fams[m], sz[m], m, M.mix_seed(0x5E7, len(out)))))
    el = by["elite"]
    out.append(("ELITE", el, mobs.make("MALL", 1, "WALKER", M.mix_seed(0x5E8, 1), elite=True, affixes=["VAMPIRIC", "HASTED"])))
    b = by["boss"].variants(1, 0)[0]
    out.append((f"BOSS {b['name']}", by["boss"], b))
    bb = by["billboard"]
    mv = mobs.make("ROOFTOP", 2, "WALKER", M.mix_seed(0x5E9, 4))
    out.append(("BILLBOARD 24PX", bb, {"type": "billboard", "id": "BB24", "mob": mv, "height": 24, "dirs": 4}))
    if "heir" in by:
        out.append(("HEIR", by["heir"], by["heir"].variants(1, 0)[0]))
    return out


def block(title, mod, v, names=None):
    st = mod.states(v)
    names = names or list(dict.fromkeys(list(mod.REQUIRED) + list(st)))
    nd = v.get("dirs", mod.DIRS)
    rows = []
    for s in names:
        rows.append((s, [mod.render(v, s, i, 0) for i in range(len(st[s]["frames"]))], [f["dur"] for f in st[s]["frames"]]))
    first = next((n for n in names if n.split(".")[-1] == "idle"), next((n for n in st if "idle" in n), list(st)[0]))
    rows.insert(0, ("VIEWS" if nd > 1 else "VIEW", [mod.render(v, first, 0, d) for d in range(nd)], None))
    boxes = [im.getbbox() for _, ims, _ in rows for im in ims if im.getbbox()]
    x0 = max(0, min(b[0] for b in boxes) - 2)
    y0 = max(0, min(b[1] for b in boxes) - 2)
    x1 = max(b[2] for b in boxes) + 2
    y1 = max(b[3] for b in boxes) + 2
    cw, ch = x1 - x0, y1 - y0
    nmax = max(len(ims) for _, ims, _ in rows)
    rh = ch + 10
    W = LAB + nmax * (cw + 2)
    H = 12 + len(rows) * rh + 4
    im = Image.new("RGB", (W, H), BG)
    E.panel(im, 0, 0, W - 1, 10)
    E.text(im, 2, 2, title, E.WHITE)
    for r, (s, ims, durs) in enumerate(rows):
        y = 12 + r * rh
        lab = s.replace("_", " ").replace(".", " ").upper()
        E.text(im, 2, y + ch // 2 - 4, lab[:12], E.CYAN if durs else E.YEL)
        for i, f in enumerate(ims):
            x = LAB + i * (cw + 2)
            cell = Image.new("RGBA", (cw, ch), BG2 if (i + r) % 2 else (40, 26, 78))
            cell.alpha_composite(f.crop((x0, y0, x1, y1)))
            im.paste(cell.convert("RGB"), (x, y))
            if durs:
                E.text(im, x, y + ch + 1, f"{durs[i]}", (150, 116, 210))
    return im


def contact():
    blocks = []
    for title, mod, v in pick():
        if mod.TYPE == "boss":
            req = list(mod.REQUIRED)
            first = ["spawn"] + [s for s in req if s.startswith("p1.")] + ["phase_1to2"]
            second = [s for s in req if s.startswith("p2.")] + ["phase_2to3"]
            third = [s for s in req if s.startswith("p3.")] + ["death"]
            blocks += [block(title + " P1", mod, v, first), block(title + " P2", mod, v, second),
                       block(title + " P3", mod, v, third)]
        else:
            blocks.append(block(title, mod, v))
    # pack into columns of similar height
    ncol = 6
    cols = [[] for _ in range(ncol)]
    hs = [0] * ncol
    for b in sorted(blocks, key=lambda b: -b.height):
        k = hs.index(min(hs))
        cols[k].append(b)
        hs[k] += b.height + 6
    widths = [max((b.width for b in c), default=0) for c in cols]
    head = 22
    W = sum(widths) + 6 * (ncol + 1)
    H = head + max(hs) + 6
    sheet = Image.new("RGB", (W, H), E.INK)
    E.panel(sheet, 0, 0, W - 1, 17)
    E.text(sheet, 4, 5, "EGRESS.EXE  ANIMATION STATES  EVERY TYPE, EVERY STATE, EVERY FRAME (DIR S)  "
           "NUMBER = TICKS AT 60HZ  YELLOW ROW = VIEWS", E.WHITE)
    x = 6
    for c, w in zip(cols, widths):
        y = head
        for b in c:
            sheet.paste(b, (x, y))
            y += b.height + 6
        x += w + 6
    p = OUT / "anim_states_contact.png"
    sheet.resize((W * 2, H * 2), Image.NEAREST).save(p)
    return p


def gif(step=4):
    tiles = []
    for title, mod, v in pick():
        st = mod.states(v)
        names = list(dict.fromkeys(list(mod.REQUIRED) + list(st)))
        seq = []  # (state, frame index) per tick
        for s in names:
            fr = st[s]["frames"]
            n = 0
            target = max(36, sum(f["dur"] for f in fr))
            while n < target:
                for i, f in enumerate(fr):
                    seq += [(s, i)] * f["dur"]
                    n += f["dur"]
                if not st[s]["loop"]:
                    break
            seq += [(s, len(fr) - 1)] * 10
        cache = {}
        allf = []
        for s, i in set(seq):
            cache[(s, i)] = mod.render(v, s, i, 0)
            allf.append(cache[(s, i)])
        boxes = [f.getbbox() for f in allf if f.getbbox()]
        box = (min(b[0] for b in boxes) - 2, min(b[1] for b in boxes) - 2, max(b[2] for b in boxes) + 2, max(b[3] for b in boxes) + 2)
        tiles.append((title, seq, cache, box))
    tw = max(max(b[2] - b[0] for *_, b in tiles), 112)
    th = max(b[3] - b[1] for *_, b in tiles) + 22
    ncol = 5
    nrow = (len(tiles) + ncol - 1) // ncol
    total = max(len(s) for _, s, _, _ in tiles)
    frames = []
    for t in range(0, total, step):
        im = Image.new("RGB", (ncol * tw, nrow * th), E.INK)
        for k, (title, seq, cache, box) in enumerate(tiles):
            s, i = seq[t % len(seq)]
            x, y = (k % ncol) * tw, (k // ncol) * th
            cell = Image.new("RGBA", (tw, th - 22), BG if k % 2 else BG2)
            f = cache[(s, i)].crop(box)
            cell.alpha_composite(f, ((tw - f.width) // 2, th - 22 - f.height))
            im.paste(cell.convert("RGB"), (x, y + 20))
            short = title.replace("MOB ", "").replace("TALKER (TRADER)", "TRADER").replace("ANCESTOR ", "")
            short = "BOSS" if short.startswith("BOSS") else short.replace("BILLBOARD", "BBOARD")
            E.text(im, x + 2, y + 1, short[:13], E.WHITE)
            E.text(im, x + 2, y + 10, s.replace("_", " ").replace(".", " ")[:13], E.CYAN)
        frames.append(im.resize((im.width * 2, im.height * 2), Image.NEAREST).quantize(64, dither=Image.Dither.NONE))
    p = OUT / "anim_states.gif"
    frames[0].save(p, save_all=True, append_images=frames[1:], duration=round(step * 1000 / 60), loop=0, optimize=True)
    return p, len(frames)


if __name__ == "__main__":
    print(contact())
    print(gif())
