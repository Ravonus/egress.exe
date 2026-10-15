"""Bake the PRESS START title: seeded backrooms loops rendered offline by the v2 renderer, stored in ROM as per-frame
records of what changes, the tiles named by index into the loop's dictionary of unique tiles (docs/redesign.md 15;
src/title3d/t3d.h has the cartridge side).

    python tools/title3d/bake.py OUT_DIR HOST_DIR

Writes OUT_DIR/tb_l<K>_c<J>.s (a loop's records) and tb_l<K>_d<J>.s (its dictionary, 1024 tiles to a chunk), each an
autobanked chunk of at most 16 KiB; OUT_DIR/tb_index.s (the chunk and loop tables, ROM0), OUT_DIR/tb.h (constants for
the C side), OUT_DIR/chunks.mk (the chunk objects, for the Makefile), HOST_DIR/verify/loop<K>.bin + .cams (every
frame's expected tile buffer and camera, for the harness), and OUT_DIR/ledger.txt (the ROM cost of every table).

The renderer is proto/raycast/v2/src/core2.c built for 160x104 (bake_host.c). Every frame is checked here: the records
are played again from their bytes the way t3d_core.c plays them (the dictionary copies, the uploads, the map runs) into
a model of VRAM, twice round the loop, and each frame's visible tiles must equal the renderer's, exactly.

Why a dictionary: a corridor walk re-uploads the same tiles as they scroll in and out of the 256 VRAM slots (each loop
uploads ~2.7x as many tiles as it has unique ones), so the records carry 3-byte dictionary runs instead of 16 bytes a
tile. The cartridge copies the runs out of ROM into WRAM (one bank switch a run) and HBlank-DMAs them into VRAM: no
decoding (an LZ4-style stream cost ~270 scanlines a frame in C; raw records with the tiles inline cost 976 KiB).

A loop is one record per frame; a record never crosses a chunk:
    u16 next    offset of the next record from this one (0: the next chunk's start)
    u8  njobs   tile uploads (one HBlank DMA each)
    u8  nmap    map runs
    u8  nrng    dictionary runs
    njobs x { u16 VRAM address, u8 tiles }
    nmap  x { u16 shadow offset (row * 32 + column), u8 n, n tile ids }
    nrng  x { u16 first dictionary tile, u8 n }   (the frame's tiles in job order; a run never crosses a dict chunk)
The records run: K (frame 0, the keyframe: its tiles and the whole map), frames 1..N-1, W (frame 0 again, no uploads:
the map changes from N-1 back to 0), then frames 1.. again (the loop table points at frame 1's record).
VRAM slots: bank 0 ids 0-255 only (bank 1 holds the logo, the font, the blank), so the view's BG attributes never
change (each row's palette band, bank 0): 26 blocks of map ids a frame and no attributes.
Frame 0's tiles live in slots 0..K0-1 and are never reassigned. Frames 1.. use the rest as a FIFO ring from K0 (reset
every pass), never overwriting a tile that the frame on screen or the frame being built uses.
"""

from __future__ import annotations

import random
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import bake_world as BW  # noqa: E402

W, H = 160, 104
TW, TH = W // 8, H // 8
NCELL = TW * TH
BUF = NCELL * 16
MASKB = (NCELL + 7) // 8
FPS = 30
CELLS_PER_S = 1.0
STATIC = 152            # bank 1 ids STATIC..255 are the title's static tiles (t3d.h T3D_STATIC)
NSLOT = 256             # dynamic slots: VRAM bank 0 only
CHUNK = 0x4000
MAXRUNS = 48            # t3d.h T3D_MAX_JOBS
STAGE_TILES = 128       # t3d.h T3D_STAGE_TILES: a record's tiles fit the staging
JOB_MAX = 24            # t3d.h T3D_JOB_MAX: tiles per upload job (one HBlank DMA)
MAXRANGES = 128         # t3d.h T3D_MAX_RANGES: dictionary runs a record stages
MAP_GAP = 2             # unchanged cells a map run bridges (rewriting them is cheaper than a new run's header)
BAND_OF_ROW = [0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5]  # t3d_art.BAND_OF_ROW (asserted in main)
LOOP_SEEDS = [0x4F2A, 0x7A11, 0xBEEF]  # 0x1D3B (24 cells, 180 KB) dropped for the 512 KB cap
LOOP_CELLS = (20, 28)


def slot_bank_id(s: int) -> tuple[int, int]:
    return (0, s) if s < 256 else (1, s - 256)


def render(host: Path, mp, cams, tmp: Path) -> list[list[bytes]]:
    (tmp / "map.bin").write_bytes(bytes(v for row in mp for v in row))
    (tmp / "cams.bin").write_bytes(b"".join(struct.pack("<HHH", *c) for c in cams))
    subprocess.run([str(host), str(tmp / "map.bin"), str(tmp / "cams.bin"), str(tmp / "fb.bin")], check=True)
    raw = (tmp / "fb.bin").read_bytes()
    assert len(raw) == len(cams) * BUF
    frames = []
    for i in range(len(cams)):
        fb = raw[i * BUF:(i + 1) * BUF]
        # row-major cells (r, c); the framebuffer is column-major tile strips: tile (c, r) = c * TH + r
        frames.append([fb[(c * TH + r) * 16:(c * TH + r) * 16 + 16] for r in range(TH) for c in range(TW)])
    return frames


def runs_of(slots: list[int]) -> list[tuple[int, int]]:
    """consecutive slots as runs, split where VRAM is not contiguous (id 128, the bank) and at JOB_MAX"""
    out: list[list[int]] = []
    for s in slots:
        if out and s == out[-1][0] + out[-1][1] and s not in (128, 256, 256 + 128) and out[-1][1] < JOB_MAX:
            out[-1][1] += 1
        else:
            out.append([s, 1])
    return [(a, b) for a, b in out]


def vram_addr(slot: int) -> tuple[int, int]:
    """BG tile data at 0x8800 (signed ids): ids 0-127 at 0x9000, 128-255 at 0x8800"""
    b, i = slot_bank_id(slot)
    return (0x9000 + i * 16 if i < 128 else 0x8800 + (i - 128) * 16), b


def plan_loop(frames: list[list[bytes]]) -> tuple[list[dict], dict]:
    """per frame: the uploads (slot, tile) and the full map (slot per cell); K, 1..N-1, then W"""
    n = len(frames)
    key = list(dict.fromkeys(frames[0]))
    k0 = len(key)
    assert k0 <= 200, k0
    where = {t: i for i, t in enumerate(key)}
    slot_tile: dict[int, bytes] = {i: t for i, t in enumerate(key)}
    head = k0
    out = [{"frame": 0, "ups": list(zip(range(k0), key)), "map": [where[t] for t in frames[0]]}]
    for i in list(range(1, n)) + [0]:
        ts = frames[i]
        if i == 1:
            # the ring restarts at k0 each pass: forget ring tiles from the last pass
            for s in list(slot_tile):
                if s >= k0:
                    del where[slot_tile.pop(s)]
            head = k0
        need = set(ts)
        # a frame's tiles upload once the frame before it is on screen (t3d_core.c): that one stays intact
        shown = set(frames[i - 1] if i else frames[n - 1])
        ups = []
        for t in dict.fromkeys(ts):
            if t in where:
                continue
            for _ in range(NSLOT):
                old = slot_tile.get(head)
                if old is None or (old not in need and old not in shown):
                    break
                head = k0 + (head + 1 - k0) % (NSLOT - k0)
            else:
                raise RuntimeError("tile ring full")
            if old is not None:
                del where[old]
            slot_tile[head] = t
            where[t] = head
            ups.append((head, t))
            head = k0 + (head + 1 - k0) % (NSLOT - k0)
        assert i or not ups, "frame 0's tiles are permanent: W uploads nothing"
        out.append({"frame": i, "ups": ups, "map": [where[t] for t in ts]})
    return out, {"k0": k0}


def map_runs(prev: list[int] | None, mp: list[int]) -> list[tuple[int, list[int]]]:
    """changed cells as runs inside a row (bridging short gaps): (shadow offset, cells)"""
    runs = []
    for r in range(TH):
        cols = [c for c in range(TW) if prev is None or prev[r * TW + c] != mp[r * TW + c]]
        cur: list[int] = []
        for c in cols:
            if cur and c - cur[-1] - 1 <= MAP_GAP:
                cur += list(range(cur[-1] + 1, c + 1))
            else:
                if cur:
                    runs.append((r * 32 + cur[0], [r * TW + x for x in cur]))
                cur = [c]
        if cur:
            runs.append((r * 32 + cur[0], [r * TW + x for x in cur]))
    return runs


DICT_TILES = CHUNK // 16  # tiles per dictionary chunk (one ROM bank's window)


def make_dict(plans: list[dict]) -> dict[bytes, int]:
    """the loop's unique uploaded tiles, numbered in first-upload order (so a frame's new tiles are mostly one run)"""
    d: dict[bytes, int] = {}
    for pl in plans:
        for _, t in pl["ups"]:
            d.setdefault(t, len(d))
    return d


def ranges_of(idx: list[int]) -> list[tuple[int, int]]:
    """dictionary indices as runs (start, n), split at dictionary chunk boundaries and at 255"""
    out: list[list[int]] = []
    for i in idx:
        if out and i == out[-1][0] + out[-1][1] and i % DICT_TILES and out[-1][1] < 255:
            out[-1][1] += 1
        else:
            out.append([i, 1])
    return [(a, b) for a, b in out]


def record(plan: dict, prev: list[int] | None, dic: dict[bytes, int]) -> tuple[bytes, dict]:
    """one frame's record, `next` left 0 (patched when packed)"""
    mp = plan["map"]
    slots = [s for s, _ in plan["ups"]]
    tiles = {s: t for s, t in plan["ups"]}
    jobs = runs_of(slots)
    assert len(jobs) <= MAXRUNS and len(slots) <= STAGE_TILES
    mruns = map_runs(prev, mp)
    rng = ranges_of([dic[tiles[s]] for a, b in jobs for s in range(a, a + b)])
    assert len(rng) <= MAXRANGES
    rec = bytearray(struct.pack("<HBBB", 0, len(jobs), len(mruns), len(rng)))
    for a, b in jobs:
        addr, bank = vram_addr(a)
        assert bank == 0
        rec += struct.pack("<HB", addr, b)
    for off, cells in mruns:
        rec += struct.pack("<HB", off, len(cells))
        assert all(mp[k] < 256 for k in cells)
        rec += bytes(mp[k] for k in cells)
    for a, b in rng:
        rec += struct.pack("<HB", a, b)
    return bytes(rec), {"uploads": len(slots), "jobs": len(jobs), "mruns": len(mruns), "ranges": len(rng),
                        "cells": sum(len(c) for _, c in mruns)}


def pack(recs: list[bytes]) -> tuple[list[bytearray], list[tuple[int, int]]]:
    """records into chunks of at most 16 KiB (a record never crosses one); each record's (chunk, offset)"""
    chunks = [bytearray()]
    at = []
    for r in recs:
        assert len(r) <= CHUNK
        if len(chunks[-1]) + len(r) > CHUNK:
            chunks.append(bytearray())
        at.append((len(chunks) - 1, len(chunks[-1])))
        chunks[-1] += r
    for i, (c, o) in enumerate(at):
        nxt = at[i + 1] if i + 1 < len(at) else None
        v = 0 if nxt is None or nxt[0] != c else nxt[1] - o
        struct.pack_into("<H", chunks[c], o, v)
    return chunks, at


def dict_chunks(dic: dict[bytes, int]) -> list[bytes]:
    tiles = list(dic)
    return [b"".join(tiles[i:i + DICT_TILES]) for i in range(0, len(tiles), DICT_TILES)]


def verify_loop(chunks: list[bytearray], at: list[tuple[int, int]], dchunks: list[bytes],
                frames: list[list[bytes]], passes: int = 2) -> None:
    """play the records the way the cartridge does (t3d_core.c tb_play), twice round the loop (K, 1..N-1, W,
    1..N-1, W), into a model of VRAM and the shadow map, in the cartridge's order: frame t's map goes in and flips
    (t is on screen), then frame t+1's uploads (by HBlank DMA while t shows), their tiles fetched from the
    dictionary chunks by index. After every step the frame on screen must show exactly the renderer's frame."""
    n = len(frames)
    vram: dict[tuple[int, int], bytes] = {}
    order = [0] + [i for _ in range(passes) for i in list(range(1, n)) + [0]]
    pos = []
    c, o = at[0]
    for step, fi in enumerate(order):
        pos.append((c, o))
        nxt = struct.unpack_from("<H", chunks[c], o)[0]
        if fi == 0 and step > 0:
            c, o = at[1]
        else:
            c, o = (c, o + nxt) if nxt else (c + 1, 0)

    def parse(step):
        c, o = pos[step]
        rec = chunks[c]
        _, nj, nm, nr = struct.unpack_from("<HBBB", rec, o)
        p = o + 5
        jobs = []
        for _ in range(nj):
            addr, cnt = struct.unpack_from("<HB", rec, p)
            p += 3
            assert cnt <= JOB_MAX
            jobs.append((addr, cnt))
        runs = []
        for _ in range(nm):
            so, cnt = struct.unpack_from("<HB", rec, p)
            p += 3
            runs.append((so, bytes(rec[p:p + cnt])))
            p += cnt
        staged = []
        for _ in range(nr):
            i, cnt = struct.unpack_from("<HB", rec, p)
            p += 3
            ch, k = divmod(i, DICT_TILES)
            assert k + cnt <= DICT_TILES
            staged += [dchunks[ch][(k + t) * 16:(k + t) * 16 + 16] for t in range(cnt)]
        assert len(staged) == sum(cnt for _, cnt in jobs) <= STAGE_TILES
        return jobs, runs, staged

    def render(shadow):
        out = []
        for r in range(TH):
            for col in range(TW):
                tid = shadow[r * 32 + col]
                out.append(vram.get((0, 0x9000 + tid * 16 if tid < 128 else 0x8800 + (tid - 128) * 16)))
        return out

    def upload(step):
        jobs, _, staged = parse(step)
        q = 0
        for addr, cnt in jobs:
            for t in range(cnt):
                vram[(0, addr + 16 * t)] = staged[q]
                q += 1

    shadow: dict[int, int] = {}
    maps: list[dict[int, int]] = []
    upload(0)
    for step, fi in enumerate(order):
        for so, ids in parse(step)[1]:
            for t, v in enumerate(ids):
                shadow[so + t] = v
        maps.append(dict(shadow))
        assert render(shadow) == frames[fi], f"step {step} frame {fi}: map"
        if step + 1 < len(order):
            upload(step + 1)
            assert render(maps[-1]) == frames[fi], f"step {step + 1}: its uploads broke frame {fi}"


def cached_render(host: Path, mp, cams, tmp: Path, stem: Path) -> list[list[bytes]]:
    """the renderer's frames, reused when this loop's cameras, map and renderer are unchanged since the last bake"""
    key = b"".join(struct.pack("<HHH", *c) for c in cams) + bytes(v for row in mp for v in row)
    kf, bf = stem.with_suffix(".key"), stem.with_suffix(".bin")
    if kf.exists() and bf.exists() and kf.read_bytes() == key and bf.stat().st_mtime > host.stat().st_mtime:
        raw = bf.read_bytes()
        return [[raw[(i * NCELL + k) * 16:(i * NCELL + k) * 16 + 16] for k in range(NCELL)] for i in range(len(cams))]
    frames = render(host, mp, cams, tmp)
    kf.write_bytes(key)
    return frames


def chunk_asm(name: str, data: bytes) -> str:
    lines = [f"; generated by tools/title3d/bake.py: {name} ({len(data)} bytes)", f"\t.module {name}",
             f"\t.globl ___bank_{name}", f"___bank_{name} = 255", "\t.area _CODE_255", f"_{name}::"]
    for i in range(0, len(data), 32):
        lines.append("\t.db " + ",".join(f"0x{b:02X}" for b in data[i:i + 32]))
    return "\n".join(lines) + "\n"


def main() -> None:
    out, hostdir = Path(sys.argv[1]), Path(sys.argv[2])
    host = hostdir / "bake_host"
    out.mkdir(parents=True, exist_ok=True)
    (hostdir / "verify").mkdir(parents=True, exist_ok=True)
    tmp = hostdir / "tmp"
    tmp.mkdir(exist_ok=True)
    import t3d_art
    assert t3d_art.BAND_OF_ROW == BAND_OF_ROW
    index = ["; generated by tools/title3d/bake.py: the title loops' ROM chunks (bank, address); per loop: first",
             "; chunk, chunks, frames, frame 1's record (chunk, offset), first dictionary chunk",
             "\t.module tb_index", "\t.area _CODE", "_tb_chunks::"]
    loops_meta = []
    objs = []
    ledger = []
    first = 0
    total = 0
    for k, seed in enumerate(LOOP_SEEDS):
        rng = random.Random(seed)
        m = BW.gen_maze(rng)
        cyc = BW.find_cycle(m, rng, *LOOP_CELLS)
        mp = BW.dress(m, rng, cyc)
        nfr = round(len(cyc) / CELLS_PER_S * FPS)
        cams = BW.walk(cyc, nfr)
        frames = cached_render(host, mp, cams, tmp, hostdir / "verify" / f"loop{k}")
        plans, st = plan_loop(frames)
        dic = make_dict(plans)
        recs, stats, prev = [], [], None
        for pl in plans:
            r, rs = record(pl, prev, dic)
            recs.append(r)
            stats.append(rs)
            prev = pl["map"]
        chunks, at = pack(recs)
        dchunks = dict_chunks(dic)
        verify_loop(chunks, at, dchunks, frames)
        (hostdir / "verify" / f"loop{k}.bin").write_bytes(b"".join(b"".join(f) for f in frames))
        (hostdir / "verify" / f"loop{k}.cams").write_bytes(b"".join(struct.pack("<HHH", *c) for c in cams))
        names = [f"tb_l{k}_c{j}" for j in range(len(chunks))] + [f"tb_l{k}_d{j}" for j in range(len(dchunks))]
        for name, ch in zip(names, list(chunks) + dchunks):
            (out / f"{name}.s").write_text(chunk_asm(name, bytes(ch)))
            objs.append(name)
            index += [f"\t.globl ___bank_{name}", f"\t.globl _{name}",
                      f"\t.db ___bank_{name}", f"\t.dw _{name}"]
        loops_meta.append((first, len(chunks), nfr, at[1], first + len(chunks)))
        first += len(names)
        mid = stats[1:-1]
        up = [x["uploads"] for x in mid]
        rb = sum(len(r) for r in recs)
        db = sum(len(d) for d in dchunks)
        total += rb + db
        uploads = sum(x["uploads"] for x in stats)
        ledger.append(f"loop {k} seed {seed:04X}: {len(cyc)} cells, {nfr} frames ({nfr / FPS:.1f} s); keyframe "
                      f"{st['k0']} tiles; uploads/frame mean {sum(up) / len(up):.1f} max {max(up)}, "
                      f"{uploads} in all of which {len(dic)} unique ({uploads / len(dic):.2f}x dedup); HDMA jobs/frame "
                      f"max {max(x['jobs'] for x in mid)}; dictionary runs/frame mean "
                      f"{sum(x['ranges'] for x in mid) / len(mid):.1f} max {max(x['ranges'] for x in mid)}; map runs/frame mean "
                      f"{sum(x['mruns'] for x in mid) / len(mid):.1f}, cells mean "
                      f"{sum(x['cells'] for x in mid) / len(mid):.1f}; records {rb} B ({rb / nfr:.0f} B/frame, "
                      f"{len(chunks)} chunks) + dictionary {db} B ({len(dchunks)} chunks) = {rb + db} B")
    index += ["_tb_loops::"]
    for a, b, c, (c1, o1), d0 in loops_meta:
        index.append(f"\t.db {a}, {b}")
        index.append(f"\t.dw {c}")
        index.append(f"\t.db {a + c1}")
        index.append(f"\t.dw {o1}")
        index.append(f"\t.db {d0}")
    (out / "tb_index.s").write_text("\n".join(index) + "\n")
    hdr = ["/* generated by tools/title3d/bake.py */", "#ifndef TB_H", "#define TB_H",
           f"#define TB_W {W}u", f"#define TB_H_PX {H}u", f"#define TB_TW {TW}u", f"#define TB_TH {TH}u",
           f"#define TB_NCELL {NCELL}u", f"#define TB_MASKB {MASKB}u", f"#define TB_STATIC {STATIC}u",
           f"#define TB_NSLOT {NSLOT}u", f"#define TB_JOB_MAX {JOB_MAX}u", f"#define TB_STAGE_TILES {STAGE_TILES}u", f"#define TB_MAX_JOBS {MAXRUNS}u",
           f"#define TB_MAXRUNS {MAXRUNS}u", f"#define TB_MAX_RANGES {MAXRANGES}u", f"#define TB_DICT_TILES {DICT_TILES}u", f"#define TB_NLOOPS {len(LOOP_SEEDS)}u", f"#define TB_FPS {FPS}u",
           "#endif"]
    (out / "tb.h").write_text("\n".join(hdr) + "\n")
    (out / "chunks.mk").write_text("TB_CHUNKS := " + " ".join(objs) + "\n")
    idx = 3 * len(objs) + 8 * len(LOOP_SEEDS)
    ledger.append(f"title total {total} B ({total / 1024:.0f} KiB) in {len(objs)} autobanked chunks (records and "
                  f"dictionaries, unpadded: bankpack fills the gaps) + index {idx} B in ROM0; cap 512 KB. (Raw "
                  f"records with the tiles inline were 976 KiB for 4 loops; an LZ4-style stream ~400 KB but ~270 "
                  f"scanlines a frame to decode.)")
    (out / "ledger.txt").write_text("\n".join(ledger) + "\n")
    print("\n".join(ledger))


if __name__ == "__main__":
    main()
