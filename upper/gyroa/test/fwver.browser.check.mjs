// fwver1：应用固件版本（0x23 → 0x32，升级页「当前版本」）+ 去掉「其他校准」卡 headless Chrome 检查 —— gyro3。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8833 SHOTS=/workspace/fwver1/shots TAG=gyro3 node test/fwver.browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8833", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "gyro3";
const VERSION = process.env.VERSION || "20261004startup1"; // ui-shell.js
const BH = process.env.BH_VERSION || "20261004startup1"; // biashist1：app.js / style.css
const mock = ["mock-serial.js", "filter-mock.js", "zaru-mock.js", "bias-mock.js", "vqf-mock.js", "fwver-mock.js"].map((f) => readFileSync(join(here, f), "utf8")).join("\n");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--disable-background-timer-throttling", "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
async function session({ vw = 1440, vh = 900, fwver = {}, vqf = {} } = {}) {
  const page = await browser.newPage(); await page.setCacheEnabled(false); await page.setViewport({ width: vw, height: vh });
  await page.evaluateOnNewDocument(mock);
  await page.evaluateOnNewDocument((fwver, vqf) => { Object.assign(window.__mock.dev, { extended: true, configVersion: 3, capabilities: 127, activeMode: 0, savedMode: 0 }); Object.assign(window.__mock.fwver, fwver); Object.assign(window.__mock.vqf, vqf); }, fwver, vqf);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url())) errors.push("requestfailed: " + r.url()); });
  assert.equal((await page.goto(BASE + "/", { waitUntil: "networkidle0" })).status(), 200);
  const ev = (fn, ...a) => page.evaluate(fn, ...a);
  const waitFor = async (fn, what, timeout = 6000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); } };
  const nav = async (v) => { await page.click(`.nav-item[data-nav="${v}"]`); await waitFor((v) => !document.querySelector(`.view[data-view="${v}"]`).hidden, "view " + v, 3000, v); await sleep(150); };
  const connect = async () => { await nav("status"); await page.click("#connectBtn"); await waitFor(() => window.__gyro?.state().running && window.__mock.fwver.log.length > 0, "connect + 0x23"); await sleep(250); };
  const ver = () => ev(() => { const b = document.getElementById("fwVersion"); return { text: b.textContent, title: b.title }; });
  const layout = () => ev(() => { const d = document.scrollingElement, v = [...document.querySelectorAll(".view")].find((x) => !x.hidden);
    const small = [...v.querySelectorAll("*")].filter((n) => n.getClientRects().length && n.textContent.trim() && parseFloat(getComputedStyle(n).fontSize) < 12).length;
    const p = document.getElementById("vqfPanel").getBoundingClientRect(), a = document.querySelector(".calib-main>.card").getBoundingClientRect();
    return { scroll: [d.scrollHeight - innerHeight, d.scrollWidth - innerWidth, v.scrollHeight - v.clientHeight, v.scrollWidth - v.clientWidth], small, vqf: [Math.round(p.top), Math.round(p.bottom), Math.round(p.width)], acc: [Math.round(a.top), Math.round(a.width)], vh: innerHeight }; });
  return { page, ev, waitFor, nav, connect, ver, layout };
}
const shot = async (s, n) => { if (SHOTS) await s.page.screenshot({ path: join(SHOTS, `${TAG}-${n}.png`) }); };
try {
  {
    const s = await session();
    for (const [sel, attr, ver] of [["script[src^='/app.js']", "src", BH], ["script[src^='/ui-shell.js']", "src", VERSION], ["link[href^='/style.css']", "href", BH]])
      assert.ok((await s.page.$eval(sel, (e, a) => e.getAttribute(a), attr)).endsWith(`?v=${ver}`), sel);
    await s.nav("upgrade"); assert.deepEqual(await s.ver(), { text: "--", title: "未读到应用版本" }); step("before connect: 当前版本 --");
    await s.connect();
    const log = await s.ev(() => window.__mock.fwver.log); assert.equal(log.length, 1, "0x23 sent once on connect"); assert.deepEqual(log[0].payload, [], "empty payload");
    await s.nav("upgrade"); await s.waitFor(() => document.getElementById("fwVersion").textContent !== "--", "version shown");
    assert.deepEqual(await s.ver(), { text: "20261002b", title: "应用固件版本（0x23 回读）" });
    assert.match(await s.ev(() => document.getElementById("log").textContent), /应用固件版本：20261002b/);
    assert.equal(await s.ev(() => window.__gyro.state().stats.badLengthFrames), 0);
    await shot(s, "1440x900-upgrade-fwver");
    step("connect → one empty 0x23 → 0x32 (16 B) → 升级页 当前版本 20261002b");
    // 其他校准卡已移除；VQF 卡占右栏
    await s.nav("calib"); await s.waitFor(() => window.__gyro.vqf.state.supported === true, "vqf");
    assert.deepEqual(await s.ev(() => ["zeroBtn", "calBtn", "gyro60Btn"].map((id) => !!document.getElementById(id))), [false, false, false]);
    assert.doesNotMatch(await s.ev(() => document.querySelector('.view[data-view="calib"]').textContent), /其他校准|航向归零|陀螺仪零偏|运行时陀螺标定/);
    let L = await s.layout(); assert.ok(L.scroll.every((n) => n <= 0), "1440 no scroll " + L.scroll); assert.equal(L.small, 0); assert.equal(L.vqf[0], L.acc[0], "VQF card top-aligned with 六面校准 " + JSON.stringify(L));
    await shot(s, "1440x900-calib");
    step(`其他校准 card + 航向归零/陀螺仪零偏/运行时陀螺标定 rows gone; VQF card top of right column (${L.vqf.join("/")}), 1440×900 no scroll`);
    // 静置采集期间仍可查询版本
    await s.page.click("#vqfStart"); await s.waitFor(() => window.__gyro.vqf.active(), "vqf active");
    await s.ev(() => { window.__mock.fwver.text = "20261002c"; return queryFwVersion(); });
    await s.waitFor(() => document.getElementById("fwVersion").textContent === "20261002c", "requery during collection");
    step("0x23 allowed during 静置初始化 collection (version re-read)");
    await s.page.setViewport({ width: 1024, height: 768 }); await sleep(400);
    L = await s.layout(); assert.ok(L.scroll.every((n) => n <= 0), "1024 collecting no scroll " + L.scroll); assert.equal(L.small, 0); assert.ok(L.vqf[1] <= L.vh);
    await shot(s, "1024x768-calib-collecting");
    await s.page.click("#vqfCancel"); await s.waitFor(() => !window.__gyro.vqf.active(), "cancel"); await sleep(200);
    L = await s.layout(); assert.ok(L.scroll.every((n) => n <= 0)); await shot(s, "1024x768-calib");
    await s.nav("upgrade"); await shot(s, "1024x768-upgrade-fwver");
    step("1024×768: calib (idle + collecting) no scroll, fonts ≥ 12 px; upgrade page");
    // 断开 → 重连：先清空再重新查询
    await s.page.click("#disconnectBtn"); await s.waitFor(() => !window.__gyro.state().running, "disc");
    await sleep(400); await s.ev(() => { window.__mock.fwver.text = "20261003a"; document.getElementById("connectBtn").click(); });
    await s.waitFor(() => document.getElementById("fwVersion").textContent === "20261003a", "reconnect version");
    assert.equal(await s.ev(() => window.__mock.fwver.log.length), 3);
    step("reconnect → version reset and re-queried (20261003a)");
    await s.page.close();
  }
  for (const [name, fwver, title] of [["old fw ACK 0x01", { supported: false }, "当前固件不上报版本（旧固件，ACK 0x01）"], ["no reply 3 s", { silent: true }, "未读到应用版本"],
    ["format≠1", { corrupt: "format" }, "未读到应用版本"], ["no NUL", { corrupt: "len" }, "未读到应用版本"], ["non-ASCII", { corrupt: "ascii" }, "未读到应用版本"], ["15 B", { corrupt: "short" }, "未读到应用版本"]]) {
    const s = await session({ fwver });
    await s.connect(); await sleep(name === "no reply 3 s" ? 3300 : 400); await s.nav("upgrade");
    assert.deepEqual(await s.ver(), { text: "--", title }, name);
    if (name === "no reply 3 s") assert.match(await s.ev(() => document.getElementById("log").textContent), /未收到应用版本（0x32），当前版本保持 --/);
    if (name === "old fw ACK 0x01") await shot(s, "1440x900-upgrade-oldfw");
    assert.equal(await s.ev(() => window.__gyro.state().running), true, "connection unaffected");
    step(`${name} → 当前版本 stays --`);
    await s.page.close();
  }
  assert.deepEqual(errors, []); step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e.stack || e.message, errors); process.exitCode = 1; } finally { await browser.close(); }
