"""Verify the cartridge sky engine (built on the host) against Astronomy Engine over 2026-2126.

    .venv/bin/python proto/sky/verify.py          # builds out/host_dump, writes out/verify.json, prints the tables

Checks:
1. every baked event decodes (sky_find walk) to exactly the baked minute, state and flag;
2. live longitudes vs Astronomy Engine (apparent, of date): Sun, Moon, Mercury..Saturn, plus Ascendant and MC
   against the textbook formula on Astronomy Engine's sidereal time;
3. live retrograde (the 6-hour motion) vs the baked stations, and live station dates (fallback) vs baked;
4. the fallback (SKY_LIVE): phases and eclipses from the live model vs the tables;
5. void-of-course moon vs a reference computed from Astronomy Engine positions;
6. published real-world dates (fixtures.json).
"""

from __future__ import annotations

import datetime as dt
import json
import math
import os
import random
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).parent
sys.path.insert(0, str(HERE))
import ref  # noqa: E402

OUT = HERE / "out"
D0, D1 = 9497, 46387
EPOCH = dt.datetime(2000, 1, 1)
NAMES = ["SUN", "MOON", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN", "NODE", "ASC", "MC"]


def build() -> Path:
    exe = OUT / "host_dump"
    srcs = ["host_dump.c", "sky.c", "sky_bank0.c", "out/sky_tab.c", "out/sky_ev.c"] + sorted(str(p.relative_to(HERE)) for p in OUT.glob("sky_ev_b*.c"))
    subprocess.run(["cc", "-O2", "-std=c99", "-Wall", "-Wextra", "-DSKY_HOST_TEST", "-I.", "-Iout", "-o", str(exe), *srcs],
                   cwd=HERE, check=True)
    return exe


def run(exe: Path, *args: str, live: bool = False) -> list[list[int]]:
    env = dict(os.environ)
    if live:
        env["SKY_LIVE"] = "1"
    r = subprocess.run([str(exe), *args], capture_output=True, text=True, check=True, env=env)
    return [[int(x) for x in ln.split()] for ln in r.stdout.splitlines()]


def deg(u: int) -> float:
    return u * 360.0 / 65536.0


def stamp(m: int) -> str:
    return (EPOCH + dt.timedelta(minutes=m)).strftime("%Y-%m-%d %H:%M")


def mday(s: str) -> int:
    d = dt.datetime.strptime(s[:10], "%Y-%m-%d")
    return (d - EPOCH).days


def main() -> int:
    exe = build()
    ev = json.loads((OUT / "events.json").read_text())
    tabs = json.loads((OUT / "tables.json").read_text())
    rep: dict = {}
    M26 = 9497 * 1440

    # 1. exact decode
    dec = run(exe, "events")
    names = [s["name"] for s in tabs["streams"]]
    by: dict[str, list] = {n: [] for n in names}
    marks = {}
    for row in dec:
        by[names[row[0]]].append(row[2:5])
        if len(row) > 5 and row[5]:
            marks[row[1]] = (row[5], row[6])
    want = {"phase": [[m - M26, q, 0] for m, q in ev["phases"]], "perigee": [[m - M26, 0, 0] for m, _ in ev["perigee"]]}
    for p, st in ev["stations"].items():
        want["st_" + p.lower()] = [[m - M26, 1 if k == "R" else 0, 0] for m, k in st]
    for p, ing in ev["ingress"].items():
        want["in_" + p.lower()] = [[m - M26, s, f] for m, s, f in ing]
    bad = {n: sum(1 for a, b in zip(by[n], want[n]) if a != b) + abs(len(by[n]) - len(want[n])) for n in names}
    pm = [m for m, _ in ev["phases"]]
    kinds = {("solar", "partial"): 1, ("solar", "annular"): 2, ("solar", "total"): 3, ("lunar", "penumbral"): 5,
             ("lunar", "partial"): 6, ("lunar", "total"): 7}
    ecl_bad = 0
    for m, who, k in ev["eclipses"]:
        i = min(range(max(0, np.searchsorted(pm, m) - 1), min(len(pm), np.searchsorted(pm, m) + 1)), key=lambda j: abs(pm[j] - m))
        f, off = marks.get(i, (0, 0))
        if (f & 7) != kinds[(who, k)] or pm[i] + off != m:
            ecl_bad += 1
    sup_bad = sum(1 for m, q, _ in ev["super"] if not (marks.get(pm.index(m), (0, 0))[0] & 8))
    nev = sum(len(v) for v in want.values()) + len(ev["eclipses"]) + len(ev["super"])
    rep["decode"] = dict(events=nev, mismatches=sum(bad.values()) + ecl_bad + sup_bad, per_stream=bad,
                         eclipse_mismatch=ecl_bad, super_mismatch=sup_bad)
    print(f"1. decode: {nev} events, {rep['decode']['mismatches']} mismatches", flush=True)

    # 2. live longitudes
    step = 731 if "--quick" not in sys.argv else 4001  # minutes: 12 h 11 min, so the samples walk through every hour of the day
    rows = run(exe, "pos", str(D0), str(D1), str(step))
    random.seed(7)
    err: dict[str, list[float]] = {n: [] for n in NAMES if n != "NODE"}
    for r in rows:
        m = r[0] * 1440 + r[1]
        for i, n in enumerate(NAMES[:7]):
            err[n].append(abs(ref.dang(deg(r[2 + i]), ref.geo_lon(n, m))))
        err["ASC"].append(abs(ref.dang(deg(r[2 + 8]), ref.ascendant(m))))
        t = ref.atime(m)
        ramc = math.radians(ref.A.SiderealTime(t) * 15.0)
        mc = math.degrees(math.atan2(math.sin(ramc), math.cos(ramc) * math.cos(math.radians(ref.OBLIQ)))) % 360.0
        err["MC"].append(abs(ref.dang(deg(r[2 + 9]), mc)))
    rep["longitude"] = {}
    print(f"2. live longitudes vs Astronomy Engine, {len(rows)} samples 2026-2126 (deg)")
    print("| body | mean | p99 | max | target |")
    for n, e in err.items():
        a = np.array(e)
        tgt = 2.0 if n in ("JUPITER", "SATURN") else 1.0
        rep["longitude"][n] = dict(mean=round(float(a.mean()), 4), p99=round(float(np.percentile(a, 99)), 4),
                                   max=round(float(a.max()), 4), target=tgt)
        print(f"| {n} | {a.mean():.3f} | {np.percentile(a, 99):.3f} | {a.max():.3f} | {tgt} |", flush=True)
    if "--quick" in sys.argv:
        return 0
    # the ascendant formula itself, against a brute-force horizon search on Astronomy Engine (a few moments)
    ax = []
    obs = ref.A.Observer(ref.LAT, 0.0, 0.0)
    for k in range(40):
        m = random.randrange(D0 * 1440, D1 * 1440)
        t = ref.atime(m)
        best = None
        for lam10 in range(0, 3600):
            lam = math.radians(lam10 / 10.0)
            e = math.radians(ref.OBLIQ)
            ra = math.degrees(math.atan2(math.sin(lam) * math.cos(e), math.cos(lam))) / 15.0 % 24.0
            dec_ = math.degrees(math.asin(math.sin(lam) * math.sin(e)))
            h = ref.A.Horizon(t, obs, ra, dec_, ref.A.Refraction.Airless)
            if 0 < h.azimuth < 180 and (best is None or abs(h.altitude) < best[0]):
                best = (abs(h.altitude), lam10 / 10.0)
        ax.append(abs(ref.dang(best[1], ref.ascendant(m))))
    rep["asc_formula_vs_horizon_max"] = round(max(ax), 3)
    print(f"   ascendant formula vs a horizon search (40 moments): max {max(ax):.3f} deg", flush=True)

    # 3. retrograde: the live motion over two days (t-1 d .. t+1 d, the fallback's rule) vs the baked stations
    rows = run(exe, "pos", str(D0), str(D1), "360")
    lon = np.array([r[2:9] for r in rows], dtype=np.int64)
    tmin = np.array([r[0] * 1440 + r[1] for r in rows])
    rep["retro"] = {}
    print("3. retrograde: live (two-day motion) vs baked stations")
    print("| planet | samples agree | disagreements, farthest from a station (days) | worst live station error (days) | stations |")
    for b, p in enumerate(["MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"]):
        col = lon[:, 2 + b]
        mot = ((col[8:] - col[:-8] + 32768) % 65536) - 32768
        tm = tmin[4:-4]
        live = mot < 0
        st = ev["stations"][p]
        sm = np.array([m for m, _ in st])
        state0 = ev["start"][p][1]
        k = np.searchsorted(sm, tm, side="right")
        table = np.array([(1 if st[j - 1][1] == "R" else 0) if j else state0 for j in k]).astype(bool)
        dis = tm[live != table]
        far = max((float(np.min(np.abs(sm - x))) / 1440.0 for x in dis), default=0.0)
        flips = tm[1:][live[1:] != live[:-1]]
        worst = max(float(np.min(np.abs(flips - m))) / 1440.0 for m in sm)
        rep["retro"][p] = dict(agree=round(float((live == table).mean()), 5), disagree_farthest_days=round(far, 2),
                               worst_station_days=round(worst, 2), stations=len(sm), live_flips=int(len(flips)))
        print(f"| {p} | {(live == table).mean() * 100:.3f}% | {far:.2f} | {worst:.2f} | {len(sm)} (live {len(flips)}) |", flush=True)

    # 4. the fallback: phases and eclipses from the live model alone (SKY_LIVE), daily at 00:00 and 12:00
    rd = run(exe, "read", str(D0), str(D1), "720", live=True)
    tab = run(exe, "read", str(D0), str(D1), "720")
    ph_diff = [abs(((a[3] - b[3] + 4) % 8) - 4) for a, b in zip(rd, tab)]
    # eclipses: a live call within a day of each baked one, and live calls with no baked eclipse within 2 days
    ecl_live = [(r[0] * 1440 + r[1], r[4]) for r in rd if r[4]]
    hit = 0
    kind_ok = 0
    cls = {1: "solar partial", 2: "solar central", 3: "solar central", 5: "lunar penumbral", 6: "lunar partial", 7: "lunar total"}
    missed: dict[str, int] = {}
    for m, who, k in ev["eclipses"]:
        near = [e for e in ecl_live if abs(e[0] - m) <= 1440]
        want_k = kinds[(who, k)]
        if near:
            hit += 1
            kind_ok += int(cls[near[0][1]] == cls[want_k])
        else:
            missed[cls[want_k]] = missed.get(cls[want_k], 0) + 1
    rep["fallback_eclipse_missed_by_kind"] = missed
    em = [e[0] for e in ev["eclipses"]]
    false = len({(m // 1440) for m, _ in ecl_live if min(abs(x - m) for x in em) > 2880})
    rep["fallback"] = dict(phase8_exact=round(ph_diff.count(0) / len(ph_diff), 4), phase8_off_by_one=ph_diff.count(1),
                           phase8_worse=sum(1 for d in ph_diff if d > 1), eclipses=len(ev["eclipses"]), eclipses_found=hit,
                           eclipse_kind_rough_ok=kind_ok, false_eclipse_days=false)
    print(f"4. fallback: phase8 equal to the table {ph_diff.count(0) / len(ph_diff) * 100:.2f}%, off by one {ph_diff.count(1)}, "
          f"worse {rep['fallback']['phase8_worse']}; eclipses found {hit}/{len(ev['eclipses'])} (rough kind right {kind_ok}), "
          f"false eclipse days {false}; missed by kind {missed}", flush=True)
    # live phase timing: the live new/full moon (elongation crossing) vs the baked one
    rows = run(exe, "pos", str(D0), str(D1), "60")
    el = np.array([(r[3] - r[2]) % 65536 for r in rows])
    tm = np.array([r[0] * 1440 + r[1] for r in rows])
    cross = tm[1:][(el[1:] < el[:-1])]  # wraps through 0: new moons
    newm = np.array([m for m, q in ev["phases"] if q == 0])
    dmin = [int(np.min(np.abs(cross - m))) for m in newm]
    rep["live_new_moon_minutes_max"] = max(dmin)
    print(f"   live new moons vs baked: worst {max(dmin)} min over {len(newm)}", flush=True)

    # 5. void of course: the C rule vs Astronomy Engine positions, every 2 hours for 2026-2027
    vr = run(exe, "read", "9497", "10227", "120")
    agree = 0
    tot = 0
    AS = [0, 90, 120, 180, 240, 270]
    for r in vr[::6]:
        m = r[0] * 1440 + r[1]
        mo = ref.geo_lon("MOON", m)
        sign = int(mo // 30)
        t = m
        void = True
        while True:
            t2 = t + 30
            mo2 = ref.geo_lon("MOON", t2)
            if int(mo2 // 30) != sign:
                break
            for b in ["SUN", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"]:
                e1 = (ref.geo_lon("MOON", t) - ref.geo_lon(b, t)) % 360
                e2 = (mo2 - ref.geo_lon(b, t2)) % 360
                for a in AS:
                    if ((a - e1) % 360) <= ((e2 - e1) % 360) and (e2 - e1) % 360 < 30:
                        void = False
            if not void:
                break
            t = t2
        tot += 1
        agree += int(void == bool(r[2] & 2))
    rep["voc_agree"] = round(agree / tot, 4)
    print(f"5. void of course: {agree}/{tot} samples agree ({agree / tot * 100:.1f}%)", flush=True)

    # 6. published dates
    fx = json.loads((HERE / "fixtures.json").read_text())
    pub = []
    for p, lst in fx["stations"].items():
        st = ev["stations"][p]
        for day, k in lst:
            d = mday(day)
            best = min(st, key=lambda s: abs(s[0] / 1440.0 - d - 0.5))
            pub.append(dict(what=f"{p} station {k}", published=day, engine=stamp(best[0]), days=round(best[0] / 1440.0 - d - 0.5, 2),
                            type_ok=best[1] == k))
    for day, who, k in fx["eclipses"]:
        d = mday(day)
        best = min(ev["eclipses"], key=lambda e: abs(e[0] / 1440.0 - d - 0.5))
        pub.append(dict(what=f"{who} eclipse ({k})", published=day, engine=f"{stamp(best[0])} {best[2]}",
                        days=round(best[0] / 1440.0 - d - 0.5, 2), type_ok=best[1] == who and best[2] == k))
    for s, what in fx["seasons_2026"]:
        m = int((dt.datetime.strptime(s, "%Y-%m-%d %H:%M") - EPOCH).total_seconds() // 60)
        best = min(ev["ingress"]["SUN"], key=lambda e: abs(e[0] - m))
        pub.append(dict(what=what, published=s, engine=stamp(best[0]), days=round((best[0] - m) / 1440.0, 4),
                        type_ok=best[1] in (0, 3, 6, 9)))
    for day in fx["full_moons_2026"]:
        d = mday(day)
        best = min((e for e in ev["phases"] if e[1] == 2), key=lambda e: abs(e[0] / 1440.0 - d - 0.5))
        pub.append(dict(what="full moon", published=day, engine=stamp(best[0]), days=round(best[0] / 1440.0 - d - 0.5, 2), type_ok=True))
    rep["published"] = pub
    miss = [p for p in pub if abs(p["days"]) > 1.0 or not p["type_ok"]]
    rep["published_misses"] = miss
    print(f"6. published dates: {len(pub)} checked, {len(miss)} off by more than a day or of the wrong kind")
    for p in miss:
        print("   MISS", p)
    (OUT / "verify.json").write_text(json.dumps(rep, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
