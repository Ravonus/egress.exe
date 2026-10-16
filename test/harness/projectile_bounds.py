"""Fire actual B taps out of the map; shots must leave without wrapping or reading beyond the board."""
import argparse, hashlib, json
from pathlib import Path
from play import Game,start_run
from stage2 import teleport

CASES=[('west',12,136,{'left'}),('east',244,136,{'right'}),
       ('north',120,12,{'up'}),('south',120,212,{'down'})]

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);results={};detail=[]
    for name,px,py,buttons in CASES:
        g=Game(rom);assert start_run(g,True,True,floor=2)
        g.poke('stage_type',0);g.poke('nen',0)
        for i in range(224):g.poke('room',0,i)
        teleport(g,px,py)
        g.hold(buttons);g.tick(2);g.hold(set());g.tick(2);g.press('b',2)
        trace=[]
        for _ in range(18):
            g.tick()
            for k in range(8):
                if g.peek('orbs',k*10):
                    trace.append((g.peek16_at('orbs',k*10+4),g.peek16_at('orbs',k*10+6)))
        results[f'{name}_shot_was_fired']=bool(trace)
        results[f'{name}_never_wraps']=all(x<4096 and y<3584 for x,y in trace)
        results[f'{name}_shot_leaves_map']=not any(g.peek('orbs',k*10) for k in range(8))
        detail.append({'edge':name,'trace':trace});g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'detail':detail}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
