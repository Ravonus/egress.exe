"""Compiled medicine packet scan against an independent rectangle oracle."""
import argparse,hashlib,json
from pathlib import Path
from play import Game

def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);g=Game(rom);cases=0;maximum=0
 for _ in range(120):
  g.tick()
  if g.peek('scan_ready'):break
 assert g.peek('scan_ready')
 for pattern in range(32):
  # Include all ignored flag combinations and non-medical near matches.
  choices=[0,5,36,38,37,101,165,229]
  if pattern<6:
   raw=bytearray(224)
   if pattern<4:raw[[223,211,63,0][pattern]]=37+(pattern&3)*64
   if pattern==5:raw[:]=bytes([229])*224
   data=bytes(raw)
  else:data=bytes(choices[((k*17)^(k>>2)^(pattern*13))&7] if (k+pattern)%5==0 else 0 for k in range(224))
  for k,v in enumerate(data):g.poke('scan_data',v,k)
  for x,y,width,height,mode in [(0,0,4,14,142),(12,0,4,14,142),(0,0,16,4,16),(0,10,16,4,16)]:
   cells=[yy*16+xx for yy in range(y,y+height) for xx in range(x,x+width)]
   for count in range(5):
    before=[240+k for k in range(count)]+[255]*(4-count)
    expected=(before[:count]+[c for c in cells if data[c]&63==37])[:4]
    expected+=[255]*(4-len(expected))
    for k,v in enumerate(before):g.poke('health_cell',v,k)
    for k in range(8):g.poke('scan_guard',167,k)
    for key,v in [('start',y*16+x),('rows',mode),('count',count),('request',cases%254+1)]:g.poke('scan_'+key,v)
    for frame in range(10):
     g.tick()
     if g.peek('scan_done')==cases%254+1:break
    else:raise AssertionError('Compiled scan did not finish')
    maximum=max(maximum,frame+1)
    got=[g.peek('health_cell',k) for k in range(4)]
    assert got==expected,(pattern,x,y,count,got,expected)
    assert [g.peek('scan_guard',k) for k in range(8)]==[167]*8
    assert bytes(g.peek('scan_data',k) for k in range(224))==data
    cases+=1
 g.stop();report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'cases':cases,'maxDisplayFramesPerOperation':maximum,'results':{'all_four_directions_match_rectangle_oracle':True,'all_retained_counts_and_ignored_flags_match':True,'full_output_never_overwrites_guard_or_source':True,'bounded_native_operation':maximum<=3}}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(report['results'].values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
