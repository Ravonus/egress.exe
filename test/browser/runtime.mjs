// Current-ROM crafting and sprite animation through native-timed DOM keyboard input.
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
const {chromium}=await import(pathToFileURL(resolve(process.env.EGRESS_SITE_ROOT??'../wt/egress-site','node_modules/@playwright/test/index.mjs')).href);
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const base=process.argv[2]||'http://127.0.0.1:3241',romRoot=process.argv[3]||'build';
const expected=createHash('sha256').update(await readFile(romRoot+'/egress.gbc')).digest('hex');
const noi=await readFile(romRoot+'/egress.noi','utf8'),sym=Object.fromEntries([...noi.matchAll(/DEF (\S+) 0x([0-9A-Fa-f]+)/g)].map(m=>[m[1],parseInt(m[2],16)]));
const fixture=JSON.parse(await readFile(process.argv[4]||'build/sprints/current-browser-fixture.json','utf8'));
assert.equal(fixture.sha256,expected);
const out='build/sprints/browser-bench';await mkdir(out,{recursive:true});
const browser=process.env.EGRESS_BROWSER_WS?await chromium.connect(process.env.EGRESS_BROWSER_WS):await chromium.launch({args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
try {
 const page=await browser.newPage({viewport:{width:320,height:600}}),errors=[],loads=[];
 page.on('pageerror',e=>errors.push(e.message));page.on('response',r=>{if(r.url().endsWith('.gbc')&&['fetch','xhr'].includes(r.request().resourceType()))loads.push(r.body().then(b=>createHash('sha256').update(b).digest('hex')));});
 await page.addInitScript(({benchOn,phase,fixture})=>{
  localStorage.setItem('egress:battery:v1:'+fixture.sha256,JSON.stringify({v:1,data:fixture.battery}));
  window.__benchPaints={phases:[],frames:[],images:{}};
  const paint=CanvasRenderingContext2D.prototype.putImageData;
  CanvasRenderingContext2D.prototype.putImageData=function(...args){
   const result=paint.apply(this,args),n=window.__egressNative;
   if(n&&this.canvas.classList.contains('game-canvas')&&n.m._emulator_read_mem(n.id,benchOn)===3){
    const p=n.m._emulator_read_mem(n.id,phase),url=this.canvas.toDataURL('image/png');
    window.__benchPaints.frames.push(url);window.__benchPaints.images[p]=url;
    if(!window.__benchPaints.phases.includes(p))window.__benchPaints.phases.push(p);
   }
   return result;
  };
  let factory;Object.defineProperty(window,'Binjgb',{configurable:true,get(){return factory},set(next){factory=async args=>{const m=await next(args),create=m._emulator_new_simple;m._emulator_new_simple=(...a)=>{const id=create(...a);window.__egressNative={m,id};return id;};return m;};}});},{benchOn:sym._bench_on,phase:sym._bench_phase,fixture});
 const htext=await readFile(romRoot+'/obj/heir.sym','utf8'),offset=Object.fromEntries([...htext.matchAll(/^\s*\d+\s+(_\w+)\s+([0-9A-F]+)\s+/gm)].map(m=>[m[1],parseInt(m[2],16)]));
 const addr={mode:sym._game_mode,bench:sym._bench_on,pose:sym._ha_dir-offset._ha_dir+offset._ha_fr,shown:sym._ha_state-offset._ha_state+offset._shown_pose,dd:sym._ha_dir-offset._ha_dir+offset._shown_dd,dir:sym._ha_dir};
 await page.addInitScript(addr=>{
  window.__animation={paints:0,last:[],samples:0,wait:0,maxWait:0,wanted:[],visible:[],frames:[]};
  const paint=CanvasRenderingContext2D.prototype.putImageData;
  CanvasRenderingContext2D.prototype.putImageData=function(...args){const result=paint.apply(this,args),n=window.__egressNative;
   if(n&&this.canvas.classList.contains('game-canvas')){const r=a=>n.m._emulator_read_mem(n.id,a),o=window.__animation;o.paints++;o.last=[r(addr.mode),r(0xff70),r(0xff4f),r(0xc000),r(0xc002),r(0xfe00),r(0xfe02),r(0xff44)];
    if(r(addr.mode)===2&&r(addr.bench)===0&&(r(0xff70)&7)===1&&!(r(0xff4f)&1)&&r(0xc000)&&r(0xc002)<16&&!(r(0xc003)&8)){
     let d=r(addr.pose+1);d=d===128?0:(d+r(addr.dir))&7;if(d>=5)d=8-d;
     const want=r(addr.pose)+','+d,shown=r(addr.shown)+','+r(addr.dd);o.samples++;o.wait=want===shown?0:o.wait+1;o.maxWait=Math.max(o.maxWait,o.wait);
     if(!o.wanted.includes(want))o.wanted.push(want);const tile=r(0xc002)&8,key=Array.from({length:128},(_,i)=>r(0x8000+16*tile+i)).join(',');if(!o.visible.includes(key))o.visible.push(key);
     if(o.samples%3===0&&o.frames.length<200)o.frames.push(this.canvas.toDataURL('image/png'));
    }
   }return result;};
 },addr);
 await page.goto(base,{waitUntil:'networkidle'});await page.getByRole('button',{name:'Play now',exact:true}).click();
 await page.waitForFunction(()=>document.querySelector('.game-canvas')?.dataset.bootReady==='true',null,{timeout:120000});
 const peek=async name=>page.evaluate(address=>{const {m,id}=window.__egressNative;return m._emulator_read_mem(id,address);},sym['_'+name]);
 const state=async()=>page.evaluate(sym=>{const {m,id}=window.__egressNative,r=a=>m._emulator_read_mem(id,a);if((r(0xff70)&7)>1)return null;return Object.fromEntries(['game_mode','family_ready','family_page','pl_x','pl_y','bench_on','bench_mode','bench_page','bench_cur','bench_phase','pocket_n','tool_piece','floor_no'].map(k=>[k,r(sym['_'+k])]).concat([['pocket',Array.from({length:r(sym._pocket_n)},(_,i)=>r(sym._pocket+i))],['room',Array.from({length:224},(_,i)=>r(sym._room+i))]]));},sym);
 const wait=async(predicate,timeout=120000)=>{const end=Date.now()+timeout;while(Date.now()<end){const s=await state();if(s&&predicate(s))return s;await page.waitForTimeout(25);}throw new Error('Native state timed out: '+JSON.stringify(await state()));};
 await page.evaluate(()=>{
  const paint=CanvasRenderingContext2D.prototype.putImageData;let pending=null;
  const ticks=()=>{const {m,id}=window.__native??window.__egressNative;return m._emulator_get_ticks_f64(id);};
  const event=(type,code)=>(document.querySelector('.game-canvas')??document).dispatchEvent(new KeyboardEvent(type,{code,key:code==='KeyX'?'x':code==='KeyZ'?'z':code==='ShiftLeft'?'Shift':code,bubbles:true,cancelable:true}));
  window.__nativeInput=(code,hold,rest)=>new Promise(resolve=>{if(pending)throw new Error('overlapping test input');if(code)event('keydown',code);pending={code,rest,target:ticks()+Math.ceil((code?hold:rest)*4194304/1000),resolve};});
  CanvasRenderingContext2D.prototype.putImageData=function(...args){const result=paint.apply(this,args);if(pending&&this.canvas.classList.contains('game-canvas')){const now=ticks();if(now>=pending.target){if(pending.code){event('keyup',pending.code);pending.code=null;pending.target=now+Math.ceil(pending.rest*4194304/1000);}else{const done=pending.resolve;pending=null;done();}}}return result;};
 });
 const waitEmulated=ms=>page.evaluate(ms=>window.__nativeInput(null,0,ms),ms);
 const press=key=>page.evaluate(key=>window.__nativeInput(key,110,300),key);
 const capture=async name=>{const u=await page.locator('.game-canvas').evaluate(c=>c.toDataURL('image/png'));await writeFile(out+'/'+name,Buffer.from(u.split(',')[1],'base64'));return u;};
 await page.waitForFunction(address=>{const {m,id}=window.__egressNative;return m._emulator_read_mem(id,address)===1;},sym._game_mode,{timeout:120000});
 await waitEmulated(2500);await page.locator('.game-canvas').focus();await press('Enter');
 const isLobby=()=>page.evaluate(a=>{const {m,id}=window.__egressNative;return (m._emulator_read_mem(id,0xff70)&7)<=1&&m._emulator_read_mem(id,a)===1;},sym._menu_active);
 const isPlaying=()=>page.evaluate(a=>{const {m,id}=window.__egressNative;return (m._emulator_read_mem(id,0xff70)&7)<=1&&m._emulator_read_mem(id,a)===2;},sym._game_mode);
 for(let i=0;i<20&&!(await isLobby())&&!(await isPlaying());i++){await press('Enter');await page.waitForTimeout(300);}
 if(await isLobby())await press('KeyX');
 await wait(s=>s.game_mode===2&&s.floor_no===1);await page.waitForTimeout(1000);console.log('Native game started');
 const walkStep=async(key,axis,delta)=>{
  const s=await wait(s=>s.game_mode===2);const target=Math.floor(s[axis]/16)*16+8+delta;
  const opposite={ArrowUp:'ArrowDown',ArrowDown:'ArrowUp',ArrowRight:'ArrowLeft',ArrowLeft:'ArrowRight'};
  for(let i=0;i<96;i++){
   const p=await wait(s=>s.game_mode===2);if(Math.abs(p[axis]-target)<=3)break;
   const move=(p[axis]<target)===(delta>0)?key:opposite[key];
   await page.evaluate(move=>window.__nativeInput(move,35,350),move);
  }
  const reached=await wait(s=>s.game_mode===2);assert.ok(Math.abs(reached[axis]-target)<=3,JSON.stringify({axis,target,reached}));
 };
 for(const args of [['ArrowUp','pl_y',-16],['ArrowRight','pl_x',16],['ArrowDown','pl_y',16],['ArrowLeft','pl_x',-16]])await walkStep(...args);
 const before=await wait(s=>s.pocket_n===2);assert.deepEqual(before.pocket,[4,2]);console.log('Real floor materials collected');
 await press('Enter');await wait(s=>s.bench_on===4);await press('KeyX');await wait(s=>s.bench_on===1&&s.bench_mode===0);
 await press('KeyX');assert.deepEqual((await state()).pocket,[4,2]);
 await press('KeyZ'); // cancel the explicit discard confirmation
 await press('ArrowDown');await press('ShiftLeft');await wait(s=>s.bench_on===0&&s.tool_piece===2);
 const equipped=await state();assert.deepEqual(equipped.pocket,[4,2]);
 const field=await wait(s=>s.bench_on===0),start=Math.floor(field.pl_y/16)*16+Math.floor(field.pl_x/16),queue=[start],previous=new Map([[start,null]]);let benchFloor;
 const dirs=[['ArrowUp','pl_y',-16,-16],['ArrowDown','pl_y',16,16],['ArrowLeft','pl_x',-16,-1],['ArrowRight','pl_x',16,1]];
 for(let h=0;h<queue.length&&benchFloor===undefined;h++)for(const d of dirs){const c=queue[h],n=c+d[3];if(n<0||n>=224||Math.abs(n%16-c%16)+Math.abs(Math.floor(n/16)-Math.floor(c/16))!==1)continue;if(field.room[n]===34){benchFloor=c;break;}if(!previous.has(n)&&[0,1,5,10,12].includes(field.room[n])){previous.set(n,[c,d]);queue.push(n);}}
 assert.notEqual(benchFloor,undefined,'A physical bench must be reachable');const route=[];for(let c=benchFloor;previous.get(c)!==null;){const [p,d]=previous.get(c);route.push(d);c=p;}for(const d of route.reverse())await walkStep(...d.slice(0,3));
 await press('KeyX');await wait(s=>s.bench_on===1&&s.bench_mode===1);await press('ShiftLeft');await wait(s=>s.bench_page===1);await capture('inventory-page.png');await press('KeyX');await wait(s=>s.bench_page===0);await press('ShiftLeft');await press('ArrowLeft');await press('KeyX');await wait(s=>s.bench_page===0);await capture('recipe-ready.png');
 const phases=new Set(),images=new Set();let count=0;await page.evaluate(()=>window.__nativeInput('KeyX',60,0));
 const end=Date.now()+120000;
 while(Date.now()<end){const s=await state();if(s?.bench_on===3){phases.add(s.bench_phase);images.add(await capture('merge-'+String(count++).padStart(3,'0')+'.png'));}if(s?.bench_on===1&&count)break;await page.waitForTimeout(16);}
 const painted=await page.evaluate(()=>window.__benchPaints);
 for(const [p,url] of Object.entries(painted.images)){phases.add(Number(p));images.add(url);await writeFile(out+'/painted-phase-'+p+'.png',Buffer.from(url.split(',')[1],'base64'));}
 for(let i=0;i<painted.frames.length;i++){images.add(painted.frames[i]);await writeFile(out+'/painted-'+String(i).padStart(3,'0')+'.png',Buffer.from(painted.frames[i].split(',')[1],'base64'));}
 const waiting=await state();assert.equal(waiting.bench_phase,4);assert.deepEqual(waiting.pocket,[4,2]);await capture('awaiting-collection.png');
 await press('KeyX');
 const after=await state();assert.deepEqual(after.pocket,[8]);assert.equal(after.tool_piece,8);assert.ok([1,2,3,4].every(p=>painted.phases.includes(p)),JSON.stringify(painted.phases));assert.ok(images.size>=5);await capture('result.png');
 assert.ok((await Promise.all(loads)).includes(expected));assert.deepEqual(errors,[]);
 const report={base,viewport:page.viewportSize(),sha256:expected,source:'Current-ROM authored crafting checkpoint in the website; DOM keyboard events released at native paint boundaries, actual pickups, walking to a physical bench and A to craft; no RAM or inventory writes.',results:{exactRomFetch:true,realFloorPickups:true,bagCannotCraft:true,walkToPhysicalBench:true,aOpensClearedBench:true,fourMergePhases:true,visibleMergeFrames:images.size>=5,resultWaitsForCollection:true,finiteRecipeConsumption:true,selectEquipsWithoutConsuming:true,recipeAutoEquips:true,pageErrors:errors.length===0},phases:painted.phases,frames:painted.frames.length,before:before.pocket,after:after.pocket};
 await writeFile(out+'/report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
 await press('KeyZ');await wait(s=>s.bench_on===0);
 for(const d of [...route].reverse()){const reverse=dirs.find(q=>q[3]===-d[3]);await walkStep(...reverse.slice(0,3));}
 await page.evaluate(()=>window.__nativeInput('KeyX',110,500));
 await page.evaluate(()=>window.__nativeInput('KeyX',500,700));
 await waitEmulated(5000);
 const data=await page.evaluate(()=>window.__animation),animationOut='browser-results/animation';await mkdir(animationOut,{recursive:true});
 const diagnostic={paints:data.paints,last:data.last,samples:data.samples,wanted:data.wanted.length,visible:data.visible.length,maxWait:data.maxWait};
 await writeFile(animationOut+'/sampling.json',JSON.stringify(diagnostic,null,2));console.log('Animation',diagnostic);
 assert.ok(data.samples>120&&data.wanted.length>8&&data.visible.length>8,JSON.stringify(diagnostic));assert.ok(data.maxWait<8,JSON.stringify(diagnostic));
 for(let i=0;i<data.frames.length;i++)await writeFile(animationOut+'/frame-'+String(i).padStart(3,'0')+'.png',Buffer.from(data.frames[i].split(',')[1],'base64'));
 const animationReport={base,viewport:page.viewportSize(),sha256:expected,source:'Actual website canvas paints and native reads while playing the current-ROM authored crafting course; DOM keyboard input released at native frame boundaries; no RAM writes.',samples:data.samples,requestedPoses:data.wanted.length,visibleSpriteBuffers:data.visible.length,maximumUnservedPosePaints:data.maxWait,results:{exactRomFetch:true,visibleAnimationAdvances:true,poseDoesNotStarve:true,pageErrors:errors.length===0}};
 await writeFile(animationOut+'/report.json',JSON.stringify(animationReport,null,2)+'\n');console.log(JSON.stringify(animationReport,null,2));

} finally {await browser.close();}
