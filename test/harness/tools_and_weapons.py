"""Native fixtures for tools, five weapon hit boxes, seven mob families and 16 item idle cards.
Fixture positioning/inventory is explicit; every activation/attack uses normal controller input.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from PIL import Image
from play import Game,start_run,bench_act,bench_open,bench_cursor,pocket
from stage2 import fresh,teleport


def aim_fixture(g,weapon=0,dx=16,dy=0):
    g.poke('stage_type',0)
    e=g.addr('en');m=g.pb.memory
    for y in range(5,12):
        for x in range(4,13):m[g.addr('room')+y*16+x]=0
    teleport(g,120,136)
    m[e+1]=0;m[e+3]=20;m[e+6]=255
    x,y=120+dx,136+dy
    for off,val in [(8,x*16),(10,y*16)]:m[e+off]=val&255;m[e+off+1]=val>>8
    m[e+12]=m[e+13]=0;m[e+14]=x;m[e+15]=y
    g.poke('mob',32,10);g.poke('mob',240,11)
    g.poke('heir',weapon,7);g.poke('pin',weapon,13)
    g.hold({'right'});g.tick(1);g.hold(set())


def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);res={};details={};log=[]
    for p,name,bits in [(2,'water_extinguishes',1),(9,'wet_cold_freezes',8),(12,'wet_shock_stuns',32),
                        (8,'mud_slows',4),(13,'steam_blinds',128),(15,'dust_blinds',128)]:
        g=fresh(rom,1,0);aim_fixture(g)
        g.poke('pocket',p);g.poke('pocket_n',1)
        g.poke('mob',16 if p==2 else (1 if p in (9,12) else 0),10)
        g.poke('mob',240,11)
        act='P'+chr(ord('a')+p-1)+'R'
        used=bench_act(g,act,log)
        status=g.peek('mob',10)
        res[name]=used and bool(status&bits) and (p!=2 or not status&16)
        details[name]={'status':status,'hp':g.peek('en',3),'bag':pocket(g)}
        g.shot(out/f'{name}.png');g.stop()
    # All five profiles: the same stationary target and tap, then enough frames for recovery.
    profiles=[]
    for w in range(5):
        row=[]
        for dx,dy in [(32,0),(20,12),(12,0)]:
            g=fresh(rom,1,0);aim_fixture(g,w,dx,dy)
            g.press('a',2);g.tick(32)
            row.append(20-g.peek('en',3));g.stop()
        profiles.append(row)
    details['weapon_damage_far_wide_close']=profiles
    res['whip_and_launcher_reach_farther']=profiles[2][0]>0 and profiles[4][0]>0 and profiles[0][0]==0 and profiles[3][0]==0
    res['hammer_has_force']=profiles[1][2]>profiles[0][2]>0
    res['long_weapons_have_narrower_arc']=profiles[0][1]>0 and profiles[2][1]==0 and profiles[4][1]==0
    g=Game(rom);assert start_run(g,True,True,physical_fixture=1)
    g.poke('pocket',5);g.poke('pocket',2,1);g.poke('pocket_n',2)
    assert bench_open(g,log,craft=True)
    bench_cursor(g,0);g.press('a',4);g.tick(12)
    bench_cursor(g,1);g.press('a',4);g.tick(12);g.press('a',4);g.tick(12)
    res['invalid_recipe_keeps_both_units']=pocket(g)==[5,2]
    g.press('b',4);g.press('b',4);g.press('b',4);g.tick(20)
    for k in range(24):g.poke('pocket',5,k)
    g.poke('pocket_n',24)
    assert bench_open(g,log);g.hold({'right'});g.tick(45)
    res['navigation_never_discards_materials']=g.peek('pocket_n')==24
    g.hold(set());g.tick(4);g.press('a',4);g.tick(12)
    res['discard_requires_confirmation']=g.peek('pocket_n')==24 and g.peek('bench_phase')==5
    g.press('a',4);g.tick(12)
    res['full_bag_can_discard_one_unit']=g.peek('pocket_n')==23
    g.tick(80);res['discard_does_not_repeat']=g.peek('pocket_n')==23
    g.shot(out/'finite-satchel.png');g.stop()
    # Renderer fixtures: real family art, idle/walk poses, attack tell, hurt and dead.
    # AI is paused explicitly; these prove state rendering, not that a player caused the transition.
    mob_frames=[Image.new('RGB',(7*48,48)) for _ in range(6)]
    for fam in range(7):
        g=fresh(rom,1,fam);g.poke('stage_type',0);teleport(g,40,136)
        e=g.addr('en');m=g.pb.memory
        for off,val in [(8,136*16),(10,136*16)]:m[e+off]=val&255;m[e+off+1]=val>>8
        m[e+6]=255;g.poke('mob',0,10);g.press('start',3);g.tick(10)
        states=[]
        for i,(st,anim,want) in enumerate([(3,0,1),(3,16,2),(0,0,1),(0,8,2),(1,0,3),(4,0,1)]):
            m[e+1]=st;m[e+2]=30;m[e+4]=2 if st==4 else 0;m[e+7]=anim
            g.poke('mob',0,10);g.tick(4)
            states.append(g.peek('mob',15)==want)
            mob_frames[i].paste(g.pb.screen.image.convert('RGB').crop((104,16,152,64)),(fam*48,0))
        res[f'mob_{fam}_idle_walk_tell_hurt']=all(states)
        m[e+1]=6;g.tick(4)
        slot=g.peek('mob',9);tile0=16+slot*24
        visible=[m[0xFE00+k*4+2] for k in range(40) if 0<m[0xFE00+k*4]<160 and 0<m[0xFE00+k*4+1]<168]
        res[f'mob_{fam}_dead_body_removed']=not any(tile0<=t<tile0+16 for t in visible)
        g.stop()
    mob_frames[0].save(out/'mob-states-native.gif',save_all=True,append_images=mob_frames[1:],duration=200,loop=0)
    # Native signed BG tile upload for every card and phase, with the original canonical pose retained.
    data=[int(v) for v in re.findall(r'\d+',Path('src/gen/item_idle.inc').read_text().split('= {',1)[1])]
    assert len(data)==4096
    g=Game(rom);assert start_run(g,True,True,physical_fixture=1)
    g.press('start');g.tick(10) # pause AI, retaining the look's VBlank animation loop
    # A paused gravity/push turn cannot monopolize the presentation budget.
    # Keep both physics counters frozen while every actual item upload runs.
    g.poke('grav_t',1);g.poke('push_t',2)
    g.poke('room',1,241)
    frames=[Image.new('RGB',(16*32,32)) for _ in range(4)]
    import sys
    sys.path.insert(0,'tools')
    import bake_look as art
    cards={name:base for name,_,_,base in art.layout()[0]}
    item0=int(re.search(r'#define M_ITEM0 (\d+)u',Path('src/gen/look_art.h').read_text())[1])
    for p in range(1,17):
        g.poke('room',p,248)
        # A fixture write can land between the native read of the previous
        # piece and its 64-byte VRAM transfer (observed DUST -> CLOUD). Finish
        # that in-flight call before taking the unrelated-card baseline.
        g.tick(2)
        # Fixture tilemap: the ROM still supplies and animates every 2bpp card byte.
        tile=cards['ITEM_'+art.ITEM_ART[p-1][0]];at=0x9800+22*32+4
        assert g.peek('meta_tile',(item0+p-1)*4)==tile
        untouched=bytes(g.pb.memory[0,0x9000+k] for k in range(110*16))
        for off,inc in [(0,0),(32,1),(1,2),(33,3)]:
            g.pb.memory[0,at+off]=tile+inc
            g.pb.memory[1,at+off]=art.ITEM_ART[p-1][1]
        seen=set()
        for _ in range(150):
            g.tick()
            at=0x9000+tile*16
            actual=bytes(g.pb.memory[0,at+k] for k in range(64))
            for phase in range(4):
                expected=bytes(data[phase*1024+(p-1)*64:phase*1024+p*64])
                if actual==expected:
                    seen.add(phase)
                    frames[phase].paste(g.pb.screen.image.convert('RGB').crop((24,32,56,64)),((p-1)*32,0))
        res[f'item_{p}_four_native_phases']=len(seen)==4
        after=bytes(g.pb.memory[0,0x9000+k] for k in range(110*16))
        lo=tile*16;hi=lo+64
        res[f'item_{p}_preserves_every_other_card']=after[:lo]==untouched[:lo] and after[hi:]==untouched[hi:]
        if not res[f'item_{p}_preserves_every_other_card']:
            details[f'item_{p}_outside_changes']=[hex(0x9000+i) for i,(a,b) in enumerate(zip(untouched,after)) if a!=b and not lo<=i<hi]
    frames[0].save(out/'all-items-native.gif',save_all=True,append_images=frames[1:],duration=180,loop=0)
    res['paused_animation_preserves_physics_counters']=g.peek('grav_t')==1 and g.peek('push_t')==2
    g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':res,'details':details,'log':log}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return all(res.values()) and not log


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('rom',type=Path);ap.add_argument('out',type=Path)
    a=ap.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
