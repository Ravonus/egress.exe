"""Read-only checks of actual CGB map/attribute bursts and their masked duration."""
import re
from pathlib import Path

class GridObserver:
    def __init__(self,g):
        self.g=g;self.pending=None;self.returns=set();self.calls=0;self.max_cycles=0;self.cells=set();self.attrs=set()
        sym=Path(__file__).resolve().parents[2]/'build/obj/grid.sym'
        symbols={m[2]:(int(m[1]),int(m[3],16)) for m in re.finditer(r'^\s*(\d+)\s+(_\w+)\s+([0-9A-F]+)\s+\w+',sym.read_text(),re.M)}
        def address(name):
            area,off=symbols['_'+name]
            for anchor,(aa,ao) in symbols.items():
                if aa==area and anchor in g.sym:return g.sym[anchor]+off-ao
            raise AssertionError(name)
        self.dst=address('quad_dst');self.src=address('quad_src');self.attr=address('quad_attr')
        entry=address('quad_write')
        assert bytes(g.pb.memory[entry:entry+5])==bytes([0x3E,1,0xE0,0x4F,0x21]),'observer must use matching native objects'
        g.pb.hook_register(entry>>16,entry&65535,self.enter,None)
    def enter(self,_):
        g=self.g;m=g.pb.memory;r=g.pb.register_file;sp=r.SP
        assert self.pending is None
        dst=m[self.dst]|m[self.dst+1]<<8;src=m[self.src]|m[self.src+1]<<8;attr=m[self.attr]
        base=0x9C00 if m[0xFF40]&8 else 0x9800
        assert base<=dst<base+0x400 and dst&1==0
        assert (dst&31)<31
        want=bytes(m[src:src+4]);ret=m[sp]|m[sp+1]<<8
        bank=m[g.sym['__current_bank']] if 0x4000<=ret<0x8000 else 0
        key=bank,ret;self.pending=(key,sp+2,dst,want,attr,g.pb._cycles())
        if key not in self.returns:self.returns.add(key);g.pb.hook_register(*key,self.leave,key)
    def leave(self,key):
        if self.pending is None or self.pending[0]!=key:return
        _,sp,dst,want,attr,start=self.pending;g=self.g
        if g.pb.register_file.SP!=sp:return
        cells=[dst,dst+1,dst+32,dst+33]
        assert bytes(g.pb.memory[0,i] for i in cells)==want,{'map':hex(dst),'want':list(want),'got':[g.pb.memory[0,i] for i in cells]}
        assert [g.pb.memory[1,i] for i in cells]==[attr]*4,{'attributes':hex(dst),'attr':attr}
        assert g.pb.memory[0xFF4F]&1==0
        duration=g.pb._cycles()-start;assert duration<4096,duration
        self.max_cycles=max(self.max_cycles,duration);self.calls+=1;self.cells.add(dst);self.attrs.add(attr);self.pending=None
    def report(self):
        return {'native_quads':self.calls,'distinct_cells':len(self.cells),'attributes':sorted(self.attrs),
                'max_masked_cycles':self.max_cycles,'all_map_and_attributes_match':self.calls>20 and self.pending is None}
