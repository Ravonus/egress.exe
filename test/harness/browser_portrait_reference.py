"""Compare browser ROM portraits to the same genes composed by the native ROM.

The browser flow uses only normal inputs. Its recorded candidate genes are the
explicit native fixture; palette conversion may differ, pixel structure may not.
"""
import argparse,json
from collections import defaultdict
from pathlib import Path
import numpy as np
from PIL import Image
from play import Game
from picker_collision import open_picker

def run(rom,observed,image,out):
    data=json.loads(observed.read_text());genes=data['family'][38:95];assert len(genes)==57
    g=Game(rom);at=g.sym['_art_load_game'];injected=[]
    def prepare(_):
        if g.peek('game_mode')==6:
            for k,v in enumerate(genes):g.poke('family',v,38+k)
            injected.append(True)
    g.pb.hook_register(at>>16,at&65535,prepare,None);open_picker(g);assert injected
    out.mkdir(parents=True,exist_ok=True);g.shot(out/'native-reference.png');native=np.asarray(g.pb.screen.image)[:,:,:3];browser=np.asarray(Image.open(image).convert('RGB'))
    assert browser.shape==native.shape,(browser.shape,native.shape)
    results=[]
    for card,x in enumerate((8,64,120)):
        n=native[40:88,x:x+32];b=browser[40:88,x:x+32]
        shape_n=np.any(n!=n[0,0],axis=2);shape_b=np.any(b!=b[0,0],axis=2)
        colours=defaultdict(set)
        for nc,bc in zip(n.reshape(-1,3),b.reshape(-1,3)):colours[tuple(nc)].add(tuple(bc))
        results.append({'card':card,'different_silhouette_pixels':int(np.count_nonzero(shape_n!=shape_b)),
                        'same_colour_pixel_structure':all(len(v)==1 for v in colours.values()),'native_colours':len(colours)})
    g.stop();report={'results':results,'all_portraits_match':all(r['different_silhouette_pixels']==0 and r['same_colour_pixel_structure'] for r in results)}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return report['all_portraits_match']

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('rom','observed','image','out'):p.add_argument(name,type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.observed,a.image,a.out) else 1)
