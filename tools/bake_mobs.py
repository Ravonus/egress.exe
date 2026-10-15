#!/usr/bin/env python3
"""Bake the office mobs' looks for the cartridge (docs/redesign.md 7.6, src/mob.h).

Looks come from the 7.6 generator, tools/mock/mobgen.py (the same parts, rules, ramps and tells as the mock sheets),
cut to the cartridge's 16x16 mob box (two 8x16 OBJs) in the D3 family style mobgen already draws: a dithered
mid-to-light ramp toward the top-left and a 1 px inner outline.

Per look, 24 sprite tiles (one look slot, src/mob.h):
  0..15   the body (palette A, colours 1..3): frames idle 0, idle 1, tell, dissolve; each frame is two 8x16 OBJs
          (left: tiles +0 +1, right: +2 +3)
  16..23  the overlay (palette B: 1 ink, 2 the cue colour; the loader turns 2 into 3 for the floor's second group)
          for idle 0 and idle 1 (the tell reuses idle 1's, the dissolve has none)
plus a record: family, cart tag, the body ramp (3 x RGB555), the cue colour, which overlay OBJs are empty, a name.

Families (src/mob.h FAM_*), each LOOKS_PER_FAM seeded variants with their own tag. A look must fit 16x16 in every
frame; seeds that don't are skipped (deterministically), so the bake is stable.

Outputs: src/gen/mob_art_0.c, mob_art_1.c (tiles, half the looks each), src/gen/mob_info.c (records, names,
palettes), src/gen/mob_art.h, and docs/shots/b2_mobs_sheet.png (every look, every frame, at 4x, on floor and void).

usage: .venv/bin/python tools/bake_mobs.py [--sheet-only]
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "mock"))
import mobgen as M  # noqa: E402

GEN = ROOT / "src" / "gen"
SHEET = ROOT / "docs" / "shots" / "b2_mobs_sheet.png"
LOOKS_PER_FAM = 6
BOX = 16
FRAMES = [("IDLE", {}), ("IDLE 2", {"squash": 1}), ("TELL", {"squash": 2, "eyes": "roll"}),
          ("DISSOLVE", {"dissolve": 0.5, "eyes": "closed", "pal": "death"})]
CART_TAGS = ["HOT", "COLD", "WET", "SHINY", "ALIVE", "MAGIC", "GLITCH", "STONE"]  # eg_piece.h TAG_* order
NOTAG = 8

# family: (name, mobgen body, size, mobility, defence look, tags its variants cycle through)
# tags follow proto/matrix/mx_mob.c's rules for the family's axes (src/mob.c FAM table)
FAMS = [
    ("INTERN", 0, 1, "WALKER", "NONE", ["NONE", "WET", "SHINY", "ALIVE", "COLD", "NONE"]),
    ("CABINET", 1, 1, "WALKER", "NONE", ["STONE", "NONE", "COLD", "STONE", "SHINY", "NONE"]),
    ("LAMP", 2, 1, "TURRET", "NONE", ["SHINY", "MAGIC", "NONE", "SHINY", "COLD", "ALIVE"]),
    ("FAX", 3, 1, "FLYER", "NONE", ["NONE", "ALIVE", "WET", "SHINY", "NONE", "COLD"]),
    ("STAPLES", 0, 0, "SWARM", "NONE", ["NONE", "HOT", "GLITCH", "STONE", "HOT", "ALIVE"]),
    ("MEMO", 0, 1, "WALKER", "ARMOURED", ["NONE", "COLD", "SHINY", "NONE", "COLD", "SHINY"]),
    ("MOLE", 3, 0, None, "NONE", ["ALIVE", "NONE", "COLD", "WET", "NONE", "ALIVE"]),
]


# proto/matrix/mx_mob.c's element rules for each family's axes (body, defence): a baked tag must be legal
TAG_BANS = {"CABINET": {"AIRY", "GLITCH"}, "FAX": {"STONE", "GLITCH"}, "MOLE": {"AIRY", "SHINY", "GLITCH"},
            "INTERN": {"GLITCH"}, "MEMO": {"GLITCH"}, "LAMP": set(), "STAPLES": set()}
for _f in FAMS:
    assert not set(_f[5]) & TAG_BANS[_f[0]], _f

# FX and marker tiles (8x16 OBJs, the shape in the top 8x8; colour 3 = the bright one, 2 mid, 1 ink), after the look
# slots: MOB_FX_TILE + 2 * FXT_*
FX_TILES = {
    "CROWN": ["........", ".3.3.3..", ".33333..", ".32323..", ".33333..", "........", "........", "........"],
    "DOT": ["........", "........", "...33...", "..3223..", "..3223..", "...33...", "........", "........"],
    "STAR": ["...3....", "...3....", ".3.3.3..", "..333...", "3333333.", "..333...", ".3.3.3..", "...3...."],
    "SHARD": ["........", "....3...", "...32...", "..322...", "..32....", ".32.....", "........", "........"],
    "DROP": ["........", "...3....", "...3....", "..323...", "..323...", "..333...", "........", "........"],
    "EMBER": ["........", "...3....", "..33....", "..323...", ".3223...", ".3223...", "..33....", "........"],
    "BOLT": ["....33..", "...33...", "..3333..", "....33..", "...33...", "..33....", ".3......", "........"],
    "LEAF": ["........", "....33..", "...323..", "..323...", ".333....", ".3......", "........", "........"],
    "RUNE": ["...3....", "..3.3...", ".3.2.3..", "3.222.3.", ".3.2.3..", "..3.3...", "...3....", "........"],
    "CLANG": ["3......3", ".3....3.", "..3..3..", "........", "........", "..3..3..", ".3....3.", "3......3"],
    "BUMP": ["........", "........", "........", "........", "..2.2...", ".21212..", "2111112.", ".22222.."],
    "RING": [".1.1.1..", "1.....1.", "........", "1.....1.", "........", "1.....1.", ".1.1.1..", "........"],
}
FXT = list(FX_TILES)


def fx_tile_bytes():
    out = bytearray()
    for nm in FXT:
        rows = FX_TILES[nm]
        for y in range(16):
            lo = hi = 0
            for x in range(8):
                ch = rows[y][x] if y < 8 else "."
                i = 0 if ch == "." else int(ch)
                lo |= (i & 1) << (7 - x)
                hi |= (i >> 1) << (7 - x)
            out += bytes((lo, hi))
    return bytes(out)


def rgb555(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def frames_of(lk):
    """the four frames as code grids, and their shared crop (None if a frame breaks the 16x16 box)"""
    cvs = [M.render(lk, pose) for _, pose in FRAMES]
    xs, ys = [], []
    for cv in cvs:
        for y in range(cv.h):
            for x in range(cv.w):
                if cv.c[y][x]:
                    xs.append(x)
                    ys.append(y)
    if not xs:
        return None
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    if y1 - y0 + 1 > BOX:
        # a tell floating over the head (HOT's ember): close the empty rows between it and the body
        keep = [y for y in range(y0, y1 + 1) if any(cv.c[y][x] for cv in cvs for x in range(cv.w))]
        drop = set(range(y0, y1 + 1)) - set(keep)
        if drop:
            for cv in cvs:
                rows = [cv.c[y] for y in range(cv.h) if y not in drop]
                cv.c = [[0] * cv.w for _ in range(cv.h - len(rows))] + rows
            y0 += len(drop)
    if x1 - x0 + 1 > BOX or y1 - y0 + 1 > BOX:
        return None
    cx = (x0 + x1 + 1) // 2
    ox, oy = cx - BOX // 2, y1 + 1 - BOX  # bottom-aligned, centred
    ox = min(max(ox, x1 + 1 - BOX), x0)
    grids = []
    for cv in cvs:
        g = [[cv.c[oy + y][ox + x] if 0 <= oy + y < cv.h and 0 <= ox + x < cv.w else 0 for x in range(BOX)]
             for y in range(BOX)]
        grids.append(g)
    return grids


def pick_looks():
    looks = []
    for fi, (fname, body, size, mob, dfn, tags) in enumerate(FAMS):
        for vi in range(LOOKS_PER_FAM):
            tag = tags[vi]
            for attempt in range(400):
                seed = M.mix_seed(0xB2, fi, vi, attempt)
                sz = size if attempt < 200 else 0  # an M look that won't fit the box falls back to S
                lk = M.gen(seed, fam="OFFICE", size=sz, tag=tag, defence=dfn if sz else "NONE", mobility=mob,
                           body=body)
                if tag == "HOT" and attempt & 1:
                    lk["topper"] = "NONE"  # the ember is the topper
                if fname in ("MOLE", "STAPLES") and lk["limb"] not in ("NONE", "TENTACLES", "LEGS2"):
                    continue
                if dfn != "NONE" and lk["defence"] != dfn:
                    continue
                g = frames_of(lk)
                if g is None:
                    continue
                looks.append({"fam": fi, "fname": fname, "lk": lk, "grids": g, "tag": tag, "seed": seed})
                break
            else:
                raise SystemExit(f"no 16x16 look for {fname} variant {vi}")
    return looks


def tile_bytes(grid, col, row, plane):
    """one 8x8 tile of `grid` at OBJ column col (0/1), row (0/1), 2bpp; plane 'A' codes 1..3, 'B' 4 ink 5/6 cue"""
    out = bytearray()
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = grid[row * 8 + y][col * 8 + x]
            if plane == "A":
                i = v if 1 <= v <= 3 else (1 if v else 0)  # under the overlay: outline dark, so a dropped overlay leaves no hole
            else:
                i = 1 if v == 4 else (2 if v in (5, 6) else 0)
            lo |= (i & 1) << (7 - x)
            hi |= (i >> 1) << (7 - x)
        out += bytes((lo, hi))
    return out


def frame_tiles(grid, plane):
    # 8x16 order: left OBJ (top, bottom), right OBJ (top, bottom)
    return b"".join(tile_bytes(grid, c, r, plane) for c in (0, 1) for r in (0, 1))


def look_record(L):
    lk = L["lk"]
    pa = M.pal_body("OFFICE", lk["tag"], lk["hue"], lk.get("ramp", 0))
    pb = M.pal_overlay("OFFICE", lk["tag"], lk["accent"])
    cue = pb[2] if lk["tag"] != "NONE" else pb[1]
    tiles = bytearray()
    for g in L["grids"]:
        tiles += frame_tiles(g, "A")
    bmask = 0
    for fi in (0, 1):
        t = frame_tiles(L["grids"][fi], "B")
        if any(t[0:32]):
            bmask |= 1 << (fi * 2)
        if any(t[32:64]):
            bmask |= 2 << (fi * 2)
        tiles += t
    assert len(tiles) == 24 * 16
    name = look_name(L)
    return {"tiles": bytes(tiles), "pal": [rgb555(c) for c in pa], "cue": rgb555(cue), "bmask": bmask,
            "tag": CART_TAGS.index(lk["tag"]) if lk["tag"] in CART_TAGS else NOTAG, "name": name}


# names (7.1 voice): an adjective that tells the truth about the tag (or the family's habit when untagged) and the
# family's noun, so the name hints at what the mob is and what kills it
ADJ_TAG = {"HOT": ["TOASTY", "WARM"], "COLD": ["CHILLY", "FROSTY"], "WET": ["DAMP", "SOGGY"],
           "SHINY": ["GLOSSY", "WAXED"], "ALIVE": ["LEAFY", "MOSSY"], "MAGIC": ["HEXED", "MINTY"],
           "GLITCH": ["WRONG", "BENT"], "STONE": ["DENSE", "GRAVEL"]}
ADJ_FAM = [["SORRY", "LATE", "PALE"], ["HEAVY", "SOLID", "LOOSE"], ["BRIGHT", "STERN", "LOUD"],
           ["NOISY", "BUSY", "SPARE"], ["TINY", "LOOSE", "SPARE"], ["STERN", "BOSSY", "STIFF"],
           ["SHY", "LOW", "SLY"]]
NOUN_FAM = [["INTERN", "TEMP", "CLERK"], ["CABINET", "FILER", "DRAWERS"], ["LAMP", "DESK LAMP", "LAMP"],
            ["FAX", "PRINTER", "FAX"], ["STAPLES", "PINS", "CLIPS"], ["MEMO", "CLIPBOARD", "MEMO"],
            ["CABLE", "WIRE", "CORD"]]


def look_name(L):
    h = L["seed"]
    adj = ADJ_TAG[L["tag"]][h & 1] if L["tag"] in ADJ_TAG else ADJ_FAM[L["fam"]][h % 3]
    for k in range(3):
        noun = NOUN_FAM[L["fam"]][(h >> 4) % 3 - k]
        if len(adj) + 1 + len(noun) <= 13:
            return f"{adj} {noun}"
    return adj + " " + NOUN_FAM[L["fam"]][0]


def c_bytes(b, per=24):
    rows = []
    for i in range(0, len(b), per):
        rows.append("  " + ", ".join(f"0x{v:02x}" for v in b[i:i + per]) + ",")
    return "\n".join(rows)


def write_c(looks, recs):
    n = len(looks)
    half = (n + 1) // 2
    for part, (a, b) in enumerate(((0, half), (half, n))):
        body = "\n".join("  {\n" + c_bytes(recs[i]["tiles"]) + "\n  }," for i in range(a, b))
        src = f"""/* generated by tools/bake_mobs.py: office mob looks {a}..{b - 1}, 24 sprite tiles each (src/mob.h) */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "mob_art.h"

static const uint8_t tiles[{b - a}u][384] = {{
{body}
}};

/* look `look` into sprite tiles tile0..tile0+23; recolour 1: the overlay's cue colour 2 becomes 3 (group 1) */
void mob_art_upload{part}(uint8_t look, uint8_t tile0, uint8_t recolour) BANKED {{
  static uint8_t buf[128];
  const uint8_t *p = tiles[(uint8_t)(look - {a}u)];
  uint8_t i;
  set_sprite_data(tile0, 16u, p);
  memcpy(buf, p + 256u, 128u);
  if (recolour)
    for (i = 0; i != 128u; i += 2u) buf[i] |= buf[i + 1u];
  set_sprite_data((uint8_t)(tile0 + 16u), 8u, buf);
}}
"""
        (GEN / f"mob_art_{part}.c").write_text(src)
    fam_first = []
    for fi in range(len(FAMS)):
        fam_first.append(next(i for i, L in enumerate(looks) if L["fam"] == fi))
    rows = []
    for L, r in zip(looks, recs):
        p = r["pal"]
        rows.append(f"  {{{L['fam']}u, {r['tag']}u, 0x{r['bmask']:02x}u, {{0x{p[0]:04x}, 0x{p[1]:04x}, 0x{p[2]:04x}}}, "
                    f"0x{r['cue']:04x}, \"{r['name']}\"}}, /* {L['fname']} {L['tag']} */")
    aff = [rgb555(M.AFFIX_COL[a]) for a in ("SHIELDED", "VAMPIRIC", "SPLITTING", "HASTED", "TELEPORT", "COLLAPSE")]
    elcue = [rgb555(M.ELEMENT[t][1]) for t in CART_TAGS]
    info = f"""/* generated by tools/bake_mobs.py: the office looks' records (src/gen/mob_art.h) */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "mob_art.h"

static const mob_look_t looks[MOB_LOOKS] = {{
{chr(10).join(rows)}
}};
static const uint16_t affix_col[6] = {{{", ".join(f"0x{v:04x}" for v in aff)}}};
static const uint16_t tag_cue[8] = {{{", ".join(f"0x{v:04x}" for v in elcue)}}};

static const uint8_t fx_tiles[{len(FXT) * 32}u] = {{
{c_bytes(fx_tile_bytes())}
}};

void mob_art_info(uint8_t look, mob_look_t *out) BANKED {{ memcpy(out, &looks[look], sizeof(mob_look_t)); }}
void mob_art_fx(uint8_t tile0) BANKED {{ set_sprite_data(tile0, {len(FXT) * 2}u, fx_tiles); }}
uint16_t mob_art_affix_col(uint8_t af) BANKED {{ return af < 6u ? affix_col[af] : tag_cue[(uint8_t)(af - 6u) & 7u]; }}
"""
    (GEN / "mob_info.c").write_text(info)
    hdr = f"""#ifndef MOB_ART_H
#define MOB_ART_H
/* generated by tools/bake_mobs.py: the office mobs' baked looks (src/mob.h) */
#include <gb/gb.h>
#include <stdint.h>

#define MOB_LOOKS {n}u
#define MOB_LOOKS_PER_FAM {LOOKS_PER_FAM}u
#define MOB_ART_SPLIT {half}u /* looks below it: mob_art_upload0, else mob_art_upload1 */
typedef struct {{
  uint8_t fam, tag, bmask; /* FAM_*, TAG_* (8 none), overlay OBJs present: bit 0/1 idle 0 left/right, 2/3 idle 1 */
  uint16_t pal[3];         /* the body ramp: dark, mid, light */
  uint16_t cue;            /* the overlay's cue colour */
  char name[14];
}} mob_look_t;
void mob_art_upload0(uint8_t look, uint8_t tile0, uint8_t recolour) BANKED;
void mob_art_upload1(uint8_t look, uint8_t tile0, uint8_t recolour) BANKED;
void mob_art_info(uint8_t look, mob_look_t *out) BANKED;
uint16_t mob_art_affix_col(uint8_t af) BANKED; /* MF_* affix colour; MF_AURA0 + tag: the tag's cue */
void mob_art_fx(uint8_t tile0) BANKED;         /* the FX and marker tiles: FXT_* pairs from tile0 */
enum {{ {", ".join(f"FXT_{n}" for n in FXT)}, FXT_COUNT }};

#endif
"""
    (GEN / "mob_art.h").write_text(hdr)


def sheet(looks, recs):
    S = 4
    cell = BOX * S + 8
    cols = len(FRAMES) * 2 + 1
    W = cols * cell + 120
    H = len(looks) * cell + 24
    im = Image.new("RGB", (W, H), (24, 18, 48))
    d = ImageDraw.Draw(im)
    floor = M.FLOOR_BASE
    void = M.VOID_TONE
    d.text((6, 6), f"B2 OFFICE MOBS  {len(looks)} LOOKS  BODY FRAMES ON FLOOR / VOID, OVERLAY ON TOP", fill=(250, 246, 255))
    for i, (L, r) in enumerate(zip(looks, recs)):
        y = 24 + i * cell
        d.text((6, y + cell // 2 - 10), r["name"], fill=(250, 246, 255))
        d.text((6, y + cell // 2 + 2), f"{L['fname']} {L['tag']}", fill=(180, 170, 210))
        pa = [None] + [((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3) for v in r["pal"]]
        cue = ((r["cue"] & 31) << 3, ((r["cue"] >> 5) & 31) << 3, ((r["cue"] >> 10) & 31) << 3)
        pb = [None, M.INK, cue, cue]
        for fi, g in enumerate(L["grids"]):
            for bg_i, bg in enumerate((floor, void)):
                x = 120 + (fi * 2 + bg_i) * cell
                d.rectangle([x, y, x + BOX * S - 1, y + BOX * S - 1], fill=bg)
                for yy in range(BOX):
                    for xx in range(BOX):
                        v = g[yy][xx]
                        c = None
                        if 1 <= v <= 3:
                            c = pa[v]
                        if fi < 3 and v in (4, 5, 6):
                            c = pb[1 if v == 4 else 2]
                        if c:
                            d.rectangle([x + xx * S, y + yy * S, x + xx * S + S - 1, y + yy * S + S - 1], fill=c)
    SHEET.parent.mkdir(parents=True, exist_ok=True)
    im.save(SHEET)


def main():
    looks = pick_looks()
    recs = [look_record(L) for L in looks]
    if "--sheet-only" not in sys.argv:
        write_c(looks, recs)
    sheet(looks, recs)
    print(f"{len(looks)} looks, {sum(len(r['tiles']) for r in recs)} tile bytes; sheet {SHEET.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
