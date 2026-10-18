"""Ready floor movement, presentation-independent boundaries and tile-edge melee."""
import argparse,hashlib,json
from pathlib import Path
from stage2 import fresh,teleport

DIRS=[('n',0,-1,{'up'}),('ne',1,-1,{'up','right'}),('e',1,0,{'right'}),
      ('se',1,1,{'down','right'}),('s',0,1,{'down'}),('sw',-1,1,{'down','left'}),
      ('w',-1,0,{'left'}),('nw',-1,-1,{'up','left'})]

def arena(g):
    g.poke('stage_type',0);g.poke('world_on',0);g.poke('pl_inv',0)
    for y in range(14):
        for x in range(16):
            c=y*16+x;g.poke('room',2 if x in (0,15) or y in (0,13) else 0,c)
            g.poke('look_skin',0,c)
    g.poke('door_open',0);g.poke('tutorial_on',0)

def missing_body(g):
    x,y=g.player();bad=[]
    for py in (y-6,y+5):
        for px in (x-6,x+5):
            if not (0<=px<256 and 0<=py<224):continue
            c=(py//16)*16+px//16;mask=1<<(c&7)
            if g.peek('roam_hidden',c>>3)&mask and not g.peek('look_present',c>>3)&mask:bad.append(c)
    return bad

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);results={};detail=[]
    for direction,(name,dx,dy,buttons) in enumerate(DIRS):
        g=fresh(rom,0);g.tick(30);arena(g);teleport(g,120,104)
        g.poke('pl_inv',0);g.poke('heir',1,8);g.poke('pin',1)
        g.poke('pmv',direction,5)
        # The spawn wave has completed. Camera travel keeps floor resident.
        for k in range(28):g.poke('look_present',255,k);g.poke('roam_hidden',0,k)
        misses=[];fc=[];speeds=[];states=set();last=g.player()
        at=g.sym['_frame_wait']
        def completed(_):
            if g.peek('game_mode')==2 and not g.peek('pl_air'):
                bad=missing_body(g)
                if bad:misses.append((g.frames,g.player(),bad))
        g.pb.hook_register(at>>16,at&65535,completed,None)
        # Face first, release, then double tap forward into the fastest blink.
        for held,ticks in ((buttons,2),(set(),2),(buttons,2),(set(),2),(buttons,40),(set(),18)):
            g.hold(held)
            for _ in range(ticks):
                g.tick();pos=g.player();speeds.append(max(abs(pos[0]-last[0]),abs(pos[1]-last[1])));last=pos
                fc.append(g.peek16('frame_count'));states.add(g.peek('pl_state'))
        results[name+'_whole_footprint_is_built']=not misses
        results[name+'_evade_runs']=2 in states
        results[name+'_fast_blink_is_exercised']=max(speeds)>=(4 if dx and dy else 6)
        spans=[(b-a)&65535 for a,b in zip(fc,fc[1:])]
        results[name+'_keeps_one_update_per_display']=min(spans)==max(spans)==1
        g.shot(out/(name+'.png'));detail.append({'direction':name,'missing':misses[:8],'max_step_px':max(speeds),'frame_steps':sorted(set(spans))});g.stop()
        # A held sprint shares PS_ROLL with evades, but has no evade vector.
        # It must prepare the held direction across already assembled floor.
        g=fresh(rom,0);g.tick(30);arena(g);teleport(g,120,104)
        for k in range(28):g.poke('look_present',255,k);g.poke('roam_hidden',0,k)
        start=g.player();misses=[];moves=set()
        def sprint_completed(_):
            if g.peek('game_mode')==2 and not g.peek('pl_air'):
                bad=missing_body(g)
                if bad:misses.append((g.frames,g.player(),bad))
        at=g.sym['_frame_wait'];g.pb.hook_register(at>>16,at&65535,sprint_completed,None)
        g.hold(buttons)
        for _ in range(48):g.tick();moves.add(g.peek('pin',2))
        end=g.player()
        results[name+'_held_sprint_crosses_ready_floor']=all((b-a)*d>40 for a,b,d in zip(start,end,(dx,dy)) if d)
        results[name+'_sprint_whole_footprint_is_built']=not misses
        results[name+'_sprint_without_an_evade_vector']=g.peek('roll_dx')==g.peek('roll_dy')==0 and 2 in moves
        detail[-1]['sprint']={'from':start,'to':end,'missing':misses[:8],'moves':sorted(moves)}
        g.stop()
    # Visible floor with a stale presentation bit used to act like an
    # invisible wall. Only real material collision may reject the left step.
    g=fresh(rom,6);g.tick(30);arena(g);teleport(g,24,104)
    c=6*16;g.poke('room',0,c);g.poke('look_skin',0,c)
    g.poke('look_present',g.peek('look_present',c>>3)&~(1<<(c&7)),c>>3)
    g.poke('roam_hidden',g.peek('roam_hidden',c>>3)|(1<<(c&7)),c>>3)
    g.hold({'left'});g.tick(14)
    results['leftmost_floor_ignores_stale_presentation_flag']=g.player()[0]<16
    detail.append({'left_edge_position':g.player(),'floor':g.room()[c]})
    g.shot(out/'leftmost-floor.png');g.stop()
    # Approach all four faces of drywall, including both off-centre sides.
    # The blade arc should hit the whole tile, independent of its top row.
    for name,dx,dy,buttons in [DIRS[i] for i in (0,2,4,6)]:
        for offset in (-9,0,9):
            g=fresh(rom,0);g.tick(30);arena(g)
            target=6*16+7;g.poke('room',3,target)
            x=120-dx*22+(offset if dy else 0);y=104-dy*22+(offset if dx else 0)
            teleport(g,x,y);g.poke('pl_inv',0);g.poke('heir',0,7);g.poke('pin',0,13)
            g.hold(buttons);g.tick(2);g.hold(set());g.tick(4);g.press('a',2);g.tick(20)
            results[f'{name}_{offset}_wall_hit_uses_tile_area']=bool(g.peek('cracked',target>>3)&(1<<(target&7))) or (g.peek('room',target)&31)==0
            g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'travel':detail}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
