"""Byte-for-byte native circuit/portal trajectories, advanced with joypad input."""
import argparse,hashlib,io,json,subprocess
from pathlib import Path
from pyboy import PyBoy
p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference',type=Path);p.add_argument('rom',type=Path);a=p.parse_args()
expected=subprocess.check_output([str(a.reference)])
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0)
count=0
try:
    for frame in range(10000):
        g.tick(1,False);result=list(g.memory[0xC400:0xC404]);assert not result[3],result
        if result[0]==0x5A and result[1]*32+result[2]==count:
            got=bytes(g.memory[0xC600:0xC778]);want=expected[count*376:(count+1)*376]
            assert got==want,{'case':result[1],'step':result[2],'bytes':[(i,x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y][:16]}
            count+=1;g.button_press('a');g.tick(2,False);g.button_release('a')
        if result[0]==0xA5:break
    assert result==[0xA5,15,31,0] and count==512,result
finally:g.stop(save=False)
print(json.dumps({'rom':str(a.rom),'sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),
 'states':count,'bytes_per_state':376,'frames':frame+1,'portable_trace_sha256':hashlib.sha256(expected).hexdigest(),
 'all_state_bytes_match':True,'bank_one_and_stack_canary':True},indent=2))
