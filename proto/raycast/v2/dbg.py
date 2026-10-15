"""Diff the cartridge framebuffer against host2 at given cameras: dbg.py ROM HOST x,y,a [x,y,a ...]"""
import re, sys, subprocess, tempfile
from pathlib import Path
from pyboy import PyBoy
rom, host = Path(sys.argv[1]), sys.argv[2]
cams = [tuple(int(v) for v in c.split(",")) for c in sys.argv[3:]]
noi = rom.with_suffix(".noi").read_text(encoding="latin-1")
sym = {m[1]: int(m[2], 16) for m in re.finditer(r"DEF (\S+) 0x([0-9A-Fa-f]+)", noi)}
W, H = 96, 64
pb = PyBoy(str(rom), window="null", cgb=True, sound_emulated=False, log_level="ERROR")
pb.set_emulation_speed(0)
m = pb.memory
for _ in range(120): pb.tick(1, False)
def w16(n, v): m[sym[n]] = v & 255; m[sym[n] + 1] = v >> 8
for cam in cams:
    m[sym["_freeze"]] = 1
    while not m[sym["_frozen"]]: pb.tick(1, False)
    w16("_cam_x", cam[0]); w16("_cam_y", cam[1]); w16("_cam_a", cam[2]); m[sym["_cam_lock"]] = 1
    m[sym["_freeze"]] = 0
    for _ in range(20): pb.tick(1, False)
    m[sym["_freeze"]] = 1
    while not m[sym["_frozen"]]: pb.tick(1, False)
    lb = m[sym["_last_buf"]]
    bp = m[sym["_buf"] + 2 * lb] | (m[sym["_buf"] + 2 * lb + 1] << 8)
    gb = bytes(m[bp + i] for i in range(12 * H * 2))
    rh = bytes(m[sym["_ray_h"] + i] for i in range(24))
    blob = bytes(m[0xD800 + i] for i in range(1024)) + bytes([cam[0] & 255, cam[0] >> 8, cam[1] & 255, cam[1] >> 8, cam[2] & 255, cam[2] >> 8])
    with tempfile.TemporaryDirectory() as td:
        Path(td, "i").write_bytes(blob)
        subprocess.run([host, f"{td}/i", f"{td}/o"], check=True)
        ref = Path(td, "o").read_bytes()
    hh = ref[:24]
    fb = ref[24 * 5:]
    gcam = [m[sym[n]] | (m[sym[n] + 1] << 8) for n in ("_cam_x", "_cam_y", "_cam_a")]
    print("cam", cam, "gb cam", gcam, "ray_h gb", list(rh), list(hh))
    for i, (a, b) in enumerate(zip(gb, fb)):
        if a != b:
            t, row, pl = i // (H * 2), (i % (H * 2)) // 2, i % 2
            print(f"  tile {t} row {row} pl {pl} gb {a:02x} host {b:02x}  h={hh[2*t]},{hh[2*t+1]}")
    m[sym["_freeze"]] = 0
pb.stop(save=False)
