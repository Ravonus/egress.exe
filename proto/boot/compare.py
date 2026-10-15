"""Boot both cartridges headless in PyBoy, capture every frame and the audio of the KEEL start-up, and compare
EGRESS's against CRUCIBLE's.

CRUCIBLE's start-up is not in crucible.gbc: crucible.onkeel.io plays it on the handheld's screen over the emulator
(src/lib/boot.ts). So the CRUCIBLE side is (a) crucible.gbc booted in PyBoy, to show what the cartridge itself puts
on screen meanwhile, and (b) keel_boot_ref.py, boot.ts evaluated at Game Boy frame times, which is what the site
shows on the handheld's screen. EGRESS is compared frame by frame against (b).

    .venv/bin/python proto/boot/compare.py [build/egress.gbc] [~/dev/crucible.exe/build/crucible.gbc]
"""

from __future__ import annotations

import io
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from pyboy import PyBoy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import keel_boot_ref as ref  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
EGRESS = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/egress.gbc"
CRUCIBLE = Path(sys.argv[2]) if len(sys.argv) > 2 else Path.home() / "dev/crucible.exe/build/crucible.gbc"
RUN = 420


def capture(rom: Path, ram: int, frames: int) -> tuple[list[np.ndarray], list[np.ndarray], int]:
    pb = PyBoy(str(rom), window="null", cgb=True, sound_emulated=True, ram_file=io.BytesIO(bytes(ram)))
    shots, audio = [], []
    for _ in range(frames):
        pb.tick(1, True, True)
        shots.append(np.array(pb.screen.image.convert("RGB")))
        audio.append(np.array(pb.sound.ndarray, copy=True))
    rate = pb.sound.sample_rate
    pb.stop(save=False)
    return shots, audio, rate


def first_boot_frame(shots: list[np.ndarray]) -> int:
    bg = (np.array(ref.BG) >> 3) << 3
    for i, s in enumerate(shots):
        if (s.reshape(-1, 3) == bg).all(axis=1).mean() > 0.9:
            return i
    raise SystemExit("no KEEL start-up frame found")


def write_wav(path: Path, samples: np.ndarray, rate: int) -> None:
    pcm = np.clip(samples, -1, 1)
    pcm = (pcm * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(pcm.shape[1] if pcm.ndim == 2 else 1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def to_float(chunks: list[np.ndarray]) -> np.ndarray:
    a = np.concatenate([c.reshape(-1, 2) for c in chunks if c.size]).astype(np.float64)
    return a / 128.0 if a.dtype != np.float64 or np.abs(a).max() > 2 else a


def envelope(x: np.ndarray, rate: int, hop: float) -> np.ndarray:
    n = int(rate * hop)
    k = len(x) // n
    return np.sqrt((x[: k * n].reshape(k, n) ** 2).mean(axis=1))


def pitch(x: np.ndarray, rate: int) -> float:
    x = x - x.mean()
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)), 1 << 18))
    return float(np.argmax(spec) * rate / (1 << 18))


def main() -> None:
    lines: list[str] = []
    out = lines.append
    n_boot = ref.frames_total()
    eg, eg_audio, rate = capture(EGRESS, 8192, RUN)
    cr, _, _ = capture(CRUCIBLE, 131072, RUN)
    k0 = first_boot_frame(eg)
    out(f"KEEL start-up: CRUCIBLE (crucible.onkeel.io boot.ts at Game Boy frame times) vs EGRESS ({EGRESS.name}, PyBoy)")
    out(f"boot length {n_boot} frames; chime at boot frame {ref.chime_frame()}; EGRESS shows boot frame 0 at PyBoy frame {k0}")
    out("colours compared as the CGB shows them (8-bit -> 5-bit per channel, PyBoy's c<<3); under the dither: black")
    out("")
    same, diffs = 0, []
    for k in range(n_boot):
        want = ref.render(k)
        got = eg[k0 + k]
        bad = int((want != got).any(axis=2).sum())
        if bad == 0:
            same += 1
        else:
            diffs.append((k, bad))
    out(f"frames pixel-identical: {same}/{n_boot}")
    for k, bad in diffs:
        s = ref.timeline(k)
        out(f"  frame {k:3d} ({s['phase']}): {bad} pixels differ")
    title_at = next(i for i in range(k0 + n_boot, RUN) if (eg[i] != 0).any())
    gap = eg[k0 + n_boot:title_at]
    out(f"frames {n_boot}..{title_at - k0 - 1} (done: the game's frame, EGRESS's title loading at fade level 0): "
        f"{'all black' if all((g == 0).all() for g in gap) else 'NOT all black'}; the title's fade-in starts at boot "
        f"frame {title_at - k0} (PyBoy frame {title_at})")
    cr_title = next((i for i in range(RUN) if len({tuple(p) for p in cr[i].reshape(-1, 3)[::97]}) > 4 and i > 64), None)
    out(f"crucible.gbc in PyBoy for the same {RUN} frames: no start-up of its own (blank while it boots; title at "
        f"PyBoy frame {cr_title if cr_title is not None else '>' + str(RUN)}, ~933 on a blank save)")

    # ---- audio ----
    out("")
    eg_a = to_float(eg_audio[k0:k0 + n_boot + 10])
    mono = eg_a.mean(axis=1)
    per = len(mono) / (n_boot + 10)
    site = ref.chime_wave(rate)
    on = np.flatnonzero(np.abs(mono) > 1e-3)
    out(f"audio: PyBoy {rate} Hz, {per:.1f} samples per frame")
    if on.size:
        start_f = on[0] / per
        end_f = on[-1] / per
        out(f"EGRESS chime: sound from boot frame {start_f:.2f} to {end_f:.2f} (reference: {ref.chime_frame()} to "
            f"{ref.chime_frame() + 49}, 5 + 44 frames)")
        s0 = int(on[0])
        n1 = int(5 * per)
        out(f"note 1 pitch: EGRESS {pitch(mono[s0:s0 + n1], rate):.1f} Hz, site {pitch(site[:n1], rate):.1f} Hz, "
            f"register 1923 -> {131072 / (2048 - 1923):.1f} Hz")
        out(f"note 2 pitch: EGRESS {pitch(mono[s0 + n1:s0 + n1 + int(20 * per)], rate):.1f} Hz, "
            f"site {pitch(site[n1:n1 + int(20 * per)], rate):.1f} Hz, register 1982 -> {131072 / (2048 - 1982):.1f} Hz")
        ee, se = envelope(mono[s0:], rate, 1 / 120), envelope(site, rate, 1 / 120)
        m = min(len(ee), len(se))
        ee, se = ee[:m], se[:m]
        corr = float(np.corrcoef(ee, se)[0, 1])
        out(f"loudness envelope (1/120 s RMS) correlation EGRESS vs site: {corr:.3f}")
        ze = next((i for i in range(len(ee)) if i > 20 and ee[i] < 0.02 * ee.max()), m) / 120
        zs = next((i for i in range(len(se)) if i > 20 and se[i] < 0.02 * se.max()), m) / 120
        out(f"note 2 audible until: EGRESS {ze * 1000:.0f} ms after the chime, site {zs * 1000:.0f} ms (the site "
            "ramps NRx2 0xa3 linearly over the note; the APU steps the volume down every 3/64 s and is silent at 10 steps)")
        write_wav(HERE / "boot_egress.wav", eg_a / max(1e-9, np.abs(eg_a).max()) * 0.8, rate)
        pad = np.zeros(int(ref.chime_frame() * per))
        site_full = np.concatenate([pad, site, np.zeros(int(10 * per))])
        write_wav(HERE / "boot_crucible_site.wav", site_full / np.abs(site_full).max() * 0.8, rate)
        for name in ("boot_egress", "boot_crucible_site"):
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(HERE / f"{name}.wav"), "-b:a", "128k",
                            str(HERE / f"{name}.mp3")], check=False)
    else:
        out("EGRESS: no sound captured")

    (HERE / "compare.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))

    # ---- contact sheet: CRUCIBLE (site boot) | EGRESS | crucible.gbc in PyBoy, 2x ----
    keys = [0, 10, 25, 45, 70, 89, 90, 110, 131, 134, 140, 146, 149]
    sc, pad_, lab = 2, 6, 14
    cols = 3
    cw, chh = 160 * sc, 144 * sc
    sheet = Image.new("RGB", (cols * (cw + pad_) + pad_ + 110, len(keys) * (chh + pad_) + pad_ + lab), (24, 20, 36))
    d = ImageDraw.Draw(sheet)
    for c, t in enumerate(["CRUCIBLE (site boot.ts)", "EGRESS.EXE (PyBoy)", "crucible.gbc (PyBoy)"]):
        d.text((110 + pad_ + c * (cw + pad_), 2), t, fill=(230, 230, 240))
    for r, k in enumerate(keys):
        y = lab + pad_ + r * (chh + pad_)
        want = ref.render(k)
        got = eg[k0 + k]
        bad = int((want != got).any(axis=2).sum())
        s = ref.timeline(k)
        d.text((6, y + 4), f"frame {k}", fill=(230, 230, 240))
        d.text((6, y + 20), s["phase"], fill=(180, 180, 200))
        d.text((6, y + 36), "identical" if bad == 0 else f"{bad} px differ", fill=(120, 230, 140) if bad == 0 else (255, 120, 120))
        for c, img in enumerate([want, got, cr[k0 + k]]):
            sheet.paste(Image.fromarray(img).resize((cw, chh), Image.NEAREST), (110 + pad_ + c * (cw + pad_), y))
    sheet.save(ROOT / "docs/shots/boot_compare.png")


if __name__ == "__main__":
    main()
