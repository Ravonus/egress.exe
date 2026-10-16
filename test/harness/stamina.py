"""Native energy costs, charge release, exhaustion and recovery through controller input."""
import argparse,hashlib,json
from pathlib import Path
from controls import fixture

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};costs={}
    for action,want in [('a',8),('b',10),('a_charge',40),('b_charge',30)]:
        g=fixture(rom);cap=g.peek('stamina',1)
        if action.endswith('charge'):
            g.hold({action[0]});g.tick(40);before=g.peek('stamina');g.hold(set());g.tick(7)
            # A begins with a real primary strike (8); only its follow-up
            # charge is deferred. B has no initial tap shot while held.
            r[action+'_pays_on_release']=before==cap-(8 if action=='a_charge' else 0)
        else:g.press(action,2);g.tick(7)
        costs[action]=cap-g.peek('stamina');r[action+'_cost']=costs[action]==want;g.stop()
    for button,name,want in [('up','roll',24),('left','sidestep',12),('down','backstep',16)]:
        g=fixture(rom);g.hold({'up'});g.tick(3);g.hold(set());g.tick(22);cap=g.peek('stamina',1)
        g.press(button,2);g.hold({button});g.tick(3);g.hold(set())
        costs[name]=cap-g.peek('stamina');r[name+'_cost']=costs[name]==want;g.stop()
    g=fixture(rom);g.hold({'right'});g.tick(28);v=g.peek('stamina');g.tick(32)
    r['running_drains_over_time']=g.peek('stamina')<v;g.hold(set());g.tick(250)
    r['resting_recovers_to_cap']=g.peek('stamina')==g.peek('stamina',1);g.stop()
    g=fixture(rom);g.poke('stamina',0);g.poke('stamina',24,2)
    g.press('a',2);g.tick(2)
    r['exhaustion_denies_attack']=g.peek('pin',2)==0 and not g.peek('pl_arc')
    r['exhaustion_has_recovery_pose']=g.peek('ha_state')==32;g.shot(out/'exhausted.png');g.tick(10)
    r['recovery_waits_before_refilling']=g.peek('stamina')==0;g.tick(240)
    r['exhaustion_is_recoverable']=g.peek('stamina')==g.peek('stamina',1);g.stop()
    # Explicit resource fixtures: inspect the actual VBlank-uploaded bar tiles,
    # preserving every threshold while replacing the per-update long divide.
    g=fixture(rom);meters=True;palettes=True;sampled=0
    for cap in (100,104,108,112,116):
        g.poke('stamina',cap,1)
        for value in range(cap+1):
            g.poke('stamina',value);g.poke('stamina',24,2);g.tick(3)
            base=0x9c00 if g.pb.memory[0xff40]&0x40 else 0x9800
            at=base+(32 if g.pb.memory[0xff4a]==120 else 0)
            lit=value*24//cap
            expected=[243+max(0,min(8,lit-k*8)) for k in range(3)]
            meters=meters and [g.pb.memory[0,at+k] for k in range(3)]==expected
            other=base+(0 if at!=base else 32)
            palettes=palettes and all(g.pb.memory[1,at+k]&7==6 and g.pb.memory[1,other+k]&7==5 for k in range(3))
            sampled+=1
    r['all_545_native_meter_thresholds_unchanged']=meters and sampled==545
    r['meter_palette_tracks_toast_row']=palettes
    g.press('start',3);g.tick(4);g.press('b',3);g.tick(4)
    base=0x9c00 if g.pb.memory[0xff40]&0x40 else 0x9800
    at=base+(32 if g.pb.memory[0xff4a]==120 else 0)
    r['controls_page_restores_meter_palette']=all(g.pb.memory[1,at+k]&7==6 for k in range(3));g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'costs':costs}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
