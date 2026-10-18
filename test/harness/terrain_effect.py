"""Observe offscreen preparation and visible fall trajectories during ordinary opening fights.
No board, camera, actor-health or animation-state writes. God mode only protects
health so the same route can be observed under live enemy load.
"""
import argparse,hashlib,json
from collections import deque
from pathlib import Path
from play import Game,start_run,step
from terrain_rim import TerrainRim

def stable_route(g):
 # A fixed rectangle can repeatedly run into a generated abyss. Follow real
 # stable floor when that route never produces actual camera travel.
 cells=g.room();x,y=g.player();start=y//16*16+x//16
 q=deque([start]);prev={start:None};directions={};last=start
 while q:
  c=q.popleft();last=c
  for d,delta in [('R',1),('D',16),('L',-1),('U',-16)]:
   n=c+delta
   if not 0<=n<224 or n in prev or abs(c%16-n%16)+abs(c//16-n//16)!=1:continue
   if cells[n]&31 not in (0,1,5,6,10,12,17,19) or cells[n]&128:continue
   prev[n]=c;directions[n]=d;q.append(n)
 path=[]
 while last!=start:path.append(directions[last]);last=prev[last]
 return path[::-1]

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
     if kind==1 and dy!=0:arrivals.append([previous[3],dy])
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
  tick0=g.tick
  def tick(n=1):
   for _ in range(n):
    tick0()
    if seed==0x5105 and g.frames%2==0:frames.append(g.pb.screen.image.convert('RGB'))
  g.tick=tick
  for buttons,n in [({'right'},50),({'up'},40),({'left'},50),({'down'},45),({'up','right'},30),({'down','left'},25),(set(),30)]:
   g.hold(buttons)
   for _ in range(n):
    g.tick()
  safe_steps=0;log=[]
  for _ in range(6):
   if len(departures)>=3:break
   g.hold(set())
   for __ in range(96):
    if g.peek('pl_state')==0 and not g.peek('pl_air') and g.peek('pin',2)==0:break
    g.tick()
   g.tick(16);view=tuple(g.peek('world_view',k) for k in range(2))
   for d in stable_route(g):
    if not step(g,d,log):break
    safe_steps+=1
    if tuple(g.peek('world_view',k) for k in range(2))!=view:break
   g.hold(set());g.tick(30)
  row=rim.report();row.update(seed=seed,updates=updates,arrival_steps=len(arrivals),departure_steps=len(departures),additional_safe_steps=safe_steps,driver_log=log,wrong_motion_kinds=wrong[:5],art_mismatches=art_bad[:5],late_updates=late[:5]);rows.append(row)
  results[f'{seed:04x}_visible_departure_without_incoming_flights']=not arrivals and len(departures)>=3
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
