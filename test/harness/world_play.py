"""Play persistent codec worlds through normal inputs on the actual cartridge."""
import argparse
from collections import deque
import hashlib,json,re,subprocess
from pathlib import Path
from play import Game,start_run,PASSABLE,pocket,bench_act
from heir import OamMeter
from grid_observer import GridObserver
from heir_pixels import Pixels

class WorldPlayer:
    def __init__(self,g,tool,rom=None):
        self.g=g;self.monitor=True;self.log=[];self.plan=bytes(g.peek('world_plan',i) for i in range(83))
        self.w,self.h=self.plan[2:4];self.vertical=self.plan[8]
        raw=subprocess.check_output([str(tool),str(int.from_bytes(self.plan[:2],'little')),
                                     str(self.plan[6]),str(self.plan[4]),str(self.plan[5])])
        assert raw[:83]==self.plan
        self.cells=bytearray(raw[83::4]);self.visited=set();self.warps=[]
        self.key=next((i%self.w,i//self.w) for i,t in enumerate(self.cells) if t&31==6)
        self.nodes=[list(self.plan[11+k*6:17+k*6]) for k in range(self.plan[9])]
        self.items=[list(self.plan[35+k*3:38+k*3]) for k in range(self.plan[10])]
        self.portal_points={self.xy(u+d,v) for kind,u,v,*_ in self.nodes if kind==4 for d in (-2,2)}
        self.cached_pos=self.raw_pos();self.fc=[];self.missing=[];self.body=[];self.hitches=[];self.last=self.pos();self.screens=[]
        self.stream_wait=self.stream_wait_max=self.stream_wait_total=0
        self.stream_wait_samples=[];self.stream_clamp=False
        self.last_view=tuple(g.peek('world_view',i) for i in range(2));self.pit_checks=0
        symbols=(Path(__file__).resolve().parents[2]/'build/obj/look.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)\s+\w+',symbols,re.M)}
        data=g.addr('look_present')-offsets['_look_present']
        self.pool_queue=data+offsets['_q_n'];self.pool_live=data+offsets['_n_flying'];self.pool_peak=self.live_peak=0
        # Several complete idle poses intentionally need fewer than four OBJs.
        # Compare the completed shadow list to the pose's actual count, and
        # independently verify composed gene pixels when the ROM is supplied.
        symbols=(Path(__file__).resolve().parents[2]/'build/obj/heir.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)\s+\w+',symbols,re.M)}
        self.body_count=g.addr('ha_dir')-offsets['_ha_dir']+offsets['_d_n']
        symbols=(Path(__file__).resolve().parents[2]/'build/obj/world_play.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)\s+\w+',symbols,re.M)}
        self.preparation=g.addr('world_seams')-offsets['_world_seams']+offsets['_px']
        # Observe the actual mover result immediately before the native guard.
        # Standing against a wall during dodge recovery is not a stream wait.
        obj=Path(__file__).resolve().parents[2]/'build/obj/player'
        symbols=obj.with_suffix('.sym').read_text()
        offset=int(re.search(r'^\s*8\s+_player_update\s+([0-9A-F]+)',symbols,re.M)[1],16)
        listing=obj.with_suffix('.lst').read_text().split('if(world_on) {',1)[1]
        clamp=int(re.search(r'^\s+([0-9A-F]{8})\s+\S+',listing,re.M)[1],16)
        at=g.sym['_player_update'];g.pb.hook_register(at>>16,(at&65535)-offset+clamp,self.before_clamp,None)
        self.pixels=Pixels(g,rom) if rom is not None else None
        self.meter=OamMeter(g);self.grid=GridObserver(g)
        self.tick0=g.tick;g.tick=self.tick
        at=g.sym['_frame_wait'];g.pb.hook_register(at>>16,at&65535,self.completed,None)
        g.pb.memory[0xddd0:0xddf0]=[0xa7]*32
        self.observe()
    def xy(self,u,v):return (v,u) if self.vertical else (u,v)
    def raw_pos(self):
        g=self.g
        return g.peek('world_view')*16+g.peek('pl_x'),g.peek('world_view',1)*16+g.peek('pl_y')
    def pos(self):return self.cached_pos
    def cell(self):
        x,y=self.pos();return x//16,y//16
    def observe(self):
        g=self.g;ox,oy=g.peek('world_view'),g.peek('world_view',1)
        for y in range(14):
            for x in range(16):
                self.cells[(oy+y)*self.w+ox+x]=g.peek('room',y*16+x)
        self.visited.add((ox,oy))
    def before_clamp(self,_):
        g=self.g
        if not self.monitor or not g.peek('world_on'):return
        x=g.peek16('pmv');y=g.peek('pmv',2)|(g.peek('pmv',3)<<8)
        x=x if x<32768 else x-65536;y=y if y<32768 else y-65536
        vx,vy=g.peek('world_view'),g.peek('world_view',1)
        self.stream_clamp|=bool((vx and x<288) or (vx<self.w-16 and x>3616) or
                               (vy and y<544) or (vy<self.h-14 and y>3104))
    def completed(self,_):
        g=self.g
        if not self.monitor:return
        if g.peek('floor_no')!=7 or g.peek('game_mode')!=2:return
        self.observe();pos=self.raw_pos();self.cached_pos=pos
        vx,vy=g.peek('world_view'),g.peek('world_view',1);joy=g.peek('joy')
        waiting=self.stream_clamp and pos==self.last and not g.peek('bench_on');self.stream_clamp=False
        self.stream_wait=self.stream_wait+1 if waiting else 0
        self.stream_wait_max=max(self.stream_wait_max,self.stream_wait)
        self.stream_wait_total+=bool(waiting)
        if self.stream_wait in (1,8,16,24) and len(self.stream_wait_samples)<12:
            self.stream_wait_samples.append({'frame':g.frames,'waiting':self.stream_wait,'position':pos,
                'view':[vx,vy],'preparation':list(g.pb.memory[self.preparation:self.preparation+10]),'joy':joy})
        if g.peek('pl_state') not in (5,6):
            self.pool_peak=max(self.pool_peak,g.pb.memory[self.pool_queue]+g.pb.memory[self.pool_live])
            self.live_peak=max(self.live_peak,g.pb.memory[self.pool_live])
            n=g.pb.memory[self.body_count]
            if not n or any(not g.pb.memory[0xc000+4*k] for k in range(n)):
                if not self.body and hasattr(self,'out'):g.shot(self.out/'first-missing-body.png')
                self.body.append({'frame':g.frames,'local':g.player(),'global':pos,
                                  'view':[g.peek('world_view'),g.peek('world_view',1)],
                                  'camera':[g.peek('cam_x'),g.peek('cam_y')],
                                  'invincible':g.peek('pl_inv'),'state':g.peek('pl_state'),
                                  'height':g.peek('pl_z'),'bench':g.peek('bench_on'),
                                  'joy':g.peek('joy'),'preparation':list(g.pb.memory[self.preparation:self.preparation+10]),
                                  'pose':g.peek('ha_state'),'count':n,
                                  'shadow':list(g.pb.memory[0xc000:0xc010])})
        view=tuple(g.peek('world_view',i) for i in range(2))
        if view!=self.last_view:
            dx,dy=view[0]-self.last_view[0],view[1]-self.last_view[1]
            for y in range(14):
                for x in range(16):
                    if not ((dx>0 and x>=12) or (dx<0 and x<4) or (dy>0 and y>=10) or (dy<0 and y<4)):continue
                    c=y*16+x
                    if g.peek('room',c)&31==9:
                        assert g.peek('look_present',c>>3)&(1<<(c&7)),('incoming pit',view,c)
                        self.pit_checks+=1
            self.last_view=view
        if abs(pos[0]-self.last[0])+abs(pos[1]-self.last[1])>48:self.warps.append((self.last,pos))
        self.last=pos
    def tick(self,n=1):
        g=self.g
        if not self.monitor:self.tick0(n);return
        for _ in range(n):
            before=g.peek16('frame_count');bench=g.peek('bench_on');self.tick0()
            if g.peek('floor_no')!=7 or g.peek('game_mode')!=2:return
            fc=g.peek16('frame_count')
            active=not bench and not g.peek('bench_on') and g.peek('pl_state') not in (5,6)
            self.fc.append(fc if active else None)
            if active:
                delta=(fc-before)&65535
                if delta!=1:self.hitches.append((g.frames,delta,g.peek16('world_seams')))
                c=(g.peek('pl_y')//16)*16+g.peek('pl_x')//16
                if not g.peek('pl_air') and not (g.peek('look_present',c>>3)&(1<<(c&7))):
                    self.missing.append((g.frames,self.cell(),g.peek('pl_state'),g.peek16('world_seams')))
    def route(self,target):
        start=self.cell();q=deque([start]);seen={start:None}
        while q:
            x,y=q.popleft()
            if (x,y)==target:break
            for dx,dy in ((1,0),(0,1),(-1,0),(0,-1)):
                p=x+dx,y+dy
                if not (0<=p[0]<self.w and 0<=p[1]<self.h) or p in seen:continue
                if p in self.portal_points and p!=target:continue
                t=self.cells[p[1]*self.w+p[0]]&31
                if t not in PASSABLE and not (p==target and t==7):continue
                seen[p]=(x,y);q.append(p)
        if target not in seen:raise AssertionError(('no route',start,target,self.nodes))
        path=[];p=target
        while p!=start:path.append(p);p=seen[p]
        return path[::-1]
    def align(self,axis,target):
        g=self.g
        for _ in range(120):
            p=self.pos()[axis];d=target-p
            if abs(d)<=2:return
            g.hold({('right' if d>0 else 'left') if not axis else ('down' if d>0 else 'up')});g.tick()
        raise AssertionError(('align',axis,target,self.pos()))
    def walk(self,target,portal=False):
        g=self.g;target=tuple(target);warps_before=len(self.warps)
        for _ in range(2*(self.w+self.h)):
            if self.cell()==target:
                self.align(0,target[0]*16+8);self.align(1,target[1]*16+8)
                g.hold(set());g.tick(16)
                if portal:
                    for __ in range(180):
                        if len(self.warps)>warps_before:return
                        g.tick()
                    raise AssertionError(('portal timeout',target,self.pos(),[g.peek('room',i) for i in range(308,320)]))
                return
            path=self.route(target);p=path[0];x,y=self.cell();axis=int(p[1]!=y)
            self.align(1-axis,(y if not axis else x)*16+8)
            oldwarps=len(self.warps)
            dest=p[axis]*16+8
            for __ in range(160):
                at=self.pos()[axis];d=dest-at
                if abs(d)<=2:break
                g.hold({('right' if d>0 else 'left') if not axis else ('down' if d>0 else 'up')});g.tick()
                if g.peek('floor_no')!=7 or g.peek('game_mode')!=2:g.hold(set());return
                if len(self.warps)>oldwarps:
                    g.hold(set());g.tick(24)
                    if portal:return
                    raise AssertionError(('unexpected portal',target,self.warps[-1]))
            else:raise AssertionError(('stuck',self.cell(),p,self.pos(),list(g.pb.memory[g.addr('world_view'):g.addr('world_view')+4])))
        raise AssertionError(('walk limit',target,self.pos()))
    def push(self,crate,dx,dy):
        g=self.g;self.walk((crate[0]-dx,crate[1]-dy));g.hold(set());g.tick(16)
        g.hold({('right' if dx>0 else 'left') if dx else ('down' if dy>0 else 'up')})
        for _ in range(120):
            g.tick()
            if self.cells[crate[1]*self.w+crate[0]]&31 not in (21,22,23):break
        else:raise AssertionError(('push failed',crate,self.pos()))
        g.hold(set());g.tick(18)
    def face_tool(self,direction):
        # Keep the target in global coordinates if a view moves while aligning.
        # The legacy one-room bench driver intentionally uses local coordinates.
        g=self.g;x,y=self.cell();button={'U':'up','D':'down','L':'left','R':'right'}[direction]
        self.align(1,y*16+8);self.align(0,x*16+8)
        for _ in range(4):
            g.hold({button})
            g.tick();g.hold(set());g.tick(2)
            if g.peek('pl_face')=='UDLR'.index(direction):return
        raise AssertionError(('tool facing',direction,g.peek('pl_face')))
    def solve(self,out):
        g=self.g
        for kind,u,v,condition,needs,alts in self.nodes:
            if kind==0:
                if condition>=2:
                    c=self.xy(u-3,v+2);d=self.xy(-1,0);self.push(c,*d)
                if condition!=2:
                    for cv in (v+1,v):self.push(self.xy(u-3,cv),*self.xy(0,-1))
                self.walk(self.xy(u-1,v));self.walk(self.xy(u+2,v))
            elif kind==4:
                self.walk(self.xy(u-2,v),portal=True)
                assert len(self.warps),('portal did not transfer',self.pos())
                self.walk(self.xy(u+2,v+1))
                board=bytes(self.cells);bag=pocket(g)
                self.walk(self.xy(u+2,v),portal=True);self.walk(self.xy(u-3,v))
                self.walk(self.xy(u-2,v),portal=True);self.walk(self.xy(u+2,v+1))
                assert len(self.warps)==3 and pocket(g)==bag
                assert all(a==b for i,(a,b) in enumerate(zip(board,self.cells)) if (a&31) not in (1,6,17)), 'mutated objects after backtracking'
            elif kind in (1,2,3):
                for iu,iv,piece in self.items:
                    if iu==u-5:self.walk(self.xy(iu,iv))
                if kind==2:
                    assert bench_act(g,'Ccg',self.log,prepare_use=self.face_tool),self.log
                    piece=9;target=(u,v)
                elif kind==3:
                    assert bench_act(g,'Cef',self.log,prepare_use=self.face_tool),self.log
                    piece=12;target=(u-3,v-1)
                else:piece=1;target=(u,v)
                self.walk(self.xy(target[0]-1,target[1]))
                assert bench_act(g,'P'+chr(96+piece)+('D' if self.vertical else 'R'),self.log,prepare_use=self.face_tool),self.log
                self.walk(self.xy(u+2,v))
            else:raise AssertionError(('unhandled node',kind))
            g.shot(out/f'goal-{kind}-{u}.png')
        door=next((i%self.w,i//self.w) for i,t in enumerate(self.cells) if t&31==7)
        if self.cells[self.key[1]*self.w+self.key[0]]&31==6:self.walk(self.key)
        self.walk(door)
        return g.peek('floor_no')!=7 or g.peek('game_mode')!=2
    def report(self):
        g=self.g;f=self.fc
        spans=[(f[i]-f[i-120])&65535 for i in range(120,len(f)) if None not in f[i-120:i+1]]
        return {'size':[self.w,self.h],'vertical':bool(self.vertical),'nodes':self.nodes,
                'preparation':list(g.pb.memory[g.addr('world_seams')-12:g.addr('world_seams')]),
                'view':list(g.pb.memory[g.addr('world_view'):g.addr('world_view')+4]),
                'camera':[g.peek('cam_x'),g.peek('cam_y')],
                'incoming_pit_presentation_checks':self.pit_checks,
                'maximum_pending_terrain':self.pool_peak,'maximum_animated_cards':self.live_peak,
                'maximum_stream_wait_frames':self.stream_wait_max,'total_stream_wait_frames':self.stream_wait_total,
                'stream_wait_samples':self.stream_wait_samples,
                'native_heir_uploads':self.pixels.samples if self.pixels else None,
                'heir_pixel_mismatches':self.pixels.errors if self.pixels else [],
                'seams':g.peek16('world_seams'),'windows':len(self.visited),'warps':self.warps,
                'minimum_game_frames_per_120':min(spans) if spans else None,'hitches':self.hitches[:30],
                'missing_floor':self.missing[:30],'missing_body':self.body[:30],
                'oam_max':self.meter.max_line,'oam_over':self.meter.over,'grid':self.grid.report(),
                'stack_canary':bytes(g.pb.memory[0xddd0:0xddf0])==bytes([0xa7]*32)}

def run(rom,out,tool,seeds,rule=None,size=None,biome=0):
    out.mkdir(parents=True,exist_ok=True);reports=[]
    for seed in seeds:
        case=out/f'{seed:04x}';case.mkdir(exist_ok=True)
        g=Game(rom)
        if rule is not None:
            assert g.wait_mode(1,1500)
            g.poke('dbg_world_rule',rule+1);g.poke('dbg_world_size',0 if size is None else size+1);g.poke('dbg_world_biome',biome)
        assert start_run(g,True,True,seed=seed,floor=7);assert g.peek('world_on')
        p=WorldPlayer(g,tool,rom);p.out=case
        try:
            g.tick(160);g.shot(case/'start.png');won=p.solve(case);r=p.report();r['solved']=won;r['seed']=seed
            reports.append(r);print(json.dumps(r,indent=2),flush=True)
        except BaseException:
            g.shot(case/'failure.png');r=p.report();(case/'failure.json').write_text(json.dumps(r,indent=2)+'\n');raise
        finally:g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'cases':reports}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    return all(r['solved'] and not r['missing_floor'] and not r['missing_body'] and not r['oam_over'] and
               not r['heir_pixel_mismatches'] and r['native_heir_uploads']>10 and r['stack_canary'] and
               r['minimum_game_frames_per_120']==120 and r['grid']['all_map_and_attributes_match'] for r in reports)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path)
    p.add_argument('--tool',type=Path,default=Path('build/host/land_one'));p.add_argument('--seeds',nargs='+',type=lambda v:int(v,0),default=[0x5fed])
    p.add_argument('--rule',type=int);p.add_argument('--size',type=int);p.add_argument('--biome',type=int,default=0)
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out,a.tool,a.seeds,a.rule,a.size,a.biome) else 1)
