"""Observe exact HUD rows, attributes, bank restoration and native ISR cost."""
import re
from pathlib import Path
class HudVblank:
    def __init__(self,g,rom):
        self.g=g;self.pending=None;self.returns=set();self.samples=0;self.rows=0;self.maximum_cycles=0;self.errors=[]
        sym=(Path(__file__).resolve().parents[2]/'build/obj/hud.sym').read_text()
        ss={m[2]:(int(m[1]),int(m[3],16)) for m in re.finditer(r'^\s*(\d+)\s+(_\w+)\s+([0-9A-F]+)\s+',sym,re.M)}
        assert ss['_hud_vbl'][0]==ss['_hud_msg'][0]
        at=g.addr('hud_msg')-ss['_hud_msg'][1]+ss['_hud_vbl'][1]
        code=rom.read_bytes()[at:at+12];j=code.index(b'\xfa');rows=int.from_bytes(code[j+1:j+3],'little')
        assert 0xc000<=rows<0xd800
        data=rows-ss['_vbl_rows'][1];self.addr={k:data+v for k,(area,v) in ss.items() if area==ss['_vbl_rows'][0]}
        g.pb.hook_register(0,at,self.enter,None)
    def read(self,a,n=1):
        m=self.g.pb.memory
        return list(m[1,a:a+n] if a>=0xd000 else m[a:a+n])
    def enter(self,_):
        g=self.g
        if g.peek('game_mode')!=2:return
        assert self.pending is None
        m=g.pb.memory;r=g.pb.register_file;ret=m[r.SP]|m[r.SP+1]<<8
        self.pending={'start':g.pb._cycles(),'rows':self.read(self.addr['_vbl_rows'])[0],
            'move':self.read(self.addr['_vbl_move'])[0],'top':self.read(self.addr['_vbl_top'])[0],
            'sh':self.read(self.addr['_sh'],60),'base':0x9c00 if m[0xff40]&64 else 0x9800,
            'vbk':m[0xff4f],'svbk':m[0xff70]&7,'ret':ret}
        assert ret<0x4000
        if ret not in self.returns:self.returns.add(ret);g.pb.hook_register(0,ret,self.leave,ret)
    def leave(self,ret):
        if not self.pending or self.pending['ret']!=ret:return
        d=self.pending;self.pending=None;g=self.g;m=g.pb.memory;self.samples+=1;bad=[]
        self.maximum_cycles=max(self.maximum_cycles,g.pb._cycles()-d['start'])
        for row in range(3):
            if d['rows']&(1<<row):
                self.rows+=1;a=d['base']+32*row
                if list(m[0,a:a+20])!=d['sh'][20*row:20*(row+1)]:bad.append('row-'+str(row))
        if d['rows'] and d['move']:
            for row in (0,1):
                want=14 if row==d['top'] else 13;a=d['base']+32*row
                if list(m[1,a:a+3])!=[want]*3:bad.append('attributes-'+str(row))
        if d['move'] and (m[0xff4a]!=(120 if d['top'] else 128) or m[0xff4b]!=7):bad.append('window-position')
        if m[0xff4f]!=d['vbk'] or (m[0xff70]&7)!=d['svbk'] or d['svbk']!=1:bad.append('bank-restoration')
        if bad and len(self.errors)<12:self.errors.append({'frame':g.frames,'fields':bad})
    def report(self):
        return {'callbacks':self.samples,'rows_checked':self.rows,'maximum_callback_cycles':self.maximum_cycles,
                'mismatches':self.errors,'native_hud_is_exact_and_bounded':self.samples>120 and self.rows>10 and not self.errors and self.maximum_cycles<6000}
