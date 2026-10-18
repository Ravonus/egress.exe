"""Authored CRT shafts: actual cartridge input owns lifting and held stamina.
Fixtures set board/actor resources before the observation; they never move a
monitor or replenish stamina during the tested controller sequence.
"""
import argparse,hashlib,json
from pathlib import Path
from controls import fixture as controls_fixture
from stage2 import teleport

BASE=6*16+6
def fixture(rom,load=1,strength=1,weapon=0,power=0):
    g=controls_fixture(rom)
    if strength>1:
        # Explicit class/power/generation fixture. The real floor reset,
        # including its complete assembly, computes native lift strength.
        g.poke('heir',weapon,7);g.poke('family_stat',power,2)
        g.poke('family',6 if strength==2 else 18,279);g.poke('family',0,280)
        g.poke('door_open',0);g.poke('tokens',2)
        g.hold({'select'});g.tick(65);g.hold(set());loading=False
        for _ in range(1200):
            g.tick()
            if g.peek('game_mode')==3:loading=True
            if loading and g.peek('game_mode')==2 and g.peek('pl_lift_force',1)==strength:break
        else:raise AssertionError('class fixture reset did not finish native assembly')
        assert g.peek('heir',7)==weapon and g.peek('family_stat',2)==power
    assert g.peek('pl_lift_force',1)==strength
    g.poke('world_on',0);g.poke('nen',0);g.poke('enemies_alive',0)
    g.poke('floor_has_crt',1);g.poke('door_open',0)
    for i in range(224):g.poke('room',2 if i%16 in (0,15) or i//16 in (0,13) else 0,i)
    # Side walls keep the test stack from rolling away before contact.
    for y in range(1,8):
        for x in (5,7):g.poke('room',2,y*16+x)
    for k in range(load):g.poke('room',4,BASE-k*16)
    g.poke('room',0,224);g.poke('room',0,225)
    g.poke('heir',weapon,7);g.poke('pin',weapon,13);g.poke('family_stat',power,2)
    g.poke('pl_lift',255);g.poke('pl_lift',0,1);g.poke('pl_lift_force',0)
    g.poke('stamina',100);g.poke('stamina',100,1);g.poke('stamina',24,2)
    g.poke('pl_state',0);g.poke('pl_air',0);g.poke('pl_iframe',0)
    g.pb.memory[1,0xde78:0xde98]=[0xa7]*32
    stop=g.stop
    def checked_stop():
        assert bytes(g.pb.memory[1,0xde78:0xde98])==bytes([0xa7]*32),'lift reached floor stack canary'
        return stop()
    g.stop=checked_stop
    teleport(g,104,120);g.hold(set())
    return g

def raise_once(g):
    g.hold({'up'})
    for _ in range(30):
        g.tick()
        if g.peek('pl_lift',1):return True
    return False

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};detail={}
    g=fixture(rom);r['one_monitor_lifts_one_tile']=raise_once(g) and g.peek('room',BASE)==0 and g.peek('room',BASE-16)==4
    r['lift_costs_stamina']=g.peek('stamina')<100
    value=g.peek('stamina');g.hold(set());g.tick(64)
    r['released_direction_still_holds_weight_without_regen']=0<g.peek('stamina')<value and g.peek('stamina',2)==24 and g.peek('pl_lift_force')==1
    r['brace_uses_push_animation']=g.peek('ha_state')==41 # HA_PUSH
    g.hold({'up'});g.tick(26)
    r['one_strength_cannot_raise_a_second_tile']=g.peek('pl_lift',1)==1 and g.peek('room',BASE-16)==4 and g.peek('room',BASE-32)!=4
    g.hold({'down'});g.tick(24);g.hold({'right'});g.tick(24);g.hold(set());g.tick(180)
    r['moving_clear_restores_stamina']=g.peek('stamina')>value and g.peek('pl_lift_force')==0
    detail['one']={'stamina_after_clear':g.peek('stamina'),'player':g.player()};g.stop()
    g=fixture(rom);g.poke('room',16,BASE+16);g.tick(64) # T_PLANK supports the CRT itself.
    r['world_supported_monitor_does_not_drain_the_heir']=g.peek('pl_lift_force')==0 and g.peek('stamina')==100;g.stop()
    g=fixture(rom,2);g.hold({'up'});g.tick(24)
    r['two_stacked_cannot_lift_even_one_at_strength_one']=g.peek('pl_lift',1)==0 and g.peek('room',BASE-32)!=4;g.stop()
    g=fixture(rom,2,2,1,2);r['two_strength_lifts_two_stacked_one_tile']=raise_once(g) and g.peek('room',BASE)==0 and all(g.peek('room',BASE-16*k)==4 for k in (1,2))
    g.hold(set());g.tick(6);g.hold({'up'});g.tick(24)
    r['two_strength_cannot_lift_two_stacked_two_tiles']=g.peek('pl_lift',1)==1 and g.peek('room',BASE-48)!=4;g.stop()
    g=fixture(rom,1,2,1,2);assert raise_once(g);g.hold({'up'})
    for _ in range(40):
        g.tick()
        if g.peek('pl_lift',1)==2:break
    r['two_strength_lifts_one_monitor_two_tiles']=g.peek('pl_lift',1)==2 and g.peek('room',BASE-32)==4
    g.hold(set());g.tick(8);g.hold({'up'});g.tick(22)
    r['button_release_does_not_reset_height_budget']=g.peek('pl_lift',1)==2 and g.peek('room',BASE-48)!=4;g.stop()
    g=fixture(rom);assert raise_once(g);g.hold(set());g.poke('stamina',1)
    initial=g.player();g.tick(72)
    r['exhaustion_breaks_brace_and_weight_falls']=g.player()[1]>initial[1] and g.peek('room',BASE-16)!=4 and g.peek('stamina')<20
    detail['exhaustion']={'from':initial,'to':g.player(),'stamina':g.peek('stamina'),'load':g.peek('pl_lift_force')};g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'source':__doc__,'results':r,'details':detail}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
