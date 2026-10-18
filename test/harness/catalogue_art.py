"""Read back every native discovery/pet frame and its targeted CGB colours."""
import argparse
import hashlib
import json
import sys
from pathlib import Path
from PIL import Image
from play import Game
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import bake_catalogue_art as art
import catalogue_colours as colours


def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);g=Game(rom)
 for warmup in range(360):
  g.tick()
  if g.peek('art_test_done')==1:break
 else:raise AssertionError('Native art cartridge did not finish cold boot')
 ramps=colours.ramps();seq=0;checks=0;max_wait=0;pet_frames={}
 def request(identity,direction,state,pose,seed=1234):
  nonlocal seq,checks,max_wait
  seq=(seq%254)+1;g.poke('art_test_done',0);g.poke16('run_seed',seed);g.poke16('art_test_id',identity)
  palette=4 if identity>=433 else 0;g.poke('art_test_palette',palette)
  for key,value in [('direction',direction),('state',state),('pose',pose),('request',seq)]:g.poke('art_test_'+key,value)
  for wait in range(1,5):
   g.tick()
   if g.peek('art_test_done')==seq:break
  else:raise AssertionError(('Frame upload did not complete',identity,direction,state,pose))
  max_wait=max(max_wait,wait);tile=4 if seq&1 else 0
  v=min(direction,8-direction)
  expected=art.packed(art.pet(identity-433,state,pose,v) if identity>=433 else art.pixels(identity,pose))
  assert bytes(g.pb.memory[0,0x8000+tile*16:0x8000+tile*16+64])==expected,(identity,direction,state,pose,'VRAM mismatch')
  ramp=ramps[art.family_form(identity)[0]-1][colours.variant(seed,identity)]
  observed=[]
  for index in range(8):
   g.pb.memory[0xff6a]=palette*8+index;observed.append(g.pb.memory[0xff6b])
  assert bytes(observed)==b''.join(c.to_bytes(2,'little') for c in ramp),(identity,seed,'Palette mismatch')
  if palette==4:assert bytes(g.pb.memory[g.addr('pal_sp')+32:g.addr('pal_sp')+40])==bytes(observed),'Palette commit cache mismatch'
  assert g.pb.memory[0xfe03] & 0x20 == (0x20 if direction>4 else 0)
  checks+=1;g.tick();return g.pb.screen.image.convert('RGB').crop((64,56,80,72))
 for i in range(1,433):
  for pose in range(4):request(i,4,0,pose)
 for k in range(12):
  frames=[]
  for pose in range(4):
   sheet=Image.new('RGB',(128,128),(25,19,40))
   for state in range(8):
    for direction in range(8):sheet.paste(request(433+k,direction,state,pose),(direction*16,state*16))
   frames.append(sheet)
  frames[0].save(out/f'pet-{k+1:02}-native.png')
  frames[0].save(out/f'pet-{k+1:02}-native.gif',save_all=True,append_images=frames[1:],duration=160,loop=0)
  pet_frames[k]=frames
 for seed in (0,1,65535):
  for i in (1,2,5,19,24,255,256,384,385,409,433,444):request(i,4,0,0,seed)
 g.stop()
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'warmupFrames':warmup+1,'requests':checks,'maxFramesToUpload':max_wait,'results':{'all_discovery_poses_match_codec':True,'all_pet_views_and_states_match_codec':True,'west_views_use_correct_mirroring':True,'seeded_palette_matches_every_pose':True},'scope':'Isolated native art cartridge. Gameplay, bench integration and physical hardware remain separate gates.'}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return True

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();run(a.rom,a.out)
