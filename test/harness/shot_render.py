"""Read-only parity check of native projectile draw requests."""
import re
from pathlib import Path
from oam_observer import signed
from return_observer import watch_return
class ShotRender:
    def __init__(self,g,prepare=None):
        self.g=g;self.prepare=prepare;self.pending=None;self.returns=set();self.samples=0;self.groups=0;self.errors=[];self.maximum_pixel=0
        symbols=(Path(__file__).resolve().parents[2]/'build/obj/sprites.sym').read_text()
        ss={m[2]:(int(m[1]),int(m[3],16)) for m in re.finditer(r'^\s*(\d+)\s+(_\w+)\s+([0-9A-F]+)\s+',symbols,re.M)}
        assert ss['_draw_orbs'][0]==ss['_draw_sprites'][0]
        at=g.sym['_draw_sprites']-ss['_draw_sprites'][1]+ss['_draw_orbs'][1]
        g.pb.hook_register(at>>16,at&65535,self.enter,None)
    def enter(self,_):
        g=self.g;m=g.pb.memory;r=g.pb.register_file
        if g.peek('game_mode')!=2:return
        if self.prepare:self.prepare(g)
        assert self.pending is None
        want=[]
        for k in range(8):
            if not g.peek('orbs',k*10):continue
            x=g.peek16_at('orbs',k*10+4)//16;y=g.peek16_at('orbs',k*10+6)//16
            self.maximum_pixel=max(self.maximum_pixel,x,y)
            want.append((x-g.peek('cam_x')-4,y-g.peek('cam_y')-4,1,132,0,4 if g.peek('orbs',k*10+1) else 5))
        ret=m[r.SP]|m[r.SP+1]<<8;key=(m[g.sym['__current_bank']] if 0x4000<=ret<0x8000 else 0,ret)
        self.pending=(key,r.SP+2,want,[])
        if key not in self.returns:self.returns.add(key);watch_return(g,key,self.leave)
    def group(self,_):
        if not self.pending:return
        r=self.g.pb.register_file;m=self.g.pb.memory
        self.pending[3].append((signed(r.D*256+r.E),signed(r.B*256+r.C),*[m[r.SP+2+i] for i in range(4)]))
    def leave(self,key):
        if not self.pending or self.pending[0]!=key or self.g.pb.register_file.SP!=self.pending[1]:return
        _,_,want,got=self.pending;self.pending=None;self.samples+=1;self.groups+=len(got)
        if want!=got and len(self.errors)<8:self.errors.append({'frame':self.g.frames,'expected':want,'actual':got})
    def report(self):
        return {'passes':self.samples,'groups':self.groups,'maximum_pixel_coordinate':self.maximum_pixel,'mismatches':self.errors,'shot_requests_match':not self.errors}
