"""Ordinary generated openings, real fights, and fresh maps after real deaths.

Seed sweeps pin only the run seed. Death/New Run checks use the title's real
entropy path. No map, inventory, actor-health or wave-result writes are used.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
from play import Game,start_run,MODE_TITLE
from gameplay_rebuild import walk
from world_play import WorldPlayer
from animation_progress import AnimationProgress
from terrain_rim import TerrainRim

def fingerprint(g):
    plan=bytes(g.peek('world_plan',k) for k in range(83))
    assert g.peek('world_on') and plan[6]==0
    cells=subprocess.check_output(['build/host/land_one',str(int.from_bytes(plan[:2],'little')),str(plan[6]),str(plan[4]),str(plan[5])])
    assert cells[:83]==plan
    outline=bytes(int(t&31!=9) for t in cells[83::4]);w=plan[2]
    ground=[i for i,t in enumerate(outline) if t]
    bounds=(max(i%w for i in ground)-min(i%w for i in ground)+1)*(max(i//w for i in ground)-min(i//w for i in ground)+1)
    return {'run_seed':g.peek16('run_seed'),'plan_sha256':hashlib.sha256(plan).hexdigest(),
            'material_layout_sha256':hashlib.sha256(cells[83::4]).hexdigest(),
            'playable_outline_sha256':hashlib.sha256(outline).hexdigest(),'irregular_playable_outline':sum(outline)<bounds,
            'native_entry_sha256':hashlib.sha256(bytes(g.room()[:224])).hexdigest(),
            'size':list(plan[2:4]),'profile':plan[4],'shape':plan[7],
            'conditions':[plan[14+6*k] for k in range(plan[9])],
            'spawned_enemies':g.peek('enemies_alive')}

def closest(g):
    live=[k for k in range(g.peek('nen')) if g.peek('en',16*k+1)!=6]
    if not live:return None
    x,y=g.player()
    return min(live,key=lambda k:abs(g.peek16_at('en',16*k+8)//16-x)+abs(g.peek16_at('en',16*k+10)//16-y))

def fight(g,log,fatal=False):
    for turn in range(320 if fatal else 200):
        if fatal and g.peek('game_mode')==4:return True
        if not fatal and not g.peek('enemies_alive'):g.hold(set());g.tick(16);return True
        k=closest(g)
        if k is None:g.hold(set());g.tick(16);continue
        ex=g.peek16_at('en',16*k+8)//16;ey=g.peek16_at('en',16*k+10)//16
        x,y=g.player()
        if max(abs(ex-x),abs(ey-y))>18:walk(g,(ey//16)*16+ex//16,log,2)
        x,y=g.player();dx,dy=ex-x,ey-y
        aim=set()
        if abs(dx)*2>=abs(dy):aim.add('right' if dx>0 else 'left')
        if abs(dy)*2>=abs(dx):aim.add('down' if dy>0 else 'up')
        # A retreating flyer can hover above a void. Give it room to approach
        # again instead of firing forever at an unreachable cardinal angle.
        if not fatal and turn%16==15 and max(abs(dx),abs(dy))>24:
            walk(g,g.room()[224],log,6);g.hold(set());g.tick(60);continue
        g.hold(aim);g.tick(1);g.hold(set());g.tick(3)
        if fatal:g.tick(16);continue
        g.hold(aim|{'b'});g.tick(2);g.hold(set());g.tick(12)
        g.hold(aim|{'a'});g.tick(35);g.hold(set());g.tick(22)
    return False

def abandon(g):
    from menu import lobby
    g.poke('dbg_god',1);lobby(g)
    assert g.peek('menu_active') and g.peek('resume_ready') and not g.peek('menu_cursor')
    g.press('down',2);g.tick(8);assert g.peek('menu_cursor')==1
    g.press('a',2);g.tick(10);g.press('a',2)
    for _ in range(300):
        if g.peek('game_mode')==2:return
        if g.peek('game_mode')==6 and g.peek('family_ready'):g.press('a',2)
        g.tick(8)
    raise AssertionError('New Run did not enter the new generated opening')

def run(rom,out,tool):
    out.mkdir(parents=True,exist_ok=True);res={};sweep=[]
    for seed in range(0x5100,0x5118):
        g=Game(rom);assert start_run(g,True,True,seed=seed)
        row=fingerprint(g);sweep.append(row)
        if seed<0x5108:g.shot(out/f'opening-{seed:04x}.png')
        g.stop()
    res['all_pinned_seeds_construct_different_openings']=len({r['plan_sha256'] for r in sweep})==len(sweep)
    res['actual_complete_material_layouts_differ']=len({r['material_layout_sha256'] for r in sweep})==len(sweep)
    res['actual_floor_and_void_outlines_vary']=len({r['playable_outline_sha256'] for r in sweep})>=20
    res['nonrectangular_playable_stages_are_common']=sum(r['irregular_playable_outline'] for r in sweep)>=20
    res['all_opening_profiles_are_used']={r['profile'] for r in sweep}=={0,1,2}
    res['opening_dimensions_vary']=len({tuple(r['size']) for r in sweep})>=6
    res['all_geography_shapes_are_used']={r['shape'] for r in sweep}=={0,1,2,3}
    res['switch_conditions_vary']=len({c for r in sweep for c in r['conditions']})>=3

    # The actual first floor includes native matrix actors, then its puzzle.
    g=Game(rom);assert start_run(g,True,False,seed=0x5100);entry=fingerprint(g);log=[]
    res['ordinary_opening_has_real_enemies']=1<=entry['spawned_enemies']<=2
    animation=AnimationProgress(g);late=[];epochs=[]
    def completed(_):
        if g.peek('game_mode')!=2:return
        ep=g.peek('frame_epoch');epochs.append(ep if not epochs else epochs[-1]+((ep-epochs[-1])&255))
        if g.peek('video_epoch')!=ep:late.append(g.frames)
    at=g.sym['_frame_wait'];g.pb.hook_register(at>>16,at&65535,completed,None)
    tick=g.tick
    def record(n=1):
        for _ in range(n):tick();animation.observe()
    g.tick=record
    res['opening_enemies_defeated_by_normal_attacks']=fight(g,log) and g.peek('kills_run')>=entry['spawned_enemies']
    windows=[epochs[i]-epochs[i-120] for i in range(120,len(epochs))]
    res['generated_world_combat_meets_frame_deadlines']=bool(windows) and min(windows)==max(windows)==120 and not late
    ar=animation.report();res['generated_world_combat_animation_advances']=ar['animation_keeps_advancing']
    g.pb.hook_deregister(at>>16,at&65535);g.tick=tick;g.shot(out/'opening-fight-cleared.png')
    (out/'opening-fight.json').write_text(json.dumps({'results':res,'animation':ar,'late':late,'epochs':([min(windows),max(windows)] if windows else [])},indent=2)+'\n')
    print('opening fight',res,flush=True)
    for name in ('_heir_draw','_heir_compose','_heir_cache_get','_heir_cache_put'):
        at=g.sym[name];g.pb.hook_deregister(at>>16,at&65535)
    p=WorldPlayer(g,tool,rom);p.out=out
    rim=TerrainRim(g,out);images=[];travel_tick=g.tick
    def travel(n=1):
        for _ in range(n):
            travel_tick();rim.observe()
            if g.frames%4==0:images.append(g.pb.screen.image.convert('RGB'))
    g.tick=travel
    res['ordinary_opening_puzzle_solved_by_input']=p.solve(out)
    g.tick=travel_tick;p.monitor=False;g.tick(240)
    rr=rim.report();res['ordinary_world_builds_ahead_and_falls_behind']=rr['build_and_fall_are_visible']
    res['ordinary_world_keeps_effects_at_the_edge']=rr['central_view_is_built'] and rr['construction_stays_at_rim']
    if images:images[0].save(out/'ordinary-world-travel.gif',save_all=True,append_images=images[1:],duration=68,loop=0)
    res['ordinary_progression_reaches_survival_stage']=g.peek('floor_no')==2 and g.peek('stage_type')==1
    arena=bytes(g.room()[:224]);res['survival_arena_has_generated_void_edges']=any((arena[y*16+x]&31)==9 for y in range(1,13) for x in range(1,15))
    g.shot(out/'next-survival-arena.png')
    for _ in range(120):
        if g.peek('stage_won'):break
        if g.peek('enemies_alive'):assert fight(g,log)
        else:g.hold(set());g.tick(30)
    res['ordinary_survival_requires_and_accepts_player_kills']=bool(g.peek('stage_won')) and g.peek('kills_run')>entry['spawned_enemies']
    assert walk(g,g.room()[225],log);g.tick(240)
    res['next_floor_deals_a_different_puzzle_family']=g.peek('floor_no')==3 and g.peek('world_on') and g.peek('world_plan',6) in (2,3)
    g.shot(out/'next-procedural-puzzle.png');g.stop()

    # Real mortality advances the family. Every next run uses fresh entropy.
    deaths=[];save=None
    for generation in range(3):
        g=Game(rom,sram=save);assert start_run(g,False,False,seed=None)
        row=fingerprint(g);row['generation']=g.peek16_at('family',279);deaths.append(row)
        g.shot(out/f'generation-{generation+1}.png')
        if generation<2:
            assert fight(g,[],fatal=True),'real opening enemies must kill the mortal heir'
            assert g.peek('game_mode')==4
        save=g.stop()
    res['real_deaths_advance_family']=[r['generation'] for r in deaths]==[1,2,3]
    res['death_restarts_roll_new_run_seeds']=len({r['run_seed'] for r in deaths})==3
    res['death_restarts_construct_new_maps']=len({r['material_layout_sha256'] for r in deaths})==3

    # Continue retains this run; explicitly abandoning it must roll a new one.
    g=Game(rom,sram=save);assert start_run(g,True,False,seed=None);continued=fingerprint(g);checkpoint=g.stop()
    res['continue_keeps_generated_plan']=continued['plan_sha256']==deaths[-1]['plan_sha256'] and continued['run_seed']==deaths[-1]['run_seed']
    g=Game(rom,sram=checkpoint);abandon(g);after=fingerprint(g);g.stop()
    res['new_run_cannot_restore_the_abandoned_seed']=after['run_seed']!=continued['run_seed'] and after['material_layout_sha256']!=continued['material_layout_sha256']
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':res,'seeded_openings':sweep,
            'ordinary_entry':entry,'ordinary_world_effects':rr,'combat_animation':ar,'combat_late_completions':late,'combat_epoch_span':([min(windows),max(windows)] if windows else []),
            'real_death_generations':deaths,'continued':continued,'new_run_after_abandon':after,'log':log}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
    return all(res.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);p.add_argument('--tool',type=Path,default=Path('build/host/land_one'))
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out,a.tool) else 1)
