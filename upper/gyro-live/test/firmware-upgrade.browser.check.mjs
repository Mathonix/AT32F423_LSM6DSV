import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,existsSync,mkdirSync,mkdtempSync} from 'node:fs';
import {dirname,join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
import http from 'node:http';
const here=dirname(fileURLToPath(import.meta.url)),root=join(here,'../src/public');
const server=http.createServer((req,res)=>{
  const pathname=new URL(req.url,'http://localhost').pathname,p=join(root,pathname==='/'?'index.html':pathname);
  try {const b=readFileSync(p);res.setHeader('Content-Type',p.endsWith('.js')?'text/javascript':p.endsWith('.css')?'text/css':p.endsWith('.html')?'text/html':'application/octet-stream');res.end(b);}catch{res.writeHead(404);res.end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const base='http://127.0.0.1:'+server.address().port;
const chrome=process.env.CHROME||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser=await puppeteer.launch({executablePath:chrome,headless:true}),results=[],dir=mkdtempSync(join(tmpdir(),'gyro-fw-browser-'));
const bytes=Buffer.alloc(4097,0xa5);bytes.writeUInt32LE(0x2000bff0,0);bytes.writeUInt32LE(0x08008009,4);
const binary=join(dir,'application.bin'),wrong=join(dir,'bootloader.bin');writeFileSync(binary,bytes);
const bad=Buffer.from(bytes);bad.writeUInt32LE(0x08000009,4);writeFileSync(wrong,bad);
async function setup(options={}) {
  const page=await browser.newPage();await page.setViewport({width:1440,height:1000});
  await page.evaluateOnNewDocument(readFileSync(join(here,'mock-serial.js'),'utf8'));
  await page.evaluateOnNewDocument(options=>{
    const {dev:d,port:p}=window.__mock;Object.assign(d,{extended:true,configVersion:3,capabilities:255,boot:!!options.boot});
    // Independent reference CRC, generated as a table rather than the production bit loop.
    const table=Array.from({length:256},(_,i)=>{let c=i;for(let k=0;k<8;k++)c=(c>>>1)^((c&1)?0xedb88320:0);return c>>>0;});
    const crc=data=>{let c=0xffffffff;for(const b of data)c=(c>>>8)^table[(c^b)&255];return(c^0xffffffff)>>>0;};
    d.bl={commands:[],data:[],size:0,crc:0,verified:false,erases:0,delay:options.delay||3,failEnd:!!options.failEnd,wrongOffset:!!options.wrongOffset,openFailures:options.openFailures||0,pickers:0,portLists:0,helloPort:null};
    if(d.boot)d.emitting=false;
    const originalHandle=d.handle;d.handle=function(id,seq,pl){
      if(id===0x16){this.commands.push({id,payload:[]});this.boot=true;this.emitting=false;return this.ack(id,0,0);}
      return originalHandle.call(this,id,seq,pl);
    };
    const appReceive=d.onHostBytes;
    d.onHostBytes=function(chunk, source){
      if(!this.boot)return appReceive.call(this,chunk, source);
      const out=source||p;
      const s=this.bl,v=new DataView(chunk.buffer,chunk.byteOffset,chunk.byteLength);
      if(chunk[0]!==0x42||chunk[1]!==0x4c||chunk[2]!==1)throw new Error('Application request leaked into Bootloader');
      const cmd=chunk[3],addr=v.getUint32(6,true),len=v.getUint32(10,true),imageCrc=v.getUint32(14,true);
      s.commands.push(cmd);let value=0,status=0;
      if(cmd===1){value=options.wrongBase?0x08000000:0x08008000;s.helloPort=out;}
      else if(cmd===2){s.erases++;s.size=len;s.crc=imageCrc;s.data=[];s.verified=false;}
      else if(cmd===3){
        if(addr!==0x08008000+s.data.length||len!==chunk.length-18||crc(chunk.slice(18))!==imageCrc)status=2;
        else{s.data.push(...chunk.slice(18));value=s.data.length;if(s.wrongOffset)value--;}
      }else if(cmd===4){
        if(s.failEnd||s.data.length!==s.size||crc(s.data)!==s.crc)status=3;
        else{s.verified=true;value=s.data.length;}
      }else if(cmd===5){s.verified=false;}
      else if(cmd===6){
        if(!s.verified)throw new Error('BOOT before verified END');
        this.reboot();this.boot=false;this.emitting=true;
        if(options.staleReconnect){
          const dead=this.bl.bootPort;
          dead.open=function(opts){this.openCalls.push({...opts,hung:true});return new Promise(()=>{});};
          const app=window.__mock.makePort({usbVendorId:0x2e3c,usbProductId:0xf401});
          app.open=function(opts){return originalOpen.call(this,opts);};
          this.appPort=app;
          const arm=()=>{
            if(dead.openCalls.length>0 && window.__mock.listenerCount('connect')>0){window.__mock.emit('connect', app);return;}
            setTimeout(arm, 50);
          };
          setTimeout(arm, 50);
        } else if(options.usb){
          const app=window.__mock.makePort({usbVendorId:0x2e3c,usbProductId:0xf401});
          app.open=function(opts){return originalOpen.call(this,opts);};
          granted=[app];this.appPort=app;
        }
      }
      else status=1;
      const response=new Uint8Array(14),rv=new DataView(response.buffer);response.set([0x42,0x4c,1,cmd|128,status,0]);rv.setUint32(6,value,true);rv.setUint32(10,crc(response.slice(0,10)),true);
      // Include framing noise and split each ACK across stream reads.
      setTimeout(()=>{out.push(Uint8Array.from([0x00,0x42,0xff,...response.slice(0,7)]));setTimeout(()=>out.push(response.slice(7)),1);},cmd===3?s.delay:3);
    };
    const originalOpen=p.open;p.open=async function(opts){
      if(d.boot&&d.bl.openFailures-->0)throw new DOMException('USB is re-enumerating','NetworkError');
      return originalOpen.call(this,opts);
    };
    const serial=navigator.serial,originalPicker=serial.requestPort;serial.requestPort=async()=>{d.bl.pickers++;return originalPicker();};
    if(options.usb) p.getInfo=()=>({usbVendorId:0x2e3c,usbProductId:0xf401});
    let granted=[p];
    const makeBoard=()=>{
      const port=window.__mock.makePort({usbVendorId:0x2e3c,usbProductId:0xf401});
      port.open=function(opts){return originalOpen.call(this,opts);};
      return port;
    };
    serial.getPorts=async()=>{
      d.bl.portLists++;
      if(options.usb) return granted.slice();
      throw new Error('Must not pick a similar device during update');
    };
    const enterHandle=d.handle;
    d.handle=function(id,seq,pl){
      const reply=enterHandle.call(this,id,seq,pl);
      if(id===0x16 && options.usb){
        const neu=makeBoard();
        const extra=options.ambiguous?makeBoard():null;
        p.open=async()=>{throw new DOMException('USB removed','NetworkError');};
        granted=extra?[neu,extra]:[neu];
        this.bl.bootPort=neu;
      }
      return reply;
    };
  },options);
  await page.goto(base,{waitUntil:'networkidle0'});
  await page.$eval('#firmwarePanel',e=>e.open=true);
  if(!options.boot){await page.click('#connectBtn');await page.waitForFunction(()=>window.__gyro.state().deviceConfig?.version===3);}
  await (await page.$('#firmwareFile')).uploadFile(binary);
  await page.waitForFunction(()=>window.__gyro.state().firmware.imageBytes===4097);
  return page;
}
const wait=(page,phase)=>page.waitForFunction(phase=>window.__gyro.state().firmware.phase===phase&&!window.__gyro.state().firmware.busy,{timeout:20000},phase);
try {
  const page=await setup();
  await (await page.$('#firmwareFile')).uploadFile(wrong);await page.waitForFunction(()=>window.__gyro.state().firmware.phase==='invalid');
  assert.equal(await page.$eval('#firmwareStartBtn',e=>e.disabled),true);assert.equal(await page.evaluate(()=>window.__mock.dev.bl.erases),0);
  await (await page.$('#firmwareFile')).uploadFile(binary);await page.waitForFunction(()=>window.__gyro.state().firmware.imageBytes===4097);
  await page.click('#firmwareStartBtn');await page.waitForFunction(()=>window.__gyro.state().firmware.busy);
  assert.equal(await page.$eval('#disconnectBtn',e=>e.disabled),true);assert.equal(await page.$eval('#firmwareFile',e=>e.disabled),true);
  assert.equal(await page.$eval('.settings .grp',e=>e.inert),true);
  await wait(page,'complete');
  const full=await page.evaluate(()=>({data:window.__mock.dev.bl.data,commands:window.__mock.dev.bl.commands,pickers:window.__mock.dev.bl.pickers,opens:window.__mock.port.openCalls,state:window.__gyro.state(),progress:document.getElementById('firmwareProgress').value}));
  assert.deepEqual(Buffer.from(full.data),bytes);assert.equal(full.commands.filter(x=>x===2).length,1);assert.equal(full.commands.at(-1),6);
  assert.equal(full.pickers,1);assert.equal(full.state.running,true);assert.equal(full.progress,100);assert.equal(full.state.reconnecting,false);
  assert.equal(await page.evaluate(()=>window.__mock.dev.bl.portLists),0);
  results.push('invalid-image-no-erase-and-uart-complete-reconnect-with-port-exclusivity');
  await page.close();

  const uartRetry=await setup({openFailures:3});await uartRetry.click('#firmwareStartBtn');await wait(uartRetry,'complete');
  assert.equal(await uartRetry.evaluate(()=>window.__mock.dev.bl.pickers),1);assert.equal(await uartRetry.evaluate(()=>window.__mock.dev.bl.erases),1);
  assert.equal(await uartRetry.evaluate(()=>window.__mock.dev.bl.portLists),0);
  results.push('uart-reopen-same-port-without-listing-other-devices');await uartRetry.close();

  const usb=await setup({usb:true});
  assert.match(await usb.$eval('#firmwareLink',e=>e.textContent),/USB/);
  await usb.click('#firmwareStartBtn');await wait(usb,'complete');
  const usbResult=await usb.evaluate(()=>{
    const d=window.__mock.dev, origin=window.__mock.port;
    return {pickers:d.bl.pickers,erases:d.bl.erases,portLists:d.bl.portLists,
      helloIsBoot:d.bl.helloPort===d.bl.bootPort,helloIsOrigin:d.bl.helloPort===origin,
      originOpens:origin.openCalls.length,appOpens:d.appPort?d.appPort.openCalls.length:0,
      link:document.getElementById('firmwareStatus').textContent};
  });
  assert.equal(usbResult.pickers,1);assert.equal(usbResult.erases,1);assert.ok(usbResult.portLists>0);
  assert.equal(usbResult.helloIsBoot,true);assert.equal(usbResult.helloIsOrigin,false);
  assert.equal(usbResult.originOpens,1);assert.ok(usbResult.appOpens>=1);
  assert.match(usbResult.link,/USB 已重新连接/);
  results.push('usb-reenumeration-one-new-granted-port-then-app-port-without-picker');await usb.close();

  const stale=await setup({usb:true,staleReconnect:true});
  await stale.click('#firmwareStartBtn');await wait(stale,'complete');
  const staleResult=await stale.evaluate(()=>{
    const d=window.__mock.dev;
    return {pickers:d.bl.pickers,erases:d.bl.erases,
      progress:document.getElementById('firmwareProgress').value,
      link:document.getElementById('firmwareStatus').textContent,
      deadHangs:d.bl.bootPort?d.bl.bootPort.openCalls.filter(x=>x.hung).length:0,
      appOpens:d.appPort?d.appPort.openCalls.length:0};
  });
  assert.equal(staleResult.pickers,1);assert.equal(staleResult.erases,1);assert.equal(staleResult.progress,100);
  assert.ok(staleResult.deadHangs>=1);assert.ok(staleResult.appOpens>=1);
  assert.match(staleResult.link,/USB 已重新连接/);
  results.push('usb-stale-open-then-connect-event-reaches-100-without-picker');await stale.close();

  const ambiguous=await setup({usb:true,ambiguous:true});
  await ambiguous.click('#firmwareStartBtn');await wait(ambiguous,'failed');
  const ambiguousText=await ambiguous.$eval('#firmwareStatus',e=>e.textContent);
  assert.match(ambiguousText,/多块相同的 USB/);
  assert.equal(await ambiguous.evaluate(()=>window.__mock.dev.bl.erases),0);
  assert.equal(await ambiguous.evaluate(()=>window.__mock.dev.bl.pickers),1);
  assert.equal(await ambiguous.$eval('#firmwareRecovery',e=>e.checked),true);
  results.push('usb-two-new-boards-stop-before-erase');await ambiguous.close();

  const failure=await setup({failEnd:true});await failure.click('#firmwareStartBtn');await wait(failure,'failed');
  assert.equal(await failure.evaluate(()=>window.__mock.dev.bl.commands.includes(6)),false);
  assert.ok((await failure.$eval('#firmwareStatus',e=>e.textContent)).includes('未发送应用启动命令'));
  assert.equal(await failure.$eval('#firmwareRecovery',e=>e.checked),true);assert.equal(await failure.evaluate(()=>window.__gyro.state().reconnecting),false);
  await failure.evaluate(()=>window.__mock.dev.bl.failEnd=false);await failure.click('#firmwareStartBtn');await wait(failure,'complete');
  assert.equal(await failure.evaluate(()=>window.__mock.dev.bl.erases),2);assert.equal(await failure.evaluate(()=>window.__mock.dev.bl.pickers),1);
  results.push('end-crc-failure-never-boots-and-full-recovery-retry');await failure.close();

  const offset=await setup({wrongOffset:true});await offset.click('#firmwareStartBtn');await wait(offset,'failed');
  assert.equal(await offset.evaluate(()=>window.__mock.dev.bl.commands.filter(x=>x===3).length),1);
  assert.equal(await offset.evaluate(()=>window.__mock.dev.bl.commands.includes(4)||window.__mock.dev.bl.commands.includes(6)),false);
  results.push('wrong-offset-stops-without-data-retry-or-end-boot');await offset.close();

  const stopped=await setup({delay:35});await stopped.click('#firmwareStartBtn');await stopped.waitForFunction(()=>window.__gyro.state().firmware.phase==='write');
  await stopped.click('#firmwareStopBtn');await wait(stopped,'failed');
  const stopCommands=await stopped.evaluate(()=>window.__mock.dev.bl.commands);assert.equal(stopCommands.at(-1),5);assert.equal(stopCommands.includes(6),false);
  results.push('stop-after-current-block-aborts-and-keeps-maintenance-mode');await stopped.close();

  const recovery=await setup({boot:true});await recovery.click('#firmwareRecovery');await recovery.click('#firmwareStartBtn');await wait(recovery,'complete');
  assert.equal(await recovery.evaluate(()=>window.__mock.dev.commands.some(c=>c.id===0x16)),false);
  assert.equal(await recovery.evaluate(()=>window.__mock.dev.bl.pickers),1);results.push('already-in-boot-recovery-without-app-entry-command');
  for(const width of [1440,820,390,360]){await recovery.setViewport({width,height:1000});assert.ok(await recovery.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));}
  const artifacts=join(here,'../../../artifacts/web-host');mkdirSync(artifacts,{recursive:true});
  await recovery.setViewport({width:1440,height:1000});await recovery.screenshot({path:join(artifacts,'firmware-upgrade-browser-20261001fw1.png'),fullPage:true});
  results.push('expanded-upgrader-responsive-1440-820-390-360');await recovery.close();
  const baseMismatch=await setup({wrongBase:true});await baseMismatch.click('#firmwareStartBtn');await wait(baseMismatch,'failed');
  assert.equal(await baseMismatch.evaluate(()=>window.__mock.dev.bl.erases),0);results.push('wrong-boot-partition-rejected-before-begin');await baseMismatch.close();
  writeFileSync(join(artifacts,'firmware-upgrade-browser-20261001fw1.json'),JSON.stringify({passed:true,scope:'mock serial; no hardware opened',results},null,2));
  console.log('PASS firmware browser:',results.join(', '));
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
