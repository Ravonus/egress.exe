"""Lose a finite world-puzzle answer through input, observe its cue, then reset."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,bench_open,pocket
from world_play import WorldPlayer

def run(rom,out,tool):
    out.mkdir(parents=True,exist_ok=True);g=Game(rom);log=[]
    try:
        assert g.wait_mode(1,1500)
        g.poke('dbg_world_rule',1);g.poke('dbg_world_size',1)
        assert start_run(g,True,True,seed=0x5fed,floor=7)
        p=WorldPlayer(g,tool,rom);p.out=out;g.tick(160)
        original=p.plan;kind,u,v,condition,*_=p.nodes[0]
        assert kind==0 and condition==0 and len(p.nodes)==1
        for iu,iv,_ in p.items:p.walk(p.xy(iu,iv))
        assert len(pocket(g))==2 and bench_open(g,log),log
        while pocket(g):
            before=len(pocket(g));g.hold({'right'})
            for _ in range(240):
                g.tick()
                if len(pocket(g))==before-1:break
            g.hold(set());g.tick(20)
            assert len(pocket(g))==before-1
        g.press('b',4);g.tick(20);assert not g.peek('bench_on')
        crate=p.xy(u-3,v+1)
        while crate[0]>1:
            p.push(crate,-1,0);crate=(crate[0]-1,crate[1])
            if p.cells[crate[1]*p.w+crate[0]]&31 not in (21,22,23):break
        for waited in range(2400):
            if g.peek('dead_t'):break
            g.tick()
        assert g.peek('dead_t'),'irreversible crate loss never showed the reset cue'
        g.tick(4);g.shot(out/'room-forgets.png');before=p.report()
        p.monitor=False;g.hold({'select'});g.tick(65);g.hold(set());g.tick(300)
        assert g.peek('floor_resets')==1 and not g.peek('dead_t')
        assert bytes(g.peek('world_plan',i) for i in range(83))==original
        assert not pocket(g)
        ox,oy=g.peek('world_view'),g.peek('world_view',1)
        cx,cy=p.xy(u-3,v+1);local=(cy-oy)*16+cx-ox
        assert 0<=cx-ox<16 and 0<=cy-oy<14 and g.peek('room',local)&31==21
        assert all(g.peek('room',(iv-oy)*16+iu-ox)&31==17 for iu,iv,_ in p.items)
        assert bytes(g.pb.memory[0xddd0:0xddf0])==bytes([0xa7]*32)
        g.shot(out/'reset.png')
        report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'crate_stopped_or_lost_at':crate,
                'warning_wait_frames':waited,'finite_alternatives_discarded':True,
                'reset_restores_plan_crate_and_ingredients':True,'native_input_only_after_start':True,
                'before_reset':before}
        (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
        return not before['missing_body'] and not before['heir_pixel_mismatches'] and not before['oam_over']
    finally:g.stop()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    p.add_argument('--tool',type=Path,default=Path('build/host/land_one'));a=p.parse_args()
    raise SystemExit(0 if run(a.rom,a.out,a.tool) else 1)
