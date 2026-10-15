"""Compose EGRESS's scores: `python tools/compose.py OUT_HEADER [--wav DIR]`.

Three original pieces, written from fixed seeds: warm, ambient chiptune with soft plucked pads, a sparse lead with an echo,
a round wave-channel bass and quiet brushes. Nothing harsh: no 12.5 % duty, no loud noise, slow tempos.

  LOBBY      (title)  F lydian, slow, almost no percussion
  STAIRWELL  (floors) D major, a little motion, hats enter after the intro
  WATCHMAN   (boss)   A minor, a steady pulse and a walking bass; tense, still warm (no harsh duty, soft noise)

Writes a C header (gen/music_data.h) for src/sound.c; --wav also renders a rough square-wave preview of each song.
"""

from __future__ import annotations

import argparse
import math
import random
import struct
import wave
from pathlib import Path

MN_REST, MN_HOLD, MN_END = 0x7F, 0x7E, 0xFF
NOTE_NAMES = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7,
              "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}


def n(name: str, octave: int) -> int:
    """pitch index from C2 = 0"""
    return NOTE_NAMES[name] + (octave - 2) * 12


def period(i: int) -> int:
    f = 65.406 * 2 ** (i / 12)
    return max(0, min(2047, round(2048 - 131072 / f)))


WAVES = [
    # a round, slightly hollow sine: the bass
    [round(7.5 + 6.5 * math.sin(2 * math.pi * k / 32) + 1.0 * math.sin(4 * math.pi * k / 32)) for k in range(32)],
]
NOISE = [
    (0x21, 0x11),  # 0 soft hat
    (0x32, 0x24),  # 1 brush
    (0x41, 0x63),  # 2 low thump
]


class Song:
    def __init__(self, name: str, fpr: int, pad: tuple[int, int], lead: tuple[int, int], wave_vol: int, echo: int):
        self.name = name
        self.fpr = fpr
        self.pad, self.lead, self.wave_vol, self.echo = pad, lead, wave_vol, echo
        self.ch: list[list[tuple[int, int]]] = [[], [], [], []]

    def put(self, c: int, note: int, rows: int) -> None:
        while rows > 255:
            self.ch[c].append((note, 255))
            note = MN_HOLD
            rows -= 255
        if rows > 0:
            self.ch[c].append((note, rows))

    def rows(self, c: int) -> int:
        return sum(r for _, r in self.ch[c])


def chord_tones(root: str, kind: str) -> list[int]:
    r = NOTE_NAMES[root]
    iv = {"maj7": [0, 4, 7, 11], "m7": [0, 3, 7, 10], "maj9": [0, 4, 7, 11, 14], "sus2": [0, 2, 7, 9],
          "m9": [0, 3, 7, 10, 14], "lyd": [0, 4, 6, 7, 11], "add9": [0, 4, 7, 14], "sus4": [0, 5, 7, 10]}[kind]
    return [(r + i) % 12 for i in iv]


def nearest(pitch_classes: list[int], target: int, lo: int, hi: int) -> int:
    best = None
    for p in range(lo, hi + 1):
        if p % 12 in pitch_classes and (best is None or abs(p - target) < abs(best - target)):
            best = p
    assert best is not None
    return best


def write_pad(s: Song, prog: list[tuple[str, str, int]], step: int, lo: int, hi: int, rng: random.Random) -> None:
    """soft plucks on chord tones every `step` rows, voice-led, alternating a lower and an upper tone"""
    prev_a, prev_b = (lo + hi) // 2 - 3, (lo + hi) // 2 + 3
    for root, kind, rows in prog:
        tones = chord_tones(root, kind)
        a = nearest(tones, prev_a, lo, hi)
        b = nearest([t for t in tones if t != a % 12], prev_b, lo, hi)
        if b <= a:
            b = nearest([t for t in tones if t != a % 12], a + 5, a + 1, hi + 4)
        k = 0
        for _ in range(rows // step):
            s.put(0, a if k % 2 == 0 else b, step)
            k += 1
        prev_a, prev_b = a, b


def write_bass(s: Song, prog: list[tuple[str, str, int]], octave: int, walk: bool) -> None:
    for root, _kind, rows in prog:
        r = n(root, octave) + 12  # the wave channel sounds an octave below the pulse table
        fifth = r + 7 if r + 7 < n("D", octave + 1) + 12 else r - 5
        if rows >= 16 and walk:
            s.put(2, r, 8)
            s.put(2, MN_HOLD, rows // 2 - 8)
            s.put(2, fifth, rows // 2 - 2)
            s.put(2, MN_REST, 2)
        else:
            s.put(2, r, rows - 2)
            s.put(2, MN_REST, 2)


RHYTHMS = [[0, 6, 10], [0, 3, 8, 12], [4, 8, 14], [0, 8], [2, 6, 12], [0, 10], [0, 4, 6, 12]]


def phrase(prog: list[tuple[str, str, int]], scale: list[int], lo: int, hi: int, rng: random.Random,
           density: float) -> list[tuple[int, int]]:
    """a melodic line over the progression: (row, note) hits, a random walk on the scale, chord tones on downbeats"""
    pos = 0
    hits: list[tuple[int, int]] = []
    pitches = [p for p in range(lo, hi + 1) if p % 12 in scale]
    cur = pitches[len(pitches) // 2]
    for root, kind, rows in prog:
        tones = chord_tones(root, kind)
        for bar in range(rows // 16):
            if rng.random() > density:
                pos += 16
                continue
            rhythm = rng.choice(RHYTHMS)
            for i, r in enumerate(rhythm):
                if i == 0 or r % 8 == 0:
                    cur = nearest(tones, cur + rng.choice([-2, -1, 0, 1, 2]), lo, hi)
                else:
                    j = pitches.index(min(pitches, key=lambda p: abs(p - cur)))
                    j = max(0, min(len(pitches) - 1, j + rng.choice([-2, -1, -1, 1, 1, 2])))
                    cur = pitches[j]
                hits.append((pos + r, cur))
            pos += 16
            _ = bar
    return hits


def write_lead(s: Song, hits: list[tuple[int, int]], total: int) -> None:
    pos = 0
    if not hits or hits[0][0] > 0:
        first = hits[0][0] if hits else total
        s.put(1, MN_REST, first)
        pos = first
    for i, (r, note) in enumerate(hits):
        end = hits[i + 1][0] if i + 1 < len(hits) else total
        length = end - r
        ring = min(length, 6)
        s.put(1, note, ring)
        if length - ring:
            s.put(1, MN_REST, length - ring)
        pos = end
    assert pos == total, (pos, total)


def write_drums(s: Song, bars_off: int, bars_on: int, pattern: dict[int, int], fill_every: int) -> None:
    s.put(3, MN_REST, bars_off * 16)
    for b in range(bars_on):
        row = 0
        hits = dict(pattern)
        if fill_every and b % fill_every == fill_every - 1:
            hits[12] = 1
            hits[14] = 0
        for r in sorted(hits):
            if r > row:
                s.put(3, MN_REST, r - row)
            s.put(3, hits[r], 1)
            row = r + 1
        if row < 16:
            s.put(3, MN_REST, 16 - row)


def stairwell() -> Song:
    rng = random.Random(0x5717)
    s = Song("STAIRWELL", fpr=8, pad=(2, 0x46), lead=(1, 0x74), wave_vol=0x40, echo=3)
    a = [("D", "maj9", 32), ("B", "m7", 32), ("G", "maj7", 32), ("A", "sus4", 16), ("A", "add9", 16)]
    c = [("E", "m7", 32), ("F#", "m7", 32), ("G", "maj7", 32), ("A", "sus2", 32)]
    prog = a + a + a + c
    total = sum(r for *_, r in prog)
    write_pad(s, prog, 4, n("F#", 3), n("E", 5), rng)
    write_bass(s, prog, 2, True)
    scale = [NOTE_NAMES[x] for x in ("D", "E", "F#", "A", "B")]
    p1 = phrase(a, scale, n("A", 4), n("D", 6), rng, 0.85)
    p2 = phrase(a, scale, n("A", 4), n("D", 6), rng, 0.85)
    p3 = phrase(c, scale, n("F#", 4), n("B", 5), rng, 0.6)
    la = sum(r for *_, r in a)
    hits = [(r, x) for r, x in p1] + [(r + la, x) for r, x in p2] + [(r + 2 * la, x) for r, x in p1]
    hits += [(r + 3 * la, x) for r, x in p3]
    write_lead(s, hits, total)
    write_drums(s, 8, total // 16 - 8, {2: 0, 6: 0, 10: 0, 14: 0, 0: 2}, 4)
    for ch in range(4):
        assert s.rows(ch) == total, (ch, s.rows(ch), total)
    return s


def lobby() -> Song:
    rng = random.Random(0x10BB)
    s = Song("LOBBY", fpr=11, pad=(2, 0x37), lead=(2, 0x64), wave_vol=0x40, echo=4)
    prog = [("F", "lyd", 32), ("G", "add9", 32), ("E", "m7", 32), ("A", "m9", 32)] * 2
    total = sum(r for *_, r in prog)
    write_pad(s, prog, 8, n("A", 3), n("G", 5), rng)
    write_bass(s, prog, 2, False)
    scale = [NOTE_NAMES[x] for x in ("F", "G", "A", "B", "C", "E")]
    hits = phrase(prog, scale, n("C", 5), n("E", 6), rng, 0.45)
    write_lead(s, hits, total)
    write_drums(s, 4, total // 16 - 4, {12: 1}, 0)
    for ch in range(4):
        assert s.rows(ch) == total, (ch, s.rows(ch), total)
    return s


def watchman() -> Song:
    rng = random.Random(0x3A7C)
    s = Song("WATCHMAN", fpr=7, pad=(2, 0x55), lead=(1, 0x84), wave_vol=0x40, echo=3)
    a = [("A", "m9", 32), ("F", "maj7", 32), ("D", "m7", 32), ("E", "sus4", 32)]
    b = [("C", "maj7", 32), ("G", "add9", 32), ("F", "lyd", 32), ("E", "sus4", 16), ("E", "m7", 16)]
    prog = a + a + b + a
    total = sum(r for *_, r in prog)
    write_pad(s, prog, 4, n("E", 3), n("E", 5), rng)
    write_bass(s, prog, 2, True)
    scale = [NOTE_NAMES[x] for x in ("A", "B", "C", "D", "E", "G")]
    p1 = phrase(a, scale, n("E", 4), n("A", 5), rng, 0.8)
    p2 = phrase(b, scale, n("G", 4), n("C", 6), rng, 0.9)
    la, lb = sum(r for *_, r in a), sum(r for *_, r in b)
    hits = [(r, x) for r, x in p1] + [(r + la, x) for r, x in p1] + [(r + 2 * la, x) for r, x in p2]
    hits += [(r + 2 * la + lb, x) for r, x in p1]
    write_lead(s, hits, total)
    write_drums(s, 2, total // 16 - 2, {0: 2, 4: 0, 8: 1, 12: 0, 14: 0}, 4)
    for ch in range(4):
        assert s.rows(ch) == total, (ch, s.rows(ch), total)
    return s


def header(songs: list[Song]) -> str:
    out = ["/* Generated by tools/compose.py. Do not edit. Included by src/sound.c only. */",
           "#ifndef MUSIC_DATA_H", "#define MUSIC_DATA_H", "#include <stdint.h>",
           f"#define MN_REST 0x{MN_REST:02x}u", f"#define MN_HOLD 0x{MN_HOLD:02x}u", f"#define MN_END 0x{MN_END:02x}u"]
    out.append("static const uint16_t mn_period[72] = {" + ", ".join(str(period(i)) for i in range(72)) + "};")
    wv = []
    for w in WAVES:
        b = [(w[2 * k] << 4) | w[2 * k + 1] for k in range(16)]
        wv.append("{" + ", ".join(f"0x{x:02x}" for x in b) + "}")
    out.append("static const uint8_t mn_waves[][16] = {" + ", ".join(wv) + "};")
    out.append("static const uint8_t mn_noise[][2] = {" + ", ".join(f"{{0x{e:02x}, 0x{p:02x}}}" for e, p in NOISE) + "};")
    names = []
    for si, s in enumerate(songs):
        for c in range(4):
            data = []
            for note, rows in s.ch[c]:
                data += [note, rows]
            data += [MN_END, 0]
            nm = f"mn_{s.name.lower()}_{c}"
            names.append(nm)
            out.append(f"static const uint8_t {nm}[] = {{" + ", ".join(str(x) for x in data) + "};")
        _ = si
    out.append("static const uint8_t *const mn_streams[] = {" + ", ".join(names) + "};")
    hs = []
    for s in songs:
        hs.append("{" + ", ".join(str(x) for x in (s.fpr, s.pad[0], s.pad[1], s.lead[0], s.lead[1], s.wave_vol,
                                                     s.echo, 0)) + "}")
    out.append("/* fpr, pad duty, pad env, lead duty, lead env, wave level (NR32), echo rows, wave */")
    out.append("static const uint8_t mn_songs[][8] = {" + ", ".join(hs) + "};")
    out.append("#endif")
    return "\n".join(out) + "\n"


def render(s: Song, path: Path, seconds: float = 40.0) -> None:
    """a rough preview: square pulses with linear envelopes, the wave table for bass, white noise"""
    rate = 32000
    frame = rate / 59.73
    out = [0.0] * int(seconds * rate)
    rngn = random.Random(1)

    def env_level(env: int, t: float) -> float:
        v, period_ = env >> 4, env & 7
        if period_:
            v = max(0, v - int(t * 64 / period_))
        return v / 15

    for c in range(4):
        t = 0.0
        events = s.ch[c] * 8
        for note, rows in events:
            dur = rows * s.fpr * frame
            start = int(t)
            if note not in (MN_REST, MN_HOLD) and start < len(out):
                if c == 3:
                    e, _p = NOISE[note]
                    for k in range(start, min(len(out), start + int(0.25 * rate))):
                        out[k] += (rngn.random() * 2 - 1) * env_level(e, (k - start) / rate) * 0.18
                else:
                    f = 65.406 * 2 ** (note / 12) * (0.5 if c == 2 else 1)
                    env = s.pad[1] if c == 0 else s.lead[1]
                    duty = [0.125, 0.25, 0.5, 0.75][s.pad[0] if c == 0 else s.lead[0]]
                    for k in range(start, min(len(out), start + int(dur))):
                        ph = ((k - start) * f / rate) % 1
                        tt = (k - start) / rate
                        if c == 2:
                            amp = (WAVES[0][int(ph * 32)] / 7.5 - 1) * 0.22
                        else:
                            amp = (1 if ph < duty else -1) * env_level(env, tt) * 0.12
                            if c == 1 and tt > s.echo * s.fpr / 59.73:
                                amp = (1 if ph < duty else -1) * env_level(0x34, tt - s.echo * s.fpr / 59.73) * 0.12
                        out[k] += amp
            t += dur
            if t >= len(out):
                break
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        # a gentle low-pass, like the GBC's output stage
        y, acc = [], 0.0
        for v in out:
            acc += (v - acc) * 0.35
            y.append(acc)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v)) * 30000)) for v in y))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", type=Path)
    ap.add_argument("--wav", type=Path)
    a = ap.parse_args()
    songs = [lobby(), stairwell(), watchman()]
    a.out.write_text(header(songs))
    for s in songs:
        print(f"{s.name}: {s.rows(0)} rows, {s.rows(0) * s.fpr / 59.73:.0f} s per loop")
        if a.wav:
            a.wav.mkdir(parents=True, exist_ok=True)
            render(s, a.wav / f"{s.name.lower()}.wav")


if __name__ == "__main__":
    main()
