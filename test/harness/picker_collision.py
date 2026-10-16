"""Native two-page heir selection and visual/collision alignment regression."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,MODE_TITLE
from stage2 import teleport

def wait_ready(g):
    for _ in range(600):
        g.tick()
        if g.peek('family_ready'):g.tick(2);return
    raise AssertionError('heir screen did not finish drawing')

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};g=Game(rom);assert g.wait_mode(MODE_TITLE,1500)
    g.poke16('dbg_seed',0x5eed)
    for _ in range(30):
        g.press('start',3);g.tick(20)
        if g.peek('game_mode')==6:break
    assert g.peek('game_mode')==6
    wait_ready(g)
    r['three_heir_cards_first']=g.peek('family_page')==0
    g.shot(out/'01-three-heirs.png');g.press('right',3);wait_ready(g)
    r['cursor_selects_second_card']=g.peek('family_cur')==1 and g.peek('family_page')==0
    g.shot(out/'02-highlighted-heir.png');g.press('a',3);wait_ready(g)
    r['selection_opens_details_without_starting']=g.peek('family_page')==1 and g.peek('game_mode')==6
    g.shot(out/'03-heir-details.png');g.press('b',3);wait_ready(g)
    r['details_back_retains_selection']=g.peek('family_page')==0 and g.peek('family_cur')==1
    g.press('a',3);wait_ready(g);g.press('a',3)
    r['confirm_starts_selected_heir']=g.wait_mode(2,1800)
    g.stop()
    g=Game(rom);assert start_run(g,True,True,floor=2);g.poke('stage_type',0)
    for y in range(2,12):
        for x in range(3,13):g.poke('room',0 if x==7 else 2,y*16+x)
    teleport(g,120,168);g.poke('pl_inv',0);g.hold({'up'});g.tick(35);g.hold(set());g.tick(3)
    r['one_tile_passage_works_at_center']=g.peek('pl_x')==120 and g.peek('pl_y')<130
    raw=[g.pb.memory[0xFE01+i*4] for i in range(4) if g.pb.memory[0xFE00+i*4]]
    r['body_center_matches_collision_center']=bool(raw) and min(raw)==g.peek('pl_x')-g.peek('cam_x')
    g.shot(out/'04-centered-passage.png')
    before=bytes(g.pb.memory[0xD940+i] for i in range(256))
    g.poke('door_open',1);g.hold({'up'});g.tick(3);g.hold(set())
    after=bytes(g.pb.memory[0xD940+i] for i in range(256))
    r['door_state_does_not_change_carpet_collision']=all(a==b for i,(a,b) in enumerate(zip(before,after)) if (i&31)!=7) and after[23]==0
    g.stop();report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
