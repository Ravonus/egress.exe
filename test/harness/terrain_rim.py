"""Observe cosmetic construction against the real camera's central play area."""
import re
from pathlib import Path

class TerrainRim:
    def __init__(self,g):
        self.g=g;self.samples=0;self.bad=[];self.core_missing=[];self.rim_frames=0
        symbols=(Path(__file__).resolve().parents[2]/'build/obj/look.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)',symbols,re.M)}
        base=g.addr('look_present')-offsets['_look_present']
        self.addr={k:base+v for k,v in offsets.items()}
    def observe(self):
        g=self.g
        if g.peek('game_mode')!=2 or g.peek('pl_state') in (5,6):return
        self.samples+=1;cx,cy=g.peek('cam_x'),g.peek('cam_y');m=g.pb.memory;rim=False
        def read(name,k):
            a=self.addr[name]+k
            return m[1,a] if a>=0xd000 else m[a]
        for k in range(8):
            kind=read('_c_kind',k)
            if not kind or read('_c_brk',k):continue
            a=39-2*k
            if not (m[0xc000+4*a] or m[0xc000+4*(a-1)]):continue
            cell=read('_c_cell',k);x=(cell%16)*16-cx;y=(cell//16)*16-cy
            if x+16<=0 or x>=160 or y+16<=0 or y>=128:continue
            rim=True
            if x<144 and x+16>16 and y<112 and y+16>16 and len(self.bad)<20:
                self.bad.append({'frame':g.frames,'cell':cell,'kind':kind,'screen':[x,y]})
        self.rim_frames+=rim
        missing=[]
        for y in range(14):
            sy=y*16-cy
            if sy>=112 or sy+16<=16:continue
            for x in range(1,15):
                sx=x*16-cx
                if sx>=144 or sx+16<=16:continue
                c=y*16+x;bit=1<<(c&7)
                if g.peek('roam_hidden',c>>3)&bit and not g.peek('look_present',c>>3)&bit and g.peek('room',c)&31!=9:missing.append(c)
        if missing and len(self.core_missing)<20:self.core_missing.append({'frame':g.frames,'cells':missing,'camera':[cx,cy]})
    def report(self):
        return {'samples':self.samples,'rim_animation_frames':self.rim_frames,'construction_in_central_view':self.bad,
                'unbuilt_central_cells':self.core_missing,'construction_stays_at_rim':not self.bad,
                'central_view_is_built':not self.core_missing}
