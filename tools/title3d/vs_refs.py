"""docs/shots/b1_title_vs_refs.png: the cartridge's title (b1_title.gif frames) beside the approved references, the
mock (docs/shots/mock_title.png) and the v2 stage stills (docs/shots/raycast_v1_v2_stills.png), all at 2x.

    python tools/title3d/vs_refs.py docs/shots
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

d = Path(sys.argv[1])
S = 2
cell = (160 * S, 144 * S)
mock = Image.open(d / "mock_title.png").convert("RGB").resize(cell, Image.NEAREST)
st = Image.open(d / "raycast_v1_v2_stills.png").convert("RGB")
v2 = [st.crop((336, 22 + 296 * k, 656, 310 + 296 * k)).resize(cell, Image.NEAREST) for k in (0, 4)]
g = Image.open(d / "b1_title.gif")
ours = []
for i in (20, 55, 90):
    g.seek(min(i, g.n_frames - 1))
    ours.append(g.convert("RGB").resize(cell, Image.NEAREST))
cols = [("mock_title (approved)", mock), ("v2 stage still", v2[0]), ("v2 stage still", v2[1])]
cols2 = [("b1 title, cartridge", im) for im in ours]
W, H = cell[0] * 3 + 40, (cell[1] + 18) * 2 + 30
out = Image.new("RGB", (W, H), (18, 14, 34))
dr = ImageDraw.Draw(out)
for r, row in enumerate((cols, cols2)):
    for c, (lab, im) in enumerate(row):
        x, y = 10 + c * (cell[0] + 10), 10 + r * (cell[1] + 28)
        dr.text((x, y), lab, fill=(220, 210, 250))
        out.paste(im, (x, y + 14))
out.save(d / "b1_title_vs_refs.png")
print(d / "b1_title_vs_refs.png")
