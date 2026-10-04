// restore1：零角速保持「恢复默认」0x31 headless Chrome 检查 —— gyro3（自动设置模式门：确认点击才 0x17 → 0x31 → 0x18）。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8972 SHOTS=/workspace/bias1/shots TAG=gyro3 node test/zaru-restore.browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8972", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "gyro3";
const VERSION = process.env.VERSION || "20261002restore1";
const VQF = process.env.VQF_VERSION || "20261002fwver1"; // fwver1：ui-shell.js
const BHV = process.env.BH_VERSION || "20261002biashist1"; // biashist1：style.css
const mock = ["mock-serial.js", "filter-mock.js", "zaru-mock.js", "bias-mock.js"].map((f) => readFileSync(join(here, f), "utf8")).join("\n");
const KEYS = ["enterDps", "exitDps", "accDevMs2", "enterFilterMs", "enterConfirmMs", "exitConfirmMs"];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
const SIX = { extended: true, configVersion: 3, capabilities: 127, activeMode: 0, savedMode: 0 };

async function session({ vw = 1440, vh = 900, dev = SIX, filter = { profileCount: 4, active: 3, saved: 3 }, zaru = {} } = {}) {
  const page = await browser.newPage(); await page.setViewport({ width: vw, height: vh });
  await page.evaluateOnNewDocument(mock);
  await page.evaluateOnNewDocument((dev, filter, zaru) => { Object.assign(window.__mock.dev, dev); Object.assign(window.__mock.filter, filter); Object.assign(window.__mock.zaru, zaru); }, dev, filter, zaru);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url())) errors.push("requestfailed: " + r.url()); });
  assert.equal((await page.goto(BASE + "/", { waitUntil: "networkidle0" })).status(), 200);
  const ev = (fn, ...a) => page.evaluate(fn, ...a);
  const waitFor = async (fn, what, timeout = 5000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); } };
  const x = { page, ev, waitFor };
  x.nav = async (v) => { await page.click(`.nav-item[data-nav="${v}"]`); await waitFor((v) => !document.querySelector(`.view[data-view="${v}"]`).hidden, "view " + v, 3000, v); await sleep(150); };
  x.connect = async () => {
    await x.nav("status"); await page.click("#connectBtn");
    await waitFor(() => window.__gyro?.filter?.state.config && window.__mock.zaru.log.length > 0, "connect + 0x2E");
    await sleep(200); await x.nav("settings");
  };
  x.open = (on = true) => ev((on) => { document.getElementById("zaruPanel").open = on; }, on);
  x.fill = (vals) => ev((vals) => { for (const [k, v] of Object.entries(vals)) { const i = document.getElementById("zaru_" + k); i.value = String(v); i.dispatchEvent(new Event("input", { bubbles: true })); } }, vals);
  x.setPersist = (on) => ev((on) => { const c = document.getElementById("zaruPersist"); if (c.checked !== on) c.click(); }, on);
  x.writes = () => ev(() => window.__mock.zaru.log.filter((l) => l.id === 0x2f));
  x.cmds = () => ev(() => window.__mock.dev.commands.map((c) => c.id));
  x.idle = () => waitFor(() => !window.GyroUI.gate.busy && !window.__gyro.zaru.state.pending, "gate idle", 6000);
  x.ui = () => ev((KEYS) => {
    const p = document.getElementById("zaruPanel"), card = document.getElementById("filterPanel").getBoundingClientRect(), d = document.scrollingElement;
    const v = [...document.querySelectorAll(".view")].find((x) => !x.hidden);
    const ins = KEYS.map((k) => document.getElementById("zaru_" + k));
    const small = [...p.querySelectorAll("*")].filter((n) => n.getClientRects().length && parseFloat(getComputedStyle(n).fontSize) < 12).map((n) => n.tagName + "." + n.className);
    return { hidden: p.hidden, state: p.dataset.state, result: p.dataset.result, values: ins.map((i) => i.value), disabled: ins.map((i) => i.disabled), invalid: ins.map((i) => i.getAttribute("aria-invalid")),
      labels: [...p.querySelectorAll(".zaru-f")].map((l) => l.textContent.trim()), save: document.getElementById("zaruSave"), saveDisabled: document.getElementById("zaruSave").disabled, saveHidden: document.getElementById("zaruSave").hidden, rs: document.getElementById("zaruRestore").textContent, rsHidden: document.getElementById("zaruRestore").hidden, rsDisabled: document.getElementById("zaruRestore").disabled, rsArmed: document.getElementById("zaruRestore").dataset.armed,
      read: document.getElementById("zaruRead").disabled, summary: document.getElementById("zaruSummary").textContent, saved: document.getElementById("zaruSaved").textContent,
      note: document.getElementById("zaruNote").textContent, msg: document.getElementById("zaruMsg").textContent, filterHint: document.getElementById("filterHint").textContent,
      lineH: Math.round(p.querySelector(".zaru-line").getBoundingClientRect().height), fits: [...p.querySelectorAll(".zaru-f, button")].every((n) => { const r = n.getBoundingClientRect(); return r.width === 0 || (r.left >= card.left - .5 && r.right <= card.right + .5); }),
      below: p.getBoundingClientRect().top >= document.querySelector('#filterPanel input[value="3"]').closest("label").getBoundingClientRect().bottom,
      small, scroll: [d.scrollHeight - innerHeight, d.scrollWidth - innerWidth, v.scrollHeight - v.clientHeight, v.scrollWidth - v.clientWidth] };
  }, KEYS).then((u) => { delete u.save; return u; });
  return x;
}
const noScroll = (u, what) => assert.ok(u.scroll.every((n) => n <= 0), `${what} no scroll ${JSON.stringify(u.scroll)}`);

const CUSTOM = { enterDps: 0.5, exitDps: 1.2, accDevMs2: 0.4, enterFilterMs: 0, enterConfirmMs: 200, exitConfirmMs: 20 };
const custom = (o = {}) => ({ runtime: { ...CUSTOM }, saved: { ...CUSTOM }, ...o });
// enterSettings() 先发 0x18 探测再 0x17：去掉 0x17 之前的 0x18 探测
const restoreSeq = async (s, c0) => { const q = (await s.cmds()).slice(c0).filter((id) => [0x17, 0x18, 0x31, 0x2f].includes(id)); const i = q.indexOf(0x17); assert.ok(i < 0 || q.slice(0, i).every((id) => id === 0x18), "only EXIT probe before 0x17"); return i < 0 ? q : q.slice(i); };
const twoClicks = async (s) => { await s.page.click("#zaruRestore"); await sleep(150); await s.page.click("#zaruRestore"); };
try {
  // ---- A：成功路径 ----
  {
    const s = await session({ zaru: custom() });
    for (const [sel, attr, ver] of [["script[src^='/zaru-limits.js']", "src", VERSION], ["script[src^='/ui-shell.js']", "src", VQF], ["link[href^='/style.css']", "href", BHV]])
      assert.ok((await s.page.$eval(sel, (e, a) => e.getAttribute(a), attr)).endsWith(`?v=${ver}`), sel);
    await s.connect(); await s.waitFor(() => window.__gyro.zaru.state.supported === true, "supported");
    let u = await s.ui(); noScroll(u, "1440×900 settings collapsed");
    await s.open(); await sleep(200); u = await s.ui();
    assert.equal(u.rsHidden, false); assert.equal(u.rsDisabled, false); assert.equal(u.rs, "恢复默认"); assert.deepEqual(u.values, ["0.5", "1.2", "0.4", "0", "200", "20"]);
    assert.deepEqual(u.small, []); assert.ok(u.fits, "fits in card"); assert.ok(u.lineH <= 40, `button row single line (${u.lineH}px)`);
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-restore-idle.png`) });
    // 第一次点击：只上膛，不进设置模式、不发送
    const c0 = (await s.cmds()).length;
    await s.page.click("#zaruRestore"); await sleep(300);
    u = await s.ui(); assert.equal(u.rs, "确认恢复"); assert.equal(u.rsArmed, "true"); assert.match(u.msg, /再次点击「确认恢复」/);
    assert.deepEqual(await restoreSeq(s, c0), [], "arming sends nothing (no 0x17)"); assert.equal(await s.ev(() => window.__gyro.state().setting), false);
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-restore-armed.png`) });
    step("restore button in 零角速保持阈值 row; first click arms only (确认恢复, no 0x17 / 0x31)");
    // 第二次点击：0x17 → 0x31 [1,0] (settings=1) → ACK + 0x0F → 0x18
    await s.page.click("#zaruRestore");
    await s.waitFor(() => window.__gyro.zaru.state.result?.ok === true, "restore ok"); await s.idle();
    const seq = await restoreSeq(s, c0);
    assert.deepEqual(seq, [0x17, 0x31, 0x18], "gate order " + JSON.stringify(seq));
    const w = await s.ev(() => window.__mock.zaru.log.filter((l) => l.id === 0x31)); assert.equal(w.length, 1); assert.deepEqual(w[0].payload, [1, 0]); assert.equal(w[0].settings, 1);
    u = await s.ui();
    assert.deepEqual(u.values, ["0.3", "0.7", "0.15", "10", "50", "3"], "inputs from 0x0F readback"); assert.match(u.msg, /已恢复默认阈值（重启后保留），已回读核对/); assert.equal(u.result, "ok");
    assert.equal(u.summary, "当前 0.30/0.70 °/s · 0.15 m/s² · 10/50/3 ms"); assert.equal(u.saved, "已保存：与当前相同"); assert.equal(u.rs, "恢复默认");
    assert.deepEqual(await s.ev(() => window.__mock.zaru.saved), { enterDps: 0.3, exitDps: 0.7, accDevMs2: 0.15, enterFilterMs: 10, enterConfirmMs: 50, exitConfirmMs: 3 });
    const cmds = await s.cmds(); assert.ok(![0x2f, 0x27, 0x19, 0x1e, 0x13].some((id) => cmds.slice(c0).includes(id)), "no 0x2F / profile / mode / startup / stream writes");
    assert.deepEqual(await s.ev(() => [window.__mock.filter.active, window.__mock.filter.saved]), [3, 3], "profile untouched");
    assert.equal(await s.ev(() => window.__gyro.state().setting), false, "settings mode exited");
    step("confirm: 0x17 → 0x31 [1,0] in settings → ACK + 0x0F → 0x18; inputs = readback defaults; saved == defaults; no 0x2F, profile untouched");
    if (SHOTS) { await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-restore-ok.png`) }); await s.page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-restore-ok.png`) }); }
    // 上膛 4 s 后自动取消
    await s.page.click("#zaruRestore"); await sleep(300); assert.equal((await s.ui()).rs, "确认恢复");
    const c1 = (await s.cmds()).length; await sleep(4300);
    u = await s.ui(); assert.equal(u.rs, "恢复默认"); assert.equal(u.msg, ""); assert.deepEqual(await restoreSeq(s, c1), []);
    step("armed state expires after 4 s; nothing sent");
    // 1024×768
    await s.open(false); await s.page.setViewport({ width: 1024, height: 768 }); await sleep(400);
    noScroll(await s.ui(), "1024×768 collapsed");
    await s.open(true); await sleep(200); u = await s.ui(); assert.ok(u.fits && u.scroll[1] <= 0 && u.scroll[3] <= 0, "1024 open no horizontal overflow"); assert.deepEqual(u.small, []);
    assert.ok(u.lineH <= 40, `1024 button row single line (${u.lineH}px)`);
    if (SHOTS) await s.page.screenshot({ path: join(SHOTS, `${TAG}-1024x768-restore.png`) });
    step(`1024×768: collapsed no scroll; open fits, row ${u.lineH}px, fonts ≥ 12px`);
    // 断开 → 禁用
    await s.page.click("#disconnectBtn"); await s.waitFor(() => !window.__gyro.state().running, "disconnected"); await sleep(200);
    assert.equal((await s.ui()).rsDisabled, true); step("disconnected → 恢复默认 disabled");
    await s.page.close();
  }
  // ---- B：旧固件（阈值支持，但不认识 0x31）→ ACK 0x01 → 隐藏「恢复默认」，不当成已恢复 ----
  {
    const s = await session({ zaru: custom({ restore: false }) });
    await s.connect(); await s.open(); await s.waitFor(() => window.__gyro.zaru.state.supported === true, "supported");
    const c0 = (await s.cmds()).length;
    await twoClicks(s); await s.waitFor(() => window.__gyro.zaru.state.restoreSupported === false, "ACK 0x01"); await s.idle();
    const u = await s.ui();
    assert.equal(u.rsHidden, true); assert.match(u.msg, /当前固件不支持恢复默认（旧固件，ACK 0x01），未恢复/); assert.equal(u.result, "fail");
    assert.deepEqual(u.values, ["0.5", "1.2", "0.4", "0", "200", "20"], "values unchanged"); assert.equal(u.saveHidden, false, "保存 still available");
    assert.deepEqual(await restoreSeq(s, c0), [0x17, 0x31, 0x18]);
    assert.doesNotMatch(await s.ev(() => document.getElementById("message").textContent), /已恢复/);
    step("old fw (0x31 → ACK 0x01): 恢复默认 hidden, not treated as restored, editor + 保存 unaffected, settings exited");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-restore-oldfw.png`) });
    await s.page.close();
  }
  // ---- C：Flash 失败 / D：超时 / E：回读不是默认值 ----
  {
    const s = await session({ zaru: custom({ flashFail: true }) });
    await s.connect(); await s.open(); await s.waitFor(() => window.__gyro.zaru.state.supported === true, "supported");
    await twoClicks(s); await s.waitFor(() => window.__gyro.zaru.state.result, "result"); await s.idle();
    const u = await s.ui(); assert.match(u.msg, /恢复失败（ACK 0x03：未处于设置模式或 Flash 写入失败）/); assert.equal(u.result, "fail");
    assert.deepEqual(u.values, ["0.5", "1.2", "0.4", "0", "200", "20"]); assert.equal(await s.ev(() => window.__mock.zaru.runtime.enterDps), 0.5);
    step("flash failure → ACK 0x03 error, running thresholds unchanged");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-restore-flash-fail.png`) });
    await s.page.close();
  }
  {
    const s = await session({ zaru: custom({ dropReply: true }) });
    await s.connect(); await s.open(); await s.waitFor(() => window.__gyro.zaru.state.supported === true, "supported");
    await twoClicks(s); await s.waitFor(() => window.__gyro.zaru.state.pending?.acked, "ACK 0");
    assert.equal(await s.ev(() => window.__gyro.tempPollAllowed()), false, "temp poll paused while pending");
    await s.idle();
    const u = await s.ui(); assert.match(u.msg, /未确认：未收到恢复默认的回读，界面保留原值/); assert.equal(u.result, "unconfirmed");
    assert.deepEqual(u.values, ["0.5", "1.2", "0.4", "0", "200", "20"], "UI keeps previous values"); assert.match(u.summary, /0\.50\/1\.20/);
    assert.equal(await s.ev(() => window.__gyro.state().setting), false);
    step("timeout (ACK, no 0x0F) → 未确认, UI keeps previous values, gate exits settings");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-restore-unconfirmed.png`) });
    await s.page.close();
  }
  {
    const s = await session({ zaru: custom({ restoreWrong: true }) });
    await s.connect(); await s.open(); await s.waitFor(() => window.__gyro.zaru.state.supported === true, "supported");
    await twoClicks(s); await s.waitFor(() => window.__gyro.zaru.state.result, "result"); await s.idle();
    const u = await s.ui(); assert.match(u.msg, /回读不一致：设备当前值不是固件默认值/); assert.equal(u.values[5], "4", "display = readback");
    step("ACK 0 but readback ≠ defaults → failure, display shows device readback");
    await s.page.close();
  }
  // ---- F：阈值编辑区不支持 / 只读 → 无「恢复默认」 ----
  {
    const s = await session({ zaru: { flag: 0 } });
    await s.connect(); await s.open(); await s.waitFor(() => window.__gyro.zaru.state.supported === false, "flag 0");
    assert.equal((await s.ui()).rsHidden, true); step("supported=0 (read-only) → 恢复默认 hidden");
    await s.page.close();
  }
  assert.deepEqual(errors, []); step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e.stack || e.message, errors); process.exitCode = 1; } finally { await browser.close(); }
