"""Animation state harness: every type, generated variants, every REQUIRED state, every direction, every frame.

Run from the repo root:  .venv/bin/python proto/anim/test_states.py  [--quick] [--no-cost]

FAILS (exit 1) if a type's REQUIRED list does not cover the minimum state set, or if, for any variant and direction,
a required state is missing, has fewer than 2 frames, has a bad duration or event index, renders every frame
pixel-identical (a static state), or a render raises / returns something other than an RGBA image.
Coverage: mobs = every family x every allowed size x every mobility; elites = every mobility x size; billboards =
every mobility x every pre-scale height; bosses = every template (4) with all phases; talkers/ghosts/heirs =
sampled variants. Also prints an OBJ tile cost estimate per type (8x16 OBJ, X-flip dedupe, palette swaps free).
"""
from __future__ import annotations

import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import core as C  # noqa: E402
import registry  # noqa: E402
from PIL import Image  # noqa: E402

QUICK = "--quick" in sys.argv
SAMPLE = {"talker": 8, "ghost": 8, "heir": 6, "boss": 4}


def type_variants(mod):
    t = mod.TYPE
    if hasattr(mod, "coverage_variants") and not QUICK:
        return mod.coverage_variants()
    if t == "boss":
        return mod.variants(4, 0)
    return mod.variants(SAMPLE.get(t, 9 if QUICK else 12), 0)


def covers_min(req):
    missing = []
    for s in C.MIN_STATES:
        if s in req:
            continue
        if any(r.endswith("." + s) for r in req) or (s in ("spawn", "death") and s in req):
            continue
        missing.append(s)
    return missing


def check_type(mod, fails):
    t = mod.TYPE
    req = list(mod.REQUIRED)
    miss = covers_min(req)
    if miss:
        fails.append(f"{t}: REQUIRED does not cover minimum states {miss}")
    vs = type_variants(mod)
    stats = {s: {"frames": set(), "ok": 0, "bad": 0} for s in req}
    renders = 0
    dirs_seen = set()
    for v in vs:
        vid = v.get("id", "?")
        try:
            st = mod.states(v)
        except Exception as e:  # noqa: BLE001
            fails.append(f"{t} {vid}: states() raised {type(e).__name__}: {e}")
            continue
        nd = v.get("dirs", mod.DIRS)
        dirs_seen.add(nd)
        names = list(dict.fromkeys(req + list(st)))
        for s in names:
            if s not in st:
                fails.append(f"{t} {vid}: missing state {s}")
                if s in stats:
                    stats[s]["bad"] += 1
                continue
            d = st[s]
            fr = d.get("frames", [])
            if s not in stats:
                stats[s] = {"frames": set(), "ok": 0, "bad": 0}
            stats[s]["frames"].add(len(fr))
            err = None
            if len(fr) < 2:
                err = f"{len(fr)} frame(s)"
            elif any(not isinstance(f.get("dur"), int) or f["dur"] < 1 or not isinstance(f.get("pose"), dict) for f in fr):
                err = "bad frame (dur must be int >= 1, pose a dict)"
            elif any(not (0 <= i < len(fr)) or not isinstance(e, str) for i, e in d.get("events", [])):
                err = "bad event"
            elif not isinstance(d.get("loop"), bool):
                err = "loop must be bool"
            if err:
                fails.append(f"{t} {vid} {s}: {err}")
                stats[s]["bad"] += 1
                continue
            for direction in range(nd):
                try:
                    ims = []
                    for i in range(len(fr)):
                        im = mod.render(v, s, i, direction)
                        renders += 1
                        if not isinstance(im, Image.Image) or im.mode != "RGBA":
                            raise TypeError(f"render returned {type(im).__name__} {getattr(im, 'mode', '')}")
                        ims.append(im.tobytes())
                    if all(b == ims[0] for b in ims):
                        fails.append(f"{t} {vid} {s} dir {direction}: static (all {len(ims)} frames identical)")
                        stats[s]["bad"] += 1
                    elif not any(any(b) for b in ims):
                        fails.append(f"{t} {vid} {s} dir {direction}: every frame empty")
                        stats[s]["bad"] += 1
                    else:
                        stats[s]["ok"] += 1
                except Exception as e:  # noqa: BLE001
                    fails.append(f"{t} {vid} {s} dir {direction}: render raised {type(e).__name__}: {e}")
                    if "-v" in sys.argv:
                        traceback.print_exc()
                    stats[s]["bad"] += 1
    return {"variants": len(vs), "dirs": sorted(dirs_seen), "renders": renders, "stats": stats, "sample": vs[0] if vs else None}


# ---------------------------------------------------------------------------------------------------------------------
# OBJ cost estimate: unique 8x16 tiles over every state/frame of one variant (views S, E, N; W is an X-flip)
# ---------------------------------------------------------------------------------------------------------------------
def tile_cost(mod, v):
    C.COUNT_MODE = True
    try:
        st = mod.states(v)
        tiles = set()
        frames = 0
        max_obj = 0
        max_wide = 0
        nd = v.get("dirs", mod.DIRS)
        for s, d in st.items():
            for i in range(len(d["frames"])):
                for direction in range(min(nd, 3)):
                    im = mod.render(v, s, i, direction)
                    frames += 1
                    b = im.getbbox()
                    if not b:
                        continue
                    cut = im.crop(b)
                    w, h = cut.width, cut.height
                    tw, th = (w + 7) // 8, (h + 15) // 16
                    pad = Image.new("RGBA", (tw * 8, th * 16), (0, 0, 0, 0))
                    pad.paste(cut, (0, th * 16 - h))
                    n = 0
                    for ty in range(th):
                        for tx in range(tw):
                            t = pad.crop((tx * 8, ty * 16, tx * 8 + 8, ty * 16 + 16))
                            if not t.getbbox():
                                continue
                            n += 1
                            a, bflip = t.tobytes(), t.transpose(Image.FLIP_LEFT_RIGHT).tobytes()
                            tiles.add(min(a, bflip))
                    max_obj = max(max_obj, n)
                    max_wide = max(max_wide, tw)
        return {"frames": frames, "tiles": len(tiles), "bytes": len(tiles) * 32, "max_obj": max_obj, "max_wide": max_wide}
    finally:
        C.COUNT_MODE = False


def short(s):
    return {"idle_fidget": "fidg", "windup": "wind", "recover": "recv", "stagger": "stag", "frozen": "froz",
            "status": "stat", "attack": "atk", "spawn": "spwn", "death": "dth"}.get(s, s[:4])


def main():
    t0 = time.time()
    mods, imp_err = registry.load()
    fails = [f"heirs.py present but failed to import: {e}" for e in imp_err.values()]
    results = {}
    for mod in mods:
        t1 = time.time()
        results[mod.TYPE] = r = check_type(mod, fails)
        print(f"  {mod.TYPE:10s} {r['variants']:4d} variants  {r['renders']:7d} renders  {time.time() - t1:6.1f}s", flush=True)
    # ---- summary: types x minimum states (frame count, passes = variant x direction checks) -----------------------
    cols = C.MIN_STATES
    print()
    print("SUMMARY  cell = frames/state : passing (variant x direction) checks   [phase types: p1..p3 summed]")
    hdr = f"{'type':9s} {'vars':>4s} {'dirs':>4s} " + " ".join(f"{short(s):>8s}" for s in cols) + "  extras"
    print(hdr)
    print("-" * len(hdr))
    for t, r in results.items():
        cells = []
        for s in cols:
            keys = [k for k in r["stats"] if k == s or k.endswith("." + s)]
            fr = sorted({n for k in keys for n in r["stats"][k]["frames"]})
            ok = sum(r["stats"][k]["ok"] for k in keys)
            bad = sum(r["stats"][k]["bad"] for k in keys)
            frs = "/".join(str(x) for x in fr) if fr else "-"
            cells.append(f"{frs}:{ok}" + ("!" if bad else ""))
        extras = [k for k in r["stats"] if k not in cols and not any(k.endswith("." + s) for s in cols)]
        print(f"{t:9s} {r['variants']:4d} {','.join(map(str, r['dirs'])):>4s} " + " ".join(f"{c:>8s}" for c in cells)
              + "  " + " ".join(f"{e}({'/'.join(map(str, sorted(r['stats'][e]['frames'])))})" for e in extras))
    total = sum(r["renders"] for r in results.values())
    # ---- OBJ cost estimate --------------------------------------------------------------------------------------
    if "--no-cost" not in sys.argv:
        print()
        print("OBJ COST (one variant per type; COUNT_MODE: palette swaps and FX excluded, W = X-flip, offsets = OAM)")
        print(f"{'type':9s} {'variant':34s} {'frames':>6s} {'uniq8x16':>8s} {'VRAM B':>7s} {'maxOBJ':>6s} {'wide':>4s}")
        for mod in mods:
            r = results[mod.TYPE]
            samples = [r["sample"]] if r["sample"] else []
            if mod.TYPE in ("mob", "elite"):
                vs = type_variants(mod)
                seen = set()
                samples = []
                for v in vs:
                    k = (v["mobility"], v["look"]["size"])
                    if v["mobility"] not in seen and v["look"]["size"] == max(x["look"]["size"] for x in vs if x["mobility"] == v["mobility"]):
                        seen.add(v["mobility"])
                        samples.append(v)
            for v in samples:
                try:
                    c = tile_cost(mod, v)
                    print(f"{mod.TYPE:9s} {v.get('id', '?')[:34]:34s} {c['frames']:6d} {c['tiles']:8d} {c['bytes']:7d} {c['max_obj']:6d} {c['max_wide']:4d}")
                except Exception as e:  # noqa: BLE001
                    print(f"{mod.TYPE:9s} cost failed: {e}")
    print()
    print(f"{len(results)} types, {total} renders, {time.time() - t0:.1f}s")
    if fails:
        print(f"FAIL: {len(fails)} problem(s)")
        for f in fails[:60]:
            print("  " + f)
        if len(fails) > 60:
            print(f"  ... and {len(fails) - 60} more")
        sys.exit(1)
    print("PASS: every type has every required state, >= 2 frames, animated in every direction")


if __name__ == "__main__":
    main()
