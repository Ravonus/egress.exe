"""Reference sky for EGRESS (host only): Astronomy Engine (MIT, `pip install astronomy-engine`, about arcminute
accuracy). Everything here works in the cartridge's time base: whole minutes since 2000-01-01 00:00 UTC.

Longitudes are apparent geocentric ecliptic longitudes of date (true equinox), the tropical zodiac astrology uses.
"""

from __future__ import annotations

import math

import astronomy as A

BODIES = ["SUN", "MOON", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN", "URANUS", "NEPTUNE", "PLUTO"]
_AB = {
    "SUN": A.Body.Sun, "MOON": A.Body.Moon, "MERCURY": A.Body.Mercury, "VENUS": A.Body.Venus, "MARS": A.Body.Mars,
    "JUPITER": A.Body.Jupiter, "SATURN": A.Body.Saturn, "URANUS": A.Body.Uranus, "NEPTUNE": A.Body.Neptune,
    "PLUTO": A.Body.Pluto, "EARTH": A.Body.Earth,
}
LAT = 40.0  # the shared world's building: 40 N (documented in redesign 9.7)
OBLIQ = 23.4393

M2026 = 9497 * 1440  # 2026-01-01 00:00 UTC in cartridge minutes (days since 2000-01-01: 9497)


def atime(minutes: float) -> A.Time:
    """Cartridge minutes (since 2000-01-01 00:00 UTC) -> Astronomy Engine time (UT days since J2000 noon)."""
    return A.Time(minutes / 1440.0 - 0.5)


def minutes_of(t: A.Time) -> float:
    return (t.ut + 0.5) * 1440.0


def geo_lon(body: str, minutes: float) -> float:
    t = atime(minutes)
    if body == "SUN":
        return A.SunPosition(t).elon % 360.0
    if body == "MOON":
        return A.EclipticGeoMoon(t).lon % 360.0
    v = A.GeoVector(_AB[body], t, True)
    e = A.RotateVector(A.Rotation_EQJ_ECT(t), v)
    return math.degrees(math.atan2(e.y, e.x)) % 360.0


def helio(body: str, minutes: float) -> tuple[float, float]:
    """Heliocentric ecliptic-of-date longitude (deg) and distance (AU)."""
    t = atime(minutes)
    v = A.HelioVector(_AB[body], t)
    e = A.RotateVector(A.Rotation_EQJ_ECT(t), v)
    return math.degrees(math.atan2(e.y, e.x)) % 360.0, math.hypot(e.x, e.y)


def ascendant(minutes: float, lat: float = LAT, lon_east: float = 0.0) -> float:
    t = atime(minutes)
    ramc = math.radians((A.SiderealTime(t) * 15.0 + lon_east) % 360.0)
    eps, phi = math.radians(OBLIQ), math.radians(lat)
    y = math.cos(ramc)
    x = -(math.sin(ramc) * math.cos(eps) + math.tan(phi) * math.sin(eps))
    return math.degrees(math.atan2(y, x)) % 360.0


def dang(a: float, b: float) -> float:
    """Signed smallest difference a - b in degrees."""
    return (a - b + 180.0) % 360.0 - 180.0
