"""Read back the shared native 444-item body scaler and Crucible contour renderer."""
import argparse,hashlib,json,sys
from pathlib import Path
from PIL import Image
from play import Game
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import bake_catalogue_art as art

def enlarged(m):
 data=bytearray()
 for ty in (0,16):
  for tx in range(0,32,8):
   for y in range(ty,ty+16):
    row=[m[y//2][x//2] for x in range(tx,tx+8)]
    data.extend((sum((v&1)<<(7-x) for x,v in enumerate(row)),sum(((v>>1)&1)<<(7-x) for x,v in enumerate(row))))
 return bytes(data)
def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);g=Game(rom);r={};fail=[];requests=0;maximum=0;frames=[];timings={}
 for _ in range(360):
  g.tick()
  if g.peek('fusion_done')==1:break
 assert g.peek('fusion_done')==1
 def request(mode,id,a=1,b=2,step=0,pose=0):
  nonlocal requests,maximum
  g.poke16('fusion_a',a);g.poke16('fusion_b',b);g.poke16('fusion_result',id);g.poke('fusion_step',step);g.poke('fusion_pose',pose);g.poke('fusion_mode',mode);g.poke('fusion_done',0)
  token=requests%254+1;g.poke('fusion_request',token)
  for n in range(300):
   g.tick()
   if g.peek('fusion_done')==token:break
  assert g.peek('fusion_done')==token,(mode,id,a,b,step)
  requests+=1;maximum=max(maximum,n+1);key='preview' if mode==0 else 'decode_and_first_pose' if mode==1 else 'pose';timings[key]=max(timings.get(key,0),n+1);at=0x8000+g.peek('fusion_buffer')*16
  return bytes(g.pb.memory[1,at:at+512])
 # Every body and idle, including 255/256, bombs, tools and critters.
 for id in range(1,445):
  for pose in range(4):
   data=request(0,id,pose=pose);expected=enlarged(art.pixels(id,pose))
   if data[:256]!=expected:fail.append(['body',id,pose])
 r['all_1776_scaled_bodies_match_codec']=not fail
 # Representative graph edges cover every family and all categories. The
 # game core exhausts all 5283 pair identities separately.
 # Keep this gate independent of the JSON report's presentation format.
 resolved=json.loads((ROOT/'build/catalogue/resolved.json').read_text())
 items=resolved.get('items',resolved.get('discoveries',[]))
 pairs=[]
 for item in items:
  id=item.get('id');ing=item.get('ingredients',item.get('recipe'))
  if id and ing and (id<=24 or id>=385 or id in (25,39,40,255,256,384)):
   if isinstance(ing,dict):ing=[ing['a'],ing['b']]
   pairs.append((int(ing[0]),int(ing[1]),int(id)))
 if not pairs:raise AssertionError('No canonical graph edges found')
 count=0
 for a,b,id in pairs:
  poses=[]
  for step in range(17):
   data=request(1 if step==0 else 2,id,a,b,step);poses.append(data)
   if step==0 and (data[:256]!=enlarged(art.pixels(a,0)) or data[256:]!=enlarged(art.pixels(b,0))):fail.append(['source',a,b,id])
   if step==16:
    target=art.pixels(id,0);left=[[v if x<8 else 0 for x,v in enumerate(row)] for row in target];right=[[v if x>=8 else 0 for x,v in enumerate(row)] for row in target]
    if data[:256]!=enlarged(left) or data[256:]!=enlarged(right):fail.append(['result',id])
   if count<3 or id>=433:
    g.tick(2);frames.append(g.pb.screen.image.convert('RGB').resize((320,288),Image.Resampling.NEAREST))
  if len(set(poses))<14:fail.append(['distinct_poses',a,b,id,len(set(poses))])
  count+=1
 r['shared_fusion_preserves_sources_and_exact_result']=not fail;r['all_native_operations_bounded']=maximum<=12
 if frames:frames[0].save(out/'shared-fusion-native.gif',save_all=True,append_images=frames[1:],duration=67,loop=0)
 g.shot(out/'final-native.png');g.stop();report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'requests':requests,'recipes':count,'maxDisplayFramesPerOperation':maximum,'maximumByOperation':timings,'failures':fail[:30]};(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
