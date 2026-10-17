"""Native lobby, controls, persisted settings and safe voluntary new-run flow."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,MODE_TITLE,start_run

PREFS=(0xf68,0xf78)
def lobby(g):
    assert g.wait_mode(MODE_TITLE,1500)
    g.tick(60);g.press('start',3)
    for _ in range(600):
        g.tick()
        if g.peek('menu_active'):g.hold(set());g.tick(4);return
    raise AssertionError('lobby failed to open')

def cursor(g,want):
    for _ in range(6):
        if g.peek('menu_cursor')==want:return
        g.press('down',3);g.tick(3)
    raise AssertionError('menu cursor failed')

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};g=Game(rom);lobby(g)
    r['new_save_defaults_to_new_run']=g.peek('menu_cursor')==1 and not g.peek('resume_ready')
    order=" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,:!?-+/'>x"
    base=0x9c00 if g.pb.memory[0xff40]&8 else 0x9800
    r['lobby_title_and_footer_use_correct_rom_bank']=g.pb.memory[0,base+34]==190+order.index('E') and g.pb.memory[0,base+544]==190+order.index('A')
    g.shot(out/'01-lobby.png');cursor(g,0);g.press('a',3);g.tick(20)
    r['missing_continue_stays_in_menu']=bool(g.peek('menu_active')) and not g.peek('floor_no')
    cursor(g,2);g.press('a',3);g.tick(15);g.shot(out/'02-actions.png');g.press('right',3);g.tick(10);g.shot(out/'03-movement.png')
    g.press('b',3);g.tick(10);r['controls_return_to_menu']=bool(g.peek('menu_active')) and g.peek('menu_cursor')==2
    cursor(g,3);g.press('a',3)
    for _ in range(600):
        g.tick()
        if g.peek('game_mode')==2:break
    r['tutorial_enters_a_dedicated_fixed_stage']=g.peek('game_mode')==2 and g.peek('tutorial_on')==2 and g.peek('room',room_start:=10*16+3)==0 and g.peek('room',8*16+3)==0
    g.shot(out/'tutorial-course.png');g.press('start',3);g.tick(20);lobby(g)
    r['course_returns_to_lobby']=bool(g.peek('menu_active')) and not g.peek('tutorial_on')
    cursor(g,5)
    for _ in range(3):g.press('a',3);g.tick(10)
    r['voice_toggles']=not g.peek('voice_on');save=g.stop()
    r['settings_do_not_write_family']=save[0x100:0x426]==bytes(0x326)
    g=Game(rom,sram=save);lobby(g)
    r['settings_survive_poweroff']=not g.peek('voice_on') and not g.peek('tutorial_on');g.stop()
    newest=max(PREFS,key=lambda p:save[p+1]);torn=bytearray(save);torn[newest+14]^=0x80
    g=Game(rom,sram=bytes(torn));lobby(g)
    r['torn_settings_keep_previous_record']=not g.peek('tutorial_on') and bool(g.peek('voice_on'));g.stop()
    g=Game(rom);assert start_run(g,True,True);old_generation=g.peek16_at('family',279)
    board=bytes(g.room()[:224]);position=g.player();palettes=bytes(g.pb.memory[g.addr('pal_bg'):g.addr('pal_bg')+64])
    g.press('start',3);g.tick(8);g.shot(out/'06-pause.png')
    r['pause_uses_framed_in_world_window']=g.peek('hud_line')==80
    g.hold({'right','a'});g.tick(12);g.hold(set());g.tick(3)
    r['pause_freezes_gameplay']=g.player()==position and bytes(g.room()[:224])==board
    g.press('b',3);g.tick(4)
    r['pause_restores_field_and_palette']=g.peek('hud_line') in (120,128) and g.pb.memory[0xff4a]==g.peek('hud_line') and bytes(g.pb.memory[g.addr('pal_bg'):g.addr('pal_bg')+64])==palettes
    save=g.stop()
    g=Game(rom,sram=save);lobby(g)
    r['saved_run_defaults_to_continue']=g.peek('menu_cursor')==0 and bool(g.peek('resume_ready'))
    cursor(g,1);g.press('a',3);g.tick(10);g.shot(out/'04-new-run-confirm.png')
    r['new_run_does_not_silently_discard_heir']=bool(g.peek('menu_active')) and g.peek16('meta')==0
    g.press('b',3);g.tick(10);r['new_run_can_be_cancelled']=g.peek('menu_cursor')==1 and not g.peek('resume_new')
    g.press('a',3);g.tick(10);g.press('a',3)
    for _ in range(900):
        g.tick()
        if g.peek('game_mode')==6 and g.peek('family_ready'):break
    r['confirmed_new_run_records_lineage_once']=g.peek('game_mode')==6 and g.peek16('meta')==1 and g.peek16_at('family',279)==old_generation+1
    g.shot(out/'05-next-generation.png');g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
