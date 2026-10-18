"""Every native fusion, reversed ingredient order, four-card paging and duplicate quantities."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,bench_open,bench_cursor,pocket

PAIRS=[(2,1,13),(4,2,8),(4,1,14),(4,3,15),(3,2,7),(3,1,6),(3,13,16),(3,7,9),(2,9,10),(1,8,11),(6,5,12)]

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);g=Game(rom);log=[];results={};details=[]
 assert start_run(g,True,True,physical_fixture=1)
 for i,(a,b,r) in enumerate(PAIRS):
  if i&1:a,b=b,a
  g.poke('pocket_n',2);g.poke('pocket',a,0);g.poke('pocket',b,1)
  assert bench_open(g,log,craft=True)
  bench_cursor(g,0);g.press('a',4);g.tick(12);bench_cursor(g,1);g.press('a',4);g.tick(12)
  g.press('a',4)
  for _ in range(500):
   g.tick()
   if g.peek('bench_phase')==4 and g.peek('bench_on')==1:break
  g.shot(out/f'result-{r:02}.png');waiting=pocket(g)==[a,b]
  g.press('a',4);g.tick(18);ok=pocket(g)==[r] and g.peek('tool_piece')==r
  results[f'recipe_{a}_{b}_makes_{r}_on_collection']=waiting and ok
  details.append({'ingredients':[a,b],'result':r,'collects':ok})
  g.press('b',4);g.tick(12)
 for k in range(16):g.poke('pocket',k+1,k)
 for k in range(16,24):g.poke('pocket',5,k)
 g.poke('pocket_n',24)
 assert bench_open(g,log)
 for i in (0,6,12,15):
  bench_cursor(g,i);g.shot(out/f'bag-page-{i}.png')
 results['all_sixteen_materials_can_be_selected_without_discard']=g.peek('bench_cur')==15 and g.peek('pocket_n')==24
 g.press('b',4);g.tick(12);g.stop()
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'recipes':details,'fixture':'Material quantities only; physical approach, selection, mixing and collection use controller input.','log':log}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values()) and not log

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
