// Actual board bytes, test-only local serial transport. Native permission dialog excluded.
import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const chrome = process.env.CHROME || [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
].find(existsSync);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true });
const base = process.env.BASE || 'http://127.0.0.1:8798';
const cases = [];
let page;
try {
  page = await browser.newPage(); await page.setViewport({ width: 1400, height: 1100 });
  await page.evaluateOnNewDocument(readFileSync(join(here, 'real-serial-transport.js'), 'utf8'));
  const errors = []; page.on('pageerror', (e) => errors.push(e.message)); page.on('dialog', (d) => d.accept());
  await page.goto(base, { waitUntil: 'networkidle0' }); await page.click('#connectBtn');
  await page.waitForFunction(() => window.__gyro.state().deviceConfig?.source === 1, { timeout: 10000 });
  const original = await page.evaluate(() => window.__gyro.state().deviceConfig);
  console.log('Real device', JSON.stringify(original));
  const configure = async (target, fmt, mask, persist = false) => {
    await page.waitForFunction((target) => !document.getElementById('outputPanel' + target).disabled, {}, target);
    await page.select('#outputFormat' + target, String(fmt));
    await page.evaluate((target, mask, persist) => {
      document.querySelectorAll(`#outputFields${target} input`).forEach((n) => { n.checked = !!(mask & (1 << Number(n.value))); });
      document.getElementById('outputPersist' + target).checked = persist;
    }, target, mask, persist);
    await page.click('#outputApply' + target);
    await page.waitForFunction((target, fmt, mask) => {
      const o = window.__gyro.state().deviceConfig?.outputs[target];
      return o?.format === fmt && o.mask === mask && !document.getElementById('outputPanel' + target).disabled;
    }, { timeout: 5000 }, target, fmt, mask);
  };
  await configure(0, 1, 448);
  for (const fmt of [0, 1]) for (const mask of [1, 2, 4, 8, 16, 32, 64, 128, 256, 65, 56, 448, 7, 511]) {
    await configure(1, fmt, mask);
    await page.waitForFunction((mask) => {
      const s = window.__gyro.state(); const all = { ...s.pose, ...s.imu };
      return ['yaw','pitch','roll','ax','ay','az','gx','gy','gz'].every((k, i) => mask & (1 << i) ? Number.isFinite(all[k]) : all[k] === null);
    }, { timeout: 5000 }, mask);
    const s = await page.evaluate(() => window.__gyro.state());
    assert.equal(s.deviceConfig.outputs[0].mask, 448);
    cases.push({ format: fmt, mask, pose: s.pose, imu: s.imu, stats: s.stats });
    console.log(`Real USB → Web: protocol ${fmt}, mask ${mask} OK`);
  }
  if (process.env.SHOT) await page.screenshot({ path: process.env.SHOT, fullPage: true });
  await configure(1, 1, 0);
  await page.waitForFunction(() => document.getElementById('dataState').textContent === '输出已关闭');
  await page.click('#pingBtn');
  await page.waitForFunction(() => document.getElementById('message').textContent.includes('PONG'));
  await page.click('#enterBtn'); await page.waitForFunction(() => window.__gyro.state().setting);
  await page.click('input[name="fusion"][value="0"]');
  await page.$eval('#fastStart', (e) => { e.checked = true; });
  await page.click('#applyBtn');
  await page.waitForFunction(() => window.__gyro.state().deviceConfig?.savedMode === 0 && window.__gyro.state().deviceConfig.savedFast === 1);
  let state = await page.evaluate(() => window.__gyro.state());
  assert.equal(state.deviceConfig.activeMode, original.activeMode);
  assert.equal(state.deviceConfig.outputs[0].mask, 448);
  await page.click('#enterBtn'); await page.waitForFunction(() => window.__gyro.state().setting);
  await page.click(`input[name="fusion"][value="${original.savedMode}"]`);
  await page.$eval('#fastStart', (e, v) => { e.checked = !!v; }, original.savedFast);
  await page.click('#applyBtn');
  await page.waitForFunction((o) => window.__gyro.state().deviceConfig.savedMode === o.savedMode && window.__gyro.state().deviceConfig.savedFast === o.savedFast, {}, original);
  await configure(0, 0, 7, true); await configure(1, 0, 7, true);
  await page.waitForFunction(() => Number.isFinite(window.__gyro.state().pose.roll));
  state = await page.evaluate(() => window.__gyro.state()); assert.deepEqual(errors, []);
  if (process.env.RESULT) writeFileSync(process.env.RESULT, JSON.stringify({ base, transport: 'real COM bytes / test-only local bridge; native browser permission excluded', cases, final: state }, null, 2));
  console.log(`PASS ${cases.length} real-board Web UI selections, independent UART, disable/PING and startup settings; defaults restored`);
} catch (e) {
  if (page) console.error(await page.evaluate(() => ({ state: window.__gyro?.state(), log: document.getElementById('log')?.textContent?.slice(-4000) })).catch(() => null));
  throw e;
} finally {
  if (page) await page.evaluate(() => document.getElementById('disconnectBtn')?.click()).catch(() => {});
  await fetch('http://127.0.0.1:8800/close', { method: 'POST' }).catch(() => {});
  await browser.close();
}
