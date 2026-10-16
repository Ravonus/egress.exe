"""Compare both-axis cartridge window travel with its complete host trace."""
import argparse,hashlib,io,json,re,subprocess
from pathlib import Path
from pyboy import PyBoy
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('reference',type=Path);ap.add_argument('rom',type=Path);a=ap.parse_args()
expect=subprocess.check_output([str(a.reference)])
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0)
sym={m[1]:int(m[2],16)for m in re.finditer(r'DEF (\S+) 0x([0-9A-Fa-f]+)',a.rom.with_suffix('.noi').read_text())}
start=[];times=[];paths=[]
def begin(name):start.append(g._cycles());paths.append(name)
def end(_):times.append(g._cycles()-start[-1])
for name,fn in [('_eg_view_shift_cached',begin),('_eg_view_shift_prepared',begin),('_snapshot',end)]:
    addr=sym[name];g.hook_register(addr>>16,addr&65535,fn,name)
count=0;corners=set();previous=(0,0);costs={'horizontal':[],'vertical':[]}
prepared={'horizontal':[],'vertical':[]}
try:
    for frame in range(20000):
        g.tick(1,False);r=list(g.memory[0xC400:0xC404]);assert not r[3],r
        if r[0]==0x5A and r[1]*96+r[2]==count:
            got=bytes(g.memory[0xC600:0xC8CC]);want=expect[count*716:(count+1)*716]
            assert got==want,{'step':count,'mismatch':[(i,x,y)for i,(x,y)in enumerate(zip(got,want))if x!=y][:20]}
            x,y,w,h=got[572:576];corners.add((w,h,x,y))
            if not r[2]:previous=(0,0)
            axis='horizontal' if x!=previous[0] else 'vertical'
            costs[axis].append(times[-1])
            if paths[-1]=='_eg_view_shift_prepared':prepared[axis].append(times[-1])
            previous=(x,y);count+=1
            g.button_press('a');g.tick(2,False);g.button_release('a')
        if r[0]==0xA5:break
    assert r[0]==0xA5 and count==288,(r,count)
finally:g.stop(save=False)
for w,h in ((32,18),(64,34),(128,54)):
    for x,y in ((0,0),(w-16,0),(w-16,h-14),(0,h-14)):assert (w,h,x,y)in corners
assert max(times)<=86000,{'shift_budget_exceeded':max(times)}
assert sum(map(len,prepared.values()))==144
print(json.dumps({'rom_sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'trace_sha256':hashlib.sha256(expect).hexdigest(),'frames':frame+1,'seams':count,'bytes_per_state':716,'max_shift_cycles':max(times),'mean_shift_cycles':sum(times)//len(times),'axis_max_cycles':{k:max(v)for k,v in costs.items()},'prepared_axis_max_cycles':{k:max(v)for k,v in prepared.items()},'prepared_seams':144,'all_bytes_match':True,'five_presentation_bitmaps_match':True,'all_corners':True,'bank_one_and_stack_canary':True},indent=2))
