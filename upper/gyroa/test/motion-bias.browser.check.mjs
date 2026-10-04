// bias1：VQF 运动零偏状态（只读）headless Chrome 检查—— gyro3（高级设置诊断区）。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8972 SHOTS=/workspace/bias1/shots TAG=gyro3 node test/motion-bias.browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8972", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "gyro3";
const VERSION = process.env.VERSION || "20261002bias1";
const mock = ["mock-serial.js", "filter-mock.js", "zaru-mock.js", "bias-mock.js"].map((f) => readFileSync(join(here, f), "utf8")).join("\n");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
const SIX = { extended: true, configVersion: 3, capabilities: 127, activeMode: 0, savedMode: 0 };
async function session({ vw = 1440, vh = 900, dev = SIX, filter = { profileCount: 4, active: 1, saved: 1 }, bias = {} } = {}) {
  const page = await browser.newPage(); await page.setViewport({ width: vw, height: vh });
  await page.evaluateOnNewDocument(mock);
  await page.evaluateOnNewDocument((dev, filter, bias) => { Object.assign(window.__mock.dev, dev); Object.assign(window.__mock.filter, filter); Object.assign(window.__mock.bias, bias); }, dev, filter, bias);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url())) errors.push("requestfailed: " + r.url()); });
  assert.equal((await page.goto(BASE + "/", { waitUntil: "networkidle0" })).status(), 200);
  const ev = (fn, ...a) => page.evaluate(fn, ...a);
  const waitFor = async (fn, what, timeout = 5000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); } };
  const x = { page, ev, waitFor };
  x.nav = async (v) => { await page.click(`.nav-item[data-nav="${v}"]`); await waitFor((v) => !document.querySelector(`.view[data-view="${v}"]`).hidden, "view " + v, 3000, v); await sleep(150); };
  x.connect = async () => { await x.nav("status"); await page.click("#connectBtn"); await waitFor(() => window.__gyro?.filter?.state.config, "filter"); await waitFor(() => window.__mock.bias.log.length > 0, "0x30"); await sleep(200); await x.nav("settings"); };
  x.q = () => ev(() => window.__mock.bias.log.length);
  // 高级设置（诊断）折叠块：运动零偏位于其中
  x.open = async (on = true) => { await ev((on) => { const p = document.getElementById("advPanel"); if (p.open !== on) p.querySelector("summary").click(); }, on); await sleep(300); };
  x.ui = () => ev(() => {
    const p = document.getElementById("biasPanel"), t = (id) => document.getElementById(id).textContent, d = document.scrollingElement, fp = document.getElementById("advPanel").getBoundingClientRect();
    const v = [...document.querySelectorAll(".view")].find((x) => !x.hidden);
    const small = [...p.querySelectorAll("*")].filter((n) => n.getClientRects().length && parseFloat(getComputedStyle(n).fontSize) < 12).map((n) => n.tagName + "." + n.className);
    return { hidden: p.hidden, state: p.dataset.state, summary: t("biasSummary"), flags: t("biasFlags"), vec: t("biasVec"), note: t("biasNote"), msg: t("biasMsg"), read: document.getElementById("biasRead").disabled,
      params: [...p.querySelectorAll(".bias-p")].map((n) => [n.querySelector("i").textContent, n.querySelector("b").textContent, n.title]),
      inputs: p.querySelectorAll("input, select, textarea").length, buttons: [...p.querySelectorAll("button")].map((b) => b.textContent), small,
      fits: [...p.querySelectorAll(".bias-p, button, p")].every((n) => { const r = n.getBoundingClientRect(); return r.width === 0 || (r.left >= fp.left - .5 && r.right <= fp.right + .5); }),
      scroll: [d.scrollHeight - innerHeight, d.scrollWidth - innerWidth, v.scrollHeight - v.clientHeight, v.scrollWidth - v.clientWidth], top: t("message"), log: t("log") };
  });
  return x;
}
const noScroll = (u, what) => assert.ok(u.scroll.every((n) => n <= 0), `${what} no scroll ${JSON.stringify(u.scroll)}`);
try {
  // ---- A：支持、六轴、均衡档 ----
  {
    const s = await session();
    for (const [sel, ver] of [["script[src^='/app.js']", process.env.APP_VERSION || "20261002biashist1"], ["script[src^='/motion-bias.js']", VERSION], ["script[src^='/ui-adv.js']", VERSION], ["link[href^='/style.css']", process.env.STYLE_VERSION || "20261002biashist1"], ["script[src^='/filter-profile.js']", "20261002zaru1"]]) {
      const attr = sel.startsWith("link") ? "href" : "src"; assert.ok((await s.page.$eval(sel, (e, a) => e.getAttribute(a), attr)).endsWith(`?v=${ver}`), sel);
    }
    await s.nav("settings"); assert.equal((await s.ui()).read, true, "读取 disabled before connect");
    await s.connect();
    assert.deepEqual(await s.ev(() => window.__mock.bias.log.map((l) => l.payload.length)), [0], "one empty 0x30 on connect");
    await s.waitFor(() => window.__gyro.bias.state.ok, "ok");
    let u = await s.ui();
    noScroll(u, "1440×900 settings (高级设置 collapsed)");
    assert.equal(u.hidden, false); assert.equal(u.state, "ok"); assert.match(u.summary, /^运动零偏已开 · 零偏 0\.012\/-0\.004\/0\.001 °\/s$/);
    await sleep(1200); assert.equal(await s.q(), 1, "no auto refresh while 高级设置 collapsed");
    await s.open();
    u = await s.ui();
    assert.deepEqual(u.params.map((p) => [p[0], p[1]]), [["σ运动", "0.100"], ["竖直遗忘", "0.0001"], ["遗忘时间", "100"], ["限幅", "2.00"], ["σ静止", "0.035"], ["tauAcc（当前档）", "2.5"]]);
    assert.deepEqual(u.params.map((p) => p[2]).slice(0, 4), ["biasSigmaMotion", "biasVerticalForgettingFactor", "biasForgettingTime", "biasClip"]);
    assert.match(u.flags, /^运动零偏已开 · 静止零偏已开 · 静止：是 · 零角速保持：本档 \/ 九轴未启用$/);
    assert.match(u.vec, /^零偏 XYZ \+0\.01\d\d \/ -0\.00\d\d \/ \+0\.00\d\d °\/s · 残差 0\.0087 °\/s$/);
    assert.equal(u.note, "6 轴没有绝对航向：运动零偏不能消除航向漂移。");
    assert.equal(u.inputs, 0, "read-only: no inputs/switches"); assert.deepEqual(u.buttons, ["读取"]);
    assert.ok(u.fits, "fits in 高级设置"); assert.deepEqual(u.small, [], "no font < 12px"); assert.ok(u.scroll[1] <= 0 && u.scroll[3] <= 0, "no horizontal overflow");
    step("connect: empty 0x30 → summary; 高级设置 open → 4 params + σ静止 + tauAcc(当前档), XYZ, residual, rest/zaru, note; no inputs; fonts ≥ 12px; 1440×900 settings no scroll when collapsed");
    if (SHOTS) { await (await s.page.$("#biasPanel")).screenshot({ path: join(SHOTS, `${TAG}-bias-open.png`) }); await s.page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-settings-adv-bias.png`) }); }
    const sendLines = (u.log.match(/发送 CMD 0x30/g) || []).length;
    const n0 = await s.q(); await sleep(2000); const n1 = await s.q();
    assert.ok(n1 - n0 >= 3 && n1 - n0 <= 6, `~2 Hz while open (${n1 - n0} in 2 s)`);
    assert.equal(((await s.ui()).log.match(/发送 CMD 0x30/g) || []).length, sendLines, "auto refresh is quiet");
    await s.nav("status"); await sleep(400); const n2 = await s.q(); await sleep(1200); assert.equal(await s.q(), n2, "no refresh on other view");
    await s.nav("settings"); await s.open(false); await sleep(400); const n3 = await s.q(); await sleep(1200); assert.equal(await s.q(), n3, "no refresh when collapsed");
    step(`auto refresh: ${n1 - n0} quiet 0x30 in 2 s while 高级设置 open on 设置; none on other view / collapsed; no log lines`);
    // tauAcc 随档位：经自动设置模式门应用 静态稳定
    await s.page.click('#filterPanel label:has(input[value="2"])'); await s.page.click("#filterApply");
    await s.waitFor(() => window.__gyro.filter.state.result?.ok === true, "filter apply");
    await s.waitFor(() => !window.GyroUI.gate.busy, "gate idle", 6000);
    await s.open(); const q0 = await s.q(); await s.page.click("#biasRead"); await s.waitFor((q) => window.__mock.bias.log.length > q, "读取", 2000, q0);
    await s.waitFor(() => document.getElementById("bias_tauAcc").textContent === "4", "tauAcc follows profile");
    const q1 = await s.q(); await s.page.click("#refreshBtn"); await s.waitFor((q) => window.__mock.bias.log.length > q, "刷新状态", 2000, q1);
    await s.open(false); await sleep(300);
    const q2 = await s.q(); await s.page.click("#filterRead"); await s.waitFor((q) => window.__mock.bias.log.length > q, "filter 读取", 2000, q2);
    const cmds = await s.ev(() => window.__mock.dev.commands.map((c) => c.id));
    assert.ok(!cmds.includes(0x2f) && !cmds.includes(0x19) && !cmds.includes(0x1e), "no threshold/mode/startup writes");
    step("tauAcc follows profile (均衡 2.5 → 静态稳定 4 [mock]) after gated apply; 0x30 sent by 读取 / 刷新状态 / 姿态稳定性读取");
    // 1024×768
    await s.page.setViewport({ width: 1024, height: 768 }); await sleep(400);
    noScroll(await s.ui(), "1024×768 settings (collapsed)");
    await s.open(); u = await s.ui(); assert.ok(u.fits && u.scroll[1] <= 0 && u.scroll[3] <= 0, "1024 open: no horizontal overflow"); assert.deepEqual(u.small, []);
    if (SHOTS) await (await s.page.$("#biasPanel")).screenshot({ path: join(SHOTS, `${TAG}-1024x768-bias-open.png`) });
    step("1024×768: settings no scroll collapsed; bias block fits when 高级设置 open");
    await s.page.close();
  }
  // ---- B：旧固件 ACK 0x01 → 静默隐藏 ----
  {
    const s = await session({ bias: { supported: false } });
    await s.connect(); await s.open();
    const u = await s.ui();
    assert.equal(u.hidden, true); assert.equal(u.state, "unsupported"); assert.doesNotMatch(u.top, /失败|故障|运动零偏/);
    assert.doesNotMatch(u.log, /运动零偏/, "no extra log text"); assert.equal(await s.q(), 1);
    await s.page.click("#refreshBtn"); await sleep(600); assert.equal(await s.q(), 1, "no re-query after unsupported");
    step("old fw ACK 0x01 → block hidden silently, not re-queried (even with 高级设置 open)");
    if (SHOTS) await (await s.page.$("#advPanel")).screenshot({ path: join(SHOTS, `${TAG}-bias-oldfw-hidden.png`) });
    await s.page.close();
  }
  // ---- C：标志未开 ----
  {
    const s = await session({ bias: { motion: 0 } });
    await s.connect(); await s.open();
    const u = await s.ui();
    assert.equal(u.state, "off"); assert.equal(u.summary, "零偏估计未全部开启"); assert.match(u.flags, /运动零偏未开/); assert.match(u.msg, /未全部开启（运动 0，静止 1）/);
    assert.equal(await s.ev(() => window.__gyro.bias.state.ok), false);
    step("motion_bias_enabled=0 → not success, warning shown");
    if (SHOTS) await (await s.page.$("#biasPanel")).screenshot({ path: join(SHOTS, `${TAG}-bias-disabled.png`) });
    await s.page.close();
  }
  // ---- D：长度 / 版本 / NaN 异常 → 丢弃 ----
  for (const [bias, what] of [[{ badLength: true }, "len=44"], [{ version: 2 }, "版本或内容无效"], [{ nonfinite: true }, "版本或内容无效"]]) {
    const s = await session({ bias });
    await s.connect(); await sleep(150);
    const u = await s.ui();
    assert.equal(u.summary, "--", what); assert.equal(await s.ev(() => window.__gyro.bias.state.ok), false); assert.match(u.log, /丢弃无法识别的运动零偏帧 0x09/); assert.ok(u.log.includes(what), what);
    await s.page.close();
  }
  step("bad length 44 / version 2 / NaN → frame dropped + logged, nothing shown");
  // ---- E：零角速保持档 + zaru_hold ----
  {
    const s = await session({ filter: { profileCount: 4, active: 3, saved: 3 }, bias: { zaruHold: 1, restDetected: 1 } });
    await s.connect(); await s.open();
    const u = await s.ui();
    assert.match(u.flags, /静止：是 · 零角速保持：锁定中$/); assert.equal(u.params[5][1], "2.5");
    step("六轴 + 零角速保持档 + zaru_hold=1 → 零角速保持：锁定中");
    if (SHOTS) await (await s.page.$("#biasPanel")).screenshot({ path: join(SHOTS, `${TAG}-bias-zaru-hold.png`) });
    await s.page.close();
  }
  assert.deepEqual(errors, []); step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e.stack || e.message, errors); process.exitCode = 1; } finally { await browser.close(); }
