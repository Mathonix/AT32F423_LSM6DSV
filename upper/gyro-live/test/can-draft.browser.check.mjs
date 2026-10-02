import puppeteer from 'puppeteer-core';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const chrome = process.env.CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true });
const base = process.env.BASE || 'http://127.0.0.1:8798';
const artifacts = join(here, '../../../artifacts/web-host');
mkdirSync(artifacts, { recursive: true });
const errors = [];
try {
  const page = await browser.newPage();
  page.on('pageerror', (e) => errors.push(e.message));
  await page.setViewport({ width: 1440, height: 1000 });
  await page.evaluateOnNewDocument(readFileSync(join(here, 'mock-serial.js'), 'utf8'));
  await page.evaluateOnNewDocument(() => { Object.assign(window.__mock.dev, { extended: true, canExtended: true, capabilities: 15 }); });
  await page.goto(base, { waitUntil: 'networkidle0' });
  await page.click('#connectBtn');
  await page.waitForFunction(() => !!window.__gyro.state().canConfig);
  await page.click('#enterBtn');
  await page.waitForFunction(() => window.__gyro.state().setting);
  await page.evaluate(() => {
    const d = window.__mock.dev;
    d.can.periodMs = 30;
    window.__mock.port.push(d.canConfig());
    window.__lateCanReplies = 0;
    const handle = d.handle;
    d.handle = function(id, seq, payload) {
      if (id === 0x21) {
        const reply = this.canConfig();
        setTimeout(() => { window.__mock.port.push(reply); window.__lateCanReplies++; }, 250);
        return null;
      }
      return handle.call(this, id, seq, payload);
    };
  });
  await page.waitForFunction(() => document.getElementById('canPeriod').value === '30');
  await page.click('#canRefresh');
  await page.$eval('#canPeriod', (e) => { e.value = '1'; e.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.waitForFunction(() => window.__lateCanReplies >= 1);
  assert.equal(await page.$eval('#canPeriod', (e) => e.value), '1');
  assert.equal(await page.evaluate(() => window.__gyro.state().canConfig.active.periodMs), 30);
  assert.match(await page.$eval('#canDraftHint', (e) => e.textContent), /未应用/);
  console.log('PASS: late CAN query keeps edited period, actual state still updates');

  await page.click('#refreshBtn');
  await page.evaluate(() => {
    const values = { canRequestId: '0x222', canMasterId: '0x333', canPeriod: '2', canBaud: '7', canActive: '0' };
    for (const [id, value] of Object.entries(values)) {
      const e = document.getElementById(id); e.value = value;
      e.dispatchEvent(new Event('input', { bubbles: true }));
    }
    document.querySelectorAll('#canOutputs input').forEach((e) => { e.checked = !!(9 & (1 << Number(e.value))); e.dispatchEvent(new Event('change', { bubbles: true })); });
  });
  await page.waitForFunction(() => window.__lateCanReplies >= 2);
  const draft = await page.evaluate(() => ['canRequestId', 'canMasterId', 'canPeriod', 'canBaud', 'canActive'].map((id) => document.getElementById(id).value));
  assert.deepEqual(draft, ['0x222', '0x333', '2', '7', '0']);
  assert.equal(await page.evaluate(() => [...document.querySelectorAll('#canOutputs input')].reduce((m, e) => m | (e.checked ? 1 << Number(e.value) : 0), 0)), 9);
  console.log('PASS: global refresh preserves every CAN field and output selection');

  await page.click('#canDiscard');
  assert.equal(await page.$eval('#canPeriod', (e) => e.value), '30');
  assert.equal(await page.evaluate(() => window.__gyro.state().canFormDirty), false);
  await page.evaluate(() => { window.__mock.dev.failSave = true; });
  await page.$eval('#canPeriod', (e) => { e.value = '100'; e.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.click('#canApply');
  await page.waitForFunction(() => document.getElementById('message').textContent.includes('CAN 配置失败'));
  await page.waitForFunction(() => window.__lateCanReplies >= 3);
  assert.equal(await page.$eval('#canPeriod', (e) => e.value), '100');
  assert.equal(await page.evaluate(() => window.__gyro.state().canConfig.active.periodMs), 30);
  console.log('PASS: failed save preserves draft and reports unchanged device state');

  await page.evaluate(() => { window.__mock.dev.failSave = false; });
  await page.click('#canApply');
  await page.waitForFunction(() => !window.__gyro.state().pendingCan && window.__gyro.state().canConfig.saved.periodMs === 100);
  assert.equal(await page.evaluate(() => window.__gyro.state().canFormDirty), false);
  assert.match(await page.$eval('#message', (e) => e.textContent), /已应用并保存/);
  console.log('PASS: success waits for matching active and saved readback');

  // ACK alone is not confirmation that the device applied the requested values.
  await page.evaluate(() => {
    const d = window.__mock.dev, handle = d.handle;
    d.handle = function(id, seq, payload) {
      if (id === 0x22) return this.ack(id, 0, 0);
      if (id === 0x21) return this.canConfig();
      return handle.call(this, id, seq, payload);
    };
  });
  await page.$eval('#canPeriod', (e) => { e.value = '200'; e.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.click('#canApply');
  await page.waitForFunction(() => document.getElementById('message').textContent.includes('未确认 CAN 配置回读'), { timeout: 5000 });
  assert.equal(await page.$eval('#canPeriod', (e) => e.value), '200');
  assert.equal(await page.evaluate(() => window.__gyro.state().canConfig.active.periodMs), 100);
  assert.equal(await page.$eval('#canPanel', (e) => e.disabled), false);
  console.log('PASS: mismatched readback times out, unlocks form and keeps draft');
  await page.click('#canDiscard');
  await page.click('#pingBtn');
  await page.waitForFunction(() => document.getElementById('message').textContent.includes('PONG'));
  await page.evaluate(() => window.scrollTo({ top: 0, behavior: 'instant' }));

  for (const [name, width, height] of [['desktop', 1440, 1000], ['tablet', 820, 1100], ['mobile', 390, 844], ['mobile-small', 360, 800]]) {
    await page.setViewport({ width, height });
    await page.evaluate(() => window.scrollTo({ top: 0, behavior: 'instant' }));
    await page.evaluate(() => new Promise(requestAnimationFrame));
    const overflow = await page.evaluate(() => ({ viewport: innerWidth, page: document.documentElement.scrollWidth,
      elements: [...document.querySelectorAll('main *')].filter((e) => e.getBoundingClientRect().right > innerWidth + 1).map((e) => e.id || e.className || e.tagName) }));
    assert.ok(overflow.page <= width, JSON.stringify({ name, overflow }));
    assert.deepEqual(overflow.elements, [], JSON.stringify({ name, overflow }));
    const clippedLabels = await page.$$eval('[id^="outputFields"] label', (nodes) => nodes.filter((e) => e.scrollWidth > e.clientWidth + 1).map((e) => e.textContent.trim()));
    assert.deepEqual(clippedLabels, [], `${name}: clipped channel labels`);
    if (name === 'desktop' || name === 'mobile') await page.screenshot({ path: join(artifacts, `layout-20261001-${name}.png`), fullPage: true });
    console.log(`PASS: ${name} ${width}px, no horizontal overflow`);
  }
  await page.click('#disconnectBtn');
  await page.waitForFunction(() => !window.__gyro.state().running);
  assert.equal(await page.$eval('#canDraftHint', (e) => e.textContent), '');
  assert.deepEqual(errors, []);
  console.log('CAN draft and responsive layout checks passed');
} finally { await browser.close(); }
