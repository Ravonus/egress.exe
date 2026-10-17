"""Input-driven rebuild acceptance. First three floors use normal progression; separate fixtures check save failure.
God mode only removes health loss during the combat route. No player/room warps or forced wave kills are used.
"""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import subprocess
from play import Game, start_run, step, solve_here, bench_open, bench_cursor, bench_act, pocket, MODE_TITLE, PASSABLE


def route(g, target):
    cells=g.room()[:224]; x,y=g.player(); start=(y//16)*16+x//16
    q=deque([start]); prev={start:None}; dirs={}
    while q:
        c=q.popleft()
        for d,delta in [('U',-16),('D',16),('L',-1),('R',1)]:
            n=c+delta
            if n<0 or n>=224 or n in prev or (cells[n]&31 not in PASSABLE and not (cells[n]&31==7 and g.peek('door_open'))): continue
            prev[n]=c;dirs[n]=d;q.append(n)
    if target not in prev:
        target=min(prev,key=lambda c:abs(c%16-target%16)+abs(c//16-target//16))
    out=[]
    while prev[target] is not None:
        out.append(dirs[target]);target=prev[target]
    return out[::-1]


def walk(g,target,log,limit=999):
    for d in route(g,target)[:limit]:
        if not step(g,d,log): return False
    return True


def run(rom,out,tool):
    out.mkdir(parents=True,exist_ok=True)
    g=Game(rom); log=[]; result={}; report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':result,'log':log}
    assert start_run(g,True,False)
    dump=subprocess.check_output([str(tool),'phys-dump',str(g.peek16('run_seed')^0x9e37^0x51a7),'1',str(g.peek('slice',19))],text=True)
    result['first_room_matches_shared_generator']=bytes(g.room()[:224])==bytes.fromhex(dump.split()[0])
    result['first_room_is_push']=g.peek('slice',22)==1
    g.shot(out/'01-push.png')
    for d in 'URDL': assert step(g,d,log)
    result['pickup_units']=pocket(g)==[4,2]
    g.hold({'select'});g.tick(62);g.hold(set());g.tick(180)
    result['reset_restores_entry_bag_and_pickups']=pocket(g)==[] and g.room()[178]==17 and g.room()[179]==17
    for d in 'URDL': assert step(g,d,log)
    assert bench_open(g,log); bench_cursor(g,0);g.shot(out/'02-bench.png')
    g.press('start');g.tick(35)
    result['invalid_target_keeps_units']=pocket(g)==[4,2]
    assert bench_act(g,'Cdb',log)
    result['finite_recipe_consumption']=pocket(g)==[8]
    moves=solve_here(g,tool,log);g.tick(240)
    result['normal_floor2_wave_stage']=g.peek('floor_no')==2 and g.peek('stage_type')==1
    result['inventory_carried_into_combat']=pocket(g)==[8]
    # Staying anywhere, including the old ring, cannot complete a wave.
    g.tick(600)
    result['waves_require_fighting']=g.peek('stage_pct')<100 and not g.peek('stage_won') and g.peek('enemies_alive')>0
    g.shot(out/'03-waves.png')
    for _ in range(240):
        if g.peek('stage_won'):break
        live=[k for k in range(g.peek('nen')) if g.peek('en',k*16+1)!=6]
        if not live:g.tick(30);continue
        x,y=g.player()
        k=min(live,key=lambda k:abs(g.peek16_at('en',k*16+8)//16-x)+abs(g.peek16_at('en',k*16+10)//16-y))
        ex=g.peek16_at('en',k*16+8)//16;ey=g.peek16_at('en',k*16+10)//16
        walk(g,(ey//16)*16+ex//16,log,4)
        x,y=g.player();ex=g.peek16_at('en',k*16+8)//16;ey=g.peek16_at('en',k*16+10)//16
        dx,dy=ex-x,ey-y;aim=set()
        if abs(dx)*2>=abs(dy):aim.add('right' if dx>0 else 'left')
        if abs(dy)*2>=abs(dx):aim.add('down' if dy>0 else 'up')
        g.hold(aim);g.tick(1)
        g.hold({'a'});g.tick(38);g.hold(set());g.tick(40)
    result['wave_victory_by_player_attacks']=bool(g.peek('stage_won')) and g.peek('kills_run')>0
    report['wave_kills']=g.peek('kills_run');report['wave_count']=g.peek('stage_wave')
    if g.peek('stage_won'):
        walk(g,g.room()[225],log);g.tick(240)
        result['normal_floor3_falling_puzzle']=g.peek('floor_no')==3 and g.peek('slice',22)==2
        g.shot(out/'04-falling.png')
        report['drop_plan']=solve_here(g,tool,log)
        g.tick(240)
        result['drop_solved_by_input']=g.peek('floor_no')==3 and g.peek('game_mode')!=2 or g.peek('floor_no')>=4
    result['family_choice_saved']=g.peek('family',283)==1
    selected=bytes(g.peek('family',k) for k in range(19))
    saved=bytearray(g.stop())
    # This fixture isolates heir persistence; floor/landing continuation is covered by resume.py.
    saved[0x40]=saved[0x140]=0
    saved=bytes(saved)
    g=Game(rom,sram=saved);assert start_run(g,True,False)
    result['heir_choice_survives_poweroff']=bytes(g.peek('family',k) for k in range(19))==selected and g.peek('family',283)==1
    solve_here(g,tool,log);g.tick(240)
    g.poke('dbg_god',0)
    # Approach actual enemies instead of assuming this input route leaves the
    # heir somewhere they can reach. Rendering timing can change that final
    # position; standing safely behind room geometry is not a death test.
    death_hunt_steps=0
    for _ in range(1600):
        if g.peek('game_mode')==4:break
        live=[k for k in range(g.peek('nen')) if g.peek('en',k*16+1)!=6]
        x,y=g.player()
        if live:
            k=min(live,key=lambda k:abs(g.peek16_at('en',k*16+8)//16-x)+abs(g.peek16_at('en',k*16+10)//16-y))
            ex=g.peek16_at('en',k*16+8)//16;ey=g.peek16_at('en',k*16+10)//16
            if max(abs(ex-x),abs(ey-y))>18:
                walk(g,(ey//16)*16+ex//16,log,2);death_hunt_steps+=1
        g.hold(set());g.tick(10)
    report['death_hunt_steps']=death_hunt_steps
    result['death_creates_actual_parent_record']=g.peek('game_mode')==4 and g.peek16_at('family',279)==2 and bytes(g.peek('family',19+k) for k in range(19))==selected
    assert result['death_creates_actual_parent_record'],'real enemy damage must create the next generation before testing its picker'
    dead_save=g.stop()
    g=Game(rom,sram=dead_save);assert g.wait_mode(MODE_TITLE)
    g.tick(40)
    for _ in range(100):
        g.press('start');g.tick(10)
        if g.peek('game_mode')==6:break
    g.tick(40);g.shot(out/'05-next-generation.png')
    result['three_related_heirs_after_death']=g.peek('game_mode')==6 and g.peek16_at('family',279)==2 and g.peek('family',284)==1
    looks=[]
    for choice in range(3):
        looks.append(bytes(g.peek('family',38+choice*19+k) for k in range(7)))
        g.shot(out/f'heir-{choice+1}.png');g.press('right');g.tick(25)
    result['siblings_visibly_distinct']=len(set(looks))==3
    from picker_collision import wait_ready
    g.press('a');wait_ready(g);g.press('a');assert g.wait_mode(2)
    result['legacy_meta_preserved']=g.peek('meta_loaded')==1 and g.peek16('meta')==1
    g.stop()
    # Tear the newest extension; CRC journal must retain the preceding selected heir and legacy meta.
    damaged=bytearray(dead_save)
    newest=max((0x570,0xa6c),key=lambda at:damaged[at+3]);damaged[newest+20]^=0x80
    g=Game(rom,sram=bytes(damaged));assert start_run(g,True,False)
    result['torn_family_save_falls_back']=g.peek16_at('family',279)==1 and bytes(g.peek('family',k) for k in range(19))==selected and g.peek16('meta')==1
    g.stop()
    for answer in ('crt','mud'):
        g=Game(rom);assert start_run(g,True,True,floor=7)
        seed=g.peek16('run_seed')^((7*0x9e37)&65535)^0x51a7
        moves_alt=subprocess.check_output([str(tool),'phys-solve',str(seed or 1),'3',str(g.peek('slice',19)),answer],text=True).strip()
        from play import drive_plan
        drive_plan(g,moves_alt,log);g.tick(240)
        result[f'bridge_{answer}_route_by_input']=g.peek('floor_no')==8
        if answer=='mud':result['bridge_consumes_crafted_unit']=8 not in pocket(g) and g.peek('last_rx') in (0,21)
        report[f'bridge_{answer}_plan']=moves_alt
        g.shot(out/f'06-bridge-{answer}.png');g.stop()
    report['push_plan']=moves
    report['log']=log
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return all(result.values()) and not log


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('rom',type=Path);ap.add_argument('out',type=Path)
    ap.add_argument('--tool',type=Path,default=Path('build/host/egtool'))
    args=ap.parse_args();raise SystemExit(0 if run(args.rom,args.out,args.tool) else 1)
