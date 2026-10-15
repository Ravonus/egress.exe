"""The survival model's plots (host/egplan survive, docs/redesign.md 3.6.7).

    build/host/egplan survive 4000 40 docs/shots/run50_survival.json
    python tools/plot_survival.py docs/shots/run50_survival.json docs/shots
"""
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

INK, PAPER, MUTED = "#2b2233", "#f4ecd8", "#8a8178"
SEQ = ["#b9d3f2", "#7fb0e8", "#3f86d9", "#1f5fb0", "#0d3570"]  # one hue, light -> dark: generation is ordinal
C = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
GENS = [1, 5, 10, 20, 30]


def main(path, outdir):
    d = json.load(open(path))
    out = Path(outdir)
    plt.rcParams.update({"figure.facecolor": PAPER, "axes.facecolor": PAPER, "axes.edgecolor": INK,
                         "text.color": INK, "axes.labelcolor": INK, "xtick.color": INK, "ytick.color": INK,
                         "font.size": 9, "axes.spines.top": False, "axes.spines.right": False})
    R = d["reach"]
    G = d["generations"]

    def at(g, b):  # % of generation g's runs that reached biome position b (8: the egress)
        row = R[g]
        return 100 * sum(row[b:]) / sum(row)

    fig, axes = plt.subplots(1, 2, figsize=(13, 4.8))
    ax = axes[0]
    xs = list(range(1, 10))
    for gi, g in enumerate(GENS):
        if g > G:
            continue
        ys = [at(g - 1, b) for b in range(9)]
        ax.plot(xs, ys, "o-", color=SEQ[gi], lw=2, ms=5, label=f"generation {g}")
        ax.annotate(f"{ys[-1]:.1f}%", (9, ys[-1]), xytext=(6, 0), textcoords="offset points", va="center",
                    fontsize=8, color=INK)
    ax.set_xticks(xs, [f"biome {b}" for b in range(1, 9)] + ["EGRESS"], rotation=30, ha="right")
    ax.set_ylabel("% of that generation's runs reaching it")
    ax.set_ylim(0, 103)
    ax.set_xlim(0.7, 9.8)
    ax.grid(axis="y", alpha=0.2)
    ax.legend(frameon=False, fontsize=8, loc="upper right")
    ax.set_title(f"Survival by biome, skilled player ({d['lines']} lines; shortcut starts count as reached)")

    ax = axes[1]
    n = [sum(R[g]) for g in range(G)]
    eg = [100 * d["egress"][g] / n[g] for g in range(G)]
    fd = [100 * d["front_door_clear"][g] / n[g] for g in range(G)]

    def smooth(v, k=2):
        return [sum(v[max(0, i - k):i + k + 1]) / len(v[max(0, i - k):i + k + 1]) for i in range(len(v))]

    gx = list(range(1, G + 1))
    ax.plot(gx, smooth(eg), "-", color=C[0], lw=2, label="egress per run, any door (5-gen mean)")
    s = d["strategy_egress"]
    ax.plot(gx, smooth([100 * v for v in s["bred"]]), ":", color=C[2], lw=1.5,
            label="egress, best bred strategy (9.4's +3.7 pt gap)")
    ax.plot(gx, smooth([100 * v for v in s["random"]]), ":", color=C[6], lw=1.5, label="egress, random picks")
    fe = d["first_egress"]
    cum, c = [], 0
    for g in range(G):
        c += fe[g]
        cum.append(100 * c / d["lines"])
    ax2 = ax.inset_axes([0.08, 0.5, 0.36, 0.42])
    ax2.set_facecolor(PAPER)
    ax2.plot(gx, cum, "-", color=INK, lw=1.5)
    ax2.set_title("lines with an egress so far (%)", fontsize=7.5)
    ax2.tick_params(labelsize=7)
    ax2.set_ylim(0, 100)
    ax2.grid(alpha=0.2)
    ax.set_xlabel("generation (one heir, one run)")
    ax.set_ylabel("% of runs")
    ax.set_ylim(0, max(12, max(smooth(eg)) * 1.2))
    ax.grid(axis="y", alpha=0.2)
    ax.legend(frameon=False, fontsize=7.5, loc="upper right")
    ax.set_title("Full-clear rate per run by generation")
    fig.tight_layout()
    fig.savefig(out / "run50_survival.png", dpi=110)

    # the doors taken
    fig, ax = plt.subplots(figsize=(8, 3.6))
    D = d["door"]
    bottom = [0.0] * G
    for p in range(8):
        vals = [100 * D[g][p] / n[g] for g in range(G)]
        if max(vals) < 0.5:
            continue
        ax.bar(gx, vals, bottom=bottom, color=C[p], width=0.85, edgecolor=PAPER, linewidth=0.5,
               label="front door" if p == 0 else f"door {p + 1}")
        bottom = [b + v for b, v in zip(bottom, vals)]
    ax.set_xlabel("generation")
    ax.set_ylabel("% of runs")
    ax.set_ylim(0, 100)
    ax.legend(frameon=False, fontsize=7.5, ncol=4, loc="lower left")
    ax.set_title("Where heirs start: lineage shortcut doors (door k = the entrance of biome k)")
    fig.tight_layout()
    fig.savefig(out / "run50_doors.png", dpi=110)
    # what heirs actually played: introductions and mixes by biome position, by generation group
    fig, axes = plt.subplots(1, 2, figsize=(12, 4))
    groups = ["generations 1-3", "4-8", "9-15", "16-30", "31-40"]
    st, mx, it, runs = d["played_stages"], d["played_mixed"], d["played_intros"], d["played_runs_by_group"]
    for gi in range(5):
        ok = [b for b in range(8) if st[gi][b] >= 200]
        axes[0].plot([b + 1 for b in ok], [it[gi][b] / runs[gi] for b in ok], "o-", color=C[gi], ms=5,
                     label=groups[gi])
        axes[1].plot([b + 1 for b in ok], [100 * mx[gi][b] / st[gi][b] for b in ok], "o-", color=C[gi], ms=5,
                     label=groups[gi])
    axes[0].set_title("New mechanics keep arriving: introductions per run, where heirs play", fontsize=10)
    axes[0].set_ylabel("introductions per run")
    axes[1].set_title("Late biomes mostly combine (stages heirs played)", fontsize=10)
    axes[1].set_ylabel("stages mixing 2+ mechanics (%)")
    axes[1].set_ylim(0, 100)
    for ax in axes:
        ax.set_xlabel("biome position")
        ax.set_xticks(range(1, 9))
        ax.grid(alpha=0.2)
        ax.legend(fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(out / "run50_mix.png", dpi=110)
    print("wrote run50_survival.png run50_doors.png run50_mix.png")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
