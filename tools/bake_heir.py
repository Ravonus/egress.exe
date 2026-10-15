"""Bake the D3 heir sprites for the cartridge (B1).

The owner-approved style D3 (tools/mock/sprite_styles.py: 16x22, BODY + WEAR OBJ layers, two 8x16 sprites each,
2 OBJ palettes of 3 colours) and the animation state table of proto/anim/heirs.py are rendered here, on the host,
by the mock's own rig, shader and layer legaliser. The cartridge composes any genome at run time:

  * colours never live in the tiles. The BODY palette (ink/hair, skin, hue-shifted skin shadow) is a function of the
    skin gene alone, the WEAR palette (cloth dark, cloth, rim) of the accent gene and the biome. Both are small baked
    tables, so every skin tone and every cloth colour costs nothing in ROM.
  * shapes are a BASE frame (look DREAM, hair SHORT, crew neck, plain cloth, no mark, a blade) plus one DELTA per
    other value of each shape gene, each rendered by the real renderer with only that gene changed:
        look 3, hair 5 (as the portrait draws it), neck 4, cloth pattern 8, worn mark 8, weapon 5
    A delta is a list of changed tile rows (row index, pixel mask, the two bit planes). The cartridge copies the
    base's 128 bytes, applies the heir's (at most six) deltas in a fixed order and uploads 8 tiles: 3,840 shape
    combinations x 6 skins x 8 cloths, from about 0.4 MB.
  * size (SMALL/HEAVY's +-0.5 px) and the face-dither gene are rendered at their middle value, and the fringe jag is
    seeded with 0. `--check` measures how far a composed heir is from the renderer's own frame.

A frame record is keyed by (pose, drawn direction 0..4 = S SE E NE N); W, SW and NW are the hardware X-flip of E, SE
and NE (the mock mirrors its rig the same way; on the cartridge the key light flips with it).

    .venv/bin/python tools/bake_heir.py src/gen            # bake (writes heir_gen.h, heir_tab.c, heir_data_*.c)
    .venv/bin/python tools/bake_heir.py --check [N]        # composite vs renderer on N random genomes
"""
from __future__ import annotations

import hashlib
import json
import math
import pickle
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
for _p in (ROOT / "tools", ROOT / "tools/mock", ROOT / "proto/genetics", ROOT / "proto/anim"):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))
import genes  # noqa: E402
import heirs as HA  # noqa: E402
import sprite_styles as SS  # noqa: E402

STYLE = "D3"
ST = SS.STYLES[STYLE]
M = SS.M
W, H = ST["W"], ST["H"]
BODY_Y = 1  # box rows the BODY sprites start at (cover cells (0,1), (8,1))
WEAR_Y = 6  # and the WEAR sprites ((0,6), (8,6))
CACHE = ROOT / "build/heir_cache.pkl"
BANK_BYTES = 16000  # per generated data file (one autobanked ROM bank each)

# ------------------------------------------------------------------------------------------------ the shape genes
# (name, number of values, base value). The cartridge applies deltas in this order; a later delta wins a pixel.
DIMS = [("look", 3, 0), ("neck", 4, 0), ("pat", 8, 0), ("hair", 5, 1), ("mark", 8, 0), ("weapon", 5, 0)]
SLOTS = [(d, v) for d, n, b in DIMS for v in range(n) if v != b]  # 27 delta slots per frame
WEAPONS = genes.WEAPON
BASE = {d: b for d, _, b in DIMS}

# a representative palette for the legaliser's luminance remap (colours never reach the tiles)
REP_PAL = [(60, 34, 30), (176, 112, 80), (120, 60, 54), (44, 48, 92), (128, 136, 184), (80, 232, 248)]


def heir_dict(shape):
    """a sprite_styles heir with these shape genes (hair = the hair the portrait draws, 0..4)"""
    return {"hair": shape["hair"], "look": shape["look"], "neck": shape["neck"], "pat": shape["pat"], "dith": 0,
            "size": 1, "mark": shape["mark"], "weapon": WEAPONS[shape["weapon"]], "seed": 0, "pal": REP_PAL,
            "skin": 2, "accent": 0, "dodge": "ROLL", "traits": [], "name": "", "genome": "0"}


def render_col(shape, dd, pose):
    """colour slots (-1 empty, 0..2 BODY, 3..5 WEAR) for a drawn direction (no mirror) at 1x"""
    st = dict(ST, _dith=0)
    h = heir_dict(shape)
    C, _, mirror = SS.rig(st, h, dd, pose)
    assert not mirror
    col = SS.shade(C, st, pose, mirror)
    col, _, _ = SS.legalize(col, st, REP_PAL, pose)
    return col


def to_tiles(col):
    """4 sprites (BODY L, BODY R, WEAR L, WEAR R) x 16 rows x 2 planes = 128 bytes, GB 2bpp order"""
    out = bytearray(128)
    for s, (x0, y0, layer) in enumerate(((0, BODY_Y, 0), (8, BODY_Y, 0), (0, WEAR_Y, 1), (8, WEAR_Y, 1))):
        for r in range(16):
            lo = hi = 0
            for x in range(8):
                c = int(col[M + y0 + r, M + x0 + x])
                v = 0
                if c >= 0 and (c >= 3) == bool(layer):
                    v = c + 1 if layer == 0 else c - 2
                lo |= (v & 1) << (7 - x)
                hi |= (v >> 1) << (7 - x)
            out[s * 32 + r * 2] = lo
            out[s * 32 + r * 2 + 1] = hi
    return bytes(out)


def row_mask(a, b, r):
    """pixels of row r (0..63) where the tiles differ"""
    o = r * 2
    m = 0
    for x in range(8):
        bit = 0x80 >> x
        if ((a[o] ^ b[o]) & bit) or ((a[o + 1] ^ b[o + 1]) & bit):
            m |= bit
    return m


def delta(base, var):
    out = []
    for r in range(64):
        m = row_mask(base, var, r)
        if m:
            out.append((r, m, var[r * 2] & m, var[r * 2 + 1] & m))
    return out


def apply(buf, patches):
    for r, m, lo, hi in patches:
        buf[r * 2] = (buf[r * 2] & ~m & 0xFF) | lo
        buf[r * 2 + 1] = (buf[r * 2 + 1] & ~m & 0xFF) | hi


# ------------------------------------------------------------------------------------------------ the state table
def P(state, k):
    return HA.TABLE[state]["frames"][k]


def fit(frames, total):
    """durations scaled to sum to `total` (>= 1 each): animation timed by the move's frame data"""
    d = [f["dur"] for f in frames]
    s = sum(d)
    out = [max(1, round(x * total / s)) for x in d]
    while sum(out) > total:
        out[out.index(max(out))] -= 1
    while sum(out) < total:
        out[out.index(max(out))] += 1
    return [dict(f, dur=v) for f, v in zip(frames, out)]


def seq(*parts):
    """[(state, frame index, duration)] -> frames"""
    return [dict(P(s, k), dur=d) for s, k, d in parts]


def cart_states():
    """the states the cartridge plays: (C name, frames, loop). Combat moves are timed by eg_input's frame data
    (proto/input/FRAMES.md) so the hit frame of the art is the hit frame of the game."""
    T = HA.TABLE
    S = []

    def add(name, frames, loop):
        S.append((name, frames, loop))

    add("IDLE", T["idle"]["frames"], True)
    add("IDLE_BLINK", T["idle_blink"]["frames"], True)
    for f in HA.FIDGETS + ["fidget_twitch", "fidget_bounce"]:
        add(f.upper(), T[f]["frames"], False)
    for f in ("long_sit", "long_sit_twitch", "long_doze"):
        add(f.upper(), T[f]["frames"], T[f]["loop"])
    for f in ("ctx_ready", "ctx_cool", "ctx_lowhp"):
        add(f.upper(), T[f]["frames"], True)
    add("WALK", T["walk"]["frames"], True)
    add("RUN", T["run"]["frames"], True)
    add("TURN", T["turn"]["frames"], False)
    add("SPRINT", T["sprint"]["frames"], True)
    add("SKID", seq(("dodge_slide", 3, 4)), False)
    add("DODGE_ROLL", fit(T["dodge_roll"]["frames"], 18), False)
    add("DODGE_BLINK", fit(T["dodge_blink"]["frames"], 14), False)
    add("DODGE_SLIDE", fit(T["dodge_slide"]["frames"], 20), False)
    # hits: windup = startup, attack = active, recover = recovery
    add("HIT1", seq(("windup", 0, 4), ("attack", 0, 1), ("attack", 1, 1), ("recover", 0, 5), ("recover", 1, 5)), False)
    add("HIT2", seq(("windup", 0, 4), ("attack", 0, 1), ("attack", 1, 1), ("recover", 0, 6), ("recover", 1, 6)), False)
    add("HIT3", seq(("windup", 0, 3), ("windup", 1, 3), ("attack", 0, 1), ("attack", 1, 2), ("recover", 0, 9),
                    ("recover", 1, 9)), False)
    add("WINDUP", T["windup"]["frames"], False)
    add("ATTACK", T["attack"]["frames"], False)
    add("RECOVER", T["recover"]["frames"], False)
    add("LUNGE", seq(("lunge", 0, 2), ("lunge", 1, 4), ("lunge", 2, 12)), False)
    # charge: one frame per level (11-29 held, 30-49, 50+), the cartridge picks it from the hold time
    add("CHARGE", seq(("charge", 0, 19), ("charge", 1, 20), ("charge", 2, 255)), False)
    add("CHARGED", seq(("windup", 1, 3), ("attack", 0, 2), ("attack", 1, 2), ("recover", 0, 7), ("recover", 1, 7)),
        False)
    add("SPIN", seq(("spin", 0, 2), ("spin", 1, 3), ("spin", 2, 3), ("spin", 3, 3), ("spin", 0, 3), ("spin", 1, 3),
                    ("recover", 0, 4)), False)
    add("PARRY", seq(("parry", 0, 2), ("parry", 1, 8), ("parry", 2, 12)), False)
    add("HURT", T["hurt"]["frames"], False)
    add("DEATH", T["death"]["frames"], False)
    add("SPAWN", T["spawn"]["frames"], False)
    add("PUSH", T["push"]["frames"], True)
    add("DIG", fit(T["dig"]["frames"], 12), False)
    add("PICKUP", fit(T["pickup"]["frames"], 16), False)
    add("BENCH", T["bench"]["frames"], True)
    return S


FLASH = {None: 0, "white": 1, "red": 2, "ice": 3, "dim": 4}


def frame_key(fr):
    """(pose json without dir/flash, direction offset, face_cam, flash code)"""
    p = dict(fr["pose"])
    off = p.pop("dir", 0) % 8
    cam = bool(p.pop("face_cam", False))
    fl = FLASH[p.pop("flash", None)]
    return json.dumps(p, sort_keys=True), off, cam, fl


# ------------------------------------------------------------------------------------------------ the bake
def bake_records(poses):
    """{(pose_json, dd): (base bytes, [patch list per SLOT])}, cached by the renderer's source hash"""
    src = b"".join((ROOT / p).read_bytes() for p in ("tools/mock/sprite_styles.py", "tools/bake_heir.py"))
    key = hashlib.sha256(src + json.dumps(sorted(poses)).encode()).hexdigest()
    if CACHE.exists():
        c = pickle.loads(CACHE.read_bytes())
        if c.get("key") == key:
            return c["rec"]
    rec = {}
    base_shape = dict(BASE)
    n = 0
    for pj in sorted(poses):
        pose = json.loads(pj)
        # JSON turns the tuples of hand offsets into lists: the rig only indexes them
        for dd in range(5):
            b = to_tiles(render_col(base_shape, dd, pose))
            ds = []
            for d, v in SLOTS:
                if d == "weapon" and not pose.get("weapon"):
                    ds.append([])
                    continue
                sh = dict(base_shape, **{d: v})
                ds.append(delta(b, to_tiles(render_col(sh, dd, pose))))
            rec[(pj, dd)] = (b, ds)
            n += 1
            if n % 50 == 0:
                print(f"  {n} / {len(poses) * 5} frame records", flush=True)
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_bytes(pickle.dumps({"key": key, "rec": rec}))
    return rec


def compose(rec, shape):
    b, ds = rec
    buf = bytearray(b)
    for i, (d, v) in enumerate(SLOTS):
        if shape[d] == v:
            apply(buf, ds[i])
    return bytes(buf)


def palettes():
    """BODY palettes per skin gene (ink/hair, skin, skin shadow) and WEAR cloth pairs per accent, as RGB555"""
    body = []
    for skin in range(6):
        mid = SKIN6[genes.SKIN_ORDER[skin]]
        ink = shade555(mid, 0)
        sk = SS.AP.rgb(mid)
        sh = SS.skin_shadow(sk)
        body.append([0x0000, ink, mid, rgb555(sh)])
    cloth = [[rgb555(cd), rgb555(cb)] for cd, cb in SS.CLOTH]
    return body, cloth


SKIN6 = [0x2E7F, 0x223B, 0x19B7, 0x1135, 0x08CF, 0x3EDF]  # src/avatar/crucible_avatar.c


def shade555(c, light):
    o = 0
    for k in range(3):
        v = (c >> (k * 5)) & 31
        v = v + ((31 - v) * 2) // 3 if light else (v * 2) // 7
        o |= v << (k * 5)
    return o


def rgb555(c):
    r, g, b = (int(round(v * 31 / 255)) for v in c)
    return r | (g << 5) | (b << 10)


def carr(name, data, per=16, ctype="uint8_t"):
    rows = []
    for i in range(0, len(data), per):
        rows.append("  " + ", ".join(f"0x{x:02X}" if ctype == "uint8_t" else f"0x{x:04X}" for x in data[i:i + per]) + ",")
    return f"const {ctype} {name}[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n"


def main_bake(out: Path):
    states = cart_states()
    poses = set()
    for _, frames, _ in states:
        for fr in frames:
            poses.add(frame_key(fr)[0])
    print(f"{len(states)} states, {sum(len(f) for _, f, _ in states)} frames, {len(poses)} poses")
    rec = bake_records(poses)
    pose_ids = {pj: i for i, pj in enumerate(sorted(poses))}
    # frame records -> data chunks (one bank each)
    chunks, cur, index = [], bytearray(), []
    total_patch = 0
    for pj in sorted(poses):
        for dd in range(5):
            b, ds = rec[(pj, dd)]
            blob = bytearray(b)
            for p in ds:
                blob.append(len(p))
                for r, m, lo, hi in p:
                    blob += bytes((r, m, lo, hi))
                total_patch += len(p)
            if len(cur) + len(blob) > BANK_BYTES:
                chunks.append(cur)
                cur = bytearray()
            index.append((len(chunks), len(cur)))
            cur += blob
    chunks.append(cur)
    # the state table
    st_rows, fr_rows = [], []
    for name, frames, loop in states:
        st_rows.append((len(fr_rows), len(frames), int(loop)))
        for fr in frames:
            pj, off, cam, fl = frame_key(fr)
            fr_rows.append((pose_ids[pj], 0x80 if cam else off, min(255, fr["dur"]), fl))
    body, cloth = palettes()
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("heir_data_*.c"):
        old.unlink()
    for i, ch in enumerate(chunks):
        (out / f"heir_data_{i}.c").write_text(
            "/* Generated by tools/bake_heir.py. Do not edit. D3 heir frame records: 128-byte base + 27 deltas. */\n"
            "#pragma bank 255\n#include <gb/gb.h>\n#include <stdint.h>\n"
            f"BANKREF(heir_data_{i})\n" + carr(f"heir_data_{i}", ch))
    h = ["/* Generated by tools/bake_heir.py. Do not edit. */", "#ifndef HEIR_GEN_H", "#define HEIR_GEN_H",
         "#include <gb/gb.h>", "#include <stdint.h>", ""]
    for i, (name, _, _) in enumerate(states):
        h.append(f"#define HA_{name} {i}u")
    h += [f"#define HA_COUNT {len(states)}u", f"#define HEIR_POSES {len(poses)}u",
          f"#define HEIR_CHUNKS {len(chunks)}u", f"#define HEIR_SLOTS {len(SLOTS)}u", ""]
    # slot index of each dimension's first non-base value, so the cartridge can find its delta
    k = 0
    for d, n, b in DIMS:
        h.append(f"#define HEIR_SLOT_{d.upper()} {k}u /* values 0..{n - 1}, base {b} */")
        h.append(f"#define HEIR_BASE_{d.upper()} {b}u")
        k += n - 1
    h += ["#define HEIR_FLASH_WHITE 1u", "#define HEIR_FLASH_RED 2u", "#define HEIR_FLASH_ICE 3u",
          "#define HEIR_FLASH_DIM 4u", "#define HEIR_DIR_CAM 0x80u", "",
          "typedef struct { uint8_t first_lo, first_hi, n, loop; } heir_state_t;",
          "typedef struct { uint8_t pose, dir, dur, flash; } heir_frame_t;",
          "typedef struct { uint8_t chunk, lo, hi; } heir_rec_t;",
          "/* in the table bank (heir_tab): read them with that bank switched in */",
          "extern const heir_state_t heir_states[HA_COUNT];", f"extern const heir_frame_t heir_frames[{len(fr_rows)}];",
          f"extern const heir_rec_t heir_recs[{len(index)}]; /* pose * 5 + drawn direction */",
          "extern const uint16_t heir_body_pal[6][4]; /* per skin gene: -, ink/hair, skin, skin shadow */",
          "extern const uint16_t heir_cloth_pal[8][2]; /* per accent: cloth dark, cloth */",
          "BANKREF_EXTERN(heir_tab)"]
    for i in range(len(chunks)):
        h.append(f"BANKREF_EXTERN(heir_data_{i})")
        h.append(f"extern const uint8_t heir_data_{i}[];")
    h += ["/* the chunks' ROM banks, filled at run time (BANK() is a link-time symbol) */",
          "void heir_chunk_banks(uint8_t *out) NONBANKED;",
          "/* the chunks' addresses */", "extern const uint8_t *const heir_chunk_ptr[HEIR_CHUNKS];", "", "#endif", ""]
    (out / "heir_gen.h").write_text("\n".join(h))
    t = ["/* Generated by tools/bake_heir.py. Do not edit. The heir state table, frame index and palettes. */",
         "#pragma bank 255", '#include "heir_gen.h"', "BANKREF(heir_tab)",
         "const heir_state_t heir_states[HA_COUNT] = {"]
    for (name, _, _), (f0, n, lp) in zip(states, st_rows):
        t.append(f"  {{{f0 & 255}u, {f0 >> 8}u, {n}u, {lp}u}}, /* {name} */")
    t.append("};")
    t.append(f"const heir_frame_t heir_frames[{len(fr_rows)}] = {{")
    t += [f"  {{{p}u, {o}u, {d}u, {f}u}}," for p, o, d, f in fr_rows]
    t.append("};")
    t.append(f"const heir_rec_t heir_recs[{len(index)}] = {{")
    t += [f"  {{{c}u, {o & 255}u, {o >> 8}u}}," for c, o in index]
    t.append("};")
    t.append("const uint16_t heir_body_pal[6][4] = {")
    t += ["  {" + ", ".join(f"0x{v:04X}" for v in p) + "}," for p in body]
    t.append("};")
    t.append("const uint16_t heir_cloth_pal[8][2] = {")
    t += ["  {" + ", ".join(f"0x{v:04X}" for v in p) + "}," for p in cloth]
    t.append("};")
    (out / "heir_tab.c").write_text("\n".join(t) + "\n")
    # bank 0: the chunk pointers and banks (small)
    b0 = ["/* Generated by tools/bake_heir.py. Do not edit. Bank 0: where the heir chunks live. */",
          '#include "heir_gen.h"', "const uint8_t *const heir_chunk_ptr[HEIR_CHUNKS] = {"]
    b0 += [f"  heir_data_{i}," for i in range(len(chunks))]
    b0 += ["};", "void heir_chunk_banks(uint8_t *out) NONBANKED {"]
    b0 += [f"  out[{i}] = BANK(heir_data_{i});" for i in range(len(chunks))]
    b0 += ["}", ""]
    (out / "heir_banks.c").write_text("\n".join(b0))
    size = sum(len(c) for c in chunks)
    print(f"records {len(index)}, data {size} bytes in {len(chunks)} chunks, patches {total_patch} "
          f"(avg {total_patch / len(index):.1f} rows/record), frames {len(fr_rows)}")
    return states, rec


# ------------------------------------------------------------------------------------------------ the check
def random_shape(rng):
    g = genes.founder(rng)
    return {"look": g["look"], "neck": g["neck"], "pat": g["pat"], "hair": SS.eff_hair(g), "mark": g["mark"],
            "weapon": g["weapon"][0]}


def main_check(n):
    states = cart_states()
    poses = set()
    for _, frames, _ in states:
        for fr in frames:
            poses.add(frame_key(fr)[0])
    rec = bake_records(poses)
    rng = genes.Rng(12345)
    pl = sorted(poses)
    bad_px = tot_px = 0
    worst = 0
    import random
    r = random.Random(7)
    for i in range(n):
        shape = random_shape(rng)
        for _ in range(12):
            pj = r.choice(pl)
            dd = r.randrange(5)
            want = to_tiles(render_col(shape, dd, json.loads(pj)))
            got = compose(rec[(pj, dd)], shape)
            diff = sum(bin(row_mask(want, got, rr)).count("1") for rr in range(64))
            opaque = sum(bin(want[rr * 2] | want[rr * 2 + 1]).count("1") for rr in range(64))
            bad_px += diff
            tot_px += opaque
            worst = max(worst, diff)
    print(f"composite vs renderer: {bad_px} of {tot_px} opaque pixels differ ({100 * bad_px / max(1, tot_px):.2f}%),"
          f" worst frame {worst} px")


if __name__ == "__main__":
    if "--check" in sys.argv:
        main_check(int(sys.argv[-1]) if sys.argv[-1].isdigit() else 20)
    else:
        main_bake(Path(sys.argv[1] if len(sys.argv) > 1 else "src/gen"))
