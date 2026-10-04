// gyro3 quick1/quick2 headless Chrome check (mock serial, NOT real hardware):
// 首页设备卡六栏（Yaw/Pitch/Roll/温度/数据速率/数据）+ YPR 三位小数 + 3D 下方「快捷设置」（融合模式 立即重启 / 姿态稳定性 persist=1）。
// BASE=http://127.0.0.1:8833 node test/quick-settings.browser.check.mjs   (SHOTS=/dir optional)
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8833";
const CHROME = process.env.CHROME || "/usr/bin/google-chrome";
const SHOTS = process.env.SHOTS || "";
const QUICK = "20261002quick2";
const BIAS = process.env.BIAS || "20261004startup1"; // biashist1：app.js / style.css
const mock = readFileSync(join(here, "mock-serial.js"), "utf8") + "\n" + readFileSync(join(here, "filter-mock.js"), "utf8");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];

const browser = await puppeteer.launch({ executablePath: CHROME, headless: true, args: ["--no-sandbox", "--disable-background-timer-throttling", "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
try {
  async function open(setup, { w = 1440, h = 900 } = {}) {
    const page = await browser.newPage(); await page.setCacheEnabled(false);
    await page.setViewport({ width: w, height: h });
    await page.evaluateOnNewDocument(mock);
    page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
    page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
    page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url()) || !BASE.startsWith("http://127")) errors.push("requestfailed: " + r.url()); });
    const resp = await page.goto(BASE + "/", { waitUntil: "networkidle0" });
    assert.equal(resp.status(), 200);
    const ev = (fn, ...a) => page.evaluate(fn, ...a);
    const waitFor = async (fn, what, timeout = 5000, arg) => {
      const t0 = Date.now();
      for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); }
    };
    await ev(setup || (() => Object.assign(window.__mock.dev, { extended: true, configVersion: 3, capabilities: 127, canExtended: true })));
    await page.click("#connectBtn");
    await waitFor(() => document.getElementById("connStatus").textContent.includes("已连接"), "connected", 5000);
    return { page, ev, waitFor };
  }
  const layout = (page) => page.evaluate(() => {
    const d = document.scrollingElement; const v = [...document.querySelectorAll(".view")].find((x) => !x.hidden);
    return { sy: d.scrollHeight - innerHeight, sx: d.scrollWidth - innerWidth, vy: v.scrollHeight - v.clientHeight, vx: v.scrollWidth - v.clientWidth };
  });
  const cmds = (ev) => ev(() => window.__mock.dev.commands.map((c) => c.id));

  // ---------- 1. static + six items + 3 decimals ----------
  let s = await open();
  let { page, ev, waitFor } = s;
  // bias1：app.js / style.css 改为 20261002bias1（零角速保持阈值 + 运动零偏）；quick-settings.js 未改
  for (const [sel, attr, ver] of [["script[src^='/app.js']", "src", BIAS], ["script[src^='/quick-settings.js']", "src", QUICK], ["link[href^='/style.css']", "href", BIAS]])
    assert.ok((await page.$eval(sel, (n, a) => n.getAttribute(a), attr)).endsWith(`?v=${ver}`), sel);
  // quick2：恢复 temp1 布局（3D → Yaw/Pitch/Roll 卡 → 快捷设置；设备卡三栏），快捷设置无小字
  assert.deepEqual(await ev(() => [...document.querySelector(".home-main").children].map((n) => n.id || n.className)), ["stage-wrap", "card ypr", "quickSet"], "3D → YPR card → 快捷设置");
  assert.deepEqual(await ev(() => [...document.querySelectorAll(".card.ypr .ypr-item > span")].map((n) => n.textContent)), ["Yaw", "Pitch", "Roll"]);
  assert.deepEqual(await ev(() => [...document.querySelectorAll(".dev-card .dev-row .k")].map((n) => n.textContent)), ["温度", "数据速率", "数据"]);
  assert.equal(await ev(() => document.querySelectorAll("#quickSet .qs-k small").length), 0, "no subtitles");
  assert.deepEqual(await ev(() => [...document.querySelectorAll("#quickSet .qs-k")].map((n) => n.textContent)), ["融合模式", "姿态稳定性"]);
  assert.ok(!(await ev(() => document.getElementById("quickSet").textContent)).match(/切换即重启|重启后保留/), "subtitle text gone (tooltips only)");
  await waitFor(() => document.getElementById("yaw").textContent === "12.500", "yaw 3 decimals");
  assert.deepEqual(await ev(() => ["pitch", "roll"].map((id) => document.getElementById(id).textContent)), ["-3.250", "45.500"]);
  await ev(() => { window.__mock.dev.pose.yaw = -100.1234; });
  await waitFor(() => document.getElementById("yaw").textContent === "-100.123", "yaw update 3 decimals");
  await waitFor(() => document.getElementById("temp").textContent !== "--", "temp still shown");
  step("home: 3D → Yaw/Pitch/Roll card (3 decimals 12.500 / -3.250 / 45.500 / -100.123) → 快捷设置 (no subtitles); 设备 card 温度/数据速率/数据");

  // ---------- 2. layout ----------
  for (const [w, h] of [[1440, 900], [1024, 768]]) {
    await page.setViewport({ width: w, height: h }); await sleep(400);
    const l = await layout(page);
    assert.ok(l.sy <= 0 && l.sx <= 0 && l.vy <= 0 && l.vx <= 0, `no scroll ${w}x${h} ${JSON.stringify(l)}`);
    const minFont = await ev(() => Math.min(...[...document.querySelectorAll(".view-status *")].filter((e) => !e.closest("[hidden], .sr-only, svg") && [...e.childNodes].some((n) => n.nodeType === 3 && n.textContent.trim()) && e.getBoundingClientRect().width > 1).map((e) => parseFloat(getComputedStyle(e).fontSize))));
    assert.ok(minFont >= 12, `font ≥ 12px at ${w}x${h}: ${minFont}`);
    const order = await ev(() => ["stage", ".card.ypr", "#quickSet"].map((s) => document.querySelector(s.startsWith(".") || s.startsWith("#") ? s : "." + s).getBoundingClientRect()).map((r) => [Math.round(r.top), Math.round(r.bottom)]));
    assert.ok(order[0][1] <= order[1][0] && order[1][1] <= order[2][0], `vertical order 3D → YPR → 快捷设置 ${JSON.stringify(order)}`);
    if (SHOTS) { mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: join(SHOTS, `gyro3-quick2-home-${w}.png`) }); }
  }
  await page.setViewport({ width: 1440, height: 900 }); await sleep(300);
  // charts shown (IMU stream) → still no scroll
  await ev(() => { window.__mock.dev.streamMode = 3; }); await sleep(800);
  for (const [w, h] of [[1440, 900], [1024, 768]]) {
    await page.setViewport({ width: w, height: h }); await sleep(400);
    assert.ok(await ev(() => document.querySelector(".home-grid").classList.contains("has-charts")), "charts shown");
    const l = await layout(page);
    assert.ok(l.sy <= 0 && l.sx <= 0 && l.vy <= 0 && l.vx <= 0, `no scroll with charts ${w}x${h} ${JSON.stringify(l)}`);
  }
  await ev(() => { window.__mock.dev.streamMode = 0; });
  await page.setViewport({ width: 1440, height: 900 }); await waitFor(() => document.getElementById("yaw").textContent !== "--", "pose back", 4000); await sleep(300);
  step("no page/view scroll, font ≥ 12px at 1440×900 and 1024×768 (charts hidden and shown); order 3D → YPR → 快捷设置");

  // ---------- 3. initial state ----------
  await waitFor(() => window.__gyro.filter.state.config && window.__gyro.state().deviceConfig, "config read");
  assert.deepEqual(await ev(() => [...document.querySelectorAll("#quickSet button.on")].map((b) => b.textContent)), ["九轴", "响应优先"]);
  assert.deepEqual(await ev(() => [...document.querySelectorAll("#qsFilter button")].filter((b) => !b.hidden).map((b) => b.textContent)), ["响应优先", "均衡", "静态稳定", "零角速保持"]);
  assert.equal(await ev(() => document.getElementById("qsFusionRow").hidden), false);
  step("快捷设置 reflects device: 九轴 (0x07 activeMode=1), 响应优先 (0x0B active=0), 4 profiles (caps=4)");

  // ---------- 4. filter quick switch persist=1 ----------
  let b0 = (await cmds(ev)).length;
  await page.click('#qsFilter button[data-profile="2"]');
  await waitFor(() => !window.GyroUI.quick.state.filter && window.GyroUI.gate.busy === null && !window.__gyro.filter.state.pending, "filter quick done", 6000);
  let seq = (await cmds(ev)).slice(b0);
  assert.deepEqual(seq.filter((c) => [0x17, 0x18, 0x26, 0x27].includes(c)).slice(-4), [0x17, 0x27, 0x26, 0x18], "auto enter → 0x27 → 0x26 verify → exit");
  assert.deepEqual(await ev(() => window.__mock.filter.log.filter((l) => l.id === 0x27).at(-1).payload), [2, 1], "0x27 payload [2, persist=1]");
  for (const bad of [0x15, 0x19, 0x1e, 0x1d]) assert.ok(!seq.includes(bad), "no reboot/fusion cmd " + bad);
  assert.deepEqual(await ev(() => [window.__mock.filter.active, window.__mock.filter.saved]), [2, 2]);
  assert.equal(await ev(() => document.querySelector("#qsFilter button.on").textContent), "静态稳定");
  assert.match(await ev(() => document.getElementById("qsMsg").textContent), /已切换为静态稳定（重启后保留）/);
  assert.equal(await ev(() => document.getElementById("filterState").textContent), "当前：静态稳定 · 已保存：静态稳定 · 内部 1000 Hz");
  step("姿态稳定性 quick 静态稳定: 0x17 → 0x27[2,1] → ACK → 0x26 readback active=saved=2 → 0x18; no reboot/fusion cmds");

  // filter failure (ACK 0x03) → reported, device unchanged
  await ev(() => { window.__mock.filter.forceStatus = 3; });
  await page.click('#qsFilter button[data-profile="0"]');
  await waitFor(() => !window.GyroUI.quick.state.filter && window.GyroUI.gate.busy === null, "filter fail done", 6000);
  assert.match(await ev(() => document.getElementById("qsMsg").textContent), /失败/);
  assert.equal(await ev(() => document.querySelector("#qsFilter button.on").textContent), "静态稳定");
  await ev(() => { window.__mock.filter.forceStatus = null; });
  step("filter quick failure (ACK 0x03): message shown, highlight stays on device value");

  // ---------- 5. fusion quick switch → STARTUP apply_now=1 → restart → reconnect → readback ----------
  const restartBefore = await ev(() => document.getElementById("restartNow").checked);
  b0 = (await cmds(ev)).length;
  await page.click('#qsFusion button[data-mode="0"]');
  await waitFor(() => window.GyroUI.quick.state.fusion?.phase === "restarting", "restarting", 4000);
  await waitFor(() => !window.GyroUI.quick.state.fusion, "fusion done", 20000);
  const all = await ev(() => window.__mock.dev.commands.map((c) => ({ id: c.id, payload: c.payload })));
  seq = all.slice(b0).map((c) => c.id);
  const st = all.slice(b0).find((c) => c.id === 0x1e);
  assert.ok(st, "0x1e STARTUP sent");
  assert.deepEqual(st.payload, [0, 0, 1, 2000 & 0xff, 2000 >> 8, 1000 & 0xff, 1000 >> 8], "STARTUP [mode 0, fast 0, apply_now 1, init 2000 ms, range 1000 dps] (saved values)");
  assert.ok(seq.indexOf(0x17) >= 0 && seq.indexOf(0x17) < seq.indexOf(0x1e), "auto ENTER before STARTUP");
  assert.ok(seq.lastIndexOf(0x1f) > seq.indexOf(0x1e), "0x1f readback after restart");
  assert.ok(!seq.includes(0x19) && !seq.includes(0x15), "no other reboot/mode cmds");
  const d = await ev(() => ({ a: window.__gyro.state().deviceConfig?.activeMode, m: window.__mock.dev.activeMode, dirty: window.__gyro.state().startupFormDirty, conn: document.getElementById("connStatus").textContent, msg: document.getElementById("qsMsg").textContent, on: document.querySelector("#qsFusion button.on")?.textContent, rn: document.getElementById("restartNow").checked }));
  assert.equal(d.a, 0); assert.equal(d.m, 0); assert.equal(d.dirty, false); assert.match(d.conn, /已连接/);
  assert.match(d.msg, /已切换为六轴（已重启）/); assert.equal(d.on, "六轴"); assert.equal(d.rn, restartBefore, "保存后重启 restored");
  assert.deepEqual(await ev(() => [window.__mock.filter.active, window.__mock.filter.saved]), [2, 2], "persisted filter survives restart");
  assert.equal(await ev(() => document.querySelector("#qsFilter button.on").textContent), "静态稳定");
  step("融合模式 quick 六轴: 0x17 → 0x1e[0,0,1,…] → ACK → restart → auto reconnect → 0x1f activeMode=0 ✓; 静态稳定 kept after restart");

  // fusion failure (save fails) → no restart, form reverted
  await ev(() => { window.__mock.dev.failSave = true; });
  b0 = (await cmds(ev)).length;
  await page.click('#qsFusion button[data-mode="1"]');
  await waitFor(() => !window.GyroUI.quick.state.fusion && window.GyroUI.gate.busy === null, "fusion fail done", 8000);
  const f2 = await ev(() => ({ msg: document.getElementById("qsMsg").textContent, a: window.__gyro.state().deviceConfig?.activeMode, dirty: window.__gyro.state().startupFormDirty, radio: document.querySelector('input[name="fusion"]:checked').value, plan: !!window.__gyro.state().reconnectPlan, on: document.querySelector("#qsFusion button.on")?.textContent }));
  assert.match(f2.msg, /失败/); assert.equal(f2.a, 0); assert.equal(f2.dirty, false); assert.equal(f2.radio, "0"); assert.equal(f2.on, "六轴");
  await ev(() => { window.__mock.dev.failSave = false; });
  step("fusion quick failure (STARTUP ACK FAIL): no restart, message shown, settings form reverted to saved");

  // gating: busy device → buttons disabled
  await ev(() => { window.GyroUI.gate.busy = "test"; }); await sleep(250);
  assert.ok(await ev(() => [...document.querySelectorAll("#quickSet button")].filter((b) => !b.hidden).every((b) => b.disabled)), "disabled while gate busy");
  await ev(() => { window.GyroUI.gate.busy = null; }); await sleep(250);
  assert.ok(await ev(() => !document.querySelector('#qsFusion button[data-mode="1"]').disabled));
  if (SHOTS) await page.screenshot({ path: join(SHOTS, "gyro3-quick2-home.png") });
  await page.click("#disconnectBtn"); await sleep(400);
  assert.ok(await ev(() => [...document.querySelectorAll("#quickSet button")].every((b) => b.disabled)), "disabled when disconnected");
  step("gating: all quick buttons disabled while settings gate busy and when disconnected");
  await page.close();

  // ---------- 6. caps=3 hides 零角速保持; old firmware hides fusion ----------
  s = await open(() => { Object.assign(window.__mock.dev, { extended: true, configVersion: 3, capabilities: 127 }); window.__mock.filter.profileCount = 3; });
  await s.waitFor(() => window.__gyro.filter.state.profileCount === 3, "caps 3");
  await sleep(300);
  assert.deepEqual(await s.ev(() => [...document.querySelectorAll("#qsFilter button")].filter((b) => !b.hidden).map((b) => b.textContent)), ["响应优先", "均衡", "静态稳定"]);
  step("filter caps=3: 零角速保持 hidden in 快捷设置");
  await s.page.close();
  s = await open(() => { Object.assign(window.__mock.dev, { extended: false }); window.__mock.filter.supported = false; });
  await sleep(2200);
  const old = await s.ev(() => ({ f: document.getElementById("qsFusionRow").hidden, p: document.getElementById("qsFilterRow").hidden, e: document.getElementById("qsEmpty").hidden, yaw: document.getElementById("yaw").textContent }));
  assert.deepEqual([old.f, old.p, old.e], [true, true, false], "old firmware: both rows hidden, short note shown");
  assert.match(old.yaw, /^-?\d+\.\d{3}$/);
  step("old firmware (no 0x07 CONFIG, 0x26 ACK 0x01): 融合模式 + 姿态稳定性 hidden, 「当前固件不支持快捷设置」");
  await s.page.close();

  assert.deepEqual(errors, [], "JS errors: " + errors.join("\n"));
  step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} finally { await browser.close(); }
