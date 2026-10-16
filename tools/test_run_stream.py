"""Compare native streamed layouts to the canonical host, including bank canaries."""
import argparse, hashlib, io, json, subprocess
from pathlib import Path
from pyboy import PyBoy

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('reference',type=Path);p.add_argument('rom',type=Path)
a=p.parse_args();expected=subprocess.check_output([str(a.reference)])
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,
        ram_file=io.BytesIO(bytes(8192)),log_level='ERROR')
g.set_emulation_speed(0);off=0;reports=[]
try:
    for frame in range(20000):
        g.tick(1,False)
        result=list(g.memory[0xC400:0xC404])
        if result[0]==0x5A and len(reports)==result[1]:
            header=bytes(g.memory[0xC410:0xC438]);n=header[0]
            actual=header+bytes(g.memory[0xC800:0xC800+n*12])
            assert actual==expected[off:off+len(actual)], {'case':result[1],'n':n,
                'mismatch':[(i,x,y) for i,(x,y) in enumerate(zip(actual,expected[off:])) if x!=y][:8]}
            off+=len(actual);reports.append({'case':result[1],'slots':n,'floors':header[1]})
            g.button_press('a');g.tick(2,False);g.button_release('a')
        if result[0]==0xA5:break
        assert result[0] in (0,0x5A,0xA5),result
    assert result[0]==0xA5 and len(reports)==24 and off==len(expected),result
finally:g.stop(save=False)
print(json.dumps({'rom':str(a.rom),'sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),
                  'frames':frame+1,'bytes':off,'native_host_exact':True,
                  'bank_one_canaries':True,'cases':reports},indent=2))
