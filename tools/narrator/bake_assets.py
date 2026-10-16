#!/usr/bin/env python3
"""Rebuild committed narrator banks from the retained WAVs, without fal or credentials.

Use --check for a reproducibility/hash gate. Generation itself is separate in
fal_generate.py; this deterministic asset bake never makes a network request.
"""
import argparse,contextlib,hashlib,io,json,tempfile
from pathlib import Path
from pcm4 import bake

ROOT=Path(__file__).resolve().parents[2]
def main(check):
    folder=ROOT/'assets/narrator/voice';manifest=json.loads((folder/'manifest.json').read_text())
    files={};header=['#ifndef VOICE_ASSETS_H','#define VOICE_ASSETS_H','#include <gb/gb.h>'];total=0
    assert [c['index'] for c in manifest['clips']]==list(range(8))
    with tempfile.TemporaryDirectory(prefix='egress-voice-bake-') as tmp:
        for clip in manifest['clips']:
            source=folder/(clip['id']+'.wav');out=Path(tmp)/clip['id'];i=clip['index']
            assert hashlib.sha256(source.read_bytes()).hexdigest()==clip['source_sha256'],source
            with contextlib.redirect_stdout(io.StringIO()):bake(source,out)
            raw=(out/'voice.pcm4').read_bytes()
            assert hashlib.sha256(raw).hexdigest()==clip['pcm_sha256'],clip['id']
            assert len(raw)==clip['bytes']==clip['blocks']*16 and len(raw)<=16384
            total+=len(raw);name='voice_data_'+str(i)
            files[name+'.c']='#pragma bank 255\n#include <gb/gb.h>\nBANKREF('+name+')\nconst uint8_t '+name+'[]={\n'+''.join('  '+','.join(map(str,raw[k:k+32]))+',\n' for k in range(0,len(raw),32))+'};\n'
            header+=['BANKREF_EXTERN('+name+')','extern const uint8_t '+name+'[];',f'#define VOICE_BLOCKS_{i} {clip["blocks"]}u']
    assert total==manifest['total_pcm_bytes'];files['voice_assets.h']='\n'.join(header+['#endif'])+'\n'
    for name,content in files.items():
        path=ROOT/'src/gen'/name
        if check:assert path.read_text()==content,f'{name} needs rebaking'
        else:path.write_text(content)
    print(f'{"verified" if check else "baked"}: 8 source WAVs, {total} PCM bytes, 8192 Hz / 4 bit')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');main(p.parse_args().check)
