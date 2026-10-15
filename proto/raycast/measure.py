"""Measure the raycaster prototype in PyBoy (CGB, headless).

    .venv/bin/python proto/raycast/measure.py proto/raycast/build/ray_128x80_c4.gbc \
        [--frames 900] [--shot shots/x.png --shot-at 400] [--gif shots/walk.gif --gif-from 300 --gif-len 270]

The ROM walks the maze on its own (time-based). Every rendered view logs its phase times in scanlines (154 per
frame) and the VBlank at which it was flipped onto the screen; this script drains that ring, reports frames per
view and fps, then freezes the ROM and diffs its rays + framebuffer against the host build of the same C.
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
    ap.add_argument("--shot", type=Path)
    ap.add_argument("--shot-at", type=int, default=400)
    ap.add_argument("--gif", type=Path)
    ap.add_argument("--gif-from", type=int, default=300)
    ap.add_argument("--gif-len", type=int, default=270)
    ap.add_argument("--gif-step", type=int, default=3)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--checks", type=int, default=8)
    a = ap.parse_args()

    tag = re.search(r"ray_(\d+)x(\d+)_c(\d)", a.rom.name)
    W, H, C = int(tag[1]), int(tag[2]), int(tag[3])
    nrays, bufsz = W // C, (W // 8) * H * 2
    noi = a.rom.with_suffix(".noi").read_text(encoding="latin-1")
    sym = {m[1]: int(m[2], 16) for m in re.finditer(r"DEF (\S+) 0x([0-9A-Fa-f]+)", noi)}

    pb = PyBoy(str(a.rom), window="null", cgb=True, sound_emulated=False, log_level="ERROR")
    pb.set_emulation_speed(0)
    mem = pb.memory

    def u8(name: str, off: int = 0) -> int:
        return mem[sym["_" + name] + off]

    def u16(name: str, off: int = 0) -> int:
        return u8(name, off) | (u8(name, off + 1) << 8)

    views: list[dict] = []
    read_upto = 0  # next view id to read (flipped views only)
    frames_seen = 0
    gif_frames: list[Image.Image] = []

    def drain() -> None:
        nonlocal read_upto
        nflips = u16("nflips")
        while read_upto < nflips:
            k = read_upto
            s = k % LOGN
            views.append(
                {
                    "id": k,
                    "flip": u16("log_flip", 2 * s),
                    "upd": u16("log_upd", 2 * s),
                    "ray": u16("log_ray", 2 * s),
                    "fill": u16("log_fill", 2 * s),
                    "spr": u16("log_spr", 2 * s),
                    "wait": u16("log_wait", 2 * s),
                }
            )
            read_upto += 1

    total = a.warmup + a.frames
    for f in range(total):
        pb.tick(1, True)
        frames_seen += 1
        if f % 8 == 0:
            drain()
        if a.shot and f == a.shot_at:
            a.shot.parent.mkdir(parents=True, exist_ok=True)
            img = pb.screen.image.convert("RGB")
            img.resize((img.width * 4, img.height * 4), Image.NEAREST).save(a.shot)
        if a.gif and a.gif_from <= f < a.gif_from + a.gif_len and (f - a.gif_from) % a.gif_step == 0:
            img = pb.screen.image.convert("RGB")
            gif_frames.append(img.resize((img.width * 4, img.height * 4), Image.NEAREST))
    drain()
    cc, cl = u16("copy_calls"), u16("copy_lines")

    # frames per view over the measured window (skip warm-up views)
    vw = [v for v in views if v["id"] > 0]
    for i in range(1, len(views)):
        views[i]["fpv"] = (views[i]["flip"] - views[i - 1]["flip"]) & 0xFFFF
    warm_vbl = a.warmup
    meas = [v for v in views[1:] if v["flip"] > warm_vbl]
    fpv = [v["fpv"] for v in meas]
    res: dict = {"rom": a.rom.name, "view": f"{W}x{H}", "colw": C, "rays": nrays, "bufsz": bufsz}
    if fpv:
        span = meas[-1]["flip"] - meas[0]["flip"] + meas[0]["fpv"]
        res.update(
            views=len(fpv),
            frames=span,
            fps_avg=round(len(fpv) * FPS_LCD / span, 2),
            fps_min=round(FPS_LCD / max(fpv), 2),
            fps_max=round(FPS_LCD / min(fpv), 2),
            fpv_hist={str(k): fpv.count(k) for k in sorted(set(fpv))},
        )
        for key in ("upd", "ray", "fill", "spr", "wait"):
            vals = [v[key] for v in meas]
            res[f"{key}_lines_avg"] = round(sum(vals) / len(vals), 1)
            res[f"{key}_lines_max"] = max(vals)
        res["copy_lines_per_chunk"] = round(cl / cc, 2) if cc else None
        res["copy_chunks_per_view"] = round(cc / max(1, u16("nflips")), 2)
    res["rom_bytes"] = a.rom.stat().st_size

    # freeze at several points after the measured window and diff against the host build of the same C
    host = a.rom.parent / f"host_{W}x{H}_c{C}"
    checks, bad = 0, 0
    for k in range(a.checks):
        for _ in range(97):
            pb.tick(1, False)
        mem[sym["_freeze"]] = 1
        for _ in range(600):
            pb.tick(1, False)
            if u8("frozen"):
                break
        if not (u8("frozen") and host.exists()):
            bad += 1
            mem[sym["_freeze"]] = 0
            continue
        with tempfile.TemporaryDirectory() as td:
            inp, out = Path(td) / "in.bin", Path(td) / "out.bin"
            blob = bytes(mem[sym["_map"] + i] for i in range(1024))
            for n in ("cam_x", "cam_y", "cam_a", "spr_x", "spr_y"):
                blob += bytes([u8(n), u8(n, 1)])
            blob += bytes([u8("spr_on")])
            inp.write_bytes(blob)
            subprocess.run([str(host), str(inp), str(out)], check=True)
            ref = out.read_bytes()
        bufs = sym["_buf"]
        lb = u8("last_buf")
        bp = mem[bufs + 2 * lb] | (mem[bufs + 2 * lb + 1] << 8)
        gb = bytes(mem[sym["_ray_h"] + i] for i in range(nrays))
        gb += bytes(mem[sym["_ray_u"] + i] for i in range(nrays))
        gb += bytes(mem[sym["_ray_t"] + i] for i in range(nrays))
        gb += bytes(mem[sym["_ray_d"] + i] for i in range(2 * nrays))
        gb += bytes(mem[bp + i] for i in range(bufsz))
        checks += 1
        if gb != ref:
            bad += 1
            res.setdefault("host_mismatch_cams", []).append([u16("cam_x"), u16("cam_y"), u16("cam_a")])
        mem[sym["_freeze"]] = 0
    res["host_checks"] = checks
    res["host_match"] = checks == a.checks and bad == 0
    pb.stop(save=False)

    if gif_frames:
        a.gif.parent.mkdir(parents=True, exist_ok=True)
        gif_frames[0].save(
            a.gif, save_all=True, append_images=gif_frames[1:], duration=round(1000 * a.gif_step / FPS_LCD),
            loop=0, optimize=True,
        )
    print(json.dumps(res, indent=1))
    if a.json:
        a.json.write_text(json.dumps(res, indent=1))
    return 0 if res.get("host_match", False) else 1


if __name__ == "__main__":
    sys.exit(main())
