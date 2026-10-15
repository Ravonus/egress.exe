"""Elites: any mob of any mobility, family or size plus 1-2 affixes (MOBGEN elite parts: pulsing palette-7 outline,
crown, orbiting particles, size bump; swarm elites glow only; mimic elites keep the disguise with a rim shimmer).

The pulse is the elite palette phase, which alternates every frame of every state, so even a held pose shimmers.
Extra state: intro (the entrance: name tag, a 12-frame pause, camera nudge, its own sting).
HASTED elites run every movement state 1.5x faster (durations divided).
"""
from __future__ import annotations

import core as C
from core import M
import mobs

TYPE = "elite"
DIRS = 4
REQUIRED = list(C.MIN_STATES) + ["intro"]


def variants(n, seed=0):
    out = []
    for i in range(n):
        m = mobs.MOBILITIES[i % len(mobs.MOBILITIES)]
        fam = M.FAMILIES[(i * 3 + seed) % 8]
        szs = mobs.sizes_for(m)
        out.append(mobs.make(fam, szs[(i + seed) % len(szs)], m, M.mix_seed(0xE1E7, seed, i), elite=True))
    return out


def coverage_variants(seed=0):
    """every mobility, every allowed size, two families each"""
    out = []
    for mi, m in enumerate(mobs.MOBILITIES):
        for k, sc in enumerate(mobs.sizes_for(m)):
            fam = M.FAMILIES[(mi * 2 + k + seed) % 8]
            out.append(mobs.make(fam, sc, m, M.mix_seed(0xE1E8, seed, mi, sc), elite=True))
    return out


def states(v):
    return mobs.states(v)


def render(v, state, frame_index, direction):
    return mobs.render(v, state, frame_index, direction)
