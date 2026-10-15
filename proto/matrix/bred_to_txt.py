"""Flatten proto/genetics/out/bred_lines.json into bred_lines.txt for the C sim (one heir per line).
Columns: strategy weapon dodge trait0 trait1 HOT COLD WET SHINY ALIVE MAGIC GLITCH SPD POW GRD FOC"""
import json, sys, pathlib
src = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "../genetics/out/bred_lines.json")
dst = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "bred_lines.txt")
tags = ["HOT", "COLD", "WET", "SHINY", "ALIVE", "MAGIC", "GLITCH"]
rows = []
for strat, lines in json.loads(src.read_text()).items():
    for h in lines:
        t = (h["traits"] + ["-", "-"])[:2]
        rows.append(" ".join([strat, h["weapon"], h["dodge"], t[0], t[1]] + [str(h["aff"].get(k, 0)) for k in tags] +
                             [str(h["stats"][k]) for k in ("SPD", "POW", "GRD", "FOC")]))
dst.write_text("\n".join(rows) + "\n")
print(len(rows), "heirs ->", dst)
