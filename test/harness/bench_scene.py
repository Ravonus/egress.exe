"""Pick real ingredients, watch them combine and verify the finite satchel transaction."""
import argparse,hashlib,json
from pathlib import Path
from PIL import Image
from play import Game,start_run,step,bench_act,pocket

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);g=Game(rom);log=[];assert start_run(g,True,True)
    for d in 'URDL':assert step(g,d,log)
    before=pocket(g);frames=[];list_icons=[];tick=g.tick
    def record(n=1):
        for _ in range(n):
            tick()
            if g.peek('bench_on')==1 and not list_icons:
                base=0x9c00 if g.pb.memory[0xff40]&0x40 else 0x9800
                list_icons.extend([g.pb.memory[0,base+33],g.pb.memory[0,base+65]])
            if g.peek('bench_on')==3:
                frames.append(g.pb.screen.image.convert('RGB').resize((320,288),Image.Resampling.NEAREST))
    g.tick=record
    made=bench_act(g,'Cdb',log);del g.tick
    r={'real_ingredients_picked':before==[4,2],'bag_has_native_item_icons':list_icons==[115,113],
       'combination_scene_plays':len(frames)>=40 and len({im.tobytes() for im in frames})>=4,
       'craft_consumes_exactly_two_units':made and pocket(g)==[8],
       'bench_closes_and_restores_hud':not g.peek('bench_on') and g.pb.memory[0xff4a] in (120,128)}
    g.tick(4);base=0x9c00 if g.pb.memory[0xff40]&0x40 else 0x9800
    at=base+(32 if g.pb.memory[0xff4a]==120 else 0);other=base+(0 if at!=base else 32)
    r['bench_restores_meter_tiles_and_palettes']=all(243<=g.pb.memory[0,at+k]<=251 and
        g.pb.memory[1,at+k]&7==6 and g.pb.memory[1,other+k]&7==5 for k in range(3))
    if frames:frames[0].save(out/'crafting-native.gif',save_all=True,append_images=frames[1::2],duration=34,loop=0)
    g.shot(out/'after-crafting.png');g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'scene_frames':len(frames),'icons':list_icons,'log':log}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
