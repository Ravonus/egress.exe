"""The real physical workbench: input, readable scene, fusion, collection and exact VRAM restoration."""
import argparse,hashlib,json
from pathlib import Path
from PIL import Image
from play import Game,start_run,step,bench_open,bench_cursor,pocket


def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);g=Game(rom);log=[];r={}
    assert start_run(g,True,True,physical_fixture=1)
    for d in 'URDL':assert step(g,d,log)
    before=pocket(g);r['real_ingredients_picked']=before==[4,2]
    assert bench_open(g,log,craft=True)
    r['scene_uses_full_native_screen']=g.pb.memory[0xff4a]==0 and g.peek('hud_line')==144
    g.shot(out/'01-choose-first.png')
    g.press('select',4);g.tick(12);g.shot(out/'01-inventory-page.png')
    r['select_toggles_separate_merge_and_inventory_pages']=g.peek('bench_page')==1
    g.press('select',4);g.tick(12)
    r['select_toggles_separate_merge_and_inventory_pages'] &= g.peek('bench_page')==0
    saved=bytes(g.pb.memory[0,0xb500:0xb940])
    r['scene_preserves_exact_terrain_and_mob_tiles_in_scratch']=len(saved)==1088 and any(saved)
    # Horizontal navigation is selection, including a prolonged held direction.
    n=g.peek('pocket_n');g.press('right',4);g.tick(12)
    r['horizontal_selection_moves_between_materials']=g.peek('bench_cur')==1
    g.hold({'right'});g.tick(45);g.hold(set());g.tick(4)
    r['selection_never_discards']=g.peek('pocket_n')==n
    bench_cursor(g,0);g.press('a',4);g.tick(12);g.shot(out/'02-first-in-bay.png')
    bench_cursor(g,1);g.press('a',4);g.tick(12);g.shot(out/'03-ready.png')
    # B removes only the second bay, leaving the first material in place.
    g.press('b',4);g.tick(12);bench_cursor(g,1);g.press('a',4);g.tick(12)
    frames=[];phases=set();poses=set();early=[];tick=g.tick
    def record(n=1):
        for _ in range(n):
            tick()
            if g.peek('bench_on')==3 or g.peek('bench_phase')==4:
                phase=g.peek('bench_phase')
                if phase not in phases:g.shot(out/f'phase-{phase}.png')
                phases.add(phase)
                if phase in (2,3):
                    active=0x8000+(g.pb.memory[0xfe02]&32)*16
                    poses.add(bytes(g.pb.memory[1,active:active+512]))
                if pocket(g)!=before:early.append(g.frames)
                frames.append(g.pb.screen.image.convert('RGB').resize((320,288),Image.Resampling.NEAREST))
    g.tick=record;g.press('a',4)
    for _ in range(500):
        g.tick()
        if g.peek('bench_on')==1 and g.peek('bench_phase')==4:break
    r['body_contact_deformation_and_reveal']=phases=={1,2,3,4} and len(poses)>=17
    r['ingredients_stay_finite_until_collected']=not early and pocket(g)==before
    g.tick(60);r['result_waits_for_collection']=g.peek('bench_phase')==4 and pocket(g)==before
    del g.tick
    g.shot(out/'04-result-awaits-collection.png');g.press('a',4);g.tick(20)
    r['collect_commits_exactly_two_units_and_equips_result']=pocket(g)==[8] and g.peek('tool_piece')==8 and not g.peek('bench_phase')
    g.shot(out/'05-collected.png')
    restored=[]
    def closing(_):
        if g.peek('bench_on')==4 and pocket(g)==[8]:restored.append(bytes(g.pb.memory[1,0x8000:0x8440]))
    hook=g.sym['_frame_wait'];g.pb.hook_register(hook>>16,hook&65535,closing,None)
    g.press('b',4);g.tick(12)
    g.pb.hook_deregister(hook>>16,hook&65535)
    r['bench_closes_and_restores_hud']=not g.peek('bench_on') and g.pb.memory[0xff4a] in (120,128)
    actual=bytes(g.pb.memory[1,0x8000:0x8440])
    r['exact_borrowed_vram_restored']=bool(restored) and restored[-1]==saved
    (out/'restore-diff.json').write_text(json.dumps({'differences':[{'offset':i,'before':a,'after':b} for i,(a,b) in enumerate(zip(saved,actual)) if a!=b]},indent=2)+'\n')
    g.tick(4);base=0x9c00 if g.pb.memory[0xff40]&0x40 else 0x9800
    at=base+(32 if g.pb.memory[0xff4a]==120 else 0);other=base+(0 if at!=base else 32)
    r['meters_and_palettes_restored']=all(243<=g.pb.memory[0,at+k]<=251 and g.pb.memory[1,at+k]&7==6 and g.pb.memory[1,other+k]&7==5 for k in range(3))
    if frames:frames[0].save(out/'body-fusion-native.gif',save_all=True,append_images=frames[1::2],duration=34,loop=0)
    g.shot(out/'06-back-in-world.png');g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'phases':sorted(phases),'deforming_poses':len(poses),'scene_frames':len(frames),'log':log}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values()) and not log

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
