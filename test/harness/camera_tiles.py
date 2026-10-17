"""Native travel through an explicit push motif; pickups/input are real, with no board or position writes."""
import argparse,hashlib,json,re
from pathlib import Path
from play import Game,start_run,pocket
from gameplay_rebuild import walk
from heir import OamMeter
from terrain_rim import TerrainRim

def present(g,c):return bool(g.peek('look_present',c>>3)&(1<<(c&7)))

def run(rom,out,measure=False):
    out.mkdir(parents=True,exist_ok=True);g=Game(rom);assert start_run(g,True,True,physical_fixture=1);g.tick(40)
    log=[];r={};meter=OamMeter(g);rim=TerrainRim(g,out);tick=g.tick;frames=[];fc=[];missing=[];body_missing=[];moves=set()
    symbols=(Path(__file__).resolve().parents[2]/'build/obj/heir.sym').read_text()
    offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)',symbols,re.M)}
    mask_addr=g.addr('ha_dir')-offsets['_ha_dir']+offsets['_spr_mask']
    def record(n=1):
        for _ in range(n):
            previous=g.pb.memory[1,mask_addr].bit_count()
            tick();rim.observe();fc.append(g.peek16('frame_count'))
            moves.add(g.peek('pin',2))
            x,y=g.player();c=(y//16)*16+x//16
            if g.peek('game_mode')==2 and not g.peek('pl_air') and g.peek('pl_state')==0 and not present(g,c):missing.append((len(fc),c,x,y))
            # Some complete poses intentionally use fewer than four layers.
            # OAM publishes at VBlank, on either side of the sampled update.
            expected=g.pb.memory[1,mask_addr].bit_count()
            actual=sum(bool(g.pb.memory[0xfe00+4*k]) and g.pb.memory[0xfe02+4*k]<16 and not(g.pb.memory[0xfe03+4*k]&8) for k in range(40))
            if actual not in (previous,expected) or not actual:body_missing.append((len(fc),actual,previous,expected))
            if len(fc)%4==0:frames.append(g.pb.screen.image.convert('RGB'))
    g.tick=record;g.pb.memory[1,0xddd0:0xddf0]=[0xa7]*32
    pickup=178;left=194;right=205
    assert walk(g,pickup,log);g.hold(set());g.tick(30)
    bag=pocket(g);r['real_ingredient_collected']=g.room()[pickup]==0 and bag==[4]
    crate=next(i for i,v in enumerate(g.room()[:224]) if v==21)
    assert walk(g,crate-1,log);g.hold({'right'})
    for _ in range(100):
        g.tick()
        if g.room()[crate]!=21:break
    g.hold(set());g.tick(12)
    r['crate_moved_by_normal_push']=g.room()[crate]==0 and g.room()[crate+1]==21
    assert walk(g,left,log);g.hold(set());g.tick(40)
    board=bytes(g.room()[:224]);r['starting_floor_present']=present(g,left)
    assert walk(g,right,log);g.hold(set());g.tick(100)
    r['camera_travel_exposes_east']=g.peek('cam_x')>=90
    r['floor_arrives_to_east']=present(g,right)
    r['floor_behind_camera_collapses']=not present(g,left)
    r['travel_preserves_puzzle_board']=bytes(g.room()[:224])==board
    g.shot(out/'east.png')
    assert walk(g,left,log);g.hold(set());g.tick(100)
    r['camera_returns_west']=g.peek('cam_x')<8
    r['old_floor_reassembles']=present(g,left)
    r['east_floor_collapses_on_return']=not present(g,right)
    r['collected_item_does_not_respawn']=g.room()[pickup]==0 and pocket(g)==bag
    r['board_mutations_survive_backtracking']=bytes(g.room()[:224])==board
    r['moved_crate_stays_moved']=g.room()[crate]==0 and g.room()[crate+1]==21
    # Two-axis camera travel and an immediate reversal, through ordinary input.
    assert walk(g,181,log);x0,y0=g.player()
    g.hold({'up','right'});g.tick(32);x1,y1=g.player()
    g.hold({'down','left'});g.tick(24);x2,y2=g.player();g.hold(set());g.tick(20)
    r['real_diagonal_travel_and_reversal']=x1-x0>16 and y0-y1>16 and x1-x2>16 and y2-y1>16
    assert walk(g,200,log)
    reversal_board=bytes(g.room()[:224]);reversal_bag=pocket(g)
    g.hold({'right'});g.tick(2);g.hold(set());g.tick(22)
    g.hold({'right'});g.tick(2);g.hold(set());g.tick(2);g.hold({'right'});g.tick(12)
    g.hold({'left'});g.tick(10);g.hold(set());g.tick(2);g.hold({'left'});g.tick(12)
    g.hold(set());g.tick(80)
    r['real_directional_evade_used']=1 in moves
    r['fast_reversal_keeps_mutations']=bytes(g.room()[:224])==reversal_board and pocket(g)==reversal_bag
    r['floor_under_feet_stays_present']=not missing
    r['complete_body_retained']=not body_missing
    r['sprite_line_budget']=meter.over==0
    r['floor_stack_canary']=bytes(g.pb.memory[1,0xddd0:0xddf0])==bytes([0xa7]*32)
    spans=[(fc[i]-fc[i-120])&65535 for i in range(120,len(fc))]
    r['movement_frames_120_per_120']=bool(spans) and min(spans)==120
    terrain=rim.report();r['construction_stays_at_rim']=terrain['construction_stays_at_rim'];r['central_view_is_built']=terrain['central_view_is_built']
    r['both_edge_effects_are_visible']=terrain['build_and_fall_are_visible']
    g.shot(out/'returned.png');g.stop()
    if frames:frames[0].save(out/'camera-travel.gif',save_all=True,append_images=frames[1:],duration=68,loop=0)
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':r,'frames':len(fc),'minimum_game_frames_per_120':min(spans) if spans else None,'oam_max':meter.max_line,'missing_floor':missing[:20],'missing_body':body_missing[:20],'log':log}
    report['terrain_rim']=terrain
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return measure or all(r.values())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);p.add_argument('--measure',action='store_true')
    a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out,a.measure) else 1)
