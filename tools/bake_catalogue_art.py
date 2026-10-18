"""Native 16x16 discovery cells, four idles and authored pet action states.

The same 64-byte item serves inventory, companion drawing and the shared
fusion renderer. No per-recipe animation payload is emitted.
"""
from pathlib import Path
import json,math,argparse,hashlib
from PIL import Image,ImageDraw
import catalogue_colours as colours
import element_pixels
ROOT=Path(__file__).resolve().parents[1]
C=json.loads((ROOT/'assets/alchemy/catalogue.json').read_text())
PETS=[json.loads(p.read_text()) for p in sorted((ROOT/'assets/alchemy/pets').glob('*.json')) if not p.name.startswith('.')]

def blank():return [[0]*16 for _ in range(16)]
def outline(mask,seed=0):
 out=blank()
 for y in range(16):
  for x in range(16):
   if not mask[y][x]:continue
   edge=x==0 or y==0 or x==15 or y==15 or any(not mask[yy][xx] for xx,yy in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)] if 0<=xx<16 and 0<=yy<16)
   out[y][x]=1 if edge else 3 if (x+y+seed)%9==0 or (x<7 and y<7 and (x+y)&1==0) else 2
 return out

def source(f):
 return element_pixels.pixels(C['families'][f-1][0])

def form_mask(n,x,y):
 dx=x-7.5;dy=y-7.5;r=dx*dx+dy*dy
 if n==1:return abs(dx)+abs(dy*.85)<6.4
 if n==2:return r<43
 if n==3:return any((x-a)**2+(y-b)**2<10 for a,b in [(5,5),(11,6),(7,12)])
 if n==4:return 20<r<49 or abs(dx)<2
 if n==5:return dx*dx*.8+(dy-1)**2*1.4<35
 if n==6:return abs(dx+math.sin(y*.65)*2)<(8-y)*.3+3 and 1<=y<=14
 if n==7:return abs(dx)<6 and abs(dy)<5 and abs(dx)+abs(dy)<8
 if n==8:return y>5 and dx*dx+(dy-2)**2*2<48
 if n==9:return r<15 or any((x-a)**2+(y-b)**2<8 for a,b in [(7,3),(3,7),(12,7),(7,12)])
 if n==10:return abs(dx)*.8+abs(dy)<7
 if n==11:return 23<r<45 or (abs(dx)<1.4 and abs(dy)<4) or (abs(dy)<1.4 and abs(dx)<4)
 if n==12:return r<38 and not(abs(dx)<2 and abs(dy)>4)
 if n==13:return abs(dx)<2 or abs(dy)<2 or abs(abs(dx)-abs(dy))<1.3 and r<54
 if n==14:return 3<=x<=12 and 6<=y<=13 or 2<=y<=7 and any(abs(x-a)<=1+(y-2)//3 for a in (3,7,12))
 return r<13 or any(abs(math.sin(math.atan2(dy,dx)*5))*5+2>math.sqrt(r) for _ in [0])

def material(f,n):
 src=source(f)
 if not n:return src
 # The substance remains the foreground silhouette. A small inset form mark
 # identifies the discovery without replacing mud/fire/water with a blob.
 out=[row[:] for row in src]
 mask=[[form_mask(n, x*4+1, y*4+1) for x in range(4)] for y in range(4)]
 for y in range(4):
  for x in range(4):
   out[11+y][11+x]=3 if mask[y][x] else 1
 for x in range(4):out[14][11+x]=3 if n&(1<<x) else 1
 # Facets change the interior, keeping every exterior edge and its light.
 for y in range(3,11):
  for x in range(3,13):
   if src[y][x]==2 and (x*3+y+n*5)%17==0:out[y][x]=3
 return out

def shifted(src,dx=0,dy=0):
 out=blank()
 for y,row in enumerate(src):
  for x,v in enumerate(row):
   if v and 0<=x+dx<16 and 0<=y+dy<16:out[y+dy][x+dx]=v
 return out

def pet(k,state=0,pose=0,view=4):
 a=PETS[k];key=['back','back_diagonal','side','front_diagonal',None][view]
 rows=a['views'][key] if key else a['pixels']
 src=[[0 if c=='.' else int(c) for c in row] for row in rows]
 assert len(src)==16 and all(len(r)==16 for r in src)
 out=[r[:] for r in src];gait=a['gait'];phase=pose&3
 if state in (3,7):
  # Settled, grounded silhouette; slow breathing never re-opens its eyes.
  out=blank()
  bottom=8 if state==7 else 4
  for y in range(bottom,16):
   sy=min(15,(y-bottom)*16//(16-bottom))
   for x in range(16):out[y][x]=src[sy][x]
  if phase in (1,2):
   for y in range(5,10):
    for x in range(3,13):
     if out[y][x]==2 and (x+y)%7==0:out[y][x]=3
  if state==7:
   if phase==2:
    out=blank()
    for x,y in [(3,13),(6,11),(10,12),(13,14)]:out[y][x]=3
   elif phase==3:out=blank()
 elif state==4:
  out=shifted(src,dx=[-1,1,-1,0][phase],dy=[0,1,0,0][phase])
  if phase==1:out=[[3 if v==2 else v for v in r] for r in out]
 elif state==5:
  out=blank()
  for y in range(2,16):out[y]=src[min(15,(y-2)*16//14)][:]
  # Braced feet and an outward shield contour; no extra sprite allocation.
  for x,y in [(1,8),(1,9),(2,6),(2,11),(3,5),(3,12)]:out[y][x]=3 if phase&1 else 2
 elif state==6:
  out=shifted(src,dy=-1 if phase in (1,2) else 0)
  for x,y in [[(3,12),(12,12)],[(2,9),(13,9)],[(3,5),(12,5)],[(5,2),(10,2)]][phase]:out[y][x]=3
 elif state==1 and gait=='walk':
  # Keep the torso intact; alternating limb contacts/lifts are independent.
  out=shifted(src,dy=-1 if phase in (1,3) else 0)
  for y in range(13,16):out[y]=[0]*16
  for y in range(13,16):
   for x,v in enumerate(src[y]):
    if not v:continue
    near=x>=8;dx=([-1,0,1,0] if near else [1,0,-1,0])[phase]
    dy=([0,-1,0,0] if near else [0,0,0,-1])[phase]
    xx=x+dx;yy=y+dy
    if 0<=xx<16 and 0<=yy<16:out[yy][xx]=v
 elif state==1 and gait=='fly':
  out=blank()
  # Wings pivot at the shoulder instead of moving as a rigid whole sprite.
  for y,row in enumerate(src):
   for x,v in enumerate(row):
    if not v:continue
    wing=(x<5 or x>10) and y>=6
    dy=([-2,-1,1,0][phase] if wing else [0,-1,0,1][phase])
    xx=x+(1 if x<5 else -1) if wing and phase==1 else x
    yy=y+dy
    if 0<=xx<16 and 0<=yy<16:out[yy][xx]=v
 elif state==1 and gait=='hop':
  if phase==0:
   out=blank()
   for y in range(3,16):out[y]=src[min(15,(y-3)*16//13)][:]
  else:out=shifted(src,dy=[0,-2,-3,-1][phase])
 elif state==1 and gait=='crawl':
  out=blank()
  for y,row in enumerate(src):
   for x,v in enumerate(row):
    if not v:continue
    yy=y+(-1 if (x//3+phase)%4==0 else 0)
    xx=x+(-1 if y>12 and (x//3+phase)%4==2 else 0)
    if 0<=xx<16 and 0<=yy<16:out[yy][xx]=v
 elif state==1:out=shifted(src,dy=[0,-1,-2,-1][phase])
 elif state==2:
  # Wind-up, release, pulse, recovery. The body visibly leans into its cast.
  out=shifted(src,dx=[-1,0,1,0][phase] if view==2 else 0,dy=[1,-1,0,0][phase])
  for x,y in [[(4,3)],[(2,2),(13,3)],[(1,5),(14,6),(2,11),(13,12)],[(3,5),(12,6)]][phase]:out[y][x]=3
 elif phase==2:
  # Breathing changes a shoulder edge, keeping feet planted.
  for y in range(8,11):
   xs=[x for x,v in enumerate(out[y]) if v]
   if xs and min(xs)>0:out[y][min(xs)-1]=1
 if state in (0,3,6,7) and (phase==3 or state in (3,7)) and view in (2,3,4):
  # Dark pixels enclosed by body colour are eyes; collapse into short lids.
  for y in range(3,10):
   xs=[x for x in range(2,14) if out[y][x]==1 and out[y][x-1]>1 and out[y][x+1]>1]
   if xs:
    for x in xs:out[y][x+1]=1
    break
 return out

def special(f,kind):
 # Bomb flask / tool silhouette, carrying the material in its chamber.
 mask=[[((x-7.5)**2+(y-9)**2<29 or (5<=x<=9 and 3<=y<=5) or (8<=x<=10 and y==2)) if kind==1 else ((5<=x<=10 and 5<=y<=13) or (6<=x<=9 and 2<=y<=5) or (3<=x<=12 and y==8)) for x in range(16)] for y in range(16)]
 out=outline(mask,f);src=source(f)
 for y in range(6,12):
  for x in range(5,11):
   if out[y][x]>1:out[y][x]=src[y][x] or 2
 for k in range(5):
  if out[11][5+k]>1:out[11][5+k]=3 if f&(1<<k) else 2
 return out

def family_form(i):
 if i<=24:return i,0,0
 if i<385:return (i-25)//15+1,(i-25)%15+1,0
 if i<409:return i-384,0,1
 if i<433:return i-408,0,2
 return C['pets'][i-433][0],0,3

def pixels(i,pose=0):
 f,n,kind=family_form(i)
 if kind==3:return pet(i-433,0,pose)
 out=material(f,n) if kind==0 else special(f,kind)
 if pose==1:
  out=[row[:] for row in out]
  for y in range(2,14):
   for x in range(2,14):
    if out[y][x]==2 and (x+y+f)%7==0:out[y][x]=3
 elif pose==2:out=out[1:]+[[0]*16]
 elif pose==3:
  out=[[3 if v==2 and (x*3+y+f)%11==0 else v for x,v in enumerate(row)] for y,row in enumerate(out)]
 return out

def packed(m):
 # Two 8x16 OAM columns: TL/BL, TR/BR. BG cards map 0,2/1,3.
 data=bytearray()
 for tx in (0,8):
  for y in range(16):
   row=m[y][tx:tx+8];data.extend((sum((v&1)<<(7-x) for x,v in enumerate(row)),sum(((v>>1)&1)<<(7-x) for x,v in enumerate(row))))
 return bytes(data)

def main():
 p=argparse.ArgumentParser();p.add_argument('--preview-only',action='store_true');a=p.parse_args();out=ROOT/'build/catalogue/art';out.mkdir(parents=True,exist_ok=True)
 ramps=colours.ramps()
 paper=(25,19,40);white=(255,239,213)
 # Source review only; released website illustrations come from native ROM
 # captures, never from this sheet.
 sheet=Image.new('RGB',(384,192),paper);d=ImageDraw.Draw(sheet)
 for f in range(1,25):
  x0=((f-1)%6)*64;y0=((f-1)//6)*48
  ramp=[colours.unpack(c) for c in ramps[f-1][0]];ramp[0]=paper
  im=Image.new('RGB',(16,16));im.putdata([ramp[v] for row in source(f) for v in row])
  sheet.paste(im.resize((32,32),Image.Resampling.NEAREST),(x0+16,y0))
  d.text((x0+2,y0+34),C['families'][f-1][0],fill=white)
 sheet.save(out/'material-source.png')
 sheet=Image.new('RGB',(480,320),paper);d=ImageDraw.Draw(sheet)
 for k in range(12):
  ramp=[colours.unpack(c) for c in ramps[C['pets'][k][0]-1][colours.variant(1234,433+k)]];ramp[0]=paper
  for state in range(4):
   im=Image.new('RGB',(16,16));im.putdata([ramp[v] for row in pet(k,state,2) for v in row]);sheet.paste(im.resize((32,32),Image.Resampling.NEAREST),((k%6)*80+state%2*36,(k//6)*160+state//2*48));
  d.text(((k%6)*80,(k//6)*160+104),C['pets'][k][1].replace(' ','\n'),fill=white)
 sheet.save(out/'pet-states-source.png')
 # Every stored view and every state animates together; no palette flicker.
 frames=[]
 for phase in range(4):
  sheet=Image.new('RGB',(720,1128),paper);d=ImageDraw.Draw(sheet)
  for k in range(12):
   x0=(k%4)*180;y0=(k//4)*376
   ramp=[colours.unpack(c) for c in ramps[C['pets'][k][0]-1][colours.variant(1234,433+k)]];ramp[0]=paper
   d.text((x0,y0),C['pets'][k][1],fill=white)
   for view in range(5):
    for state in range(8):
     im=Image.new('RGB',(16,16));im.putdata([ramp[v] for row in pet(k,state,phase,view) for v in row]);sheet.paste(im.resize((32,32),Image.Resampling.NEAREST),(x0+view*34,y0+20+state*42))
  frames.append(sheet)
 frames[0].save(out/'pet-directional-source.png')
 frames[0].save(out/'pet-directional-source.gif',save_all=True,append_images=frames[1:],duration=160,loop=0)
 sheet=Image.new('RGB',(640,384),paper);d=ImageDraw.Draw(sheet)
 for f in range(24):
  x0=(f%4)*160;y0=(f//4)*64;d.text((x0,y0),C['families'][f][0],fill=white)
  for n in range(8):
   for j,c in enumerate(ramps[f][n*8][1:]):d.rectangle((x0+n*19,y0+16+j*12,x0+n*19+17,y0+26+j*12),fill=colours.unpack(c))
 sheet.save(out/'targeted-palettes.png')
 if a.preview_only:return
 gen=ROOT/'src/gen';all_data=[]
 for i in range(1,445):all_data.append([packed(pixels(i,pose)) for pose in range(4)])
 # Thirty bodies leave room for both accessors: two chunks can share a bank.
 for chunk,start in enumerate(range(0,444,30)):
  rows=all_data[start:start+30];text='#pragma bank 255\n#include <gb/gb.h>\n#include <string.h>\nstatic const uint8_t cells['+str(len(rows))+'][4][64]={\n'
  text+=''.join('{'+','.join('{'+','.join(map(str,v))+'}' for v in item)+'},\n' for item in rows)+'};\n'
  text+='void cat_copy_'+str(chunk)+'(uint8_t n,uint8_t pose,uint8_t *out) BANKED {memcpy(out,cells[n][pose&3u],64u);}\n'
  text+='void cat_upload_'+str(chunk)+'(uint8_t n,uint8_t pose,uint8_t tile) BANKED {set_sprite_data(tile,4u,cells[n][pose&3u]);}\n'
  (gen/('catalogue_cells_'+str(chunk)+'.c')).write_text(text)
 for group in range(12):
  rows=[[packed(pet(group,state,pose,view)) for state in range(8) for pose in range(4)] for view in range(5)]
  text='#pragma bank 255\n#include <gb/gb.h>\nstatic const uint8_t cells[5][32][64]={\n'+''.join('{'+','.join('{'+','.join(map(str,v))+'}' for v in view)+'},\n' for view in rows)+'};\n'
  text+='void cat_pet_'+str(group)+'(uint8_t view,uint8_t state,uint8_t pose,uint8_t tile) BANKED {set_sprite_data(tile,4u,cells[view][state*4u+(pose&3u)]);}\n'
  (gen/('catalogue_pet_'+str(group)+'.c')).write_text(text)
 text='#pragma bank 255\n#include <gb/gb.h>\n#include "../catalogue_art.h"\n'
 for chunk in range(15):
  text+='void cat_copy_'+str(chunk)+'(uint8_t,uint8_t,uint8_t*) BANKED;\nvoid cat_upload_'+str(chunk)+'(uint8_t,uint8_t,uint8_t) BANKED;\n'
 for group in range(12):text+='void cat_pet_'+str(group)+'(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;\n'
 text+='void catalogue_art_raw(eg_item_id id,uint8_t pose,uint8_t *out) BANKED {uint8_t n;if(!id || id>444u)id=1u;id--;n=(uint8_t)(id%30u);switch(id/30u){\n'
 for chunk in range(15):text+='case '+str(chunk)+'u:cat_copy_'+str(chunk)+'(n,pose,out);break;\n'
 text+='}}\nvoid catalogue_art_upload(eg_item_id id,uint8_t state,uint8_t pose,uint8_t tile) BANKED {uint8_t n,view;if(!id || id>444u)id=1u;if(id>=433u){n=(uint8_t)(id-433u);view=(state>>3)&7u;if(view>4u)view=8u-view;switch(n){\n'
 for group in range(12):text+='case '+str(group)+'u:cat_pet_'+str(group)+'(view,state&7u,pose,tile);break;\n'
 text+='}return;}id--;n=(uint8_t)(id%30u);switch(id/30u){\n'
 for chunk in range(15):text+='case '+str(chunk)+'u:cat_upload_'+str(chunk)+'(n,pose,tile);break;\n'
 text+='}}\n';(gen/'catalogue_dispatch.c').write_text(text)
 text='#pragma bank 255\n#include <gb/gb.h>\n#include <gb/cgb.h>\n#include <string.h>\n#include "../catalogue_art.h"\nextern uint16_t run_seed,pal_sp[32];\nstatic const uint16_t ramps[24][64][4]={\n'
 text+=''.join('{'+','.join('{'+','.join(map(str,r))+'}' for r in fam)+'},\n' for fam in ramps)+'};\n'
 text+='''static uint8_t variant(eg_item_id id){uint16_t v=run_seed^(id+(id<<8));v^=v>>7;v^=v<<9;v^=v>>5;return (uint8_t)v&63u;}
void catalogue_art_palette(eg_item_id id,uint8_t slot,uint8_t obj) BANKED {
 uint8_t f=eg_catalogue_family(id);const uint16_t *p=ramps[f ? f-1u : 0u][variant(id)];
 if(obj){if(slot==4u)memcpy(pal_sp+16u,p,8u);set_sprite_palette(slot,1u,p);}else set_bkg_palette(slot,1u,p);
}
static const uint8_t groups[24]={0,1,2,3,4,5,1,3,1,1,3,5,4,0,4,4,2,2,4,2,3,4,1,5};
static const uint8_t slots[6]={0,1,4,5,6,7};
uint8_t catalogue_art_bg_palette(eg_item_id id) BANKED {uint8_t f=eg_catalogue_family(id);return slots[groups[f ? f-1u : 0u]];}
void catalogue_art_ui_palettes(void) BANKED {
 static const uint8_t families[6]={1,2,20,4,15,6};uint8_t k;
 for(k=0u;k<6u;k++)set_bkg_palette(slots[k],1u,ramps[families[k]-1u][variant(families[k])]);
}
''';(gen/'catalogue_palettes.c').write_text(text)
 hashes={str(i):hashlib.sha256(packed(pixels(i))).hexdigest() for i in range(1,445)}
 (out/'body-hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
 (out/'budget.json').write_text(json.dumps({'discoveries':444,'idleBytes':444*4*64,'directionalPetBytes':12*5*8*4*64,'storedPetViews':5,'petDirections':8,'petStates':8,'framesPerState':4,'paletteBytes':24*64*8,'perBodyBytes':64,'perPose32pxBytes':512,'perRecipeBakedBytes':0},indent=2)+'\n')
 print((out/'budget.json').read_text())
if __name__=='__main__':main()
