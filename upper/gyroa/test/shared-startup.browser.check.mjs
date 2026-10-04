// Mock Web Serial regression; no physical board writes.
import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import {readFileSync,existsSync,mkdirSync} from 'node:fs';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';
const here=dirname(fileURLToPath(import.meta.url));
const chrome=process.env.CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','/usr/bin/google-chrome'].find(existsSync);
const browser=await puppeteer.launch({executablePath:chrome,headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
const errors=[];
const mock=['mock-serial.js','filter-mock.js','fwver-mock.js'].map(n=>readFileSync(join(here,n),'utf8')).join('\n');
try {
  async function open(version=4) {
    const page=await browser.newPage();await page.setViewport({width:1440,height:900});
    page.on('pageerror',e=>errors.push(e.message));
    await page.evaluateOnNewDocument(mock);
    await page.evaluateOnNewDocument(version=>{
      Object.assign(window.__mock.dev,{extended:true,configVersion:version,capabilities:127,canExtended:true,modelEnabled:version===4,activeFast:0,savedFast:0});
      window.__mock.fwver.text='20261003e';
    },version);
    await page.goto(process.env.BASE || 'http://127.0.0.1:8833',{waitUntil:'networkidle0'});
    await page.click('#connectBtn');await page.waitForFunction(v=>window.__gyro.state().deviceConfig?.version===v,{timeout:8000},version);
    return page;
  }
  const idle=p=>p.waitForFunction(()=>!window.__gyro.state().pendingStartup && !window.__gyro.state().setting && !window.GyroUI.gate.busy && !window.__gyro.state().reconnecting,{timeout:10000});
  const field=(p,value)=>p.$eval('#gyroInitSeconds',(e,v)=>{e.value=v;e.dispatchEvent(new Event('input',{bubbles:true}));},value);
  const count=p=>p.evaluate(()=>window.__mock.dev.commands.filter(c=>c.id===0x1e).length);
  const p=await open();
  await p.waitForFunction(()=>window.__gyro.deviceModel().text==='AT32' && window.__gyro.fwVersion().text==='20261003e');
  assert.equal(await p.$eval('#deviceModel',e=>e.textContent),'AT32');
  assert.equal(await p.evaluate(()=>window.__mock.dev.commands.filter(c=>c.id===0x35).length),1);
  await p.click('.nav-item[data-nav="settings"]');
  assert.equal(await p.$eval('#fastStartRow',e=>e.hidden),true);
  assert.equal(await p.$eval('#gyroInitRow',e=>e.hidden),false);
  assert.equal(await p.$eval('#gyroInitSeconds',e=>e.min),'0');
  assert.equal(await p.$eval('#gyroInitSeconds',e=>e.value),'2');
  assert.match(await p.$eval('#gyroInitHint',e=>e.innerText),/同时进行/);
  const other=await p.evaluate(()=>JSON.stringify({can:window.__mock.dev.can,outputs:window.__mock.dev.outputs,range:window.__mock.dev.savedRangeDps,rate:window.__mock.dev.savedOutputHz}));
  const before=await count(p);
  for(const invalid of ['', '-0.1','60.1']) {
    await field(p,invalid);await p.click('#applyBtn');await idle(p);
    assert.match(await p.$eval('#message',e=>e.textContent),/0～60 秒/);
    assert.equal(await count(p),before);
  }
  for(const seconds of [0,60,2]) {
    await field(p,String(seconds));await p.click('#applyBtn');
    await p.waitForFunction(ms=>window.__gyro.state().deviceConfig.savedInitMs===ms && !window.__gyro.state().pendingStartup,{timeout:8000},seconds*1000);
    await idle(p);
    assert.equal(await p.evaluate(()=>window.__gyro.state().deviceConfig.savedFast),+(seconds===0));
    assert.equal(await p.$eval('#gyroInitSeconds',e=>e.disabled),false);
    const pl=await p.evaluate(()=>window.__mock.dev.commands.filter(c=>c.id===0x1e).at(-1).payload);
    assert.deepEqual(pl,[1,+(seconds===0),0,(seconds*1000)&255,(seconds*1000)>>8,232,3]);
  }
  assert.equal(await p.evaluate(()=>JSON.stringify({can:window.__mock.dev.can,outputs:window.__mock.dev.outputs,range:window.__mock.dev.savedRangeDps,rate:window.__mock.dev.savedOutputHz})),other);
  await p.evaluate(()=>window.__mock.dev.failSave=true);await field(p,'3');await p.click('#applyBtn');await idle(p);
  assert.equal(await p.$eval('#gyroInitSeconds',e=>e.value),'3');
  assert.equal(await p.evaluate(()=>window.__gyro.state().deviceConfig.savedInitMs),2000);
  await p.evaluate(()=>window.__mock.dev.failSave=false);await p.click('#startupDiscard');
  assert.equal(await p.$eval('#gyroInitSeconds',e=>e.value),'2');
  await p.select('#gyroRange','2000');await p.click('#applyBtn');await idle(p);
  assert.equal(await p.evaluate(()=>window.__gyro.state().deviceConfig.savedRangeDps),2000);
  await p.click('#advPanel > summary');
  await p.waitForFunction(()=>document.querySelector('#stStartup tr[data-k="fast"]').hidden);
  assert.equal(await p.$eval('#stStartup tr[data-k="init"] th',e=>e.textContent),'启动窗口');
  await p.click('#advPanel > summary');
  await p.click('.nav-item[data-nav="output"]');
  await p.$eval('#outHz',e=>{e.value='500';e.dispatchEvent(new Event('input',{bubbles:true}));});
  await p.click('#outputApplyAll');
  await p.waitForFunction(()=>window.__gyro.state().deviceConfig.savedOutputHz===500 && !window.__gyro.state().pendingRate);
  await p.click('.nav-item[data-nav="settings"]');
  console.log('PASS v4: invalid input, 0/2/60s, derived fast flag, exact payload/readback, preserved outputs/CAN/range/rate, failed save/discard');
  for(const [width,height] of [[1440,900],[390,844]]) {
    await p.setViewport({width,height});
    await new Promise(r=>setTimeout(r,350));
    assert.ok(await p.evaluate(()=>document.scrollingElement.scrollWidth<=innerWidth),'no horizontal overflow');
    if(process.env.SHOTS) {mkdirSync(process.env.SHOTS,{recursive:true});await p.screenshot({path:join(process.env.SHOTS,`startup-${width}.png`),fullPage:true});}
  }
  await p.setViewport({width:1440,height:900});
  for(const seconds of [0,60]) {
    await field(p,String(seconds));await p.click('#applyBtn');await idle(p);
    await p.click('.nav-item[data-nav="status"]');
    const mode=await p.evaluate(()=>1-window.__gyro.state().deviceConfig.activeMode);
    const opens=await p.evaluate(()=>window.__mock.port.openCalls.length);
    await p.waitForFunction(()=>!document.querySelector('#qsFusion button').disabled);
    await p.click(`#qsFusion button[data-mode="${mode}"]`);
    await p.waitForFunction(m=>!window.GyroUI.quick.state.fusion && !window.__gyro.state().reconnecting && window.__gyro.state().deviceConfig?.activeMode===m,{timeout:15000},mode);
    assert.equal(await p.evaluate(()=>window.__gyro.state().deviceConfig.activeInitMs),seconds*1000);
    assert.equal(await p.evaluate(()=>window.__gyro.state().deviceConfig.activeRangeDps),2000);
    assert.equal(await p.evaluate(()=>window.__gyro.state().deviceConfig.savedOutputHz),500);
    assert.ok(await p.evaluate(n=>window.__mock.port.openCalls.length>n,opens),'same port reopened');
    await p.click('.nav-item[data-nav="settings"]');
  }
  console.log('PASS v4 quick fusion at T=0 and 60s: same-port reconnect, T/range/rate preserved (reboot timing simulated)');
  await p.close();
  for(const version of [1,2,3]) {
    const legacy=await open(version);await legacy.click('.nav-item[data-nav="settings"]');
    assert.equal(await legacy.$eval('#fastStartRow',e=>e.hidden),false);
    assert.equal(await legacy.$eval('#gyroInitSeconds',e=>e.min),'0.1');
    await legacy.click('#fastStart');
    assert.equal(await legacy.$eval('#gyroInitRow',e=>e.hidden),true);
    await legacy.click('#fastStart');
    if(version>=2) {
      await field(legacy,'0');const n=await count(legacy);await legacy.click('#applyBtn');await idle(legacy);assert.equal(await count(legacy),n);
      await field(legacy,'2');await legacy.click('#applyBtn');await idle(legacy);
      assert.equal(await legacy.evaluate(()=>window.__gyro.state().deviceConfig.savedInitMs),2000);
    } else assert.equal(await legacy.$eval('#gyroInitSeconds',e=>e.disabled),true);
    await legacy.waitForFunction(()=>window.__gyro.deviceModel().supported===false);
    assert.equal(await legacy.$eval('#deviceModel',e=>e.textContent),'--');
    await legacy.close();
  }
  assert.deepEqual(errors,[]);console.log('PASS legacy v1/v2/v3 controls and unsupported model query');
} finally {await browser.close();}
