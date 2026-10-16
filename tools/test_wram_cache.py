"""Run the standalone CGB bank-cache canary ROM; this is not a playable larger stage."""
import argparse,hashlib,io,json
from pathlib import Path
from pyboy import PyBoy

p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);a=p.parse_args()
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR')
g.set_emulation_speed(0)
for frame in range(1200):
    g.tick(1,False)
    if g.memory[0xc400]:break
result=list(g.memory[0xc400:0xc404]);g.stop(save=False)
report={'sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'result':result,'frames':frame+1,
        'scope':'eight complete sectors, eight partial transfers, live bank-one canaries, restored SVBK and invalid bounds'}
print(json.dumps(report,indent=2));raise SystemExit(0 if result==[0xa5,16,0,0] else 1)
