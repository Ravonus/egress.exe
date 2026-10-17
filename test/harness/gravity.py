"""Compare actual cartridge gravity with the portable host across complete boards.

The native loop runs through normal frame input and updates; no injected function
calls or emulator CPU patches. Cases cover empty rows, falling/rolling stacks,
plates and shutters, and CRTs breaking glass, filling pits or falling into the abyss.
"""
import argparse
import json
import random
import subprocess
from pathlib import Path

from play import Game, MODE_TITLE, start_run


def check(rom, reference, cases=96):
    rng = random.Random(0xC07E)
    g = Game(rom)
    assert g.wait_mode(MODE_TITLE, 1500)
    assert start_run(g, True, True, seed=0x5EED,physical_fixture=1)
    g.poke("stage_type", 0)
    g.poke("nen", 0)
    g.poke("room", 0, 286)  # no living obstacles in this gravity fixture
    g.poke("room", 34, 224)
    g.poke("room", 17, 225)
    g.poke16("psx", 40 << 4)
    g.poke16("psy", 40 << 4)
    g.poke("pl_x", 40)
    g.poke("pl_y", 40)
    captured = []
    def gravity_done(_):
        captured.append(bytes(g.peek("room", i) for i in range(224)))
    symbol = g.sym["_actors_crush"]
    g.pb.hook_register(symbol >> 16, symbol & 0xFFFF, gravity_done, None)
    count = 0
    with subprocess.Popen([str(reference)], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as proc:
        for case in range(cases):
            cells = [2] * 224
            kinds = [0] * 12 + [4] * (1 + case % 5) + [3, 9, 28, 10, 11, 12, 22, 36, 138, 164]
            if case & 1:
                kinds.append(41)
                for y in range(14):cells[y*16]=cells[y*16+15]=41
                for x in range(16):cells[208+x]=41
            for y in range(1, 13):
                for x in range(1, 15):
                    c = y * 16 + x
                    cells[c] = 0 if x < 5 and y < 5 else rng.choice(kinds)
            cells[17] = 7
            if case<4:
                cells=[2]*224
                for y in range(1,13):
                    for x in range(1,15):cells[y*16+x]=0
                cells[17]=7;cells[35]=11 if case<3 else 12
                cells[34]=[10,138,22,0][case]
                if case==3:cells[68]=10
            for i, v in enumerate(cells):
                g.poke("room", v, i)
            # A direct board fixture replaces the constructor's capability
            # classification as well as its material cells.
            g.poke('floor_has_crt',int(any(v&31==4 for v in cells)))
            g.poke("eg_plates_dirty", 1)
            for step in range(8):
                before = bytes(g.peek("room", i) for i in range(224))
                proc.stdin.write(bytes([1, 34, 0, 0, 0]) + before)
                proc.stdin.flush()
                expected = proc.stdout.read(225)
                assert len(expected) == 225
                captured.clear()
                g.poke("grav_t", 1)
                for _ in range(30):
                    g.tick()
                    if captured:
                        break
                assert captured, "native gravity did not return"
                got = captured[0]
                assert got == expected[:224], {
                    "case": case, "step": step,
                    "cells": [(i, a, b) for i, (a, b) in enumerate(zip(got, expected)) if a != b][:12]
                }
                count += 1
        proc.stdin.close()
        assert proc.wait() == 0
    g.stop()
    return {"rom": str(rom), "boards": cases, "gravity_steps": count, "match": True}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("reference", type=Path)
    p.add_argument("rom", type=Path, nargs="+")
    args = p.parse_args()
    print(json.dumps([check(rom, args.reference) for rom in args.rom], indent=2))
