"""Real generated push room: player-weight switches, stranded crates and held-SELECT reset."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run
from gameplay_rebuild import walk

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};log=[];g=Game(rom);assert start_run(g,True,True)
    plate=next(i for i,v in enumerate(g.room()[:224]) if v&31==10)
    gate=next(i for i,v in enumerate(g.room()[:224]) if v&31==11)
    assert walk(g,plate,log);g.hold(set());g.tick(30)
    r['standing_on_plate_opens_gate']=g.room()[gate]&31==12;g.shot(out/'01-player-weight.png')
    g.hold({'down'});g.tick(14);g.hold(set());g.tick(30)
    r['leaving_plate_releases_gate']=g.room()[gate]&31==11;g.stop()
    g=Game(rom);assert start_run(g,True,True)
    g.pb.memory[0xddd0:0xddf0]=[0xa7]*32
    crate=next(i for i,v in enumerate(g.room()[:224]) if v&31==21)
    assert walk(g,crate+1,log);g.hold({'left'})
    for _ in range(380):
        g.tick()
        if g.room()[crate//16*16+1]&31==21:break
    g.hold(set());g.tick(300)
    r['crate_reaches_unpushable_rim']=g.room()[crate//16*16+1]&31==21
    r['stranded_crate_shows_reset_cue']=g.peek('dead_t')==1;g.shot(out/'02-stranded-crate.png')
    g.hold({'select'});g.tick(62);g.hold(set());g.tick(240)
    r['hold_select_restores_original_crate']=g.room()[crate]&31==21 and g.room()[crate//16*16+1]&31==0 and g.peek('dead_t')==0
    r['floor_stack_does_not_reach_skin_scratch']=bytes(g.pb.memory[0xddd0:0xddf0])==bytes([0xa7]*32)
    g.shot(out/'03-reset.png');g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'log':log}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
