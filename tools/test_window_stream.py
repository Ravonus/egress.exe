"""Whole-board native window trajectories, actual cache banking and measured shift cost."""
import argparse,io,json,re,subprocess,hashlib
from pathlib import Path
from pyboy import PyBoy
parser=argparse.ArgumentParser(description='Native streamed-window state and bounded-cycle proof');parser.add_argument('reference',type=Path);parser.add_argument('rom',type=Path);args=parser.parse_args();p=args.rom;expect=subprocess.check_output([str(args.reference)])
g=PyBoy(str(p),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0)
sym={m[1]:int(m[2],16) for m in re.finditer(r'DEF (\S+) 0x([0-9A-Fa-f]+)',p.with_suffix('.noi').read_text())}
start=[];times=[]
def begin(_):start.append(g._cycles())
def end(_):times.append(g._cycles()-start[-1])
for name,fn in [('_eg_window_shift_cached',begin),('_snapshot',end)]:
 a=sym[name];g.hook_register(a>>16,a&65535,fn,None)
count=0
try:
 for frame in range(3000):
  g.tick(1,False);r=list(g.memory[0xC400:0xC404]);assert r[3]==0,r
  if r[0]==0x5A and r[1]==count:
   got=bytes(g.memory[0xC600:0xC7FC]);want=expect[count*508:(count+1)*508]
   assert got==want,{'step':count,'bytes':[(i,a,b)for i,(a,b)in enumerate(zip(got,want))if a!=b][:20]}
   count+=1;g.button_press('a');g.tick(2,False);g.button_release('a')
  if r[0]==0xA5:break
 assert count==64 and r==[0xA5,63,0,0],r
finally:g.stop(save=False)
report={'rom_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'trace_sha256':hashlib.sha256(expect).hexdigest(),'seams':count,'bytes_per_state':508,'frames':frame+1,'max_shift_cycles':max(times),'mean_shift_cycles':sum(times)//len(times),'all_bytes_match':True,'bank_one_and_stack_canary':True}
assert max(times)<=70000,report
print(json.dumps(report,indent=2))
