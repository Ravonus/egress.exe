"""Read-only observer for requested poses reaching the native sprite compositor.

A complete cached body is necessary but insufficient: repeated late frames must
not leave the character showing one old pose while the animation keeps playing.
"""
import re
from pathlib import Path

class AnimationProgress:
    def __init__(self,g):
        self.g=g;self.requested=None;self.composed=None;self.wait=0;self.max_wait=0
        self.samples=0;self.uploads=0;self.starved=[];self.poses=set();self.draw_lines=[]
        self.visible=set();self.last_draw=-100;self.invalid=[];self.cache_reads=0;self.cache_writes=0
        sym=(Path(__file__).resolve().parents[2]/'build/obj/heir.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)',sym,re.M)}
        self.frame=g.addr('ha_dir')-offsets['_ha_dir']+offsets['_ha_fr']
        for name,fn in [('_heir_draw',self.draw),('_heir_compose',self.compose)]:
            at=g.sym[name];g.pb.hook_register(at>>16,at&65535,fn,None)
        for name,fn in [('_heir_cache_get',self.cache_get),('_heir_cache_put',self.cache_put)]:
            if name in g.sym:
                at=g.sym[name];g.pb.hook_register(at>>16,at&65535,fn,None)
    def cache_get(self,_):self.cache_reads+=1
    def cache_put(self,_):self.cache_writes+=1
    def draw(self,_):
        g=self.g;pose,direction=g.pb.memory[self.frame:self.frame+2]
        facing=0 if direction==128 else (g.peek('ha_dir')+direction)&7
        self.requested=(pose,8-facing if facing>=5 else facing)
        self.poses.add(self.requested);self.last_draw=g.peek16('frame_count')
        self.draw_lines.append(g.pb.memory[0xff44])
    def compose(self,_):
        r=self.g.pb.register_file;self.composed=(r.A,r.E);self.uploads+=1
        if r.A>=113 or r.E>4:self.invalid.append(self.composed)
    def observe(self):
        g=self.g
        if g.peek('game_mode')!=2 or g.peek('freeze') or ((g.peek16('frame_count')-self.last_draw)&65535)>2:return
        self.samples+=1
        if self.composed is not None and self.requested!=self.composed:self.wait+=1
        else:self.wait=0
        self.max_wait=max(self.max_wait,self.wait)
        if self.wait==8:self.starved.append({'frame':g.frames,'state':g.peek('ha_state'),'requested':self.requested,'composed':self.composed,'line':self.draw_lines[-1]})
        # Observe the actual double-buffered sprite tiles referenced by OAM.
        if g.pb.memory[0xfe00] and g.pb.memory[0xfe02]<16:
            tile=g.pb.memory[0xfe02]&8
            self.visible.add(bytes(g.pb.memory[0,0x8000+16*tile:0x8080+16*tile]))
    def report(self):
        return {'samples':self.samples,'requested_poses':len(self.poses),'compositions':self.uploads,
                'visible_sprite_buffers':len(self.visible),'maximum_unserved_pose_frames':self.max_wait,
                'cache_reads':self.cache_reads,'cache_hits':self.cache_reads-self.cache_writes,
                'starved':self.starved[:12],'invalid_pose_or_direction':self.invalid[:12],
                'animation_keeps_advancing':self.samples>120 and len(self.poses)>5 and len(self.visible)>5 and self.max_wait<8 and not self.invalid}
