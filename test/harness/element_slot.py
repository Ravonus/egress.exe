"""Native quick-slot input, finite material and floor-entry persistence acceptance.
Terrain/inventory writes below are explicit isolated fixtures. Equipping, crafting,
casting, holding reset and continuing use the actual controller loop.
"""
import argparse,hashlib,json
from pathlib import Path
import re
from heir import OamMeter
from play import Game,start_run,step,pocket,bench_open,bench_cursor,bench_act,solve_here
from gameplay_rebuild import walk


def run(rom,out,tool):
 out.mkdir(parents=True,exist_ok=True);r={};log=[];g=Game(rom)
 assert start_run(g,True,True,physical_fixture=1)
 origin=g.player()
 for d in 'URDL':assert step(g,d,log)
 r['pickup_equips_first_material']=pocket(g)==[4,2] and g.peek('tool_piece')==4
 assert bench_act(g,'Cdb',log)
 r['recipe_equips_result_and_consumes_two']=pocket(g)==[8] and g.peek('tool_piece')==8
 # Invalid use is a real tap with the same finite unit and stamina intact.
 x,y=g.player();c=y//16*16+x//16
 face=next(d for d,n in [('up',-16),('down',16),('left',-1),('right',1)] if 0<=c+n<224 and g.peek('room',c+n)==34)
 g.press(face,1);g.tick(4)
 value=g.peek('stamina');g.press('select',3);g.tick(12)
 r['invalid_target_keeps_item_and_stamina']=pocket(g)==[8] and g.peek('stamina')>=value
 assert walk(g,origin[1]//16*16+origin[0]//16,log)
 solve_here(g,tool,log);g.tick(220)
 r['slot_survives_next_floor']=g.peek('floor_no')==2 and g.peek('tool_piece')==8
 save=g.stop();g=Game(rom,sram=save);assert start_run(g,True,True)
 r['slot_restored_with_current_continue']=g.peek('floor_no')==2 and pocket(g)==[8] and g.peek('tool_piece')==8
 g.stop()
 # A stable type, rather than a shifting array index, survives consumption.
 g=Game(rom);assert start_run(g,True,True,physical_fixture=1)
 meter=OamMeter(g)
 for k,p in enumerate([2,1,1]):g.poke('pocket',p,k)
 g.poke('pocket_n',3)
 assert bench_open(g,log);bench_cursor(g,1)
 n=pocket(g);g.press('select',3);g.tick(16)
 r['equipping_never_casts_on_its_closing_release']=pocket(g)==n and g.peek('tool_piece')==1 and not g.peek('bench_on')
 x,y=g.player();c=y//16*16+x//16
 # Put a solid wall in place before approaching, so it cannot materialise
 # under the feet; normal collision and aim remain active.
 g.poke('room',15,c-16)
 g.press('up',2);g.tick(4);assert g.peek('pl_face')==0
 g.poke('stamina',100);g.poke('stamina',24,2)
 g.press('select',3)
 frames=[];poses=set();particle_tiles=set();start=g.peek16('frame_count')
 for _ in range(24):
  g.tick();poses.add(g.peek('ha_state'));particle_tiles.update(g.pb.memory[0xfe00+k*4+2] for k in range(40) if 0<g.pb.memory[0xfe00+k*4]<160 and 0<g.pb.memory[0xfe00+k*4+1]<168);frames.append(g.pb.screen.image.convert('RGB'))
 r['fire_burns_and_consumes_exactly_one']=g.peek('room',c-16)==0 and pocket(g)==[2,1]
 r['compaction_keeps_equipped_material']=g.peek('tool_piece')==1
 r['cast_uses_hand_animation_and_stamina']=31 in poses and g.peek('stamina')<=90
 ember=[v.strip() for v in re.search(r'enum \{ (FXT_CROWN.*?) \}',Path('src/gen/mob_art.h').read_text())[1].split(',')].index('FXT_EMBER')
 r['fire_emits_native_ember_particles']=88+ember*2 in particle_tiles
 r['cast_keeps_native_oam_budget']=meter.over==0
 r['cast_recovers_without_freezing_frames']=((g.peek16('frame_count')-start)&65535)>=24
 frames[0].save(out/'fire-native.gif',save_all=True,append_images=frames[1:],duration=17,loop=0)
 g.shot(out/'fire-after.png')
 g.tick(32);g.poke('room',15,c-15)
 g.hold({'up','right'});g.tick(3);g.hold(set());g.tick(8)
 g.press('select',3);g.tick(24)
 r['diagonal_cast_hits_the_diagonal_cell']=g.peek('room',c-15)==0 and pocket(g)==[2]
 g.tick(32);g.press('select',3);g.tick(8)
 r['empty_slot_never_uses_the_next_material']=pocket(g)==[2] and g.peek('tool_piece')==1
 g.poke('pocket',1,1);g.poke('pocket_n',2)
 # Repeated holding cannot cast again or silently change the selected type.
 g.poke('room',15,c-15);g.hold({'select'});g.tick(32)
 r['holding_does_not_autofire']=pocket(g)==[2,1] and g.peek('room',c-15)==15
 g.tick(32);g.hold(set());g.tick(200)
 r['hold_reset_restores_entry_slot_and_bag']=pocket(g)==[] and g.peek('tool_piece')==0 and g.peek('floor_resets')==1
 g.stop()
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'log':log,'fixtureWrites':'One paper target and finite inventory only; all actions use controller input.'}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values()) and not log

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);p.add_argument('--tool',type=Path,default=Path('build/host/egtool'));a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out,a.tool) else 1)
