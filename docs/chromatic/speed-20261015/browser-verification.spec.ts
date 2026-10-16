import {expect,test,type Page} from '@playwright/test';

async function hardware(page:Page,mode:'ok'|'cancel'|'corrupt'|'granted'|'timeout'|'verified'='ok'){
 await page.route('https://install.shinyrelay.com/cart/engine.fs',async route=>{if(process.env.CHROMATIC_ENGINE_FILE)await route.fulfill({path:process.env.CHROMATIC_ENGINE_FILE,contentType:'application/octet-stream',headers:{'access-control-allow-origin':'*'}});else await route.continue();});
 await page.addInitScript((mode)=>{
  const state={serialRequests:0,usbRequests:0,commands:[] as string[],userActivation:[] as boolean[],open:false,usbOpen:false,serialPresent:true,jtag:[] as number[]};
  Object.assign(window,{__chromatic:state});
  const enc=new TextEncoder();let input:ReadableStreamDefaultController<Uint8Array>;
  const crc=(bytes:Uint8Array)=>{let c=0xffffffff;for(const b of bytes){c^=b;for(let k=0;k<8;k++)c=(c>>>1)^((c&1)?0xedb88320:0);}return(c^0xffffffff)>>>0;};
  const line=(text:string)=>input.enqueue(enc.encode(text+'\r\n'));
  const rom=new Uint8Array(32768).fill(255);rom.set(enc.encode('TEST'),0x134);rom[0x138]=0;rom[0x143]=0xc0;rom[0x147]=0x1b;rom[0x148]=0;rom[0x149]=2;let checksum=0;for(let i=0x134;i<=0x14c;i++)checksum=(checksum-rom[i]!-1)&255;rom[0x14d]=checksum;
  let saved=new Uint8Array(32768).fill(23);let job:{kind:string;bytes:Uint8Array;crc:number}|null=null;
  const info='CBU1 info proto=1 flash=32768 writable=32768 ram=32768 chunk=1024 skipblank=1 bin=1 engine=1 type=1b romcode=00 ramcode=02 header=ok title="TEST"';
  const port={getInfo:()=>({usbVendorId:0x374e,usbProductId:0x0101}),readable:null as ReadableStream<Uint8Array>|null,writable:new WritableStream<Uint8Array>({write(bytes){if(bytes[0]===0xa5){if(!job)throw new Error('Unexpected fixture write');const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),off=v.getUint32(4,true),n=v.getUint16(8,true);job.bytes.set(bytes.subarray(10,10+n),off);if(off+n<job.bytes.length){line(`CBU1 want ${off+n} ${Math.min(1024,job.bytes.length-off-n)} bin`);return;}const done=job;if(done.kind==='rom')rom.set(done.bytes);else saved=done.bytes;if(mode==='verified'&&done.kind==='rom'){line('CBU1 progress rom verify 16384 32768 80');setTimeout(()=>{line('CBU1 time rom verify 3500');line('CBU1 progress rom check-header 0 32768 99');setTimeout(()=>{line('CBU1 time rom check-header 2500');line(`CBU1 done rom ${done.bytes.length} ${crc(done.bytes).toString(16).padStart(8,'0')}`);line('CBU1 ready');job=null;},2500);},3500);return;}line(done.kind==='rom'?`CBU1 done rom ${done.bytes.length} ${crc(done.bytes).toString(16).padStart(8,'0')}`:`CBU1 done sav ${crc(done.bytes).toString(16).padStart(8,'0')}`);line('CBU1 ready');job=null;return;}const cmd=new TextDecoder().decode(bytes).trim();if(!cmd)return;state.commands.push(cmd);
   if(mode==='timeout')return;
   if(cmd==='fwversion')line('{"mcu":"fixture-mcu","fpga":"fixture-fpga","chromatic":"fixture-unit"}');
   else if(cmd==='cwl status')line('{"enabled":1,"wifi":1,"ip":"192.0.2.3","status":"connected","ssid":"Fixture Network","password":"DO_NOT_KEEP"}');
   else if(cmd==='cwl relay status')line('{"text":"Fixture relay idle","state":"idle","code":"DO_NOT_KEEP","device_id":"DO_NOT_KEEP"}');
   else if(cmd==='cart usb status')line('CBU1 status idle');
   else if(cmd==='cart usb info'){line(info);line('CBU1 ready');}
   else if(cmd.startsWith('cart usb write ')){const parts=cmd.split(' '),kind=parts[3],length=kind==='rom'?Number(parts[4]):32768;job={kind,bytes:new Uint8Array(length),crc:kind==='rom'?parseInt(parts[5],16):0};line(`CBU1 want 0 ${Math.min(1024,length)} bin`);}
   else if(cmd.startsWith('cart usb read ')){const kind=cmd.split(' ')[3],data=kind==='rom'?rom:saved;for(let off=0;off<data.length;off+=1024){const part=data.slice(off,off+1024),fr=new Uint8Array(part.length+15),v=new DataView(fr.buffer);fr.set([0xa5,67,66,85,kind==='rom'?82:83]);v.setUint32(5,off,true);v.setUint16(9,part.length,true);fr.set(part,11);v.setUint32(11+part.length,crc(fr.subarray(4,11+part.length)),true);if(mode==='corrupt'&&off===0)fr[12]=fr[12]!^1;input.enqueue(fr);}line(`CBU1 end ${kind} 0 ${data.length} ${crc(data).toString(16).padStart(8,'0')}`);line('CBU1 ready');}
  }}),async open(options:object){Object.assign(state,{options});state.open=true;this.readable=new ReadableStream({start(c){input=c;}});},async setSignals(signals:object){Object.assign(state,{signals:{...(state as unknown as {signals:object}).signals,...signals}});},async close(){state.open=false;}};
  let command=0,read=0,configured=false,edit=false;
  const device={vendorId:0x33aa,productId:0x0120,opened:false,configuration:{configurationValue:1,interfaces:[{interfaceNumber:0,alternate:{endpoints:[{endpointNumber:1,direction:'in'},{endpointNumber:2,direction:'out'},{endpointNumber:3,direction:'in'}]}}]},async open(){state.usbOpen=true;this.opened=true;},async close(){state.usbOpen=false;this.opened=false;},async claimInterface(){},async transferOut(ep:number,data:Uint8Array){
   const cmd=data[0]===0x5b&&data[1]===3&&data[3]===0x6b?data[5]:data[0]===0x6b&&data.length===6?data[2]:undefined;
   if(cmd!==undefined){command=cmd;read=0;state.jtag.push(cmd);if(cmd===0x15)edit=true;if(cmd===0x3a)edit=false;if(cmd===0x3c){state.serialPresent=false;serial.dispatchEvent(Object.assign(new Event('disconnect'),{port}));}if(cmd===8){configured=true;state.serialPresent=true;}}
   return {status:'ok',bytesWritten:data.length};},async transferIn(ep:number,n:number){const value=command===0x11?0x1281b:32|(edit?128:0)|(configured?8192:0);const raw=Uint8Array.of(value&255,value>>>8&255,value>>>16&255,value>>>24);const bytes=ep===3?new Uint8Array(4):raw.slice(read,read+n);if(ep!==3)read+=n;return {status:'ok',data:new DataView(bytes.buffer)};}};
  const serial=Object.assign(new EventTarget(),{async getPorts(){return state.serialPresent&&(state.serialRequests>0||mode==='granted')?[port]:[];},async requestPort(options:object){state.serialRequests++;state.userActivation.push(navigator.userActivation.isActive);Object.assign(state,{serialFilters:options});if(mode==='cancel')throw new DOMException('Canceled','NotFoundError');return port;}});
  const usb=Object.assign(new EventTarget(),{async getDevices(){return state.usbOpen||mode==='granted'?[device]:[];},async requestDevice(options:object){state.usbRequests++;state.userActivation.push(navigator.userActivation.isActive);Object.assign(state,{usbFilters:options});return device;}});
  Object.defineProperty(navigator,'serial',{configurable:true,value:serial});Object.defineProperty(navigator,'usb',{configurable:true,value:usb});
  Object.assign(window,{__unplug:()=>input.error(new Error('USB disconnected'))});
 },mode);
}


function targetRom(){const b=Buffer.alloc(32768,8);b.write('ACKTEST',0x134);b[0x13b]=0;b[0x143]=0xc0;b[0x147]=0x1b;b[0x148]=0;b[0x149]=2;let h=0;for(let i=0x134;i<=0x14c;i++)h=(h-b[i]!-1)&255;b[0x14d]=h;return b;}
async function setup(page:Page,mode:'ok'|'verified'='ok'){
 await hardware(page,mode);await page.goto('/chromatic/');
 await page.getByRole('button',{name:'Choose console',exact:true}).click();await expect(page.getByRole('button',{name:'Disconnect console',exact:true})).toBeEnabled();
 await page.getByRole('button',{name:'Choose USB bridge',exact:true}).click();await expect(page.getByRole('button',{name:'Disconnect USB bridge',exact:true})).toBeEnabled();
 await page.getByRole('button',{name:'library',exact:true}).click();
 await page.locator('input[type=file]').setInputFiles({name:'ack-test.gbc',mimeType:'application/octet-stream',buffer:targetRom()});
 await expect(page.getByRole('status')).toContainText('File imported');
 await page.getByRole('button',{name:'Cartridge',exact:true}).click();await page.getByRole('button',{name:'Read cartridge identity',exact:true}).click();
 await page.getByRole('combobox',{name:'ROM to write',exact:true}).selectOption({label:'ack-test'});
 await expect(page.getByRole('button',{name:'Back up before flashing (optional)',exact:true})).toBeEnabled({timeout:30000});
}
test('no backup: acknowledgment gates writing and resets when the selected ROM changes',async({page},testInfo)=>{
 await setup(page);
 const ack=page.getByRole('checkbox',{name:/I understand flashing will replace/}),flash=page.getByRole('button',{name:'Flash selected ROM',exact:true});
 await expect(ack).not.toBeChecked();await expect(flash).toBeDisabled();await ack.check();await expect(flash).toBeEnabled();
 await page.getByRole('combobox',{name:'ROM to write',exact:true}).selectOption('');await expect(flash).toBeDisabled();await page.getByRole('combobox',{name:'ROM to write',exact:true}).selectOption({label:'ack-test'});await expect(ack).not.toBeChecked();
 await ack.check();await page.screenshot({path:testInfo.outputPath('acknowledged.png'),fullPage:true});await flash.click();
 await expect(page.locator('.chromatic-writer')).toContainText('written and verified successfully',{timeout:30000});await expect(ack).not.toBeChecked();await expect(flash).toBeDisabled();
 const evidence=await page.evaluate(()=>({commands:(window as any).__chromatic.commands as string[],record:JSON.parse(localStorage.getItem('keel-cartridge-write:egress')!)}));
 const first=evidence.commands.findIndex(c=>c.startsWith('cart usb write'));
 expect(first).toBeGreaterThan(-1);expect(evidence.commands.slice(0,first).some(c=>c.startsWith('cart usb read'))).toBe(false);
 expect(evidence.commands.filter(c=>c.startsWith('cart usb read'))).toHaveLength(2);expect(evidence.record).toMatchObject({state:'complete',backupSkipped:true,backupGroup:null,backupRomSha256:null});
 await page.reload();await expect(page.getByRole('button',{name:'Flash selected ROM',exact:true})).toBeDisabled();
});
test('optional verified backup remains available and uses the backed-up acknowledgment',async({page})=>{
 await setup(page);await page.getByRole('button',{name:'Back up before flashing (optional)',exact:true}).click();
 await expect(page.locator('.chromatic-writer')).toContainText('Backup ready:',{timeout:30000});
 const flash=page.getByRole('button',{name:'Flash selected ROM',exact:true});await expect(flash).toBeDisabled();
 await page.getByRole('checkbox',{name:/I approve replacing this cartridge/}).check();await flash.click();
 await expect(page.locator('.chromatic-writer')).toContainText('written and verified successfully',{timeout:30000});
 const record=await page.evaluate(()=>JSON.parse(localStorage.getItem('keel-cartridge-write:egress')!));expect(record.backupSkipped).toBe(false);expect(record.backupGroup).toBeTruthy();expect(record.backupRomSha256).toMatch(/^[a-f0-9]{64}$/);
});

test('acknowledgment accepts label clicks and keyboard before a device is connected; Flash stays blocked',async({page},testInfo)=>{
 await hardware(page);await page.goto('/chromatic/');await page.getByRole('button',{name:'library',exact:true}).click();
 await page.locator('input[type=file]').setInputFiles({name:'ack-test.gbc',mimeType:'application/octet-stream',buffer:targetRom()});await expect(page.getByRole('status')).toContainText('File imported');
 await page.getByRole('button',{name:'Cartridge',exact:true}).click();await page.getByRole('combobox',{name:'ROM to write',exact:true}).selectOption({label:'ack-test'});
 const ack=page.getByRole('checkbox',{name:/I understand flashing will replace/}),flash=page.getByRole('button',{name:'Flash selected ROM',exact:true});
 await expect(ack).toBeEnabled();await page.locator('.chromatic-writer .chromatic-check').click();await expect(ack).toBeChecked();await expect(flash).toBeDisabled();
 await ack.press('Space');await expect(ack).not.toBeChecked();await expect(flash).toBeDisabled();
 await expect(page.locator('.chromatic-writer')).toContainText('To enable Flash, connect the console and USB bridge');
 await page.locator('.chromatic-writer').screenshot({path:testInfo.outputPath('checkbox-before-connect.png')});
 expect((await page.evaluate(()=>(window as any).__chromatic)).commands).toEqual([]);
});

test('firmware verification avoids downloading ROM again and phase time resets independently of total',async({page},testInfo)=>{
 await setup(page,'verified');await page.getByRole('checkbox',{name:/I understand flashing will replace/}).check();
 await page.getByRole('button',{name:'Flash selected ROM',exact:true}).click();
 const progress=page.getByRole('region',{name:'Operation progress'});
 await expect(progress).toContainText(/Verifying ROM on device · 2s this step · \d+s total/);
 await expect(progress).toContainText(/Verifying boot block · 0s this step · [3-9]s total/);
 await progress.screenshot({path:testInfo.outputPath('phase-timing.png')});
 await expect(page.locator('.chromatic-writer')).toContainText('firmware verified the ROM with SHA-256 and boot-block CRC32');
 const evidence=await page.evaluate(()=>({commands:(window as any).__chromatic.commands as string[],record:JSON.parse(localStorage.getItem('keel-cartridge-write:egress')!)}));
 expect(evidence.commands.filter(c=>c.startsWith('cart usb read'))).toEqual(['cart usb read sav 0 32768']);
 expect(evidence.record).toMatchObject({state:'complete',romVerification:'firmware-sha256-crc32',deviceTimings:{verify:3500,'check-header':2500}});
 await page.locator('.chromatic-writer').screenshot({path:testInfo.outputPath('verified-firmware.png')});
});
