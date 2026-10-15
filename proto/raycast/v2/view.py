"""Render host2 framebuffers to PNG with the biome palettes: view.py HOSTBIN SNAPDIR OUTPNG [W H pal]"""
import subprocess, sys, tempfile
from pathlib import Path
from PIL import Image
sys.path.insert(0, str(Path(__file__).parent))
import art as A
host, snapdir, outp = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
W = int(sys.argv[4]) if len(sys.argv) > 4 else 96
H = int(sys.argv[5]) if len(sys.argv) > 5 else 64
pal = A.biome_palettes(sys.argv[6] if len(sys.argv) > 6 else "office")
pal = [[A.q555(c) for c in row] for row in pal]
n = W // 4
ims = []
for sp in sorted(snapdir.glob("snap_*.bin"), key=lambda p: int(p.stem.split("_")[1])):
    with tempfile.TemporaryDirectory() as td:
        o = Path(td) / "o.bin"
        subprocess.run([host, str(sp), str(o)], check=True)
        d = o.read_bytes()
    fb = d[n * 5:]
    im = Image.new("RGB", (W, H))
    for tx in range(W // 8):
        for y in range(H):
            lo, hi = fb[tx * H * 2 + y * 2], fb[tx * H * 2 + y * 2 + 1]
            for x in range(8):
                c = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
                im.putpixel((tx * 8 + x, y), pal[y * 8 // H][c])
    ims.append(im.resize((W * 4, H * 4), Image.NEAREST))
sheet = Image.new("RGB", (W * 4 * 2 + 8, (H * 4 + 8) * ((len(ims) + 1) // 2)), (20, 14, 46))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 2) * (W * 4 + 8), (i // 2) * (H * 4 + 8)))
sheet.save(outp)
