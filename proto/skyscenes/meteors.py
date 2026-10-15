"""Bake the annual meteor-shower peaks of 2026 .. 2126 for the sky event scenes (host only).

    .venv/bin/python proto/skyscenes/meteors.py     # -> proto/skyscenes/out/meteors.json, meteor_tab.c, meteor_tab.h

A shower peaks when the Sun reaches the shower's peak solar longitude (IMO working list, J2000.0 equinox). The Sun's
J2000 ecliptic longitude comes from Astronomy Engine (GeoVector of the Sun, rotated EQJ -> ECL), and the crossing is
bisected to well under a minute. Times are whole minutes since 2026-01-01 00:00 UTC, the time base of the sky
engine's event streams (proto/sky/gen_tables.py), so `sky_find`-style lookups and these peaks compare directly.

Peak times are good to the minute against the solar longitude, but a real shower's peak is only known to an hour or
so (and some, like the Draconids and Leonids, vary year to year); the scene fires for the whole peak day anyway.

Encoding (per shower, 111 bytes; 11 showers = 1221 bytes + 4 for the header = 1225 bytes):
  u8  id (index into the name table, 0..10)       u8  zhr (typical zenithal hourly rate, capped 255)
  u8  radiant octant (0..7, where the streaks come from on the window) + colour index (hi nibble)
  u8  scale: residual unit in minutes
  u32 base: the 2026 peak (minutes since 2026-01-01 00:00 UTC)
  u16 year: the mean step per year in minutes, minus 525000
  i8  res[101]: peak(y) = base + (y-2026)*(525000+year) + res[y-2026]*scale
The decoder is 3 multiplies and an add. Names live in the scene text bank (about 100 bytes, not counted here).
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import astronomy as A

OUT = Path(__file__).parent / "out"
M2026 = 9497 * 1440  # 2026-01-01 00:00 UTC in minutes since 2000-01-01 00:00 UTC
Y0, Y1 = 2026, 2126

# (code, caption name, peak solar longitude J2000 deg, typical ZHR, radiant octant on the window, streak colour index)
# IMO working list values. Radiant octant: 0 = top-left .. clockwise; colour: 0 white, 1 cyan, 2 yellow, 3 pink, 4 green
SHOWERS = [
    ("QUA", "QUADRANTIDS", 283.15, 80, 1, 1),
    ("LYR", "LYRIDS", 32.32, 18, 2, 0),
    ("ETA", "ETA AQUARIIDS", 45.5, 50, 6, 2),
    ("SDA", "DELTA AQUARIIDS", 127.0, 25, 5, 4),
    ("PER", "PERSEIDS", 140.0, 100, 2, 1),
    ("DRA", "DRACONIDS", 195.4, 10, 1, 4),
    ("ORI", "ORIONIDS", 208.0, 20, 3, 2),
    ("NTA", "TAURIDS", 230.0, 5, 0, 3),
    ("LEO", "LEONIDS", 235.27, 15, 2, 0),
    ("GEM", "GEMINIDS", 262.2, 150, 0, 3),
    ("URS", "URSIDS", 270.7, 10, 1, 1),
]


def atime(m26: float) -> A.Time:
    return A.Time((m26 + M2026) / 1440.0 - 0.5)


def sun_lon_j2000(m26: float) -> float:
    v = A.GeoVector(A.Body.Sun, atime(m26), True)
    return A.Ecliptic(v).elon % 360.0


def dang(a: float, b: float) -> float:
    return (a - b + 180.0) % 360.0 - 180.0


def peak(lam: float, year: int) -> int:
    """minute (since 2026) when the Sun's J2000 longitude crosses lam in `year`"""
    # first guess: the Sun moves ~0.9856 deg/day from the March equinox (lambda 0 near Mar 20)
    d0 = (A.Time.Make(year, 3, 20, 0, 0, 0).ut + 0.5) * 1440.0 - M2026
    a = d0 + (lam / 0.98565) * 1440.0 - 4 * 1440.0
    if lam > 275.0:  # early-January showers (the Quadrantids) peak at the start of `year`, not the end
        a -= 365.2422 * 1440.0
    b = a + 10 * 1440.0
    fa = dang(sun_lon_j2000(a), lam) >= 0
    assert fa != (dang(sun_lon_j2000(b), lam) >= 0)
    while b - a > 0.05:
        m = (a + b) / 2.0
        if (dang(sun_lon_j2000(m), lam) >= 0) == fa:
            a = m
        else:
            b = m
    return int(math.floor((a + b) / 2.0 + 0.5))


def civil(m26: int) -> str:
    t = A.Time((m26 + M2026) / 1440.0 - 0.5)
    return str(t)[:16].replace("T", " ")


def main() -> int:
    OUT.mkdir(exist_ok=True)
    rows, blob, report = {}, bytearray(), []
    blob += bytes([len(SHOWERS), Y0 - 2000, Y1 - Y0 + 1, 0])
    for i, (code, name, lam, zhr, octant, col) in enumerate(SHOWERS):
        pk = []
        for y in range(Y0, Y1 + 1):
            p = peak(lam, y)
            # a shower whose peak falls in the first days of January belongs to that year (Quadrantids): fine as is
            pk.append(p)
        n = len(pk)
        # least-squares line through the peaks, then residuals
        xs = list(range(n))
        mx, my = sum(xs) / n, sum(pk) / n
        step = sum((x - mx) * (p - my) for x, p in zip(xs, pk)) / sum((x - mx) ** 2 for x in xs)
        step_i = int(round(step))
        base = pk[0]
        raw = [p - (base + k * step_i) for k, p in enumerate(pk)]
        # centre the residuals: move the base so they straddle zero
        off = (max(raw) + min(raw)) // 2
        base += off
        raw = [r - off for r in raw]
        scale = max(1, math.ceil(max(abs(r) for r in raw) / 127))
        res = [int(round(r / scale)) for r in raw]
        err = max(abs(base + k * step_i + res[k] * scale - p) for k, p in enumerate(pk))
        rows[code] = dict(name=name, lam_j2000=lam, zhr=zhr, octant=octant, colour=col,
                          peaks=pk, peaks_utc=[civil(p) for p in pk])
        blob += bytes([i, min(zhr, 255), (col << 4) | octant, scale])
        blob += base.to_bytes(4, "little") + (step_i - 525000).to_bytes(2, "little")
        blob += bytes((r & 0xFF) for r in res)
        report.append(f"{code} {name:16s} lam {lam:7.2f}  2026 {civil(pk[0])}  2126 {civil(pk[-1])}  "
                      f"step {step_i} min/yr  scale {scale} min  max decode error {err} min")
        print(report[-1], flush=True)
    (OUT / "meteors.json").write_text(json.dumps(dict(base="minutes since 2026-01-01 00:00 UTC", years=[Y0, Y1],
                                                      showers=rows, bytes=len(blob)), indent=1))
    body = ",\n  ".join(", ".join(f"0x{b:02X}" for b in blob[k:k + 16]) for k in range(0, len(blob), 16))
    (OUT / "meteor_tab.c").write_text(
        "/* generated by proto/skyscenes/meteors.py: annual meteor-shower peaks 2026..2126 (see the header there) */\n"
        f'#include "meteor_tab.h"\nconst uint8_t meteor_tab[{len(blob)}] = {{\n  {body}\n}};\n')
    (OUT / "meteor_tab.h").write_text(
        "#ifndef EG_METEOR_TAB_H\n#define EG_METEOR_TAB_H\n#include <stdint.h>\n"
        f"#define METEOR_NSHOWER {len(SHOWERS)}\n#define METEOR_REC 111u\n#define METEOR_Y0 {Y0}\n"
        f"extern const uint8_t meteor_tab[{len(blob)}];\n"
        "/* peak minute (since 2026-01-01 UTC) of shower s in year y (2026..2126) */\n"
        "static inline uint32_t meteor_peak(uint8_t s, uint8_t yi) {\n"
        "  const uint8_t *r = meteor_tab + 4 + (uint16_t)s * METEOR_REC;\n"
        "  uint32_t base = r[4] | ((uint32_t)r[5] << 8) | ((uint32_t)r[6] << 16) | ((uint32_t)r[7] << 24);\n"
        "  uint32_t step = 525000ul + (r[8] | ((uint16_t)r[9] << 8));\n"
        "  return base + (uint32_t)yi * step + (int32_t)(int8_t)r[10 + yi] * r[3];\n}\n#endif\n")
    print(f"table bytes: {len(blob)}  ({len(SHOWERS)} showers x {Y1 - Y0 + 1} years)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
