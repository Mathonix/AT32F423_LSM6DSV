import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,existsSync,mkdirSync} from 'node:fs';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';
const here=dirname(fileURLToPath(import.meta.url)), base=process.env.BASE||'http://127.0.0.1:8798';
const chrome=process.env.CHROME||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser=await puppeteer.launch({executablePath:chrome,headless:true}), results=[];
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function setup(mode) {
  const page=await browser.newPage();
  await page.evaluateOnNewDocument(readFileSync(join(here,'mock-serial.js'),'utf8'));
  await page.evaluateOnNewDocument(mode=>{
    const m=window.__mock, d=m.dev, p=m.port, serial=navigator.serial;
    Object.assign(d,{extended:true,configVersion:2,capabilities:31,canExtended:true});
    const other=new p.constructor(), open=p.open.bind(p), request=serial.requestPort, handle=d.handle;
    const t=window.__reconnectTest={mode,available:true,requests:0,attempts:0,queryAttempts:0,openDelay:0,quietUntil:0,other};
    serial.requestPort=async()=>{t.requests++;return request();};
    serial.getPorts=async()=>[other,p]; // Identical VID/PID, deliberately wrong order.
    p.open=async options=>{
      t.attempts++;
      if(!t.available) throw new DOMException('Port is not available','NetworkError');
      if(t.openDelay) await new Promise(r=>setTimeout(r,t.openDelay));
      return open(options);
    };
    d.handle=function(id,seq,payload){
      if(id===0x1f) t.queryAttempts++;
      if([0x1f,0x14].includes(id)&&Date.now()<t.quietUntil) return null;
      const reply=handle.call(this,id,seq,payload);
      if(id===0x1e&&payload[2]&&!this.failSave) {
        if(mode==='quiet') t.quietUntil=Date.now()+2400;
        if(['usb-loss','lost-ack','cancel'].includes(mode)) {
          t.available=false;
          setTimeout(()=>m.unplug(),mode==='lost-ack'?0:15);
          if(mode!=='cancel') setTimeout(()=>{t.available=true;},1800);
        }
        if(mode==='lost-ack') return null;
      }
      return reply;
    };
  },mode);
  await page.goto(base,{waitUntil:'networkidle0'});
  await page.select('#baud','115200');
  await page.click('#connectBtn');
  await page.waitForFunction(()=>window.__gyro.state().deviceConfig?.version===2);
  await page.click('#enterBtn'); await page.waitForFunction(()=>window.__gyro.state().setting);
  return page;
}
async function restart(page) { await page.click('#restartNow'); await page.click('#applyBtn'); }
try {
  for(const mode of ['uart','usb-loss','lost-ack','quiet']) {
    const page=await setup(mode); await restart(page);
    await page.waitForFunction(()=>window.__reconnectTest.attempts>1);
    await page.waitForFunction(()=>window.__gyro.state().running&&!window.__gyro.state().reconnecting&&window.__gyro.state().deviceConfig?.version===2,{timeout:12000});
    const state=await page.evaluate(()=>({requests:window.__reconnectTest.requests,attempts:window.__reconnectTest.attempts,
      opens:window.__mock.port.openCalls,otherOpens:window.__reconnectTest.other.openCalls.length,queries:window.__reconnectTest.queryAttempts}));
    assert.equal(state.requests,1); assert.equal(state.otherOpens,0); assert.equal(state.opens.length,2);
    assert.ok(state.opens.every(o=>o.baudRate===115200&&o.bufferSize===65536));
    if(mode==='quiet') assert.ok(state.queries>2);
    results.push({mode,...state}); console.log('PASS',mode,state.attempts,'attempts, same port, one picker'); await page.close();
  }
  const failed=await setup('failure'); await failed.evaluate(()=>window.__mock.dev.failSave=true); await restart(failed);
  await failed.waitForFunction(()=>document.getElementById('message').textContent.includes('保存失败'));
  await sleep(1300); assert.equal(await failed.evaluate(()=>window.__reconnectTest.attempts),1);
  assert.equal(await failed.evaluate(()=>window.__gyro.state().reconnecting),false); results.push({mode:'failed-save',passed:true}); await failed.close();
  const cancel=await setup('cancel'); await restart(cancel);
  await cancel.waitForFunction(()=>window.__reconnectTest.attempts>1);
  await cancel.evaluate(()=>{window.__reconnectTest.available=true;window.__reconnectTest.openDelay=1000;});
  await cancel.waitForFunction(()=>window.__reconnectTest.attempts>2);
  await cancel.click('#disconnectBtn'); await sleep(1800);
  const canceled=await cancel.evaluate(()=>({running:window.__gyro.state().running,reconnecting:window.__gyro.state().reconnecting,readable:!!window.__mock.port.readable,attempts:window.__reconnectTest.attempts}));
  assert.equal(canceled.running,false);assert.equal(canceled.reconnecting,false);assert.equal(canceled.readable,false);
  await sleep(900);assert.equal(await cancel.evaluate(()=>window.__reconnectTest.attempts),canceled.attempts);
  results.push({mode:'cancel-during-open',...canceled});await cancel.close();
  const waiting=await setup('cancel');await restart(waiting);
  await waiting.waitForFunction(()=>window.__reconnectTest.attempts>1);
  await waiting.click('#disconnectBtn');
  assert.equal(await waiting.$eval('#linkState',e=>e.textContent),'未连接');
  assert.equal(await waiting.$eval('#connectBtn',e=>e.disabled),false);
  const attempts=await waiting.evaluate(()=>window.__reconnectTest.attempts);
  await sleep(1000);assert.equal(await waiting.evaluate(()=>window.__reconnectTest.attempts),attempts);
  results.push({mode:'cancel-between-attempts',passed:true});await waiting.close();
  const physical=await setup('physical'); await physical.click('#fastStart'); await physical.click('#applyBtn');
  await physical.waitForFunction(()=>window.__gyro.state().fusionPendingRestart&&!window.__gyro.state().pendingStartup);
  await physical.evaluate(()=>{window.__mock.dev.reboot();window.__mock.unplug();});
  await physical.waitForFunction(()=>window.__reconnectTest.attempts>1&&!window.__gyro.state().reconnecting&&window.__gyro.state().deviceConfig?.activeFast===1,{timeout:12000});
  assert.equal(await physical.evaluate(()=>window.__reconnectTest.requests),1); results.push({mode:'saved-then-physical-reboot',passed:true});
  const out=join(here,'../../../artifacts/web-host');mkdirSync(out,{recursive:true});
  await physical.screenshot({path:join(out,'reconnect-20261001.png'),fullPage:true});
  writeFileSync(join(out,'reconnect-browser-20261001.json'),JSON.stringify({base,results,passed:true},null,2));
  console.log('PASS: reboot reconnect, loss before ACK, boot delay, save failure, cancellation race and device identity');
} finally {await browser.close();}
