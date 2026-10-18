"""Two medicine bodies and two 8x16 fuse flasks share twelve resident tiles."""
from pathlib import Path
import json
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
import bake_catalogue_art as art
a=json.loads((ROOT/'assets/alchemy/pickups/health.json').read_text())
src=[[0 if c=='.' else int(c) for c in row] for row in a['pixels']]
assert len(src)==16 and all(len(row)==16 for row in src)
frames=[src,[[3 if v==2 and (x+y)%5==0 else v for x,v in enumerate(row)] for y,row in enumerate(src)]]
bomb=[]
for phase in range(2):
 m=[[0]*8 for _ in range(16)]
 for y in range(16):
  for x in range(8):
   if (2<=x<=5 and 5<=y<=6) or ((x-3.5)**2+(y-10)**2<12):m[y][x]=1 if x in (0,7) or y in (5,13) or (x-3.5)**2+(y-10)**2>8 else 3 if x<3 else 2
 m[2+phase][5]=3;m[3][4]=1;m[4][4]=2
 bomb.append(m)
def packed8(m):
 return bytes(v for row in m for v in (sum((p&1)<<(7-x) for x,p in enumerate(row)),sum(((p>>1)&1)<<(7-x) for x,p in enumerate(row))))
data=b''.join(art.packed(m) for m in frames)+b''.join(packed8(m) for m in bomb)
assert len(data)==192
text='#pragma bank 255\n#include <gb/gb.h>\n#include "../health.h"\nstatic const uint8_t cells[192]={'+','.join(map(str,data))+'};\nvoid health_art_load(void) BANKED {set_sprite_data(ST_HEALTH,12u,cells); }\n'
(ROOT/'src/gen/health_art.c').write_text(text)
out=ROOT/'build/catalogue/art';out.mkdir(parents=True,exist_ok=True)
im=Image.new('RGB',(32,16),(25,19,40));pal=[(25,19,40),(56,37,68),(139,197,150),(255,241,223)]
for k,m in enumerate(frames):
 p=Image.new('RGB',(16,16));p.putdata([pal[v] for row in m for v in row]);im.paste(p,(k*16,0))
im.save(out/'medicine-source.png')
