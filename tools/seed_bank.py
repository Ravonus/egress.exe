"""The seed bank estimate for the full run (docs/redesign.md 3.6.9), from the survival model's demand.

host/egplan survive records, for every cell the planner draws a room from (lead mechanic x mix size x curriculum
class x stage type x tier), how many times a line plays it over its first 30 generations (mean and mean square over lines). A cell
holding n rooms, played v times by a line, repeats about v^2 / 2n of them (the birthday bound, an upper bound). The
pool that meets a repeat target r for the least rooms gives each cell n_c ~ sqrt(E[v^2]) (Lagrange), with a floor of
N_MIN rooms per cell so no run ever repeats inside itself.

    python tools/seed_bank.py docs/shots/run50_survival.json docs/shots/run50_seed_bank.json
"""
import json
import math
import sys

SG = ["puzzle", "holdout", "chase", "hunt", "escort", "mixp", "dark", "heist", "3d", "rift", "sky"]
ROOM_TYPES = {0, 5, 6}         # puzzle, mix under pressure, dark: rooms from the backwards generator (3.2)
STAGE_TYPES = {1, 2, 3, 4, 7}  # holdout, chase, hunt, escort, heist: the stage generator + stage sim (4)
ONCART = {8, 9, 10}            # 3D mazes, rifts, sky stages: generated on the cart (3.6.9)
N_MIN = 4
REC_ROOM, REC_STAGE = 6, 5     # bytes per baked record (3.6.9)
VARIANTS = 1.8                 # verified transforms per baked seed (mirror, herring swap): the bake keeps the mask
BIOME_SHARE = 0.75             # a baked room plays in this share of biomes (its fragile kinds map; 3.6.9)


def plan(cells, r):
    S = sum(math.sqrt(v2) for _, _, v2 in cells)
    V = sum(v for _, v, _ in cells)
    k = S / (2 * r * V) if V else 0
    ns = [max(N_MIN, math.ceil(math.sqrt(v2) * k)) for _, _, v2 in cells]
    rep = sum(v2 / (2 * n) for (_, _, v2), n in zip(cells, ns)) / V if V else 0
    return sum(ns), rep, ns


def main(src, dst):
    d = json.load(open(src))
    out = {"source": src, "lines": d["lines"], "targets": {}}
    groups = {"rooms": [], "stages": [], "on_cart": []}
    for key, v, v2 in d["cells"]:
        st = (key >> 4) & 15
        g = "rooms" if st in ROOM_TYPES else "stages" if st in STAGE_TYPES else "on_cart"
        groups[g].append((key, v, v2))
    for r in (0.10, 0.05):
        row = {}
        for g in ("rooms", "stages"):
            n, rep, _ = plan(groups[g], r)
            phys = math.ceil(n / VARIANTS / BIOME_SHARE) if g == "rooms" else math.ceil(n / BIOME_SHARE)
            rec = REC_ROOM if g == "rooms" else REC_STAGE
            row[g] = {"cells": len(groups[g]), "plays_per_line_30_gens": round(sum(v for _, v, _ in groups[g]), 1),
                      "effective_pool": n, "baked_records": phys, "bytes": phys * rec,
                      "repeat_share_upper_bound": round(rep, 4)}
        row["total_bytes"] = row["rooms"]["bytes"] + row["stages"]["bytes"]
        out["targets"][f"repeat_{int(r * 100)}pct"] = row
    out["on_cart_plays_per_line"] = round(sum(v for _, v, _ in groups["on_cart"]), 1)
    # by tier, for the doc's table (rooms, 10% target)
    n, _, ns = plan(groups["rooms"], 0.10)
    tiers = [0] * 6
    for (key, _, _), c in zip(groups["rooms"], ns):
        tiers[key & 15] += c
    out["rooms_effective_by_tier_10pct"] = tiers
    cls = {1: 0, 2: 0, 3: 0}
    for (key, _, _), c in zip(groups["rooms"], ns):
        cls[(key >> 8) & 3] += c
    out["rooms_effective_by_class_10pct"] = {"introduce": cls[1], "develop": cls[2], "mix": cls[3]}
    json.dump(out, open(dst, "w"), indent=1)
    print(json.dumps(out, indent=1))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
