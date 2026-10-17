"""Observe actual drop/settle/fall trajectories during ordinary opening fights.
No board, camera, actor-health or animation-state writes. God mode only protects
health so the same route can be observed under live enemy load.
"""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run
from terrain_rim import TerrainRim

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);rows=[];results={};frames=[]
 for seed in (0x5100,0x5104,0x5105,0x5106):
  g=Game(rom);assert start_run(g,True,False,seed=seed)
  rim=TerrainRim(g);seen={};arrivals=[];departures=[];wrong=[];art_bad=[];late=[];updates=0
  def read(name,k):
   at=rim.addr[name]+k;return g.pb.memory[1,at] if at>=0xd000 else g.pb.memory[at]
  def completed(_):
   nonlocal updates
   if g.peek('game_mode')!=2:return
   updates+=1;rim.observe();m=g.pb.memory
   if g.peek('video_epoch')!=g.peek('frame_epoch'):late.append(g.frames)
   for k in range(8):
    kind=read('_c_kind',k)
    if not kind or read('_c_brk',k):seen.pop(k,None);continue
    cell=read('_c_cell',k);t=read('_c_t',k);meta=read('_c_meta',k);a=39-2*k
    oy=m[0xc000+4*a];ox=m[0xc001+4*a]
    if not oy or not ox:continue
    dy=oy-16-((cell//16)*16-g.peek('cam_y'))
    key=(kind,cell,t,dy)
    previous=seen.get(k)
    if previous and previous[0]==kind and previous[1]==cell and previous[2]!=t:
     if kind==1 and previous[3]<dy:arrivals.append([previous[3],dy])
     if kind==7 and previous[3]<dy:departures.append([previous[3],dy])
    seen[k]=key
    if kind not in (1,7):wrong.append(kind)
    pal=g.peek('meta_pal',meta)
    if pal&7 not in (0,2):continue
    # Compare actual OBJ bytes with the same grounded card's BG quadrants.
    # This catches generic slab substitutions and pool slot corruption.
    dst=m[0xc002+4*a];prop=m[0xc003+4*a]
    for j,q in enumerate((0,2,1,3)):
     tile=g.peek('meta_tile',meta*4+q);bank=0 if pal&128 else 1
     addr=(0x9000+tile*16) if tile<128 else (0x8800+(tile-128)*16)
     actual=bytes(m[(prop>>3)&1,0x8000+(dst+j)*16+b] for b in range(16))
     expected=bytes(m[bank,addr+b] for b in range(16))
     if actual!=expected:art_bad.append([g.frames,k,meta,j]);break
  at=g.sym['_frame_wait'];g.pb.hook_register(at>>16,at&65535,completed,None)
  for buttons,n in [({'right'},50),({'up'},40),({'left'},50),({'down'},45),({'up','right'},30),({'down','left'},25),(set(),30)]:
   g.hold(buttons)
   for _ in range(n):
    g.tick()
    if seed==0x5105 and g.frames%2==0:frames.append(g.pb.screen.image.convert('RGB'))
  row=rim.report();row.update(seed=seed,updates=updates,arrival_steps=len(arrivals),departure_steps=len(departures),wrong_motion_kinds=wrong[:5],art_mismatches=art_bad[:5],late_updates=late[:5]);rows.append(row)
  results[f'{seed:04x}_visible_downward_arrival_and_departure']=len(arrivals)>=3 and len(departures)>=3
  results[f'{seed:04x}_consistent_motion']=not wrong
  results[f'{seed:04x}_canonical_ground_art']=not art_bad
  results[f'{seed:04x}_central_view_ready']=not rim.core_missing and not rim.bad
  results[f'{seed:04x}_one_update_per_display']=not late
  g.shot(out/f'{seed:04x}.png');g.stop()
 if frames:frames[0].save(out/'combat-drop-and-fall.gif',save_all=True,append_images=frames[1:],duration=34,loop=0)
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'routes':rows}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
