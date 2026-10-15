"""The baked title's checks for the PyBoy harness (src/title3d, tools/title3d/bake.py, docs/redesign.md 15).

    python test/harness/title3d.py build/egress.gbc docs/shots [--verify build/t3d_bake/v160x104/verify]

Also called from play.py's boot pass. From power-on: the KEEL boot hands over to the title; the walk's frame rate is
measured (map flips per second over 10 s of emulated time, target 30); at four points the walk is held and the frame
on screen is read back out of VRAM (the shown BG map's view rows, their attributes, the tiles they name, through the
0x8800 addressing) and compared with the bake's frame for that loop and index: exact, every tile; the VBlank's work
per frame is profiled (HBlank DMA blocks, palette bytes); the title stack's high-water mark is read; the screenshot
(b1_title.png, once the logo is lit) and a 6 s GIF (b1_title.gif) are written. START must then start a run.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from PIL import Image

MODE_TITLE, MODE_PLAY = 1, 2
TW, TH, ROW_VIEW = 20, 13, 18
BUF = TW * TH * 16
VERIFY = Path("build/t3d_bake/v160x104/verify")


def shot_lit(g, path: Path, limit: int = 600) -> None:
    """a screenshot once the logo is lit (it ignites, flickers and glitches on its own clock)"""
    for _ in range(limit):
        logo = g.pb.screen.image.convert("RGB").crop((0, 8, 160, 30))
        if sum(1 for r, gg, b in logo.getdata() if r > 200 and gg < 140 and b > 140) > 150:
            break
        g.tick()
    g.shot(path)


def vram(g, bank: int, addr: int, n: int) -> bytes:
    return bytes(g.pb.memory[bank, addr: addr + n])


def shown_view(g) -> list[bytes]:
    """the 13 x 20 view tiles on screen, row-major, read out of VRAM"""
    lcdc = g.pb.memory[0xFF40]
    base = (0x9C00 if lcdc & 0x08 else 0x9800) + ROW_VIEW * 32
    ids = vram(g, 0, base, TH * 32)
    attrs = vram(g, 1, base, TH * 32)
    out = []
    for r in range(TH):
        for c in range(TW):
            i, a = ids[r * 32 + c], attrs[r * 32 + c]
            addr = 0x9000 + i * 16 if i < 128 else 0x8800 + (i - 128) * 16
            out.append(vram(g, (a >> 3) & 1, addr, 16))
    return out


def baked(loop: int, frame: int, verify: Path) -> list[bytes]:
    with open(verify / f"loop{loop}.bin", "rb") as f:
        f.seek(frame * BUF)
        b = f.read(BUF)
    return [b[k * 16: k * 16 + 16] for k in range(TW * TH)]


def hold(g) -> tuple[int, int]:
    """stop the walk after its next flip; the loop and frame on screen"""
    g.poke("t3d_freeze", 1)
    for _ in range(30):
        g.tick()
        if g.peek("t3d_frozen"):
            break
    assert g.peek("t3d_frozen"), "the walk did not hold"
    return g.peek("t3d_loop"), g.peek16("t3d_frame")


def check_held(g, verify: Path) -> tuple[int, int, int]:
    loop, frame = g.peek("t3d_loop"), g.peek16("t3d_frame")
    got, want = shown_view(g), baked(loop, frame, verify)
    return loop, frame, sum(a != b for a, b in zip(got, want))


def frame_check(g, report: dict, verify: Path, n: int = 4) -> bool:
    ok = True
    checks = []
    for k in range(n):
        hold(g)
        loop, frame, bad = check_held(g, verify)
        checks.append({"loop": loop, "frame": frame, "tiles_differ": bad})
        ok &= bad == 0
        g.poke("t3d_freeze", 0)
        g.tick(37 + 23 * k)
    report["title_vram_vs_bake"] = checks
    return ok


def wrap_check(g, verify: Path) -> dict:
    """every frame from 12 before the loop's end to 12 after its wrap (the W record, then frame 1's), each read back
    from VRAM and compared with the bake, exact"""
    loop = g.peek("t3d_loop")
    nfr = (verify / f"loop{loop}.bin").stat().st_size // BUF
    while True:
        _, f = hold(g)
        g.poke("t3d_freeze", 0)
        if nfr - 40 <= f < nfr - 12:
            break
        g.tick(30 if f < nfr - 100 else 2)
    seen, bad, wrapped = 0, 0, False
    last = -1
    while not (wrapped and last >= 12):
        _, f = hold(g)
        _, frame, d = check_held(g, verify)
        if f < last:
            wrapped = True
        if f >= nfr - 12 or wrapped:
            seen += 1
            bad += d
        last = f
        g.poke("t3d_freeze", 0)
        g.tick()
    return {"loop": loop, "frames": nfr, "checked": seen, "tiles_differ": bad}


def chunks_banked(g) -> bool:
    """every record and dictionary chunk sits inside the switchable ROM window (the player maps its bank and reads it
    at the address the chunk table gives)"""
    syms = {k: v for k, v in g.sym.items() if re.fullmatch(r"_tb_l\d+_[cd]\d+", k)}
    return bool(syms) and all(0x4000 <= (v & 0xFFFF) < 0x8000 for v in syms.values())


def nloops(gen: Path = Path("build/t3d_bake/gen/tb.h")) -> int:
    return int(re.search(r"#define TB_NLOOPS (\d+)u", gen.read_text()).group(1))


def seed_for(k: int, n: int, nvar: int = 4) -> int:
    """a seed that picks loop k and palette variant k % nvar (title3d.c: seed % TB_NLOOPS, (seed >> 4) % T3D_NVAR)"""
    return next(s for s in range(0x100, 0x10000) if s % n == k and (s >> 4) % nvar == k % nvar)


def stack_high_water(g) -> int:
    a = g.addr("t3d_stack")
    for i in range(192):
        if g.pb.memory[a + i] != 0xA5:
            return 192 - i
    return 0


def measure(g, frames: int = 600) -> tuple[float, float, float]:
    """flips per second, GDMA blocks and palette bytes per shown frame"""
    f0, d0, p0 = g.peek16("t3d_nflips"), g.peek16("t3d_cyc_tiles"), g.peek16("t3d_cyc_pal")
    g.tick(frames)
    f1, d1, p1 = g.peek16("t3d_nflips"), g.peek16("t3d_cyc_tiles"), g.peek16("t3d_cyc_pal")
    nf = (f1 - f0) & 0xFFFF
    return nf * 60.0 / frames, ((d1 - d0) & 0xFFFF) / max(nf, 1), ((p1 - p0) & 0xFFFF) / max(nf, 1)


def gif(g, path: Path, seconds: float = 6.0, every: int = 2, scale: int = 2) -> None:
    frames = []
    for _ in range(int(seconds * 60 / every)):
        g.tick(every)
        img = g.pb.screen.image.convert("RGB")
        frames.append(img.resize((160 * scale, 144 * scale), Image.NEAREST))
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=int(1000 * every / 60), loop=0,
                   optimize=False)


def title_pass(g, out: Path, report: dict, verify: Path | None = VERIFY) -> bool:
    """g is at MODE_TITLE, just entered"""
    g.tick(150)  # the fade in and the logo's ignition
    shot_lit(g, out / "b1_title.png")
    report["title_seed"] = f"{g.peek16('t3d_seed'):04X}"
    report["title_loop"] = g.peek("t3d_loop")
    report["title_variant"] = g.peek("t3d_var")
    fps, dma, pal = measure(g)
    report["title_fps"] = round(fps, 2)
    # VRAM work per shown frame, all by HBlank DMA (one 16-byte block a visible line): the tiles and 26 map-id blocks
    report["title_upload_profile"] = {"tiles_per_frame": round(dma, 1), "hdma_lines_per_frame": round(dma + 26, 1),
                                      "palette_bytes_per_frame": round(pal, 1)}
    gif(g, out / "b1_title.gif")
    ok_v = frame_check(g, report, verify) if verify and (verify / "loop0.bin").exists() else True
    report["title_stack_high_water"] = stack_high_water(g)
    report["title_flips_missed"] = g.peek("t3d_flip_late")
    report["title_flip_line_max"] = 144 + g.peek("t3d_flip_line_max")
    ok = fps >= 29.5 and ok_v and report["title_stack_high_water"] < 176 and report["title_flips_missed"] == 0
    report["title_ok"] = ok
    return ok


def main() -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import argparse
    import json

    from play import Game

    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("out", type=Path, nargs="?")
    ap.add_argument("--verify", type=Path, default=VERIFY)
    ap.add_argument("--loops", action="store_true", help="every loop (forced seeds) across its wrap, then exit")
    a = ap.parse_args()
    report: dict = {}
    if a.loops:
        ok = True
        res = []
        n = nloops()
        for k in range(n):
            g = Game(a.rom)
            if k == 0:
                ok &= chunks_banked(g)
                report["chunks_banked"] = ok
            g.tick(150)  # past the boot ROM and the C runtime's RAM clear, inside the KEEL boot
            g.poke16("t3d_seed_force", seed_for(k, n))  # loop k, palette variant k
            ok &= g.wait_mode(MODE_TITLE, 1800)
            g.tick(60)
            fps, _, _ = measure(g, 300)
            r = wrap_check(g, a.verify)
            r["fps"] = round(fps, 2)
            r["variant"] = g.peek("t3d_var")
            r["flips_missed"] = g.peek("t3d_flip_late")
            r["flip_line_max"] = 144 + g.peek("t3d_flip_line_max")
            ok &= r["flips_missed"] == 0 and r["tiles_differ"] == 0 and r["loop"] == k and fps >= 29.5 and r["checked"] >= 24
            res.append(r)
            g.stop()
        report["title_loops"] = res
        report["ok"] = ok
        print(json.dumps(report, indent=2))
        return 0 if ok else 1
    a.out.mkdir(parents=True, exist_ok=True)
    g = Game(a.rom)
    g.tick(150)
    ok = g.wait_mode(MODE_TITLE, 1500)
    ok = ok and title_pass(g, a.out, report, a.verify)
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
