"""Input-driven portal/circuit chain, its persistent objects and native item uploads."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
from play import Game,start_run,room_command,drive_plan,step,bench_act,pocket,MODE_PLAY
from heir import OamMeter
from grid_observer import GridObserver

class ItemObserver:
    def __init__(self,g):
        self.g=g;self.pending=None;self.returns=set();self.calls=0;self.pieces=set();self.phases=set()
        root=Path(__file__).resolve().parents[2]
        text=(root/'build/obj/items.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*\d+\s+(_\w+)\s+([0-9A-F]+)\s+\w+',text,re.M)}
        self.base=(g.sym['_items_tick']+offsets['_item_idle']-offsets['_items_tick'])&65535
        self.item0=int(re.search(r'#define M_ITEM0 (\d+)u',(root/'src/gen/look_art.h').read_text())[1])
        at=g.sym['_set_bkg_data'];g.pb.hook_register(at>>16,at&65535,self.enter,None)
    def enter(self,_):
        g=self.g;m=g.pb.memory;sp=g.pb.register_file.SP
        # GBDK's library routine uses sdcccall(0), all arguments on the stack.
        t,n=m[sp+2],m[sp+3];src=m[sp+4]|m[sp+5]<<8
        if n!=4 or not self.base<=src<self.base+4096:return
        off=src-self.base
        if off&63:return
        piece=(off%1024)//64+1;phase=off//1024
        want=g.peek('meta_tile',(self.item0+piece-1)*4)
        assert t==want,{'item':piece,'actual_tile':t,'mapped_tile':want}
        ret=m[sp]|m[sp+1]<<8;bank=m[g.sym['__current_bank']] if 0x4000<=ret<0x8000 else 0
        key=bank,ret;assert self.pending is None
        self.pending=(key,sp+2,0x9000+t*16,bytes(m[src:src+64]),piece,phase)
        if key not in self.returns:self.returns.add(key);g.pb.hook_register(*key,self.leave,key)
    def leave(self,key):
        if self.pending is None or self.pending[0]!=key:return
        _,sp,at,want,piece,phase=self.pending;g=self.g
        if g.pb.register_file.SP!=sp:return
        assert bytes(g.pb.memory[0,i] for i in range(at,at+64))==want,{'item_pixels':piece,'phase':phase}
        self.calls+=1;self.pieces.add(piece);self.phases.add(phase);self.pending=None
    def report(self):
        return {'uploads':self.calls,'pieces':sorted(self.pieces),'poses':sorted(self.phases),
                'mapped_destination_and_pixels_match':self.calls>10 and len(self.pieces)>=3 and len(self.phases)>=3 and self.pending is None}

def run(rom,out,tool):
    out.mkdir(parents=True,exist_ok=True);reports=[]
    for seed in (0x5EEC,0x5ECC):
        g=Game(rom);assert start_run(g,True,True,seed=seed,floor=7,physical_fixture=4)
        assert g.peek('slice',22)&31==4
        host=subprocess.check_output(room_command(g,tool,'dump')+['full'])
        assert len(host)==320 and bytes(g.peek('room',i) for i in range(320))==host
        meter=OamMeter(g);grid=GridObserver(g);items=ItemObserver(g);log=[];warps=[];snapshots=[]
        tick=g.tick;last=g.player();ports=list(host[317:319]);active=[];missing=[];hitches=[];prior_fc=g.peek16('frame_count')
        g.pb.memory[1,0xDE78:0xDE98]=[0xA7]*32
        def record(n=1):
            nonlocal last,prior_fc
            for _ in range(n):
                before=g.peek('bench_on');mode=g.peek('game_mode');state=g.peek('pl_state');tick()
                pos=g.player();c=(pos[1]//16)*16+pos[0]//16
                fc=g.peek16('frame_count')
                if (fc-prior_fc)&65535!=1:hitches.append((g.frames,(fc-prior_fc)&65535,before,g.peek('bench_on'),g.peek('pl_state')))
                prior_fc=fc
                if mode==MODE_PLAY and g.peek('game_mode')==MODE_PLAY and state!=5 and g.peek('pl_state')!=5 and not before and not g.peek('bench_on'):
                    active.append(g.peek16('frame_count'))
                    if not g.peek('pl_air') and g.peek('pl_state') not in (5,6) and not (g.peek('look_present',c>>3)&(1<<(c&7))):missing.append((g.frames,c,g.peek('pl_state'),g.peek('cam_x'),g.peek('cam_y')))
                else:active.append(None)
                if abs(pos[0]-last[0])+abs(pos[1]-last[1])>64 and c in ports:
                    warps.append({'from':last,'to':pos,'board':list(g.room()[:224])})
                    g.shot(out/f'{seed:04x}-portal-{len(warps)}.png')
                last=pos
        g.tick=record;g.tick(180);g.shot(out/f'{seed:04x}-start.png')
        approach='crates' if seed==0x5EEC else 'tools'
        plan=subprocess.check_output(room_command(g,tool,'solve')+[approach],text=True).strip();assert plan
        backtracked=False
        def capture(_):
            nonlocal backtracked
            if warps and not backtracked:
                before_board=bytes(g.room()[:224]);before_bag=pocket(g)
                for d in 'UDDU':assert step(g,d,log)
                assert len(warps)==3 and bytes(g.room()[:224])==before_board and pocket(g)==before_bag
                backtracked=True
            snapshots.append(bytes(g.room()[:224]))
        drive_plan(g,plan,log,after_act=capture)
        # The last act exits through the real floor transition; its collapse
        # intentionally takes the floor away and is outside the active frame gate.
        won=g.peek('floor_no')!=7 or g.peek('game_mode')!=MODE_PLAY
        assert not log and won,{'seed':seed,'log':log,'position':g.player(),'plan':plan}
        assert warps and len(warps)<=4
        entry_gate=host[315];source_a=host[309]
        assert any(w['board'][entry_gate]==11 and w['board'][source_a]==22 for w in warps)
        assert any(b[entry_gate]==12 for b in snapshots)
        spans=[(active[i]-active[i-120])&65535 for i in range(120,len(active)) if None not in active[i-120:i+1]]
        item_report=items.report();grid_report=grid.report()
        results={'native_generator_matches_all_320_bytes':True,'solved_by_real_input':won,'independent_portal_and_gate':True,
                 'portal_arrival_does_not_rebound':True,'floor_under_feet_present':not missing,
                 'native_bg_reference':grid_report['all_map_and_attributes_match'],
                 'native_item_uploads':item_report['mapped_destination_and_pixels_match'],
                 'sprite_line_budget':meter.over==0,'active_frames_120_per_120':bool(spans) and min(spans)==120,
                 'stack_canary':bytes(g.pb.memory[1,0xDE78:0xDE98])==bytes([0xA7]*32)}
        results['backtracking_keeps_objects_and_inventory']=backtracked
        reports.append({'seed':seed,'approach':approach,'results':results,'plan':plan,'warps':len(warps),'missing_floor':missing[:20],
                        'minimum_game_frames_per_120':min(spans) if spans else None,'hitches':hitches,'oam_max':meter.max_line,'items':item_report,'grid':grid_report})
        g.stop()
    # Fuse both FIRE units, then AIR: the ENERGY shard spends both HOT units.
    # The normal sliced checker
    # must identify the cut and offer the existing token reset, without a warp.
    g=Game(rom);assert start_run(g,True,True,seed=0x5EEC,floor=7,physical_fixture=4)
    log=[];cells=bytes(g.peek('room',i) for i in range(320));start=cells[224]
    direction='R' if start%16<8 else 'L'
    for _ in range(5):assert step(g,direction,log)
    assert bench_act(g,'Caa',log) and bench_act(g,'Ccy',log),log
    g.tick(260);assert g.peek('dead_t')==1,{'wrong_spend_pocket':pocket(g),'cue':g.peek('dead_t')}
    tokens=g.peek('tokens');g.shot(out/'wrong-spend-reset-cue.png')
    g.hold({'select'});g.tick(62);g.hold(set());g.tick(240)
    after=bytes(g.peek('room',i) for i in range(320))
    assert after==cells and not pocket(g) and g.peek('tokens')==max(0,tokens-1),{'tokens_before':tokens,'tokens_after':g.peek('tokens'),'bag':pocket(g),'changed':[(i,a,b) for i,(a,b) in enumerate(zip(cells,after)) if a!=b][:20],'mode':g.peek('game_mode')}
    g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'cases':reports}
    report['wrong_spend_shows_reset_and_restores_entry']=True
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return all(all(c['results'].values()) for c in reports)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);p.add_argument('--tool',type=Path,default=Path('build/host/egtool'))
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out,a.tool) else 1)
