"""Observe actual spawn assembly, then ready floor during controller travel."""
import argparse,hashlib,json,re
from pathlib import Path
from play import Game,start_run
from gameplay_rebuild import walk

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);g=Game(rom)
 symbols=Path('build/obj/look.sym').read_text()
 offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)',symbols,re.M)}
 base=g.addr('look_present')-offsets['_look_present'];frames=[];spawn=[];active=[]
 def value(name,k):
  a=base+offsets['_'+name]+k
  return g.pb.memory[1,a] if a>=0xd000 else g.pb.memory[a]
 def completed(_):
  mode=g.peek('game_mode')
  if g.peek('floor_no')!=1 or mode not in (2,3):return
  for k in range(8):
   kind=value('c_kind',k)
   if not kind or value('c_brk',k):continue
   if mode==3:spawn.append([g.frames,k,kind,value('c_t',k)])
   else:active.append([g.frames,k,kind])
 at=g.sym['_frame_wait'];g.pb.hook_register(at>>16,at&65535,completed,None)
 tick=g.tick
 def capture(n=1):
  for _ in range(n):
   tick()
   if g.peek('game_mode')==3 and g.peek('floor_no')==1 and len(frames)<300:frames.append(g.pb.screen.image.convert('RGB'))
 g.tick=capture;assert start_run(g,True,True,physical_fixture=1)
 log=[];board=bytes(g.room()[:224]);travel=[]
 for c in (194,205,194):
  assert walk(g,c,log),log
  g.hold(set());g.tick(40);travel.append({'cell':c,'player':g.player(),'camera':[g.peek('cam_x'),g.peek('cam_y')]})
 results={'spawn_has_real_animated_frames':len({r[0] for r in spawn})>10 and len({r[3] for r in spawn})>4,
          'camera_travel_has_no_assembly_or_departure':not active,'travel_preserves_materials':board==bytes(g.room()[:224]),
          'real_inputs_reach_left_and_right_floor':not log and travel[1]['camera'][0]>80 and travel[2]['camera'][0]<8}
 g.shot(out/'returned.png');g.stop()
 if frames:frames[0].save(out/'spawn-native.gif',save_all=True,append_images=frames[1:],duration=[20 if k%3!=2 else 10 for k in range(len(frames))],loop=0)
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'spawnSamples':len(spawn),'spawnFrames':len(frames),'activeMotion':active[:10],'travel':travel,'log':log}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
