"""Exact native/host codec plans and every material/design byte, without CPU pokes."""
import argparse, hashlib, io, json, subprocess
from pathlib import Path
from pyboy import PyBoy

ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('reference',type=Path);ap.add_argument('rom',type=Path);a=ap.parse_args()
expected=subprocess.check_output([str(a.reference)])
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0)
offset=0;case=0;row=0;rows=0;cells=0;reports=[]
try:
    for frame in range(30000):
        g.tick(1,False);r=list(g.memory[0xC400:0xC404]);assert not r[3],r
        if r[0]==0x5A and r[1]==case and r[2]==row:
            if row==0:
                header=bytes(g.memory[0xC410:0xC463]);want=expected[offset:offset+83]
                assert header==want,{'case':case,'header_mismatch':[(i,x,y)for i,(x,y)in enumerate(zip(header,want))if x!=y]}
                offset+=83;width,height=header[2:4]
                reports.append({'case':case,'width':width,'height':height,'size':header[4],'biome':header[5],'rule':header[6],'shape':header[7],'vertical':header[8]})
            n=width*4;got=bytes(g.memory[0xC600:0xC600+n]);want=expected[offset:offset+n]
            assert got==want,{'case':case,'row':row,'mismatch':[(i,x,y)for i,(x,y)in enumerate(zip(got,want))if x!=y][:16]}
            offset+=n;row+=1;rows+=1;cells+=width
            if row==height:row=0;case+=1
            g.button_press('a');g.tick(2,False);g.button_release('a')
        if r[0]==0xA5:break
    assert r[0]==0xA5 and case==24 and offset==len(expected),(r,case,offset,len(expected))
finally:g.stop(save=False)
print(json.dumps({'rom_sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'trace_sha256':hashlib.sha256(expected).hexdigest(),'frames':frame+1,'rows':rows,'cells':cells,'all_bytes_match':True,'bank_one_and_stack_canary':True,'cases':reports},indent=2))
