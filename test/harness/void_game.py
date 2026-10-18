"""Native input tests for falling and gap-crossing; isolated board edits are explicit fixtures."""
import argparse,hashlib,json
from pathlib import Path
from PIL import Image
from play import Game,start_run
from stage2 import teleport

def fixture(rom,width=1,god=False,audio=False):
    g=Game(rom,audio=audio);assert start_run(g,god,True,floor=2)
    g.poke('stage_type',0)
    for y in range(3,12):
        for x in range(2,14):g.pb.memory[g.addr('room')+16*y+x]=0
    for y in range(3,12):
        for x in range(7,7+width):g.pb.memory[g.addr('room')+16*y+x]=9
    teleport(g,98,136);g.hold(set());g.tick(20)
    g.poke('heir',0,8);g.poke('pin',0,0) # inherited roll profile; actual resolver still handles every input
    g.hold({'right'});g.tick(2);g.hold(set());g.tick(22);teleport(g,98,136);g.tick(2)
    return g

def jump(g,frames=18):
    # Aim along the crossing, release, then a deliberate second tap within the resolver window.
    g.hold({'right'});g.tick(2);g.hold(set());g.tick(2);g.hold({'right'})
    trace=[]
    for _ in range(frames):
        g.tick();trace.append((g.peek('pl_x'),g.peek('pl_y'),g.peek('pl_state'),g.peek('pl_air'),g.peek('pl_z'),g.peek('pl_hp')))
        if g.peek('pl_state')==6:g.hold(set())
    g.hold(set());g.tick(3);return trace

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};details={}
    g=fixture(rom);hp=g.peek('pl_hp');g.hold({'right'});states=[];positions=[];frames=[];heights=[]
    for i in range(70):
        g.tick();states.append(g.peek('pl_state'));positions.append(g.player());heights.append(g.peek('pl_z'))
        if g.peek('pl_state')==6:g.hold(set())
        if i%2==0:frames.append(g.pb.screen.image.convert('RGB'))
    g.hold(set());r['walking_off_platform_starts_a_visible_fall']=6 in states and any(z>128 for z in heights)
    r['fall_charges_exactly_one_quarter_heart']=g.peek('pl_hp')==hp and g.peek('pl_quarter')==1
    r['fall_returns_to_last_firm_floor']=g.player()[0]<112 and g.peek('pl_state')!=6
    details['walk_fall']={'states':sorted(set(states)),'halfHp':[hp,g.peek('pl_hp')],'missingQuarter':g.peek('pl_quarter'),'end':g.player()}
    g.shot(out/'three-quarter-heart-after-fall.png')
    base=0x9c00 if g.pb.memory[0xff40]&0x40 else 0x9800
    at=base+(64 if g.pb.memory[0xff4a]==120 else 32)+(g.peek('pl_hp')*2-g.peek('pl_quarter'))//4
    r['hud_uses_a_partial_heart_tile']=g.pb.memory[0,at]==239
    frames[0].save(out/'fall.gif',save_all=True,append_images=frames[1:],duration=34,loop=0);g.stop()
    g=fixture(rom);hp=g.peek('pl_hp');t=jump(g)
    r['forward_evade_has_airborne_arc']=any(x[3] and x[4] for x in t)
    r['roll_crosses_one_tile_hole']=g.peek('pl_x')>=128 and g.peek('pl_hp')==hp and all(x[2]!=6 for x in t)
    details['roll_jump']=t;g.shot(out/'roll-landing.png');g.stop()
    g=fixture(rom,width=3);hp=g.peek('pl_hp');t=jump(g,55)
    r['jump_too_short_falls_instead_of_floating']=any(x[2]==6 for x in t) and g.peek('pl_hp')==hp and g.peek('pl_quarter')==1
    g.stop()
    g=fixture(rom);g.poke('pl_inv',200);hp=g.peek('pl_hp');g.hold({'right'});g.tick(50)
    r['invulnerability_does_not_remove_void_cost']=g.peek('pl_hp')==hp and g.peek('pl_quarter')==1;g.stop()
    # Exterior abyss is the same bounded cost, including the last quarter.
    for initial_hp,initial_q in ((6,0),(6,1),(1,0),(1,1)):
        g=fixture(rom);g.poke('pl_hp',initial_hp);g.poke('pl_quarter',initial_q)
        for y in range(3,12):g.poke('room',41,16*y+7)
        before=initial_hp*2-initial_q;g.hold({'right'})
        seen=False
        for _ in range(85):
            g.tick()
            if g.peek('pl_state')==6:seen=True;g.hold(set())
        after=g.peek('pl_hp')*2-g.peek('pl_quarter')
        r[f'abyss_{initial_hp}_{initial_q}_bounded_quarter_and_safe_return']=seen and after==max(1,before-1) and g.peek('game_mode')==2 and g.peek('pl_state')!=6 and g.player()[0]<112
        g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'fixture':'isolated pit columns in a native room; actual directional input and resolver','results':r,'details':details}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
