"""Native integration checks for the recovered B2 branches.

Run: python test/harness/stage2.py build/egress.gbc OUT
Checks real stage entry, matrix-mob waves, exact returned-orb ownership, and
four-way crate pushing through the cartridge's input and collision loop.
"""
import argparse
import json
from pathlib import Path

from play import Game, MODE_TITLE, start_run
from heir import OamMeter


def fresh(rom, stage, family=None, enemies=False):
    g = Game(rom)
    assert g.wait_mode(MODE_TITLE, 1500)
    g.poke("dbg_stage", stage + 1)
    if family is not None:
        g.poke("dbg_mob_n", 1)
        g.poke("dbg_mob", family + 1)
        g.poke("dbg_mob_cell", 8 * 16 + 8)
    assert start_run(g, True, not (enemies or family is not None), seed=0xB222, floor=2)
    return g


def teleport(g, x, y):
    g.poke16("psx", x << 4)
    g.poke16("psy", y << 4)
    g.poke("pl_x", x)
    g.poke("pl_y", y)
    g.poke("pl_inv", 255)


def returned_orb(g, shooter):
    e, o = g.addr("en"), g.addr("orbs")
    m = g.pb.memory
    for i in range(10):
        m[o + i] = 0
    m[o], m[o + 1], m[o + 2], m[o + 3] = 1, 1, 255, shooter + 1
    for i in range(4):
        m[o + 4 + i] = m[e + 8 + i]
    g.tick(12)


def run(rom, out):
    out.mkdir(parents=True, exist_ok=True)
    report, result = {}, {}
    for stage, name in enumerate(["puzzle", "holdout", "chase", "hunt", "escort", "mixp", "dark", "heist"]):
        if stage == 0:
            continue
        g = fresh(rom, stage)
        meter = OamMeter(g)
        g.tick(120)
        g.shot(out / f"b2_stage_{name}.png")
        goal = bytes(g.peek("stage_goal", i) for i in range(19)).split(b"\0")[0].decode("ascii")
        result[f"stage_{name}"] = g.peek("stage_type") == stage and bool(goal) and len(goal) <= 18
        if stage == 3:
            k = g.peek("stage_cell")
            result["hunt_uses_matrix_mob"] = k < 8 and g.peek("en", k * 16) == 3
        result[f"stage_{name}_oam"] = meter.over == 0
        report[name] = {"goal": goal, "oam_max": meter.max_line, "over": meter.over}
        g.stop()

    g = fresh(rom, 1, 2)  # the actual LAMP: only its own returned orb can hurt it
    g.poke("stage_type", 0)  # isolate this spawn from later holdout waves
    teleport(g, 40, 168)
    e = g.addr("en")
    g.pb.memory[e + 6] = 255
    hp = g.pb.memory[e + 3]
    returned_orb(g, 1)
    result["foreign_orb_blocked"] = g.pb.memory[e + 3] == hp and g.peek("hit_result") == 1
    returned_orb(g, 0)
    result["own_orb_kills"] = g.pb.memory[e + 1] == 6 and g.peek("hit_result") == 3
    g.shot(out / "b2_lamp_returned_orb.png")
    g.stop()

    g = fresh(rom, 1)
    g.poke("stage_type", 0)
    a = g.addr("room")
    for y in range(5, 12):
        for x in range(6, 11):
            g.pb.memory[a + y * 16 + x] = 0
    g.pb.memory[a + 8 * 16 + 8] = 21
    teleport(g, 136, 152)
    g.hold({"up"})
    g.tick(45)
    g.hold(set())
    result["crate_pushes_north"] = g.pb.memory[a + 8 * 16 + 8] != 21 and any(
        g.pb.memory[a + y * 16 + 8] == 21 for y in range(5, 8))
    g.stop()

    g = fresh(rom, 1, enemies=True)
    g.tick(210)
    live = [k for k in range(g.peek("nen")) if g.peek("en", k * 16 + 1) != 6]
    result["waves_use_matrix_mobs"] = bool(live) and all(g.peek("en", k * 16) == 3 for k in live)
    report["wave_live"] = live
    g.stop()
    report["results"] = result
    (out / "stage2-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return all(result.values())


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()
    raise SystemExit(0 if run(args.rom, args.out) else 1)
