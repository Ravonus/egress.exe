"""Survival waves through real attacks; only the stage difficulty is a fixture.
No enemy-health writes, teleportation, or simulated kills. All spawn art is native.
"""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,bench_open
from gameplay_rebuild import walk
from heir import OamMeter
from clock_progress import ClockProgress
from hud_vblank import HudVblank
from fx_progress import FxProgress
from mob_render import MobRender
from shot_render import ShotRender
from spawn_view import SpawnView

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);results={};cases=[]
    for level in (0,5,10):
        g=Game(rom);entry=g.sym['_stage_build']
        def choose_level(_,level=level):g.poke('slice',level,19)
        g.pb.hook_register(entry>>16,entry&65535,choose_level,None)
        assert g.wait_mode(1,1500);g.poke('dbg_stage',2)
        assert start_run(g,True,False,seed=0xb222,floor=2)
        meter=OamMeter(g);clock=ClockProgress(g,rom);hud=HudVblank(g,rom);fx=FxProgress(g,rom);shots=ShotRender(g);art=MobRender(g,observe_group=shots.group);spawn=SpawnView(g);original=g.tick;families=set();by_wave={};wave_start={};wave_kills={};max_alive=0;fc=[];log=[];hitches=[];late=[];epochs=[];starts=[]
        def input_start(_):
            if g.peek('game_mode')==2:
                ly=g.pb.memory[0xff44];starts.append(ly-144 if ly>=144 else ly+10)
        at=g.sym['_input_poll'];g.pb.hook_register(at>>16,at&65535,input_start,None)
        def completed(_):
            if g.peek('game_mode')!=2:return
            epoch=g.peek('frame_epoch')
            if g.peek('video_epoch')!=epoch:late.append(g.peek16('frame_count'))
            epochs.append(epoch if not epochs else epochs[-1]+((epoch-epochs[-1])&255))
        at=g.sym['_frame_wait'];g.pb.hook_register(at>>16,at&65535,completed,None)
        def record(n=1):
            nonlocal max_alive
            for _ in range(n):
                before=g.peek16('frame_count');original();clock.observe();after=g.peek16('frame_count');fc.append(after)
                if ((after-before)&65535)!=1:hitches.append({'display':g.frames,'before':before,'after':after,'pc':hex(g.pb.register_file.PC),'line':g.pb.memory[0xff44],'mode':g.peek('game_mode'),'wave':g.peek('stage_wave'),'state':g.peek('pl_state'),'epoch_delta':(g.peek('video_epoch')-g.peek('frame_epoch'))&255})
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
            dx,dy=ex-x,ey-y
            aim=set()
            if abs(dx)*2>=abs(dy):aim.add('right' if dx>0 else 'left')
            if abs(dy)*2>=abs(dx):aim.add('down' if dy>0 else 'up')
            g.hold(aim);g.tick(1);g.hold({'a'})
            for _ in range(34):
                g.tick()
                if g.peek('stage_won'):break
            g.hold(set())
            if g.peek('stage_won'):break
            for _ in range(40):
                g.tick()
                if g.peek('stage_won'):break
        results[f'level_{level}_victory_requires_real_kills']=bool(g.peek('stage_won')) and g.peek('kills_run')>0 and g.peek('stage_pct')==100
        results[f'level_{level}_finite_complete_waves']=g.peek('stage_wave')==g.peek('stg',5) and not g.peek('enemies_alive')
        results[f'level_{level}_pooled_three_live_enemies']=max_alive<=3 and meter.over==0
        results[f'level_{level}_every_wave_grows']=all(wave_kills.get(w,0)==min(8,g.peek('stg',6)+level//5+w-1) for w in range(1,g.peek('stg',5)+1))
        spans=[(fc[i]-fc[i-120])&65535 for i in range(120,len(fc))]
        # Observe completed updates against the actual VBlank epoch. Sampling
        # frame_count at LCD line zero can land just before/after ISR return
        # and produce a paired 0/2 even when no update missed its deadline.
        windows=[epochs[i]-epochs[i-120] for i in range(120,len(epochs))]
        results[f'level_{level}_combat_120_per_120']=bool(windows) and min(windows)==max(windows)==120
        results[f'level_{level}_completes_before_vblank']=not late
        results[f'level_{level}_wakes_at_vblank']=bool(starts) and max(starts)<20
        results[f'level_{level}_clock_preserves_elapsed_vblanks']=clock.report()['elapsed_vblanks_preserved']
        results[f'level_{level}_hud_is_exact_and_bounded']=hud.report()['native_hud_is_exact_and_bounded']
        results[f'level_{level}_particle_updates_match']=fx.report()['particle_updates_match']
        results[f'level_{level}_mob_art_requests_match']=art.report()['mob_art_requests_match']
        results[f'level_{level}_shot_requests_match']=shots.report()['shot_requests_match']
        results[f'level_{level}_reinforcements_created_outside_camera']=spawn.report()['all_entries_start_offscreen']
        if level>=5:results[f'level_{level}_enemy_projectiles_drawn']=shots.report()['groups']>10
        if level>=5:results[f'level_{level}_different_enemy_roles']=len(families)>=3
        g.shot(out/f'level-{level}-cleared.png')
        cases.append({'level':level,'generated_level':g.peek('stg',1),'waves':g.peek('stage_wave'),
            'kills':g.peek('kills_run'),'max_alive':max_alive,'families':sorted(families),
            'by_wave':{k:sorted(v) for k,v in by_wave.items()},'wave_kills':wave_kills,
            'vblank_span_for_120_completed_updates':[min(windows),max(windows)] if windows else None,
            'maximum_input_start_scanlines_after_vblank':max(starts) if starts else None,
            'raw_counter_minimum_at_lcd_boundary':min(spans) if spans else None,'hitches':hitches[:30],'late_frames':late[:30],'clock':clock.report(),'hud':hud.report(),'fx':fx.report(),'mob_art':art.report(),'shots':shots.report(),'spawn_view':spawn.report(),'oam_max':meter.max_line,'log':log[:8]})
        # Combat metrics above cover active play only. After real victory, walk
        # to the generated solid bench and open it with A, rather than a menu.
        g.tick=original
        results[f'level_{level}_cleared_waves_unlock_physical_bench']=bench_open(g,log,craft=True) and g.peek('bench_mode')==1
        g.shot(out/f'level-{level}-bench.png')
        g.stop()
    results['later_stages_spawn_more_enemies']=cases[-1]['kills']>cases[0]['kills']
    results['entry_positions_follow_the_moving_camera']=len({tuple(c) for case in cases for c in case['spawn_view']['camera_positions']})>1
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'cases':cases}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
