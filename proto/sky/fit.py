"""Fit the cartridge's live sky model to Astronomy Engine over 2026-2126 (host only).

    .venv/bin/python proto/sky/fit.py            # writes proto/sky/out/fit.json and prints the float-model errors

The model is what the cartridge evaluates in integers (sky.c):
- each body has a mean longitude (a constant and a daily rate) and a short trigonometric series whose arguments are
  small integer combinations of mean angles (the Standish/Meeus mean elements); the amplitudes and phases are a
  linear least-squares fit to Astronomy Engine, so the series is tuned to this century, not to J2000;
- planets: a heliocentric longitude series and a radius series, then helio -> geo with the Earth's own series;
- the Moon: a geocentric series in Meeus's arguments D, M, M', F (the biggest terms of ELP-2000);
- the Sun: the Earth's heliocentric longitude + 180 degrees.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import ref  # noqa: E402

OUT = Path(__file__).parent / "out"
D0, D1 = 9497.0, 46387.0  # 2026-01-01 .. 2127-01-01 (days since 2000-01-01 00:00 UTC)


def cy(deg0: float, per_cy: float) -> tuple[float, float]:
    """Standish J2000 (noon) element -> (value at 2000-01-01 00:00, deg/day)."""
    r = per_cy / 36525.0
    return (deg0 - 0.5 * r) % 360.0, r


PREC = 1.396971 / 36525.0  # general precession in longitude, deg/day: J2000 frame -> of date
# mean longitude L and longitude of perihelion w (Standish 1800-2050), node O for the inclination term
EL = {
    "MERCURY": dict(L=cy(252.25032350, 149472.67411175), w=cy(77.45779628, 0.16047689), O=cy(48.33076593, -0.12534081)),
    "VENUS": dict(L=cy(181.97909950, 58517.81538729), w=cy(131.60246718, 0.00268329), O=cy(76.67984255, -0.27769418)),
    "EARTH": dict(L=cy(100.46457166, 35999.37244981), w=cy(102.93768193, 0.32327364), O=None),
    "MARS": dict(L=cy(-4.55343205, 19140.30268499), w=cy(-23.94362959, 0.44441088), O=cy(49.55953891, -0.29257343)),
    "JUPITER": dict(L=cy(34.39644051, 3034.74612775), w=cy(14.72847983, 0.21252668), O=cy(100.47390909, 0.20469106)),
    "SATURN": dict(L=cy(49.95424423, 1222.49362201), w=cy(92.59887831, -0.41897216), O=cy(113.66242448, -0.28867794)),
}
# the fundamental mean angles the series use, (deg at d=0, deg/day); "of date": add precession to longitudes
ANG: dict[str, tuple[float, float]] = {}
for p, e in EL.items():
    L0, Lr = e["L"]
    w0, wr = e["w"]
    ANG["L" + p[:2]] = (L0, Lr + PREC)  # mean longitude of date
    ANG["M" + p[:2]] = ((L0 - w0) % 360.0, Lr - wr)  # mean anomaly
    if e["O"]:
        O0, Or = e["O"]
        ANG["U" + p[:2]] = ((L0 - O0) % 360.0, Lr - Or)  # mean argument of latitude
# the Moon (Meeus 47, at d=0: T = -0.5/36525)
def mj(c0, c1):
    return ((c0 - 0.5 * c1 / 36525.0) % 360.0, c1 / 36525.0)
ANG["LMO"] = mj(218.3164477, 481267.88123421)
ANG["D"] = mj(297.8501921, 445267.1114034)
ANG["MS"] = mj(357.5291092, 35999.0502909)
ANG["MM"] = mj(134.9633964, 477198.8675055)
ANG["F"] = mj(93.2720950, 483202.0175233)
ANG["A1"] = mj(119.75, 131.849)
ANG["NODE"] = mj(125.0445479, -1934.1362891)  # the mean lunar node

# series arguments: list of {angle: multiplier}
def kM(p, ks):
    return [{"M" + p[:2]: k} for k in ks]

JS = [(2, -2), (3, -5), (1, -2), (1, -1), (2, -3), (1, -5), (2, -4), (2, -6), (1, -3), (3, -6), (3, -4)]
SERIES = {
    "MERCURY": dict(lon=kM("MERCURY", range(1, 7)) + [{"UME": 2}, {"MME": 1, "MVE": -1}], rad=kM("MERCURY", range(1, 5))),
    "VENUS": dict(lon=kM("VENUS", range(1, 3)) + [{"UVE": 2}, {"MVE": 2, "MEA": -2}, {"MVE": 1, "MEA": -1}, {"MVE": 3, "MEA": -5}],
                  rad=kM("VENUS", range(1, 3)) + [{"MVE": 2, "MEA": -2}]),
    "EARTH": dict(lon=kM("EARTH", range(1, 4)) + [{"D": 1}, {"MEA": 2, "MVE": -2}, {"MEA": 1, "MJU": -1}, {"MEA": 1, "MVE": -1},
                  {"MEA": 2, "MJU": -2}, {"MEA": 2, "MMA": -2}, {"MEA": 1, "MMA": -1}],
                  rad=kM("EARTH", range(1, 3)) + [{"D": 1}, {"MEA": 2, "MVE": -2}]),
    "MARS": dict(lon=kM("MARS", range(1, 5)) + [{"UMA": 2}, {"MMA": 2, "MJU": -2}, {"MMA": 1, "MJU": -1}, {"MMA": 2, "MEA": -2},
                 {"MMA": 2, "MEA": -1}, {"MMA": 4, "MEA": -2}],
                 rad=kM("MARS", range(1, 4)) + [{"MMA": 2, "MJU": -2}, {"MMA": 2, "MEA": -2}]),
    "JUPITER": dict(lon=kM("JUPITER", range(1, 4)) + [{"MJU": a, "MSA": b} for a, b in JS] + [{"UJU": 2}],
                    rad=kM("JUPITER", range(1, 3)) + [{"MJU": a, "MSA": b} for a, b in JS[:6]]),
    "SATURN": dict(lon=kM("SATURN", range(1, 4)) + [{"MJU": a, "MSA": b} for a, b in JS] + [{"USA": 2}],
                   rad=kM("SATURN", range(1, 4)) + [{"MJU": a, "MSA": b} for a, b in JS[:6]]),
}
# Moon: Meeus table 47.A arguments (D, M, M', F), the larger terms, + A1 and L'-F
MOON_ARGS = [(0, 0, 1, 0), (2, 0, -1, 0), (2, 0, 0, 0), (0, 0, 2, 0), (0, 1, 0, 0), (0, 0, 0, 2), (2, 0, -2, 0), (2, -1, -1, 0),
             (2, 0, 1, 0), (2, -1, 0, 0), (0, 1, -1, 0), (1, 0, 0, 0), (0, 1, 1, 0), (2, 0, 0, -2), (0, 0, 1, 2), (0, 0, 1, -2),
             (4, 0, -1, 0), (0, 0, 3, 0), (4, 0, -2, 0), (2, 1, -1, 0), (2, 1, 0, 0), (1, 0, -1, 0), (1, 1, 0, 0), (2, -1, 1, 0),
             (2, 0, 2, 0), (4, 0, 0, 0), (2, 0, -3, 0), (0, 1, -2, 0), (2, 0, -1, 2), (2, -1, -2, 0), (1, 0, 1, 0), (2, -2, 0, 0),
             (0, 1, 2, 0), (0, 2, 0, 0)]
MOON_SERIES = [dict(zip(("D", "MS", "MM", "F"), a)) for a in MOON_ARGS] + [{"A1": 1}, {"LMO": 1, "F": -1}]


def ang(name: str, d: np.ndarray) -> np.ndarray:
    a0, r = ANG[name]
    return np.radians(a0 + r * d)


def arg(term: dict, d: np.ndarray) -> np.ndarray:
    return sum(k * ang(n, d) for n, k in term.items() if k)


def design(terms: list[dict], d: np.ndarray, with_lin: bool) -> np.ndarray:
    cols = [np.ones_like(d)]
    if with_lin:
        cols.append((d - 27000.0) / 10000.0)
    for t in terms:
        a = arg(t, d)
        cols += [np.sin(a), np.cos(a)]
    return np.stack(cols, 1)


def ridge(X: np.ndarray, y: np.ndarray, lam: float = 1e-6) -> np.ndarray:
    """Least squares with a whisper of Tikhonov damping: near-degenerate long-period arguments stay small."""
    n = X.shape[1]
    A_ = X.T @ X + lam * len(y) * np.eye(n)
    return np.linalg.solve(A_, X.T @ y)


def to_terms(terms: list[dict], c: np.ndarray, off: int) -> list[dict]:
    """sin/cos pairs -> amplitude * sin(arg + phase)."""
    out = []
    for i, t in enumerate(terms):
        s, co = c[off + 2 * i], c[off + 2 * i + 1]
        out.append(dict(arg=t, amp=float(math.hypot(s, co)), ph=float(math.degrees(math.atan2(co, s)) % 360.0)))
    return out


def fit_body(p: str, d: np.ndarray, lon: np.ndarray, rad: np.ndarray | None) -> dict:
    Lname = "L" + p[:2]
    base = np.degrees(ang(Lname, d))
    res = (lon - base + 180.0) % 360.0 - 180.0
    S = SERIES[p]
    X = design(S["lon"], d, True)
    c = ridge(X, res)
    out = dict(L=Lname, c0=float(c[0]), c1=float(c[1]), lon=to_terms(S["lon"], c, 2))
    fitted = X @ c
    out["lon_resid_max"] = float(np.max(np.abs(res - fitted)))
    if rad is not None:
        Xr = design(S["rad"], d, True)
        cr = ridge(Xr, rad)
        out.update(r0=float(cr[0]), r1=float(cr[1]), rad=to_terms(S["rad"], cr, 2))
        out["rad_resid_max"] = float(np.max(np.abs(rad - Xr @ cr)))
    return out


def eval_series(terms: list[dict], d: np.ndarray) -> np.ndarray:
    return sum(t["amp"] * np.sin(arg(t["arg"], d) + math.radians(t["ph"])) for t in terms)


def eval_lon(f: dict, d: np.ndarray) -> np.ndarray:
    return (np.degrees(ang(f["L"], d)) + f["c0"] + f["c1"] * (d - 27000.0) / 10000.0 + np.degrees(0) + eval_series(f["lon"], d)) % 360.0


def eval_rad(f: dict, d: np.ndarray) -> np.ndarray:
    return f["r0"] + f["r1"] * (d - 27000.0) / 10000.0 + eval_series(f["rad"], d)


def main() -> int:
    OUT.mkdir(exist_ok=True)
    fits: dict = {"angles": {k: list(v) for k, v in ANG.items()}}
    for p, step in (("MERCURY", 1.3), ("VENUS", 2.1), ("EARTH", 2.3), ("MARS", 2.9), ("JUPITER", 7.1), ("SATURN", 9.3)):
        d = np.arange(D0, D1, step)
        lr = np.array([ref.helio(p, x * 1440.0) for x in d])
        fits[p] = fit_body(p, d, lr[:, 0], lr[:, 1])
        print(f"{p:8s} helio lon resid max {fits[p]['lon_resid_max']:.4f} deg, radius {fits[p]['rad_resid_max']:.5f} AU")
    d = np.arange(D0, D1, 0.173)
    ml = np.array([ref.geo_lon("MOON", x * 1440.0) for x in d])
    base = np.degrees(ang("LMO", d))
    res = (ml - base + 180.0) % 360.0 - 180.0
    X = design(MOON_SERIES, d, True)
    c, *_ = np.linalg.lstsq(X, res, rcond=None)
    fits["MOON"] = dict(L="LMO", c0=float(c[0]), c1=float(c[1]), lon=to_terms(MOON_SERIES, c, 2),
                        lon_resid_max=float(np.max(np.abs(res - X @ c))))
    print(f"MOON     geo lon resid max {fits['MOON']['lon_resid_max']:.4f} deg ({len(d)} samples)")
    (OUT / "fit.json").write_text(json.dumps(fits, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
