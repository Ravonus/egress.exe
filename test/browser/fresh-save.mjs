// Earlier browser cartridges start fresh; no native memory or save payload writes.
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
const {chromium}=await import(pathToFileURL(resolve(process.env.EGRESS_SITE_ROOT??'../wt/egress-site','node_modules/@playwright/test/index.mjs')).href);
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const base=process.argv[2]||'http://127.0.0.1:3247',romRoot=process.argv[3]||'build';
const sha=createHash('sha256').update(await readFile(romRoot+'/egress.gbc')).digest('hex');
const fixture=JSON.parse(await readFile(process.argv[4]||'build/fresh-save/mbc5/previous-browser-fixture.json','utf8'));
assert.ok(fixture.generation>1);assert.notEqual(fixture.previousSha256,sha);
const noi=await readFile(romRoot+'/egress.noi','utf8'),symbols=Object.fromEntries([...noi.matchAll(/DEF (\S+) 0x([0-9A-Fa-f]+)/g)].map(m=>[m[1],parseInt(m[2],16)]));
const browser=process.env.EGRESS_BROWSER_WS?await chromium.connect(process.env.EGRESS_BROWSER_WS):await chromium.launch({args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
try {
 const page=await browser.newPage({viewport:{width:320,height:600}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(fixture=>{
  window.__oldBattery=JSON.stringify({v:1,data:fixture.battery});localStorage.setItem('egress:battery:v1:'+fixture.previousSha256,window.__oldBattery);
  let factory;Object.defineProperty(window,'Binjgb',{configurable:true,get(){return factory},set(next){factory=async args=>{const m=await next(args),create=m._emulator_new_simple;m._emulator_new_simple=(...a)=>{const id=create(...a);window.__egressNative={m,id};return id;};return m;};}});
 },fixture);
 await page.goto(base,{waitUntil:'networkidle'});await page.getByRole('button',{name:'Play now',exact:true}).click();
 await page.waitForFunction(()=>document.querySelector('.game-canvas')?.dataset.bootReady==='true',null,{timeout:120000});
 await page.bringToFront();await page.locator('.game-canvas').focus();
 const waitEmulated=async ms=>{const from=await page.evaluate(()=>{const {m,id}=window.__egressNative;return m._emulator_get_ticks_f64(id);});await page.waitForFunction(({from,ms})=>{const {m,id}=window.__egressNative;return m._emulator_get_ticks_f64(id)>=from+Math.ceil(ms*4194304/1000);},{from,ms},{timeout:120000});};
 // Menu inputs need no movement precision. Poll native ticks rather than
 // depending on an LCD paint while native scenes turn the display off.
 const press=async key=>{await page.keyboard.down(key);await waitEmulated(110);await page.keyboard.up(key);await waitEmulated(300);};
 // C000-CFFF remains readable while the title renderer switches D000-DFFF.
 const wait=async(field,value)=>page.waitForFunction(({a,value})=>{const {m,id}=window.__egressNative;return (a<0xd000||(m._emulator_read_mem(id,0xff70)&7)<=1)&&m._emulator_read_mem(id,a)===value;},{a:symbols['_'+field],value},{timeout:120000});
 const snapshot=()=>page.evaluate(s=>{const {m,id}=window.__egressNative,r=a=>m._emulator_read_mem(id,a);return {generation:r(s._family+279)|(r(s._family+280)<<8),history:r(s._family+284),runs:r(s._meta)|(r(s._meta+1)<<8),floor:r(s._floor_no),ready:r(s._resume_ready),pocket:r(s._pocket_n)};},symbols);
 await wait('game_mode',1);console.log('Native title reached');await waitEmulated(2500);await page.locator('.game-canvas').focus();await press('Enter');await wait('menu_active',1);
 const lobby=await snapshot();console.log('Native fresh lobby',lobby);assert.equal(lobby.ready,0);await press('KeyX');
 // A blank cartridge asks for its clock before the heir picker. Use B SKIP,
 // just as a player can; do not seed the new battery or bypass native setup.
 await page.waitForFunction(a=>{const {m,id}=window.__egressNative;return [5,6].includes(m._emulator_read_mem(id,a));},symbols._game_mode,{timeout:120000});
 const clockPrompt=await page.evaluate(a=>{const {m,id}=window.__egressNative;return m._emulator_read_mem(id,a)===5;},symbols._game_mode);
 if(clockPrompt){await waitEmulated(500);await press('KeyZ');}
 await wait('game_mode',6);await wait('family_ready',1);
 const fresh=await snapshot();console.log('Native fresh heir',fresh);assert.equal(fresh.generation,1);assert.equal(fresh.history,0);assert.equal(fresh.runs,0);
 const image=await page.locator('.game-canvas').evaluate(c=>c.toDataURL('image/png'));await writeFile('build/sprints/browser-fresh-save.png',Buffer.from(image.split(',')[1],'base64'));
 await press('KeyX');await wait('family_page',1);await wait('family_ready',1);await press('KeyX');await wait('game_mode',2);
 const playing=await snapshot();assert.equal(playing.floor,1);assert.equal(playing.pocket,0);assert.equal(playing.generation,1);
 await page.waitForFunction(sha=>{const text=localStorage.getItem('egress:battery:v1:'+sha);if(!text)return false;const b=atob(JSON.parse(text).data);return [0x570,0xa6c].some(p=>b.slice(p,p+3)==='FA'+String.fromCharCode(4));},sha,{timeout:30000});
 assert.ok(await page.evaluate(old=>localStorage.getItem('egress:battery:v1:'+old)===window.__oldBattery,fixture.previousSha256));assert.deepEqual(errors,[]);
 const report={base,viewport:page.viewportSize(),input:'DOM keyboard events with bounded native tick waits; no LCD-paint dependency.',sha256:sha,previousSha256:fixture.previousSha256,previousGeneration:fixture.generation,clockPrompt,results:{previousSaveIgnored:true,oldStorageUntouched:true,freshGenerationOne:true,noInheritedProgress:true,currentSaveWritten:true,nativeGameStarted:true,pageErrors:true}};
 await writeFile('build/sprints/browser-fresh-save-report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
} finally {await browser.close();}
