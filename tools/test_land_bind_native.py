"""Compare cartridge global-to-local puzzle identities, including inactive state."""
import argparse,hashlib,io,json,subprocess
from pathlib import Path
from pyboy import PyBoy
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('reference',type=Path);ap.add_argument('rom',type=Path);a=ap.parse_args()
expected=subprocess.check_output([str(a.reference)])
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0);count=0
try:
    for frame in range(50000):
        g.tick(1,False);r=list(g.memory[0xC400:0xC404]);assert not r[3],r
        if r[0]==0x5A and r[1]*16+r[2]==count:
            got=bytes(g.memory[0xC600:0xC740]);want=expected[count*320:(count+1)*320]
            assert got==want,{'case':count,'mismatch':[(i,x,y)for i,(x,y)in enumerate(zip(got,want))if x!=y][:20]}
            count+=1;g.button_press('a');g.tick(2,False);g.button_release('a')
        if r[0]==0xA5:break
    assert r[0]==0xA5 and count==384 and len(expected)==count*320,(r,count)
finally:g.stop(save=False)
print(json.dumps({'rom_sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'trace_sha256':hashlib.sha256(expected).hexdigest(),'frames':frame+1,'states':count,'bytes_per_state':320,'all_bytes_match':True,'bank_one_and_stack_canary':True},indent=2))
