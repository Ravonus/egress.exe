"""Compare uploaded native heir pixels with the baked record and every selected gene.

Read-only CPU hooks observe normal composition/upload. Empty deltas must not
prevent later hair, clothing, marks or weapons from being applied.
"""
import argparse,hashlib,json,re,sys
from pathlib import Path
from heir import boot_heir,ha_states
from return_observer import watch_return

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from bake_heir import SLOTS

class Pixels:
    def __init__(self,g,rom):
        self.g=g;self.rom=rom.read_bytes();self.pending=[];self.active=None;self.returns=set()
        self.samples=0;self.errors=[];self.zero_then_nonzero=0
        a=g.sym['_heir_compose'];g.pb.hook_register(a>>16,a&65535,self.compose,None)
        mapper='mbc3' if self.rom[0x147]==0x10 else 'mbc5'
        symbols=(ROOT/f'build/obj-{mapper}/heir_draw.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*5\s+(_\w+)\s+([0-9A-F]+)\s+\w+',symbols,re.M)}
        base=g.sym['_heir_compose']-offsets['_heir_compose']
        for name in ('_copy128',):
            if name in offsets:g.pb.hook_register(0,base+offsets[name],self.copy,None)
        data={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)\s+\w+',symbols,re.M)}
        self.tile_addr=g.sym['_heir_pending']-data['_heir_pending']+data['_up_tile']
        g.pb.hook_register(0,base+offsets['_heir_vbl'],self.vblank,None)
    def read(self,addr,n):
        bank=addr>>16;off=(bank*16384+(addr&16383)) if bank else addr
        return self.rom[off:off+n]
    def compose(self,_):
        g=self.g;m=g.pb.memory;r=g.pb.register_file
        pose,dd,tile=r.A,r.E,m[r.SP+2]
        rec=self.read(g.sym['_heir_recs']+3*(pose*5+dd),3)
        chunk,lo,hi=rec;addr=g.sym['_heir_data_'+str(chunk)]+lo+256*hi
        blob=self.read(addr,16384-(addr&16383));buf=bytearray(blob[1:129]);p=129
        sh={name:g.peek('heir',off) for name,off in [('look',1),('neck',3),('pat',4),('hair',2),('mark',5),('weapon',7)]}
        zero=False
        for name,value in SLOTS:
            n=blob[p];p+=1;chosen=sh[name]==value
            if not n:
                if chosen:zero=True
                continue
            p+=1
            if chosen:
                if zero:self.zero_then_nonzero+=1
                for k in range(n):
                    row,mask,low,high=blob[p+k*4:p+k*4+4]
                    buf[2*row]=(buf[2*row]&~mask)|low;buf[2*row+1]=(buf[2*row+1]&~mask)|high
            p+=n*4
        self.pending.append((tile,bytes(buf),pose,dd,sh))
    def copy(self,_):
        r=self.g.pb.register_file;m=self.g.pb.memory;dest=(r.D<<8)|r.E
        if not 0x8000<=dest<0x9000:return
        assert self.pending,'native upload without an observed composition'
        self.active=self.pending.pop(0)
        assert dest==0x8000+self.active[0]*16,(dest,self.active[0])
        ret=m[r.SP]|(m[r.SP+1]<<8)
        if ret not in self.returns:
            watch_return(self.g,(0,ret),self.upload);self.returns.add(ret)
    def vblank(self,_):
        g=self.g;m=g.pb.memory;r=g.pb.register_file
        if not g.peek('heir_pending'):return
        assert self.pending,'native VBlank upload without an observed composition'
        self.active=self.pending.pop(0)
        assert m[self.tile_addr]==self.active[0]
        ret=m[r.SP]|m[r.SP+1]<<8
        if ret not in self.returns:
            watch_return(g,(0,ret),self.upload);self.returns.add(ret)
    def upload(self,_):
        if self.active is None:return
        tile,want,pose,dd,sh=self.active;got=bytes(self.g.pb.memory[0,0x8000+tile*16:0x8000+tile*16+128]);self.samples+=1
        if got!=want and len(self.errors)<12:self.errors.append({'pose':pose,'drawn_direction':dd,'shape':sh,'different_bytes':sum(a!=b for a,b in zip(got,want))})
        self.active=None

def run(rom,out,measure=False):
    out.mkdir(parents=True,exist_ok=True);samples=empty_cases=0;errors=[]
    for seed in (6,4,2,18):
        g=boot_heir(rom,seed);obs=Pixels(g,rom)
        for name,state in ha_states().items():
            for direction in range(8):
                g.poke('dbg_heir_state',state+1);g.poke('dbg_heir_dir',direction);g.tick(8)
        samples+=obs.samples;empty_cases+=obs.zero_then_nonzero;errors+=obs.errors;g.stop()
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'native_uploads':samples,'empty_selected_slot_followed_by_visible_gene':empty_cases,'mismatches':errors[:12],'all_pixels_match':not errors and samples>1000 and empty_cases>100}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return measure or report['all_pixels_match']
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);p.add_argument('--measure',action='store_true');a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out,a.measure) else 1)
