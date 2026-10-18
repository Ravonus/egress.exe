"""Compare compiled SM83 journal CRCs to an independent CCITT oracle."""
import argparse,hashlib,json,sys
from pathlib import Path
from play import Game
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from rtc_test import crc16
def run(rom,out):
 out.mkdir(parents=True,exist_ok=True);g=Game(rom);maximum=0;cases=0;fail=[]
 for _ in range(120):
  g.tick()
  if g.peek('journal_ready'):break
 assert g.peek('journal_ready')
 for pattern in range(3):
  data=bytes(((k*109)^(k>>1)^(pattern*127))&255 for k in range(384))
  for k,v in enumerate(data):g.poke('journal_data',v,k)
  for size in range(385):
   token=cases%254+1;g.poke16('journal_size',size);g.poke('journal_request',token)
   for frame in range(20):
    g.tick()
    if g.peek('journal_done')==token:break
   assert g.peek('journal_done')==token
   maximum=max(maximum,frame+1);cases+=1;got=g.peek16('journal_value');expected=crc16(data[:size])
   if got!=expected:fail.append({'pattern':pattern,'bytes':size,'expected':expected,'actual':got})
 g.stop();r={'all_native_crcs_match_independent_oracle':not fail,'bounded_native_operation':maximum<=3}
 report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'cases':cases,'lengths':'Every length 0..384; three byte patterns, including both counter bytes.','maxDisplayFramesPerOperation':maximum,'failures':fail[:20]}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
