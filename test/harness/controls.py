"""The owner's A/B attack and directional evade mapping, exercised with real cartridge input."""
import argparse
import hashlib
import json
from pathlib import Path
from play import Game,start_run
from stage2 import teleport

def fixture(rom):
    g=Game(rom);assert start_run(g,True,True,floor=2)
    g.poke('stage_type',0)
    for y in range(3,12):
        for x in range(2,14):g.pb.memory[g.addr('room')+y*16+x]=0
    teleport(g,120,136)
    g.poke('heir',0,7);g.poke('pin',0,13);g.poke('family_stat',0,3)
    return g

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);res={};detail={}
    g=fixture(rom);g.press('a',2);g.tick(3)
    res['a_primary_attack']=g.peek('pin',2)==5;g.stop()
    g=fixture(rom);xy=g.player();g.press('b',2);g.tick(6)
    shots=sum(g.peek('orbs',k*10)!=0 for k in range(8))
    res['b_tap_fires_without_evading']=shots>0 and g.peek('pin',2)==11 and g.player()==xy
    g.shot(out/'b-shot.png');g.stop()
    g=fixture(rom);xy=g.player();g.hold({'b'});g.tick(60)
    shots=sum(g.peek('orbs',k*10)!=0 for k in range(8))
    res['b_hold_charges_without_shooting']=g.peek('pin',2)==12 and shots==0 and g.player()==xy
    g.hold(set());g.tick(7)
    res['b_release_executes_charged_secondary']=g.peek('pin',2)==13 and any(g.peek('orbs',k*10+1)>1 for k in range(8))
    g.shot(out/'b-charged.png');g.stop()
    g=fixture(rom);g.hold({'a'});g.tick(40);g.hold(set());g.tick(6)
    res['a_hold_charged_attack']=g.peek('pin',2)==9 and g.peek('pl_level')==2;g.stop()
    for order,want in [('ab',10),('ba',4)]:
        g=fixture(rom);g.press(order[0],2);g.press(order[1],2)
        res[order+'_ordered_combo']=g.peek('pin',2)==want;g.shot(out/(order+'-combo.png'));g.stop()
    for name,button,kind in [('forward','up',0),('side','left',1),('back','down',2)]:
        g=fixture(rom)
        g.hold({'up'});g.tick(3);g.hold(set());g.tick(22)
        g.press(button,2);xy=g.player();g.hold({button});g.tick(3)
        res[name+'_directional_evade']=g.peek('pin',2)==1 and g.peek('pin',21)==kind
        if kind:res[name+'_keeps_aim']=g.peek('pl_face')==0
        g.shot(out/(name+'-evade.png'))
        g.tick(5);now=g.player();detail[name]={'from':xy,'to':now,'animation':g.peek('ha_state')}
        res[name+'_moves_in_requested_direction']=(now[1]<xy[1] if name=='forward' else now[0]<xy[0] if name=='side' else now[1]>xy[1])
        g.tick(20);res[name+'_held_direction_runs']=g.peek('pin',2)==2
        g.stop()
    g=fixture(rom);g.hold({'right'});g.tick(28)
    res['direction_hold_alone_runs']=g.peek('pin',2)==2
    g.hold({'right','b'});g.tick(30)
    res['movement_and_secondary_charge_together']=g.peek('pin',2)==12 and g.peek('pin',23)==1
    before=g.peek16('frame_count');g.tick(600);elapsed=(g.peek16('frame_count')-before)&65535
    res['steady_game_frame_pacing']=elapsed>=590
    detail['game_frames_per_600_vblanks']=elapsed
    g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':res,'details':detail}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return all(res.values())

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('rom',type=Path);ap.add_argument('out',type=Path)
    a=ap.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
