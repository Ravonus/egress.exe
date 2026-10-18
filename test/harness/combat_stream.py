"""First-floor free tiles remain traversable while the original fight is alive.

The two real opening seeds start with floor continuing west of the active
window. Pin only seed/invulnerability; never edit actors, materials or position.
"""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run
from heir import OamMeter

def actors(g):
    ox,oy=g.peek('world_view'),g.peek('world_view',1);rows=[]
    for k in range(g.peek('nen')):
        x=g.peek16_at('en',16*k+8);y=g.peek16_at('en',16*k+10)
        state=g.peek('en',16*k+1);flags=g.peek('mob',19*k+13)
        rows.append({'slot':k,'x':ox*256+(x if x<32768 else x-65536),
                     'y':oy*256+(y if y<32768 else y-65536),'hp':g.peek('en',16*k+3),
                     'state':state,'parked':flags>>5,'flags':flags&31,
                     'timer':g.peek('en',16*k+2),'anim':g.peek('en',16*k+7)})
    return rows

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);rows=[]
    for seed in (0x69,0x99):
        g=Game(rom);assert start_run(g,True,False,seed=seed);meter=OamMeter(g)
        initial=[g.peek('world_view'),g.peek('world_view',1)];alive=g.peek('enemies_alive');initial_actors=actors(g)
        assert initial[0]>0 and alive
        epochs=[];late=[];positions=[];seams=[];parked={};resumed=set();park_errors=[];images=[]
        def completed(_):
            if g.peek('game_mode')!=2:return
            ep=g.peek('frame_epoch');epochs.append(ep if not epochs else epochs[-1]+((ep-epochs[-1])&255))
            if g.peek('video_epoch')!=ep:late.append(g.frames)
        at=g.sym['_frame_wait'];g.pb.hook_register(at>>16,at&65535,completed,None)
        old_seams=0
        for direction,duration in (('left',96),('right',192),('left',192),('right',192)):
            g.hold({direction})
            for _ in range(duration):
                g.tick();view=[g.peek('world_view'),g.peek('world_view',1)]
                positions.append([view[0]*16+g.peek('pl_x'),view[1]*16+g.peek('pl_y'),g.peek('pl_state')])
                count=g.peek16('world_seams')
                if count!=old_seams:seams.append({'frame':g.frames,'view':view,'alive':g.peek('enemies_alive'),'actors':actors(g)});old_seams=count
                for a in actors(g):
                    k=a['slot']
                    if a['parked']:
                        if k in parked and parked[k]!=a:park_errors.append([parked[k],a])
                        parked[k]=a.copy()
                    elif k in parked:
                        resumed.add(k);parked.pop(k)
                if len(positions)%3==0:images.append(g.pb.screen.image.convert('RGB'))
            g.hold(set());g.tick(18)
        spans=[epochs[i]-epochs[i-120] for i in range(120,len(epochs))]
        r={'seed':seed,'initial_view':initial,'original_enemies':alive,'initial_actors':initial_actors,
           'minimum_global_x':min(p[0] for p in positions),'previous_invisible_boundary_x':initial[0]*16+24,
           'crossed_free_floor_while_fight_alive':any(s['view'][0]<initial[0] and s['alive'] for s in seams),
           'no_fight_clear_required':g.peek('kills_run')==0 and g.peek('enemies_alive')==alive,
           'parked_actor_state_frozen':not park_errors,'resumed_slots':sorted(resumed),
           'seams':seams,'late_frames':late,'oam_over':meter.over,
           'timing':[min(spans),max(spans)] if spans else [],'final_actors':actors(g)}
        g.shot(out/f'{seed:04x}-returned.png')
        if images:images[0].save(out/f'{seed:04x}-combat-travel.gif',save_all=True,append_images=images[1:],duration=50,loop=0)
        rows.append(r);print(json.dumps(r),flush=True);g.stop()
    results={'free_western_floor_has_no_combat_wall':all(r['crossed_free_floor_while_fight_alive'] and r['minimum_global_x']<r['previous_invisible_boundary_x'] for r in rows),
             'original_enemies_survive_travel':all(r['no_fight_clear_required'] for r in rows),
             'inactive_enemies_freeze_and_resume':all(r['parked_actor_state_frozen'] and r['resumed_slots'] for r in rows),
             'native_frame_and_oam_budgets':all(not r['late_frames'] and not r['oam_over'] and r['timing']==[120,120] for r in rows)}
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'cases':rows}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(results,indent=2));return all(results.values())
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('rom',type=Path);ap.add_argument('out',type=Path);a=ap.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
