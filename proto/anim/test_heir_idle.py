"""Tests for the heir idle scheduler (heirs.idle_plan). Plain asserts; runs with pytest or directly:

    .venv/bin/python proto/anim/test_heir_idle.py
"""
from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import heirs as H  # noqa: E402

SEEDS = range(300)
POOL = set(H.FIDGETS) | {"fidget_twitch", "fidget_bounce", "ctx_shiver", "ctx_warm", "ctx_peer"}


def fidgets(plan):
    return [n for (_, _, n) in plan if n in POOL]


def test_contiguous_and_bounded():
    for s in SEEDS:
        p = H.idle_plan(s, until=1800)
        assert p[0][0] == 0 and p[-1][1] == 1800
        for (a, b, _), (c, _, _) in zip(p, p[1:]):
            assert a < b == c
        assert all(n in H.TABLE for (_, _, n) in p)


def test_deterministic():
    for s in SEEDS:
        assert H.idle_plan(s, ("HEAVY",), 1800) == H.idle_plan(s, ("HEAVY",), 1800)
    assert len({tuple(H.idle_plan(s, until=1800)) for s in SEEDS}) > 50  # the seed matters


def test_no_back_to_back_fidget():
    for traits in [(), ("HEAVY",), ("GLITCHED",), ("SMALL",), ("INSOMNIAC",)]:
        for ctx in (None, "water", "fire", "dark", "talker"):
            for s in SEEDS:
                f = fidgets(H.idle_plan(s, traits, 3600, context=ctx))
                assert all(a != b for a, b in zip(f, f[1:])), (traits, ctx, s, f)
                # and every fidget is separated from the next by the base loop
                p = H.idle_plan(s, traits, 3600, context=ctx)
                for (_, _, a), (_, _, b) in zip(p, p[1:]):
                    assert not (a in POOL and b in POOL), (a, b)


def test_long_idle_after_10s():
    for s in SEEDS:
        p = H.idle_plan(s, until=1800)
        longs = [(a, n) for (a, _, n) in p if n.startswith("long_")]
        assert longs and longs[0][0] >= H.LONG_AFTER
        assert all(a < H.LONG_AFTER for (a, _, n) in p if n in POOL)


def test_traits_change_the_pool():
    def count(traits, ctx=None):
        c = Counter()
        for s in SEEDS:
            c.update(n for (_, _, n) in H.idle_plan(s, traits, 3600, context=ctx))
        return c

    base, ins, heavy, gl, small = count(()), count(("INSOMNIAC",)), count(("HEAVY",)), count(("GLITCHED",)), \
        count(("SMALL",))
    assert ins["long_doze"] == 0 and ins["long_lie"] == 0 and ins["fidget_yawn"] == 0
    assert ins["fidget_twitch"] > 0 and ins["long_sit_twitch"] > 0 and base["long_sit_twitch"] == 0
    assert base["fidget_twitch"] == 0 and base["fidget_bounce"] == 0
    assert base["long_doze"] > 0
    assert heavy["fidget_shift"] > 2 * base["fidget_shift"]
    assert gl["fidget_glitch"] > 4 * max(1, base["fidget_glitch"])
    assert small["fidget_bounce"] > 0
    cold = count((), "water")
    assert cold["ctx_shiver"] > 0 and base["ctx_shiver"] == 0


def test_context():
    for s in SEEDS:
        lo = H.idle_plan(s, until=1800, hp_low=True)
        assert all(n not in ("long_lie", "long_doze", "idle") for (_, _, n) in lo)
        assert lo[0][2] == "ctx_lowhp"
        tk = H.idle_plan(s, until=1800, context="talker")
        assert tk[0][2] == "ctx_wave"
        fi = H.idle_plan(s, until=1800, context="fight")
        assert [n for (_, _, n) in fi[:2]] == ["ctx_ready", "ctx_cool"]


def test_input_cancels_instantly():
    for s in SEEDS:
        full = H.idle_plan(s, ("HEAVY",), 1800)
        for t in (1, 37, 300, 599, 600, 1001):
            cut = H.idle_plan(s, ("HEAVY",), 1800, inputs=(t, t + 50))
            assert cut[-1][1] == t  # the timeline ends on the input tick, mid-state if need be
            assert all(b <= t for (_, b, _) in cut)
            # everything before the input is exactly the uncancelled plan
            pre = [(a, min(b, t), n) for (a, b, n) in full if a < t]
            assert cut == pre, (s, t)


if __name__ == "__main__":
    n = 0
    for k, f in list(globals().items()):
        if k.startswith("test_") and callable(f):
            f()
            n += 1
            print("ok", k)
    print(f"PASS {n} tests")
