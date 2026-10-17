"""Survival waves through real attacks; only the stage difficulty is a fixture.
No enemy-health writes, teleportation, or simulated kills. All spawn art is native.
"""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run
from gameplay_rebuild import walk
from heir import OamMeter

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);results={};cases=[]
    for level in (0,5,10):
        g=Game(rom);entry=g.sym['_stage_build']
        def choose_level(_,level=level):g.poke('slice',level,19)
        g.pb.hook_register(entry>>16,entry&65535,choose_level,None)
        assert g.wait_mode(1,1500);g.poke('dbg_stage',2)
        assert start_run(g,True,False,seed=0xb222,floor=2)
        meter=OamMeter(g);original=g.tick;families=set();by_wave={};wave_start={};wave_kills={};max_alive=0;fc=[];log=[]
        def record(n=1):
            nonlocal max_alive
            for _ in range(n):
                original();fc.append(g.peek16('frame_count'))
                live=[k for k in range(g.peek('nen')) if g.peek('en',k*16+1)!=6]
                max_alive=max(max_alive,len(live))
                w=g.peek('stage_wave')
                if w:
                    wave_start.setdefault(w,g.peek('kills_run'))
                    wave_kills[w]=g.peek('kills_run')-wave_start[w]
                for k in live:
                    fam=g.peek('mob',k*19+7);families.add(fam)
                    by_wave.setdefault(w,set()).add(fam)
        g.tick=record
        # Start tile is far outside the former charge ring. Actual enemies
        # must spawn and progress cannot advance until they are defeated.
        g.tick(360)
        x,y=g.player();ring=g.peek('stg',8)
        outside=max(abs(x//16-ring%16),abs(y//16-ring//16))>g.peek('stg',9)
        results[f'level_{level}_spawns_without_standing_zone']=outside and max_alive>0 and g.peek('stage_wave')==1
        results[f'level_{level}_waiting_cannot_win']=not g.peek('stage_won') and g.peek('stage_pct')==0
        g.shot(out/f'level-{level}-incoming.png')
        for turn in range(650):
            if g.peek('stage_won'):break
            live=[k for k in range(g.peek('nen')) if g.peek('en',k*16+1)!=6]
            if not live:g.hold(set());g.tick(30);continue
            x,y=g.player()
            k=min(live,key=lambda k:abs(g.peek16_at('en',k*16+8)//16-x)+abs(g.peek16_at('en',k*16+10)//16-y))
            ex=g.peek16_at('en',k*16+8)//16;ey=g.peek16_at('en',k*16+10)//16
            if max(abs(ex-x),abs(ey-y))>24:walk(g,(ey//16)*16+ex//16,log,2)
            x,y=g.player();ex=g.peek16_at('en',k*16+8)//16;ey=g.peek16_at('en',k*16+10)//16
            aim={'right' if ex>x else 'left'} if abs(ex-x)>abs(ey-y) else {'down' if ey>y else 'up'}
            g.hold(aim);g.tick(1);g.hold({'a'});g.tick(34);g.hold(set());g.tick(40)
        results[f'level_{level}_victory_requires_real_kills']=bool(g.peek('stage_won')) and g.peek('kills_run')>0 and g.peek('stage_pct')==100
        results[f'level_{level}_finite_complete_waves']=g.peek('stage_wave')==g.peek('stg',5) and not g.peek('enemies_alive')
        results[f'level_{level}_pooled_three_live_enemies']=max_alive<=3 and meter.over==0
        results[f'level_{level}_every_wave_grows']=all(wave_kills.get(w,0)==min(8,g.peek('stg',6)+level//5+w-1) for w in range(1,g.peek('stg',5)+1))
        spans=[(fc[i]-fc[i-120])&65535 for i in range(120,len(fc))]
        results[f'level_{level}_combat_120_per_120']=bool(spans) and min(spans)==120
        if level>=5:results[f'level_{level}_different_enemy_roles']=len(families)>=3
        g.shot(out/f'level-{level}-cleared.png')
        cases.append({'level':level,'generated_level':g.peek('stg',1),'waves':g.peek('stage_wave'),
            'kills':g.peek('kills_run'),'max_alive':max_alive,'families':sorted(families),
            'by_wave':{k:sorted(v) for k,v in by_wave.items()},'wave_kills':wave_kills,
            'minimum_game_frames_per_120':min(spans) if spans else None,'oam_max':meter.max_line,'log':log[:8]})
        g.stop()
    results['later_stages_spawn_more_enemies']=cases[-1]['kills']>cases[0]['kills']
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'cases':cases}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
