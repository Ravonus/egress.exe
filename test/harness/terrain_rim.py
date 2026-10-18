"""Observe cosmetic construction against the real camera's central play area."""
import re
from pathlib import Path
RIM=24  # One easing slab plus a half-slab; keep the central view clear.

class TerrainRim:
    def __init__(self,g,out=None):
        self.g=g;self.out=out;self.samples=0;self.bad=[];self.core_missing=[];self.rim_frames=0
        self.assembly_frames=0;self.collapse_frames=0
        self.maximum_depth=0
        self.last_camera=None;self.motion=(0,0);self.ahead_build_frames=0;self.behind_fall_frames=0
        self.offscreen_restorations=0;self.incoming_motion=[];self.previous_present=None
        symbols=(Path(__file__).resolve().parents[2]/'build/obj/look.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)',symbols,re.M)}
        base=g.addr('look_present')-offsets['_look_present']
        self.addr={k:base+v for k,v in offsets.items()}
    def observe(self):
        g=self.g
        if g.peek('game_mode')!=2 or g.peek('pl_state') in (5,6):return
        self.samples+=1;cx,cy=g.peek('cam_x'),g.peek('cam_y');m=g.pb.memory;rim=False;assembly=False;collapse=False
        if self.last_camera and (cx,cy)!=self.last_camera:
            self.motion=(cx>self.last_camera[0])-(cx<self.last_camera[0]),(cy>self.last_camera[1])-(cy<self.last_camera[1])
        self.last_camera=(cx,cy);ahead=False;behind=False;dx,dy=self.motion
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
            self.maximum_depth=max(self.maximum_depth,min(x+16,160-x,y+16,128-y))
            assembly|=kind<=5;collapse|=kind==7
            if kind<=5:
                oy=m[0xc000+4*a]
                delta=oy-16-y
                if delta and len(self.incoming_motion)<20:self.incoming_motion.append({'frame':g.frames,'cell':cell,'offset':delta})
            ahead|=kind<=5 and ((dx>0 and x>=160-RIM) or (dx<0 and x+16<=RIM) or (dy>0 and y>=128-RIM) or (dy<0 and y+16<=RIM))
            behind|=kind==7 and ((dx>0 and x+16<=RIM) or (dx<0 and x>=160-RIM) or (dy>0 and y+16<=RIM) or (dy<0 and y>=128-RIM))
            if x<160-RIM and x+16>RIM and y<128-RIM and y+16>RIM and len(self.bad)<20:
                self.bad.append({'frame':g.frames,'cell':cell,'kind':kind,'screen':[x,y]})
        self.rim_frames+=rim
        if assembly and not self.assembly_frames and self.out:g.shot(self.out/'visible-build.png')
        if collapse and not self.collapse_frames and self.out:g.shot(self.out/'visible-fall.png')
        self.assembly_frames+=assembly;self.collapse_frames+=collapse
        self.ahead_build_frames+=ahead;self.behind_fall_frames+=behind
        now=bytes(g.peek('look_present',k) for k in range(28));view=tuple(g.peek('world_view',k) for k in range(2))
        if self.previous_present:
            old,pcx,pcy,old_view=self.previous_present
            for c in range(224):
                bit=1<<(c&7)
                if view==old_view and now[c>>3]&bit and not old[c>>3]&bit:
                    x=(c%16)*16-pcx;y=(c//16)*16-pcy
                    if x+16<=0 or x>=160 or y+16<=0 or y>=128:self.offscreen_restorations+=1
        self.previous_present=(now,cx,cy,view)
        missing=[]
        for y in range(14):
            sy=y*16-cy
            if sy>=128-RIM or sy+16<=RIM:continue
            for x in range(1,15):
                sx=x*16-cx
                if sx>=160-RIM or sx+16<=RIM:continue
                c=y*16+x;bit=1<<(c&7)
                if g.peek('roam_hidden',c>>3)&bit and not g.peek('look_present',c>>3)&bit and g.peek('room',c)&31!=9:missing.append(c)
        if missing and len(self.core_missing)<20:self.core_missing.append({'frame':g.frames,'cells':missing,'camera':[cx,cy]})
    def report(self):
        return {'samples':self.samples,'rim_animation_frames':self.rim_frames,
                'rim_pixels':RIM,'maximum_card_depth_from_edge':self.maximum_depth,
                'visible_assembly_frames':self.assembly_frames,'visible_collapse_frames':self.collapse_frames,
                'ahead_build_frames':self.ahead_build_frames,'behind_fall_frames':self.behind_fall_frames,
                'build_and_fall_are_visible':self.ahead_build_frames>0 and self.behind_fall_frames>0,'construction_in_central_view':self.bad,
                'offscreen_restorations':self.offscreen_restorations,'incoming_motion_during_play':self.incoming_motion,
                'prepared_offscreen_and_falls_behind':self.offscreen_restorations>0 and not self.incoming_motion and self.behind_fall_frames>0,
                'unbuilt_central_cells':self.core_missing,'construction_stays_at_rim':not self.bad,
                'central_view_is_built':not self.core_missing}
