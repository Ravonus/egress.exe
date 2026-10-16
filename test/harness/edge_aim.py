"""Native input attacks at the actual coordinate limits, with explicit stationary-target fixtures.
A weapon reaching outside the map must still hit its near target and never wrap onto a distant target.
"""
import argparse, hashlib, json
from pathlib import Path
from stage2 import fresh, teleport
from tools_and_weapons import aim_fixture

CASES=[('west',8,136,{'left'},(4,136),(248,136)),
       ('east',248,136,{'right'},(252,136),(8,136)),
       ('north',120,8,{'up'},(120,4),(120,216)),
       ('south',120,216,{'down'},(120,220),(120,8))]

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);results={};detail=[]
    for name,px,py,buttons,near,far in CASES:
        damage=[]
        for label,(ex,ey) in [('near',near),('far',far)]:
            g=fresh(rom,1,0);aim_fixture(g,2,48,0)
            # Explicit ground fixture keeps the combat check separate from the real-edge falling test.
            for i in range(224):g.poke('room',0,i)
            teleport(g,px,py);g.poke('mods',1)
            e=g.addr('en');m=g.pb.memory
            for off,val in [(8,ex*16),(10,ey*16)]:m[e+off]=val&255;m[e+off+1]=val>>8
            m[e+14]=ex;m[e+15]=ey
            g.hold(buttons);g.tick(2);g.hold(set());g.tick(2);g.press('a',2);g.tick(20)
            damage.append(20-g.peek('en',3))
            g.shot(out/f'{name}-{label}.png');g.stop()
        results[f'{name}_hits_local_target']=damage[0]>0
        results[f'{name}_does_not_wrap']=damage[1]==0
        detail.append({'edge':name,'near_far_damage':damage})
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':results,'detail':detail}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
