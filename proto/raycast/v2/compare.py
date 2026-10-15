"""Side-by-side v1 | v2 comparison: same-camera stills (from measure2.py --poses) and the same walk window as a GIF.
compare.py STILLS_V1 STILLS_V2 GIF_V1 GIF_V2 OUT_PNG OUT_GIF"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageSequence

s1, s2, g1, g2, op, og = (Path(x) for x in sys.argv[1:7])
SC = 2  # stills are 4x; show at 2x


def half(im):
    return im.convert("RGB").resize((im.width * SC // 4, im.height * SC // 4), Image.NEAREST)


names = sorted(p.name for p in s1.glob("view_*.png"))
cells = [(half(Image.open(s1 / n)), half(Image.open(s2 / n)), n[5:-4]) for n in names]
w, h = cells[0][0].size
pad, top = 8, 22
sheet = Image.new("RGB", (pad + 2 * (w + pad), top + len(cells) * (h + pad)), (20, 16, 36))
d = ImageDraw.Draw(sheet)
d.text((pad, 6), "v1 (flat bands, 32.8 fps)", fill=(230, 220, 250))
d.text((2 * pad + w, 6), "v2 (textured + dithered, 25.2 fps)", fill=(230, 220, 250))
for i, (a, b, f) in enumerate(cells):
    y = top + i * (h + pad)
    sheet.paste(a, (pad, y))
    sheet.paste(b, (2 * pad + w, y))
    d.text((pad + 4, y + 4), f"frame {f}", fill=(255, 255, 255))
sheet.save(op)

fa = [half(f.copy()) for f in ImageSequence.Iterator(Image.open(g1))]
fb = [half(f.copy()) for f in ImageSequence.Iterator(Image.open(g2))]
n = min(len(fa), len(fb))
dur = Image.open(g1).info.get("duration", 50)
out = []
for a, b in zip(fa[:n], fb[:n]):
    im = Image.new("RGB", (pad + 2 * (a.width + pad), a.height + 2 * pad), (20, 16, 36))
    im.paste(a, (pad, pad))
    im.paste(b, (2 * pad + a.width, pad))
    out.append(im)
out[0].save(og, save_all=True, append_images=out[1:], duration=dur, loop=0, optimize=True)
print(op, og, n, "frames")
