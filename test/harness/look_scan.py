"""Compiled visibility scans against independent per-cell row/column walks."""
import argparse,hashlib,json,random
from pathlib import Path
from play import Game

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);g=Game(rom);cases=0;maximum=0;rng=random.Random(0x564953)
 for _ in range(120):
  g.tick()
  if g.peek('scan_ready'):break
 assert g.peek('scan_ready')
 for pattern in range(12):
  if pattern==0:present=bytes(28);hidden=bytes(28)
  elif pattern==1:present=bytes(28);hidden=bytes([255])*28
  elif pattern==2:present=bytes([255])*28;hidden=bytes([255])*28
  elif pattern==3:present=bytes([85])*28;hidden=bytes([170])*28
  else:
   present=bytes(rng.randrange(256) for _ in range(28));hidden=bytes(rng.randrange(256) for _ in range(28))
  for key,data in [('look_present',present),('roam_hidden',hidden)]:
   for k,v in enumerate(data):g.poke(key,v,k)
  for axis in range(2):
   g.poke('scan_axis',axis)
   for start in range(224):
    g.poke('scan_start',start)
    for count in range(1,(14-start//16 if axis else 16-start%16)+1):
     cells=range(start,start+count*(16 if axis else 1),16 if axis else 1)
     expected=next((c for c in cells if hidden[c//8]&(1<<(c%8)) and not present[c//8]&(1<<(c%8))),255)
     token=cases%254+1;g.poke('scan_count',count);g.poke('scan_request',token)
     for frame in range(6):
      g.tick()
      if g.peek('scan_done')==token:break
     else:raise AssertionError('Compiled visibility scan did not finish')
     got=g.peek('scan_value');assert got==expected,(pattern,axis,start,count,got,expected)
     maximum=max(maximum,frame+1);cases+=1
  assert bytes(g.peek('look_present',k) for k in range(28))==present
  assert bytes(g.peek('roam_hidden',k) for k in range(28))==hidden
 g.poke('scan_axis',2)
 for count in range(1,9):
  g.poke('scan_count',count)
  for spin in range(256):
   token=cases%254+1;g.poke('scan_start',spin);g.poke('scan_request',token)
   for frame in range(6):
    g.tick()
    if g.peek('scan_done')==token:break
   else:raise AssertionError(('Compiled sprite rotation did not finish',spin,count))
   assert g.peek('scan_value')==spin%count,(spin,count,g.peek('scan_value'))
   maximum=max(maximum,frame+1);cases+=1
 g.stop();results={'every_valid_row_and_column_interval_matches':True,'source_bitmaps_unchanged':True,'all_sprite_rotations_match_remainder':True,'bounded_native_operation':maximum<=3}
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'cases':cases,'patterns':12,'maxDisplayFramesPerOperation':maximum,'results':results}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(results.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
