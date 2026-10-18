"""Native asset budget, identity, directional gait and quantised colour gates."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import bake_catalogue_art as art
import catalogue_colours as colour


def run():
    ramps = colour.ramps()
    for family in ramps:
        assert len(set(family)) >= 32, 'Seed variation collapsed after RGB555 quantisation'
        for ramp in family:
            assert len(set(ramp)) == 4 and all(0 <= c < 32768 for c in ramp)
            ink, mid, tip = [colour.luma(colour.unpack(c)) for c in ramp[1:]]
            assert mid >= ink + 69 and tip >= mid + 20
    assert all(colour.variant(s, i) == colour.variant(s, i) for s in (0, 1, 1234, 65535) for i in range(1, 445))
    assert len({colour.variant(s, 433) for s in range(256)}) == 64
    for k in range(12):
        views = [art.packed(art.pet(k, 0, 0, v)) for v in range(5)]
        assert len(set(views)) == 5, (k, 'Missing distinct directional body')
        for v in range(5):
            walk = [art.pet(k, 1, p, v) for p in range(4)]
            assert len({art.packed(m) for m in walk}) >= 3, (k, v, 'Frozen gait')
            # A full cycle returns to its original contact frame.
            assert art.pet(k, 1, 0, v) == art.pet(k, 1, 4, v)
            for state in range(8):
                for p in range(4):
                    m = art.pet(k, state, p, v)
                    assert len(m) == 16 and all(len(r) == 16 for r in m)
                    assert all(0 <= x <= 3 for r in m for x in r)
                    assert len(art.packed(m)) == 64
    # Every identity has a legal four-pose native body; no per-recipe payload.
    for i in range(1, 445):
        for p in range(4):
            assert len(art.packed(art.pixels(i, p))) == 64
    print('444 four-pose bodies; 12 critters, five stored views/eight facings, eight states; 1536 targeted RGB555 ramps.')


if __name__ == '__main__':
    run()
