"""Targeted, seeded CGB ramps. All colour-space work happens in the baker.

Index zero is transparent on OBJ; 1/2/3 are ink, body and highlight.
The seed chooses a whole ramp, never a different ramp for each animation.
"""
import colorsys
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / 'assets/alchemy/colours.json'


def rgb(hex_colour):
    return tuple(int(hex_colour.lstrip('#')[i:i+2], 16) for i in (0, 2, 4))


def word(c):
    return sum((round(v * 31 / 255) & 31) << (5 * i) for i, v in enumerate(c))


def unpack(v):
    return tuple(round(((v >> (5 * i)) & 31) * 255 / 31) for i in range(3))


def luma(c):
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000


def hash32(v):
    v = ((v ^ (v >> 16)) * 0x7feb352d) & 0xffffffff
    v = ((v ^ (v >> 15)) * 0x846ca68b) & 0xffffffff
    return v ^ (v >> 16)


def ramps():
    p = json.loads(PROFILE.read_text())
    assert p['version'] == 1 and p['variants'] == 64
    assert len(p['families']) == 24
    out = []
    for family, target in enumerate(p['families']):
        body = rgb(target['body'])
        ink = rgb(target.get('ink', p['ink']))
        tip = rgb(target.get('highlight', p['highlight']))
        assert luma(ink) < 90, 'Ink must leave room for body and highlight contrast'
        h, s, v = colorsys.rgb_to_hsv(*(x / 255 for x in body))
        family_ramps = []
        for variant in range(64):
            rand = hash32((family + 1) * 65537 + variant)
            signed = lambda shift: ((rand >> shift) & 255) / 127.5 - 1
            hue = h + (signed(0) * target.get('hueSpread', 12) / 360 if variant else 0)
            sat = max(.06, min(.95, s + (signed(8) * .16 if variant else 0)))
            val = max(.56, min(.98, v + (signed(16) * .14 if variant else 0)))
            mid = tuple(round(x * 255) for x in colorsys.hsv_to_rgb(hue % 1, sat, val))
            while luma(unpack(word(mid))) > 218:
                mid = tuple(max(0, x - 2) for x in mid)
            # Preserve readable internal shading after quantisation, including blue.
            while luma(unpack(word(mid))) < luma(unpack(word(ink))) + 70:
                mid = tuple(min(255, x + 3) for x in mid)
            high = tuple(round(mid[i] * .28 + tip[i] * .72) for i in range(3))
            while luma(unpack(word(high))) < luma(unpack(word(mid))) + 24:
                high = tuple(min(255, x + 3) for x in high)
                if high == (255, 255, 255):
                    break
            ramp = tuple(word(c) for c in [(0, 0, 0), ink, mid, high])
            assert len(set(ramp[1:])) == 3
            family_ramps.append(ramp)
        out.append(family_ramps)
    return out


def variant(seed, identity):
    """Matches catalogue_art.c's small 16-bit, multiply-free runtime selector."""
    v = (seed ^ (identity * 257)) & 65535
    v ^= v >> 7
    v ^= (v << 9) & 65535
    v ^= v >> 5
    return v & 63
