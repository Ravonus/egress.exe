"""Dump v2 ROM snapshots (map, cellset, camera) at given frames: snap.py ROM OUTDIR f1,f2,..."""
import re, sys
from pathlib import Path
from pyboy import PyBoy
rom, out, frames = Path(sys.argv[1]), Path(sys.argv[2]), sorted(int(x) for x in sys.argv[3].split(","))
out.mkdir(parents=True, exist_ok=True)
noi = rom.with_suffix(".noi").read_text(encoding="latin-1")
sym = {m[1]: int(m[2], 16) for m in re.finditer(r"DEF (\S+) 0x([0-9A-Fa-f]+)", noi)}
pb = PyBoy(str(rom), window="null", cgb=True, sound_emulated=False, log_level="ERROR")
pb.set_emulation_speed(0)
m = pb.memory
for f in range(max(frames) + 1):
    pb.tick(1, False)
    if f in frames:
        b = bytes(m[sym["_map"] + i] for i in range(1024))
        for n in ("_cam_x", "_cam_y", "_cam_a"):
            b += bytes([m[sym[n]], m[sym[n] + 1]])
        (out / f"snap_{f}.bin").write_bytes(b)
pb.stop(save=False)
