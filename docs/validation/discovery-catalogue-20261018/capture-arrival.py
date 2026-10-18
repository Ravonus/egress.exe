from pathlib import Path
import json,sys
sys.path.insert(0,'test/harness')
p=Path('test/harness/camera_tiles.py');s=p.read_text().replace('if len(fc)%4==0:','if True:').replace('duration=68,loop=0','duration=[20 if i%3<2 else 10 for i in range(len(frames))],loop=0')
ns={'__name__':'offscreen_capture','__file__':str(p.resolve())};exec(compile(s,str(p),'exec'),ns)
BaseRim=ns['TerrainRim'];motion={'incomingMotion':[],'offscreenRestorations':0,'departingFrames':[]};frames=[]
class DepartureRim(BaseRim):
 def observe(self):
  super().observe();g=self.g;m=g.pb.memory
  frames.append(g.pb.screen.image.convert('RGB'))
  def read(n,k):
   a=self.addr[n]+k;return m[1,a] if a>=0xd000 else m[a]
  for k in range(8):
   kind=read('_c_kind',k)
   if not kind or read('_c_brk',k):continue
   a=39-2*k;oy=m[0xc000+4*a];ox=m[0xc001+4*a]
   if not oy or not ox:continue
   c=read('_c_cell',k);delta=oy-16-((c//16)*16-g.peek('cam_y'))
   if kind==1 and delta!=0:motion['incomingMotion'].append([len(frames),c,delta])
   if kind==7 and 0<ox<168 and 0<oy<144:motion['departingFrames'].append(len(frames)-1)
  now=bytes(g.peek('look_present',k) for k in range(28));cam=(g.peek('cam_x'),g.peek('cam_y'))
  if hasattr(self,'previous'):
   old,camera=self.previous
   for c in range(224):
    bit=1<<(c&7)
    if now[c>>3]&bit and not old[c>>3]&bit:
     x=(c%16)*16-camera[0];y=(c//16)*16-camera[1]
     if x+16<=0 or x>=160 or y+16<=0 or y>=128:motion['offscreenRestorations']+=1
  self.previous=(now,cam)
ns['TerrainRim']=DepartureRim
out=Path('build/sprints/offscreen-arrival-native');ok=ns['run'](Path('build/egress.gbc'),out)
r=json.loads((out/'report.json').read_text());r['results']['no_moving_incoming_cards_during_play']=not motion['incomingMotion'];r['results']['background_restored_offscreen']=motion['offscreenRestorations']>0
r.update(source='Ordinary camera_tiles controller route, actual native frames, no generated or interpolated pictures. Incoming background readiness and the retained departure are observed independently; GIF timing alternates 20/20/10 ms for 60 Hz.',offscreenPreparation=motion)
(out/'report.json').write_text(json.dumps(r,indent=2)+'\n')
if motion['departingFrames']:
 a=max(0,motion['departingFrames'][0]-12);b=min(len(frames),motion['departingFrames'][0]+50)
 frames[a].save(out/'departure.gif',save_all=True,append_images=frames[a+1:b],duration=[20 if i%3<2 else 10 for i in range(b-a)],loop=0)
print(json.dumps({'cameraPassed':ok,'results':r['results'],'offscreenRestorations':motion['offscreenRestorations']}),flush=True)
assert ok and all(r['results'].values()),r['results']
