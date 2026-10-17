"""Actual event-to-caption integration: no fake narrator messages and no growth over the play field."""
import argparse,hashlib,json
from pathlib import Path
from play import Game,start_run,step,bench_act,bench_open,bench_cursor,pocket

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);g=Game(rom);log=[];res={};assert start_run(g,True,True,physical_fixture=1)
    g.tick(220)
    footprint=[]
    for _ in range(2100):
        g.tick();footprint.append(g.peek('hud_line'))
    res['heir_choice_generates_a_real_comment']=g.peek('narrator_seq')==1 and (g.peek16('narrator_cue')&31)==10
    res['caption_uses_existing_hud_footprint']=min(footprint)==128
    g.shot(out/'quiet-heir-caption.png')
    for d in 'URDL':assert step(g,d,log)
    assert pocket(g)==[4,2]
    assert bench_open(g,log);bench_cursor(g,0);g.press('start',4);g.tick(12)
    res['refused_use_keeps_items']=pocket(g)==[4,2]
    g.tick(700)
    res['text_cooldown_is_sparse']=g.peek('narrator_seq')==1
    g.tick(6000)
    assert bench_act(g,'Cdb',log)
    for _ in range(200):
        g.tick()
        if g.peek('narrator_seq')>1:break
    res['real_recipe_generates_later_comment']=g.peek('narrator_seq')==2 and (g.peek16('narrator_cue')&31)==3 and pocket(g)==[8]
    res['recipe_caption_does_not_raise_window']=g.peek('hud_line')==128
    fc=g.peek16('frame_count');g.hold({'b'});g.tick(300);g.hold(set())
    res['comment_does_not_pause_action']=((g.peek16('frame_count')-fc)&65535)==300
    g.shot(out/'recipe-caption.png');g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':res,'log':log,'speechAssetsPresent':'_voice_start' in g.sym}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(res.values()) and not log

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
    raise SystemExit(0 if run(a.rom,a.out) else 1)
