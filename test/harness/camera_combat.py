"""Camera terrain streaming while actual holdout waves and both action buttons run."""
import argparse,hashlib,json
from pathlib import Path
from stage2 import fresh
from gameplay_rebuild import walk
from heir import OamMeter
from oam_observer import OamObserver

def run(rom,out,measure=False):
    out.mkdir(parents=True,exist_ok=True);g=fresh(rom,1,enemies=True);g.tick(40)
    meter=OamMeter(g);observer=OamObserver(g);tick=g.tick;fc=[];max_alive=0;moves=set();log=[];frames=[]
    def record(n=1):
        nonlocal max_alive
        for _ in range(n):
            tick();fc.append(g.peek16('frame_count'));max_alive=max(max_alive,sum(g.peek('en',k*16+1)!=6 for k in range(g.peek('nen'))));moves.add(g.peek('pin',2))
            if len(fc)%6==0:frames.append(g.pb.screen.image.convert('RGB'))
    g.tick=record
    assert walk(g,g.peek('stg',8),log);g.hold(set());g.tick(g.peek16_at('stg',10)+60)
    for turn in range(14):
        target=[163,172,131,140][turn%4]
        walk(g,target,log,4)
        live=[k for k in range(g.peek('nen')) if g.peek('en',k*16+1)!=6]
        x,y=g.player()
        if live:
            k=min(live,key=lambda k:abs(g.peek16_at('en',k*16+8)//16-x)+abs(g.peek16_at('en',k*16+10)//16-y))
            dx=g.peek16_at('en',k*16+8)//16-x;dy=g.peek16_at('en',k*16+10)//16-y
            aim={'right' if dx>0 else 'left'} if abs(dx)>abs(dy) else {'down' if dy>0 else 'up'}
            g.hold(aim);g.tick(2)
        g.hold(aim|{'a' if turn%2 else 'b'} if live else {'a' if turn%2 else 'b'});g.tick(2)
        g.hold(aim if live else set());g.tick(26)
        if turn%4==3:g.hold({'a'});g.tick(34);g.hold(set());g.tick(25)
    spans=[(fc[i]-fc[i-120])&65535 for i in range(120,len(fc))]
    r={'real_wave_mobs_present':max_alive>=2,'both_action_buttons_used':5 in moves and 11 in moves,
       'actual_player_kills':g.peek('kills_run')>0,'sprite_line_budget':meter.over==0,
       'combat_and_travel_120_per_120':bool(spans) and min(spans)==120}
    if frames:frames[0].save(out/'camera-combat.gif',save_all=True,append_images=frames[1:],duration=102,loop=0)
    g.shot(out/'combat.png');allocations=observer.report();r['native_allocator_matches_reference']=allocations['all_allocations_match'];g.stop();report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'frames':len(fc),'minimum_game_frames_per_120':min(spans) if spans else None,'oam_max':meter.max_line,'max_enemies':max_alive,'allocator':allocations,'moves':sorted(moves),'log':log}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return measure or all(r.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);p.add_argument('--measure',action='store_true')
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out,a.measure) else 1)
