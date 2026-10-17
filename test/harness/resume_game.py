"""Native continue/migration acceptance. Gameplay actions are controller input; corruption and transaction cuts are explicit save fixtures."""
import argparse,hashlib,json,sys
from pathlib import Path
from play import Game,start_run,step,solve_here,pocket,MODE_TITLE
from gameplay_rebuild import walk
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from rtc_test import crc16

CONT=(0x40,0x140)
FAMILY=(0x570,0xa6c)

def newest(save,slots,offset):
    a,b=slots
    return a if ((save[a+offset]-save[b+offset])&255)<128 else b

def seal(block):
    block[-2:]=crc16(block[:-2]).to_bytes(2,'little')
    return block

def run(rom,out,tool):
    out.mkdir(parents=True,exist_ok=True);res={};log=[]
    g=Game(rom);assert start_run(g,True,False,physical_fixture=1)
    for d in 'URDL':assert step(g,d,log)
    solve_here(g,tool,log);g.tick(240)
    assert g.peek('floor_no')==2
    walk(g,g.peek('stg',8),log);g.poke('dbg_god',0);initial=g.peek('pl_hp')
    for _ in range(6000):
        g.tick()
        if g.peek('pl_hp')<initial:break
    hurt=g.peek('pl_hp');res['real_enemy_damage_checkpointed']=0<hurt<initial
    gene=bytes(g.peek('family',k) for k in range(19));generation=g.peek16_at('family',279)
    g.tick(5);save=g.stop()
    g=Game(rom,sram=save);assert start_run(g,True,False)
    res['same_floor_after_poweroff']=g.peek('floor_no')==2
    res['poweroff_does_not_heal']=g.peek('pl_hp')==hurt
    res['entry_inventory_and_heir_restored']=pocket(g)==[4,2] and bytes(g.peek('family',k) for k in range(19))==gene
    res['wave_restarts_as_uncleared']=not g.peek('stage_won') and g.peek('stage_pct')<100
    g.shot(out/'continued-floor.png')
    g.poke('dbg_god',0);walk(g,g.peek('stg',8),log);g.poke('pl_hp',1)
    for _ in range(9000):
        g.tick()
        if g.peek('game_mode')==4:break
    res['death_recorded_once']=g.peek('game_mode')==4 and g.peek16('meta')==1 and g.peek16_at('family',279)==generation+1
    res['death_clears_continue']=g.peek('resume_state')==0
    dead_save=g.stop()
    # Interruption after the durable end marker, after meta, or after family writes.
    # Every recovery must finish the transaction exactly once.
    at=newest(save,CONT,1);marker=seal(bytearray(save[at:at+256]))
    marker[1]=(marker[1]+1)&255;marker[2]=3;marker[64]=0;seal(marker)
    for cut in ('before_meta','after_meta','after_family'):
        torn=bytearray(save)
        if cut!='before_meta':torn[:64]=dead_save[:64]
        if cut=='after_family':
            for pos in FAMILY:torn[pos:pos+294]=dead_save[pos:pos+294]
        target=CONT[1] if at==CONT[0] else CONT[0];torn[target:target+256]=marker
        g=Game(rom,sram=bytes(torn));started=start_run(g,True,True)
        assert started,(cut,g.peek('game_mode'),g.peek('floor_no'),g.peek16('meta'),g.peek16_at('family',279),g.peek('family',283),g.peek('resume_state'))
        res['end_transaction_'+cut]=g.peek16('meta')==1 and g.peek16_at('family',279)==generation+1 and g.peek('family',284)==1
        g.stop()
    # Make a byte-for-byte FA1 checkpoint journal in its old overlapping slot.
    old=bytearray(save);pos=newest(save,FAMILY,3);legacy=bytearray(save[pos:pos+294]);legacy[2]=1;seal(legacy)
    for pos in FAMILY:old[pos]=0
    for pos in CONT:old[pos]=0
    old[0x300:0x300+294]=legacy
    g=Game(rom,sram=bytes(old));assert start_run(g,True,True)
    res['checkpoint_family_migrated']=bytes(g.peek('family',k) for k in range(19))==gene and g.peek16_at('family',279)==generation
    migrated=g.stop()
    res['new_family_regions_versioned']=any(migrated[pos:pos+3]==b'FA\x02' for pos in FAMILY)
    res['legacy_meta_untouched_by_migration']=migrated[:64]==old[:64]
    res['clock_regions_preserved']=migrated[0x14e0:0x1500]==old[0x14e0:0x1500]
    # Invalid newest floor record must leave the preceding valid checkpoint usable.
    damaged=bytearray(save);pos=newest(save,CONT,1);damaged[pos+100]^=128
    g=Game(rom,sram=bytes(damaged));assert start_run(g,True,False)
    res['torn_continue_falls_back']=g.peek('floor_no')==2 and pocket(g)==[4,2]
    g.stop()
    # Explicit floor-three fixture; solve the real drop puzzle, then use the actual talk/trade/rest controls.
    g=Game(rom);assert start_run(g,True,True,floor=3,physical_fixture=2);g.poke('tokens',40)
    solve_here(g,tool,log);g.tick(220)
    res['landing_entry_checkpoint']=g.peek('floor_no')==3 and g.peek('resume_state')==2 and not g.peek('resume_rest')
    at_landing=g.stop()
    def enter_landing(saved):
        z=Game(rom,sram=saved);assert z.wait_mode(MODE_TITLE);z.tick(30)
        for _ in range(80):
            z.press('start',3);z.tick(16)
            if z.peek('game_mode')==3 and z.peek('floor_no')==3:break
        assert z.peek('game_mode')==3 and z.peek('floor_no')==3
        z.tick(80);return z
    g=enter_landing(at_landing)
    res['landing_resumes_at_same_conversation']=not g.peek('resume_rest') and g.peek('tokens')==40
    g.press('a',4);g.tick(45);g.press('a',4);g.tick(30)
    bought=(g.peek('tokens'),g.peek('mods'))
    res['real_trade_spends_tokens']=bought[0]<40 and bought[1]>0
    g.press('b',4);g.tick(80)
    post_trade_truth=bytes(g.peek('truth',k) for k in range(9))
    res['post_trade_rest_checkpoint']=g.peek('resume_rest')==1 and g.peek('resume_state')==2
    g.shot(out/'saved-landing.png');rest_save=g.stop()
    g=enter_landing(rest_save)
    res['rest_does_not_repeat_trade_or_dialogue']=g.peek('resume_rest')==1 and (g.peek('tokens'),g.peek('mods'))==bought and bytes(g.peek('truth',k) for k in range(9))==post_trade_truth
    g.press('a',4);assert g.wait_mode(2,1800);g.tick(40)
    res['landing_continue_reaches_next_floor']=g.peek('floor_no')==4 and (g.peek('tokens'),g.peek('mods'))==bought
    g.shot(out/'continued-after-landing.png');g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'results':res,'log':log}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return all(res.values()) and not log

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('rom',type=Path);ap.add_argument('out',type=Path)
    ap.add_argument('--tool',type=Path,default=Path('build/host/egtool'));a=ap.parse_args()
    raise SystemExit(0 if run(a.rom,a.out,a.tool) else 1)
