"""Read actual CGB uploads, including every other card and signed tile addressing."""
import argparse,hashlib,io,json,re
from pathlib import Path
from pyboy import PyBoy
import bake_look as L
import bake_world_art as W
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('rom',type=Path);a=ap.parse_args()
sym={m[1]:int(m[2],16)for m in re.finditer(r'DEF (\S+) 0x([0-9A-Fa-f]+)',a.rom.with_suffix('.noi').read_text())}
ms,b1,*_=L.layout()
def addr(t):return 0x9000+(t if t<128 else t-256)*16
baseline=bytearray(4096)
for t,data in b1.items():baseline[addr(t)-0x8800:addr(t)-0x8800+16]=data
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0)
case=0;checks=0;expected=bytearray();seen=set();poses={k:set()for k in range(12)}
def update(k,phase,style):
    tiles=L.meta_tiles(W.cards(style,phase)[k]);base=ms[W.IDS[k]][3]
    for j,data in enumerate(tiles):
        at=addr(base+j)-0x8800;expected[at:at+16]=data
    poses[k].add(phase)
try:
    for frame in range(10000):
        g.tick(1,False);r=list(g.memory[0xC400:0xC404]);assert not r[3],r
        key=tuple(r[:3])
        if r[0]in(0x5A,0x5B) and key not in seen:
            seen.add(key);style,f=r[1:3]
            if r[0]==0x5B:
                expected=bytearray(baseline)
                for k in range(12):update(k,0,style)
            elif not f&3:update(8+((f>>2)&3),(f>>4)&1,style)
            elif f&7==2:update((f>>3)&7,(f>>6)&1,style)
            actual=bytes(g.memory[1,i]for i in range(0x8800,0x9800))
            assert actual==expected,{'style':style,'frame':f,'initial':r[0]==0x5B,'mismatch':[(hex(i+0x8800),x,y)for i,(x,y)in enumerate(zip(actual,expected))if x!=y][:12]}
            mp=bytes(g.memory[sym['_pal']:sym['_pal']+101]);want=bytearray(m[1]for m in ms)
            for k,n in enumerate(W.IDS):want[n]=W.PALS[k]
            assert mp==want
            checks+=1;g.button_press('a');g.tick(2,False);g.button_release('a')
        if r[0]==0xA5:break
    assert checks==4*257 and all(v=={0,1}for v in poses.values()),(checks,poses)
finally:g.stop(save=False)
print(json.dumps({'rom_sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'frames':frame+1,'native_full_vram_comparisons':checks,'styles':4,'cards':12,'both_idle_poses':True,'every_other_card_preserved':True,'bank_one_vbk_and_stack_canary':True},indent=2))
