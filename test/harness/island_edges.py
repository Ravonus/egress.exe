"""Fall from a real generated island and lose a required crate over its edge; no board or position fixtures."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run
from gameplay_rebuild import walk

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};log=[];g=Game(rom);assert start_run(g,False,True,physical_fixture=1)
    r['generated_boundary_is_real_abyss']=g.room()[192]==41 and g.room()[210]==41
    hp=g.peek('pl_hp');g.hold({'left'});states=[];frames=[]
    for i in range(80):
        g.tick();states.append(g.peek('pl_state'))
        if g.peek('pl_state')==6:g.hold(set())
        if i%2==0:frames.append(g.pb.screen.image.convert('RGB'))
    r['walking_off_actual_edge_falls']=6 in states
    r['real_edge_fall_costs_one_and_recovers']=g.peek('pl_hp')==hp-1 and g.peek('pl_state')!=6 and g.peek('pl_x')>=16
    frames[0].save(out/'generated-edge-fall.gif',save_all=True,append_images=frames[1:],duration=34,loop=0);g.stop()
    g=Game(rom);assert start_run(g,False,True,physical_fixture=1)
    crate=next(i for i,v in enumerate(g.room()[:224]) if v&31==21)
    assert walk(g,crate+1,log);g.hold({'left'})
    for _ in range(450):
        g.tick()
        if not any(v&31==21 for v in g.room()[:224]):break
    g.hold(set());g.tick(300)
    r['pushed_crate_is_lost_to_open_void']=not any(v&31==21 for v in g.room()[:224]) and g.room()[crate//16*16]==41
    r['lost_required_crate_prompts_reset']=g.peek('dead_t')==1;g.shot(out/'lost-crate.png')
    g.hold({'select'});g.tick(62);g.hold(set());g.tick(240)
    r['reset_restores_crate_and_abyss']=g.room()[crate]&31==21 and g.room()[crate//16*16]==41
    g.stop();report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'log':log}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
