// vqfinit1：静置初始化 VQF（vqf-init.js）headless Chrome 检查 —— gyro3 校准页（自动设置模式门：0x17 → 0x2A → 0x18）。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8993 SHOTS=/workspace/vqfinit1/shots TAG=gyro3 node test/vqf-init.browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8993", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "gyro3";
const VERSION = process.env.VERSION || "20261002vqfinit1"; // vqf-init.js
const FWV = process.env.FW_VERSION || "20261002fwver1"; // fwver1：ui-shell.js
const BHV = process.env.BH_VERSION || "20261002biashist1"; // biashist1：app.js / style.css
const mock = ["mock-serial.js", "filter-mock.js", "zaru-mock.js", "bias-mock.js", "vqf-mock.js"].map((f) => readFileSync(join(here, f), "utf8")).join("\n");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
const SIX = { extended: true, configVersion: 3, capabilities: 127, activeMode: 0, savedMode: 0 };
const ERR = { 1: "采集前一直没放稳，或采集中移动了", 2: "陀螺噪声过大", 9: "校验通过，但 Flash 写入失败。旧记录保留，运行参数不变" };

async function session({ vw = 1440, vh = 900, vqf = {} } = {}) {
  const page = await browser.newPage(); await page.setViewport({ width: vw, height: vh });
  await page.evaluateOnNewDocument(mock);
  await page.evaluateOnNewDocument((dev, vqf) => { Object.assign(window.__mock.dev, dev); Object.assign(window.__mock.filter, { profileCount: 4, active: 1, saved: 1 }); Object.assign(window.__mock.vqf, vqf); }, SIX, vqf);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url())) errors.push("requestfailed: " + r.url()); });
  assert.equal((await page.goto(BASE + "/", { waitUntil: "networkidle0" })).status(), 200);
  const ev = (fn, ...a) => page.evaluate(fn, ...a);
  const waitFor = async (fn, what, timeout = 6000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); } };
  const x = { page, ev, waitFor };
  x.nav = async (v) => { await page.click(`.nav-item[data-nav="${v}"]`); await waitFor((v) => !document.querySelector(`.view[data-view="${v}"]`).hidden, "view " + v, 3000, v); await sleep(150); };
  x.connect = async () => {
    await x.nav("status"); await page.click("#connectBtn");
    await waitFor(() => window.__gyro?.filter?.state.config && window.__mock.vqf.log.length >= 2, "connect + 0x2C/0x29");
    await sleep(200); await x.nav("calib");
  };
  x.cmds = () => ev(() => window.__mock.dev.commands.map((c) => c.id));
  x.idle = () => waitFor(() => !window.GyroUI.gate.busy && !window.__gyro.vqf.state.pending, "gate idle", 7000);
  x.ui = () => ev(() => {
    const $ = (id) => document.getElementById(id), p = $("vqfPanel"), d = document.scrollingElement;
    const v = [...document.querySelectorAll(".view")].find((x) => !x.hidden);
    const vis = (n) => !!n && !n.hidden && n.getClientRects().length > 0;
    const small = [...p.querySelectorAll("*")].filter((n) => n.getClientRects().length && n.textContent.trim() && parseFloat(getComputedStyle(n).fontSize) < 12).map((n) => n.tagName + "#" + n.id + "." + n.className);
    const rows = [...$("vqfParams").querySelectorAll("tr")].map((r) => [r.querySelector("th").textContent.trim(), ...[...r.querySelectorAll("td")].map((t) => t.textContent)]);
    const r = p.getBoundingClientRect();
    return { hidden: p.hidden || !vis(p), state: p.dataset.state, result: p.dataset.result, source: $("vqfSource").textContent, rows, bias: $("vqfBias").textContent, biasK: $("vqfBiasK").textContent,
      temp: vis($("vqfTempRow")) ? $("vqfTemp").textContent : null, st: $("vqfState").textContent, bar: $("vqfBar").style.width, prog: vis($("vqfProg")), live: $("vqfLive").textContent, warn: $("vqfLive").dataset.warn,
      msg: $("vqfMsg").textContent, start: [vis($("vqfStart")), $("vqfStart").disabled, $("vqfStart").textContent], cancel: [vis($("vqfCancel")), $("vqfCancel").disabled],
      restore: [vis($("vqfRestore")), $("vqfRestore").disabled, $("vqfRestore").textContent], busyBody: document.body.classList.contains("vqf-busy"),
      otherCard: !!$("gyro60Btn") || !!$("zeroBtn") || /其他校准/.test(document.querySelector('.view[data-view="calib"]').textContent),
      small, inView: r.bottom <= innerHeight + 0.5 && r.right <= innerWidth + 0.5, message: $("message").textContent,
      scroll: [d.scrollHeight - innerHeight, d.scrollWidth - innerWidth, v.scrollHeight - v.clientHeight, v.scrollWidth - v.clientWidth] };
  });
  x.log = () => ev(() => document.getElementById("log").textContent);
  return x;
}
const noScroll = (u, what) => assert.ok(u.scroll.every((n) => n <= 0), `${what} no scroll ${JSON.stringify(u.scroll)}`);
const gateSeq = async (s, c0, ids) => { const q = (await s.cmds()).slice(c0).filter((id) => [0x17, 0x18, ...ids].includes(id)); const i = q.indexOf(0x17); assert.ok(i < 0 || q.slice(0, i).every((id) => id === 0x18), "only EXIT probe before 0x17 " + JSON.stringify(q)); return i < 0 ? q : q.slice(i); };
const shot = async (s, name) => { if (SHOTS) await s.page.screenshot({ path: join(SHOTS, `${TAG}-${name}.png`) }); };
const card = async (s, name) => { if (SHOTS) await (await s.page.$("#vqfPanel")).screenshot({ path: join(SHOTS, `${TAG}-vqf-${name}.png`) }); };
try {
  // ---- A：成功路径 + 锁定 + 恢复默认（1440×900）----
  {
    const s = await session({ vqf: { collectMs: 15000, preMs: 1250 } }); // 采集期间要点遍被锁按钮：拉长 mock 时间
    for (const [sel, attr, ver] of [["script[src^='/vqf-init.js']", "src", VERSION], ["script[src^='/app.js']", "src", BHV], ["script[src^='/ui-shell.js']", "src", FWV], ["link[href^='/style.css']", "href", BHV]])
      assert.ok((await s.page.$eval(sel, (e, a) => e.getAttribute(a), attr)).endsWith(`?v=${ver}`), sel);
    await s.connect(); await s.waitFor(() => window.__gyro.vqf.state.supported === true && window.__gyro.vqf.state.settings, "supported");
    const first = await s.ev(() => window.__mock.vqf.log.map((l) => [l.id, l.payload.length]));
    assert.deepEqual(first.slice(0, 2), [[0x2c, 0], [0x29, 0]], "connect sends empty 0x2C then 0x29");
    let u = await s.ui();
    assert.equal(u.hidden, false); assert.equal(u.source, "默认参数");
    assert.deepEqual(u.rows, [["初始 σ °/s", "0.500", "0.500"], ["静止 σ °/s", "0.035", "0.035"], ["静止门限 · 陀螺 °/s", "0.60", "0.60"], ["静止门限 · 加速度 m/s²", "0.15", "0.15"]]);
    assert.equal(u.biasK, "零偏（VQF 当前）"); assert.match(u.bias, /\+0\.300 \/ \+0\.038 \/ -0\.391 °\/s/); assert.equal(u.temp, null);
    assert.equal(u.st, "空闲"); assert.equal(u.prog, false); assert.deepEqual(u.start, [true, false, "开始静置"]); assert.equal(u.cancel[0], false); assert.equal(u.restore[0], false, "恢复默认 hidden when source=0");
    assert.equal(u.otherCard, false, "其他校准 card removed (fwver1)");
    assert.deepEqual(u.small, []); assert.ok(u.inView, "card fully visible"); noScroll(u, "1440×900 calib idle");
    await shot(s, "1440x900-vqf-idle"); await card(s, "idle");
    step("card on 校准 page: 当前/默认 from 0x0E, source 默认参数, 恢复默认 hidden, 其他校准 card removed, fonts ≥ 12 px, no scroll at 1440×900");
    // 开始：0x17 → 0x2A → 0x18（采集期间不占设置模式）
    const c0 = (await s.cmds()).length;
    await s.page.click("#vqfStart");
    await s.waitFor(() => window.__gyro.vqf.active(), "active"); await s.idle();
    assert.deepEqual(await gateSeq(s, c0, [0x2a]), [0x17, 0x2a, 0x18]);
    assert.equal(await s.ev(() => window.__mock.vqf.log.find((l) => l.id === 0x2a).settings), 1, "0x2A sent in settings mode");
    assert.equal(await s.ev(() => window.__gyro.state().setting), false, "settings exited while collecting");
    assert.equal(await s.ev(() => window.__mock.vqf.state >= 1 && window.__mock.vqf.state <= 4), true, "EXIT does not cancel (mock per main.c)");
    u = await s.ui(); assert.match(u.msg, /^已开始：请保持设备静止/); assert.equal(u.start[0], false); assert.deepEqual(u.cancel, [true, false]); assert.equal(u.busyBody, true);
    step("start: 0x17 → 0x2A (settings=1) → ACK + 0x0D → 0x18; UI 已开始, 取消 shown, body.vqf-busy");
    await s.waitFor(() => window.__gyro.vqf.state.status?.state === 2, "state 2");
    u = await s.ui(); assert.match(u.st, /^预稳定计时中：剩余 6[0-5] s（含采集）$/, u.st); assert.equal(u.prog, true); assert.equal(u.bar, "0%");
    await s.waitFor(() => window.__gyro.vqf.state.status?.state === 3 && window.__gyro.vqf.state.status.remainingMs < 40000, "state 3 mid", 15000);
    u = await s.ui(); assert.match(u.st, /^正在采集：剩余 \d+ s$/); const pct = parseFloat(u.bar); assert.ok(pct > 20 && pct < 100, "bar " + u.bar);
    assert.match(u.live, /^角速度 0\.12 °\/s · 加速度偏差 0\.03 m\/s² · 31\.4 °C · 样本 \d+$/); assert.equal(u.warn, "false");
    const tot = await s.ev(() => window.__gyro.vqf.state.collectTotal); assert.ok(tot > 55000 && tot <= 60000, "collect total from first state-3 frame: " + tot);
    step(`progress from remaining_ms only: state 2 剩余含采集, state 3 bar ${u.bar} (total ${tot} ms from first frame), live |ω| / acc dev / temp / samples`);
    // 锁定：被阻止的按钮点击不发送；直接发送时设备回 0x0703
    await shot(s, "1440x900-vqf-collecting"); await card(s, "collecting");
    const c1 = (await s.cmds()).length;
    await s.page.click("#acc6Btn").catch(() => {}); await sleep(150);
    await s.nav("settings");
    for (const sel of ["#filterApply", "#zaruSave"]) await s.ev((sel) => document.querySelector(sel)?.click(), sel);
    await s.nav("status");
    await s.ev(() => document.querySelector("#qsFilter button[data-profile='2']")?.click()); await s.ev(() => document.querySelector("#qsFusion button[data-mode='1']")?.click()); await sleep(300);
    const after = (await s.cmds()).slice(c1);
    assert.ok(![0x17, 0x19, 0x1c, 0x1d, 0x1e, 0x27, 0x2d, 0x2f, 0x31].some((id) => after.includes(id)), "locked buttons send nothing: " + JSON.stringify(after));
    const locked = await s.ev(() => ["applyBtn", "acc6Btn", "outputApplyAll", "rateApplyBtn", "filterApply", "zaruSave", "zaruRestore"].map((id) => document.getElementById(id)?.hasAttribute("data-vqf-lock")));
    assert.ok(locked.every(Boolean), "lock attr " + JSON.stringify(locked));
    assert.ok(await s.ev(() => [...document.querySelectorAll("#qsFilter button, #qsFusion button")].every((b) => b.hasAttribute("data-vqf-lock"))));
    await s.ev(() => send(CMD.SET_FILTER, [2, 0])); await sleep(300);
    assert.match(await s.log(), /ACK CMD 0x27 .*detail=0x0703（静置初始化进行中，设备不执行该命令（0x0703））/);
    const qa = (await s.cmds()).length; await s.ev(() => send(CMD.QUERY_ZARU)); await sleep(200);
    assert.ok((await s.cmds()).slice(qa).includes(0x2e), "queries still allowed");
    assert.equal(await s.ev(() => window.__gyro.vqf.active()), true, "still collecting during lock checks");
    step("while collecting: 0x19/0x1C/0x1D/0x1E/0x27/0x2F/0x31 buttons + quick settings locked (nothing sent); forced 0x27 → ACK 0x03/0x0703 shown in log; queries allowed");
    await s.nav("calib");
    await s.waitFor(() => window.__gyro.vqf.state.status?.state === 5, "success", 25000); await sleep(250);
    u = await s.ui();
    assert.equal(u.msg, "静置初始化成功：已写入 Flash 并立即生效"); assert.equal(u.result, "ok"); assert.equal(u.source, "静置初始化"); assert.equal(u.busyBody, false);
    assert.deepEqual(u.rows[0], ["初始 σ °/s", "0.100", "0.500"]); assert.equal(u.biasK, "零偏（静置记录）"); assert.equal(u.temp, "31.4 °C"); assert.equal(u.st, "上次结果：成功");
    assert.deepEqual(u.restore, [true, false, "恢复默认"]); assert.equal(u.start[0], true); assert.equal(u.prog, false);
    assert.equal(await s.ev(() => window.__mock.vqf.flashWrites), 1);
    assert.equal(await s.ev(() => document.getElementById("acc6Btn").hasAttribute("data-vqf-lock")), false, "unlocked after end");
    await card(s, "success"); await shot(s, "1440x900-vqf-success");
    step("end: 0x0D state 5 + 0x0E (seq 0) → 成功 text, source 静置初始化, current from 0x0E, 标定温度 shown, 恢复默认 enabled, locks released");
    // 恢复默认：第一次只上膛
    const c2 = (await s.cmds()).length;
    await s.page.click("#vqfRestore"); await sleep(250);
    u = await s.ui(); assert.equal(u.restore[2], "确认恢复"); assert.match(u.msg, /恢复后，这次静置结果会清除/);
    assert.deepEqual(await gateSeq(s, c2, [0x2d]), [], "arming sends nothing");
    await card(s, "restore-armed");
    await s.page.click("#vqfRestore");
    await s.waitFor(() => window.__gyro.vqf.state.result?.ok === true, "restore ok"); await s.idle();
    assert.deepEqual(await gateSeq(s, c2, [0x2d]), [0x17, 0x2d, 0x18]);
    u = await s.ui(); assert.match(u.msg, /^已恢复默认 VQF 参数/); assert.equal(u.source, "默认参数"); assert.deepEqual(u.rows[0], ["初始 σ °/s", "0.500", "0.500"]); assert.equal(u.restore[0], false);
    assert.match(u.bias, /\+0\.300 \/ \+0\.038 \/ -0\.391/, "bias back to pre-start values");
    step("restore: confirm → 0x17 → 0x2D → ACK + 0x0E source 0, current == defaults → success; 恢复默认 hidden again");
    // 1024×768
    await s.page.setViewport({ width: 1024, height: 768 }); await sleep(400);
    u = await s.ui(); noScroll(u, "1024×768 idle"); assert.ok(u.inView); assert.deepEqual(u.small, []);
    await shot(s, "1024x768-vqf-idle");
    await s.page.click("#vqfStart"); await s.waitFor(() => window.__gyro.vqf.state.status?.state === 3, "state 3"); await sleep(300);
    u = await s.ui(); noScroll(u, "1024×768 collecting"); assert.ok(u.inView, "card visible 1024 collecting"); assert.deepEqual(u.small, []);
    await shot(s, "1024x768-vqf-collecting");
    // 取消（不需设置模式）
    const c3 = (await s.cmds()).length;
    await s.page.click("#vqfCancel"); await s.waitFor(() => !window.__gyro.vqf.active() && !window.__gyro.vqf.state.pending, "cancelled");
    assert.deepEqual((await s.cmds()).slice(c3).filter((id) => [0x17, 0x2b].includes(id)), [0x2b], "cancel without settings mode");
    u = await s.ui(); assert.equal(u.msg, "已取消：未写 Flash，参数不变"); assert.equal(u.source, "默认参数"); assert.equal(await s.ev(() => window.__mock.vqf.flashWrites), 2);
    step("1024×768: idle + collecting no scroll, card in view, fonts ≥ 12 px; cancel → 0x2B only → 已取消：未写 Flash，参数不变");
    await s.page.click("#disconnectBtn"); await s.waitFor(() => !window.__gyro.state().running, "disc"); await sleep(200);
    u = await s.ui(); assert.equal(u.start[1], true); step("disconnected → 开始 disabled");
    await s.page.close();
  }
  // ---- B：失败原因 ----
  for (const e of [2, 9, 1]) {
    const s = await session({ vqf: { error: e } });
    await s.connect(); await s.waitFor(() => window.__gyro.vqf.state.supported === true, "supported");
    await s.page.click("#vqfStart"); await s.waitFor(() => window.__gyro.vqf.state.status?.state === 6, "failed " + e, 8000); await sleep(200);
    const u = await s.ui(); assert.equal(u.msg, `静置初始化失败：${ERR[e]}`); assert.equal(u.result, "fail"); assert.equal(u.source, "默认参数"); assert.equal(u.restore[0], false);
    if (e === 2) await card(s, "failed");
    step(`error ${e} → 静置初始化失败：${ERR[e]}`);
    await s.page.close();
  }
  // ---- C：开始被拒（0x0702 / 0x0701）→ 不切成采集 ----
  for (const [det, re] of [[0x0702, /主循环还没开始/], [0x0701, /已经在采集，或六面校准还在进行/]]) {
    const s = await session({ vqf: { startStatus: 3, startDetail: det } });
    await s.connect(); await s.waitFor(() => window.__gyro.vqf.state.supported === true, "supported");
    const c0 = (await s.cmds()).length; await s.page.click("#vqfStart"); await s.waitFor(() => window.__gyro.vqf.state.result, "result"); await s.idle();
    const u = await s.ui(); assert.match(u.msg, re); assert.equal(u.state, "idle"); assert.equal(u.prog, false); assert.equal(u.start[0], true); assert.equal(u.busyBody, false);
    assert.deepEqual(await gateSeq(s, c0, [0x2a]), [0x17, 0x2a, 0x18]);
    step(`start refused 0x${det.toString(16).padStart(4, "0")} → message, UI stays idle, settings exited`);
    await s.page.close();
  }
  // ---- D：恢复默认被拒：Flash 失败（ACK 3 + 原值 0x0E）----
  {
    const s = await session({ vqf: { source: 1, current: { sigmaInit: 0.1, sigmaRest: 0.035, restGyr: 0.6, restAcc: 0.15 }, calTempC: 30.5, restoreFlashFail: true } });
    await s.connect(); await s.waitFor(() => window.__gyro.vqf.state.settings?.source === 1, "source 1");
    let u = await s.ui(); assert.equal(u.restore[0], true); assert.equal(u.temp, "30.5 °C");
    await s.page.click("#vqfRestore"); await sleep(150); await s.page.click("#vqfRestore");
    await s.waitFor(() => window.__gyro.vqf.state.result, "result"); await s.idle();
    u = await s.ui(); assert.match(u.msg, /^恢复失败（ACK 0x03：Flash 写入失败或未在设置模式）：回读确认参数和静置记录都未变/); assert.equal(u.source, "静置初始化"); assert.equal(u.rows[0][1], "0.100");
    step("restore Flash failure → ACK 0x03 + unchanged 0x0E → failure text, values kept");
    await s.page.close();
  }
  // ---- E：采集中发 0x2D（0x0703），带 / 不带 0x0E ----
  for (const withE of [false, true]) {
    const s = await session({ vqf: { source: 1, current: { sigmaInit: 0.1, sigmaRest: 0.035, restGyr: 0.6, restAcc: 0.15 }, busyRestoreSettings: withE } });
    await s.connect(); await s.waitFor(() => window.__gyro.vqf.state.settings?.source === 1, "source 1");
    await s.page.click("#vqfStart"); await s.waitFor(() => window.__gyro.vqf.active(), "active"); await s.idle();
    let u = await s.ui(); assert.equal(u.restore[0], false, "恢复默认 hidden while collecting");
    // 绕过界面强制发送（模拟另一宿主）：前端不得把它当成已恢复
    await s.ev(() => send(CMD.RESTORE_VQF)); await sleep(400);
    assert.match(await s.log(), /ACK CMD 0x2d .*detail=0x0703/);
    u = await s.ui(); assert.equal(u.source, "静置初始化"); assert.doesNotMatch(u.msg, /已恢复/); assert.equal(await s.ev(() => window.__gyro.vqf.active()), true);
    step(`0x2D while collecting → ACK 0x03/0x0703 ${withE ? "+ 0x0E" : "(no 0x0E)"}: not treated as restored, collection continues`);
    await s.page.click("#vqfCancel"); await s.waitFor(() => !window.__gyro.vqf.active(), "cancel");
    await s.page.close();
  }
  // ---- F：采集中无推送 → 1 s 后静默补查 0x29，进度继续 ----
  {
    const s = await session({ vqf: { noPush: true, collectMs: 6000, preMs: 500 } });
    await s.connect(); await s.waitFor(() => window.__gyro.vqf.state.supported === true, "supported");
    const p0 = await s.ev(() => window.__mock.vqf.log.filter((l) => l.id === 0x29).length);
    const l0 = ((await s.log()).match(/发送 CMD 0x29/g) || []).length;
    await s.page.click("#vqfStart"); await s.waitFor(() => window.__gyro.vqf.state.status?.state === 3, "state 3 via polling", 8000);
    const r1 = await s.ev(() => window.__gyro.vqf.state.status.remainingMs); await sleep(1600);
    const r2 = await s.ev(() => window.__gyro.vqf.state.status.remainingMs); assert.ok(r2 < r1, `remaining advances via polls ${r1} → ${r2}`);
    await s.waitFor(() => window.__gyro.vqf.state.status?.state === 5, "success", 12000);
    const polls = await s.ev(() => window.__mock.vqf.log.filter((l) => l.id === 0x29).length) - p0; assert.ok(polls >= 4, "quiet 0x29 polls " + polls);
    assert.equal(((await s.log()).match(/发送 CMD 0x29/g) || []).length, l0, "fallback polls are quiet (no log lines)");
    step(`no pushes → quiet 0x29 fallback (${polls} polls), completes`);
    await s.page.close();
  }
  // ---- G：旧固件 ACK 0x01 / 3 s 无数据 → 整个功能隐藏 ----
  {
    const s = await session({ vqf: { supported: false } });
    await s.connect(); await s.waitFor(() => window.__gyro.vqf.state.supported === false, "unsupported");
    const u = await s.ui(); assert.equal(u.hidden, true); noScroll(u, "old fw calib");
    assert.equal(await s.ev(() => document.querySelector(".calib-side").getBoundingClientRect().height), 0, "right column empty");
    await shot(s, "1440x900-vqf-oldfw");
    step("old fw (ACK 0x01) → whole 静置初始化 card hidden, calib page still no scroll");
    await s.page.close();
  }
  {
    const s = await session({ vqf: { silent: true } });
    await s.connect(); assert.equal((await s.ui()).hidden, false, "visible while probing");
    const t0 = Date.now(); await s.waitFor(() => window.__gyro.vqf.state.supported === false, "3 s timeout", 5000);
    const dt = Date.now() - t0; assert.equal((await s.ui()).hidden, true);
    step(`no 0x0D/0x0E within 3 s → hidden (after ~${dt} ms more)`);
    await s.page.close();
  }
  assert.deepEqual(errors, []); step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e.stack || e.message, errors); process.exitCode = 1; } finally { await browser.close(); }
