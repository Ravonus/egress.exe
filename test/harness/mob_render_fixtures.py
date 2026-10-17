"""Exercise native body/overlay states and clipping with explicit entity fixtures.

These are renderer fixtures, not generated combat encounters. CPU execution,
sprite allocation and VRAM remain native; only entity input records are set.
"""
import argparse,hashlib,itertools,json
from pathlib import Path
from stage2 import fresh
from mob_render import MobRender
from oam_observer import OamObserver
from shot_render import ShotRender

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);g=fresh(rom,0,family=0)
    g.poke('stage_type',0);g.poke('tutorial_on',0);g.poke('mob_has_elite',1)
    for c in range(224):g.poke('room',2 if c%16 in (0,15) or c//16 in (0,13) else 0,c)
    g.pb.memory[1,0xddd0:0xddf0]=[0xa7]*32
    positions=[(-20,56),(-15,56),(-8,56),(-7,56),(-1,56),(0,56),(144,56),(145,56),(151,56),(152,56),(159,56),(160,56),(80,-20),(80,-15),(80,-1),(80,0),(80,143),(80,144)]
    cases=list(itertools.product(range(16),range(4),(0,1,2,3,4,5,7,8),(0,8,16,24,56),(0,8,64)))
    done=0;coverage=set()
    def safe_orb(g):
        for i in range(80):g.poke('orbs',0,i)
        for i,v in enumerate((1,0,255,0,128,1,128,1,0,0)):g.poke('orbs',v,i)
    safe_orb(g)
    at=g.sym['_frame_wait']
    g.pb.hook_register(at>>16,at&65535,lambda _:safe_orb(g),None)
    def prepare(g,k):
        nonlocal done
        if done==len(cases):return
        mask,face,state,anim,status=cases[done];i=done;done+=1
        sx,sy=positions[(mask*5+face*7+state*11+anim*13+status*17)%len(positions)]
        g.poke('cam_x',64);g.poke('cam_y',48)
        x=(sx+80)<<4;y=(sy+64)<<4;slot=(i>>2)%3
        for off,v in enumerate((3,state,i&7,200,(i>>3)&3,face,0,anim,x&255,x>>8,y&255,y>>8,0,0,0,0)):
            g.poke('en',v,k*16+off)
        g.poke('mob',(i>>4)&1,k*19);g.poke('mob',slot,k*19+9)
        g.poke('mob',status,k*19+10);g.poke('mob',(i>>1)&1,k*19+13)
        g.poke('mob_slot_bmask',mask,slot);coverage.add((mask,face,state,anim,status))
    def prepare_shots(g):
        g.poke('cam_x',64);g.poke('cam_y',48)
        values=(0,16,63,1023,2048,4080,65535,4095)
        for k in range(8):
            x=values[(done+k)%8];y=values[(done+k*3)%8]
            for j,v in enumerate((int(k!=7 and done%17!=0),k%3,255,0,x&255,x>>8,y&255,y>>8,0,0)):g.poke('orbs',v,k*10+j)
    alloc=OamObserver(g,register=False);shots=ShotRender(g,prepare_shots)
    def group(_):alloc.enter(None);shots.group(None)
    art=MobRender(g,prepare_body=prepare,observe_group=group)
    for _ in range(len(cases)*3+120):
        if done==len(cases):break
        g.tick()
    g.tick(2)
    a=art.report();b=alloc.report();s=shots.report()
    results={'all_native_fixtures_exercised':done==len(cases) and len(coverage)==len(cases),
             'all_body_marker_overlay_requests_match':a['mob_art_requests_match'],
             'all_allocations_match_portable_rules':b['all_allocations_match'],
             'all_column_masks_and_flips_exercised':set(a['column_masks'])=={0,1,2,3} and a['flips']==[False,True],
             'projectile_requests_match_full_coordinate_range':s['shot_requests_match'] and s['maximum_pixel_coordinate']==4095 and s['groups']>1000,
             'stack_canary':bytes(g.pb.memory[1,0xddd0:0xddf0])==bytes([0xa7]*32)}
    g.stop();report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'source':__doc__,'fixtures':done,'results':results,'art':a,'shots':s,'allocator':b}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
