"""Clock maintenance must preserve elapsed VBlanks when rendering takes priority."""
import re
from pathlib import Path
class ClockProgress:
    def __init__(self,g,rom):
        self.g=g;mapper='mbc3' if rom.read_bytes()[0x147]==0x10 else 'mbc5'
        sym=(Path(__file__).resolve().parents[2]/f'build/obj-{mapper}/clock.sym').read_text()
        offsets={m[1]:int(m[2],16) for m in re.finditer(r'^\s*2\s+(_\w+)\s+([0-9A-F]+)',sym,re.M)}
        self.last_addr=g.addr('clk_now')-offsets['_clk_now']+offsets['_last_vbl']
        self.last=self.last_vbl();self.origin=self.value();self.elapsed=0;self.maximum_lag=0;self.samples=0;self.errors=[]
    def last_vbl(self):
        m=self.g.pb.memory;a=self.last_addr;return (m[1,a]|m[1,a+1]<<8) if a>=0xD000 else (m[a]|m[a+1]<<8)
    def value(self):
        a=self.g.addr('clk_now');b=bytes(self.g.pb.memory[1,a:a+9] if a>=0xD000 else self.g.pb.memory[a:a+9])
        return ((int.from_bytes(b[:2],'little')*86400+int.from_bytes(b[2:4],'little')*60+b[4])<<18)+int.from_bytes(b[5:9],'little')
    def observe(self):
        if self.g.frames%60:return
        now=self.last_vbl();self.elapsed+=(now-self.last)&65535;self.last=now;self.samples+=1
        lag=(self.g.peek16('sys_time')-now)&65535;self.maximum_lag=max(self.maximum_lag,lag)
        if self.value()-self.origin!=self.elapsed*4389 and len(self.errors)<8:self.errors.append(self.g.frames)
    def report(self):
        return {'samples':self.samples,'maximum_poll_lag_vblanks':self.maximum_lag,'arithmetic_mismatches':self.errors,
                'elapsed_vblanks_preserved':self.samples>2 and not self.errors and self.maximum_lag<=128}
