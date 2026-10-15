"""The full run's plots (tools/plot_curve.py --full): host/egplan runsim's CSV, one row per slot."""
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

INK, PAPER, MUTED = "#2b2233", "#f4ecd8", "#8a8178"
# fixed categorical order (dataviz reference palette, light)
C = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
SK = ["stage", "mid-boss", "boss", "landing", "final", "egress"]
CST = ["introduce", "develop", "combine", "test"]
BIOMES = ["office", "vents", "atrium", "server", "mall", "garden", "rooftop", "core"]


def main(path, outdir):
    out = Path(outdir)
    rows = [{k: int(v) for k, v in r.items()} for r in csv.DictReader(open(path))]
    plt.rcParams.update({"figure.facecolor": PAPER, "axes.facecolor": PAPER, "axes.edgecolor": INK,
                         "text.color": INK, "axes.labelcolor": INK, "xtick.color": INK, "ytick.color": INK,
                         "font.size": 9, "axes.spines.top": False, "axes.spines.right": False})
    runs = sorted({r["run"] for r in rows})
    floors = [r for r in rows if r["kind"] != 3]
    fmax = max(r["floor"] for r in floors)
    F = list(range(1, fmax + 1))

    def mean_by(sel, key):
        acc = defaultdict(list)
        for r in floors:
            if sel(r):
                acc[r["floor"]].append(r[key])
        return [sum(acc[f]) / len(acc[f]) if acc[f] else None for f in F]

    # 1: the wave over the whole run
    fig, axes = plt.subplots(2, 1, figsize=(13, 8), gridspec_kw={"height_ratios": [3, 2]})
    ax = axes[0]
    # biome bands: the mean first floor of each position
    starts = defaultdict(list)
    for run in runs:
        seen = set()
        for r in floors:
            if r["run"] == run and r["bpos"] not in seen:
                seen.add(r["bpos"])
                starts[r["bpos"]].append(r["floor"])
    edges = [sum(starts[p]) / len(starts[p]) - 0.5 for p in range(8)] + [fmax + 0.5]
    for p in range(8):
        if p % 2:
            ax.axvspan(edges[p], edges[p + 1], color=INK, alpha=0.05, lw=0)
        ax.text((edges[p] + edges[p + 1]) / 2, 15.6, f"biome {p + 1}", ha="center", fontsize=8, color=MUTED)
    fights = (0, 1, 2, 4)
    for gi, (lo, hi) in enumerate(((0, 10), (10, 20), (20, 30))):
        sel = lambda r, lo=lo, hi=hi: lo <= r["gen"] < hi and r["kind"] in fights
        ax.plot(F, mean_by(sel, "target"), "-", color=C[gi], lw=2,
                label=f"planned level, generations {lo + 1}-{hi} (stages and fights)")
    mid = lambda r: 10 <= r["gen"] < 20 and r["kind"] == 0
    ax.plot(F, [v for v in mean_by(mid, "floor_min")], ":", color=INK, lw=1.5,
            label="curriculum floor, gens 11-20 (no nudge goes under it)")
    ax.plot(F, mean_by(mid, "level"), "-", color=C[7], lw=1, alpha=0.9,
            label="stage level played after the capped nudge, gens 11-20")
    for k, mk, lab in ((1, "^", "mid-boss (gens 11-20)"), (2, "v", "biome boss"), (4, "*", "final stretch")):
        xs = defaultdict(list)
        for r in floors:
            if r["kind"] == k and 10 <= r["gen"] < 20:
                xs[r["floor"]].append(r["target"])
        ax.scatter(list(xs), [sum(v) / len(v) for v in xs.values()], marker=mk, s=26, color=INK, zorder=3,
                   label=lab)
    # one run's actual shape
    ex = [r for r in floors if r["run"] == runs[17] and r["kind"] in (0, 1, 2, 4)]
    ax.plot([r["floor"] for r in ex], [r["target"] for r in ex], "-", color=MUTED, lw=0.8, alpha=0.8,
            label=f"one run (generation {ex[0]['gen'] + 1}): tension, release, spike, fight")
    ax.set_xlim(0.5, fmax + 0.5)
    ax.set_ylim(0, 16.5)
    ax.set_ylabel("level (0-15)")
    ax.legend(loc="lower right", fontsize=7.2, ncol=2, frameon=True, facecolor=PAPER, edgecolor=PAPER)
    ax.set_title(f"The full run: the wave over ~52 floors ({len(runs)} runs; biome bands at their mean floor)")
    ax.grid(axis="y", alpha=0.2)
    # stages by floor
    ax = axes[1]
    bottom = [0.0] * len(F)
    cats = [("introduce", lambda r: r["kind"] == 0 and r["cstage"] == 0, C[3]),
            ("develop", lambda r: r["kind"] == 0 and r["cstage"] == 1 and r["stype"] < 8, C[0]),
            ("combine", lambda r: r["kind"] == 0 and r["cstage"] == 2 and r["stype"] < 8, C[2]),
            ("test", lambda r: r["kind"] == 0 and r["cstage"] == 3, C[1]),
            ("rare (3D, rift, sky)", lambda r: r["kind"] == 0 and r["stype"] >= 8, C[6]),
            ("mid-boss / boss / final", lambda r: r["kind"] in (1, 2, 4), INK),
            ("egress", lambda r: r["kind"] == 5, C[4])]
    cnt = defaultdict(int)
    for r in floors:
        cnt[r["floor"]] += 1
    for name, sel, col in cats:
        acc = defaultdict(int)
        for r in floors:
            if sel(r):
                acc[r["floor"]] += 1
        vals = [100 * acc[f] / len(runs) for f in F]
        ax.bar(F, vals, bottom=bottom, color=col, label=name, width=0.85, edgecolor=PAPER, linewidth=0.6)
        bottom = [b + v for b, v in zip(bottom, vals)]
    ax.set_xlim(0.5, fmax + 0.5)
    ax.set_ylabel("% of runs with that floor")
    ax.set_xlabel("floor")
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.2), fontsize=7.5, ncol=7, frameon=False)
    ax.set_title("What each floor is (runs end between 50 and 56 floors, so the last bars thin out)")
    fig.tight_layout()
    fig.savefig(out / "run50_wave.png", dpi=110)

    print("wrote run50_wave.png")
