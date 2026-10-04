// bias1：零角速保持阈值（zaru-limits.js）headless Chrome 检查 —— gyro3（左侧导航 + 自动设置模式门）。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8972 SHOTS=/workspace/bias1/shots TAG=gyro3 node test/zaru-limits.browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8972", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "gyro3";
const VERSION = process.env.VERSION || "20261002restore1"; // restore1：zaru-limits.js / ui-shell.js / style.css；app.js 仍为 bias1
const APP_VERSION = process.env.APP_VERSION || "20261002biashist1"; // biashist1：app.js / style.css
const SHELL_VERSION = process.env.SHELL_VERSION || "20261002fwver1"; // ui-shell.js
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
      labels: [...p.querySelectorAll(".zaru-f")].map((l) => l.textContent.trim()), save: document.getElementById("zaruSave"), saveDisabled: document.getElementById("zaruSave").disabled, saveHidden: document.getElementById("zaruSave").hidden,
      read: document.getElementById("zaruRead").disabled, summary: document.getElementById("zaruSummary").textContent, saved: document.getElementById("zaruSaved").textContent,
      note: document.getElementById("zaruNote").textContent, msg: document.getElementById("zaruMsg").textContent, filterHint: document.getElementById("filterHint").textContent,
      fits: [...p.querySelectorAll(".zaru-f, button")].every((n) => { const r = n.getBoundingClientRect(); return r.width === 0 || (r.left >= card.left - .5 && r.right <= card.right + .5); }),
      below: p.getBoundingClientRect().top >= document.querySelector('#filterPanel input[value="3"]').closest("label").getBoundingClientRect().bottom,
      small, scroll: [d.scrollHeight - innerHeight, d.scrollWidth - innerWidth, v.scrollHeight - v.clientHeight, v.scrollWidth - v.clientWidth] };
  }, KEYS).then((u) => { delete u.save; return u; });
  return x;
}
const noScroll = (u, what) => assert.ok(u.scroll.every((n) => n <= 0), `${what} no scroll ${JSON.stringify(u.scroll)}`);

try {
  // ---- A：连接即查询；位置、标签、布局 ----
  {
    const s = await session();
    for (const [sel, attr, ver] of [["script[src^='/app.js']", "src", APP_VERSION], ["script[src^='/zaru-limits.js']", "src", VERSION], ["script[src^='/ui-shell.js']", "src", SHELL_VERSION], ["link[href^='/style.css']", "href", APP_VERSION]])
      assert.ok((await s.page.$eval(sel, (e, a) => e.getAttribute(a), attr)).endsWith(`?v=${ver}`), sel);
    await s.nav("settings");
    let u = await s.ui(); assert.equal(u.read, true, "读取 disabled before connect"); assert.ok(u.disabled.every(Boolean)); assert.equal(u.saveDisabled, true);
    await s.page.close();
  }
  {
    const s = await session();
    await s.connect();
    assert.deepEqual(await s.ev(() => window.__mock.zaru.log.filter((l) => l.id === 0x2e).map((l) => l.payload.length)), [0]);
    await s.waitFor(() => window.__gyro.zaru.state.supported === true, "supported");
    let u = await s.ui();
    noScroll(u, "1440×900 settings (collapsed)");
    assert.equal(u.summary, "当前 0.30/0.70 °/s · 0.15 m/s² · 10/50/3 ms");
    if (SHOTS) await s.page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-settings-collapsed.png`) });
    await s.open(); await sleep(200);
    u = await s.ui();
    assert.ok(u.below, "below 零角速保持"); assert.deepEqual(u.values, ["0.3", "0.7", "0.15", "10", "50", "3"]);
    assert.deepEqual(u.labels, ["进入°/s", "退出°/s", "加速度偏差m/s²", "进入低通ms", "进入确认ms", "退出确认ms"]);
    assert.ok(u.disabled.every((d) => !d) && !u.saveDisabled && !u.read, "auto settings mode: editable when connected");
    assert.match(u.note, /静止时锁定航向，检测到运动后解除/); assert.match(u.note, /仅六轴融合 \+ 第 4 档「零角速保持」/); assert.ok(!/VQF|1\.5/.test(u.note));
    assert.match(u.filterHint, /零角速保持：静止时锁定航向，检测到运动后立即恢复更新/, "profile hint unchanged");
    assert.deepEqual(u.small, [], "no font < 12px"); assert.ok(u.fits, "fits in card");
    assert.equal(await s.ev(() => window.__gyro.state().setting), false, "not in settings mode while idle");
    const q0 = await s.ev(() => window.__mock.zaru.log.length); await s.page.click("#filterRead"); await s.waitFor((q) => window.__mock.zaru.log.length > q, "0x2E with filter 读取", 2000, q0);
    step("connect: empty 0x2E, values in 姿态稳定性 card under 零角速保持, editable via auto gate, fonts ≥ 12px, 1440×900 settings no scroll (collapsed), filter 读取 → 0x2E");
    if (SHOTS) { await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-zaru-open.png`) }); await s.page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-settings-zaru-open.png`) }); }

    // ---- B：保存 persist 0 / 1：自动 0x17 → 0x2F → ACK + 0x0F → 0x18 ----
    const c0 = (await s.cmds()).length;
    await s.fill({ enterDps: "0.12", exitDps: "0.9", accDevMs2: "0.33", enterFilterMs: "25", enterConfirmMs: "120", exitConfirmMs: "7" }); await s.setPersist(false);
    await s.page.click("#zaruSave");
    await s.waitFor(() => window.__gyro.zaru.state.result?.ok === true, "persist 0 ok"); await s.idle();
    let seq = (await s.cmds()).slice(c0).filter((id) => [0x17, 0x18, 0x2f].includes(id));
    assert.equal(seq[seq.length - 1], 0x18, "exit settings after"); assert.ok(seq.indexOf(0x17) < seq.indexOf(0x2f), "enter before write");
    let w = await s.writes(); assert.equal(w.at(-1).settings, 1); assert.deepEqual(w.at(-1).payload.slice(18), [0, 0]);
    assert.equal(await s.ev(() => window.__mock.zaru.saved.enterDps), 0.3); assert.equal(await s.ev(() => window.__mock.zaru.runtime.exitConfirmMs), 7);
    assert.match((await s.ui()).msg, /仅本次运行/); assert.equal(await s.ev(() => window.__gyro.state().setting), false);
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-zaru-write-persist0.png`) });
    await s.fill({ exitDps: "5", enterConfirmMs: "2000" }); await s.setPersist(true); await s.page.click("#zaruSave");
    await s.waitFor(() => window.__gyro.zaru.state.result?.ok === true && window.__mock.zaru.saved.exitDps === 5, "persist 1 ok"); await s.idle();
    u = await s.ui(); assert.match(u.msg, /已保存（重启后保留）/); assert.equal(u.saved, "已保存：与当前相同");
    assert.deepEqual(await s.ev(() => [window.__mock.filter.active, window.__mock.filter.log.filter((l) => l.id === 0x27).length]), [3, 0], "profile untouched, no 0x27");
    step("保存 persist 0 / 1 via auto gate: 0x17 → 0x2F(settings=1) → ACK + 0x0F readback → 0x18; saved only with persist 1; profile untouched");

    // ---- C：不合法 → 不进设置模式、不发送 ----
    const c1 = (await s.cmds()).length, n1 = (await s.writes()).length;
    await s.fill({ enterDps: "0.8", exitDps: "0.8" }); await s.page.click("#zaruSave"); await sleep(400);
    u = await s.ui(); assert.match(u.msg, /退出阈值必须大于进入阈值/); assert.equal(u.invalid[1], "true");
    await s.fill({ exitDps: "0.9", enterFilterMs: "201" }); await s.page.click("#zaruSave"); await sleep(300); assert.match((await s.ui()).msg, /进入低通范围 0～200 ms/);
    assert.equal((await s.writes()).length, n1); assert.deepEqual((await s.cmds()).slice(c1).filter((id) => [0x17, 0x18, 0x2f].includes(id)), [], "no 0x17/0x2F/0x18 for invalid input");
    step("validation: exit<=enter / 201 ms → message + field marked; no 0x17, no 0x2F");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-zaru-exit-le-enter.png`) });
    // 1024×768：折叠不滚动，展开时 3 列
    await s.open(false); await s.page.setViewport({ width: 1024, height: 768 }); await sleep(400);
    u = await s.ui(); noScroll(u, "1024×768 settings (collapsed)");
    await s.open(true); await sleep(200); u = await s.ui(); assert.ok(u.fits && u.scroll[1] <= 0 && u.scroll[3] <= 0, "1024 expanded: no horizontal overflow"); assert.deepEqual(u.small, []);
    if (SHOTS) await s.page.screenshot({ path: join(SHOTS, `${TAG}-1024x768-settings-zaru-open.png`) });
    step(`1024×768: settings no scroll collapsed; expanded fits (view scrolls ${u.scroll[2]} px inside, collapsible)`);
    // 断开 → 禁用
    await s.page.click("#disconnectBtn"); await s.waitFor(() => !window.__gyro.state().running, "disconnected"); await sleep(200);
    u = await s.ui(); assert.ok(u.disabled.every(Boolean) && u.saveDisabled && u.read, "disconnected → all disabled");
    step("disconnected → inputs / 保存 / 读取 disabled");
    await s.page.close();
  }
  // ---- D：超时 → 未确认；温度轮询暂停；门随后退出设置模式 ----
  {
    const s = await session({ zaru: { dropReply: true } });
    await s.connect(); await s.open();
    await s.waitFor(() => window.__gyro.tempPoller.state.replies > 2, "temp polling");
    await s.fill({ enterDps: "0.22", exitDps: "0.66" }); await s.page.click("#zaruSave");
    await s.waitFor(() => window.__gyro.zaru.state.pending?.acked, "ACK 0");
    assert.equal(await s.ev(() => window.__gyro.tempPollAllowed()), false);
    await sleep(350); const t0 = await s.ev(() => window.__mock.dev.commands.filter((c) => c.id === 0x14).length); await sleep(700);
    assert.equal(await s.ev(() => window.__mock.dev.commands.filter((c) => c.id === 0x14).length), t0, "0x14 paused");
    await s.idle();
    const u = await s.ui(); assert.match(u.msg, /未确认/); assert.deepEqual(u.values.slice(0, 2), ["0.22", "0.66"]); assert.equal(u.summary, "当前 0.30/0.70 °/s · 0.15 m/s² · 10/50/3 ms");
    assert.equal(await s.ev(() => window.__gyro.state().setting), false, "gate exited settings mode");
    step("timeout: 未确认, edits kept, display unchanged, 0x14 paused while pending, gate exits settings afterwards");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-zaru-unconfirmed.png`) });
    await s.page.close();
  }
  // ---- E：Flash 失败 ----
  {
    const s = await session({ zaru: { flashFail: true } });
    await s.connect(); await s.open();
    await s.fill({ enterDps: "0.25", exitDps: "0.8" }); await s.setPersist(true); await s.page.click("#zaruSave");
    await s.waitFor(() => window.__gyro.zaru.state.result, "result"); await s.idle();
    const u = await s.ui(); assert.match(u.msg, /Flash 写入失败/); assert.equal(u.result, "fail"); assert.equal(await s.ev(() => window.__mock.zaru.runtime.enterDps), 0.3);
    step("flash failure → ACK 0x03 error, runtime unchanged, edits kept");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-zaru-flash-fail.png`) });
    await s.page.close();
  }
  // ---- F：旧固件 → 隐藏；G：supported=0 → 只读 ----
  {
    const s = await session({ zaru: { supported: false } });
    await s.connect(); await s.waitFor(() => window.__gyro.zaru.state.supported === false, "unsupported");
    const u = await s.ui(); assert.equal(u.hidden, true); noScroll(u, "old fw settings");
    await s.page.click('#filterPanel label:has(input[value="1"])'); await s.page.click("#filterApply");
    await s.waitFor(() => window.__gyro.filter.state.result?.ok === true, "filter still works");
    assert.equal((await s.writes()).length, 0);
    step("old fw ACK 0x01 → editor hidden; filter apply still works; 0 × 0x2F");
    await s.page.close();
  }
  {
    const s = await session({ zaru: { flag: 0 } });
    await s.connect(); await s.open(); await s.waitFor(() => window.__gyro.zaru.state.supported === false, "flag 0");
    const u = await s.ui(); assert.equal(u.state, "readonly"); assert.ok(u.disabled.every(Boolean)); assert.equal(u.saveHidden, true); assert.match(u.msg, /阈值只读/);
    step("supported=0 → read-only, 保存 hidden");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-zaru-supported0.png`) });
    await s.page.close();
  }
  // ---- H：九轴 ----
  {
    const s = await session({ dev: { ...SIX, activeMode: 1, savedMode: 1 } });
    await s.connect(); await s.open(); await s.waitFor(() => window.__gyro.zaru.state.fusionMode === 1, "nine-axis");
    const u = await s.ui(); assert.match(u.note, /当前为九轴融合：阈值可读写，但不锁磁力计航向/);
    await s.fill({ enterDps: "0.35" }); await s.setPersist(false); await s.page.click("#zaruSave");
    await s.waitFor(() => window.__gyro.zaru.state.result?.ok === true, "nine-axis write"); await s.idle();
    step("九轴: note 不锁磁力计航向, write still works");
    if (SHOTS) await (await s.page.$("#filterPanel")).screenshot({ path: join(SHOTS, `${TAG}-zaru-nine-axis.png`) });
    await s.page.close();
  }
  assert.deepEqual(errors, []); step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e.stack || e.message, errors); process.exitCode = 1; } finally { await browser.close(); }
