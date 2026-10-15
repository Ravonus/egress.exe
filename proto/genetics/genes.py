"""EGRESS heir genetics: one genome for looks and abilities, inheritance, mutation, selection.

A design-stage model of what the cartridge will do (integer-only, seeded LCG, same rules). It prints the genome
layout, simulates 30 generations under picking strategies, and draws the drift (charts plus a face tree from the
shared face generator).

    python3 proto/genetics/genes.py OUTDIR
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

# ---------------------------------------------------------------- the genome (14 bytes, see GENOME.md)
# Looks: the face generator's own 6-byte genome is the look genes (so a face IS its genes).
#   g0 hi nibble: skin tone 0..5 (SKIN6 ramp, light..deep) | g0 lo nibble: KIND (style) - fixed per line
#   g1 bits0-2: face shape 0..7
#   g2 bit0: mouth part | bits3-5: hair style 0..4 (bald, short, long, bun, spikes) | bits6-7: neck
#   g3 bits0-2: face pattern | bits3-5: shading dither | bits6-7: size 0..2
#   g4 bits0-2: clothes/accent colour (linked to the element affinity, see LINKS) | bits3-5: mark (freckles...)
#   g5 bits3-4: look (DREAM/BOY/GIRL)
# Abilities (diploid where it matters: two alleles a/b per locus, so recessives can skip generations):
#   dodge  2+2 bits  alleles: 0 roll (dominant), 1 slide, 2 blink (recessive)
#   weapon 3+3 bits  alleles: 0 blade 1 hammer 2 whip 3 gauntlet 4 launcher (the parent's allele expresses)
#   traits 10 loci x 2 bits (allele a, allele b): dominant traits express with one copy, recessive with two
#   affinity 7 tags x 2 bits: HOT COLD WET SHINY ALIVE MAGIC GLITCH, quantitative 0..3, expresses capped at 2
#   stats 4 x 3 bits: speed, power, guard, focus, quantitative 0..7, expressed through diminishing returns
#   flags 4 bits: mutated, glitch-born, founder, reserved
TRAITS = ["HEAVY", "SMALL", "ECHO", "GLITCHED", "INSOMNIAC", "REMEMBERS", "DAMP", "LOUD", "COLOURBLIND", "ALLERGIC"]
RECESSIVE = {"ECHO", "REMEMBERS", "COLOURBLIND", "GLITCHED"}
TAGS = ["HOT", "COLD", "WET", "SHINY", "ALIVE", "MAGIC", "GLITCH"]
STATS = ["SPD", "POW", "GRD", "FOC"]
DODGE = ["ROLL", "SLIDE", "BLINK"]
WEAPON = ["BLADE", "HAMMER", "WHIP", "GAUNTLET", "LAUNCHER"]
# linked genes (readable at a glance): HEAVY sits on the size locus, the strongest affinity tints the clothes,
# BLINK lines carry the ring-pattern, SMALL rides size 0.
CLOTH_FOR_TAG = [5, 1, 2, 6, 3, 4, 7]
SKIN_ORDER = [5, 0, 1, 2, 3, 4]  # the skin gene is a light-to-deep ramp; the face generator's SKIN6 table is not ordered  # g4 accent index per tag

# ---------------------------------------------------------------- rates (per child, out of 256)
R_SKIN_SAME, R_SKIN_STEP = 176, 80  # same tone 69%, one step either way 31% (bounded)
R_LOOK_KEEP = 200  # other look loci copy the parent 78%, else the mate
R_MUT_LOOK = 6  # 2.3% a look locus mutates to any value
R_MUT_TRAIT = 1  # 0.4% per allele: about 1 new trait every 12 children (flagged)
R_MUT_STAT = 24  # 9% a stat gene moves +/-1
STAT_CAP, AFF_CAP, TRAITS_EXPRESSED = 7, 2, 2
STAT_BUDGET = 40  # percent points shared by SPD/POW/GRD/FOC (matrix sim: 93 points made bred heirs clear 86%)


class Rng:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF or 1

    def u8(self):
        self.s = (self.s * 1103515245 + 12345) & 0xFFFFFFFF
        return (self.s >> 16) & 0xFF

    def pick(self, n):
        return self.u8() * n >> 8


def founder(rng, kind=1):
    g = {
        "skin": rng.pick(6), "kind": kind, "shape": rng.pick(8), "mouth": rng.pick(2), "hair": rng.pick(5),
        "neck": rng.pick(4), "pat": rng.pick(8), "dith": rng.pick(6), "size": rng.pick(3), "accent": rng.pick(8),
        "mark": rng.pick(8), "look": rng.pick(3),
        "dodge": [rng.pick(3), rng.pick(3)], "weapon": [rng.pick(5), rng.pick(5)],
        "traits": {t: [int(rng.u8() < 40), int(rng.u8() < 40)] for t in TRAITS},
        "aff": [rng.pick(3) if rng.u8() < 90 else 0 for _ in TAGS],
        "stats": [2 + rng.pick(3) for _ in STATS], "flags": {"mutated": 0, "glitch": 0},
    }
    return g


def mate_for(parent, rng, pool):
    """the unseen other parent: drawn from the family's pool (recessives come back through it) or a new face"""
    if pool and rng.u8() < 170:
        m = json.loads(json.dumps(pool[rng.pick(len(pool))]))
    else:
        m = founder(rng, parent["kind"])
        m["skin"] = max(0, min(5, parent["skin"] + rng.pick(3) - 1))  # partners from near the family's look
    return m


def child(parent, rng, pool):
    m = mate_for(parent, rng, pool)
    c = {"kind": parent["kind"], "flags": {"mutated": 0, "glitch": 0}}
    # skin: mostly the parent's, natural variation of one step
    r = rng.u8()
    c["skin"] = parent["skin"] if r < R_SKIN_SAME else max(0, min(5, parent["skin"] + (1 if r & 1 else -1)))
    for k, n in (("shape", 8), ("mouth", 2), ("hair", 5), ("neck", 4), ("pat", 8), ("dith", 6), ("size", 3),
                 ("accent", 8), ("mark", 8), ("look", 3)):
        c[k] = parent[k] if rng.u8() < R_LOOK_KEEP else m[k]
        if rng.u8() < R_MUT_LOOK:
            c[k] = rng.pick(n)
    # diploid loci: one allele from each parent
    c["dodge"] = [parent["dodge"][rng.pick(2)], m["dodge"][rng.pick(2)]]
    c["weapon"] = [parent["weapon"][rng.pick(2)], m["weapon"][rng.pick(2)]]
    c["traits"] = {}
    for t in TRAITS:
        a = [parent["traits"][t][rng.pick(2)], m["traits"][t][rng.pick(2)]]
        for i in range(2):
            if not a[i] and rng.u8() < R_MUT_TRAIT:
                a[i] = 1
                c["flags"]["mutated"] = 1
        c["traits"][t] = a
    c["aff"] = [min(3, max(0, (parent["aff"][i] * 3 + m["aff"][i] + 1) // 4 + (1 if rng.u8() < 20 else 0) - (1 if rng.u8() < 20 else 0))) for i in range(len(TAGS))]
    c["stats"] = []
    for i in range(len(STATS)):
        v = (parent["stats"][i] * 3 + m["stats"][i] + rng.pick(4)) // 4
        if rng.u8() < R_MUT_STAT:
            v += 1 if rng.u8() & 1 else -1
        c["stats"].append(max(0, min(STAT_CAP, v)))
    # linked genes: HEAVY needs size >= 1 (heavy heirs look big), SMALL forces size 0
    return c


def express(g):
    """phenotype: what the heir card shows and the game uses (caps and diminishing returns applied)"""
    d = g["dodge"]
    dodge = 0 if 0 in d else (1 if 1 in d else 2)  # roll > slide > blink(recessive)
    traits = [t for t in TRAITS if (sum(g["traits"][t]) == 2 if t in RECESSIVE else sum(g["traits"][t]) >= 1)]
    traits = traits[:TRAITS_EXPRESSED]  # at most 2 express; the rest stay latent
    aff = [min(AFF_CAP, a) for a in g["aff"]]
    top = max(range(len(TAGS)), key=lambda i: (aff[i], -i))
    dr = [0, 10, 18, 24, 28, 31, 33, 34]  # diminishing returns on stat genes (percent bonus)
    stats = [dr[s] for s in g["stats"]]
    tot = sum(stats)
    if tot > STAT_BUDGET:  # one shared 40-point budget across the four stats (scaled down, integer)
        stats = [v * STAT_BUDGET // tot for v in stats]
    size = 2 if "HEAVY" in traits else (0 if "SMALL" in traits else g["size"])
    accent = CLOTH_FOR_TAG[top] if aff[top] else g["accent"]
    return {"dodge": DODGE[dodge], "weapon": WEAPON[g["weapon"][0]], "traits": traits,
            "aff": {TAGS[i]: aff[i] for i in range(len(TAGS)) if aff[i]}, "top": TAGS[top] if aff[top] else None,
            "stats": dict(zip(STATS, stats)), "size": size, "accent": accent}


def face_genome(g):
    p = express(g)
    gr = g["hair"]
    if g["look"] == 1:
        gr = 1 if gr in (1, 4) else 0  # BOY picks short/spikes by parity
    g0 = (SKIN_ORDER[g["skin"]] << 4) | (g["kind"] & 15)
    g1 = g["shape"] & 7
    g2 = (g["mouth"] & 1) | ((gr & 7) << 3) | ((g["neck"] & 3) << 6)
    g3 = (g["pat"] & 7) | ((g["dith"] & 7) << 3) | ((p["size"] & 3) << 6)
    g4 = (p["accent"] & 7) | ((g["mark"] & 7) << 3)
    g5 = (g["look"] & 3) << 3
    return "%02x%02x%02x%02x%02x%02x" % (g0, g1, g2, g3, g4, g5)


# ---------------------------------------------------------------- strategies: which of 3 siblings the player picks
def score(strategy, g):
    p = express(g)
    if strategy == "random":
        return 0
    if strategy == "heavy":
        return ("HEAVY" in p["traits"]) * 10 + g["traits"]["HEAVY"].count(1)
    if strategy == "blink":
        return (p["dodge"] == "BLINK") * 10 + g["dodge"].count(2)
    if strategy == "fire":
        return g["aff"][0] * 4 + (p["top"] == "HOT")
    if strategy == "power":
        return g["stats"][1] * 2 + g["stats"][0]
    return 0


def run(strategy, seed, gens=30):
    rng = Rng(seed)
    parent = founder(rng)
    pool = []
    hist = []
    for gen in range(gens):
        sibs = [child(parent, rng, pool) for _ in range(3)]
        if strategy == "random":
            pick = sibs[rng.pick(3)]
        else:
            best = max(score(strategy, s) for s in sibs)
            cands = [s for s in sibs if score(strategy, s) == best]
            pick = cands[rng.pick(len(cands))]
        hist.append({"gen": gen + 1, "sibs": sibs, "pick": pick})
        pool = (pool + [parent])[-16:]  # the gene pool: the last 16 ancestors (the lineage ring)
        parent = pick
    return hist


def summarise(hist):
    out = []
    for h in hist:
        sibs = h["sibs"]
        ps = [express(s) for s in sibs]
        out.append({
            "gen": h["gen"],
            "heavy": sum("HEAVY" in p["traits"] for p in ps) / 3,
            "blink": sum(p["dodge"] == "BLINK" for p in ps) / 3,
            "hot": sum(s["aff"][0] for s in sibs) / 3,
            "pow": sum(s["stats"][1] for s in sibs) / 3,
            "skin": h["pick"]["skin"],
            "mutated": sum(s["flags"]["mutated"] for s in sibs),
        })
    return out


if __name__ == "__main__":
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "proto/genetics/out")
    out.mkdir(parents=True, exist_ok=True)
    res = {}
    for strat in ["random", "heavy", "blink", "fire", "power"]:
        rows = []
        for seed in range(1, 201):  # 200 lines per strategy
            rows.append(summarise(run(strat, seed * 7919)))
        avg = []
        for gi in range(30):
            avg.append({k: round(sum(r[gi][k] for r in rows) / len(rows), 3) for k in ("heavy", "blink", "hot", "pow", "mutated")})
        res[strat] = avg
    (out / "drift.json").write_text(json.dumps(res, indent=1))
    for strat, avg in res.items():
        a, b = avg[0], avg[-1]
        print(f"{strat:7s} gen1 heavy {a['heavy']:.2f} blink {a['blink']:.2f} hot {a['hot']:.2f} pow {a['pow']:.2f}"
              f"  -> gen30 heavy {b['heavy']:.2f} blink {b['blink']:.2f} hot {b['hot']:.2f} pow {b['pow']:.2f}"
              f"  mutated/gen {sum(x['mutated'] for x in avg) / 30:.2f}")


def draw(out):
    """drift charts (5 strategies x 4 measures) and a 30-generation face tree for two lines"""
    from PIL import Image, ImageDraw
    import avatar_png as AP
    AP.DUMP = str(ROOT / "build/host/avatar_dump")
    sys.path.insert(0, str(ROOT / "tools/mock"))
    from egkit import text, WHITE, CYAN, PINK, YEL, INK, void, panel
    res = json.loads((out / "drift.json").read_text())
    W, H = 5 * 128, 4 * 72 + 24
    im = Image.new("RGB", (W, H), (20, 14, 46))
    cols = {"random": (200, 200, 220), "heavy": (255, 150, 96), "blink": CYAN, "fire": (255, 90, 110), "power": YEL}
    measures = [("heavy", 1.0, "HEAVY"), ("blink", 1.0, "BLINK"), ("hot", 3.0, "HOT AFF"), ("pow", 7.0, "POWER")]
    d = ImageDraw.Draw(im)
    for mi, (k, top, label) in enumerate(measures):
        y0 = 24 + mi * 72
        text(im, 2, y0 + 2, label, WHITE)
        for si, strat in enumerate(res):
            x0 = si * 128
            if mi == 0:
                text(im, x0 + 4, 4, strat.upper(), cols[strat])
            d.rectangle((x0 + 4, y0 + 12, x0 + 123, y0 + 68), outline=(58, 36, 108))
            pts = [(x0 + 6 + i * 4, y0 + 66 - int(52 * min(1, row[k] / top))) for i, row in enumerate(res[strat])]
            d.line(pts, fill=cols[strat])
    im.resize((W * 2, H * 2), Image.NEAREST).save(out / "drift.png")
    # face trees
    for strat, seed in (("heavy", 7919 * 3), ("random", 7919 * 3)):
        hist = run(strat, seed)
        gens = [0, 1, 2, 3, 5, 8, 12, 16, 20, 25, 29]
        tree = Image.new("RGB", (len(gens) * 50 + 4, 3 * 50 + 30), (20, 14, 46))
        for col, gi in enumerate(gens):
            h = hist[gi]
            text(tree, col * 50 + 6, 2, f"G{gi + 1}", WHITE)
            gs = [face_genome(s) for s in h["sibs"]]
            outs = subprocess.run([AP.DUMP, "genome", *gs], capture_output=True, text=True, check=True).stdout.split("\n")
            outs = [o for o in outs if o.strip()]
            for r, (s, line) in enumerate(zip(h["sibs"], outs)):
                parts = line.split()
                f = AP.face_image(parts[0], [int(x, 16) for x in parts[1:5]], (46, 30, 92))
                tree.paste(f, (col * 50 + 2, 12 + r * 50))
                if s is h["pick"]:
                    ImageDraw.Draw(tree).rectangle((col * 50 + 1, 11 + r * 50, col * 50 + 50, 60 + r * 50), outline=PINK)
                p = express(s)
                if "HEAVY" in p["traits"]:
                    text(tree, col * 50 + 4, 50 + r * 50, "H", (255, 150, 96))
        text(tree, 4, 3 * 50 + 16, f"{strat.upper()} PICKS. PINK = PLAYED", CYAN)
        tree.resize((tree.width * 2, tree.height * 2), Image.NEAREST).save(out / f"tree_{strat}.png")


if __name__ == "__main__" and "--draw" in sys.argv:
    draw(Path(sys.argv[1]))
