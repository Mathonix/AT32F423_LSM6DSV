import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const chrome = process.env.CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true });
const base = process.env.BASE || 'http://127.0.0.1:8798';
const errors = [];
try {
  const page = await browser.newPage();
  page.on('pageerror', e => errors.push(e.message));
  await page.setViewport({ width: 1440, height: 1000 });
  await page.evaluateOnNewDocument(readFileSync(join(here, 'mock-serial.js'), 'utf8'));
  await page.evaluateOnNewDocument(() => {
    Object.assign(window.__mock.dev, { extended: true, configVersion: 2, capabilities: 31, canExtended: true, activeFast: 1, savedFast: 1 });
  });
  await page.goto(base, { waitUntil: 'networkidle0' });
  const wait = f => page.waitForFunction(f, { timeout: 8000 });
  const setSeconds = value => page.$eval('#gyroInitSeconds', (e, value) => {
    e.value = value; e.dispatchEvent(new Event('input', { bubbles: true }));
  }, value);
  await page.click('#connectBtn');
  await wait(() => window.__gyro.state().deviceConfig?.version === 2);
  await page.click('#enterBtn'); await wait(() => window.__gyro.state().setting);
  assert.equal(await page.$eval('#gyroInitRow', e => e.hidden), true);
  const original = await page.evaluate(() => JSON.stringify({ outputs: window.__mock.dev.outputs, can: window.__mock.dev.can }));
  await page.evaluate(() => {
    const d = window.__mock.dev;
    const handle = d.handle;
    window.__startupHandle = handle;
    d.handle = function(id, seq, payload) {
      if (id === 0x1f) { const reply = this.config(); setTimeout(() => window.__mock.port.push(reply), 250); return null; }
      return handle.call(this, id, seq, payload);
    };
  });
  await page.click('#refreshBtn');
  await page.click('#fastStart');
  assert.equal(await page.$eval('#gyroInitRow', e => e.hidden), false);
  assert.equal(await page.$eval('#gyroInitSeconds', e => e.value), '2');
  assert.equal(await page.$eval('#gyroInitSeconds', e => e.disabled), false);
  await setSeconds('3.5');
  await page.waitForFunction(() => document.getElementById('startupState').textContent.includes('已保存'), { polling: 300 });
  await new Promise(r => setTimeout(r, 300));
  assert.equal(await page.$eval('#fastStart', e => e.checked), false);
  assert.equal(await page.$eval('#gyroInitSeconds', e => e.value), '3.5');
  console.log('PASS: unchecking reveals 2-second default; delayed refresh preserves checkbox and duration');

  await page.click('#startupDiscard');
  assert.equal(await page.$eval('#fastStart', e => e.checked), true);
  await page.click('#fastStart');
  const count = await page.evaluate(() => window.__mock.dev.commands.filter(c => c.id === 0x1e).length);
  for (const invalid of ['', '0', '60.1']) {
    await setSeconds(invalid); await page.click('#applyBtn');
    assert.match(await page.$eval('#message', e => e.textContent), /初始化零偏时长须为/);
  }
  assert.equal(await page.evaluate(() => window.__mock.dev.commands.filter(c => c.id === 0x1e).length), count);
  await setSeconds('2.5');
  await page.evaluate(() => { window.__mock.dev.failSave = true; });
  await page.click('#applyBtn');
  await wait(() => document.getElementById('message').textContent.includes('启动设置保存失败'));
  assert.equal(await page.$eval('#fastStart', e => e.checked), false);
  assert.equal(await page.$eval('#gyroInitSeconds', e => e.value), '2.5');
  assert.equal(await page.evaluate(() => window.__mock.dev.savedFast), 1);
  await page.evaluate(() => { window.__mock.dev.failSave = false; });
  await page.click('#applyBtn');
  await wait(() => !window.__gyro.state().pendingStartup && window.__gyro.state().deviceConfig?.savedInitMs === 2500);
  await wait(() => !window.__gyro.state().setting);
  assert.equal(await page.evaluate(() => window.__mock.dev.savedFast), 0);
  assert.equal(await page.evaluate(() => window.__mock.dev.activeFast), 1);
  assert.match(await page.$eval('#startupState', e => e.textContent), /零偏 2.5 秒/);
  assert.equal(await page.evaluate(() => window.__gyro.state().fusionPendingRestart), true);
  assert.equal(await page.evaluate(() => JSON.stringify({ outputs: window.__mock.dev.outputs, can: window.__mock.dev.can })), original);
  console.log('PASS: validation sends no invalid commands; failure preserves draft; save/readback confirms 2500ms without changing CAN/output');

  await page.click('#enterBtn'); await wait(() => window.__gyro.state().setting);
  await setSeconds('4');
  await page.evaluate(() => {
    const d = window.__mock.dev;
    d.handle = function(id, seq, payload) {
      if (id === 0x1e) return this.ack(id, 0, this.savedMode);
      return window.__startupHandle.call(this, id, seq, payload);
    };
  });
  await page.click('#applyBtn');
  await wait(() => document.getElementById('message').textContent.includes('未确认启动设置回读'));
  assert.equal(await page.$eval('#gyroInitSeconds', e => e.value), '4');
  assert.equal(await page.$eval('#applyBtn', e => e.disabled), false);
  assert.equal(await page.evaluate(() => window.__gyro.state().startupFormDirty), true);
  assert.equal(await page.evaluate(() => window.__gyro.state().setting), true);
  console.log('PASS: ACK without matching readback times out and retains edits');

  await page.evaluate(() => { window.__mock.dev.handle = window.__startupHandle; });
  await setSeconds('2');
  await page.click('#restartNow'); await page.click('#applyBtn');
  await wait(() => !window.__gyro.state().running);
  await wait(() => !window.__gyro.state().reconnecting && window.__gyro.state().deviceConfig?.activeInitMs === 2000);
  assert.equal(await page.evaluate(() => window.__gyro.state().deviceConfig.activeFast), 0);
  assert.equal(await page.evaluate(() => window.__gyro.state().fusionPendingRestart), false);
  console.log('PASS: immediate restart and reconnect restore normal startup with 2-second calibration');

  for (const width of [1440, 820, 390, 360]) {
    await page.setViewport({ width, height: 1000 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `overflow at ${width}`);
  }
  const artifacts = join(here, '../../../artifacts/web-host'); mkdirSync(artifacts, { recursive: true });
  await page.setViewport({ width: 1440, height: 1000 });
  await page.screenshot({ path: join(artifacts, 'startup-20261001-desktop.png'), fullPage: true });
  await page.setViewport({ width: 390, height: 1000 });
  await page.screenshot({ path: join(artifacts, 'startup-20261001-mobile.png'), fullPage: true });
  await page.click('#enterBtn'); await wait(() => window.__gyro.state().setting);
  await page.evaluate(() => Object.assign(window.__mock.dev, { configVersion: 1, capabilities: 15, savedFast: 1, activeFast: 1 }));
  await page.click('#refreshBtn'); await wait(() => window.__gyro.state().deviceConfig?.version === 1);
  await page.click('#fastStart'); await page.click('#applyBtn');
  assert.match(await page.$eval('#message', e => e.textContent), /升级配套固件/);
  assert.equal(await page.$eval('#gyroInitSeconds', e => e.disabled), true);
  await page.click('#disconnectBtn');
  assert.equal(await page.evaluate(() => window.__gyro.state().startupFormDirty), false);
  assert.deepEqual(errors, []);
  console.log('PASS: 4 viewport widths; older firmware identifies missing support; disconnect clears draft');
} finally { await browser.close(); }
