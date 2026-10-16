"""Portable/native trajectories: collisions, push requests, strides, maps and both movement modes."""
import argparse,hashlib,io,json,subprocess
from pathlib import Path
from pyboy import PyBoy
p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference',type=Path);p.add_argument('rom',type=Path);a=p.parse_args()
expected=subprocess.check_output([str(a.reference)]);g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0)
count=0
try:
    for frame in range(10000):
        g.tick(1,False);result=list(g.memory[0xC400:0xC404]);assert not result[3],result
        if result[0]==0x5A and result[1]*12+result[2]==count:
            got=bytes(g.memory[0xC600:0xC760]);want=expected[count*352:(count+1)*352]
            assert got==want,{'case':result[1],'batch':result[2],'different_bytes':[(i,x,y) for i,(x,y) in enumerate(zip(got,want)) if x!=y][:12]}
            count+=1;g.button_press('a');g.tick(2,False);g.button_release('a')
        if result[0]==0xA5:break
    assert result==[0xA5,23,11,0] and count==288,result
finally:g.stop(save=False)
print(json.dumps({'rom':str(a.rom),'sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'native_steps':count*16,'cases':24,'frames':frame+1,'portable_trace_sha256':hashlib.sha256(expected).hexdigest(),'all_state_bytes_match':True,'stack_canary':True},indent=2))
