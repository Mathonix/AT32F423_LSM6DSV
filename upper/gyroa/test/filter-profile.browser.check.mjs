// 第 4 档「零角速保持」(ZARU) headless Chrome 检查（gyro1 / gyro3 通用）。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8881 SHOTS=/workspace/zaru/shots TAG=gyro1 node test/filter-profile.browser.check.mjs
// 覆盖：capabilities=4 显示 4 档并可应用+回读第 4 档；capabilities=3 只显示前三档且 0x27 零写入；九轴提示「本档不锁定航向」；profile=4 被拒绝；1440×900 / 1024×768 不滚动
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8881", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "site";
const VERSION = process.env.VERSION || "20261002zaru1";
const APP_VERSION = process.env.APP_VERSION || "20261002biashist1"; // biashist1 // bias1：app.js 因阈值 + 运动零偏更新
const mock = readFileSync(join(here, "mock-serial.js"), "utf8") + "\n" + readFileSync(join(here, "filter-mock.js"), "utf8");
const NAMES4 = ["响应优先", "均衡", "静态稳定", "零角速保持"];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });

// dev: mock 设备字段（extended 0x07 CONFIG、activeMode 融合模式）；filter: filter-mock 字段（profileCount、active、saved）
async function session(vw, vh, { dev = {}, filter = {} } = {}) {
  const page = await browser.newPage(); await page.setViewport({ width: vw, height: vh });
  await page.evaluateOnNewDocument(mock);
  await page.evaluateOnNewDocument((dev, filter) => { Object.assign(window.__mock.dev, dev); Object.assign(window.__mock.filter, filter); }, dev, filter);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url())) errors.push("requestfailed: " + r.url()); });
  assert.equal((await page.goto(BASE + "/", { waitUntil: "networkidle0" })).status(), 200);
  const ev = (fn, ...a) => page.evaluate(fn, ...a);
  const waitFor = async (fn, what, timeout = 5000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); } };
  const nav = await ev(() => !!document.querySelector('.nav-item[data-nav="settings"]')); // gyro3 布局
  const x = { page, ev, waitFor, nav };
  x.connect = async () => {
    if (nav) { await page.click('.nav-item[data-nav="status"]'); await sleep(100); }
    await page.click("#connectBtn");
    await waitFor(() => document.getElementById("filterState").textContent.includes("内部 1000 Hz"), "filter readback");
    if (nav) { await page.click('.nav-item[data-nav="settings"]'); await waitFor(() => !document.querySelector('.view[data-view="settings"]').hidden, "settings view"); await sleep(150); }
    else { await page.click("#enterBtn"); await waitFor(() => window.__gyro.state().setting, "settings mode"); }
  };
  x.ui = () => ev(() => {
    const p = document.getElementById("filterPanel");
    const labs = [...p.querySelectorAll('input[name="filterProfile"]')].map((i) => { const l = i.closest("label"), r = l.getBoundingClientRect(); return { v: i.value, name: l.textContent.trim(), shown: !l.hidden && r.width > 0, disabled: i.disabled, top: Math.round(r.top), right: r.right }; });
    const pr = p.getBoundingClientRect(), doc = document.scrollingElement;
    return { labs, hint: document.getElementById("filterHint").textContent, state: document.getElementById("filterState").textContent, msg: document.getElementById("filterMsg").textContent,
      applyDisabled: document.getElementById("filterApply").disabled, panelRight: pr.right, panelOverflow: p.scrollWidth - p.clientWidth,
      sh: doc.scrollHeight, ih: innerHeight, sw: doc.scrollWidth, iw: innerWidth,
      viewOver: (() => { const v = [...document.querySelectorAll(".view")].find((v) => !v.hidden); return v ? Math.max(v.scrollHeight - v.clientHeight, v.scrollWidth - v.clientWidth) : 0; })(),
      hintBox: (() => { const r = document.getElementById("filterHint").getBoundingClientRect(); return { w: Math.round(r.width), h: Math.round(r.height) }; })(),
      own: !p.closest("#startupControls") && !(p.closest(".card") && p.closest(".card") !== p && !p.classList.contains("grp")) };
  });
  x.sets = () => ev(() => window.__mock.filter.log.filter((l) => l.id === 0x27).map((l) => l.payload));
  return x;
}

try {
  // ---- A: capabilities=4, 六轴（0x07 activeMode=0）----
  {
    const s = await session(1440, 900, { dev: { extended: true, configVersion: 3, capabilities: 127, activeMode: 0, savedMode: 0 }, filter: { profileCount: 4 } });
    for (const [sel, ver] of [["script[src^='/app.js']", APP_VERSION], ["script[src^='/filter-profile.js']", VERSION]])
      assert.ok((await s.page.$eval(sel, (e) => e.getAttribute("src"))).endsWith(`?v=${ver}`), sel);
    await s.connect();
    await s.waitFor(() => window.__gyro.filter.state.fusionMode === 0, "fusion from 0x07");
    await s.waitFor(() => !document.getElementById("filterApply").disabled, "editable");
    let u = await s.ui();
    assert.deepEqual(u.labs.map((l) => l.name), NAMES4);
    assert.deepEqual(u.labs.map((l) => l.shown), [true, true, true, true]); assert.deepEqual(u.labs.map((l) => l.disabled), [false, false, false, false]);
    assert.equal(new Set(u.labs.map((l) => l.top)).size, 1, "4 options on one row");
    assert.ok(u.labs.every((l) => l.right <= u.panelRight + 0.5) && u.panelOverflow <= 0, "options fit inside the filter card");
    assert.equal(await s.ev(() => window.__gyro.filter.state.profileCount), 4);
    await s.page.click('#filterPanel label:has(input[value="3"])');
    u = await s.ui();
    for (const t of ["零角速保持", "静止时锁定航向，检测到运动后立即恢复更新", "滤波参数与均衡相同", "仅六轴锁定航向，九轴不锁磁力计航向"]) assert.ok(u.hint.includes(t), t);
    // 英文名：gyro1 说明行内联；gyro3（说明行须单行）放在分段按钮 title
    const enTitle = await s.ev(() => document.querySelector('#filterPanel input[value="3"]').closest("label").title);
    assert.ok(enTitle.includes("ZARU / Stationary Heading Hold"), "label title EN");
    if (!s.nav) assert.ok(u.hint.includes("零角速保持（ZARU / Stationary Heading Hold）："), "gyro1 inline EN");
    assert.ok(!u.hint.includes("本档不锁定航向"));
    if (!(await s.ev(() => document.getElementById("filterPersist").checked))) await s.page.click("#filterPersist");
    await s.page.click("#filterApply");
    await s.waitFor(() => window.__gyro.filter.state.result?.ok === true && !window.__gyro.filter.state.pending, "apply ZARU");
    assert.deepEqual((await s.sets()).at(-1), [3, 1]);
    assert.deepEqual(await s.ev(() => [window.__mock.filter.active, window.__mock.filter.saved]), [3, 3]);
    const cfg = await s.ev(() => window.__gyro.filter.state.config);
    assert.deepEqual([cfg.active, cfg.saved, cfg.profileCount, cfg.tauMag, +cfg.restTau.toFixed(3)], [3, 3, 4, 4, 0.5]);
    assert.equal((await s.ui()).state, "当前：零角速保持 · 已保存：零角速保持 · 内部 1000 Hz");
    step("caps 4 / 六轴: 4 segments one row in card, hint text, 0x27[3,1] → ACK → 0x26 readback active=saved=3, tau 4/0.5");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-filter-caps4.png`) });
    u = await s.ui(); assert.ok(u.sh <= u.ih && u.sw <= u.iw && u.viewOver <= 0, `1440x900 no scroll (${u.sh}/${u.ih}, ${u.sw}/${u.iw}, view +${u.viewOver})`); if (s.nav) assert.ok(u.hintBox.w >= 400 && u.hintBox.h <= 22, `hint full-width single line (${u.hintBox.w}×${u.hintBox.h}px)`);
    if (SHOTS) await s.page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-caps4.png`) });
    await s.page.setViewport({ width: 1024, height: 768 }); await sleep(400);
    u = await s.ui();
    // gyro3：1024×768 也不滚动；gyro1 的 1024 布局本来就是纵向滚动（与线上相同），只要求不横向溢出、子卡片放得下
    if (s.nav) { assert.ok(u.sh <= u.ih && u.sw <= u.iw && u.viewOver <= 0, `1024x768 no scroll (${u.sh}/${u.ih}, ${u.sw}/${u.iw}, view +${u.viewOver})`); if (s.nav) assert.ok(u.hintBox.w >= 400 && u.hintBox.h <= 22, `hint full-width single line (${u.hintBox.w}×${u.hintBox.h}px)`); }
    else { assert.ok(u.sw <= u.iw, `1024x768 no horizontal scroll (${u.sw}/${u.iw})`); console.log(`  · 1024×768 scrollHeight=${u.sh} (gyro1 layout scrolls vertically at this size)`); }
    assert.equal(new Set(u.labs.map((l) => l.top)).size, 1, "1024: 4 options on one row");
    assert.ok(u.labs.every((l) => l.right <= u.panelRight + 0.5) && u.panelOverflow <= 0, "1024: options fit");
    if (SHOTS) await s.page.screenshot({ path: join(SHOTS, `${TAG}-1024x768-caps4.png`) });
    step(s.nav ? `layout: no scroll at 1440×900 and 1024×768, filter card intact` : `layout: 1440×900 no scroll; 1024×768 no horizontal overflow; sub-card fits`);
    // profile=4：控制器拒绝（零写入），mock 设备也回 ACK 0x02
    const n = (await s.sets()).length;
    assert.equal(await s.ev(async () => { const f = window.__gyro.filter; f.setDraft(4); const a = f.state.draft.profile; f.state.draft.profile = 4; const r = await f.apply(); f.discard(); return [a, r]; }).then((r) => JSON.stringify(r)), "[3,false]");
    assert.equal((await s.sets()).length, n, "no 0x27 for profile 4");
    const st = await s.ev(() => { const b = window.__mock.dev.handle(0x27, 0x55, Uint8Array.from([4, 0])); return [b[2], b[5], b[6]]; });
    assert.deepEqual(st, [0x90, 0x27, 2], "mock: 0x27 profile 4 → ACK status 2");
    step("profile 4: host refuses (no 0x27), mock 0x27[4,0] → ACK 0x02");
    await s.page.close();
  }
  // ---- B: capabilities=3（旧固件）----
  {
    const s = await session(1440, 900, { dev: { extended: true, configVersion: 3, capabilities: 127, activeMode: 0, savedMode: 0 }, filter: { profileCount: 3 } });
    await s.connect();
    await s.waitFor(() => !document.getElementById("filterApply").disabled, "editable");
    const u = await s.ui();
    assert.equal(await s.ev(() => window.__gyro.filter.state.profileCount), 3);
    assert.deepEqual(u.labs.filter((l) => l.shown).map((l) => l.name), ["响应优先", "均衡", "静态稳定"]);
    assert.deepEqual([u.labs[3].shown, u.labs[3].disabled], [false, true], "零角速保持 hidden + disabled");
    // 强行尝试：点击隐藏的 input、setDraft(3)、直接改草稿后 apply —— 都不得发出 profile=3
    await s.ev(() => document.querySelector('input[name="filterProfile"][value="3"]').click());
    await s.ev(() => window.__gyro.filter.setDraft(3, true));
    assert.notEqual(await s.ev(() => window.__gyro.filter.state.draft.profile), 3);
    const r = await s.ev(async () => { const f = window.__gyro.filter; f.state.draft.profile = 3; const r = await f.apply(); return r; });
    assert.equal(r, false);
    await sleep(300);
    assert.equal((await s.sets()).length, 0, "zero 0x27 writes on caps 3");
    await s.ev(() => window.__gyro.filter.discard());
    // 前三档照常可用
    await s.page.click('#filterPanel label:has(input[value="2"])'); await s.page.click("#filterApply");
    await s.waitFor(() => window.__gyro.filter.state.result?.ok === true && !window.__gyro.filter.state.pending, "apply 静态稳定 on caps 3");
    assert.deepEqual((await s.sets()).map((p) => p[0]), [2]);
    step("caps 3 (old fw): only 3 options shown, 零角速保持 hidden/disabled, forced profile 3 → 0 × 0x27; profile 2 still applies");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-filter-caps3.png`) });
    await s.page.close();
  }
  // ---- C: 九轴（0x07 activeMode=1），当前板子已在第 4 档 ----
  {
    const s = await session(1440, 900, { dev: { extended: true, configVersion: 3, capabilities: 127, activeMode: 1, savedMode: 1 }, filter: { profileCount: 4, active: 3, saved: 3 } });
    await s.connect();
    await s.waitFor(() => window.__gyro.filter.state.fusionMode === 1, "fusion 九轴 from 0x07");
    const u = await s.ui();
    assert.equal(u.state, "当前：零角速保持 · 已保存：零角速保持 · 内部 1000 Hz");
    assert.equal(u.labs.find((l) => l.v === "3").shown, true);
    assert.match(u.hint, /本档不锁定航向/); assert.match(u.hint, /滤波参数与均衡相同/);
    await s.page.click('#filterPanel label:has(input[value="1"])');
    assert.equal((await s.ui()).hint.includes("本档不锁定航向"), false, "first three hints unchanged");
    assert.match((await s.ui()).hint, /均衡：兼顾响应与静态平滑。/);
    await s.page.click('#filterPanel label:has(input[value="3"])');
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-filter-nine-axis.png`) });
    step("九轴 (0x07 activeMode=1): 零角速保持 hint says 本档不锁定航向; 均衡 hint unchanged");
    await s.page.close();
  }
  // ---- D: 0x07 不可用（旧配置协议）→ 融合未知，第 4 档显示通用说明 ----
  {
    const s = await session(1440, 900, { filter: { profileCount: 4 } });
    await s.connect();
    await s.page.click('#filterPanel label:has(input[value="3"])').catch(() => {});
    await s.ev(() => window.__gyro.filter.setDraft(3));
    const u = await s.ui();
    assert.equal(await s.ev(() => window.__gyro.filter.state.fusionMode), null);
    assert.match(u.hint, /仅六轴锁定航向，九轴不锁磁力计航向/);
    step("no 0x07 config → fusion unknown, generic ZARU hint (no LED guess)");
    await s.page.close();
  }
  assert.deepEqual(errors, []); step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e.stack || e.message, errors); process.exitCode = 1; } finally { await browser.close(); }
