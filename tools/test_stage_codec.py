"""Reject unusable authored contracts before they become cartridge data."""
import copy,json
from pathlib import Path
from bake_stage_codec import compile_codec
data=json.loads((Path(__file__).resolve().parents[1]/'assets/stages/codec.json').read_text())
mutations=[
    lambda d:d['stages'][0].update(goals=[]),
    lambda d:d['stages'][0].update(goals=['teleport_everything']),
    lambda d:d['stages'][0].update(goals=['load','load']),
    lambda d:d['stages'][0].update(conditions=[]),
    lambda d:d['stages'][4].update(required=['portal']),
    lambda d:d['stages'][4].update(sizes=['xs']),
    lambda d:d['stages'][0].update(alternatives=['jump']),
    lambda d:d['sizes'][5].update(width=[100,132]),
    lambda d:d['sizes'][0].update(height=[13,14]),
    lambda d:d['sizes'][0].update(goals=[0,4]),
    lambda d:d['biomes'][0].update(water_style=4),
    lambda d:d['biomes'][0].update(foliage=300),
]
for mutate in mutations:
    bad=copy.deepcopy(data);mutate(bad)
    try:compile_codec(bad)
    except AssertionError:pass
    else:raise AssertionError('invalid authored codec was accepted')
print(f'stage codec: {len(mutations)} invalid contracts rejected')
