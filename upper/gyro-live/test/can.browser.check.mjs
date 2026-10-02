// REAL=1 uses real board serial bytes through an explicit local test bridge.
// No real CAN peer is simulated by this check; native Web Serial dialog excluded.
import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const real = process.env.REAL === '1';
const chrome = process.env.CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true });
const cases = []; let page, original, initial, restored = false;
try {
  page = await browser.newPage(); await page.setViewport(process.env.MOBILE === '1' ? { width: 390, height: 900 } : { width: 1400, height: 1100 });
  await page.evaluateOnNewDocument(readFileSync(join(here, real ? 'real-serial-transport.js' : 'mock-serial.js'), 'utf8'));
  if (!real) await page.evaluateOnNewDocument(() => { window.__mock.dev.extended = true; window.__mock.dev.canExtended = true; window.__mock.dev.capabilities = 15; });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.goto(process.env.BASE || 'http://127.0.0.1:8798', { waitUntil: 'networkidle0' });
  const wait = (f) => page.waitForFunction(f, { timeout: 8000 });
  const state = () => page.evaluate(() => window.__gyro.state());
  await page.click('#connectBtn'); await wait(() => !!window.__gyro.state().canConfig);
  original = (await state()).canConfig; initial = (await state()).deviceConfig;
  assert.ok(await page.$eval('#canPanel', (e) => e.disabled));
  await page.click('#enterBtn'); await wait(() => window.__gyro.state().setting);
  const apply = async (c, persist) => {
    await wait(() => !document.getElementById('canPanel').disabled);
    await page.evaluate((c, persist) => {
      const el = (id) => document.getElementById(id);
      el('canRequestId').value = '0x' + c.nodeId.toString(16); el('canMasterId').value = c.masterId;
      el('canPeriod').value = c.periodMs; el('canBaud').value = c.baud; el('canActive').value = c.active;
      el('canPersist').checked = persist;
      document.querySelectorAll('#canOutputs input').forEach((n) => { n.checked = !!(c.mask & (1 << Number(n.value))); });
      for (const id of ['canRequestId', 'canMasterId', 'canPeriod', 'canBaud', 'canActive']) {
        el(id).dispatchEvent(new Event('input', { bubbles: true }));
      }
      el('canPersist').dispatchEvent(new Event('change', { bubbles: true }));
      document.querySelectorAll('#canOutputs input').forEach((n) => n.dispatchEvent(new Event('change', { bubbles: true })));
    }, c, persist);
    await page.click('#canApply');
    await page.waitForFunction((c) => {
      const s = window.__gyro.state();
      return !s.pendingCan && s.canConfig && Object.keys(c).every((k) => s.canConfig.active[k] === c[k]);
    }, { timeout: 8000 }, c);
    return (await state()).canConfig;
  };
  for (let baud = 0; baud < 8; ++baud) for (const active of [0, 1]) {
    const c = { nodeId: 2047, masterId: 2046, periodMs: 100, baud, active, mask: 15, reserved: 0 };
    const got = await apply(c, false);
    assert.deepEqual(got.saved, original.saved);
    cases.push(c); console.log(`${real ? 'Real USB' : 'Mock'} Web CAN: baud ${baud}, active ${active} OK`);
  }
  const saved = { ...original.active, nodeId: 0x123, masterId: 0x345, periodMs: 30, baud: 7, mask: 15, active: 1 };
  const got = await apply(saved, true); assert.deepEqual(got.saved, saved);
  await page.click('#refreshBtn');
  await page.waitForFunction(() => window.__gyro.state().canConfig.active.nodeId === 0x123);
  const d = (await state()).deviceConfig;
  assert.deepEqual(d.outputs, initial.outputs); assert.equal(d.savedMode, initial.savedMode); assert.equal(d.savedFast, initial.savedFast);
  // Frontend catches invalid load without sending it to the board.
  // Programmatic value changes must emit the same input event as user edits,
  // so an in-flight refresh preserves this deliberately invalid draft.
  await page.$eval('#canPeriod', (e) => { e.value = 1; e.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.click('#canApply'); await wait(() => document.getElementById('message').textContent.includes('CAN 参数无效'));
  assert.deepEqual((await state()).canConfig.active, saved);
  if (!real) {
    await page.evaluate(() => {
      window.__mock.dev.failSave = true;
      const e = document.getElementById('canPeriod');
      e.value = 100; e.dispatchEvent(new Event('input', { bubbles: true }));
    });
    await page.click('#canApply'); await wait(() => document.getElementById('message').textContent.includes('CAN 配置失败'));
    assert.deepEqual((await state()).canConfig.active, saved);
    await page.evaluate(() => { window.__mock.dev.failSave = false; });
  }
  await page.click('#canRefresh');
  // A read updates actual state without discarding an unapplied draft.
  if (await page.$eval('#canDiscard', (e) => !e.disabled)) await page.click('#canDiscard');
  await page.waitForFunction(() => document.getElementById('canPeriod').value === '30');
  if (process.env.SHOT) await page.screenshot({ path: process.env.SHOT, fullPage: true });
  await apply(original.saved, true); await apply(original.active, false); restored = true;
  await page.click('#disconnectBtn'); assert.deepEqual(errors, []);
  if (process.env.RESULT) writeFileSync(process.env.RESULT, JSON.stringify({ real, cases, savedReadback: got, restored: original, limits: 'serial configuration only; CAN bus and native browser permission dialog excluded' }, null, 2));
  console.log('CAN browser check passed: 16 runtime combinations, flash/readback, independence, validation, restoration');
} finally {
  // Restore even on assertion/UI failure while the serial connection is usable.
  if (real && original && !restored && page) {
    try {
      await page.evaluate(async (o) => {
        const raw = (c, p) => { const b = new Uint8Array(11), v = new DataView(b.buffer); v.setUint16(0,c.nodeId,true); v.setUint16(2,c.masterId,true); v.setUint16(4,c.periodMs,true); b.set([c.baud,c.active,c.mask,0,p],6); return b; };
        await send(CMD.ENTER); await send(CMD.CAN_CONFIG, raw(o.saved, 1));
        await new Promise((resolve) => setTimeout(resolve, 500));
        await send(CMD.CAN_CONFIG, raw(o.active, 0));
      }, original);
      await new Promise((resolve) => setTimeout(resolve, 500));
    } catch (e) { console.error('CAN restore failed:', e.message); }
  }
  if (real && page) { try { await page.evaluate(() => disconnect(false)); } catch {} }
  await browser.close();
}
