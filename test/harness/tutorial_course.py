"""Enter and complete the fixed tutorial with real inputs; preserve a live heir journal."""
import argparse,hashlib,json,re
from pathlib import Path
from play import Game,start_run,bench_act,pocket,MODE_TITLE
from menu import lobby,cursor
from gameplay_rebuild import walk
from companions import fixture

# Current SRAM records stay byte-identical. Only settings/clock records and
# documented volatile floor-entry, CRC, fusion and exact-art workspaces may
# change. Keep both stats journals and both permanent critter records guarded.
VOLATILE=((0x3c0,0x41a),(0x500,0x570),(0x1000,0x14b1),(0x1500,0x1b80),(0x1f00,0x1f01))
ALLOWED=VOLATILE+((0xf68,0xf88),(0x14c0,0x1500))
def durable_changes(before,after):
 return [i for i,(a,b) in enumerate(zip(before,after)) if a!=b and not any(lo<=i<hi for lo,hi in ALLOWED)]

def enter(g):
 lobby(g);cursor(g,3);g.press('a',3)
 for _ in range(800):
  g.tick()
  if g.peek('game_mode')==2:return
 raise AssertionError('course did not load')

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);r={};log=[]
 # Begin from a real existing run. Training must not retire this heir, change
 # its checkpoint or consume its resources, even if power is lost in class.
 g=Game(rom);assert start_run(g,False,True,seed=0x5100)
 heir=bytes(g.peek('family',k) for k in range(19));floor=g.peek('floor_no');seed=g.peek16('run_seed');floor_seed=g.peek16('floor_seed');save=g.stop()
 # Explicit CRC-sealed current-format individuals: an injured Vine Frog
 # with its revival already spent, and a separate carried Ember Fox.
 save=fixture(save,443,bag=[(433,5,False,0)],spent=True,hp=3)
 g=Game(rom,sram=save);enter(g);board=bytes(g.room()[:224]);g.shot(out/'01-course.png')
 r['course_is_fixed']=board[163]==0 and board[104]==11 and board[101]==21 and board[102]==10 and board[73]==9
 frozen=bytes(g.pb.memory[0,0xa000+k] for k in range(8192));r['entry_keeps_normal_run_save']=not durable_changes(save,frozen)
 sym=Path('build/obj-mbc5/tutorial.sym').read_text();offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)',sym,re.M)}
 base=g.addr('tutorial_on')-offsets['_tutorial_on'];phase_at=base+offsets['_pending']
 def phase():return g.pb.memory[1,phase_at] if phase_at>=0xd000 else g.pb.memory[phase_at]
 progress=[]
 def done(want):
  g.hold(set());g.tick(12);progress.append([want,phase(),g.player()]);assert phase()>=want,progress
 assert walk(g,7*16+3,log);done(1)
 g.press('a',2);g.tick(25);done(2)
 g.hold({'a'});g.tick(35);g.hold(set());g.tick(35);done(3)
 g.press('b',2);g.tick(25);done(4)
 g.tick(120);g.hold({'b'});g.tick(35);g.hold(set());g.tick(35);done(5)
 for held,n in [({'up'},2),(set(),3),({'up'},2),(set(),3),({'up'},2),(set(),28)]:g.hold(held);g.tick(n)
 done(6)
 assert walk(g,7*16+4,log);assert walk(g,6*16+4,log)
 g.hold({'right'})
 for _ in range(90):
  g.tick()
  if g.peek('room',102)==22:break
 done(7)
 assert pocket(g)==[1,2],pocket(g)
 assert bench_act(g,'Cab',log);done(8);g.shot(out/'02-combined-steam.png')
 assert walk(g,5*16+7,log);assert walk(g,6*16+7,log);assert walk(g,6*16+9,log);assert walk(g,5*16+12,log)
 # A single-cell void requires a roll/jump; walking into it is not the lesson.
 g.hold(set());g.tick(150);g.press('up',1);g.tick(24)
 for held,n in [({'up'},2),(set(),3),({'up'},12),(set(),32)]:g.hold(held);g.tick(n)
 done(9);g.shot(out/'03-gap-crossed.png')
 r['landing_collects_the_course_key']=g.peek('keys_held')==1
 assert walk(g,2*16+12,log);assert walk(g,2*16+13,log)
 for _ in range(600):
  g.tick()
  if g.peek('game_mode')==MODE_TITLE:break
 r['all_course_goals_completed']=phase()>=9 and not g.peek('tutorial_on') and g.peek('game_mode')==MODE_TITLE
 after=g.stop();changed=durable_changes(save,after);log.append('durable journal changes: '+str(changed[:40]));r['training_keeps_existing_heir_and_checkpoint']=not changed
 g=Game(rom,sram=after);assert start_run(g,False,True,seed=0x5100)
 r['continue_restores_existing_heir_and_floor']=bytes(g.peek('family',k) for k in range(19))==heir and g.peek('floor_no')==floor and g.peek16('run_seed')==seed and g.peek16('floor_seed')==floor_seed
 r['continue_keeps_pet_health_and_spent_revive']=g.peek16('companion')==443 and g.peek('companion',8)==3 and bool(g.peek('companion',6)&128) and g.peek('pet_instance')==1
 r['continue_keeps_carried_pet_individual']=pocket(g)==[433] and g.peek('pet_meta')==5 and g.peek('pet_meta',2)==2
 g.stop()
 g=Game(rom,sram=after);enter(g);r['replay_uses_same_authored_board']=bytes(g.room()[:224])==board
 g.press('start',3);g.tick(12);g.press('start',3);g.tick(40);r['menu_start_leaves_training']=not g.peek('tutorial_on');g.stop()
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'progress':progress,'log':log,'allowed_settings_clock_and_volatile_ranges':ALLOWED,'pet_fixture':{'active':443,'health':3,'revive_spent':True,'carried':433,'carried_health':5}}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
