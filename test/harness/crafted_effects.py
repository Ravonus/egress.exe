"""Controller-driven crafted materials, finite fuse flasks and reusable tools.
Only starting inventory, target tiles and enemies are fixtures. Effects, pool
allocation, damage, resource use, cooldowns and rendering execute in the ROM."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,pocket
from stage2 import teleport
from heir import OamMeter
from companions import wait,target

def fresh(rom,id,observer=None):
 g=Game(rom)
 if observer:observer(g)
 assert start_run(g,True,True,physical_fixture=1);g.poke('stage_type',0);teleport(g,120,104);g.tick(3)
 for y in range(4,11):
  for x in range(3,13):g.poke('room',0,y*16+x)
 for k in range(24):g.poke('pocket',0,k)
 for k in range(3):g.poke('pocket_hi',0,k)
 for k in range(72):g.poke('pet_meta',0,k)
 g.poke('pocket',id&255);g.poke('pocket_hi',id>>8);g.poke('pocket_n',1);g.poke16('tool_piece',id);g.poke('tool_cd',0);g.poke('stamina',100);g.poke('stamina',100,1);g.poke('pmv',4,5)
 g.pb.memory[1,0xde78:0xde98]=[0xa7]*32
 return g

def use(g):g.press('select',4);g.tick(6)
def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);r={};details={}
 # Fire shard: consumes exactly one nine-bit unit only on a real reaction.
 g=fresh(rom,25);c=7*16+7;g.poke('room',15,c);before=g.peek('stamina');use(g)
 r['formed_material_reacts_and_consumes_one_unit']=not pocket(g) and g.room()[c]==0 and g.peek('stamina')<before;g.shot(out/'formed-fire.png');g.stop()
 g=fresh(rom,25);before=g.peek('stamina');use(g)
 r['wrong_target_keeps_material_and_stamina']=pocket(g)==[25] and g.peek('stamina')==before;g.stop()
 # ID 256 is a cloud form, not the low-byte item 0 or 1.
 g=fresh(rom,256);target(g);use(g)
 r['item_256_uses_its_real_family']=not pocket(g) and bool(g.peek('mob',10)&1) and g.peek('en',3)==80;g.stop()
 # A fuse is a real pooled object. It travels, burns down, reacts, then frees
 # its slot; it must never get ordinary returned-orb damage/render handling.
 g=fresh(rom,385);meter=OamMeter(g)
 for y in range(7,11):g.poke('room',15,y*16+6)
 before=g.peek('stamina');use(g);live=[k for k in range(8) if g.peek('orbs',k*10) and g.peek('orbs',k*10+1)&128]
 r['bomb_uses_finite_inventory_stamina_and_existing_pool']=len(live)==1 and not pocket(g) and g.peek('stamina')<before
 poses=set();positions=set();pixels=[];phases=set()
 for _ in range(120):
  phases.update(g.peek('orbs',k*10) for k in range(8) if g.peek('orbs',k*10+1)&128)
  for k in live:
   positions.add((g.peek16_at('orbs',k*10+4),g.peek16_at('orbs',k*10+6)))
  for k in range(40):
   t=g.pb.memory[0xfe02+k*4]
   if t in (120,122) and g.pb.memory[0xfe00+k*4]:poses.add(t)
  if len(pixels)<30:pixels.append(g.pb.screen.image.convert('RGB').resize((320,288)))
  g.tick()
 r['bomb_has_motion_fuse_animation_and_spent_state']=len(positions)>4 and poses=={120,122} and {2,3}.issubset(phases) and not g.peek('elemental_active') and not any(g.peek('orbs',k*10) for k in range(8))
 r['bomb_reacts_with_paper_in_its_area']=any(g.room()[y*16+6]==0 for y in range(7,11))
 r['bomb_oam_and_stack_limits']=meter.over==0 and bytes(g.pb.memory[1,0xde78:0xde98])==bytes([0xa7]*32)
 if pixels:pixels[0].save(out/'fuse-native.gif',save_all=True,append_images=pixels[1:],duration=34,loop=0)
 g.stop()
 g=fresh(rom,385);g.poke('room',3,c);use(g);g.tick(100)
 r['bombs_break_real_drywall']=g.room()[c]==0;g.stop()
 for defence,name in [(0,'damages_a_real_enemy'),(3,'respects_crush_only_armour')]:
  g=fresh(rom,385);target(g);g.poke('en',3,3);g.poke('mob',defence,2);use(g);g.tick(110)
  r['bomb_'+name]=(g.peek('en',1)==6 and g.peek('kills_run')>0) if not defence else (g.peek('en',1)!=6 and g.peek('en',3)==3)
  g.stop()
 # Filling the existing projectile pool cannot consume another bomb.
 g=fresh(rom,385)
 for k in range(8):
  for off,v in enumerate((1,1,200,0,128,6,128,5,0,0)):g.poke('orbs',v,k*10+off)
 before=g.peek('stamina');use(g);r['full_pool_keeps_bomb']=pocket(g)==[385] and g.peek('stamina')==before;g.stop()
 # Durable focused fire reuses the same crafted item after its cooldown.
 g=fresh(rom,409);g.poke('room',15,c);use(g);cd=g.peek('tool_cd');r['tool_reacts_without_being_consumed']=pocket(g)==[409] and g.room()[c]==0 and cd>0
 g.poke('room',15,c);use(g);r['cooldown_prevents_repeat_use']=g.room()[c]==15 and g.peek('tool_cd')<cd
 g.tick(90);g.poke('pmv',4,5);use(g);r['recharged_tool_can_react_again']=g.room()[c]==0 and pocket(g)==[409];g.stop()
 # LIFE's focused tool heals through the same quarter-heart routing core.
 g=fresh(rom,426);g.poke('pl_hp',g.peek('pl_hp_max')-2);g.poke('pl_quarter',1);before=g.peek('pl_hp');use(g)
 r['life_tool_heals_without_consuming_the_tool']=g.peek('pl_hp')>before and pocket(g)==[426] and g.peek('tool_cd')>0;g.stop()
 # A shaped metal ward protects the heir without requiring a companion.
 g=fresh(rom,413);use(g);r['ward_tool_has_a_timed_active_state']=g.peek('elemental_ward')>0 and pocket(g)==[413]
 g.poke('dbg_god',0);g.poke('pl_inv',0);hp=g.peek('pl_hp');ward=g.peek('elemental_ward');x,y=g.player()
 for off,v in enumerate((1,0,120,0,(x<<4)&255,x>>4,(y<<4)&255,y>>4,0,0)):g.poke('orbs',v,off)
 g.tick(8);r['ward_absorbs_a_real_enemy_projectile']=g.peek('pl_hp')==hp and g.peek('elemental_ward')<ward-8;g.stop()
 g=fresh(rom,17);use(g);r['glass_creates_a_finite_visible_ward']=not pocket(g) and g.peek('elemental_ward')>0;g.stop()
 # Boss windows use their own hit intake; its accepted result must still
 # consume a finite material, while closed armour leaves that unit intact.
 for opened in (False,True):
  g=Game(rom);assert g.wait_mode(1,1500);g.poke('dbg_boss',1);assert start_run(g,True,True,floor=5)
  x,y=g.peek('bs_x'),g.peek('bs_y');teleport(g,x,y+24);g.tick(2)
  g.poke('bs_st',2);g.poke('bs_hs',3 if opened else 0);g.poke('bs_t',200);g.poke('bs_open',int(opened))
  for k in range(24):g.poke('pocket',0,k)
  for k in range(3):g.poke('pocket_hi',0,k)
  g.poke('pocket',25);g.poke('pocket_n',1);g.poke16('tool_piece',25);g.poke('tool_cd',0);g.poke('stamina',100);g.poke('stamina',100,1);g.poke('pmv',0,5)
  hp=g.peek('bs_hp');use(g)
  r['boss_open_hit_consumes_one_material' if opened else 'boss_closed_armour_keeps_material']=(g.peek('bs_hp')<hp and not pocket(g)) if opened else (g.peek('bs_hp')==hp and pocket(g)==[25])
  g.stop()
 # A whole bag of duplicate reusable weights needs only two capability
 # markers, never 48 writes into the reset checker workspace.
 g=fresh(rom,429)
 for k in range(24):g.poke('pocket',429&255,k)
 for k in range(3):g.poke('pocket_hi',255,k)
 g.poke('pocket_n',24);g.poke('effect_n',128);g.poke('puzzle_dirty',1);g.tick(180)
 r['reusable_weight_certificate_is_bounded']=g.peek('effect_n')==2 and bytes(g.pb.memory[1,0xde78:0xde98])==bytes([0xa7]*32);g.stop()
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'fixtures':__doc__,'results':r};(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
