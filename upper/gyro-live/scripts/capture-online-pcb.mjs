import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = join(dirname(fileURLToPath(import.meta.url)), '../../..');
const base = process.env.BASE || 'https://gyro.233688.xyz';
const version = process.env.VERSION || '20261001f';
const chrome = process.env.CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser = await puppeteer.launch({ executablePath:chrome, headless:true });
try {
  const page = await browser.newPage();
  await page.setViewport({ width:1440, height:1000, deviceScaleFactor:1 });
  await page.goto(`${base}/?v=${version}`, {waitUntil:'networkidle0'});
  await page.waitForFunction(() => window.__gyro.model()?.renderedTriangles > 100000);
  const model = await page.evaluate(() => window.__gyro.model());
  assert.deepEqual(model.cadToBody, [[0,-1,0],[1,0,0],[0,0,1]]);
  assert.equal(model.componentMeshes,210);
  assert.ok(await page.$eval('script[src]', (e,version) => e.src.endsWith(version),version));
  const out = join(root,'artifacts/web-host'); mkdirSync(out,{recursive:true});
  await page.screenshot({path:join(out,`host-${version}-online.png`),fullPage:true});
  await (await page.$('.col-dev')).screenshot({path:join(out,`settings-${version}-online.png`)});
  writeFileSync(join(out,`pcb-online-capture-${version}.json`),JSON.stringify({base,version,capturedAt:new Date().toISOString(),model,serialConnected:false},null,2));
  console.log(`PASS: online version ${version}, real PCB, corrected installation axes; no serial session opened`);
} finally { await browser.close(); }
