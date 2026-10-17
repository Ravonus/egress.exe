"""Continue genuine prior-ROM floor-entry journals without changing their maps."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,solve_here

def run(previous,rom,out,tool):
    out.mkdir(parents=True,exist_ok=True);cases=[]
    def compare(g,label):
        before=bytes(g.room());seed=g.peek16('run_seed');world=g.peek('world_on')
        plan=bytes(g.peek('world_plan',k) for k in range(83)) if world else None
        g.shot(out/(label+'-before.png'));save=g.stop()
        n=Game(rom,sram=save);assert start_run(n,True,True,seed=None)
        results={'same_run_seed':n.peek16('run_seed')==seed,'same_native_room':bytes(n.room())==before,
                 'same_world_mode':n.peek('world_on')==world,
                 'same_spatial_plan':not world or bytes(n.peek('world_plan',k) for k in range(83))==plan}
        n.shot(out/(label+'-continued.png'));n.stop();cases.append({'case':label,'results':results})
    g=Game(previous);assert start_run(g,True,True);compare(g,'procedural-opening')
    g=Game(previous);assert start_run(g,True,True,floor=2)
    assert g.peek('floor_no')==2;compare(g,'survival-arena')
    g=Game(previous);assert start_run(g,True,True,seed=0x5fed,floor=7);assert g.peek('world_on');compare(g,'spatial-portal')
    report={'previous_sha256':hashlib.sha256(previous.read_bytes()).hexdigest(),'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'cases':cases}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return all(all(c['results'].values()) for c in cases)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('previous','rom','out'):p.add_argument(n,type=Path)
    p.add_argument('--tool',type=Path,default=Path('build/host/egtool'));a=p.parse_args();raise SystemExit(0 if run(a.previous,a.rom,a.out,a.tool) else 1)
