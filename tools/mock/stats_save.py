"""STATS save layout (docs/redesign.md 9.8): the byte map, pack/unpack, and the host-side checks.

One STATS record = a 4-byte header, the lifetime fields, 4 lineage summaries of 32 bytes, a spare and a CRC-16.
Two copies (A/B) live at 0xBC00 and 0xBD80; the valid copy with the newer sequence wins, as for the meta record.
This file is the source of truth for the offset table in 9.8: `python3 tools/mock/stats_save.py --table` prints it.

    .venv/bin/python tools/mock/stats_save.py          # runs every check, prints the totals
    .venv/bin/python tools/mock/stats_save.py --table  # the markdown offset tables for the doc

Checks: field offsets are contiguous and the record is exactly 384 bytes (lifetime share 256, lineage 4 x 32);
a pack/unpack round trip over 2,000 random records; CRC-16/CCITT-FALSE against the reference vector and a 1-bit
flip; A/B selection (a torn write keeps the older copy, the sequence wraps); saturation for u8/u16/u24 adds;
the DECAY8 favourites and the TOP-4 favourite recipe against exact counts on simulated play; the 9.5 SRAM map
(contiguous, ends at 0xC000, the claimed range clear of every other row).
"""
from __future__ import annotations

import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# ------------------------------------------------------------------------------------------------ the layout
# (name, kind, count, page, note). kinds: u8 u16 u24 (saturating, little-endian), d8 (DECAY8 counter),
# d4 (DECAY4: one nibble per kind, two per byte, low nibble first), raw (opaque bytes: masks, packed bitfields).
SIZE = {"u8": 1, "u16": 2, "u24": 3, "d8": 1, "raw": 1}


def fsize(kind, n):
    return (n + 1) // 2 if kind == "d4" else SIZE[kind] * n
HEADER = [("magic", "raw", 2, "-", "'S','T'"), ("version", "u8", 1, "-", "1"),
          ("seq", "u8", 1, "-", "write sequence; the newer valid copy wins (serial compare)")]
LIFETIME = [
    # RUNS
    ("runs", "u16", 1, "RUNS", "runs started"),
    ("deaths", "u16", 1, "RUNS", "heirs lost"),
    ("egresses", "u16", 1, "RUNS", "full clears (the last floor's egress)"),
    ("deep", "u16", 1, "RUNS", "deepest: floor bits 0-5 (1..63), biome bits 6-8, spare 9-15"),
    ("deep_gen", "u16", 1, "RUNS", "the generation (in its line) of that deepest run"),
    ("heirs", "u16", 1, "RUNS", "generations played, all lines"),
    ("lines", "u8", 1, "RUNS", "lines founded"),
    ("long_life", "u24", 1, "RUNS", "longest-lived heir, minutes (birth stamp to death)"),
    ("long_name", "u8", 1, "RUNS", "that heir's name id"),
    ("long_line", "u16", 1, "LINEAGE", "most generations in one line"),
    ("play_s", "u24", 1, "RUNS", "total play time, seconds (194 days)"),
    ("fast_biome", "u16", 1, "RUNS", "fastest biome clear, seconds (0 = none)"),
    ("fast_biome_id", "u8", 1, "RUNS", "its biome (bits 0-2); bits 3-7 spare"),
    ("fast_egress", "u24", 1, "RUNS", "fastest whole run to the egress, seconds (0 = none)"),
    # COMBAT
    ("kills", "u24", 1, "COMBAT", "all kills"),
    ("kills_fam", "u16", 8, "COMBAT", "kills by mob family (one per biome, 7.1)"),
    ("elites", "u16", 1, "COMBAT", "elites killed"),
    ("boss_n", "u8", 8, "COMBAT", "each biome's boss slot: times beaten"),
    ("boss_gen", "u8", 8, "COMBAT", "generation of the first kill (0 never, 255 = 255+)"),
    ("dmg_out", "u24", 1, "COMBAT", "damage dealt"),
    ("dmg_in", "u16", 1, "COMBAT", "damage taken, half hearts"),
    ("parries", "u16", 1, "COMBAT", "perfect parries"),
    ("lunges", "u16", 1, "COMBAT", "dodge-cancel lunges (B then A)"),
    ("k_crush", "u16", 1, "COMBAT", "kills by crushing"),
    ("k_void", "u16", 1, "COMBAT", "kills by the void (shoved or dropped)"),
    ("k_react", "u16", 1, "COMBAT", "kills by a reaction"),
    ("nohit", "u16", 1, "COMBAT", "longest no-hit streak, kills in a row"),
    # PUZZLES
    ("solved", "u16", 1, "PUZZLES", "puzzle rooms solved"),
    ("forgets", "u16", 1, "PUZZLES", "THE ROOM FORGETS resets taken"),
    ("glass", "u16", 1, "PUZZLES", "glass broken (floor glass and partitions)"),
    ("collapsed", "u24", 1, "PUZZLES", "tiles collapsed under or behind you"),
    ("pushed", "u24", 1, "PUZZLES", "block pushes"),
    ("outwit", "u16", 1, "PUZZLES", "rooms with a living obstacle solved (7.7)"),
    ("trick_best", "u8", 14, "PUZZLES", "fastest solve per trick type 1-14, seconds (0 never, 255 = 4:15+)"),
    # BENCH
    ("mixes", "u24", 1, "BENCH", "recipes made"),
    ("known", "raw", 5, "BENCH", "recipes ever discovered: 38-bit mask (bits 38-39 spare)"),
    ("fav", "d4", 38, "BENCH", "favourite recipe: DECAY4, a nibble per recipe (halve all at 15)"),
    ("reacts", "u16", 12, "BENCH", "reactions triggered, by the 12 cause reactions (12b)"),
    ("tags", "d8", 8, "BENCH", "element use by tag (HOT COLD WET SHINY ALIVE MAGIC GLITCH STONE)"),
    # TREASURE
    ("t_found", "u16", 1, "TREASURE", "patches found"),
    ("t_seen", "u16", 1, "TREASURE", "patches offered (the X of 'FOUND 2 OF X')"),
    ("t_hidden", "u16", 1, "TREASURE", "hidden patches found"),
    ("t_cursed", "u16", 1, "TREASURE", "cursed items taken"),
    ("tok_in", "u24", 1, "TREASURE", "tokens earned"),
    ("tok_out", "u24", 1, "TREASURE", "tokens spent"),
    ("trade_id", "u8", 1, "TREASURE", "best trade: the item id"),
    ("trade_gain", "u16", 1, "TREASURE", "best trade: ROM value minus the price paid, tokens"),
    # LINEAGE (with the matrix)
    ("traits", "d8", 10, "LINEAGE", "expressed traits of played heirs, DECAY8 (9.2 order)"),
    ("dodges", "d8", 3, "LINEAGE", "dodge styles of played heirs, DECAY8 (ROLL SLIDE BLINK)"),
    ("rare_mut", "u8", 1, "LINEAGE", "rarest mutation seen: bits 0-3 trait, 4-7 rarity rank (lower is rarer)"),
    ("rare_gen", "u16", 1, "LINEAGE", "the generation it appeared in"),
    ("ghosts", "u16", 1, "LINEAGE", "ancestors met as ghosts or bodies"),
    ("faction_pk", "raw", 6, "LINEAGE", "per faction: high nibble best standing, low nibble worst (offset 8)"),
    ("lore", "u16", 1, "LINEAGE", "lore fragments read"),
    ("glimpses", "raw", 1, "LINEAGE", "truths glimpsed: 4 axes x 2 leanings (shown as a count, never named)"),
    # SKY
    ("eclipses", "u8", 1, "SKY", "eclipse days played through"),
    ("retros", "u16", 1, "SKY", "floors cleared under a retrograde"),
    ("wishes", "u16", 1, "SKY", "meteor wishes caught"),
    ("birthdays", "u8", 1, "SKY", "birthdays celebrated (solar returns played)"),
    # ODDITIES
    ("crt_self", "u16", 1, "ODDITIES", "deaths by your own CRT"),
    ("self_deaths", "u16", 1, "ODDITIES", "deaths by your own hand (owner SELF)"),
    ("steps", "u24", 1, "ODDITIES", "steps taken (16 px cells entered)"),
    ("dozes", "u16", 1, "ODDITIES", "times the heir dozed off (30 s idle)"),
    ("falls", "u16", 1, "ODDITIES", "times you let go of the floor"),
    ("visits", "d8", 8, "ODDITIES", "floors entered per biome, DECAY8"),
    # FLAGS
    ("firsts", "raw", 4, "-", "32 first-time flags (FIRSTS below)"),
    ("unseen", "raw", 4, "-", "32 NEW-record bits: set at commit, cleared when that page is viewed (they blink)"),
]
LIN_SLOTS = 4
LINEAGE_SLOT = [
    ("line_no", "u8", 1, "line number (1, 2, 3...)"),
    ("founder", "u8", 1, "founder's name id"),
    ("founded", "u16", 1, "founded: days since 2000-01-01 (the kept clock)"),
    ("state", "u8", 1, "bit 0 used, bit 1 closed (NO ONE CAME AFTER), bits 2-5 the line's kind"),
    ("gens", "u16", 1, "generations"),
    ("runs", "u16", 1, "runs"),
    ("egresses", "u8", 1, "full clears"),
    ("deep", "u16", 1, "deepest floor and biome (the lifetime 'deep' packing)"),
    ("kills", "u24", 1, "kills"),
    ("bosses", "raw", 1, "biome boss slots beaten (8-bit mask)"),
    ("solved", "u16", 1, "puzzle rooms solved"),
    ("mixes", "u16", 1, "recipes made"),
    ("t_found", "u16", 1, "treasure found"),
    ("tok_in", "u24", 1, "tokens earned"),
    ("long_life", "u24", 1, "longest-lived heir, minutes"),
    ("long_name", "u8", 1, "that heir's name id"),
    ("ghosts", "u8", 1, "ancestors met as ghosts or bodies"),
    ("muts", "raw", 1, "mutations (low nibble) and glitch births (high nibble), saturating at 15"),
    ("dominant", "raw", 1, "most common trait (bits 0-3, 0 none) and dodge style (bits 4-5); recounted from the tree ring at each pick"),
]
RECORD = 384
FIRSTS = ["egress", "elite", "perfect parry", "boss (any)", "all 8 bosses", "egress with no hits taken",
          "38 of 38 recipes", "solar eclipse", "lunar eclipse", "meteor wish", "birthday", "HALLEY (you were there)",
          "HALLEY'S TAIL kept", "glitch birth", "mutation", "SKYBORN heir", "a line of 10", "a line of 30",
          "every biome entered", "a cursed item", "a ghost met", "own CRT death", "a doze", "100 floors cleared"]
UNSEEN = ["deepest", "fastest biome", "fastest egress", "longest life", "longest line", "no-hit streak",
          "best trade", "rarest mutation", "new recipe", "boss first kill"] + [f"trick {i} best" for i in range(1, 15)]


def layout(fields, start):
    rows, at = [], start
    for f in fields:
        name, kind, n = f[0], f[1], f[2]
        size = fsize(kind, n)
        rows.append((at, size, *f))
        at += size
    return rows, at


HDR_ROWS, a = layout(HEADER, 0)
LIFE_ROWS, LIFE_END = layout(LIFETIME, a)
LIN_START = LIFE_END
SLOT_ROWS, SLOT = layout([(n, k, c, "-", note) for (n, k, c, note) in LINEAGE_SLOT], 0)
LIN_END = LIN_START + LIN_SLOTS * SLOT
CRC_AT = RECORD - 2
SPARE = CRC_AT - LIN_END

# ------------------------------------------------------------------------------------------------ SRAM (9.5)
STATS_A, STATS_B = 0xBC00, 0xBC00 + RECORD
SRAM = [  # the 9.5 map after B3: (start, size, content, owner)
    (0xA000, 32, "meta record A", "-"), (0xA020, 32, "meta record B", "-"),
    (0xA040, 256, "mid-run continue record A", "-"), (0xA140, 256, "mid-run continue record B", "-"),
    (0xA240, 128, "sky event scenes (19)", "-"), (0xA2C0, 240, "free", "run50 may claim"),
    (0xA3B0, 64, "inheritance", "-"), (0xA3F0, 128, "seen history", "-"),
    (0xA470, 256, "book (best tallies, recipe margins); the stats half moved to STATS", "-"),
    (0xA570, 1276, "tree copy A", "-"), (0xAA6C, 1276, "tree copy B", "-"),
    (0xAF68, 1368, "free", "run50 may claim"),
    (0xB4C0, 32, "kept clock A/B", "-"), (0xB4E0, 16, "HOST CLOCK block", "-"), (0xB4F0, 16, "RTC block", "-"),
    (0xB500, 576, "face generator base layer", "-"),
    (0xB740, 0xBC00 - 0xB740, "free", "run50 may claim"),
    (STATS_A, RECORD, "STATS record A (B3, 9.8)", "B3"), (STATS_B, RECORD, "STATS record B (B3, 9.8)", "B3"),
    (0xBF00, 0xFF, "free", "-"),
    (0xBFFF, 1, "reserved: RTC access byte", "-"),
]


# ------------------------------------------------------------------------------------------------ primitives
def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE, bit by bit: the meta record's egc_crc16 (src/core/eg_clock.c), with a 16-bit length"""
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


MAX = {"u8": 0xFF, "u16": 0xFFFF, "u24": 0xFFFFFF}


def sat_add(v, d, kind):
    """saturating add: never wraps, never goes below 0"""
    return max(0, min(MAX[kind], v + d))


def d8_bump(counts, i, by=1):
    """DECAY8: n u8 counters; when the bumped one would pass 255, every counter is halved first.
    The argmax is the favourite. Ranks survive the halving (ties can appear, the lower index wins)."""
    for _ in range(by):
        if counts[i] == 255:
            for k in range(len(counts)):
                counts[k] >>= 1
        counts[i] += 1


def d8_fav(counts):
    m = max(counts)
    return None if m == 0 else counts.index(m)


def d4_bump(counts, i):
    """DECAY4: the same rule with 4-bit counters (halve every nibble when the bumped one would pass 15)"""
    if counts[i] == 15:
        for k in range(len(counts)):
            counts[k] >>= 1
    counts[i] += 1


def ss_fav(k, stream):
    """Space-Saving top-k (the rejected alternative, kept for the comparison): k (id, n) slots"""
    slots = [[0xFF, 0] for _ in range(k)]
    for x in stream:
        for s in slots:
            if s[0] == x:
                s[1] += 1
                break
        else:
            j = min(range(k), key=lambda q: (slots[q][0] != 0xFF, slots[q][1]))
            slots[j] = [x, slots[j][1] + 1 if slots[j][0] != 0xFF else 1]
    return max(slots, key=lambda s: s[1])[0]


# ------------------------------------------------------------------------------------------------ pack / unpack
def _put(buf, at, kind, v):
    if kind in ("u8", "d8", "raw"):
        buf[at] = v
    elif kind == "u16":
        buf[at:at + 2] = v.to_bytes(2, "little")
    elif kind == "u24":
        buf[at:at + 3] = v.to_bytes(3, "little")


def _get(buf, at, kind):
    if kind in ("u8", "d8", "raw"):
        return buf[at]
    return int.from_bytes(buf[at:at + SIZE[kind]], "little")


def blank():
    life = {}
    for (_, _, name, kind, n, *_r) in LIFE_ROWS:
        life[name] = [0] * n if n > 1 else 0
    lines = [{name: 0 for (_, _, name, *_r) in SLOT_ROWS} for _ in range(LIN_SLOTS)]
    return {"seq": 0, "life": life, "lines": lines}


def pack(rec) -> bytes:
    b = bytearray(RECORD)
    b[0:2] = b"ST"
    b[2] = 1
    b[3] = rec["seq"] & 0xFF
    for (at, size, name, kind, n, *_r) in LIFE_ROWS:
        v = rec["life"][name]
        if kind == "d4":
            for i, x in enumerate(v):
                b[at + i // 2] |= (x & 15) << (4 * (i & 1))
            continue
        vals = v if n > 1 else [v]
        for i, x in enumerate(vals):
            _put(b, at + i * SIZE[kind], kind, x)
    for s, line in enumerate(rec["lines"]):
        base = LIN_START + s * SLOT
        for (at, size, name, kind, n, *_r) in SLOT_ROWS:
            _put(b, base + at, kind, line[name])
    b[CRC_AT:CRC_AT + 2] = crc16(bytes(b[:CRC_AT])).to_bytes(2, "little")
    return bytes(b)


def valid(b: bytes) -> bool:
    return b[0:2] == b"ST" and b[2] == 1 and int.from_bytes(b[CRC_AT:], "little") == crc16(b[:CRC_AT])


def unpack(b: bytes):
    assert valid(b)
    rec = blank()
    rec["seq"] = b[3]
    for (at, size, name, kind, n, *_r) in LIFE_ROWS:
        if kind == "d4":
            rec["life"][name] = [(b[at + i // 2] >> (4 * (i & 1))) & 15 for i in range(n)]
            continue
        vals = [_get(b, at + i * SIZE[kind], kind) for i in range(n)]
        rec["life"][name] = vals if n > 1 else vals[0]
    for s in range(LIN_SLOTS):
        base = LIN_START + s * SLOT
        for (at, size, name, kind, n, *_r) in SLOT_ROWS:
            rec["lines"][s][name] = _get(b, base + at, kind)
    return rec


def pick(a: bytes, b: bytes):
    """A/B: the valid copy wins; both valid -> the newer sequence (serial compare, wraps at 256)"""
    va, vb = valid(a), valid(b)
    if va and vb:
        return "A" if ((a[3] - b[3]) & 0xFF) < 0x80 else "B"
    return "A" if va else "B" if vb else None


# ------------------------------------------------------------------------------------------------ WRAM
WRAM = [  # (name, bytes, note): the stage accumulator, committed at stage end or death
    ("d_kills", 1, "kills this stage"), ("d_fam", 8, "kills by family"), ("d_elites", 1, ""),
    ("d_boss", 1, "biome boss slot beaten (bit 7) + id"), ("d_dmg_out", 2, ""), ("d_dmg_in", 1, "half hearts"),
    ("d_parries", 1, ""), ("d_lunges", 1, ""), ("d_kcause", 3, "crush, void, reaction kills"),
    ("nohit_cur", 2, "the live streak (run-long; reset on a hit)"),
    ("d_solved", 1, ""), ("d_forgets", 1, ""), ("d_glass", 1, ""), ("d_collapsed", 2, ""), ("d_pushed", 1, ""),
    ("d_outwit", 1, ""), ("room_t0", 2, "the room's entry frame stamp (from the kept clock)"),
    ("d_mixes", 1, ""), ("mix_log", 8, "recipe ids made this stage, bumped into DECAY4 at commit (a 9th mix still counts in MIXES)"),
    ("d_reacts", 12, "per reaction"), ("d_tags", 8, "per tag"),
    ("d_treasure", 4, "found, seen, hidden, cursed"), ("d_tokens", 4, "earned u16, spent u16"),
    ("d_trade", 3, "best trade this stage"), ("d_ghosts", 1, ""), ("d_steps", 2, ""), ("d_dozes", 1, ""),
    ("d_sky", 1, "eclipse, retrograde, wish, birthday bits this stage"), ("stage_t0", 3, "stage entry frame stamp"),
    ("bests", 25, "bests cache, loaded at run start, updated live, written back at commit: deep floor 1, no-hit 2, trick bests 14, fastest biome 2, known-recipe mask 5, toast-fired flags 1"),
    ("toast", 6, "queue of 4 record ids + timer + state"),
]


# ------------------------------------------------------------------------------------------------ checks
def check_layout():
    assert RECORD == 384
    assert LIFE_END <= 252, LIFE_END  # header + lifetime fields stay inside the 256-byte lifetime share
    assert SLOT == 32, SLOT
    assert LIN_END <= CRC_AT
    at = 0
    for (o, size, *_r) in HDR_ROWS + LIFE_ROWS:
        assert o == at
        at += size
    assert len(FIRSTS) <= 32 and len(UNSEEN) <= 32
    return {"record": RECORD, "header": 4, "lifetime_fields": LIFE_END - 4, "lifetime_share": LIFE_END + 2 + SPARE,
            "lineage_slot": SLOT, "lineage_slots": LIN_SLOTS, "lineage_total": LIN_SLOTS * SLOT, "spare": SPARE,
            "crc_at": CRC_AT, "both_copies": 2 * RECORD}


def check_crc():
    assert crc16(b"123456789") == 0x29B1  # the CCITT-FALSE check value
    r = blank()
    b = bytearray(pack(r))
    assert valid(bytes(b))
    flips = 0
    for i in range(0, CRC_AT, 7):
        for bit in (0, 3, 7):
            b[i] ^= 1 << bit
            flips += not valid(bytes(b))
            b[i] ^= 1 << bit
    tested = len(range(0, CRC_AT, 7)) * 3
    assert flips == tested
    return {"check_value": "0x29B1", "single_bit_flips_caught": f"{flips}/{tested}"}


def rand_rec(rng):
    r = blank()
    r["seq"] = rng.randrange(256)
    for (_, _, name, kind, n, *_x) in LIFE_ROWS:
        top = 15 if kind == "d4" else MAX.get(kind, 255)
        vals = [rng.randrange(top + 1) for _ in range(n)]
        r["life"][name] = vals if n > 1 else vals[0]
    for line in r["lines"]:
        for (_, _, name, kind, *_x) in SLOT_ROWS:
            line[name] = rng.randrange(MAX.get(kind, 255) + 1)
    return r


def check_roundtrip(n=2000):
    rng = random.Random(9)
    for _ in range(n):
        r = rand_rec(rng)
        assert unpack(pack(r)) == r
    return {"roundtrip_records": n}


def check_ab():
    rng = random.Random(3)
    old = rand_rec(rng)
    old["seq"] = 255
    a = pack(old)
    new = dict(old, seq=0)  # wraps: 0 is newer than 255
    b = pack(new)
    assert pick(a, b) == "B"
    torn = bytearray(b)
    torn[200:] = a[200:]  # power cut halfway through writing B: B's CRC fails, A wins
    assert pick(a, bytes(torn)) == "A"
    assert pick(bytes(RECORD), bytes(RECORD)) is None  # a blank cart: both invalid, start fresh
    return {"ab_wrap_255_to_0": "B wins", "torn_write": "A kept", "blank": "fresh record"}


def check_saturation():
    out = {}
    for kind, top in MAX.items():
        assert sat_add(top - 1, 1, kind) == top
        assert sat_add(top, 1, kind) == top
        assert sat_add(top - 3, 250, kind) == top
        assert sat_add(2, -5, kind) == 0
        out[kind] = f"{top - 1}+1={top}, {top}+1={top}, {top - 3}+250={top}"
    r = blank()  # a commit path: a u16 counter pinned at the top, packed and unpacked, stays pinned
    r["life"]["deaths"] = sat_add(0xFFFE, 9, "u16")
    r["life"]["steps"] = sat_add(0xFFFFF0, 0x40, "u24")
    r["life"]["boss_gen"][0] = sat_add(250, 40, "u8")
    u = unpack(pack(r))
    assert u["life"]["deaths"] == 0xFFFF and u["life"]["steps"] == 0xFFFFFF and u["life"]["boss_gen"][0] == 255
    out["packed"] = "deaths 0xFFFF, steps 0xFFFFFF, boss_gen 255 survive pack/unpack"
    return out


def zipf_stream(rng, n_ids, n, s=1.1, drift=0.0):
    w = [1 / (k + 1) ** s for k in range(n_ids)]
    order = list(range(n_ids))
    rng.shuffle(order)
    out = []
    for i in range(n):
        if drift and rng.random() < drift:  # tastes change: a new favourite now and then
            j = rng.randrange(n_ids)
            order[0], order[j] = order[j], order[0]
        out.append(order[rng.choices(range(n_ids), w)[0]])
    return out


def check_favourites():
    rng = random.Random(42)
    res = {}
    # DECAY8 over 8 tags: the favourite agrees with the exact count (the recent-weighted one, after halvings)
    agree = trials = 0
    for t in range(300):
        st = zipf_stream(rng, 8, rng.choice([50, 400, 3000, 20000]), s=rng.choice([0.6, 1.0, 1.5]))
        c, exact = [0] * 8, [0] * 8
        for x in st:
            d8_bump(c, x)
            exact[x] += 1
        top = max(exact)
        clear = sorted(exact)[-2] < top * 0.9  # a clear favourite (within 10% is a tie the screen may call either way)
        if clear:
            trials += 1
            agree += exact.index(top) == d8_fav(c)
    res["decay8_tags"] = f"{agree}/{trials} clear favourites right"
    # DECAY4 over the 38 recipes, against Space-Saving top-k at the same or a smaller size
    tally = {"d4 (19 B)": [0, 0], "ss k=4 (12 B)": [0, 0], "ss k=6 (18 B)": [0, 0]}
    for t in range(300):
        st = zipf_stream(rng, 38, rng.choice([30, 300, 3000, 30000]), s=rng.choice([0.8, 1.0, 1.3]))
        nib, exact = [0] * 38, [0] * 38
        for x in st:
            d4_bump(nib, x)
            exact[x] += 1
        top = max(exact)
        if sorted(exact)[-2] >= top * 0.9:
            continue
        want = exact.index(top)
        for k, got in (("d4 (19 B)", nib.index(max(nib))), ("ss k=4 (12 B)", ss_fav(4, st)),
                       ("ss k=6 (18 B)", ss_fav(6, st))):
            tally[k][1] += 1
            tally[k][0] += got == want
    for k, (a, n) in tally.items():
        res["recipes " + k] = f"{a}/{n} clear favourites right"
    a, n = tally["d4 (19 B)"]
    assert a / n > 0.95
    # drifting tastes: DECAY follows the new favourite (the screen should show what you make now)
    st = [0] * 400 + [5] * 60
    nib = [0] * 38
    for x in st:
        d4_bump(nib, x)
    res["recipes drift (400 x A then 60 x B)"] = "B" if nib.index(max(nib)) == 5 else "A"
    return res


def check_sram():
    at = 0xA000
    for start, size, *_ in SRAM:
        assert start == at, (hex(start), hex(at))
        at += size
    assert at == 0xC000
    used = sum(s for (_, s, c, _) in SRAM if not c.startswith("free"))
    free = sum(s for (_, s, c, _) in SRAM if c.startswith("free"))
    b3 = [(s, z) for (s, z, c, o) in SRAM if o == "B3" and not c.startswith("free")]
    assert b3 == [(0xBC00, RECORD), (0xBD80, RECORD)]
    runs = [f"{s:04X}-{s + z - 1:04X} ({z})" for (s, z, c, _) in SRAM if c.startswith("free")]
    return {"used": used, "free": free, "total": used + free, "free_runs": runs,
            "claimed": "0xBC00-0xBEFF (768)",
            "map": [f"{s:04X}-{s + z - 1:04X} {z:5d} {c}" for (s, z, c, _) in SRAM]}


def check_wram():
    return {"stage_accumulator": sum(n for (k, n, _) in WRAM if k not in ("bests", "toast")),
            "bests_cache": 25, "toast": 6, "total": sum(n for (_, n, _) in WRAM)}


def table():
    print("| Offset | Size | Field | Page | Meaning |\n| --- | --- | --- | --- | --- |")
    for (at, size, name, kind, n, page, note) in HDR_ROWS + LIFE_ROWS:
        k = f"{kind} x {n}" if n > 1 else kind
        print(f"| `0x{at:03X}` | {size} | `{name}` ({k}) | {page} | {note} |")
    print(f"| `0x{LIN_START:03X}` | {LIN_SLOTS * SLOT} | lineage slots 0-{LIN_SLOTS - 1} ({LIN_SLOTS} x {SLOT}) | LINEAGE | below |")
    if SPARE:
        print(f"| `0x{LIN_END:03X}` | {SPARE} | spare (zero) | - | |")
    print(f"| `0x{CRC_AT:03X}` | 2 | CRC-16/CCITT-FALSE, LE | - | over bytes 0x000-0x{CRC_AT - 1:03X} |")
    print("\n| Offset | Size | Field | Meaning |\n| --- | --- | --- | --- |")
    for (at, size, name, kind, n, page, note) in SLOT_ROWS:
        print(f"| {at} | {size} | `{name}` ({kind}) | {note} |")
    print("\n| WRAM | Bytes | Note |\n| --- | --- | --- |")
    for (k, n, note) in WRAM:
        print(f"| `{k}` | {n} | {note} |")


def main():
    if "--table" in sys.argv:
        table()
        return
    rep = {"layout": check_layout(), "crc": check_crc(), "roundtrip": check_roundtrip(), "ab": check_ab(),
           "saturation": check_saturation(), "favourites": check_favourites(), "sram": check_sram(),
           "wram": check_wram()}
    out = ROOT / "docs/shots/stats_report.json"
    out.write_text(json.dumps(rep, indent=1))
    for k, v in rep.items():
        print(k, json.dumps(v) if k != "sram" else {x: v[x] for x in v if x != "map"})
    print("ALL STATS SAVE CHECKS PASS")


if __name__ == "__main__":
    main()
