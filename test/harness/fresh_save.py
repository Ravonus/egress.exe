"""Prior cartridges start fresh; current-epoch journals still resume normally."""
import argparse,base64,hashlib,json
from pathlib import Path
from play import Game,start_run,pocket
from resume_game import FAMILY,CONT,newest,seal

def run(previous,rom,out):
    out.mkdir(parents=True,exist_ok=True);cases=[]
    reference=Game(rom);assert start_run(reference,True,True,seed=0x61a1)
    fresh_gene=bytes(reference.peek('family',k) for k in range(19));fresh_board=bytes(reference.room());reference.stop()
    def check(saved,label):
        boot=Game(rom,sram=saved);assert boot.wait_mode(1,1500)
        no_continue=not boot.peek('resume_ready');untouched=boot.stop()
        regions=[(0,64),(0x40,512),*( (p,294) for p in FAMILY)]
        preserved=all(untouched[p:p+n]==saved[p:p+n] for p,n in regions)
        g=Game(rom,sram=saved);assert start_run(g,True,True,seed=0x61a1)
        gene=bytes(g.peek('family',k) for k in range(19));seed=g.peek16('run_seed')
        results={'old_continue_rejected':no_continue,'boot_does_not_rewrite_old_progress':preserved,
                 'fresh_generation_and_history':g.peek16_at('family',279)==1 and g.peek('family',284)==0,
                 'old_run_progress_rejected':g.peek16('meta')==0 and not g.peek('meta_loaded') and g.peek('floor_no')==1,
                 'fresh_inventory_and_mods':pocket(g)==[] and not g.peek('mods') and not g.peek('wmods') and not g.peek('kills_run'),
                 'matches_a_blank_save':gene==fresh_gene and bytes(g.room())==fresh_board,
                 'tutorial_starts_uncompleted':not g.peek16('tutorial_seen')}
        g.shot(out/(label+'-fresh.png'));current=g.stop()
        results['new_family_and_continue_versions']=any(current[p:p+3]==b'FA\x03' for p in FAMILY) and any(current[p]==2 for p in CONT)
        again=Game(rom,sram=current);assert start_run(again,True,True,seed=None)
        results['current_save_resumes']=again.peek16('run_seed')==seed and bytes(again.peek('family',k) for k in range(19))==gene and again.peek('floor_no')==1
        again.stop();cases.append({'case':label,'results':results})
    for floor,label in [(1,'opening'),(2,'survival'),(7,'spatial')]:
        old=Game(previous);assert start_run(old,True,True,seed=0x5fed,floor=floor)
        check(old.stop(),label)
    # Actual combat creates completed family/meta progress before rejection.
    old=Game(previous);assert start_run(old,False,False,seed=0xb222,floor=2)
    for _ in range(12000):
        old.tick()
        if old.peek('game_mode')==4:break
    assert old.peek('game_mode')==4 and old.peek16('meta')==1 and old.peek16_at('family',279)==2
    completed=old.stop();check(completed,'completed-family')
    (out/'previous-browser-fixture.json').write_text(json.dumps({'previousSha256':hashlib.sha256(previous.read_bytes()).hexdigest(),'battery':base64.b64encode(completed).decode(),'generation':2,'source':'Actual previous-ROM enemy death and completed family journal.'})+'\n')
    # The older overlapping family format is an explicit rejected-save fixture.
    earlier=bytearray(completed);p=newest(completed,FAMILY,3);record=bytearray(completed[p:p+294]);record[2]=1;seal(record)
    for p in FAMILY:earlier[p]=0
    for p in CONT:earlier[p]=0
    earlier[0x300:0x300+294]=record;check(bytes(earlier),'family-v1')
    report={'previous_sha256':hashlib.sha256(previous.read_bytes()).hexdigest(),'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'cases':cases}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return all(all(c['results'].values()) for c in cases)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('previous','rom','out'):p.add_argument(n,type=Path)
    a=p.parse_args();raise SystemExit(0 if run(a.previous,a.rom,a.out) else 1)
