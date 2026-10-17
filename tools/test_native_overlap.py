"""Exhaustive byte-axis distances plus mixed hitboxes in native SM83 code."""
import argparse,hashlib,io,json
from pyboy import PyBoy
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);a=p.parse_args()
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0)
frames=0
while frames<30000 and not g.memory[0xc400]:g.tick(30,False);frames+=30
result=list(g.memory[0xc400:0xc408]);g.stop(save=False)
print(json.dumps({'sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'frames':frames,'checks':65536*10*2+4096,'result':result},indent=2))
raise SystemExit(0 if result[:2]==[0xA5,10] else 1)
