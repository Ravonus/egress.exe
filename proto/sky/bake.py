"""Bake the discrete sky events of 2026-01-01 .. 2127-01-01 with Astronomy Engine (host only).

    .venv/bin/python proto/sky/bake.py        # -> proto/sky/out/events.json (about a minute)

Every event is a whole minute since 2000-01-01 00:00 UTC (the cartridge's time base), rounded to the nearest minute.
- moon quarters (new, first quarter, full, last quarter): Astronomy Engine's NextMoonQuarter;
- lunar perigees (NextLunarApsis), and the full and new moons that count as super (below);
- eclipses: global solar (partial, annular, total; hybrid is stored as total) and lunar (penumbral, partial, total),
  each tied to its new or full moon;
- stations (retrograde and direct) of Mercury .. Pluto: zeros of the apparent geocentric ecliptic longitude's speed,
  found from daily samples and bisected to the minute;
- sign ingresses of the Sun and Mercury .. Pluto (not the Moon): crossings of a multiple of 30 degrees, bisected to
  the minute; a backward crossing (retrograde re-entry) is flagged. The Sun's 0/90/180/270 crossings are the
  equinoxes and solstices.
A super moon here is a new or full moon within 361,000 km of the Earth's centre (about the 90%-of-perigee rule).
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import astronomy as A

sys.path.insert(0, str(Path(__file__).parent))
import ref  # noqa: E402

OUT = Path(__file__).parent / "out"
D0, D1 = 9497, 46387  # 2026-01-01 .. 2127-01-01
M0, M1 = D0 * 1440, D1 * 1440
SUPER_KM = 361000.0
PLANETS = ["MERCURY", "VENUS", "MARS", "JUPITER", "SATURN", "URANUS", "NEPTUNE", "PLUTO"]
INGRESS = ["SUN"] + PLANETS


def rmin(t: A.Time) -> int:
    return int(math.floor(ref.minutes_of(t) + 0.5))


def bisect(f, a: float, b: float) -> float:
    """f(a) and f(b) differ in sign (as booleans); return the crossing to well under a minute."""
    fa = f(a)
    while b - a > 0.05:
        m = (a + b) / 2.0
        if f(m) == fa:
            a = m
        else:
            b = m
    return (a + b) / 2.0


def moon_events() -> tuple[list, list]:
    q = A.SearchMoonQuarter(ref.atime(M0))
    phases = []
    while ref.minutes_of(q.time) < M1:
        phases.append([rmin(q.time), q.quarter])
        q = A.NextMoonQuarter(q)
    ap = A.SearchLunarApsis(ref.atime(M0))
    peri = []
    while ref.minutes_of(ap.time) < M1:
        if ap.kind == A.ApsisKind.Pericenter:
            peri.append([rmin(ap.time), round(ap.dist_km)])
        ap = A.NextLunarApsis(ap)
    return phases, peri


def eclipses() -> list:
    out = []
    e = A.SearchGlobalSolarEclipse(ref.atime(M0))
    kinds = {A.EclipseKind.Partial: "partial", A.EclipseKind.Annular: "annular", A.EclipseKind.Total: "total"}
    while ref.minutes_of(e.peak) < M1:
        out.append([rmin(e.peak), "solar", kinds.get(e.kind, "total")])  # hybrid -> total
        e = A.NextGlobalSolarEclipse(e.peak)
    e = A.SearchLunarEclipse(ref.atime(M0))
    lk = {A.EclipseKind.Penumbral: "penumbral", A.EclipseKind.Partial: "partial", A.EclipseKind.Total: "total"}
    while ref.minutes_of(e.peak) < M1:
        out.append([rmin(e.peak), "lunar", lk[e.kind]])
        e = A.NextLunarEclipse(e.peak)
    return sorted(out)


def stations_and_ingresses(body: str) -> tuple[list, list, int, int]:
    days = list(range(D0 - 2, D1 + 3))
    lon = [ref.geo_lon(body, d * 1440.0) for d in days]
    st, ing = [], []
    spd = [ref.dang(lon[i + 1], lon[i]) for i in range(len(lon) - 1)]

    def speed(m: float) -> bool:
        return ref.dang(ref.geo_lon(body, m + 90.0), ref.geo_lon(body, m - 90.0)) < 0.0

    if body != "SUN":
        for i in range(1, len(spd)):
            if (spd[i - 1] < 0) != (spd[i] < 0):
                a, b = (days[i - 1] + 0.5) * 1440.0, (days[i] + 0.5) * 1440.0
                m = bisect(speed, a, b)
                if M0 <= m < M1:
                    st.append([int(math.floor(m + 0.5)), "R" if spd[i] < 0 else "D"])
    for i in range(len(lon) - 1):
        s0, s1 = int(lon[i] // 30), int(lon[i + 1] // 30)
        if s0 != s1:
            fwd = (s1 - s0) % 12 == 1
            sgn = s1 if fwd else s0  # the boundary crossed is the start of sign s1 (fwd) or of s0 (backward)
            bnd = sgn * 30.0
            a, b = days[i] * 1440.0, days[i + 1] * 1440.0
            m = bisect(lambda x: ref.dang(ref.geo_lon(body, x), bnd) >= 0.0, a, b)
            if M0 <= m < M1:
                ing.append([int(math.floor(m + 0.5)), s1, 0 if fwd else 1])
    i0 = days.index(D0)
    sign0 = int(ref.geo_lon(body, M0) // 30)
    retro0 = 1 if ref.dang(ref.geo_lon(body, M0 + 720), ref.geo_lon(body, M0 - 720)) < 0 else 0
    del i0
    return st, ing, sign0, retro0


def main() -> int:
    OUT.mkdir(exist_ok=True)
    ev: dict = {"range_minutes": [M0, M1], "super_km": SUPER_KM}
    phases, peri = moon_events()
    ev["phases"], ev["perigee"] = phases, peri
    print(f"moon quarters {len(phases)}, perigees {len(peri)}", flush=True)
    ev["eclipses"] = eclipses()
    print(f"eclipses {len(ev['eclipses'])}", flush=True)
    sup = []
    for m, q in phases:
        if q in (0, 2):
            dist = A.GeoMoon(ref.atime(m)).Length() * A.KM_PER_AU
            if dist < SUPER_KM:
                sup.append([m, q, round(dist)])
    ev["super"] = sup
    print(f"super new/full moons {len(sup)}", flush=True)
    ev["stations"], ev["ingress"], ev["start"] = {}, {}, {}
    for b in INGRESS:
        st, ing, s0, r0 = stations_and_ingresses(b)
        if b != "SUN":
            ev["stations"][b] = st
        ev["ingress"][b] = ing
        ev["start"][b] = [s0, r0]
        print(f"{b:8s} stations {len(st):4d} ingresses {len(ing):4d}", flush=True)
    seasons = []
    for y in range(2026, 2127):
        s = A.Seasons(y)
        seasons += [rmin(s.mar_equinox), rmin(s.jun_solstice), rmin(s.sep_equinox), rmin(s.dec_solstice)]
    ev["seasons"] = seasons
    (OUT / "events.json").write_text(json.dumps(ev))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
