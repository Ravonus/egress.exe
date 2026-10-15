"""Record the EGM test ROM in PyBoy and write MP3 previews + level/harshness stats.

  python render.py ROM OUTDIR [--only name,name] [--rate 96000]

The ROM runs the real driver (egm.c) on the emulated CGB in double speed; this script only writes commands into
egm_ctl at scripted times and records the APU output frame by frame. What PyBoy does not model is applied here,
from the registers the ROM wrote: NR50 master volume (PyBoy ignores it; read back every frame and applied to that
frame's samples), the output stage's DC blocking (a first-order high-pass at 20 Hz), and band-limited resampling
(PyBoy point-samples; we record at a high rate and let ffmpeg's resampler low-pass to 48 kHz). PyBoy frames are
1/60 s of samples but the CGB's frame is 70224 cycles (59.7275 Hz), so the recording's true rate is
rate * 59.7275 / 60; the WAV is written at that rate so pitch and tempo are exact.

Levels are absolute, never normalised per file: a sum of the four channels at full volume (60 of PyBoy's 0..127)
maps to +-0.94 FS, so files are comparable with each other and with the hardware's headroom.
"""

from __future__ import annotations

import argparse
import io
import json
import subprocess
import wave
from pathlib import Path

import numpy as np
from pyboy import PyBoy

FFMPEG = "/opt/homebrew/bin/ffmpeg"
FRAME_HZ = 4194304 / 70224

MOODS = ["calm", "explore", "tension", "combat", "boss", "collapse", "dream", "glitch", "ending"]
BIOMES = ["office", "vents", "atrium", "server", "mall", "dream", "rooftop", "glitch"]
SFX = ["step", "pickup", "chime", "key", "hit", "hurt", "door", "deny"]
XFADE, DROP, BEAT = 0, 1, 2


def symbols(noi: Path) -> dict[str, int]:
    out = {}
    for line in noi.read_text().splitlines():
        p = line.split()
        if len(p) == 3 and p[0] == "DEF":
            out[p[1]] = int(p[2], 16)
    return out


def play(mood, biome, seed=0x1234, level=1):
    return [(3, [level]), (1, [MOODS.index(mood), BIOMES.index(biome), seed & 255, seed >> 8])]


# name -> (seconds, [(time_s, cmd, args)], description)
def scenarios():
    S = {}

    def add(name, secs, events, desc):
        S[name] = (secs, events, desc)

    def at(t, cmds):
        return [(t, c, a) for c, a in cmds]

    add("mood-calm-landing_office", 40, at(0, play("calm", "office", 0x51a7, 1)) + [(20, 3, [2])],
        "calm landing, office: pad + bass + lead; soft drums join at 20 s (intensity 1 -> 2)")
    add("mood-explore_vents_seed-a", 40, at(0, play("explore", "vents", 0x0e01, 1)), "uneasy explore, vents, seed A")
    add("mood-explore_vents_seed-b", 40, at(0, play("explore", "vents", 0x7c3d, 1)), "uneasy explore, vents, seed B")
    add("mood-explore_mall", 36, at(0, play("explore", "mall", 0x0e01, 1)),
        "uneasy explore in the mall food court (same seed as A: the biome palette changes key, timbre, kit, swing)")
    add("mood-tension_atrium", 40, at(0, play("tension", "atrium", 0x2b44, 1)), "tension, flooded atrium")
    add("mood-combat_server", 36, at(0, play("combat", "server", 0x6611, 2)), "combat, server farm")
    add("mood-boss_rooftop", 36, at(0, play("boss", "rooftop", 0x4b05, 2)), "boss, rooftop sky")
    add("mood-collapse-chase_office", 32, at(0, play("collapse", "office", 0x0c0c, 2)), "collapse / chase, office (no bass)")
    add("mood-dream_garden", 45, at(0, play("dream", "dream", 0x3e3e, 0)) + [(10, 3, [1]), (21, 3, [2]), (32, 3, [3])],
        "dream garden: pad alone, then lead, bass, breath (intensity 0 -> 3)")
    add("mood-glitch-rift_core", 36, at(0, play("glitch", "glitch", 0x9a1f, 2)), "glitch rift, glitch core")
    add("mood-ending_rooftop", 45, at(0, play("ending", "rooftop", 0x0e0d, 1)) + [(24, 3, [2])], "ending, rooftop sky")
    add("transition-explore-to-combat_server", 34, at(0, play("explore", "server", 0x1357, 1)) + [(14.3, 2, [MOODS.index("combat"), BIOMES.index("server"), XFADE])],
        "explore -> combat requested at 14.3 s; lands on the next bar line, the old song fades under the new for a bar")
    add("transition-combat-to-collapse_server", 32, at(0, play("combat", "server", 0x2468, 2)) + [(15.1, 2, [MOODS.index("collapse"), BIOMES.index("server"), DROP])],
        "combat -> collapse requested at 15.1 s; on the next downbeat the bass drops out and the chase takes over")
    add("transition-calm-layers-to-tension_office", 40, at(0, play("calm", "office", 0x0bad, 0)) + [(8, 3, [1]), (16, 3, [2]), (26, 2, [MOODS.index("tension"), BIOMES.index("office"), XFADE])],
        "calm at intensity 0 (pad + bass), 1 (+lead), 2 (+drums), then a crossfade into tension at 26 s")
    ev = at(0, play("explore", "office", 0x0e01, 1))
    t = 3.0
    for i in range(12):
        ev.append((t, 4, [SFX.index("pickup")]))
        t += 1.7 + (i % 3) * 0.55
    ev += [(9.6, 4, [SFX.index("chime")]), (17.2, 4, [SFX.index("chime")]), (24.0, 4, [SFX.index("key")]),
           (28.4, 4, [SFX.index("hit")]), (28.9, 4, [SFX.index("pickup")]), (31.0, 4, [SFX.index("hurt")]),
           (31.1, 4, [SFX.index("pickup")]), (34.5, 4, [SFX.index("door")]), (37.5, 4, [SFX.index("deny")])]
    add("sfx-over-music_explore_office", 40, sorted(ev, key=lambda e: e[0]),
        "pickups (current chord's tones), reaction chimes, a key, a hit, a hurt (priority: the pickup at 31.1 s is "
        "dropped), a door, a deny, all in the music's key, with the music ducked while they play")
    ev = at(0, play("explore", "office", 0x0e01, 1))
    ev += [(8, 5, [2, 5]), (11, 4, [1]), (16, 5, [0, 5]), (19, 4, [1]), (22, 5, [6, 4]), (26, 4, [2]), (32, 7, [0, 10])]
    add("volume-fades_explore_office", 38, ev,
        "MUSIC 6 -> 2 (8 s) -> 0 (16 s) -> 6 (22 s) with effects at a fixed SFX volume, then a master fade (pause) at 32 s")
    return S


def record(rom: Path, sym: dict, secs: float, events, rate: int):
    pb = PyBoy(str(rom), window="null", cgb=True, sound_emulated=True, sound_sample_rate=rate,
               ram_file=io.BytesIO(bytes(8192)), log_level="ERROR")
    pb.set_emulation_speed(0)
    ctl, idle_at = sym["_egm_ctl"], sym["_egm_idle"]
    for _ in range(600):
        pb.tick(1, False, True)
        if pb.memory[ctl + 7] == 0xA5:
            break
    else:
        raise RuntimeError("the test ROM never became ready")
    pb.memory[ctl + 6] = 1  # calibrate: frames without the tick
    base = []
    for _ in range(8):
        pb.tick(1, False, True)
        base.append(pb.memory[idle_at] | pb.memory[idle_at + 1] << 8)
    pb.memory[ctl + 6] = 0
    base = max(base[2:])
    for _ in range(4):
        pb.tick(1, False, True)
    seq = 0
    events = list(events)
    frames = int(secs * FRAME_HZ)
    chunks, nr50, costs = [], [], []
    pending = []
    for f in range(frames):
        t = f / FRAME_HZ
        while events and events[0][0] <= t:
            pending.append(events.pop(0))
        if pending:
            _, cmd, args = pending.pop(0)
            for i, a in enumerate(args):
                pb.memory[ctl + 2 + i] = a
            pb.memory[ctl + 1] = cmd
            seq = (seq + 1) & 255
            pb.memory[ctl] = seq
        pb.tick(1, False, True)
        chunks.append(pb.sound.ndarray.copy())
        nr50.append(pb.memory[0xFF24])
        costs.append(1.0 - (pb.memory[idle_at] | pb.memory[idle_at + 1] << 8) / base)
    pb.stop(save=False)
    return chunks, nr50, costs


def to_pcm(chunks, nr50):
    out = []
    for c, v in zip(chunks, nr50):
        x = c.astype(np.float32)
        l, r = ((v >> 4) & 7) + 1, (v & 7) + 1
        x[:, 0] *= l / 8
        x[:, 1] *= r / 8
        out.append(x)
    x = np.concatenate(out)
    return x / 32.0  # unipolar 0..60; after the 20 Hz high-pass (the output capacitor) a full mix swings about +-0.94


def write_wav(path: Path, x: np.ndarray, rate: float):
    # float WAV: the unipolar signal may exceed 1.0 before the high-pass, so nothing is clipped here
    import struct
    data = x.astype("<f4").tobytes()
    sr = int(round(rate))
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 2, sr, sr * 8, 8, 32)
    path.write_bytes(hdr + b"data" + struct.pack("<I", len(data)) + data)


def encode(wav: Path, mp3: Path):
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", str(wav), "-af",
                    "highpass=f=20:poles=1,aresample=48000:filter_size=64:cutoff=0.97",
                    "-c:a", "libmp3lame", "-b:a", "192k", str(mp3)], check=True)


def decode(path: Path) -> tuple[np.ndarray, int]:
    raw = subprocess.run([FFMPEG, "-loglevel", "error", "-i", str(path), "-f", "f32le", "-ac", "2", "-ar", "48000", "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, 2), 48000


MODE_STEPS = {"ionian": [0, 2, 4, 5, 7, 9, 11], "dorian": [0, 2, 3, 5, 7, 9, 10], "phrygian": [0, 1, 3, 5, 7, 8, 10],
              "lydian": [0, 2, 4, 6, 7, 9, 11], "aeolian": [0, 2, 3, 5, 7, 8, 10]}
MOOD_MODE = {"calm": "ionian", "explore": "dorian", "tension": "aeolian", "combat": "aeolian", "boss": "phrygian",
             "collapse": "aeolian", "dream": "lydian", "glitch": "phrygian", "ending": "ionian"}
BIOME_ROOT = {"office": 2, "vents": 4, "atrium": 11, "server": 0, "mall": 7, "dream": 3, "rooftop": 9, "glitch": 6}
NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def chroma(x: np.ndarray, sr: int) -> np.ndarray:
    """pitch-class energy from 110 Hz to 1 kHz (below, FFT bins straddle semitones) (above that, pulse-wave harmonics blur the picture)"""
    mono = x.mean(axis=1)
    n = 16384
    win = np.hanning(n)
    freqs = np.fft.rfftfreq(n, 1 / sr)
    sel = (freqs >= 110) & (freqs <= 1000)
    pcs = (np.round(12 * np.log2(freqs[sel] / 440.0)) + 9) % 12
    ch = np.zeros(12)
    for i in range(0, len(mono) - n, n // 2):
        p = np.abs(np.fft.rfft(mono[i:i + n] * win))[sel] ** 2
        np.add.at(ch, pcs.astype(int), p)
    return ch / (ch.sum() or 1)


def key_check(x: np.ndarray, sr: int, root: int, mode: str) -> dict:
    ch = chroma(x, sr)
    scale = {(root + s) % 12 for s in MODE_STEPS[mode]}
    out = float(sum(ch[p] for p in range(12) if p not in scale))
    top = [NAMES[i] for i in np.argsort(ch)[::-1][:3]]
    return {"key": f"{NAMES[root]} {mode}", "out_of_scale_pct": round(out * 100, 1), "top_pitch_classes": top}


def stats(x: np.ndarray, sr: int) -> dict:
    mono = x.mean(axis=1)
    peak = float(np.abs(x).max())
    rms = float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))
    n = 4096
    win = np.hanning(n)
    freqs = np.fft.rfftfreq(n, 1 / sr)
    pw = np.zeros(len(freqs))
    for i in range(0, len(mono) - n, n // 2):
        pw += np.abs(np.fft.rfft(mono[i:i + n] * win)) ** 2
    pw[freqs < 20] = 0  # ignore DC and sub-audio (some reference recordings carry the APU's unipolar offset)
    tot = pw.sum() or 1.0
    db = lambda v: round(20 * np.log10(max(v, 1e-9)), 1)
    return {
        "peak_dbfs": db(peak), "rms_dbfs": db(rms), "crest_db": round(db(peak) - db(rms), 1),
        "clipped_samples": int((np.abs(x) >= 0.999).sum()),
        "centroid_hz": round(float((freqs * pw).sum() / tot)),
        "energy_above_2k_pct": round(float(pw[freqs > 2000].sum() / tot * 100), 2),
        "energy_above_5k_pct": round(float(pw[freqs > 5000].sum() / tot * 100), 3),
        "energy_below_250_pct": round(float(pw[freqs < 250].sum() / tot * 100), 1),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--only", default="")
    ap.add_argument("--rate", type=int, default=96000)
    ap.add_argument("--wav-dir", type=Path, default=None, help="keep the float WAV recordings here (default: deleted)")
    ap.add_argument("--ref", nargs="*", default=[], help="reference MP3s to measure the same way")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    import tempfile
    tmp = None if a.wav_dir else tempfile.TemporaryDirectory()
    wav_dir = a.wav_dir or Path(tmp.name)
    wav_dir.mkdir(parents=True, exist_ok=True)
    sym = symbols(a.rom.with_suffix(".noi"))
    report = {"method": "PyBoy 2.7.0 recording of the CGB test ROM (proto/music/test_rom.c + egm.c)", "takes": []}
    statp = a.out / "stats.json"
    if a.only and statp.exists():
        report = json.loads(statp.read_text())
    only = set(a.only.split(",")) if a.only else None
    for name, (secs, events, desc) in scenarios().items():
        if only and name not in only:
            continue
        chunks, nr50, costs = record(a.rom, sym, secs, events, a.rate)
        x = to_pcm(chunks, nr50)
        wav = wav_dir / f"{name}.wav"
        write_wav(wav, x, a.rate * FRAME_HZ / 60)
        mp3 = a.out / f"{name}.mp3"
        encode(wav, mp3)
        if not a.wav_dir:
            wav.unlink()
        y, sr = decode(mp3)
        st = stats(y, sr)
        c = np.array(costs[1:], dtype=float) * 100
        st.update({"take": name, "file": mp3.name, "seconds": round(len(y) / sr, 1), "what": desc,
                   "cpu_pct_frame_mean": round(float(c.mean()), 2), "cpu_pct_frame_p95": round(float(np.percentile(c, 95)), 2),
                   "cpu_pct_frame_max": round(float(c.max()), 2)})
        mood_b = name.split("_")
        mood = mood_b[0].replace("mood-", "").replace("sfx-over-music", "").replace("volume-fades", "")
        mood = {"calm-landing": "calm", "collapse-chase": "collapse", "glitch-rift": "glitch"}.get(mood, mood)
        if name.startswith(("sfx", "volume")):
            mood = mood_b[1]
        biome = mood_b[-1].replace("-a", "").replace("-b", "")
        biome = {"garden": "dream", "core": "glitch"}.get(biome, biome)
        if name.startswith("mood-explore_vents"):
            biome = "vents"
        if mood in MOOD_MODE and biome in BIOME_ROOT:
            st.update(key_check(y, sr, BIOME_ROOT[biome], MOOD_MODE[mood]))
        report["takes"] = [t for t in report["takes"] if t["take"] != name] + [st]
        print(json.dumps(st), flush=True)
    refs = []
    for r in a.ref:
        y, sr = decode(Path(r))
        st = stats(y, sr)
        st["file"] = Path(r).name
        refs.append(st)
        print("ref", json.dumps(st))
    if refs:
        report["references"] = refs
    report["takes"].sort(key=lambda t: t["take"])
    statp.write_text(json.dumps(report, indent=1) + "\n")


if __name__ == "__main__":
    main()
