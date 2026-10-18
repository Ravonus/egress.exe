"""A real hammer heir swings toward eight aimed targets with distinct native art."""
import argparse,hashlib,json,re
from pathlib import Path
from heir import boot_heir
from terrain_boundary import DIRS,arena
from stage2 import teleport
from PIL import Image

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);r={};rows=[];frames=[]
 data=[int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',Path('src/gen/hammer.inc').read_text().split('hammer_diagonal')[0])]
 for direction,(name,dx,dy,buttons) in enumerate(DIRS):
  g=boot_heir(rom,1);g.tick(30);arena(g);teleport(g,120,104);g.poke('pl_inv',0)
  front=(6+dy)*16+7+dx;back=(6-dy)*16+7-dx
  g.poke('room',3,front);g.poke('room',3,back)
  r[name+'_real_heir_is_hammer']=g.peek('heir',7)==1
  actual=bytes(g.pb.memory[0,0x8000+134*16+i] for i in range(384))
  r[name+'_distinct_windup_and_contact_art_loaded']=actual==bytes(data)
  g.hold(buttons);g.tick(2);g.hold(set());g.tick(4);g.press('a',2)
  facing=[];ahead=[]
  for _ in range(32):
   g.tick()
   if g.peek('pin',3)==2 and g.peek('pl_arc'):
    facing.append(g.peek('pmv',5));m=g.pb.memory
    for k in range(4,12):
     if m[0xc000+k*4] and 134<=m[0xc002+k*4]<158:
      # OBJ x/y contain +8/+16 and this is the left half of a 16x16 weapon.
      x=m[0xc001+k*4]+g.peek('cam_x');y=m[0xc000+k*4]-8+g.peek('cam_y')
      ahead.append((x-g.peek('pl_x'))*dx+(y-g.peek('pl_y'))*dy)
   if _%3==0:frames.append(g.pb.screen.image.convert('RGB'))
  r[name+'_front_target_breaks']=g.peek('room',front)&31==0
  r[name+'_rear_target_untouched']=g.peek('room',back)&31==3 and not (g.peek('cracked',back>>3)&(1<<(back&7)))
  r[name+'_impact_faces_aim']=bool(facing) and set(facing)=={direction}
  if not dx*dy:r[name+'_visible_head_is_forward']=bool(ahead) and min(ahead)>0
  rows.append({'direction':name,'facings':facing,'weapon_projection':ahead});g.shot(out/f'{name}.png');g.stop()
 if frames:frames[0].save(out/'hammer-eight-directions.gif',save_all=True,append_images=frames[1:],duration=51,loop=0)
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'directions':rows}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
