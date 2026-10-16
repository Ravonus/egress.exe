"""Eight-way melee aim, visible crescents and projectile velocity in the native cartridge."""
import argparse,hashlib,json
from pathlib import Path
from stage2 import fresh
from tools_and_weapons import aim_fixture

DIRS=[(0,-1,{'up'}),(1,-1,{'up','right'}),(1,0,{'right'}),(1,1,{'down','right'}),
      (0,1,{'down'}),(-1,1,{'down','left'}),(-1,0,{'left'}),(-1,-1,{'up','left'})]

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};detail=[]
    for d,(dx,dy,buttons) in enumerate(DIRS):
        damages=[];arc=False
        for sign in (1,-1):
            g=fresh(rom,1,0);aim_fixture(g,0,sign*dx*16,sign*dy*16)
            g.hold(buttons);g.tick(2);g.hold(set());g.tick(2);g.press('a',2)
            for _ in range(20):
                g.tick()
                if any(g.pb.memory[0xfe03+k*4]&8 and g.pb.memory[0xfe00+k*4] for k in range(4,12)):arc=True
            damages.append(20-g.peek('en',3))
            if sign==1:g.shot(out/f'melee-{d}.png')
            g.stop()
        r[f'direction_{d}_hits_forward_only']=damages[0]>0 and damages[1]==0
        if d&1:r[f'direction_{d}_uses_diagonal_arc']=arc
        g=fresh(rom,1,0);aim_fixture(g,0,48,0);g.hold(buttons);g.tick(2);g.hold(set());g.tick(2);g.press('b',2);g.tick(7)
        shots=[k for k in range(8) if g.peek('orbs',k*10) and g.peek('orbs',k*10+1)]
        def signed(v):return v if v<128 else v-256
        velocity=(signed(g.peek('orbs',shots[0]*10+8)),signed(g.peek('orbs',shots[0]*10+9))) if shots else None
        r[f'direction_{d}_shoots_along_aim']=bool(velocity) and all((v>0)-(v<0)==w for v,w in zip(velocity,(dx,dy)))
        detail.append({'direction':d,'damage_front_back':damages,'shot_velocity':velocity});g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'directions':detail}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
