"""Observe real sprite calls and compare their SM83 allocation with the C rules.

Hooks only read registers/RAM at ordinary calls and returns. They never invoke a
function, edit CPU state, replace a board or add a debug path to the cartridge.
"""
from collections import Counter


def signed(v):
    return v-65536 if v&32768 else v


class OamObserver:
    def __init__(self,g):
        self.g=g;self.calls=Counter();self.errors=[];self.pending=None;self.returns=set()
        # band follows oam_n/spin/refused/last/was_busy in the module's DATA.
        self.band=g.addr('oam_n')+5
        a=g.sym['_oam_group'];g.pb.hook_register(a>>16,a&65535,self.enter,None)

    def enter(self,_):
        g=self.g;m=g.pb.memory;r=g.pb.register_file;sp=r.SP
        assert self.pending is None,'nested actor allocation'
        sx=signed(r.D*256+r.E);sy=signed(r.B*256+r.C)
        n,tile,step,prop=[m[sp+2+k] for k in range(4)]
        old=g.peek('oam_n');cap=g.peek('oam_cap');refused=g.peek('oam_refused')
        base=g.addr('shadow_OAM');expected=list(m[base:base+160]);bands=list(m[self.band:self.band+44]);result=1
        self.calls['groups']+=1;self.calls[f'size_{n}']+=1
        if ((sy+15)&65535)>158:self.calls['off_y']+=1
        else:
            full=(sx&65535)<=((160-((n<<3)&255))&255)
            visible=[k for k in range(n) if full or ((sx+8*k+7)&65535)<167]
            vis=len(visible);oy=(sy+16)&255;bi=oy>>2;count=4+bool(oy&3)
            self.calls['full' if full else 'left_clip' if sx<0 else 'right_clip']+=1
            if not vis:self.calls['off_x']+=1
            elif ((old+vis)&255)>cap or any(v>10-vis for v in bands[bi:bi+count]):
                refused=(refused+vis)&255;result=0;self.calls['refused']+=1
            else:
                for k in range(bi,bi+count):bands[k]+=vis
                for j,k in enumerate(visible):expected[(old+j)*4:(old+j+1)*4]=[oy,(sx+8*k+8)&255,(tile+k*step)&255,prop]
                old+=vis
        addr=m[sp]|m[sp+1]<<8;bank=m[g.sym['__current_bank']] if 0x4000<=addr<0x8000 else 0
        key=(bank,addr)
        self.pending=(key,(sp+6)&65535,old,refused,bands,expected,result,(sx,sy,n,tile,step,prop))
        if key not in self.returns:
            self.returns.add(key);g.pb.hook_register(bank,addr,self.leave,key)

    def leave(self,key):
        if self.pending is None or self.pending[0]!=key:return
        g=self.g;r=g.pb.register_file;m=g.pb.memory
        _,sp,n,refused,bands,expected,result,args=self.pending
        if r.SP!=sp:return
        got=(g.peek('oam_n'),g.peek('oam_refused'),list(m[self.band:self.band+44]),list(m[g.addr('shadow_OAM'):g.addr('shadow_OAM')+160]),r.A)
        want=(n,refused,bands,expected,result)
        if got!=want:
            self.errors.append({'args':args,'fields':[k for k,(a,b) in enumerate(zip(got,want)) if a!=b]})
            assert False,self.errors[-1]
        self.pending=None

    def report(self):
        return {'calls':dict(self.calls),'mismatches':self.errors,'all_allocations_match':not self.errors and self.calls['groups']>100}
