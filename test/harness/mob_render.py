"""Read-only parity check of marker/overlay requests against the mob art rules."""
from oam_observer import signed
from return_observer import watch_return

class MobRender:
    def __init__(self,g):
        self.g=g;self.pending=None;self.returns=set();self.passes=0;self.groups=0;self.errors=[];self.columns={};self.flips=set()
        for name,fn in [('_mobs_draw',self.enter),('_oam_group',self.group)]:
            a=g.sym[name];g.pb.hook_register(a>>16,a&65535,fn,None)
    def enter(self,_):
        g=self.g;r=g.pb.register_file;m=g.pb.memory
        if g.peek('game_mode')!=2:return
        assert self.pending is None
        want=[];cx=g.peek('cam_x');cy=g.peek('cam_y');marker=m[r.SP+6]==2
        for k in range(g.peek('nen')):
            e=[g.peek('en',16*k+i) for i in range(16)]
            a=[g.peek('mob',19*k+i) for i in range(19)]
            if e[0]!=3 or e[1]==6 or a[10]&64:continue
            x=(e[8]|e[9]<<8)//16;y=(e[10]|e[11]<<8)//16
            if marker:
                if g.peek('mob_has_elite') and a[13]&1:want.append((x-cx-12,y-cy-25+((e[7]>>4)&1),1,88,0,7))
                continue
            if not 1<=a[15]<=3:continue
            f=a[15]-1
            if f==2:f=1
            b=(g.peek('mob_slot_bmask',a[9])>>(2*f))&3
            self.columns[b]=self.columns.get(b,0)+1;self.flips.add(e[5]==2)
            sx=x-cx-16;sy=y-cy-16
            if e[1]==1:sx+=1 if e[2]&2 else -1
            elif e[1]==2:
                sx+=(0,0,-2,2)[e[5]];sy+=(-2,2,0,0)[e[5]]
            if a[0]==1:sy+=(0,-1,0,0)[(e[7]>>3)&3]
            tile=16+24*a[9]+16+4*f;flip=e[5]==2
            if b==3:want.append((sx,sy,2,tile+2 if flip else tile,254 if flip else 2,37 if flip else 5))
            elif flip:
                if b&2:want.append((sx,sy,1,tile+2,0,37))
                if b&1:want.append((sx+8,sy,1,tile,0,37))
            else:
                if b&1:want.append((sx,sy,1,tile,0,5))
                if b&2:want.append((sx+8,sy,1,tile+2,0,5))
        ret=m[r.SP]|m[r.SP+1]<<8
        key=(m[g.sym['__current_bank']] if 0x4000<=ret<0x8000 else 0,ret)
        self.pending=(key,r.SP+2,want,[])
        if key not in self.returns:self.returns.add(key);watch_return(g,key,self.leave)
    def group(self,_):
        if not self.pending:return
        r=self.g.pb.register_file;m=self.g.pb.memory
        self.pending[3].append((signed(r.D*256+r.E),signed(r.B*256+r.C),*[m[r.SP+2+i] for i in range(4)]))
    def leave(self,key):
        if not self.pending or self.pending[0]!=key or self.g.pb.register_file.SP!=self.pending[1]:return
        _,_,want,got=self.pending;self.pending=None;self.passes+=1;self.groups+=len(got)
        if want!=got and len(self.errors)<8:self.errors.append({'frame':self.g.frames,'expected':want,'actual':got})
    def report(self):
        return {'passes':self.passes,'groups':self.groups,'column_masks':self.columns,'flips':sorted(self.flips),'mismatches':self.errors,
                'mob_art_requests_match':self.passes>100 and self.groups>10 and not self.errors}
