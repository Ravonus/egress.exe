"""Measure a raycaster ROM (v1 or v2) in PyBoy (CGB, headless) and grab same-view stills / GIFs.

    measure2.py ROM [--frames 900] [--warmup 90] [--gif out.gif --gif-from 300 --gif-len 270 --gif-step 3]
                    [--poses poses.json --stills-dir DIR] [--record-poses poses.json --pose-frames 300,400,...]
                    [--json res.json] [--checks 8]

Every rendered view logs phase times in scanlines (154 per frame) and its flip VBlank; fps is views per LCD time
over the measured window. --record-poses saves the camera at given frames; --poses re-renders those exact cameras
(cam_lock) so v1 and v2 stills show identical views. v2 ROMs are also diffed against host2 (same C on the host).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image
from pyboy import PyBoy

LOGN = 64
FPS_LCD = 59.7275


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--warmup", type=int, default=90)
    ap.add_argument("--gif", type=Path)
    ap.add_argument("--gif-from", type=int, default=300)
    ap.add_argument("--gif-len", type=int, default=270)
    ap.add_argument("--gif-step", type=int, default=3)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--checks", type=int, default=8)
    ap.add_argument("--host", type=Path)
    ap.add_argument("--record-poses", type=Path)
    ap.add_argument("--pose-frames", default="")
    ap.add_argument("--poses", type=Path)
    ap.add_argument("--stills-dir", type=Path)
    ap.add_argument("--shots", default="", help="frame:path,frame:path screenshots during the run")
    a = ap.parse_args()

    noi = a.rom.with_suffix(".noi").read_text(encoding="latin-1")
    sym = {m[1]: int(m[2], 16) for m in re.finditer(r"DEF (\S+) 0x([0-9A-Fa-f]+)", noi)}
    v2 = "_cam_lock" in sym and "_log_flr" in sym
    tag = re.search(r"_(\d+)x(\d+)", a.rom.name)
    W, H = int(tag[1]), int(tag[2])
    nrays, bufsz = W // 4, (W // 8) * H * 2

    pb = PyBoy(str(a.rom), window="null", cgb=True, sound_emulated=False, log_level="ERROR")
    pb.set_emulation_speed(0)
    mem = pb.memory

    def u8(name, off=0):
        return mem[sym["_" + name] + off]

    def u16(name, off=0):
        return u8(name, off) | (u8(name, off + 1) << 8)

    def w16(name, v):
        mem[sym["_" + name]] = v & 255
        mem[sym["_" + name] + 1] = (v >> 8) & 255

    def grab():
        img = pb.screen.image.convert("RGB")
        return img.resize((img.width * 4, img.height * 4), Image.NEAREST)

    views, read_upto = [], 0
    gif_frames = []
    pose_frames = {int(x) for x in a.pose_frames.split(",") if x}
    poses = []
    shots = {}
    for item in a.shots.split(","):
        if item:
            f, p = item.split(":", 1)
            shots[int(f)] = Path(p)

    def drain():
        nonlocal read_upto
        nflips = u16("nflips")
        while read_upto < nflips:
            k = read_upto
            s = k % LOGN
            if "_log_u8" in sym:  # one byte per phase entry
                ph = {n: u8("log_" + n, s) for n in ("upd", "ray", "fill", "spr", "wait", "flr")}
            else:
                ph = {n: u16("log_" + n, 2 * s) if "_log_" + n in sym else 0
                      for n in ("upd", "ray", "fill", "spr", "wait", "flr")}
            views.append({"id": k, "flip": u16("log_flip", 2 * s), **ph})
            read_upto += 1

    total = a.warmup + a.frames
    for f in range(total):
        pb.tick(1, True)
        if f % 8 == 0:
            drain()
        if f in pose_frames:
            poses.append({"frame": f, "cam": [u16("cam_x"), u16("cam_y"), u16("cam_a")]})
        if f in shots:
            shots[f].parent.mkdir(parents=True, exist_ok=True)
            grab().save(shots[f])
        if a.gif and a.gif_from <= f < a.gif_from + a.gif_len and (f - a.gif_from) % a.gif_step == 0:
            gif_frames.append(grab())
    drain()
    cc, cl = u16("copy_calls"), u16("copy_lines")
    for i in range(1, len(views)):
        views[i]["fpv"] = (views[i]["flip"] - views[i - 1]["flip"]) & 0xFFFF
    meas = [v for v in views[1:] if v["flip"] > a.warmup]
    fpv = [v["fpv"] for v in meas]
    res = {"rom": a.rom.name, "view": f"{W}x{H}"}
    if fpv:
        span = meas[-1]["flip"] - meas[0]["flip"] + meas[0]["fpv"]
        res.update(views=len(fpv), frames=span, fps_avg=round(len(fpv) * FPS_LCD / span, 2),
                   fps_min=round(FPS_LCD / max(fpv), 2), fps_max=round(FPS_LCD / min(fpv), 2),
                   fpv_hist={str(k): fpv.count(k) for k in sorted(set(fpv))})
        for key in ("upd", "ray", "fill", "flr", "spr", "wait"):
            vals = [v[key] for v in meas]
            res[f"{key}_lines_avg"] = round(sum(vals) / len(vals), 1)
            res[f"{key}_lines_max"] = max(vals)
        res["copy_lines_per_chunk"] = round(cl / cc, 2) if cc else None
    res["rom_bytes"] = a.rom.stat().st_size

    def freeze_on():
        mem[sym["_freeze"]] = 1
        for _ in range(600):
            pb.tick(1, False)
            if u8("frozen"):
                return True
        return False

    # host diff (v2 only)
    host = a.host
    checks, bad = 0, 0
    if v2 and host and host.exists():
        for k in range(a.checks):
            for _ in range(97):
                pb.tick(1, False)
            if not freeze_on():
                bad += 1
                continue
            with tempfile.TemporaryDirectory() as td:
                inp, out = Path(td) / "in.bin", Path(td) / "out.bin"
                blob = bytes(mem[sym["_map"] + i] for i in range(1024))
                for n in ("cam_x", "cam_y", "cam_a"):
                    blob += bytes([u8(n), u8(n, 1)])
                inp.write_bytes(blob)
                subprocess.run([str(host), str(inp), str(out)], check=True)
                ref = out.read_bytes()
            lb = u8("last_buf")
            bp = mem[sym["_buf"] + 2 * lb] | (mem[sym["_buf"] + 2 * lb + 1] << 8)
            gb = bytes(mem[sym["_ray_h"] + i] for i in range(nrays))
            gb += bytes(mem[sym["_ray_u"] + i] for i in range(nrays))
            gb += bytes(mem[sym["_ray_t"] + i] for i in range(nrays))
            gb += bytes(mem[sym["_ray_d"] + i] for i in range(2 * nrays))
            gb += bytes(mem[bp + i] for i in range(bufsz))
            checks += 1
            if gb != ref:
                bad += 1
                diffs = [i for i, (x, y) in enumerate(zip(gb, ref)) if x != y]
                hdr = nrays * 5
                where = []
                for i in diffs[:6]:
                    if i < hdr:
                        where.append(f"ray hdr {i}")
                    else:
                        o = i - hdr
                        where.append(f"tile {o // (H * 2)} row {(o % (H * 2)) // 2} pl {o % 2} gb {gb[i]:02x} host {ref[i]:02x}")
                res.setdefault("host_mismatch", []).append({"cam": [u16("cam_x"), u16("cam_y"), u16("cam_a")],
                                                            "bytes": len(diffs), "where": where})
            mem[sym["_freeze"]] = 0
        res["host_checks"] = checks
        res["host_match"] = checks == a.checks and bad == 0

    # same-view stills
    if a.poses and a.stills_dir:
        a.stills_dir.mkdir(parents=True, exist_ok=True)
        for p in json.loads(a.poses.read_text()):
            if not freeze_on():
                continue
            w16("cam_x", p["cam"][0])
            w16("cam_y", p["cam"][1])
            w16("cam_a", p["cam"][2])
            mem[sym["_cam_lock"]] = 1
            mem[sym["_freeze"]] = 0
            for _ in range(16):
                pb.tick(1, True)
            grab().save(a.stills_dir / f"view_{p['frame']}.png")
        mem[sym["_cam_lock"]] = 0
    pb.stop(save=False)
    if a.record_poses:
        a.record_poses.write_text(json.dumps(poses, indent=1))
    if gif_frames:
        a.gif.parent.mkdir(parents=True, exist_ok=True)
        gif_frames[0].save(a.gif, save_all=True, append_images=gif_frames[1:],
                           duration=round(1000 * a.gif_step / FPS_LCD), loop=0, optimize=True)
    print(json.dumps(res, indent=1))
    if a.json:
        a.json.parent.mkdir(parents=True, exist_ok=True)
        a.json.write_text(json.dumps(res, indent=1))
    return 0 if res.get("host_match", True) else 1


if __name__ == "__main__":
    sys.exit(main())
