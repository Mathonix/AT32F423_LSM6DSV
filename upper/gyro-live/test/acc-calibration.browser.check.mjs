import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,existsSync,mkdirSync} from 'node:fs';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';
import http from 'node:http';
const here=dirname(fileURLToPath(import.meta.url)),root=join(here,'../src/public');
const server=http.createServer((req,res)=>{
  const pathname=new URL(req.url,'http://localhost').pathname;
  const p=join(root,pathname==='/'?'index.html':pathname);
  try {const bytes=readFileSync(p);res.setHeader('Content-Type',p.endsWith('.js')?'text/javascript':p.endsWith('.css')?'text/css':p.endsWith('.html')?'text/html':'application/octet-stream');res.end(bytes);} catch {res.writeHead(404);res.end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const base='http://127.0.0.1:'+server.address().port;
const chrome=process.env.CHROME||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser=await puppeteer.launch({executablePath:chrome,headless:true}),results=[];
try {
  const page=await browser.newPage();await page.setViewport({width:1440,height:1000});
  await page.evaluateOnNewDocument(readFileSync(join(here,'mock-serial.js'),'utf8'));
  await page.evaluateOnNewDocument(()=>{
    const d=window.__mock.dev;Object.assign(d,{extended:true,configVersion:3,capabilities:255});
    d.acc={status:0,phase:0,face:0,mask:0,valid:true,progress:0,error:0};
    d.accFrame=function(){const p=new Uint8Array(60),v=new DataView(p.buffer),s=this.acc;
      p.set([1,s.status,s.phase,s.face,s.mask,1,s.valid?1:0,0]);v.setUint16(8,s.progress,true);v.setUint16(10,s.error,true);
      v.setUint32(12,s.phase===2?1400:0,true);v.setUint32(16,12345,true);v.setUint32(20,40000,true);
      [.01,-.02,.03,1.01,.98,1.02,...(s.raw||[0,0,1])].forEach((x,i)=>v.setFloat32(24+4*i,x,true));
      return window.__mock.frame(10,this.seq++,p);
    };
    const original=d.handle;d.handle=function(id,seq,pl){
      if(id===0x24) {this.commands.push({id,payload:[]});return this.oldCalibration?this.ack(id,1,0):this.accFrame();}
      if(id===0x1c) {this.commands.push({id,payload:[]});Object.assign(this.acc,{status:1,phase:0,face:0,mask:0,progress:0,error:0});return Uint8Array.from([...this.ack(id,0,0x100),...this.accFrame()]);}
      if(id===0x25) {this.commands.push({id,payload:[]});Object.assign(this.acc,{status:4,phase:0,face:0,error:0x608});return Uint8Array.from([...this.ack(id,0,0),...this.accFrame()]);}
      if(id===0x18 && this.acc.status===1) Object.assign(this.acc,{status:4,phase:0,face:0,error:0x608});
      return original.call(this,id,seq,pl);
    };
    window.__accPush=s=>{Object.assign(d.acc,s);window.__mock.port.push(d.accFrame());};
  });
  const wait=fn=>page.waitForFunction(fn,{timeout:8000});
  await page.goto(base,{waitUntil:'networkidle0'});await page.click('#connectBtn');
  await wait(()=>window.__gyro.state().accCalibration?.valid);
  assert.equal(await page.$eval('#acc6Btn',e=>e.disabled),true);
  await page.click('#enterBtn');await wait(()=>window.__gyro.state().setting);
  await page.click('#acc6Btn');await wait(()=>window.__gyro.state().accCalibration?.status===1);
  assert.equal(await page.$eval('#acc6Btn',e=>e.disabled),true);assert.equal(await page.$eval('#accCancelBtn',e=>e.disabled),false);
  results.push('start-requires-settings-and-prevents-double-start');
  await page.evaluate(()=>window.__accPush({phase:2,face:6,mask:0x15,progress:650}));
  await wait(()=>document.getElementById('accStatus').textContent.includes('3/6'));
  assert.equal(await page.$$eval('#accFaces .done',e=>e.length),3);
  assert.match(await page.$eval('#accFaces .current',e=>e.textContent),/\+Z/);assert.equal(await page.$eval('#accFaceProgress',e=>e.value),650);
  assert.equal(await page.$eval('#accRawZ',e=>e.textContent),'+1.0000');
  assert.match(await page.$eval('#accPlacement',e=>e.textContent),/设备识别 \+Z.*Z 轴正方向朝上.*X 0 g \/ Y 0 g \/ Z \+1 g/);
  await page.evaluate(()=>window.__accPush({phase:2,face:1,raw:[-1.012,.018,-.025]}));
  await wait(()=>document.getElementById('accRawX').textContent==='-1.0120');
  assert.match(await page.$eval('#accPlacement',e=>e.textContent),/设备识别 −X.*X 轴正方向朝下/);
  results.push('live-raw-g-and-positive-negative-placement-reference');
  await page.evaluate(()=>window.__accPush({phase:5,face:0,progress:0,raw:[.16,-.025,.99]}));await wait(()=>document.getElementById('accStatus').textContent.includes('运动'));
  assert.match(await page.$eval('#accPlacement',e=>e.textContent),/参考方向 \+Z（按 g 值估计）/);
  assert.match(await page.$eval('#accAdvice',e=>e.textContent),/其他两轴.*0 g.*晃动或角速度/);
  await page.evaluate(()=>window.__accPush({phase:6}));await wait(()=>document.getElementById('accStatus').textContent.includes('这一面已完成'));
  results.push('unordered-faces-motion-and-duplicate-feedback');
  await page.click('#accCancelBtn');await wait(()=>window.__gyro.state().accCalibration?.status===4);
  assert.match(await page.$eval('#accStatus',e=>e.textContent),/取消.*保留/);
  assert.match(await page.$eval('#accParams',e=>e.textContent),/0.01000/);results.push('cancel-keeps-old-calibration');
  assert.match(await page.$eval('#accRawLabel',e=>e.textContent),/末次校准读数.*非实时/);
  assert.equal(await page.$eval('#accRaw',e=>e.classList.contains('stale')),true);
  results.push('terminal-readings-labelled-non-live');
  await page.click('#acc6Btn');await wait(()=>window.__gyro.state().accCalibration?.status===1);
  await page.evaluate(()=>window.__accPush({status:3,phase:7,error:0x609}));await wait(()=>document.getElementById('accStatus').textContent.includes('空间已满'));
  assert.equal(await page.$eval('#acc6Btn',e=>e.disabled),false);results.push('storage-full-and-failure-feedback');
  await page.evaluate(()=>window.__accPush({status:3,phase:7,error:0x606,mask:63,face:0}));
  await wait(()=>document.getElementById('accAdvice').textContent.includes('没有报告具体失败面'));
  assert.match(await page.$eval('#accAdvice',e=>e.textContent),/±0\.15 g.*0\.85~1\.15.*未保存.*轻微倾斜已自动补偿.*静止.*再重新开始/);
  assert.match(await page.$eval('#accParams',e=>e.textContent),/0\.01000/);
  assert.equal(await page.$eval('#accRaw',e=>e.classList.contains('stale')),true);
  results.push('fit-failure-guidance-and-preserved-calibration');
  await page.click('#acc6Btn');await wait(()=>window.__gyro.state().accCalibration?.status===1);
  await page.evaluate(()=>window.__accPush({status:2,phase:4,mask:63,face:0,progress:1000,error:0}));
  await wait(()=>document.getElementById('accStatus').textContent.includes('已保存'));
  assert.equal(await page.$$eval('#accFaces .done',e=>e.length),6);results.push('completion-after-saved-status');
  for(const width of [1440,820,390,360]) {await page.setViewport({width,height:1000});assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));}
  results.push('responsive-1440-820-390-360');
  await page.evaluate(()=>window.__mock.dev.oldCalibration=true);await page.click('#accRefreshBtn');
  await wait(()=>document.getElementById('accStatus').textContent.includes('不支持校准进度'));
  assert.equal(await page.$eval('#acc6Btn',e=>e.disabled),true);results.push('old-firmware-disabled');
  await page.evaluate(()=>window.__mock.dev.oldCalibration=false);await page.click('#accRefreshBtn');await wait(()=>window.__gyro.state().accCalibration?.enabled);
  await page.click('#disconnectBtn');assert.equal(await page.$eval('#acc6Btn',e=>e.disabled),true);results.push('disconnect-disables-actions');
  const out=join(here,'../../../artifacts');mkdirSync(out,{recursive:true});
  writeFileSync(join(out,'acc-six-face-browser-20261001.json'),JSON.stringify({results,passed:true,scope:'mock serial only'},null,2));
  console.log('PASS six-face browser:',results.join(', '));
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
