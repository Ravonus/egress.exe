"""Physical bench adjacency and combat lock, plus an inventory without recipes.
Only the two finite material units and enemies_alive are isolated fixtures;
walking, A attacks, opening, crafting rejection and equipping are controller input.
Real combat completion and bench unlocking are also checked by waves.py.
"""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,bench_visit,bench_open,pocket
from stage2 import fresh

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);r={};log=[];g=Game(rom)
 assert start_run(g,True,True,physical_fixture=1)
 benches=[i for i,c in enumerate(g.room()[:224]) if c==34]
 r['generator_places_a_solid_bench']=bool(benches)
 assert bench_visit(g,log)
 g.poke('enemies_alive',1);g.press('a',3)
 r['locked_bench_does_not_open']=not g.peek('bench_on')
 r['locked_bench_leaves_a_as_attack']=g.peek('pl_state')!=0
 g.poke('enemies_alive',0);g.tick(80);g.press('a',3);g.tick(8)
 r['a_next_to_cleared_bench_opens_crafting']=g.peek('bench_on')==1 and g.peek('bench_mode')==1
 g.shot(out/'physical-bench.png');g.press('b',3);g.tick(12)
 g.poke('pocket_n',2);g.poke('pocket',1,0);g.poke('pocket',2,1)
 assert bench_open(g,log)
 r['start_inventory_has_no_crafting_mode']=not g.peek('bench_mode')
 before=pocket(g)
 for _ in range(3):g.press('a',3);g.tick(12)
 r['bag_cannot_make_a_recipe']=pocket(g)==before and g.peek('bench_on')==1 and not g.peek('bench_phase')
 g.shot(out/'bag.png');g.press('select',3);g.tick(12)
 r['bag_equips_without_consuming_or_casting']=g.peek('tool_piece')==1 and pocket(g)==before and not g.peek('bench_on')
 r['bench_material_survives_open_and_close']=all(g.peek('room',i)==34 for i in benches)
 g.stop()
 # The no-enemy difficulty fixture advances the real finite wave scheduler.
 # Carrying the requested result must not finish the stage before those waves.
 g=fresh(rom,5);g.poke('pocket_n',1);g.poke('pocket',g.peek('stg',14))
 g.tick(8);r['carried_recipe_cannot_skip_pending_waves']=not g.peek('stage_won')
 g.tick(1800)
 r['mixing_stage_has_finite_waves']=g.peek('stage_wave')==g.peek('stg',5) and bool(g.peek('stage_won'))
 assert bench_open(g,log,craft=True)
 r['mixing_bench_unlocks_after_final_wave']=g.peek('bench_mode')==1
 g.stop()
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'bench_cells':benches,'log':log}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values()) and not log

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
 a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
