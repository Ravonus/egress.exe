import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
const {chromium}=await import(pathToFileURL(resolve(process.env.EGRESS_SITE_ROOT??'../wt/egress-site','node_modules/@playwright/test/index.mjs')).href);
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
const romRoot=process.argv[3]||'build';
const expected=createHash('sha256').update(await readFile(romRoot+'/egress.gbc')).digest('hex');
const text=await readFile(romRoot+'/egress.noi','utf8'),sym=Object.fromEntries([...text.matchAll(/DEF (\S+) 0x([0-9A-Fa-f]+)/g)].map(m=>[m[1],parseInt(m[2],16)]));
const base=process.argv[2]||'http://127.0.0.1:3241';
const browser=process.env.EGRESS_BROWSER_WS?await chromium.connect(process.env.EGRESS_BROWSER_WS):await chromium.launch({args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
try{
 const page=await browser.newPage({viewport:{width:1280,height:900}}),loads=[];
 page.on('response',r=>{if(r.url().endsWith('.gbc')&&['fetch','xhr'].includes(r.request().resourceType()))loads.push(r.body().then(b=>createHash('sha256').update(b).digest('hex')));});
 await page.addInitScript(()=>{let factory;Object.defineProperty(window,'Binjgb',{configurable:true,get(){return factory},set(next){factory=async args=>{const m=await next(args),create=m._emulator_new_simple;m._emulator_new_simple=(...a)=>{const id=create(...a);window.__egressNative={m,id};return id;};return m;};}});});
 await page.goto(base,{waitUntil:'networkidle'});await page.getByRole('button',{name:'Play now',exact:true}).click();await page.waitForFunction(()=>document.querySelector('.game-canvas')?.dataset.bootReady==='true',null,{timeout:120000});
 await page.locator('.game-canvas').focus();
 const peek=async name=>page.evaluate(address=>{let {m,id}=window.__egressNative;return m._emulator_read_mem(id,address);},sym['_'+name]);
 const waitEmulated=async ms=>{const target=await page.evaluate(ms=>{const {m,id}=window.__egressNative;return m._emulator_get_ticks_f64(id)+Math.ceil(ms*4194304/1000);},ms);await page.waitForFunction(target=>{const {m,id}=window.__egressNative;return m._emulator_get_ticks_f64(id)>=target;},target,{timeout:120000});};
 const press=async key=>{await page.keyboard.down(key);await waitEmulated(100);await page.keyboard.up(key);await waitEmulated(600);};
 await page.waitForFunction(address=>{let {m,id}=window.__egressNative;return m._emulator_read_mem(id,address)===1;},sym._game_mode,{timeout:120000});await waitEmulated(2200);await press('Enter');
 for(let i=0;i<16;i++){
  const mode=await peek('game_mode');
  if(mode===6){if(await peek('family_ready'))break;await waitEmulated(400);continue;}
  await press(mode===5?'KeyZ':'KeyX'); // ordinary B SKIP at a fresh clock prompt
 }
 await page.waitForTimeout(300);
 if(await peek('game_mode')===6&&await peek('family_page')===1)await press('KeyZ');
 await page.waitForFunction(sym=>{const {m,id}=window.__egressNative,r=a=>m._emulator_read_mem(id,a);return (r(0xff70)&7)<=1&&r(sym._game_mode)===6&&r(sym._family_page)===0&&r(sym._family_ready);},sym,{timeout:120000});
 const data=await page.evaluate(sym=>{const {m,id}=window.__egressNative,read=a=>m._emulator_read_mem(id,a),range=(a,n)=>Array.from({length:n},(_,i)=>read(a+i));return {lcd:read(0xff40),mode:read(sym._game_mode),page:read(sym._family_page),family:range(sym._family,95)};},sym);
 const hashes=await Promise.all(loads);if(!hashes.includes(expected))throw new Error('Browser loaded a different ROM');data.sha256=expected;data.actualRomFetchVerified=true;

 const img=await page.locator('.game-canvas').evaluate(c=>c.toDataURL('image/png'));await writeFile('build/sprints/browser-portrait.png',Buffer.from(img.split(',')[1],'base64'));await writeFile('build/sprints/browser-portrait.json',JSON.stringify(data,null,2)+'\n');console.log(JSON.stringify({sha256:data.sha256,lcd:data.lcd,mode:data.mode,page:data.page,actualRomFetchVerified:true}));
}finally{await browser.close();}
