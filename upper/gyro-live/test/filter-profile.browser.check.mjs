import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,existsSync,mkdirSync} from 'node:fs';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';
const here=dirname(fileURLToPath(import.meta.url)),base=process.env.BASE||'http://127.0.0.1:8798';
const chrome=process.env.CHROME||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser=await puppeteer.launch({executablePath:chrome,headless:true}),results=[];
try {
  const page=await browser.newPage();await page.setViewport({width:1440,height:1100});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.evaluateOnNewDocument(readFileSync(join(here,'mock-serial.js'),'utf8'));
  await page.evaluateOnNewDocument(()=>Object.assign(window.__mock.dev,{extended:true,configVersion:3,capabilities:255,canExtended:true,filterExtended:true}));
  await page.goto(base,{waitUntil:'networkidle0'});await page.click('#connectBtn');
  const wait=(fn,...args)=>page.waitForFunction(fn,{timeout:12000},...args);
  await wait(()=>window.__gyro.state().filterConfig?.active===1);
  assert.equal(await page.$eval('#filterApply',e=>e.disabled),true);
  await page.click('#enterBtn');await wait(()=>window.__gyro.state().setting);
  for(const mode of [0,2,1]) {
    await page.select('#filterProfile',String(mode));await page.click('#filterApply');
    await wait(mode=>window.__gyro.state().filterConfig?.active===mode&&!window.__gyro.state().pendingFilter,mode);
    assert.equal(await page.evaluate(()=>window.__mock.dev.savedFilterProfile),mode);
  }
  results.push('three-presets-live-persist');
  await page.click('#filterPersist');await page.select('#filterProfile','2');await page.click('#filterApply');
  await wait(()=>window.__gyro.state().filterConfig?.active===2&&!window.__gyro.state().pendingFilter);
  assert.equal(await page.evaluate(()=>window.__mock.dev.savedFilterProfile),1);
  await page.evaluate(()=>window.__mock.dev.reboot());await page.click('#filterRefresh');
  await wait(()=>window.__gyro.state().filterConfig?.active===1);results.push('volatile-mode-reboot-restores-saved');
  await page.click('#enterBtn');await wait(()=>window.__gyro.state().setting);await page.click('#filterPersist');
  await page.select('#filterProfile','2');await page.evaluate(()=>window.__mock.dev.failSave=true);await page.click('#filterApply');
  await wait(()=>!window.__gyro.state().pendingFilter);
  assert.equal(await page.$eval('#filterProfile',e=>e.value),'2');
  assert.equal(await page.evaluate(()=>window.__mock.dev.filterProfile),1);
  await page.click('#filterRefresh');await new Promise(r=>setTimeout(r,100));assert.equal(await page.$eval('#filterProfile',e=>e.value),'2');
  await page.evaluate(()=>window.__mock.dev.failSave=false);await page.click('#filterDiscard');results.push('failed-save-and-refresh-preserve-draft');
  await page.evaluate(()=>{
    const d=window.__mock.dev;window.__originalFilterHandle=d.handle;
    d.handle=function(id,seq,payload){const reply=window.__originalFilterHandle.call(this,id,seq,payload);if(id===0x27){setTimeout(()=>window.__mock.port.push(reply),200);return null;}return reply;};
  });
  await page.select('#filterProfile','0');await page.click('#filterApply');
  /* Simulate an external draft edit while the delayed ACK is in flight. */
  await page.$eval('#filterProfile',e=>{e.value='2';e.dispatchEvent(new Event('change',{bubbles:true}));});
  await wait(()=>!window.__gyro.state().pendingFilter);assert.equal(await page.$eval('#filterProfile',e=>e.value),'2');
  await page.evaluate(()=>window.__mock.dev.handle=window.__originalFilterHandle);await page.click('#filterDiscard');results.push('delayed-ack-preserves-newer-draft');
  await page.$eval('#filterControls details',e=>e.open=true);await page.click('#diagRead');
  await wait(()=>window.__gyro.state().fusionDiagnostic?.timestamp===123456);
  await page.click('#diagRecord');await wait(()=>window.__gyro.state().diagCount>=5);await page.click('#diagRecord');
  assert.equal(await page.$eval('#diagExport',e=>e.disabled),false);results.push('diagnostic-read-record-export-enabled');
  await page.evaluate(()=>window.__mock.dev.filterExtended=false);await page.click('#filterRefresh');
  await wait(()=>window.__gyro.state().filterConfig===null);
  assert.equal(await page.$eval('#filterApply',e=>e.disabled),true);assert.match(await page.$eval('#filterState',e=>e.textContent),/不支持/);results.push('older-firmware-disabled');
  await page.evaluate(()=>window.__mock.dev.filterExtended=true);await page.click('#filterRefresh');
  await wait(()=>window.__gyro.state().filterConfig!==null);
  const out=join(here,'../../../artifacts/web-host');mkdirSync(out,{recursive:true});
  for(const width of [1440,820,390,360]) {
    await page.setViewport({width,height:1100});assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    if(width===1440||width===390) await page.screenshot({path:join(out,`filter-profile-${width}.png`),fullPage:true});
  }
  assert.deepEqual(errors,[]);results.push('responsive-layout-no-page-errors');
  writeFileSync(join(out,'filter-profile-browser.json'),JSON.stringify({base,results,passed:true},null,2));
  console.log('PASS filter profiles:',results.join(', '));
} finally {await browser.close();}
