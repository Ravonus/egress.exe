"""Plots of the B1 curriculum sim (host/egplan sim): the difficulty components by room index, the wave, the mixed
share by depth and generation, the stages by floor.

    build/host/egplan sim 1000 curve.csv docs/shots/b1_curve.json
    python tools/plot_curve.py curve.csv docs/shots
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

rows = list(csv.DictReader(open(sys.argv[1])))
out = Path(sys.argv[2])
for r in rows:
    for k in r:
        r[k] = int(r[k])
FLOORS = range(1, 10)
INK, PAPER = "#2b2233", "#f4ecd8"
plt.rcParams.update({"figure.facecolor": PAPER, "axes.facecolor": PAPER, "axes.edgecolor": INK, "text.color": INK,
                     "axes.labelcolor": INK, "xtick.color": INK, "ytick.color": INK, "font.size": 9})


def mean_by(key, sel=lambda r: True, by="floor"):
    acc = defaultdict(list)
    for r in rows:
        if sel(r):
            acc[r[by]].append(r[key])
    return [sum(acc[f]) / len(acc[f]) if acc[f] else 0 for f in FLOORS]


# 1: the difficulty vector by room index
comps = [("level", "difficulty level (score / 16)"), ("steps", "solution length (moves)"),
         ("branch", "branching (meaningful acts x10)"), ("dead", "dead-end share (%)"),
         ("insights", "distinct insights (trick types)"), ("nmech", "trick mechanics"),
         ("enemy", "enemy pressure (placeholder)")]
fig, axes = plt.subplots(2, 4, figsize=(13, 6))
for ax, (k, title) in zip(axes.flat, comps):
    for gen, col, lab in ((range(0, 2), "#c05a3c", "runs 1-2"), (range(2, 5), "#3c7cc0", "runs 3-5"),
                          (range(5, 8), "#3ca070", "runs 6-8")):
        ax.plot(FLOORS, mean_by(k, lambda r: r["run"] in gen), "o-", color=col, label=lab, ms=3)
    ax.set_title(title)
    ax.set_xticks(list(FLOORS))
    ax.grid(alpha=0.25)
axes.flat[0].legend()
ax = axes.flat[7]
ax.axis("off")
ax.text(0, 0.5, f"{len(rows)} rooms over\n{len({(r['player'], r['run']) for r in rows})} runs\n\nfloor index on x;\n"
        "means per generation", fontsize=10)
fig.suptitle("B1 curriculum: difficulty components by room index")
fig.tight_layout()
fig.savefig(out / "b1_curve_components.png", dpi=110)

# 2: the wave: target and chosen level per floor, landings marked
fig, ax = plt.subplots(figsize=(8, 4))
ax.plot(FLOORS, mean_by("target"), "s--", color="#888", label="target (wave + depth + generation)")
ax.plot(FLOORS, mean_by("level"), "o-", color="#c05a3c", label="chosen room's level")
for f in (3.5, 6.5):
    ax.axvline(f, color=INK, alpha=0.3)
    ax.text(f, ax.get_ylim()[1] * 0.95, " landing\n (rest)", fontsize=8)
for f, t in zip(FLOORS, ["tension", "release", "spike"] * 3):
    ax.text(f, 0.4, t, ha="center", fontsize=7, rotation=90)
ax.set_xticks(list(FLOORS))
ax.set_xlabel("floor")
ax.set_ylabel("level")
ax.legend()
ax.set_title("The wave: tension, release, spike, then a landing")
fig.tight_layout()
fig.savefig(out / "b1_curve_wave.png", dpi=110)

# 3: mixed share by depth and generation
fig, ax = plt.subplots(figsize=(8, 4))
for gen, col, lab in ((range(0, 2), "#c05a3c", "runs 1-2"), (range(2, 5), "#3c7cc0", "runs 3-5"),
                      (range(5, 8), "#3ca070", "runs 6-8")):
    share = mean_by("nmech", lambda r: r["run"] in gen)
    acc = defaultdict(lambda: [0, 0])
    for r in rows:
        if r["run"] in gen:
            acc[r["floor"]][0] += r["nmech"] >= 2
            acc[r["floor"]][1] += 1
    ax.plot(FLOORS, [100 * acc[f][0] / acc[f][1] if acc[f][1] else 0 for f in FLOORS], "o-", color=col, label=lab)
ax.set_xticks(list(FLOORS))
ax.set_ylim(0, 100)
ax.set_xlabel("floor")
ax.set_ylabel("rooms mixing 2+ trick mechanics (%)")
ax.legend()
ax.set_title("Mixed puzzles get commoner with depth and with play")
fig.tight_layout()
fig.savefig(out / "b1_curve_mixed.png", dpi=110)

# 4: stages by floor
fig, ax = plt.subplots(figsize=(8, 4))
names = ["introduce", "develop", "combine", "test"]
cols = ["#e0b040", "#3c7cc0", "#3ca070", "#c05a3c"]
bottom = [0] * 9
for s, (n, c) in enumerate(zip(names, cols)):
    vals = []
    for f in FLOORS:
        fr = [r for r in rows if r["floor"] == f]
        vals.append(100 * sum(1 for r in fr if r["stage"] == s) / len(fr) if fr else 0)
    ax.bar(list(FLOORS), vals, bottom=bottom, color=c, label=n)
    bottom = [b + v for b, v in zip(bottom, vals)]
ax.set_xticks(list(FLOORS))
ax.set_xlabel("floor")
ax.set_ylabel("% of rooms")
ax.legend(loc="upper right", fontsize=8)
ax.set_title("Curriculum stages by floor (all generations)")
fig.tight_layout()
fig.savefig(out / "b1_curve_stages.png", dpi=110)
print("wrote", [p.name for p in sorted(out.glob("b1_curve_*.png"))])
