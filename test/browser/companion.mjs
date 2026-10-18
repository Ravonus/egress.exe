// A saved encounter fixture; movement and rendering run through the website's ROM.
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const {chromium}=await import(pathToFileURL(resolve(process.env.EGRESS_SITE_ROOT??'../wt/egress-site','node_modules/@playwright/test/index.mjs')).href);
const base=process.argv[2]||'http://127.0.0.1:3247',romRoot=process.argv[3]||'build';
const fixture=JSON.parse(await readFile(process.argv[4]||'build/sprints/companion-browser-fixture.json','utf8'));
const sha=createHash('sha256').update(await readFile(romRoot+'/egress.gbc')).digest('hex');assert.equal(fixture.sha256,sha);
const noi=await readFile(romRoot+'/egress.noi','utf8'),sym=Object.fromEntries([...noi.matchAll(/DEF (\S+) 0x([0-9A-Fa-f]+)/g)].map(m=>[m[1],parseInt(m[2],16)]));
const out='build/sprints/browser-companion';await mkdir(out,{recursive:true});
const browser=process.env.EGRESS_BROWSER_WS?await chromium.connect(process.env.EGRESS_BROWSER_WS):await chromium.launch({args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
try{
 const page=await browser.newPage({viewport:{width:390,height:700}}),errors=[],loads=[];
 page.on('pageerror',e=>errors.push(e.message));page.on('response',r=>{if(r.url().endsWith('.gbc')&&['fetch','xhr'].includes(r.request().resourceType()))loads.push(r.body().then(b=>createHash('sha256').update(b).digest('hex')));});
 await page.addInitScript(({fixture,sym})=>{
  localStorage.setItem('egress:battery:v1:'+fixture.sha256,JSON.stringify({v:1,data:fixture.battery}));
  let factory;Object.defineProperty(window,'Binjgb',{configurable:true,get(){return factory},set(next){factory=async args=>{const m=await next(args),create=m._emulator_new_simple;m._emulator_new_simple=(...a)=>{const id=create(...a);window.__egressNative={m,id};return id;};return m;};}});
  window.__petPaints={samples:0,patterns:[],facings:[],missing:0,heir:0,ready:false};const paint=CanvasRenderingContext2D.prototype.putImageData;
  CanvasRenderingContext2D.prototype.putImageData=function(...args){const v=paint.apply(this,args),n=window.__egressNative;if(!n||!this.canvas.classList.contains('game-canvas'))return v;const r=a=>n.m._emulator_read_mem(n.id,a),s=window.__petPaints;
   if(r(sym._game_mode)!==2||(r(0xff70)&7)!==1||(r(0xff4f)&1)||r(sym._bench_on))return v;
   const id=r(sym._companion)|(r(sym._companion+1)<<8);if(id!==fixture.pet||r(sym._companion+7)===255)return v;
   const x=r(sym._pl_x)+((r(sym._companion+2)<<24)>>24)-r(sym._cam_x),y=r(sym._pl_y)+((r(sym._companion+3)<<24)>>24)-r(sym._cam_y);if(x<8||x>152||y<10||y>118)return v;
   // At a native paint boundary CPU-side OAM reads can be unavailable.
   // Observe the published native list, as the heir-animation check does.
   const body=[];for(let k=0;k<40;k++){const a=sym._shadow_OAM+k*4,t=r(a+2);if(r(a)&&t>=124&&t<132&&!(r(a+3)&8))body.push(t);if(r(a)&&t<16&&!(r(a+3)&8))s.heir++;}
   if(body.length===2)s.ready=true;if(!s.ready)return v;
   s.samples++;if(body.length!==2)s.missing++;else{const at=n.m._emulator_get_vram_ptr(n.id)+(Math.min(...body)&~3)*16,key=Array.from(n.m.HEAPU8.subarray(at,at+64)).join(',');if(!s.patterns.includes(key))s.patterns.push(key);}
   const facing=(r(sym._companion+6)>>3)&7;if(!s.facings.includes(facing))s.facings.push(facing);return v;
  };
 },{fixture,sym});
 await page.goto(base,{waitUntil:'networkidle'});await page.getByRole('button',{name:'Play now',exact:true}).click();
 await page.waitForFunction(()=>document.querySelector('.game-canvas')?.dataset.bootReady==='true',null,{timeout:120000});await page.locator('.game-canvas').focus();
 const elapsed=async ms=>{const from=await page.evaluate(()=>{const {m,id}=window.__egressNative;return m._emulator_get_ticks_f64(id);});await page.waitForFunction(({from,ms})=>{const {m,id}=window.__egressNative;return m._emulator_get_ticks_f64(id)>=from+Math.ceil(ms*4194304/1000);},{from,ms},{timeout:120000});};
 const press=async(key,hold=110)=>{await page.keyboard.down(key);await elapsed(hold);await page.keyboard.up(key);await elapsed(300);};
 const wait=async(field,value)=>page.waitForFunction(({a,value})=>{const {m,id}=window.__egressNative;return (a<0xd000||(m._emulator_read_mem(id,0xff70)&7)===1)&&m._emulator_read_mem(id,a)===value;},{a:sym['_'+field],value},{timeout:120000});
 await wait('game_mode',1);await elapsed(2500);await press('Enter');await wait('menu_active',1);await press('KeyX');await wait('game_mode',2);
 // MODE_PLAY also brackets initial loading. Actual simulation frames advance
 // only once the room has assembled; do not spend the walking route in it.
 await page.waitForFunction(sym=>{const {m,id}=window.__egressNative,r=a=>m._emulator_read_mem(id,a);return (r(0xff70)&7)<=1&&r(sym._game_mode)===2&&(r(sym._acts+8)|(r(sym._acts+9)<<8))>=2;},sym,{timeout:120000});
 await page.waitForFunction(()=>window.__petPaints.ready,null,{timeout:120000});await elapsed(600);
 for(const [key,hold] of [['ArrowUp',220],['ArrowRight',220],['ArrowDown',160],['ArrowLeft',220]])await press(key,hold);
 await elapsed(4000);const d=await page.evaluate(()=>window.__petPaints);
 const results={exactRomFetch:(await Promise.all(loads)).includes(sha),nativePetKeptVisible:d.samples>120&&d.missing===0,walkAndIdleAnimate:d.patterns.length>=5,multipleFacingsAnimate:d.facings.length>=3,heirRemainsVisible:d.heir>120,pageErrors:errors.length===0};
 const image=await page.locator('.game-canvas').evaluate(c=>c.toDataURL('image/png'));await writeFile(out+'/native-companion.png',Buffer.from(image.split(',')[1],'base64'));
 const report={base,sha256:sha,pet:fixture.pet,source:'Explicit CRC-sealed saved encounter; ordinary DOM keyboard movement after native assembly, published native sprite list and direct VRAM reads, no gameplay RAM writes.',results,samples:d.samples,visiblePatterns:d.patterns.length,facings:d.facings,missingPetPaints:d.missing,errors};await writeFile(out+'/report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));assert.ok(Object.values(results).every(Boolean));
}finally{await browser.close();}
