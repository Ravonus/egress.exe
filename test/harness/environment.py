"""Real SELECT actions on explicit terrain/inventory fixtures. No reaction
outcome, timer, material edit or camera transfer is injected by the test."""
import argparse,hashlib,json
from pathlib import Path
from PIL import Image
from play import Game,start_run,pocket,bench_open,bench_cursor
from crafted_effects import fresh
from companions import wait
from heir import OamMeter
from grid_observer import GridObserver
from world_play import WorldPlayer

class Watch:
 def __init__(self,g,grid=None):
  self.g=g;self.late=[];self.fx=self.smoke=self.sounds=0;self.paused=True;self.epochs=[]
  self.meter=OamMeter(g);self.grid=grid if grid is not None else GridObserver(g)
  for name,callback in [('frame_wait',self.frame),('fx_burst',self.particle),('puff',self.puff),('sfx',self.sound)]:
   at=g.sym['_'+name];g.pb.hook_register(at>>16,at&65535,callback,None)
 def frame(self,_):
  g=self.g;paused=bool(g.peek('bench_on') or g.peek('pl_state') in (5,6))
  if g.peek('game_mode')==2 and not paused and not self.paused:
   self.epochs.append(g.peek('frame_epoch'))
   if g.peek('video_epoch')!=g.peek('frame_epoch'):self.late.append([g.frames,g.pb.memory[0xff44]])
  self.paused=paused
 def particle(self,_):self.fx+=1
 def puff(self,_):self.smoke+=1
 def sound(self,_):self.sounds+=1

def bag(g,ids):
 for k in range(24):g.poke('pocket',ids[k]&255 if k<len(ids) else 0,k)
 for k in range(3):g.poke('pocket_hi',sum(((id>>8)&1)<<(i&7) for i,id in enumerate(ids) if i>>3==k),k)
 g.poke('pocket_n',len(ids));g.poke('effect_n',128);g.poke('puzzle_dirty',1)

def equip(g,id):
 log=[];assert bench_open(g,log),log;bench_cursor(g,pocket(g).index(id));g.press('select',4);g.tick(14)
 assert not g.peek('bench_on') and g.peek16('tool_piece')==id

def cast(g):
 assert wait(g,lambda:not g.peek('tool_cd') and g.peek('pl_state')==0,160)
 g.press('select',4);g.tick(2)

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);results={};c=7*16+7
 # Keep the original map-write coverage by observing actual spawn writes.
 grids=[];g=fresh(rom,20,observer=lambda g:grids.append(GridObserver(g)))
 for k in range(28):g.poke('room',0,258+k)
 for y in range(4,11):
  for x in range(3,13):g.poke('look_skin',0,y*16+x)
 bag(g,[20,2,2,1,20]);w=Watch(g,grids[0]);frames=[];states=[];base_tick=g.tick
 def capture(n=1):
  for _ in range(n):
   base_tick();state=(g.room()[c],g.peek('look_skin',c))
   if not states or states[-1]!=state:states.append(state)
   if not g.peek('bench_on') and g.frames%3==0:frames.append(g.pb.screen.image.convert('RGB').resize((320,288),Image.Resampling.NEAREST))
 g.tick=capture
 for id,skin,raw,name in [(20,32,0,'seed'),(2,36,0,'sprout'),(2,34,15,'tree'),(1,39,0,'ash'),(20,32,0,'replant')]:
  if g.peek16('tool_piece')!=id:equip(g,id)
  before=len(pocket(g));g.poke('stamina',100);g.poke('pmv',4,5);cast(g)
  changed=wait(g,lambda:g.peek('look_skin',c)==skin and g.room()[c]==raw,180)
  if not changed:g.shot(out/('failed-'+name+'.png'))
  assert changed,(name,pocket(g),states)
  results[name+'_consumes_one_finite_unit']=len(pocket(g))==before-1
  assert wait(g,lambda:not g.peek('elemental_active'),180)
  g.tick(20);g.shot(out/(name+'.png'))
  if name=='tree':
   # A normal hostile projectile uses the real terrain collision path.
   for off,v in enumerate((1,0,120,0,128,7,128,7,0,0)):g.poke('orbs',v,off)
   g.tick(3);results['grown_tree_blocks_an_actual_projectile']=not g.peek('orbs') and g.room()[c]==15
   g.hold({'down'});g.tick(16);g.hold(set());g.tick(4)
   results['grown_tree_blocks_real_movement']=g.peek('pl_y')<=106
 results['burn_has_embers_and_rising_smoke']=w.fx>=10 and w.smoke>=3
 results['distinct_sound_and_animated_states']=w.sounds>=5 and len(states)>=5
 results['tree_is_solid_and_ash_is_walkable']=(15,34) in states and (0,39) in states
 results['native_grid_and_oam_match']=w.grid.report()['all_map_and_attributes_match'] and not w.meter.over
 results['no_late_active_frame']=not w.late
 results['stack_canary']=bytes(g.pb.memory[1,0xde78:0xde98])==bytes([0xa7]*32)
 details={'states':states,'fx_requests':w.fx,'smoke_requests':w.smoke,'sfx_requests':w.sounds,'late_frames':w.late,'grid':w.grid.report()}
 if frames:frames[0].save(out/'plant-water-grow-burn-native.gif',save_all=True,append_images=frames[1:],duration=50,loop=0)
 g.stop()
 # Failed planting leaves finite inventory and stamina intact.
 for raw,protected,label in [(2,False,'concrete'),(0,True,'protected_route')]:
  g=fresh(rom,20);g.poke('room',raw,c);g.poke('look_skin',0,c)
  for k in range(28):g.poke('room',0,258+k)
  if protected:g.poke('room',1<<(c&7),258+(c>>3))
  before=g.peek('stamina');cast(g);g.tick(24)
  results[label+'_keeps_seed_and_stamina']=pocket(g)==[20] and g.peek('stamina')==before and not g.peek('elemental_active');g.stop()
 # Natural lake: a reusable ice tool freezes a bounded patch at the shore, not the
 # entire connected water fixture. Ordinary movement retires and returns it.
 g=Game(rom);assert g.wait_mode(1,1500);g.poke('dbg_world_rule',1);g.poke('dbg_world_size',2)
 assert start_run(g,True,True,seed=0x5101,floor=1) and g.peek('world_on')
 bag(g,[418,409]);g.poke16('tool_piece',418);g.poke('pmv',4,5);g.poke('tool_cd',0);g.poke('stamina',100);g.poke('stamina',100,1)
 sx,sy=g.peek('pl_x')//16,g.peek('pl_y')//16
 down=sy+4<14;facing=4 if down else 0
 x,y=sx,sy+(2 if down else -2);assert 1<=y and y+2<14
 g.poke('pmv',facing,5)
 ox,oy=g.peek('world_view'),g.peek('world_view',1);center=y*16+x
 patch=[];water=[]
 for yy in range(y-1,y+3):
  for xx in range(max(0,x-2),min(16,x+3)):
   cell=yy*16+xx
   if (xx,yy)==(sx,sy):continue
   g.poke('room',13,cell);g.poke('look_skin',44,cell)
   b=258+(cell>>3);g.poke('room',g.peek('room',b)&~(1<<(cell&7)),b);water.append(cell)
 impact_y=sy+(1 if down else -1)
 patch=[c for c in water if abs(c%16-sx)+abs(c//16-impact_y)<=1]
 p=WorldPlayer(g,Path('build/host/land_one'),rom);p.out=out/'lake';p.out.mkdir(exist_ok=True)
 cast(g);assert wait(g,lambda:not g.peek('elemental_active'),220)
 results['ice_freezes_a_bounded_shore_patch']=len(patch)==4 and all(g.room()[c]==29 for c in patch)
 results['lake_outside_patch_stays_water']=all(g.room()[c]==13 for c in water if c not in patch)
 results['ice_tool_recharges_and_stays_in_bag']=418 in pocket(g) and g.peek('tool_cd')<70
 g.shot(out/'lake-frozen.png');p.observe()
 start=p.cell();kind,u,v,condition,_,_=p.nodes[0];assert kind==0
 # Open the first real load gate with ordinary crate pushes, then walk far
 # enough to retire the lake's packet. Merely approaching the gate stays in
 # the original 16-cell view and cannot establish persistence.
 if condition>=2:p.push(p.xy(u-3,v+2),*p.xy(-1,0))
 if condition!=2:
  for cv in (v+1,v):p.push(p.xy(u-3,cv),*p.xy(0,-1))
 p.walk(p.xy(u+2,v));g.tick(12)
 gx,gy=x+ox,y+oy;bank=2+gy//14;address=0xd000+(gy%14)*288+(gx>>2)*9+(gx&3)
 stored=g.pb.memory[bank,address]
 results['reaction_is_stored_outside_active_view']=not (g.peek('world_view')<=gx<g.peek('world_view')+16) and stored in (29,30)
 p.walk(start);g.tick(16)
 local=(gy-g.peek('world_view',1))*16+gx-g.peek('world_view')
 results['returning_camera_restores_the_same_ice']=0<=local<224 and g.room()[local]==stored
 report=p.report();results['lake_travel_keeps_frames_floor_body_and_banks']=not report['late_completions'] and not report['missing_body'] and not report['missing_floor'] and report['stack_canary']
 details['lake']=report;g.stop()
 record={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'fixtures':__doc__,'results':results,'details':details}
 (out/'report.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2));return all(results.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
