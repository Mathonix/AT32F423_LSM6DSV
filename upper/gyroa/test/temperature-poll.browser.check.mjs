// temp1：10 Hz 温度轮询 headless Chrome 检查（gyro1 / gyro3 通用）。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8891 SHOTS=/workspace/temp1/shots TAG=gyro1 node test/temperature-poll.browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8891", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "site";
const VERSION = process.env.VERSION || "20261002biashist1"; // app.js（biashist1） // app.js（bias1 起）
const mock = readFileSync(join(here, "mock-serial.js"), "utf8") + "\n" + readFileSync(join(here, "filter-mock.js"), "utf8");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
async function session(dev = {}) {
  const page = await browser.newPage(); await page.setViewport({ width: 1440, height: 900 });
  await page.evaluateOnNewDocument(mock);
  await page.evaluateOnNewDocument((dev) => Object.assign(window.__mock.dev, dev), dev);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url())) errors.push("requestfailed: " + r.url()); });
  assert.equal((await page.goto(BASE + "/", { waitUntil: "networkidle0" })).status(), 200);
  const ev = (fn, ...a) => page.evaluate(fn, ...a);
  const waitFor = async (fn, what, timeout = 5000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(30); } };
  const nav = await ev(() => !!document.querySelector('.nav-item[data-nav="status"]'));
  const q14 = () => ev(() => window.__mock.dev.commands.filter((c) => c.id === 0x14).length);
  const temp = () => ev(() => document.getElementById("temp").textContent);
  const logText = () => ev(() => document.getElementById("log")?.textContent || "");
  const connect = async () => {
    if (nav) { await page.click('.nav-item[data-nav="status"]'); await sleep(100); }
    await page.click("#connectBtn");
    await waitFor(() => window.__gyro.state().running && window.__gyro.tempPoller.state.replies > 2, "polling", 5000);
  };
  return { page, ev, waitFor, nav, q14, temp, logText, connect };
}
try {
  // ---- A：10 Hz、显示跟随设备、无日志、不改解析器 ----
  {
    const s = await session({ tempDrift: 0.01 });
    assert.ok((await s.page.$eval("script[src^='/app.js']", (e) => e.getAttribute("src"))).endsWith(`?v=${VERSION}`), "app.js version");
    const title = await s.ev(() => document.getElementById("temp").closest("[title]")?.title || "");
    assert.match(title, /10 Hz 轮询/);
    assert.equal(await s.temp(), "--", "-- before connect");
    await s.connect();
    const logBefore = await s.logText();
    const sendLines0 = (logBefore.match(/发送 CMD 0x14/g) || []).length, stateLines0 = (logBefore.match(/状态：融合/g) || []).length;
    const n0 = await s.q14(), t0 = Date.now(); await sleep(2000); const n1 = await s.q14(), dt = (Date.now() - t0) / 1000;
    const hz = (n1 - n0) / dt;
    assert.ok(hz >= 8 && hz <= 11, `poll rate ${hz.toFixed(1)} Hz`);
    const [shown, devTemp] = await s.ev(() => [document.getElementById("temp").textContent, window.__mock.dev.pose.temp]);
    assert.ok(Math.abs(Number(shown) - devTemp) <= 0.021, `display ${shown} tracks device ${devTemp}`);
    const logAfter = await s.logText();
    assert.equal((logAfter.match(/发送 CMD 0x14/g) || []).length, sendLines0, "no 发送 lines for polls");
    assert.equal((logAfter.match(/状态：融合/g) || []).length, stateLines0, "no 状态 lines for polled replies");
    assert.ok(await s.ev(() => window.__gyro.tempPoller.state.inFlight === null || performance.now() - window.__gyro.tempPoller.state.inFlight.at < 300), "≤1 in flight");
    step(`10 Hz poll: ${hz.toFixed(1)} Hz, #temp ${shown} °C tracks mock (drift +0.01/query), title「10 Hz 轮询」, no log lines`);
    if (SHOTS) {
      if (s.nav) { await s.page.click('.nav-item[data-nav="status"]'); await sleep(300); }
      await s.page.screenshot({ path: join(SHOTS, `${TAG}-temperature.png`) });
      await (await s.page.$("#temp")).evaluate((e) => e.closest(".metric, .dev-row").scrollIntoView());
      await (await s.page.$(s.nav ? ".dev-card" : ".metrics")).screenshot({ path: join(SHOTS, `${TAG}-temperature-card.png`) }).catch(() => {});
    }
    // 手动选择解析器后，轮询回复不得同步回去（完整 SYSINFO 处理才会）
    await s.ev(() => { const sel = document.getElementById("parseMode"); sel.value = "binary"; sel.dispatchEvent(new Event("change", { bubbles: true })); });
    await sleep(800);
    assert.equal(await s.ev(() => window.__gyro.state().parseMode), "binary", "polled SYSINFO does not syncParserToStream");
    await s.ev(() => { const sel = document.getElementById("parseMode"); sel.value = "justfloat"; sel.dispatchEvent(new Event("change", { bubbles: true })); });
    step("polled replies leave a manual parser choice alone (no syncParserToStream)");
    // 手动 0x14（刷新状态）保持完整处理：记日志
    const st = (await s.logText()).match(/状态：融合/g)?.length || 0;
    await s.ev(() => document.getElementById("refreshBtn").click());
    await s.waitFor((n) => (document.getElementById("log").textContent.match(/状态：融合/g) || []).length > n, "manual 0x14 full handling", 3000, st);
    step("manual 刷新状态 0x14 keeps full SYSINFO handling (状态 log line)");
    // 页面不可见 → 暂停；可见 → 恢复
    await s.ev(() => { Object.defineProperty(document, "visibilityState", { configurable: true, get: () => "hidden" }); });
    await sleep(250); const h0 = await s.q14(); await sleep(800); const h1 = await s.q14();
    assert.equal(h1 - h0, 0, "hidden tab: no polls");
    await s.waitFor(() => document.getElementById("temp").textContent === "--", "stale after 1 s while hidden", 2000);
    await s.ev(() => { delete document.visibilityState; });
    await s.waitFor(() => document.getElementById("temp").textContent !== "--", "resumes when visible", 2000);
    step("hidden tab pauses polling; value goes -- after 1 s; visible → resumes");
    // 应用 + 回读核对进行中 → 暂停（0x27 回复被吞，filter pending 3 s）
    if (!s.nav) { await s.page.click("#enterBtn"); await s.waitFor(() => window.__gyro.state().setting, "settings"); }
    await s.ev(() => { const d = window.__mock.dev, h = d.handle.bind(d); let once = true; d.handle = function (id, seq, pl) { if (id === 0x27 && once) { once = false; this.commands.push({ id, payload: Array.from(pl) }); return null; } return h(id, seq, pl); }; });
    if (s.nav) { await s.page.click('.nav-item[data-nav="settings"]'); await sleep(200); }
    await s.waitFor(() => !document.getElementById("filterApply").disabled, "filter editable", 4000);
    await s.page.click('#filterPanel label:has(input[value="2"])'); await s.page.click("#filterApply");
    await s.waitFor(() => !!window.__gyro.filter.state.pending, "filter pending");
    await sleep(150); const f0 = await s.q14(); await sleep(1200); const f1 = await s.q14();
    assert.equal(await s.ev(() => !!window.__gyro.filter.state.pending), true);
    assert.equal(f1 - f0, 0, "no polls while filter apply/verify pending");
    assert.equal(await s.ev(() => window.__gyro.tempPollAllowed()), false);
    await s.waitFor(() => !window.__gyro.filter.state.pending && window.__gyro.tempPollAllowed(), "filter timeout → idle", 6000);
    const g0 = await s.q14(); await sleep(600); assert.ok((await s.q14()) - g0 >= 4, "resumes after apply finished");
    step(`apply/verify pending (filter 0x27 unanswered${s.nav ? ", gyro3 auto settings gate" : ""}) pauses polling; resumes after`);
    await s.page.close();
  }
  // ---- B：丢回复 → 超时 / 降速 / -- ；恢复 ----
  {
    const s = await session();
    await s.connect();
    await s.ev(() => { window.__mock.dev.dropSysinfo = 100000; });
    await s.waitFor(() => document.getElementById("temp").textContent === "--", "-- after 1 s without replies", 2500);
    await s.waitFor(() => window.__gyro.tempPoller.state.mode === "slow", "slow after 5 timeouts", 4000);
    const n0 = await s.q14(); await sleep(2100); const slow = (await s.q14()) - n0;
    assert.ok(slow >= 1 && slow <= 3, `slow 1 Hz (${slow} in 2.1 s)`);
    assert.equal(await s.ev(() => window.__gyro.state().running), true, "link stays connected");
    await s.ev(() => { window.__mock.dev.dropSysinfo = 0; });
    await s.waitFor(() => window.__gyro.tempPoller.state.mode === "fast" && document.getElementById("temp").textContent === "31.50", "recover on good reply", 2500);
    const r0 = await s.q14(); await sleep(1000); assert.ok((await s.q14()) - r0 >= 8, "back to 10 Hz");
    step("dropped replies: -- after 1 s, 5 timeouts → 1 Hz, first good reply → 10 Hz again");
    await s.page.click("#disconnectBtn"); await sleep(300);
    const d0 = await s.q14(); await sleep(700); assert.equal((await s.q14()) - d0, 0, "no polls when disconnected");
    assert.equal(await s.temp(), "--");
    step("disconnect: polling stops, -- shown");
    await s.page.close();
  }
  // ---- C：ACK 0x01 → 停止 ----
  {
    const s = await session();
    await s.connect();
    await s.ev(() => { window.__mock.dev.sysinfoUnsupported = true; });
    await s.waitFor(() => window.__gyro.tempPoller.state.mode === "unsupported", "unsupported", 2000);
    const n0 = await s.q14(); await sleep(800); assert.equal((await s.q14()) - n0, 0, "stopped");
    const log = await s.logText();
    assert.equal((log.match(/温度轮询已停止：固件不支持/g) || []).length, 1, "one log line");
    assert.ok(!/状态查询失败/.test(log), "no 状态查询失败 toast/log from polls");
    step("ACK 0x01 on 0x14: polling stops for this connection, one log line");
    await s.page.close();
  }
  assert.deepEqual(errors, []); step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e.stack || e.message, errors); process.exitCode = 1; } finally { await browser.close(); }
