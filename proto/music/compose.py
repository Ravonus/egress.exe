"""EGM composer: `python compose.py OUT_HEADER` writes egm_data.h for egm.c (the contract is egm_types.h).

What is authored here, and what the cartridge decides:
  authored  per mood: tempo, mode, chord progressions (one chord a bar, four bars a phrase), a phrase form (A A B A),
            grooves per layer in a low and a high tier, lead motifs written as chord tones and scale degrees, an
            answer rhythm for the biome motif, and which layers play at each intensity.
            per biome: the key, the wave, the instrument for each slot, the drum kit, panning, echo, a 4-note motif.
  cartridge the seed picks a progression, a groove and a lead motif for each phrase letter, and transforms (answer
            the biome motif in bar 4, thin, hush, an octave lift); A' flips the answer; choices re-roll every cycle;
            notes are resolved against the key, the mode and the bar's chord at play time, so every combination
            of mood x biome x seed stays diatonic and held notes always land on chord tones.

Style rules (the owner's taste: warm ambient chiptune, never harsh, not "bleepy"): duties 25 % and 50 % only, pulse
peaks <= 8 before volume scaling, soft swells and plucks with release tails, a sine-family wave bass, noise only from
low LFSR clocks (no hiss above ~8 kHz) at levels <= 6, tempos 69-149 BPM, a lead that rests.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

FRAME_HZ = 4194304 / 70224
LENS = [1, 2, 3, 4, 6, 8, 12, 16]
MODES = {
    "ionian": [0, 2, 4, 5, 7, 9, 11], "dorian": [0, 2, 3, 5, 7, 9, 10], "phrygian": [0, 1, 3, 5, 7, 8, 10],
    "lydian": [0, 2, 4, 6, 7, 9, 11], "mixolydian": [0, 2, 4, 5, 7, 9, 10], "aeolian": [0, 2, 3, 5, 7, 8, 10],
}
MODE_IDS = list(MODES)
SUS4, SUS2, ADD9 = 0x08, 0x10, 0x20
PAD, ARP, LEAD, BELL = 0, 1, 2, 3
SWING, PANFLIP = 1, 2
STUTTER, OCTAVE = 1, 2

# ---- instruments: duty, peak, att, dec, rel, vib (see egm_types.h) ----
INSTS = {
    "pad_soft":  (2, 5, 3, 0, 6, 0x00),
    "pad_glass": (2, 5, 5, 0, 7, 0x81),
    "arp_pluck": (2, 5, 0, 3, 2, 0x00),
    "arp_25":    (1, 4, 0, 2, 2, 0x00),
    "arp_soft":  (2, 5, 1, 4, 3, 0x00),
    "lead_warm": (2, 6, 1, 0, 5, 0x61),
    "lead_hollow": (2, 6, 1, 6, 4, 0x81),
    "lead_bright": (1, 5, 1, 0, 3, 0x61),
    "lead_dream": (2, 6, 4, 0, 7, 0x41),
    "bell":      (2, 6, 0, 4, 3, 0x00),
}
INST_IDS = list(INSTS)


def wave(fn) -> list[int]:
    v = [fn(2 * math.pi * k / 32) for k in range(32)]
    lo, hi = min(v), max(v)
    return [max(0, min(15, round((x - lo) / (hi - lo) * 15))) for x in v]


WAVES = {
    "round": wave(lambda t: math.sin(t) + 0.25 * math.sin(2 * t)),
    "sine": wave(math.sin),
    "hollow": wave(lambda t: math.sin(t) + 0.18 * math.sin(3 * t)),
    "organ": wave(lambda t: math.sin(t) + 0.3 * math.sin(2 * t) + 0.12 * math.sin(3 * t)),
    "tri": wave(lambda t: 2 / math.pi * math.asin(math.sin(t))),
    "glass": wave(lambda t: math.sin(t) + 0.1 * math.sin(4 * t)),
    "stepped": [round(round((math.sin(2 * math.pi * k / 32) + 1) * 2.5) / 5 * 15) for k in range(32)],
}
WAVE_IDS = list(WAVES)

# ---- drum kits: four sounds each, (NR42, NR43). NR43 = shift<<4 | width<<3 | ratio; the LFSR clocks at
# 262144 / (ratio or 0.5) / 2^shift Hz. Every sound here clocks at <= 13 kHz (most well under 5 kHz). ----
KITS = {
    "paper":  [(0x21, 0x45), (0x42, 0x55), (0x61, 0x77), (0x24, 0x46)],
    "air":    [(0x21, 0x45), (0x37, 0x66), (0x51, 0x77), (0x21, 0x5b)],
    "drip":   [(0x31, 0x35), (0x32, 0x55), (0x51, 0x76), (0x24, 0x44)],
    "hum":    [(0x31, 0x44), (0x52, 0x54), (0x61, 0x77), (0x21, 0x4a)],
    "brush":  [(0x31, 0x44), (0x43, 0x56), (0x51, 0x77), (0x22, 0x35)],
    "breath": [(0x21, 0x46), (0x27, 0x67), (0x41, 0x77), (0x17, 0x57)],
    "wind":   [(0x21, 0x35), (0x37, 0x56), (0x51, 0x77), (0x27, 0x46)],
    "static": [(0x31, 0x45), (0x42, 0x55), (0x61, 0x77), (0x21, 0x5b)],
}
KIT_IDS = list(KITS)

# ---- biomes ----
NOTE = {"C": 0, "C#": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}
BIOMES = [
    # name, key, wave, (pad, arp, lead, bell), kit, NR51, echo rows, flags, lead_up, motif (key degrees)
    ("office", "D", "round", ("pad_soft", "arp_pluck", "lead_warm", "bell"), "paper", 0xFF, 3, 0, 0, (4, 2, 1, 0)),
    ("vents", "E", "hollow", ("pad_soft", "arp_soft", "lead_hollow", "bell"), "air", 0x7F, 4, 0, 0, (0, 1, 4, 3)),
    ("atrium", "B", "sine", ("pad_glass", "arp_pluck", "lead_warm", "bell"), "drip", 0xFF, 5, 0, 0, (2, 4, 5, 4)),
    ("server", "C", "organ", ("pad_soft", "arp_25", "lead_warm", "bell"), "hum", 0xFF, 2, 0, 0, (0, 0, 4, 2)),
    ("mall", "G", "round", ("pad_soft", "arp_pluck", "lead_bright", "bell"), "brush", 0xFF, 0, SWING, 0, (4, 5, 4, 2)),
    ("dream", "Eb", "glass", ("pad_glass", "arp_soft", "lead_dream", "bell"), "breath", 0xFF, 6, 0, 0, (0, 4, 6, 5)),
    ("rooftop", "A", "tri", ("pad_soft", "arp_pluck", "lead_warm", "bell"), "wind", 0x7F, 4, 0, 0, (0, 4, 7, 6)),
    ("glitch", "F#", "stepped", ("pad_soft", "arp_25", "lead_hollow", "bell"), "static", 0xFF, 3, PANFLIP, 0, (0, 1, 0, 6)),
]
BIOME_IDS = [b[0] for b in BIOMES]


# ---- pattern notation: "row:spec/len" tokens. spec: tN chord tone, rN degree from the chord root, kN key degree,
# mN biome motif note, '.' off, nN noise kit sound; '!' accents. ----
def parse(text: str | None) -> bytes | None:
    if text is None:
        return None
    out, last = [], -1
    for tok in text.split():
        row_s, rest = tok.split(":")
        row = int(row_s)
        acc = rest.endswith("!")
        rest = rest.rstrip("!")
        spec, _, ln = rest.partition("/")
        ln_i = int(ln) if ln else 1
        if ln_i not in LENS:
            raise ValueError(f"{tok}: length {ln_i}")
        if not 0 <= row <= 15 or row <= last:
            raise ValueError(f"{text}: rows must rise in 0..15")
        last = row
        k, v = spec[0], spec[1:]
        if k == "t":
            b1 = int(v)
            assert 0 <= b1 < 64
        elif k == "r":
            b1 = 0x40 | (int(v) + 8)
        elif k == "k":
            b1 = 0x80 | (int(v) + 8)
        elif k == "m":
            b1 = 0xC0 | (int(v) + 2)
        elif k == ".":
            b1 = 0xC0
        elif k == "n":
            b1 = int(v)
        else:
            raise ValueError(tok)
        b0 = row | (LENS.index(ln_i) << 4) | (0x80 if acc else 0)
        if b0 == 0xFF:
            raise ValueError(f"{tok}: reserved")
        out += [b0, b1]
    return bytes(out + [0xFF])


def arp(seq: list[int], step: int = 1, ln: int | None = None, start: int = 0) -> str:
    """chord tones in a row, one every `step` rows"""
    ln = ln or step
    return " ".join(f"{start + i * step}:t{t}/{ln}" for i, t in enumerate(seq) if start + i * step < 16)


def same(p: str | None, last: str | None = "same") -> list[str | None]:
    return [p, p, p, p if last == "same" else last]


class Song:
    def __init__(self, name, when, fpr, mode, *, flags=0, bass_lvl=2, base=(12, 36, 12), masks, hi_from, form,
                 progs, pad, lead, bass, drums, motif, lead_max=50):
        self.__dict__.update(locals())
        del self.__dict__["self"]


def C(deg: int, *ext: int) -> int:
    v = deg
    for e in ext:
        v |= e
    return v


# ---- the moods ----
SONGS: list[Song] = []

# CALM LANDING: you wake somewhere quiet. Ionian, 75 BPM, swells, a few long lead notes.
SONGS.append(Song(
    "calm", "landing on a new floor, safe rooms, traders", 12, "ionian", lead_max=47, bass_lvl=1,
    masks=(0b0101, 0b0111, 0b1111, 0b1111), hi_from=(2, 9, 2, 3), form=(0, 0, 1, 0),
    progs=[(C(0), 5, 3, C(4, SUS4)), (C(0, ADD9), 2, 3, 0), (5, 3, 0, C(4, SUS4)), (3, 4, 2, 5)],
    pad=([(PAD, same("0:t0/8 8:t2/8")), (PAD, same("0:t1/12 12:t2/4")),
          (PAD, ["0:t0/8 8:t2/8", "0:t0/6 6:t1/4 10:t2/6", "0:t0/8 8:t2/8", "0:t1/16"])],
         [(ARP, same(arp([0, 1, 2, 3, 4, 3, 2, 1], 2))), (ARP, same(arp([0, 2, 4, 2, 1, 2, 3, 2], 2)))]),
    lead=([(LEAD, ["2:t2/6 8:r1/2 10:t1/6", "0:t0/12", "4:t1/4 8:t2/8", "0:t1/16"]),
           (LEAD, ["0:t4/6 6:t3/2 8:t2/8", "8:t1/8", "0:t2/4 4:t1/4 8:t0/8", "4:r1/12"]),
           (LEAD, ["4:t2/12", None, "0:t1/6 6:r2/2 8:t2/8", "0:t0/16"])], None),
    bass=([(PAD, same("0:t0/16")), (PAD, same("0:t0/8 8:t2/8"))], [(PAD, same("0:t0/8 8:t0/4 12:t2/4"))]),
    drums=([(PAD, same("4:n0 12:n1", "4:n0 12:n1 14:n0"))], [(PAD, same("4:n0 10:n0 12:n1"))]),
    motif="0:m0/4 4:m1/4 8:m2/2 10:m3/6"))

# UNEASY EXPLORE: dorian, 90 BPM, a moving arpeggio with a 3+3+2 lilt, questions in the lead.
SONGS.append(Song(
    "explore", "exploring a floor", 10, "dorian", flags=OCTAVE, lead_max=48,
    masks=(0b0101, 0b1111, 0b1111, 0b1111), hi_from=(1, 0, 2, 2), form=(0, 1, 0, 2),
    progs=[(0, 6, 3, 0), (0, 2, 6, 3), (0, 4, C(3, SUS2), 6), (3, 0, 6, C(4, SUS4))],
    pad=([(PAD, same("0:t0/8 8:t1/8")), (PAD, same("0:t2/16"))],
         [(ARP, same(arp([0, 2, 1, 2, 3, 2, 1, 2], 2))), (ARP, same("0:t0/3 3:t2/3 6:t1/2 8:t4/3 11:t2/3 14:t1/2"))]),
    lead=([(LEAD, ["0:t0/3 3:r1/3 6:t1/6", "4:t2/4 8:r3/2 10:t2/6", "0:t1/8 8:r1/4 12:t0/4", "2:t0/12"]),
           (LEAD, ["6:t2/2 8:t1/8", "0:r-1/4 4:t0/12", None, "0:t1/3 3:t2/3 6:t3/6 12:t2/4"]),
           (LEAD, ["0:t4/4 4:t3/4 8:t2/8", "0:t1/16", "4:t2/2 6:t1/2 8:t0/8", "0:r-1/16"])], None),
    bass=([(PAD, same("0:t0/8 8:t0/8"))], [(PAD, same("0:t0/6 6:t0/2 8:t2/6 14:t4/2"))]),
    drums=([(PAD, same("2:n0 6:n0 10:n0 14:n0"))],
           [(PAD, same("0:n2 2:n0 6:n0 8:n2 10:n0 12:n1 14:n0", "0:n2 2:n0 6:n0 8:n2 10:n0 12:n1 13:n1 14:n1"))]),
    motif="0:m0/2 2:m1/2 4:m2/4 8:m3/8"))

# TENSION: aeolian, 82 BPM, a heartbeat bass, long sus pads, a lead that hardly speaks.
SONGS.append(Song(
    "tension", "something is near; a timer; a dark room", 11, "aeolian", lead_max=48,
    masks=(0b0101, 0b1111, 0b1111, 0b1111), hi_from=(2, 0, 9, 2), form=(0, 0, 1, 1),
    progs=[(C(0, SUS2), 0, 5, 5), (0, 6, 5, C(4, SUS4)), (C(0, SUS2), 3, 0, 6), (5, 6, C(0, SUS2), 0)],
    pad=([(PAD, ["0:t0/16", "0:t2/16", "0:t0/16", "0:t1/16"]), (PAD, same("0:t2/12 12:t1/4"))],
         [(ARP, same(arp([2, 0, 2, 0, 2, 0, 3, 0], 2)))]),
    lead=([(LEAD, ["8:t2/8", "0:r1/4 4:t2/12", None, "8:t0/8"]),
           (LEAD, [None, "0:t4/12 12:t3/4", "0:t2/16", None])], None),
    bass=([(PAD, same("0:t0/2 3:t0/6"))], None),
    drums=([(PAD, same("0:n2 3:n2 8:n0"))], [(PAD, same("0:n2 3:n2 8:n0 12:n0 14:n0"))]),
    motif="0:m0/6 6:m1/2 8:m2/8"))

# COMBAT: aeolian, 112 BPM, a plucked 16th arpeggio, a pumping octave bass, a soft kit.
SONGS.append(Song(
    "combat", "enemies awake", 8, "aeolian", lead_max=50,
    masks=(0b1111, 0b1111, 0b1111, 0b1111), hi_from=(2, 0, 2, 2), form=(0, 0, 1, 0),
    progs=[(0, 5, 6, 0), (0, 6, 5, 6), (0, 3, C(4, SUS4), 4), (5, 6, 0, C(0, SUS4))],
    pad=([(ARP, same(arp([0, 1, 2, 1, 4, 2, 1, 2], 2))), (ARP, same(arp([0, 2, 4, 2, 1, 2, 4, 2], 2)))],
         [(ARP, same(arp([0, 1, 2, 4] * 4))), (ARP, same(arp([4, 2, 1, 0] * 4)))]),
    lead=([(LEAD, ["0:t0/2 2:t0/2 4:t2/4 8:t1/2 10:t0/6", "0:t2/2 2:t3/2 4:t2/4 8:t4/8",
                   "0:t0/2 2:t0/2 4:t2/4 8:t1/2 10:t0/6", "0:t2/4 4:t1/4 8:t0/8"]),
           (LEAD, ["0:t4/6 6:t3/2 8:t2/4 12:t1/4", "0:t2/8 8:r1/2 10:t2/6", "0:t4/6 6:t3/2 8:t2/4 12:t4/4",
                   "0:t3/8 8:t2/8"]),
           (LEAD, same("0:t2/2 3:t2/2 6:t1/2 8:t0/4 12:t1/4", "0:t2/2 3:t2/2 6:t3/2 8:t4/8"))], None),
    bass=([(PAD, same("0:t0/2 2:t0/2 4:t4/2 6:t0/2 8:t0/2 10:t0/2 12:t4/2 14:t2/2"))],
          [(PAD, same("0:t0/2 2:t0/2 4:t4/2 6:t0/2 8:t0/2 10:t0/2 12:t4/2 14:t2/1 15:t4/1"))]),
    drums=([(PAD, same("0:n2 4:n1 8:n2 12:n1", "0:n2 4:n1 8:n2 12:n1 14:n1 15:n1"))],
           [(PAD, same("0:n2 2:n0 4:n1 6:n0 8:n2 10:n2 12:n1 14:n0", "0:n2 2:n0 4:n1 6:n0 8:n2 12:n1 13:n1 14:n1 15:n1"))]),
    motif="0:m0/2 2:m1/2 4:m2/2 6:m3/2 8:m2/8"))

# BOSS: phrygian (the flat second), 128 BPM, driving bass, heroic lead.
SONGS.append(Song(
    "boss", "a boss room", 7, "phrygian", flags=STUTTER, lead_max=50,
    masks=(0b1111, 0b1111, 0b1111, 0b1111), hi_from=(2, 0, 2, 2), form=(0, 0, 1, 0),
    progs=[(0, 1, 0, 6), (0, 5, 6, 1), (3, 1, 0, 0), (0, 2, 1, 0)],
    pad=([(ARP, same(arp([0, 2, 4, 2], 4)))], [(ARP, same(arp([0, 4, 2, 4, 1, 4, 2, 4] * 2)))]),
    lead=([(LEAD, ["0:t0/4 4:t1/4 8:t2/6 14:t3/2", "0:t2/8 8:t1/4 12:r-1/4", "0:t4/4 4:t3/4 8:t2/8", "0:t1/4 4:t0/12"]),
           (LEAD, ["0:t2/2 2:t2/2 4:t3/2 6:t2/2 8:t1/8", "0:t0/2 2:t0/2 4:t1/2 6:t0/2 8:r-1/8",
                   "0:t2/2 2:t2/2 4:t3/2 6:t2/2 8:t1/8", "0:t4/16"])], None),
    bass=([(PAD, same("0:t0/2 2:t4/2 4:t0/2 6:t4/2 8:t0/2 10:t4/2 12:t0/2 14:t2/2"))],
          [(PAD, same("0:t0/1 1:t0/1 2:t4/2 4:t0/1 5:t0/1 6:t4/2 8:t0/1 9:t0/1 10:t4/2 12:t0/2 14:t2/2"))]),
    drums=([(PAD, same("0:n2 4:n1 6:n2 8:n2 12:n1"))],
           [(PAD, same("0:n2 2:n0 4:n1 6:n2 8:n2 10:n0 12:n1 14:n0 15:n0", "0:n2 2:n0 4:n1 6:n2 8:n2 12:n1 13:n1 14:n1 15:n1!"))]),
    motif="0:m0/4 4:m1/4 8:m2/4 12:m3/4"))

# COLLAPSE / CHASE: aeolian, 149 BPM, no bass at all, a rising 16th arpeggio, 16th ticks.
SONGS.append(Song(
    "collapse", "the floor is falling; run", 6, "aeolian", lead_max=43, base=(12, 24, 12),
    masks=(0b1011, 0b1011, 0b1011, 0b1011), hi_from=(1, 0, 9, 1), form=(0, 1, 0, 1),
    progs=[(0, 5, 2, 6), (0, 6, 5, 4), (5, 6, 0, 0)],
    pad=([(ARP, same(arp([0, 2, 4, 2, 1, 2, 4, 2], 2)))], [(ARP, same(arp([0, 1, 2, 4, 1, 2, 4, 5, 0, 2, 4, 6, 2, 4, 5, 6])))]),
    lead=([(LEAD, ["0:t4/2 2:t3/2 4:t2/2 6:t1/2 8:t2/8", "0:t3/2 2:t2/2 4:t1/2 6:t0/2 8:t1/8",
                   "0:t4/2 2:t3/2 4:t2/2 6:t1/2 8:t2/8", "0:t2/4 4:t1/4 8:t0/8"]),
           (LEAD, ["0:t4/16", "0:t3/16", "0:t2/16", "0:t1/8 8:t2/8"])], None),
    bass=([], None),
    drums=([(PAD, same("0:n2 2:n0 4:n1 6:n0 8:n2 10:n0 12:n1 14:n0"))],
           [(PAD, same("0:n2 1:n0 2:n0 3:n0 4:n1 5:n0 6:n2 7:n0 8:n2 9:n0 10:n0 11:n0 12:n1 13:n0 14:n0 15:n0",
                       "0:n2 2:n0 4:n1 6:n2 8:n2 10:n0 12:n1 13:n1 14:n1 15:n1"))]),
    motif="0:m0/2 2:m1/2 4:m2/2 6:m3/2 8:m0/8"))

# DREAM: lydian (the raised fourth), 69 BPM, glass pads with vibrato, a long echo, breath instead of drums.
SONGS.append(Song(
    "dream", "dream rooms, the garden, memories", 13, "lydian", lead_max=48, bass_lvl=1,
    masks=(0b0001, 0b0011, 0b0111, 0b1111), hi_from=(3, 0, 9, 3), form=(0, 1, 0, 2),
    progs=[(0, 1, 0, 1), (C(0, ADD9), C(4, SUS2), 5, 1), (0, 2, 1, 0), (5, 1, 0, C(0, ADD9))],
    pad=([(PAD, same("0:t0/8 8:t2/8")), (PAD, ["0:t3/16", "0:t1/12 12:t2/4", "0:t3/16", "0:t0/16"])],
         [(ARP, same("0:t0/4 4:t2/4 8:t3/4 12:t5/4"))]),
    lead=([(LEAD, ["0:t2/12", "4:t3/12", "0:t4/8 8:t2/8", "0:t1/16"]),
           (LEAD, ["4:r3/2 6:t2/8", "0:t1/16", None, "0:t0/6 6:t1/2 8:t2/8"])], None),
    bass=([(PAD, same("0:t0/16")), (PAD, same("0:t0/8 8:t2/8"))], None),
    drums=([(PAD, same("0:n1 8:n3"))], None),
    motif="0:m0/4 4:m1/4 8:m2/4 12:m3/4"))

# GLITCH RIFT: phrygian, 100 BPM, off-beat plucks, fragments, soft noise rolls, the noise jumps sides.
SONGS.append(Song(
    "glitch", "a rift in the level; reality misbehaving", 9, "phrygian", flags=STUTTER, lead_max=48,
    masks=(0b0101, 0b1111, 0b1111, 0b1111), hi_from=(2, 0, 2, 1), form=(0, 0, 1, 0),
    progs=[(0, 1, 0, 1), (0, 6, 1, 0), (0, 0, 5, 1)],
    pad=([(PAD, same("0:t0/6 6:t1/6 12:t2/4"))],
         [(ARP, same("1:t0/1 3:t2/1 6:t1/2 9:t4/1 11:t2/1 14:t3/2")), (ARP, same("0:t0/1 3:t4/1 5:t2/1 8:t0/1 10:t3/1 13:t2/3"))]),
    lead=([(LEAD, ["0:t2/1 1:t2/1 2:t2/2 8:t1/6", "4:t0/8 12:r1/4", "0:t4/1 2:t4/1 4:t3/4 8:t2/8", "0:t1/3 6:t1/3 12:t0/4"]),
           (LEAD, ["6:t2/6 12:r-1/4", "0:t0/8", "6:t1/6 12:t2/4", "0:t0/2 2:t0/2 8:t1/8"])], None),
    bass=([(PAD, same("0:t0/6 6:t0/2 8:t0/8"))], [(PAD, same("0:t0/3 3:t0/3 6:t1/2 8:t0/8"))]),
    drums=([(PAD, same("0:n2 6:n0 10:n3 12:n1"))], [(PAD, same("0:n2 3:n0 6:n0 8:n2 10:n3 12:n1 15:n0"))]),
    motif="0:m0/1 1:m0/1 2:m1/2 4:m2/4 8:m3/8"))

# ENDING: ionian, 75 BPM, tender; the phrases end on IV or a suspended V: a question, not a cadence.
SONGS.append(Song(
    "ending", "the way out; credits", 12, "ionian", lead_max=47, bass_lvl=1,
    masks=(0b0101, 0b0111, 0b1111, 0b1111), hi_from=(2, 0, 2, 3), form=(0, 1, 0, 2),
    progs=[(0, 4, 5, 3), (3, 0, C(4, SUS4), 5), (5, 3, 0, C(4, SUS4)), (C(3, ADD9), 4, 2, 5)],
    pad=([(PAD, same("0:t0/8 8:t2/8")), (PAD, same("0:t1/12 12:t2/4"))],
         [(ARP, same(arp([0, 2, 4, 5, 6, 5, 4, 2], 2)))]),
    lead=([(LEAD, ["0:t2/4 4:t1/4 8:t2/8", "0:t3/4 4:t2/4 8:t1/8", "0:t2/4 4:t4/4 8:t3/8", "0:t2/16"]),
           (LEAD, ["4:t4/12", "0:t3/6 6:t2/2 8:t1/8", "0:t0/4 4:t1/4 8:t2/8", "4:t1/12"])], None),
    bass=([(PAD, same("0:t0/8 8:t2/8")), (PAD, same("0:t0/12 12:t2/4"))], None),
    drums=([(PAD, same("4:n1 8:n0 12:n1"))], None),
    motif="0:m0/4 4:m1/4 8:m2/4 12:m3/4"))

SONG_IDS = [s.name for s in SONGS]

# ---- effects: (frames, pitch spec, NRx2, duty) for pulse 1; (frames, NR43, NR42) for noise ----
SFX = [
    # name, prio, base pitch index, pulse steps, noise steps
    ("step", 0, 36, [], [(2, 0x55, 0x21)]),
    ("pickup", 1, 24, [(3, "t0", 0x61, 2), (3, "t2", 0x61, 2), (3, "t4", 0x71, 2), (14, "t6", 0x73, 2)], []),
    ("chime", 1, 24, [(6, "t4", 0x64, 2), (30, "t6", 0x66, 2)], []),
    ("key", 2, 24, [(4, "t0", 0x72, 2), (4, "t1", 0x72, 2), (4, "t2", 0x72, 2), (4, "t4", 0x72, 2), (24, "t5", 0x75, 2)], []),
    ("hit", 2, 24, [], [(3, 0x50, 0x71), (5, 0x62, 0x51)]),
    ("hurt", 3, 24, [(4, "t2", 0x92, 2), (4, "t1", 0x82, 2), (12, "t0", 0x74, 2)], [(4, 0x61, 0x72)]),
    ("door", 2, 24, [(6, "t0", 0x73, 2), (6, "t2", 0x73, 2), (6, "t4", 0x73, 2), (28, "t5", 0x75, 2)], []),
    ("deny", 1, 24, [(5, "t0", 0x62, 2), (9, "r-1", 0x63, 2)], []),
]
SFX_IDS = [s[0] for s in SFX]


def period(i: int) -> int:
    f = 65.406 * 2 ** (i / 12)
    return max(0, min(2047, round(2048 - 131072 / f)))


def spec_byte(spec: str) -> int:
    return parse(f"0:{spec}/1")[1]


def compile_all() -> tuple[str, dict]:
    pats: list[bytes] = []
    pat_key: dict[bytes, int] = {}
    sets: list[tuple[int, ...]] = []
    set_key: dict[tuple, int] = {}

    def pat_id(text: str | None) -> int:
        b = parse(text)
        if b is None:
            return 0xFF
        if b not in pat_key:
            pat_key[b] = len(pats)
            pats.append(b)
        assert pat_key[b] < 255
        return pat_key[b]

    def set_id(slot: int, bars: list[str | None]) -> int:
        assert len(bars) == 4
        t = (slot, *[pat_id(b) for b in bars])
        if t not in set_key:
            set_key[t] = len(sets)
            sets.append(t)
        return set_key[t]

    lines = []
    songs_c = []
    song_bytes = {}
    for si, s in enumerate(SONGS):
        nb = 0
        pools = []
        for li, layer in enumerate((s.pad, s.lead, s.bass, s.drums)):
            tiers = []
            for ti, tier in enumerate(layer):
                if tier is None or not tier:
                    tiers.append("0")
                    continue
                ids = [set_id(slot, bars) for slot, bars in tier]
                nm = f"egm_pool_{si}_{li}_{ti}"
                lines.append(f"static const uint8_t {nm}[] = {{{len(ids)}, {', '.join(map(str, ids))}}};")
                nb += 1 + len(ids)
                tiers.append(nm)
            pools.append("{" + ", ".join(tiers) + "}")
        lines.append(f"static const uint8_t egm_form_{si}[] = {{{', '.join(map(str, s.form))}}};")
        lines.append(f"static const uint8_t egm_prog_{si}[] = {{{', '.join(str(c) for p in s.progs for c in p)}}};")
        nb += len(s.form) + 4 * len(s.progs) + 24
        song_bytes[s.name] = nb
        motif = pat_id(s.motif)
        songs_c.append(
            f"  /* {s.name}: {s.when}; {round(FRAME_HZ * 60 / (4 * s.fpr))} BPM {s.mode} */\n"
            f"  {{{s.fpr}, {MODE_IDS.index(s.mode)}, {s.flags}, {s.bass_lvl}, {{{', '.join(map(str, s.base))}}}, "
            f"{{{', '.join(map(str, s.masks))}}}, {{{', '.join(map(str, s.hi_from))}}}, {len(s.form)}, {len(s.progs)}, "
            f"egm_form_{si}, egm_prog_{si}, {{{', '.join(pools)}}}, {motif}, {s.lead_max}}}")
    pat_at, flat = [], []
    for p in pats:
        pat_at.append(len(flat))
        flat += list(p)
    hdr = ["/* Generated by proto/music/compose.py; do not edit. Types: egm_types.h. */",
           "#ifndef EGM_DATA_H", "#define EGM_DATA_H", '#include "egm_types.h"']
    for i, n in enumerate(SONG_IDS):
        hdr.append(f"#define EGM_MOOD_{n.upper()} {i}u")
    for i, n in enumerate(BIOME_IDS):
        hdr.append(f"#define EGM_BIOME_{n.upper()} {i}u")
    for i, n in enumerate(SFX_IDS):
        hdr.append(f"#define EGM_SFX_{n.upper()} {i}u")
    hdr.append("static const uint16_t egm_period[72] = {" + ", ".join(str(period(i)) for i in range(72)) + "};")
    lv = [[0 if not k or not l else max(1, round(l * k / 16)) for l in range(16)] for k in range(17)]
    hdr.append("static const uint8_t egm_lv[17][16] = {" + ", ".join("{" + ", ".join(map(str, r)) + "}" for r in lv) + "};")
    hdr.append("static const uint8_t egm_mod7[64] = {" + ", ".join(str(i % 7) for i in range(64)) + "};")
    import random as _r
    rr = _r.Random(0xE6E5)
    rb = list(range(256))
    rr.shuffle(rb)
    hdr.append("static const uint8_t egm_rand[256] = {" + ", ".join(map(str, rb)) + "};")
    hdr.append("static const uint8_t egm_oct12[64] = {" + ", ".join(str(i // 7 * 12) for i in range(64)) + "};")
    hdr.append("static const uint8_t egm_modes[6][7] = {" + ", ".join("{" + ", ".join(map(str, MODES[m])) + "}" for m in MODE_IDS) + "};")
    hdr.append("static const egm_inst_t egm_insts[] = {" + ", ".join("{" + ", ".join(map(str, INSTS[n])) + "}" for n in INST_IDS) + "};")
    wb = []
    for n in WAVE_IDS:
        w = WAVES[n]
        wb.append("{" + ", ".join(f"0x{(w[2 * k] << 4) | w[2 * k + 1]:02x}" for k in range(16)) + "}")
    hdr.append("static const uint8_t egm_waves[][16] = {" + ", ".join(wb) + "};")
    hdr.append("static const uint8_t egm_kits[][4][2] = {" + ", ".join(
        "{" + ", ".join(f"{{0x{a:02x}, 0x{b:02x}}}" for a, b in KITS[k]) + "}" for k in KIT_IDS) + "};")
    bl = []
    for name, key, wv, insts, kit, pan, echo, flags, up, motif in BIOMES:
        bl.append(f"  /* {name} */ {{{NOTE[key]}, {WAVE_IDS.index(wv)}, {{{', '.join(str(INST_IDS.index(i)) for i in insts)}}}, "
                  f"{KIT_IDS.index(kit)}, 0x{pan:02x}, {echo}, {flags}, {up}, {{{', '.join(map(str, motif))}}}}}")
    hdr.append("static const egm_biome_t egm_biomes[] = {\n" + ",\n".join(bl) + "};")
    hdr.append("static const uint16_t egm_pat_at[] = {" + ", ".join(map(str, pat_at)) + "};")
    hdr.append("static const uint8_t egm_pat[] = {" + ", ".join(map(str, flat)) + "};")
    hdr.append("static const uint8_t egm_sets[][5] = {" + ", ".join("{" + ", ".join(map(str, t)) + "}" for t in sets) + "};")
    hdr += lines
    hdr.append("static const egm_song_t egm_songs[] = {\n" + ",\n".join(songs_c) + "};")
    for name, prio, base, p1, nz in SFX:
        if p1:
            b = [x for f, sp, env, duty in p1 for x in (f, spec_byte(sp), env, duty)] + [0]
            hdr.append(f"static const uint8_t egm_sfx_{name}_p1[] = {{{', '.join(map(str, b))}}};")
        if nz:
            b = [x for f, nr43, env in nz for x in (f, nr43, env, 0)] + [0]
            hdr.append(f"static const uint8_t egm_sfx_{name}_nz[] = {{{', '.join(map(str, b))}}};")
    hdr.append("static const egm_sfx_t egm_sfxs[] = {" + ", ".join(
        f"{{{prio}, {base}, {f'egm_sfx_{n}_p1' if p1 else '0'}, {f'egm_sfx_{n}_nz' if nz else '0'}}}"
        for n, prio, base, p1, nz in SFX) + "};")
    hdr.append("#endif")
    sizes = {
        "patterns": len(flat) + 2 * len(pat_at), "pattern_count": len(pats), "sets": 5 * len(sets),
        "songs": sum(song_bytes.values()), "biomes": 14 * len(BIOMES), "instruments": 6 * len(INSTS),
        "waves": 16 * len(WAVES), "kits": 8 * len(KITS), "tables": 144 + 42 + 272 + 128 + 256,
        "sfx": sum(1 + 4 * (len(p) + 1) + 4 * (len(n) + 1) + 6 for _, _, _, p, n in SFX),
    }
    sizes["total"] = sum(v for k, v in sizes.items() if k != "pattern_count")
    return "\n".join(hdr) + "\n", sizes


if __name__ == "__main__":
    text, sizes = compile_all()
    Path(sys.argv[1]).write_text(text)
    print(sizes)
