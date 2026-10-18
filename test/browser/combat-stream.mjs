// Reproduce the first-floor west-boundary bug through the website's real ROM.
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const {chromium}=await import(pathToFileURL(resolve(process.env.EGRESS_SITE_ROOT??'../wt/egress-site','node_modules/@playwright/test/index.mjs')).href);
const base=(process.argv[2]||'http://127.0.0.1:3287').replace(/\/+$/,'');
const root=process.argv[3]||'build',fixture=JSON.parse(await readFile(process.argv[4]||'build/sprints/combat-stream-browser-fixture.json','utf8'));
const sha=createHash('sha256').update(await readFile(root+'/egress.gbc')).digest('hex');assert.equal(fixture.sha256,sha);
const noi=await readFile(root+'/egress.noi','utf8'),sym=Object.fromEntries([...noi.matchAll(/DEF (\S+) 0x([0-9A-Fa-f]+)/g)].map(m=>[m[1],parseInt(m[2],16)]));
const out='build/sprints/browser-combat-stream';await mkdir(out,{recursive:true});
const browser=await chromium.launch({args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
try{
 const page=await browser.newPage({viewport:{width:390,height:700}}),errors=[],loads=[];
 page.on('pageerror',e=>errors.push(e.message));
 page.on('response',r=>{if(r.url().endsWith('.gbc')&&['fetch','xhr'].includes(r.request().resourceType()))loads.push(r.body().then(b=>createHash('sha256').update(b).digest('hex')));});
 await page.addInitScript(({fixture,sym})=>{
  localStorage.setItem('egress:battery:v1:'+fixture.sha256,JSON.stringify({v:1,data:fixture.battery}));
  let factory;Object.defineProperty(window,'Binjgb',{configurable:true,get(){return factory},set(next){factory=async args=>{const m=await next(args),create=m._emulator_new_simple;m._emulator_new_simple=(...a)=>{const id=create(...a);window.__egressNative={m,id};return id;};return m;};}});
  window.__streamSamples=[];const paint=CanvasRenderingContext2D.prototype.putImageData;
  CanvasRenderingContext2D.prototype.putImageData=function(...args){const v=paint.apply(this,args),n=window.__egressNative;if(!n||!this.canvas.classList.contains('game-canvas'))return v;
   const r=a=>n.m._emulator_read_mem(n.id,a);
   if(r(sym._game_mode)!==2||(r(0xff70)&7)!==1||r(sym._bench_on))return v;
   window.__streamSamples.push({x:r(sym._world_view)*16+r(sym._pl_x),view:r(sym._world_view),alive:r(sym._enemies_alive),hp:r(sym._pl_hp),seams:r(sym._world_seams)|(r(sym._world_seams+1)<<8)});return v;
  };
 },{fixture,sym});
 await page.goto(base+'/',{waitUntil:'networkidle'});
 await page.waitForFunction(()=>document.querySelector('.package-experience')?.dataset.hydrated==='true');
 await page.getByRole('button',{name:'Play now',exact:true}).click();
 await page.waitForFunction(()=>document.querySelector('.game-canvas')?.dataset.bootReady==='true',null,{timeout:120000});
 const elapsed=async ms=>{const from=await page.evaluate(()=>{const {m,id}=window.__egressNative;return m._emulator_get_ticks_f64(id);});await page.waitForFunction(({from,ms})=>{const {m,id}=window.__egressNative;return m._emulator_get_ticks_f64(id)>=from+Math.ceil(ms*4194304/1000);},{from,ms},{timeout:120000});};
 const press=async(code,ms=110)=>{await page.keyboard.down(code);await elapsed(ms);await page.keyboard.up(code);await elapsed(300);};
 const wait=async(name,value)=>page.waitForFunction(({a,value})=>{const {m,id}=window.__egressNative;return (a<0xd000||(m._emulator_read_mem(id,0xff70)&7)===1)&&m._emulator_read_mem(id,a)===value;},{a:sym['_'+name],value},{timeout:120000});
 await wait('game_mode',1);await elapsed(2500);await page.locator('.game-canvas').focus();await press('Enter');await wait('menu_active',1);await press('KeyX');await wait('game_mode',2);
 await page.waitForFunction(sym=>{const {m,id}=window.__egressNative,r=a=>m._emulator_read_mem(id,a);return (r(0xff70)&7)===1&&r(sym._game_mode)===2&&(r(sym._acts+8)|(r(sym._acts+9)<<8))>=2;},sym,{timeout:120000});
 const state=()=>page.evaluate(sym=>{const {m,id}=window.__egressNative,r=a=>m._emulator_read_mem(id,a);return {view:r(sym._world_view),x:r(sym._pl_x),alive:r(sym._enemies_alive),hp:r(sym._pl_hp),floor:r(sym._floor_no),mode:r(sym._game_mode)};},sym);
 const capture=async name=>{const u=await page.locator('.game-canvas').evaluate(c=>c.toDataURL('image/png'));await writeFile(out+'/'+name,Buffer.from(u.split(',')[1],'base64'));};
 const before=await state();assert.ok(before.view>0&&before.alive>0&&before.hp>0);await capture('before.png');
 await press('ArrowLeft',1300);const after=await state();await capture('after.png');
 const samples=await page.evaluate(()=>window.__streamSamples);
 const results={exactRomFetch:(await Promise.all(loads)).includes(sha),westFloorCrossedDuringFight:samples.some(s=>s.view<before.view&&s.x<before.view*16+24&&s.alive===before.alive),originalFightPreserved:after.alive===before.alive,playerStillAlive:after.hp>0&&after.mode===2&&after.floor===1,pageErrors:errors.length===0};
 const report={base,sha256:sha,source:'Current-ROM first-floor save, ordinary website Continue and DOM keyboard movement; no RAM, map, health or actor writes.',seed:fixture.seed,before,after,results,minimumGlobalX:Math.min(...samples.map(s=>s.x)),samples:samples.length,errors};
 await writeFile(out+'/report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));assert.ok(Object.values(results).every(Boolean));
}finally{await browser.close();}
