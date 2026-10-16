"""Native two-page heir selection and visual/collision alignment regression."""
import argparse,hashlib,json
import numpy as np
from pathlib import Path
from play import Game,start_run,MODE_TITLE
from stage2 import teleport

def wait_ready(g):
    for _ in range(600):
        g.tick()
        if g.peek('family_ready'):g.tick(2);return
    raise AssertionError('heir screen did not finish drawing')

def visible_press(g,button):
    """Observe every LCD frame, including frames while a page is rebuilding."""
    light=[];g.hold({button})
    for k in range(35):
        if k==3:g.hold(set())
        g.tick();light.append(float(np.asarray(g.pb.screen.image)[:,:,:3].mean()))
    return min(light)>8

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};g=Game(rom);assert g.wait_mode(MODE_TITLE,1500)
    g.poke16('dbg_seed',0x5eed)
    for _ in range(30):
        g.press('start',3);g.tick(20)
        if g.peek('game_mode')==6:break
    assert g.peek('game_mode')==6
    wait_ready(g)
    r['three_heir_cards_first']=g.peek('family_page')==0
    order=" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,:!?-+/'>x"
    r['overview_title_and_footer_use_correct_rom_bank']=g.pb.memory[0,0x9822]==190+order.index('C') and g.pb.memory[0,0x9a20]==190+order.index('L')
    g.shot(out/'01-three-heirs.png')
    portraits=[np.asarray(g.pb.screen.image)[40:88,x:x+32,:3].tobytes() for x in (8,64,120)]
    r['three_visibly_distinct_portraits']=len(set(portraits))==3
    pixels=bytes(g.pb.memory[0,0x9200:0x9680])
    r['next_does_not_blank_the_screen']=visible_press(g,'right');wait_ready(g)
    r['cursor_selects_second_card']=g.peek('family_cur')==1 and g.peek('family_page')==0
    r['selection_reuses_cached_portraits']=pixels==bytes(g.pb.memory[0,0x9200:0x9680])
    r['previous_does_not_blank_the_screen']=visible_press(g,'left');wait_ready(g)
    visible_press(g,'right');wait_ready(g)
    g.shot(out/'02-highlighted-heir.png');r['details_do_not_blank_the_screen']=visible_press(g,'a');wait_ready(g)
    r['selection_opens_details_without_starting']=g.peek('family_page')==1 and g.peek('game_mode')==6
    g.shot(out/'03-heir-details.png');r['back_does_not_blank_the_screen']=visible_press(g,'b');wait_ready(g)
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
    # Hardware Y includes a 16px bias. WEAR at +6 and the full 16px tile
    # place its final pixel at collider centre+6, including empty ink rows.
    wear=[g.pb.memory[0xc000+i*4] for i in range(4) if g.pb.memory[0xc003+i*4]&7==1]
    r['feet_match_collision_footprint']=bool(wear) and max(wear)-1==g.peek('pl_y')-g.peek('cam_y')+6
    g.shot(out/'04-centered-passage.png')
    before=bytes(g.pb.memory[0xD940+i] for i in range(256))
    g.poke('door_open',1);g.hold({'up'});g.tick(3);g.hold(set())
    after=bytes(g.pb.memory[0xD940+i] for i in range(256))
    r['door_state_does_not_change_carpet_collision']=all(a==b for i,(a,b) in enumerate(zip(before,after)) if (i&31)!=7) and after[23]==0
    g.stop();report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
