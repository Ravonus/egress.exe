"""Create a genuine current-ROM checkpoint in the authored crafting course.
The browser subsequently uses ordinary inputs; no browser RAM/inventory writes.
"""
import argparse,base64,hashlib,json
from pathlib import Path
from play import Game,start_run

def run(rom,out):
    g=Game(rom);assert start_run(g,False,False,seed=0x5104,physical_fixture=1)
    data={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'source':'Current ROM; explicit authored mechanic course; ordinary floor-entry save.',
          'gene':[g.peek('family',k) for k in range(19)],'player':list(g.player()),'room':g.room()}
    assert data['player']==[40,200] and g.peek('pocket_n')==0
    data['battery']=base64.b64encode(g.stop()).decode();out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(data)+'\n')
    print({k:v for k,v in data.items() if k not in ('battery','room','gene')})
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();run(a.rom,a.out)
