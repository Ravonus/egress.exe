#!/usr/bin/env python3
"""Bake a mono WAV into CGB wave-channel PCM, with silence trimming and bounded 4-bit samples."""
import argparse,hashlib,json,wave
from pathlib import Path
import numpy as np

def bake(source,out):
    with wave.open(str(source),'rb') as w:
        assert w.getnchannels()==1 and w.getsampwidth()==2
        rate=w.getframerate();x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(np.float64)/32768
    # DC removal, high/low-pass in the frequency domain before resampling.
    x-=x.mean();spec=np.fft.rfft(x);freq=np.fft.rfftfreq(len(x),1/rate)
    spec*=np.minimum(1,(freq/110)**2)/(1+(freq/3400)**8);x=np.fft.irfft(spec,n=len(x))
    live=np.flatnonzero(np.abs(x)>.012)
    if len(live):x=x[max(0,live[0]-rate//25):min(len(x),live[-1]+rate//15)]
    n=round(len(x)*8192/rate);x=np.interp(np.arange(n)*rate/8192,np.arange(len(x)),x)
    peak=np.quantile(np.abs(x),.999);x=np.clip(x/max(peak,1e-6)*.86,-1,1)
    # Wave RAM is unsigned 4-bit; light error feedback retains quiet consonants.
    q=np.empty(n,dtype=np.uint8);error=0.
    for i,v in enumerate(x):
        y=(v+1)*7.5+.65*error;value=int(np.clip(np.rint(y),0,15));error=y-value;q[i]=value
    if len(q)%32:q=np.pad(q,(0,32-len(q)%32),constant_values=8)
    raw=bytes((q[::2]<<4)|q[1::2]);out.mkdir(parents=True,exist_ok=True)
    (out/'voice.pcm4').write_bytes(raw)
    decoded=((q.astype(np.int16)-8)*4096).astype('<i2')
    with wave.open(str(out/'cartridge-preview.wav'),'wb') as w:
        w.setparams((1,2,8192,0,'NONE','not compressed'));w.writeframes(decoded.tobytes())
    report={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'pcm_sha256':hashlib.sha256(raw).hexdigest(),
      'sample_rate':8192,'bits':4,'bytes':len(raw),'duration':len(q)/8192,'bytes_per_second':4096,'blocks':len(raw)//16,'source_bytes':source.stat().st_size}
    (out/'ledger.json').write_text(json.dumps(report,indent=2)+'\n')
    c='#pragma bank 2\n#include <gb/gb.h>\nconst uint8_t test_voice[]={\n'+''.join('  '+','.join(str(v) for v in raw[i:i+32])+',\n' for i in range(0,len(raw),32))+'};\n'
    c+=f'const uint16_t test_voice_blocks={len(raw)//16}u;\n';(out/'voice.c').write_text(c)
    print(json.dumps(report,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('out',type=Path);a=p.parse_args();bake(a.source,a.out)
