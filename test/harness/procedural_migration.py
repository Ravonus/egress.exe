"""Upgrade actual prior-ROM saves onto the current constructor without losing progress."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,pocket

def run(previous,rom,out,tool):
    out.mkdir(parents=True,exist_ok=True);cases=[]
    def compare(g,label):
        before=bytes(g.room());seed=g.peek16('run_seed');floor=g.peek('floor_no')
        gene=bytes(g.peek('family',k) for k in range(19));generation=g.peek16_at('family',279)
        stats={k:g.peek(k) for k in ['pl_hp','pl_hp_max','tokens','mods','wmods','kills_run','heart_shards','floor_resets']};bag=pocket(g)
        g.shot(out/(label+'-before.png'));save=g.stop()
        n=Game(rom,sram=save);assert start_run(n,True,True,seed=None)
        current=bytes(n.room());world=n.peek('world_on');plan=bytes(n.peek('world_plan',k) for k in range(83)) if world else None
        results={'same_run_seed':n.peek16('run_seed')==seed,'same_floor':n.peek('floor_no')==floor,
                 'same_heir_and_family':bytes(n.peek('family',k) for k in range(19))==gene and n.peek16_at('family',279)==generation,
                 'same_health_inventory_and_counters':pocket(n)==bag and all(n.peek(k)==v for k,v in stats.items()),
                 'current_constructor_and_hazards':n.peek('slice',22)&96==96}
        n.shot(out/(label+'-upgraded.png'));upgraded=n.stop()
        fresh=Game(rom);assert start_run(fresh,True,True,seed=seed,floor=floor)
        results['matches_current_generator']=bytes(fresh.room())==current and fresh.peek('world_on')==world
        fresh.stop()
        again=Game(rom,sram=upgraded);assert start_run(again,True,True,seed=None)
        results['upgrade_is_durable_and_idempotent']=bytes(again.room())==current and (not world or bytes(again.peek('world_plan',k) for k in range(83))==plan)
        again.stop();cases.append({'case':label,'layout_changed':current!=before,'results':results})
    g=Game(previous);assert start_run(g,True,True);compare(g,'procedural-opening')
    g=Game(previous);assert start_run(g,True,True,floor=2);assert g.peek('floor_no')==2;compare(g,'survival-arena')
    g=Game(previous);assert start_run(g,True,True,seed=0x5fed,floor=7);assert g.peek('world_on');compare(g,'spatial-portal')
    report={'previous_sha256':hashlib.sha256(previous.read_bytes()).hexdigest(),'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'cases':cases}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return all(all(c['results'].values()) for c in cases)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('previous','rom','out'):p.add_argument(n,type=Path)
    p.add_argument('--tool',type=Path,default=Path('build/host/egtool'));a=p.parse_args();raise SystemExit(0 if run(a.previous,a.rom,a.out,a.tool) else 1)
