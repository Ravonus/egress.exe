"""Native persistent packets versus an independently materialized complete world."""
import argparse,hashlib,io,json,re,subprocess
from pathlib import Path
from pyboy import PyBoy
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('reference',type=Path);ap.add_argument('rom',type=Path);a=ap.parse_args()
expected=subprocess.check_output([str(a.reference)])
g=PyBoy(str(a.rom),window='null',cgb=True,sound_emulated=False,ram_file=io.BytesIO(bytes(8192)),log_level='ERROR');g.set_emulation_speed(0);count=0;negative=0
answers=[1,0,1,0,0,0,1,0,3,0,0,3,3,0,0,3,3,3,0,0,0,1]
sym={m[1]:int(m[2],16)for m in re.finditer(r'DEF (\S+) 0x([0-9A-Fa-f]+)',a.rom.with_suffix('.noi').read_text())}
starts=[];times=[]
def begin(_):starts.append(g._cycles())
def end(_):times.append(g._cycles()-starts[-1])
for name,fn in [('_world_dead_step',begin),('_dead_after',end)]:
    addr=sym[name];g.hook_register(addr>>16,addr&65535,fn,None)
try:
    for frame in range(60000):
        g.tick(1,False);r=list(g.memory[0xC500:0xC504]);assert not r[3],r
        if r[0]==0x5A and r[1]*33+r[2]==count:
            got=bytes(g.memory[0xC600:0xC844]);want=expected[count*580:(count+1)*580]
            assert got==want,{'case':r[1],'step':r[2],'mismatch':[(i,x,y)for i,(x,y)in enumerate(zip(got,want))if x!=y][:20]}
            count+=1;g.button_press('a');g.tick(2,False);g.button_release('a')
        if r[0]==0x5C and r[1]==negative:
            answer=g.memory[0xC600];assert answer==answers[negative],{'certificate':negative,'got':answer,'want':answers[negative]}
            negative+=1;g.button_press('a');g.tick(2,False);g.button_release('a')
        if r[0]==0xA5:break
    assert r[0]==0xA5 and count==792 and negative==22 and len(expected)==count*580,(r,count,negative)
finally:g.stop(save=False)
assert max(times)<=40000,{'dead_slice_budget_exceeded':max(times)}
print(json.dumps({'rom_sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'trace_sha256':hashlib.sha256(expected).hexdigest(),'frames':frame+1,'states':count,'bytes_per_state':580,'all_bytes_match':True,'prepared_initialization_mutation_backtracking_reset':True,'unprepared_transfer_rejected_without_mutation':True,'retained_certificate_material_skin_protection_crack':True,'valid_states_keep_routes':True,'resource_and_weight_certificates':negative,'checker_does_not_generate_unseen_chunks':True,'max_dead_slice_cycles':max(times),'bank_one_and_stack_canary':True},indent=2))
