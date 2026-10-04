import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import {readFileSync,existsSync} from 'node:fs';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';
const here=dirname(fileURLToPath(import.meta.url));
const chrome=process.env.CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser=await puppeteer.launch({executablePath:chrome,headless:true});
try {
  const page=await browser.newPage(),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.evaluateOnNewDocument(readFileSync(join(here,'mock-serial.js'),'utf8'));
  await page.evaluateOnNewDocument(()=>Object.assign(window.__mock.dev,{
    extended:true,configVersion:4,capabilities:127,canExtended:true,activeFast:0,savedFast:0,
  }));
  await page.goto(process.env.BASE || 'http://127.0.0.1:8798',{waitUntil:'networkidle0'});
  const wait=f=>page.waitForFunction(f,{timeout:8000});
  const seconds=async value=>page.$eval('#gyroInitSeconds',(e,value)=>{
    e.value=value;e.dispatchEvent(new Event('input',{bubbles:true}));
  },value);
  await page.click('#connectBtn');await wait(()=>window.__gyro.state().deviceConfig?.version===4);
  await page.click('#enterBtn');await wait(()=>window.__gyro.state().setting);
  assert.equal(await page.$eval('#fastStartRow',e=>e.hidden),true);
  assert.equal(await page.$eval('#gyroInitRow',e=>e.hidden),false);
  assert.equal(await page.$eval('#gyroInitSeconds',e=>e.disabled),false);
  assert.equal(await page.$eval('#gyroInitSeconds',e=>e.min),'0');
  assert.match(await page.$eval('#gyroInitHint',e=>e.textContent),/同时进行/);
  const other=await page.evaluate(()=>JSON.stringify({can:window.__mock.dev.can,outputs:window.__mock.dev.outputs,range:window.__mock.dev.savedRangeDps,rate:window.__mock.dev.savedOutputHz}));
  const count=await page.evaluate(()=>window.__mock.dev.commands.filter(c=>c.id===0x1e).length);
  for(const invalid of ['', '-0.1','60.1']) {
    await seconds(invalid);await page.click('#applyBtn');
    assert.match(await page.$eval('#message',e=>e.textContent),/0～60 秒/);
  }
  assert.equal(await page.evaluate(()=>window.__mock.dev.commands.filter(c=>c.id===0x1e).length),count);
  await seconds('0');await page.click('#applyBtn');
  await wait(()=>!window.__gyro.state().pendingStartup && window.__gyro.state().deviceConfig.savedInitMs===0);
  await wait(()=>!window.__gyro.state().setting);
  assert.equal(await page.evaluate(()=>window.__gyro.state().deviceConfig.savedFast),1);
  assert.equal(await page.$eval('#gyroInitRow',e=>e.hidden),false);
  assert.match(await page.$eval('#startupState',e=>e.textContent),/历史零偏/);
  await page.click('#enterBtn');await wait(()=>window.__gyro.state().setting);
  assert.equal(await page.$eval('#gyroInitSeconds',e=>e.disabled),false);
  await seconds('60');await page.click('#applyBtn');
  await wait(()=>!window.__gyro.state().pendingStartup && window.__gyro.state().deviceConfig.savedInitMs===60000);
  await wait(()=>!window.__gyro.state().setting);
  assert.equal(await page.evaluate(()=>window.__gyro.state().deviceConfig.savedFast),0);
  assert.equal(await page.evaluate(()=>JSON.stringify({can:window.__mock.dev.can,outputs:window.__mock.dev.outputs,range:window.__mock.dev.savedRangeDps,rate:window.__mock.dev.savedOutputHz})),other);
  await page.click('#enterBtn');await wait(()=>window.__gyro.state().setting);
  await seconds('2');await page.click('#restartNow');await page.click('#applyBtn');
  await wait(()=>!window.__gyro.state().running);
  await wait(()=>!window.__gyro.state().reconnecting && window.__gyro.state().deviceConfig?.activeInitMs===2000);
  assert.equal(await page.evaluate(()=>window.__gyro.state().fusionPendingRestart),false);
  assert.deepEqual(errors,[]);
  console.log('PASS shared startup v4: 0/2/60s, invalid input, history UI, save/readback, other settings, restart/reconnect');
} finally {await browser.close();}
