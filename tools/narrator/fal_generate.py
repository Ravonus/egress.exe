#!/usr/bin/env python3
"""Offline fal speech assets. Credentials stay server-side; durable jobs are never silently resubmitted."""
import argparse, hashlib, json, os, re, urllib.error, urllib.parse, urllib.request
from pathlib import Path
MODEL='xai/tts/v1'
def api(url,key,body=None):
    if urllib.parse.urlsplit(url).hostname!='queue.fal.run': raise ValueError('unexpected fal API host')
    req=urllib.request.Request(url,data=None if body is None else json.dumps(body).encode(),headers={'Authorization':'Key '+key,'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(req,timeout=45) as r: return json.load(r)
    except urllib.error.HTTPError as e:
        # Server errors cannot contain our request headers. Never print key or environment.
        raise SystemExit(f'fal HTTP {e.code}: {e.read(2048).decode(errors="replace")}') from None

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('catalogue',type=Path);p.add_argument('out',type=Path)
    p.add_argument('--key-file',type=Path);p.add_argument('--voice',default='rex');p.add_argument('--submit',action='store_true');p.add_argument('--limit',type=int,default=1)
    a=p.parse_args();key=os.getenv('FAL_KEY','')
    if not key and a.key_file:
        m=re.search(r'^\s*(?:export\s+)?FAL_KEY\s*=\s*([^\r\n]+)',a.key_file.read_text(),re.M)
        if m: key=m[1].strip().strip('\"\'')
    if not key or len(key)<20 or 'your_' in key.lower(): raise SystemExit('Configure FAL_KEY in the invoking environment or a private --key-file.')
    a.out.mkdir(parents=True,exist_ok=True);count=0
    for line in json.loads(a.catalogue.read_text())['clips']:
        stem=a.out/line['id'];job=stem.with_suffix('.job.json');result=stem.with_suffix('.result.json');wav=stem.with_suffix('.wav')
        body={'text':line['text'],'voice':a.voice,'language':'en','output_format':{'codec':'wav','sample_rate':8000}}
        digest=hashlib.sha256(json.dumps(body,sort_keys=True).encode()).hexdigest()
        if wav.exists(): continue
        if not job.exists():
            if not a.submit: continue
            data=api('https://queue.fal.run/'+MODEL,key,body);job.write_text(json.dumps({'model':MODEL,'input':body,'input_sha256':digest,**data},indent=2)+'\n')
            count+=1;print(json.dumps({'clip':line['id'],'request_id':data['request_id'],'submitted':True}),flush=True)
        data=json.loads(job.read_text())
        if data['input_sha256']!=digest: raise SystemExit(f'{job}: parameters changed; use a new output folder to preserve the original generation.')
        state=api(data['status_url'],key)
        if state['status']=='COMPLETED':
            if state.get('error'): raise SystemExit(f"fal job {data['request_id']}: {state['error']}")
            audio=api(data['response_url'],key);result.write_text(json.dumps(audio,indent=2)+'\n');url=audio['audio']['url']
            host=urllib.parse.urlsplit(url).hostname or ''
            if not host.endswith('.fal.media'): raise ValueError('unexpected generated asset host')
            with urllib.request.urlopen(url,timeout=45) as r: raw=r.read(8*1024*1024)
            if raw[:4]!=b'RIFF' or raw[8:12]!=b'WAVE': raise ValueError('fal did not return the requested WAV')
            wav.write_bytes(raw);print(json.dumps({'clip':line['id'],'ready':True,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}),flush=True)
        else: print(json.dumps({'clip':line['id'],'status':state['status']}),flush=True)
        if count>=a.limit: break
if __name__=='__main__':main()
