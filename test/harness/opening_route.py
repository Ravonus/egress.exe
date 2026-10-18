"""Actual generated openings: left floor, player-weight response and exit route.

Only the run seed and enemy/invulnerability debug options are pinned. The map,
player position, circuits and inventory are driven by ordinary controller input.
"""
import argparse,hashlib,json
from collections import deque
from pathlib import Path
from play import Game,start_run,PASSABLE
from world_play import WorldPlayer

FONT=" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,:!?-+/'>x"
def caption(g):
    base=0x9c00 if g.pb.memory[0xff40]&64 else 0x9800
    return ''.join(FONT[t-190] if 190<=t<190+len(FONT) else '?' for t in g.pb.memory[0,base:base+20]).strip()

def left_floor(p):
    start=p.cell();q=deque([start]);seen={start}
    while q:
        x,y=q.popleft()
        for dx,dy in ((-1,0),(1,0),(0,-1),(0,1)):
            n=x+dx,y+dy
            if n in seen or not (0<=n[0]<p.w and 0<=n[1]<p.h):continue
            raw=p.cells[n[1]*p.w+n[0]]
            if raw&31 not in PASSABLE or raw&128:continue
            seen.add(n);q.append(n)
    return min(seen,key=lambda c:(c[0],abs(c[1]-start[1])))

def run(rom,out,tool):
    out.mkdir(parents=True,exist_ok=True);rows=[]
    for seed in (5,0x5102,0x5103,1,0x5104,0x1c,0x69,0x99):
        g=Game(rom);case=out/f'{seed:04x}';case.mkdir(exist_ok=True)
        try:
            assert start_run(g,True,True,seed=seed)
            p=WorldPlayer(g,tool,rom);p.out=case;g.tick(32)
            target=left_floor(p);p.walk(target)
            r={'seed':seed,'size':[p.w,p.h],'nodes':p.nodes,'left_floor':target,
               'left_floor_reached':p.cell()==target,'left_position':p.pos(),'vertical':bool(p.vertical)}
            g.shot(case/'left-floor.png')
            kind,u,v,condition,*_=p.nodes[0];assert kind==0
            p.walk(p.xy(u-3,v-1));g.tick(18)
            gate=p.xy(u,v);r['player_plate_gate']=p.cells[gate[1]*p.w+gate[0]]&31
            r['caption']=caption(g)
            r['weight_response_correct']=r['player_plate_gate']==(11 if condition==3 else 12)
            r['weight_response_explained']=r['caption']==('WEIGHT BOTH PLATES' if condition==3 else 'GATE LATCHED OPEN' if condition==1 else 'GATE OPEN: ADD A BOX')
            g.shot(case/'player-on-plate.png')
            r['input_reaches_next_floor']=p.solve(case)
            report=p.report();r['late_frames']=report['late_completions'];r['oam_over']=report['oam_over'];r['missing_body']=report['missing_body']
            r['pixels_match']=not report['heir_pixel_mismatches'];r['map_writes_match']=report['grid']['all_map_and_attributes_match']
            rows.append(r);print(json.dumps(r),flush=True)
        except BaseException:
            g.shot(case/'failure.png');raise
        finally:g.stop()
    results={'left_floor_is_reachable':all(r['left_floor_reached'] for r in rows),
             'standing_weight_matches_actual_circuit':all(r['weight_response_correct'] for r in rows),
             'switch_response_is_visible_in_native_hud':all(r['weight_response_explained'] for r in rows),
             'all_routes_reach_next_floor':all(r['input_reaches_next_floor'] for r in rows),
             'horizontal_and_vertical_stages_covered':{r['vertical'] for r in rows}=={False,True},
             'all_four_switch_conditions_covered':{r['nodes'][0][3] for r in rows}=={0,1,2,3},
             'native_frame_and_sprite_budgets':all(not r['late_frames'] and not r['oam_over'] and not r['missing_body'] for r in rows),
             'native_pixels_and_map_writes_match':all(r['pixels_match'] and r['map_writes_match'] for r in rows)}
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'cases':rows}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(results,indent=2));return all(results.values())

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('rom',type=Path);ap.add_argument('out',type=Path);ap.add_argument('--tool',type=Path,default=Path('build/host/land_one'))
    a=ap.parse_args();raise SystemExit(0 if run(a.rom,a.out,a.tool) else 1)
