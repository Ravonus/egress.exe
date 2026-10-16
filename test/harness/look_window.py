"""Observe the native card pool/queue transfer against a spatial Python oracle."""
import re
from pathlib import Path

class LookWindow:
    def __init__(self,g):
        self.g=g;self.pending=None;self.samples=0;self.max_cycles=0;self.vertical=0
        obj=Path(__file__).resolve().parents[2]/'build/obj/look'
        symbols={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)\s+\w+',obj.with_suffix('.sym').read_text(),re.M)}
        data=g.addr('look_present')-symbols['_look_present']
        self.addr={name[1:]:data+off for name,off in symbols.items()}
        code=int(re.search(r'^\s*\d+\s+_look_shift_view\s+([0-9A-F]+)',obj.with_suffix('.sym').read_text(),re.M)[1],16)
        section=obj.with_suffix('.lst').read_text().split('; Function look_shift_view',1)[1].split('; Function ',1)[0]
        ret=int(re.findall(r'^\s+([0-9A-F]{8})\s+.*\tret\s*$',section,re.M)[-1],16)
        at=g.addr('look_shift_view');end=at-code+ret
        g.pb.hook_register(at>>16,at&65535,self.enter,None)
        g.pb.hook_register(end>>16,end&65535,self.leave,None)
    def read(self,name,n=1):
        a=self.addr[name];return list(self.g.pb.memory[a:a+n])
    @staticmethod
    def rebase(cell,dx,dy):
        x=(cell&15)-dx;y=(cell>>4)-dy
        return y*16+x if cell<224 and 0<=x<16 and 0<=y<14 else 255
    @staticmethod
    def shift(bits,dx,dy,fill):
        out=[255 if fill else 0]*28
        for y in range(14):
            for x in range(16):
                sx,sy=x+dx,y+dy
                if 0<=sx<16 and 0<=sy<14:
                    old=sy*16+sx;i=y*16+x
                    if bits[old>>3]&(1<<(old&7)):out[i>>3]|=1<<(i&7)
                    else:out[i>>3]&=~(1<<(i&7))
        return out
    def enter(self,_):
        g=self.g;sp=g.pb.register_file.SP
        # GBDK's banked entry retains its six-byte bank/return frame.
        dx,dy=g.pb.memory[sp+6],g.pb.memory[sp+7]
        dx=dx if dx<128 else dx-256;dy=dy if dy<128 else dy-256
        assert (dx,dy) in [(4,0),(-4,0),(0,4),(0,-4)],(dx,dy)
        p={name:self.read(name,28) for name in ['look_present','roam_hidden','redraw','queued','in_flight']}
        for name in ['c_kind','c_cell','c_t','c_meta','c_brk']:p[name]=self.read(name,8)
        for name in ['c_top','c_bot']:p[name]=self.read(name,16)
        p.update({name:self.read(name)[0] for name in ['q_head','q_n','n_flying','n_break']})
        q=self.read('q',256)
        p['queue']=[q[(p['q_head']+i)&255] for i in range(p['q_n'])]
        for k,kind in enumerate(p['c_kind']):
            if not kind:continue
            old=p['c_cell'][k];cell=self.rebase(old,dx,dy)
            if cell==255:
                p['in_flight'][old>>3]&=~(1<<(old&7))
                if 1<=kind<=5 and not p['c_brk'][k]:p['roam_hidden'][old>>3]&=~(1<<(old&7))
                if p['c_brk'][k]:p['c_brk'][k]=0;p['n_break']-=1
                p['c_kind'][k]=0;p['n_flying']-=1
            else:
                p['c_cell'][k]=cell
                for name in ['c_top','c_bot']:
                    v=(p[name][2*k]|p[name][2*k+1]<<8)-dy*16
                    p[name][2*k]=v&255;p[name][2*k+1]=(v>>8)&255
        p['queue']=[c for old in p['queue'] if (c:=self.rebase(old,dx,dy))<224]
        for name in ['look_present','roam_hidden','redraw','queued','in_flight']:
            p[name]=self.shift(p[name],dx,dy,name in ['roam_hidden','redraw'])
        # The material shift has already happened; pits require no flying slab.
        for y in range(14):
            for x in range(16):
                if not ((dx>0 and x>=12) or (dx<0 and x<4) or (dy>0 and y>=10) or (dy<0 and y<4)):continue
                i=y*16+x
                if g.peek('room',i)&31==9:
                    p['look_present'][i>>3]|=1<<(i&7);p['roam_hidden'][i>>3]&=~(1<<(i&7))
        self.pending=(p,g.pb._cycles());self.vertical+=bool(dy)
    def leave(self,_):
        assert self.pending is not None
        p,start=self.pending;g=self.g
        for name in ['look_present','roam_hidden','redraw','queued','in_flight','c_kind','c_cell','c_t','c_meta','c_brk','c_top','c_bot']:
            got=self.read(name,len(p[name]));assert got==p[name],{'presentation':name,'sample':self.samples,'got':got,'want':p[name]}
        for name in ['q_head','n_flying','n_break']:assert self.read(name)[0]==p[name],name
        assert self.read('q_n')[0]==len(p['queue'])
        q=self.read('q',256);assert [q[(p['q_head']+i)&255] for i in range(len(p['queue']))]==p['queue']
        assert g.peek('look_busy_n')==len(p['queue'])+p['n_flying']
        self.max_cycles=max(self.max_cycles,g.pb._cycles()-start);self.samples+=1;self.pending=None
    def report(self):
        return {'native_transfers':self.samples,'vertical_transfers':self.vertical,'maximum_cycles':self.max_cycles,
                'all_cards_spans_queues_and_bitmaps_match':self.pending is None}
