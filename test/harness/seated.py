"""Capture the baked seated poses from the native ROM, including mirrored facings and breathing frames."""
import argparse,hashlib,json
from pathlib import Path
from PIL import Image,ImageDraw
from heir import boot_heir,ha_states,crop_heir
from stage2 import teleport

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True)
    names=('LONG_SIT','LONG_SIT_TWITCH','LONG_DOZE','BENCH')
    states=ha_states();sheet=Image.new('RGB',(8*98,4*112),(20,14,46));draw=ImageDraw.Draw(sheet)
    failures=[];samples=0;breath=[];animation=[]
    for row,seed in enumerate((6,4,2,18)):
        g=boot_heir(rom,seed);g.poke('stage_type',0);g.poke('pl_inv',0)
        teleport(g,120,136);g.tick(30)
        for direction in range(8):
            g.poke('dbg_heir_state',states['LONG_SIT']+1);g.poke('dbg_heir_dir',direction);g.tick(8)
            sheet.paste(crop_heir(g,32).resize((96,96),Image.Resampling.NEAREST),(direction*98,row*112+16))
            draw.text((direction*98+2,row*112+2),f'{seed} / {direction}',fill=(231,194,255))
        for name in names:
            for direction in range(8):
                g.poke('dbg_heir_state',states[name]+1);g.poke('dbg_heir_dir',direction);g.tick(8)
                # BODY and WEAR remain four layered 8x16 objects. In particular,
                # the folded legs must have real lower-row WEAR pixels in VRAM.
                entries=[tuple(g.pb.memory[0xfe00+k*4+j] for j in range(4)) for k in range(4)]
                wear=[e for e in entries if (e[3]&7)==1 and e[0]]
                lower=any(any(g.pb.memory[0,0x8000+e[2]*16+22:0x8000+e[2]*16+32]) for e in wear)
                if not (g.pb.memory[0xff40]&4 and len(wear)==2 and lower):failures.append([seed,name,direction])
                samples+=1
        g.poke('dbg_heir_state',states['LONG_SIT']+1);g.poke('dbg_heir_dir',2)
        seen=set()
        for i in range(90):
            g.tick();seen.add(g.peek('ha_frame'))
            if row==0 and i%3==0:animation.append(crop_heir(g,40).resize((240,240),Image.Resampling.NEAREST))
        breath.append(len(seen)>1);g.stop()
    sheet.save(out/'seated-native.png')
    animation[0].save(out/'seated-native.gif',save_all=True,append_images=animation[1:],duration=50,loop=0)
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'native_pose_samples':samples,
            'results':{'seated_layers_and_folded_legs_present':not failures,'seated_breathing_preserved':all(breath)},'failures':failures}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(report['results'].values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
