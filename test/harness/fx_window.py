"""Read-only oracle for the actual pooled-particle camera transfer."""
import re
from pathlib import Path

class FxWindow:
    def __init__(self,g):
        self.g=g;self.pending=None;self.calls=0;self.live=0;self.clipped=0;self.max_cycles=0
        text=Path('build/obj/fx.sym').read_text();code={m[1]:int(m[2],16) for m in re.finditer(r'^\s*8\s+(_\w+)\s+([0-9A-F]+)',text,re.M)}
        anchor=g.sym['_fx_shift_view'];entry=anchor-code['_fx_shift_view']+code['_shift_fx']
        raw=bytes(g.pb.memory[entry>>16,(entry&65535):(entry&65535)+6]);assert raw[:4]==bytes([0xc5,0xd5,0x57,0x21]),'matching particle transfer leaf required'
        self.base=raw[4]|raw[5]<<8;assert 0xc000<=self.base<0xe000-56
        section=Path('build/obj/fx.lst').read_text().split('; Function shift_fx',1)[1].split('; Function ',1)[0]
        end=int(re.findall(r'^\s+([0-9A-F]{8})\s+.*\tret\s*$',section,re.M)[-1],16)
        end=anchor-code['_fx_shift_view']+end
        g.pb.hook_register(entry>>16,entry&65535,self.enter,None);g.pb.hook_register(end>>16,end&65535,self.leave,None)
    def read(self):
        g=self.g;a=self.base;return list(g.pb.memory[1,a:a+56] if a>=0xd000 else g.pb.memory[a:a+56])
    def enter(self,_):
        r=self.g.pb.register_file
        dx=r.A if r.A<128 else r.A-256;dy=r.E if r.E<128 else r.E-256
        assert (dx,dy) in ((4,0),(-4,0),(0,4),(0,-4))
        self.pending=(self.read(),dx,dy,self.g.pb._cycles())
    def leave(self,_):
        before,dx,dy,start=self.pending;after=self.read();self.pending=None
        for i in range(0,56,7):
            old=before[i:i+7];new=after[i:i+7]
            if not old[0]:assert old==new;continue
            self.live+=1;x=old[3]-dx*16;y=old[4]-dy*16
            if not (0<=x<256 and 0<=y<224):assert not new[0];self.clipped+=1
            else:
                expected=old.copy();expected[3:5]=[x,y];assert new==expected,{'particle':i//7,'expected':expected,'actual':new}
        self.calls+=1;self.max_cycles=max(self.max_cycles,self.g.pb._cycles()-start)
        assert self.max_cycles<4096,self.max_cycles
    def report(self):
        return {'transfers':self.calls,'live_particle_checks':self.live,'out_of_window_particles_clipped':self.clipped,
                'maximum_cycles':self.max_cycles,'all_retained_positions_and_state_match':self.pending is None}
