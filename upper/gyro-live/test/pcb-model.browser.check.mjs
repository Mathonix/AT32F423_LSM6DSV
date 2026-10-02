import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const base = process.env.BASE || 'http://127.0.0.1:8798';
const chrome = process.env.CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true });
const artifacts = join(here, '../../../artifacts/web-host'); mkdirSync(artifacts, { recursive:true });
const report = { base, rotations: [], viewports: [] }, errors = [];
try {
  const page = await browser.newPage();
  page.on('pageerror', e => errors.push(e.message));
  await page.setViewport({ width:1440, height:1000 });
  await page.evaluateOnNewDocument(readFileSync(join(here, 'mock-serial.js'), 'utf8'));
  await page.goto(base, { waitUntil:'networkidle0' });
  await page.waitForFunction(() => window.__gyro.model()?.renderedTriangles > 100000, { timeout:20000 });
  const initial = await page.evaluate(() => window.__gyro.model());
  assert.equal(initial.source, '3D_PCB1_2026-10-01.step');
  assert.equal(initial.componentMeshes, 210); assert.equal(initial.triangles, 142832); assert.equal(initial.materials, 5);
  assert.deepEqual(initial.cadToBody,[[0,-1,0],[1,0,0],[0,0,1]]);
  assert.ok(initial.drawCalls <= 8); report.model = initial;
  await page.click('#connectBtn'); await page.waitForFunction(() => window.__gyro.state().lastPoseAt > 0);
  for (const [yaw, pitch, roll] of [[0,0,0], [90,0,0], [0,90,0], [0,0,90], [0,0,180], [45,25,-30]]) {
    await page.evaluate((pose) => Object.assign(window.__mock.dev.pose, pose), {yaw,pitch,roll});
    await page.waitForFunction(([y,p,r]) => {
      const s = window.__gyro.state(), view = window.__gyro.projection();
      return s.pose.yaw === y && s.pose.pitch === p && s.pose.roll === r && view?.R && view.model?.ready;
    }, {}, [yaw,pitch,roll]);
    await new Promise(r => setTimeout(r, 90));
    const projection = await page.evaluate(() => window.__gyro.projection());
    assert.ok(projection.hullArea > 10000);
    assert.ok(projection.screen.every(p => p.every(Number.isFinite)));
    const m = projection.model.matrix, R = projection.R;
    for (let row=0; row<3; row++) for (let col=0; col<3; col++) assert.ok(Math.abs(m[col*4+row]-R[row][col])<1e-9);
    const actual = projection.model.cadWorldMatrix;
    for (let row=0; row<3; row++) {
      assert.ok(Math.abs(actual[row]-R[row][1])<1e-9, 'CAD X follows sensor Y');
      assert.ok(Math.abs(actual[4+row]+R[row][0])<1e-9, 'CAD Y follows sensor -X');
      assert.ok(Math.abs(actual[8+row]-R[row][2])<1e-9, 'CAD Z follows sensor Z');
    }
    report.rotations.push({yaw,pitch,roll,hullArea:projection.hullArea});
  }
  await page.evaluate(() => {
    const b = new Uint8Array(18), v = new DataView(b.buffer);
    v.setFloat32(0,Math.SQRT1_2,true); v.setFloat32(4,Math.SQRT1_2,true);
    window.__gyro.feed(window.__mock.frame(0x02,1,b));
  });
  await page.waitForFunction(() => window.__gyro.projection()?.source === '四元数'); report.quaternion = true;
  const bounds = await page.$eval('#pcbModel', e => { const r=e.getBoundingClientRect(); return {x:r.x,y:r.y,w:r.width,h:r.height}; });
  const stateBefore = await page.evaluate(() => ({pose:window.__gyro.state().pose, camera:window.__gyro.model().camera}));
  await page.mouse.move(bounds.x+bounds.w*.45,bounds.y+bounds.h*.45); await page.mouse.down();
  await page.mouse.move(bounds.x+bounds.w*.65,bounds.y+bounds.h*.6,{steps:8}); await page.mouse.up();
  const cameraAfter = await page.evaluate(() => window.__gyro.model().camera);
  assert.notDeepEqual(cameraAfter,stateBefore.camera);
  await page.mouse.wheel({deltaY:120});
  assert.notDeepEqual(await page.evaluate(() => window.__gyro.model().camera),cameraAfter);
  await page.click('#modelReset');
  const reset = await page.evaluate(() => ({pose:window.__gyro.state().pose,camera:window.__gyro.model().camera}));
  assert.deepEqual(reset.pose,stateBefore.pose); assert.deepEqual(reset.camera,initial.camera);
  report.cameraControls = true;
  for (const width of [1440,820,390,360]) {
    await page.setViewport({width,height:1000});
    const fit = await page.evaluate(() => ({width:innerWidth,scroll:document.documentElement.scrollWidth,canvas:document.getElementById('pcbModel').getBoundingClientRect().width}));
    assert.ok(fit.scroll <= fit.width); assert.ok(fit.canvas > 200); report.viewports.push(fit);
    if (width===1440 || width===390) await page.screenshot({path:join(artifacts,`pcb-model-20261001-${width}.png`),fullPage:true});
  }
  // Network/model failure must keep protocol processing and the simpler view usable.
  const fallback = await browser.newPage();
  await fallback.evaluateOnNewDocument(readFileSync(join(here,'mock-serial.js'),'utf8'));
  await fallback.setRequestInterception(true);
  fallback.on('request', r => r.url().includes('/models/pcb.glb') ? r.abort() : r.continue());
  await fallback.goto(base,{waitUntil:'networkidle0'});
  await fallback.waitForFunction(() => document.getElementById('modelStatus').textContent.includes('简化'));
  await fallback.click('#connectBtn'); await fallback.waitForFunction(() => window.__gyro.state().lastPoseAt>0);
  assert.equal(await fallback.evaluate(() => window.__gyro.model()),null); report.fallback = true;
  assert.deepEqual(errors,[]); report.passed = true;
  writeFileSync(join(artifacts,'pcb-model-browser-20261001.json'),JSON.stringify(report,null,2));
  console.log(JSON.stringify(report,null,2));
} finally { await browser.close(); }
