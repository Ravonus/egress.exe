import sys
from PIL import Image
import art as A
def img(t, pal, k=6):
    im = Image.new("RGB", (16, 16))
    for y in range(16):
        for x in range(16):
            im.putpixel((x, y), pal[t[y][x]])
    return im.resize((16*k, 16*k), Image.NEAREST)
off = [A.office_base(p) for p in range(3)] + [A.office_decor(k, i % 3) for i, k in enumerate(["outlet","poster","vent","exit","crt","stain","water","tear","clock"])] + [A.office_door(), A.office_door(True), A.office_burn(0), A.office_burn(1), A.office_burn(2), A.office_char()]
ven = [A.vents_base(p) for p in range(3)] + [A.vents_decor(k, i % 3) for i, k in enumerate(["grate","fan","pipes","hazard","sign","steam","ladder"])] + [A.vents_hatch()]
fl = [A.office_floor(), A.office_ceiling(), A.office_floor(False), A.office_ceiling(False), A.vents_floor(), A.vents_ceiling()]
po = A.biome_palettes("office"); pv = A.biome_palettes("vents")
rows = [(off, po[3]), (ven, pv[3]), (fl[:4], po[6]), (fl[4:], pv[6])]
W = max(len(r[0]) for r in rows)
sheet = Image.new("RGB", (W*100, len(rows)*100), (0,0,0))
for j,(ts,pal) in enumerate(rows):
    for i,t in enumerate(ts):
        sheet.paste(img(t, pal), (i*100, j*100))
sheet.save("out/atlas.png")
