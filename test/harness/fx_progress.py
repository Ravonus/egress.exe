"""Check particle lifetime and movement parity on both native drawing paths."""
from return_observer import watch_return
class FxProgress:
    def __init__(self,g,rom):
        self.g=g;self.pending=None;self.returns=set();self.samples=0;self.late=0;self.errors=[]
        at=g.sym['_fx_draw'];bank=at>>16;pc=at&65535
        reset=g.sym['_fx_reset'];rb=reset>>16;rp=reset&65535
        offset=rb*0x4000+rp-0x4000 if rb else rp
        code=rom.read_bytes()[offset:offset+40];j=code.index(b'\xaf\xea')
        self.addr=int.from_bytes(code[j+2:j+4],'little')-56
        assert 0xc000<=self.addr<0xd800
        g.pb.hook_register(bank,pc,self.enter,None)
    def read(self):
        a=self.addr;m=self.g.pb.memory
        return list(m[1,a:a+56] if a>=0xd000 else m[a:a+56])
    def enter(self,_):
        g=self.g;r=g.pb.register_file;m=g.pb.memory
        if g.peek('game_mode')!=2:return
        assert self.pending is None
        before=self.read();ret=m[r.SP]|m[r.SP+1]<<8
        key=(m[g.sym['__current_bank']] if 0x4000<=ret<0x8000 else 0,ret)
        self.pending=(key,r.SP+2,before)
        self.late+=bool(m[0xff44]>=88 and any(before[::7]))
        if key not in self.returns:
            self.returns.add(key);watch_return(g,key,self.leave)
    def leave(self,key):
        if not self.pending or self.pending[0]!=key or self.g.pb.register_file.SP!=self.pending[1]:return
        want=self.pending[2];self.pending=None;self.samples+=1
        for i in range(0,56,7):
            if not want[i]:continue
            want[i]-=1
            if want[i]&1:
                signed=lambda v:v-256 if v>=128 else v
                want[i+3]=(want[i+3]+(signed(want[i+5])>>3))&255
                want[i+4]=(want[i+4]+(signed(want[i+6])>>3))&255
                want[i+6]=(want[i+6]+2)&255
        got=self.read()
        if got!=want and len(self.errors)<8:self.errors.append({'frame':self.g.frames,'expected':want,'actual':got})
    def report(self):
        return {'updates':self.samples,'late_updates':self.late,'mismatches':self.errors,
                'particle_updates_match':self.samples>10 and not self.errors}
