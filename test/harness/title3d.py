"""The 3D title's checks for the PyBoy harness (src/title3d, docs/redesign.md 15).

    python test/harness/title3d.py build/egress.gbc docs/shots [--host build/host/t3d_host]

Also called from play.py's boot pass. From power-on: the KEEL boot hands over to the title; the title's walk is
measured (views per second over 10 s of emulated time), its framebuffer is diffed byte for byte against the host build
of the same renderer C at four points of the walk (the SDCC miscompile check), the title stack's high-water mark is
read, and the screenshots (b1_title.png) and a 6 s GIF (b1_title.gif) are written. START must then start a run.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

MODE_TITLE, MODE_PLAY = 1, 2
MAP, FB, FBSZ = 0xD000, 0xD400, 1536


def view_crop(img: Image.Image) -> Image.Image:
    return img.crop((32, 40, 128, 104))


def snapshot(g) -> tuple[bytes, bytes, int]:
    """freezes the title loop after a view; returns (host input: map + camera, the view's framebuffer, its bank)"""
    g.poke("t3d_freeze", 1)
    for _ in range(30):
        g.tick()
        if g.peek("t3d_frozen"):
            break
    bank = g.peek("t3d_last_bank")
    m = bytes(g.pb.memory[2, MAP: MAP + 1024])
    cam = b"".join(g.peek(n, 0).to_bytes(1, "little") + g.peek(n, 1).to_bytes(1, "little")
                   for n in ("t3d_cx", "t3d_cy", "t3d_ca"))
    fb = bytes(g.pb.memory[bank, FB: FB + FBSZ])
    return m + cam, fb, bank


def host_check(g, host: Path, report: dict, n: int = 4) -> bool:
    ok = True
    diffs = []
    for k in range(n):
        inp, fb, bank = snapshot(g)
        with tempfile.TemporaryDirectory() as d:
            ip, op = Path(d) / "in.bin", Path(d) / "out.bin"
            ip.write_bytes(inp)
            subprocess.run([str(host), str(ip), str(op)], check=True)
            hfb = op.read_bytes()[-FBSZ:]
        same = hfb == fb
        diffs.append({"bank": bank, "match": same, "bytes_differ": sum(a != b for a, b in zip(hfb, fb))})
        ok &= same
        g.poke("t3d_freeze", 0)
        g.tick(37 + 23 * k)
    report["title_host_diff"] = diffs
    return ok


def stack_high_water(g) -> int:
    a = g.addr("t3d_stack")
    for i in range(192):
        if g.pb.memory[a + i] != 0xA5:
            return 192 - i
    return 0


def measure_fps(g, frames: int = 600) -> float:
    v0 = g.peek16("t3d_nviews")
    g.tick(frames)
    v1 = g.peek16("t3d_nviews")
    return ((v1 - v0) & 0xFFFF) * 60.0 / frames


def gif(g, path: Path, seconds: float = 6.0, every: int = 3, scale: int = 2) -> None:
    frames = []
    for _ in range(int(seconds * 60 / every)):
        g.tick(every)
        img = g.pb.screen.image.convert("RGB")
        frames.append(img.resize((160 * scale, 144 * scale), Image.NEAREST))
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=int(1000 * every / 60), loop=0,
                   optimize=False)


def title_pass(g, out: Path, report: dict, host: Path | None) -> bool:
    """g is at MODE_TITLE, just entered"""
    g.tick(150)  # the fade in and the logo's ignition
    g.shot(out / "b1_title.png")
    img = g.pb.screen.image.convert("RGB")
    view = view_crop(img)
    colours = len(set(view.getdata()))
    report["title_view_colours"] = colours
    g.tick(90)
    view2 = view_crop(g.pb.screen.image.convert("RGB"))
    moved = sum(a != b for a, b in zip(view.getdata(), view2.getdata()))
    report["title_view_pixels_changed_1_5s"] = moved
    report["title_seed"] = f"{g.peek16('t3d_seed'):04X}"
    fps = measure_fps(g)
    report["title_fps"] = round(fps, 2)
    gif(g, out / "b1_title.gif")
    ok_host = host_check(g, host, report) if host and host.exists() else True
    report["title_stack_high_water"] = stack_high_water(g)
    ok = colours >= 6 and moved > 200 and fps >= 22.0 and ok_host and report["title_stack_high_water"] < 176
    report["title_ok"] = ok
    return ok


def main() -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import argparse
    import json

    from play import Game

    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--host", type=Path, default=Path("build/host/t3d_host"))
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    report: dict = {}
    g = Game(a.rom)
    g.tick(150)
    ok = g.wait_mode(MODE_TITLE, 1500)
    ok = ok and title_pass(g, a.out, report, a.host)
    for _ in range(40):
        g.press("start")
        if g.wait_mode(MODE_PLAY, 20):
            break
    report["title_to_play"] = g.peek("game_mode") == MODE_PLAY
    g.stop()
    print(json.dumps(report, indent=2))
    return 0 if ok and report["title_to_play"] else 1


if __name__ == "__main__":
    sys.exit(main())
