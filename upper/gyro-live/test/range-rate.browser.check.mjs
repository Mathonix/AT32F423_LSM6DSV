import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,existsSync,mkdirSync} from 'node:fs';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';
const here=dirname(fileURLToPath(import.meta.url)),base=process.env.BASE||'http://127.0.0.1:8798';
const chrome=process.env.CHROME||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser=await puppeteer.launch({executablePath:chrome,headless:true}),results=[];
try {
  const page=await browser.newPage();await page.setViewport({width:1440,height:1000});
  await page.evaluateOnNewDocument(readFileSync(join(here,'mock-serial.js'),'utf8'));
  await page.evaluateOnNewDocument(()=>Object.assign(window.__mock.dev,{extended:true,configVersion:3,capabilities:127,canExtended:true}));
  await page.goto(base,{waitUntil:'networkidle0'});await page.select('#baud','115200');await page.click('#connectBtn');
  const wait=(fn,...args)=>page.waitForFunction(fn,{timeout:12000},...args);
  const input=async(id,value)=>page.$eval('#'+id,(e,value)=>{e.value=value;e.dispatchEvent(new Event('input',{bubbles:true}));},String(value));
  const enter=async()=>{await page.click('#enterBtn');await wait(()=>window.__gyro.state().setting);};
  await wait(()=>window.__gyro.state().deviceConfig?.version===3&&window.__gyro.state().canConfig?.ready);
  await input('outHz',500);await page.click('#rateApplyBtn');
  await wait(()=>document.getElementById('message').textContent.includes('500 Hz 已应用并保存'));
  assert.equal(await page.evaluate(()=>window.__mock.dev.savedOutputHz),500);results.push('serial-rate-persist');
  await page.evaluate(()=>{
    const d=window.__mock.dev;window.__originalRateHandle=d.handle;
    d.handle=function(id,seq,payload){const reply=window.__originalRateHandle.call(this,id,seq,payload);if(id===0x1d){setTimeout(()=>window.__mock.port.push(reply),220);return null;}return reply;};
  });
  await input('outHz',1000);await page.click('#rateApplyBtn');await input('outHz',500);
  await wait(()=>document.getElementById('message').textContent.includes('1000 Hz 已应用并保存'));
  assert.equal(await page.$eval('#outHz',e=>e.value),'500');
  await page.evaluate(()=>window.__mock.dev.handle=window.__originalRateHandle);
  await page.click('#rateApplyBtn');await wait(()=>document.getElementById('message').textContent.includes('500 Hz 已应用并保存'));
  results.push('rate-edit-during-save');
  await input('outHz',250);await page.click('#refreshBtn');
  await new Promise(r=>setTimeout(r,150));assert.equal(await page.$eval('#outHz',e=>e.value),'250');
  await page.evaluate(()=>window.__mock.dev.failSave=true);await page.click('#rateApplyBtn');
  await wait(()=>document.getElementById('message').textContent.includes('输出频率保存失败'));
  assert.equal(await page.evaluate(()=>window.__mock.dev.outHz),500);assert.equal(await page.$eval('#outHz',e=>e.value),'250');
  await page.evaluate(()=>window.__mock.dev.failSave=false);await page.click('#rateApplyBtn');
  await wait(()=>document.getElementById('message').textContent.includes('250 Hz 已应用并保存'));results.push('rate-draft-and-save-failure');
  for(const dps of [125,250,500,1000,2000,4000]) {
    await enter();await page.select('#gyroRange',String(dps));await page.click('#applyBtn');
    await wait(dps=>window.__gyro.state().deviceConfig?.savedRangeDps===dps&&!window.__gyro.state().pendingStartup,dps);
    assert.equal(await page.evaluate(()=>window.__mock.dev.activeRangeDps),1000);
  }
  results.push('six-gyro-ranges-staged');
  await enter();await page.click('#restartNow');await page.click('#applyBtn');
  await wait(()=>window.__gyro.state().running&&!window.__gyro.state().reconnecting&&window.__gyro.state().deviceConfig?.activeRangeDps===4000);
  assert.equal(await page.evaluate(()=>window.__gyro.state().deviceConfig.outHz),250);
  assert.equal(await page.evaluate(()=>window.__mock.port.openCalls.length),2);results.push('range-and-rate-reboot-reconnect');
  await enter();await page.click('#restartNow');await page.select('#gyroRange','125');
  await page.evaluate(()=>window.__mock.dev.failSave=true);await page.click('#applyBtn');
  await wait(()=>document.getElementById('message').textContent.includes('保存失败'));
  await page.click('#refreshBtn');await new Promise(r=>setTimeout(r,150));
  assert.equal(await page.$eval('#gyroRange',e=>e.value),'125');assert.equal(await page.evaluate(()=>window.__mock.dev.savedRangeDps),4000);
  await page.evaluate(()=>window.__mock.dev.failSave=false);await page.click('#startupDiscard');results.push('gyro-draft-and-save-failure');
  await input('canFrequency',50);assert.equal(await page.$eval('#canPeriod',e=>e.value),'20');await page.click('#canApply');
  await wait(()=>window.__gyro.state().canConfig?.active.periodMs===20&&!window.__gyro.state().pendingCan);
  assert.equal(await page.evaluate(()=>window.__mock.dev.savedCan.periodMs),20);
  await input('canFrequency',60);assert.equal(await page.$eval('#canPeriod',e=>e.value),'17');
  assert.match(await page.$eval('#canFrequencyHint',e=>e.textContent),/58\.824 Hz/);
  await page.click('#canRefresh');await new Promise(r=>setTimeout(r,150));assert.equal(await page.$eval('#canFrequency',e=>e.value),'60');
  await input('canFrequency',1001);await page.click('#canApply');assert.match(await page.$eval('#message',e=>e.textContent),/CAN 参数无效/);
  await page.click('#canDiscard');assert.equal(await page.$eval('#canFrequency',e=>e.value),'50');results.push('can-hz-conversion-draft-bounds');
  await page.evaluate(()=>{const d=window.__mock.dev;d.configVersion=2;d.capabilities=31;});await page.click('#refreshBtn');
  await wait(()=>window.__gyro.state().deviceConfig?.version===2);
  assert.equal(await page.$eval('#gyroRange',e=>e.disabled),true);assert.match(await page.$eval('#rateState',e=>e.textContent),/仅本次运行/);
  await input('outHz',500);await page.click('#rateApplyBtn');await wait(()=>document.getElementById('message').textContent.includes('旧固件仅本次运行'));
  assert.equal(await page.evaluate(()=>window.__mock.dev.savedOutputHz),250);results.push('older-firmware-compatible');
  await page.evaluate(()=>{window.__mock.dev.configVersion=3;window.__mock.dev.capabilities=127;window.__mock.dev.reboot();});await page.click('#refreshBtn');
  await wait(()=>window.__gyro.state().deviceConfig?.version===3);
  const out=join(here,'../../../artifacts/web-host');mkdirSync(out,{recursive:true});
  for(const width of [1440,820,390,360]) {
    await page.setViewport({width,height:1000});
    assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    if(width===1440||width===390) await page.screenshot({path:join(out,`range-rate-20261001-${width}.png`),fullPage:true});
  }
  writeFileSync(join(out,'range-rate-browser-20261001.json'),JSON.stringify({base,results,passed:true},null,2));
  console.log('PASS range/rate:',results.join(', '));
} finally {await browser.close();}
