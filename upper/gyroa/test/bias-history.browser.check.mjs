// biashist1：启动零偏历史（只读 0x33 → 0x34，校准页左栏六面校准下方）headless Chrome 检查 —— gyro3。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8995 SHOTS=/workspace/biashist1/shots TAG=gyro3 node test/bias-history.browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8995", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "gyro3";
const VERSION = process.env.VERSION || "20261002biashist1";
const mock = ["mock-serial.js", "filter-mock.js", "zaru-mock.js", "bias-mock.js", "vqf-mock.js", "fwver-mock.js", "bh-mock.js"].map((f) => readFileSync(join(here, f), "utf8")).join("\n");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
const WRITES = [0x11, 0x12, 0x13, 0x15, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x22, 0x25, 0x27, 0x2a, 0x2b, 0x2d, 0x2f]; // 写入 / 设置模式 / 动作命令（app.js CMD）
async function session({ vw = 1440, vh = 900, bh = {} } = {}) {
  const page = await browser.newPage(); await page.setViewport({ width: vw, height: vh });
  await page.evaluateOnNewDocument(mock);
  await page.evaluateOnNewDocument((bh) => { Object.assign(window.__mock.dev, { extended: true, configVersion: 3, capabilities: 127, activeMode: 0, savedMode: 0 }); Object.assign(window.__mock.bh, bh); }, bh);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url())) errors.push("requestfailed: " + r.url()); });
  assert.ok([200, 304].includes((await page.goto(BASE + "/", { waitUntil: "networkidle0" })).status()));
  const ev = (fn, ...a) => page.evaluate(fn, ...a);
  const waitFor = async (fn, what, timeout = 6000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); } };
  const nav = async (v) => { await page.click(`.nav-item[data-nav="${v}"]`); await waitFor((v) => !document.querySelector(`.view[data-view="${v}"]`).hidden, "view " + v, 3000, v); await sleep(150); };
  const connect = async () => { await nav("status"); await page.click("#connectBtn"); await waitFor(() => window.__gyro?.state().running && window.__mock.bh.log.length > 0, "connect + 0x33"); };
  const done = () => waitFor(() => !window.__gyro.biasHist.state.reading, "read done", 8000);
  const card = () => ev(() => { const p = document.getElementById("bhPanel"), rows = [...document.querySelectorAll("#bhBody tr")];
    return { hidden: p.hidden || getComputedStyle(p).display === "none", meta: document.getElementById("bhMeta").textContent, msg: document.getElementById("bhMsg").textContent, result: p.dataset.result,
      corrupt: !document.getElementById("bhCorrupt").hidden, empty: !document.getElementById("bhEmpty").hidden, rows: rows.length, first: rows[0] && [...rows[0].cells].map((x) => x.textContent), firstNew: rows[0]?.classList.contains("bh-new"),
      last: rows.at(-1) && [...rows.at(-1).cells].map((x) => x.textContent), btn: document.getElementById("bhRead").textContent, btnDisabled: document.getElementById("bhRead").disabled,
      toasts: [...document.querySelectorAll("#toasts > *")].map((n) => n.textContent) }; });
  const layout = () => ev(() => { const d = document.scrollingElement, v = [...document.querySelectorAll(".view")].find((x) => !x.hidden);
    const small = [...v.querySelectorAll("*")].filter((n) => n.getClientRects().length && n.textContent.trim() && parseFloat(getComputedStyle(n).fontSize) < 12).length;
    const r = (el) => { const b = el.getBoundingClientRect(); return [Math.round(b.top), Math.round(b.bottom), Math.round(b.width)]; };
    const w = document.getElementById("bhWrap");
    return { scroll: [d.scrollHeight - innerHeight, d.scrollWidth - innerWidth, v.scrollHeight - v.clientHeight, v.scrollWidth - v.clientWidth], small,
      acc: r(document.querySelector(".calib-main>.card")), bh: r(document.getElementById("bhPanel")), vqf: r(document.getElementById("vqfPanel")), wrap: [w.clientHeight, w.scrollHeight, w.scrollWidth - w.clientWidth], vh: innerHeight }; });
  const cmds = () => ev(() => window.__mock.dev.commands.map((c) => c.id));
  return { page, ev, waitFor, nav, connect, done, card, layout, cmds };
}
const shot = async (s, n) => { if (SHOTS) await s.page.screenshot({ path: join(SHOTS, `${TAG}-${n}.png`) }); };
const noErrToast = (c) => assert.ok(!c.toasts.some((t) => /失败|故障|无效|未收到|0x33|0x34/.test(t)), "no error toast: " + c.toasts);
try {
  {
    const s = await session();
    for (const [sel, attr] of [["script[src^='/app.js']", "src"], ["link[href^='/style.css']", "href"], ["script[src^='/bias-history.js']", "src"]])
      assert.ok((await s.page.$eval(sel, (e, a) => e.getAttribute(a), attr)).endsWith(`?v=${VERSION}`), sel);
    await s.nav("calib"); let c = await s.card();
    assert.equal(c.hidden, false); assert.equal(c.meta, "--"); assert.equal(c.btnDisabled, true); assert.equal(c.rows, 0);
    step("before connect: 启动零偏历史 card visible under 六面校准, 读取 disabled, no rows");
    await s.connect(); await s.done();
    const log = await s.ev(() => window.__mock.bh.log.map((l) => l.payload));
    assert.equal(log.length, 12, "36 条 → 12 frames"); assert.deepEqual(log[0], [], "first request empty payload"); assert.deepEqual(log.slice(1).map((p) => p[0] | (p[1] << 8)), [3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33]);
    assert.ok(log.slice(1).every((p) => p.length === 2), "u16 LE offsets");
    const before = await s.cmds(); assert.ok(!before.some((id) => WRITES.includes(id)), "no write / settings commands during connect+read: " + before.map((x) => x.toString(16)));
    await s.nav("calib"); c = await s.card();
    assert.equal(c.meta, "36 条 · 50 条槽 · 序号 129"); assert.equal(c.rows, 36); assert.equal(c.result, "ok"); assert.match(c.msg, /已读取 36 条（从旧到新），sequence 129/);
    const e35 = await s.ev(() => window.__mock.bh.entry(35)), e0 = await s.ev(() => window.__mock.bh.entry(0)), f3 = (x) => (x >= 0 ? "+" : "") + x.toFixed(3);
    assert.deepEqual(c.first, ["35", ...e35.slice(0, 3).map(f3), e35[3].toFixed(1)], "newest first"); assert.ok(c.firstNew, "newest highlighted");
    assert.deepEqual(c.last, ["0", ...e0.slice(0, 3).map(f3), e0[3].toFixed(1)], "oldest last");
    assert.equal(c.corrupt, false); assert.equal(c.empty, false); noErrToast(c);
    assert.doesNotMatch(await s.ev(() => document.getElementById("log").textContent), /0x34|丢弃|忽略/);
    assert.equal(await s.ev(() => window.__gyro.state().stats.badLengthFrames), 0);
    step("connect → 0x33 empty + offsets 3…33 (12 frames, u16 LE) → 36 rows newest-first, meta 36 条 · 50 条槽 · 序号 129, no writes, no toast/log noise");
    let L = await s.layout();
    assert.ok(L.scroll.every((n) => n <= 0), "1440 no page scroll " + L.scroll); assert.equal(L.small, 0, "fonts ≥ 12");
    assert.ok(L.bh[1] <= L.vh, "card fits " + JSON.stringify(L)); assert.ok(L.bh[0] > L.acc[1], "below 六面校准"); assert.equal(L.bh[2], L.acc[2], "same width as 六面校准");
    assert.ok(L.wrap[0] >= 84 && L.wrap[1] > L.wrap[0], "table scrolls inside card " + L.wrap); assert.ok(L.wrap[2] <= 0, "no horizontal scroll in table");
    await shot(s, "1440x900-calib-biashist");
    step(`1440×900: card ${L.bh.join("/")} under 六面校准 (${L.acc.join("/")}), table ${L.wrap[0]}px scrolls internally, page no scroll, fonts ≥ 12 px`);
    // 手动重读 + 中途 sequence 变化 → 整表重读
    await s.ev(() => { window.__mock.bh.bumpAt = 12; window.__mock.bh.bumped = false; window.__mock.bh.count = 37; window.__mock.bh.log.length = 0; });
    await s.page.click("#bhRead"); await s.done(); c = await s.card();
    const offs = await s.ev(() => window.__mock.bh.log.map((l) => (l.payload.length ? l.payload[0] : "e")));
    assert.deepEqual(offs.slice(0, 6), ["e", 3, 6, 9, 12, 0], "restart from 0 after sequence change"); assert.equal(c.rows, 37); assert.equal(c.meta, "37 条 · 50 条槽 · 序号 130");
    assert.match(await s.ev(() => document.getElementById("log").textContent), /sequence 129 → 130，中间又保存过，整表重读/);
    step("读取 button re-reads; sequence 129→130 mid-read → whole table re-read from offset 0 (37 rows, 序号 130)");
    // 静置初始化采集期间也允许查
    await s.page.click("#vqfStart"); await s.waitFor(() => window.__gyro.vqf.active(), "vqf active");
    c = await s.card(); assert.equal(c.btnDisabled, false, "读取 not locked during collection");
    await s.ev(() => { window.__mock.bh.log.length = 0; }); await s.page.click("#bhRead"); await s.done();
    assert.equal(await s.ev(() => window.__mock.bh.log.length), 13); assert.equal((await s.card()).result, "ok");
    await s.page.setViewport({ width: 1024, height: 768 }); await sleep(400);
    L = await s.layout(); assert.ok(L.scroll.every((n) => n <= 0), "1024 collecting no scroll " + L.scroll); assert.equal(L.small, 0); assert.ok(L.bh[1] <= L.vh && L.vqf[1] <= L.vh, JSON.stringify(L));
    assert.ok(L.wrap[0] >= 84, "table still usable " + L.wrap); assert.ok(L.wrap[2] <= 0, "no horizontal scroll 1024");
    await shot(s, "1024x768-calib-biashist-collecting");
    await s.ev(() => { if (window.__gyro.vqf.active()) document.getElementById("vqfCancel").click(); }); await s.waitFor(() => !window.__gyro.vqf.active(), "cancel / finished", 20000); await sleep(200);
    L = await s.layout(); assert.ok(L.scroll.every((n) => n <= 0)); await shot(s, "1024x768-calib-biashist");
    step(`0x33 allowed during 静置初始化 collection; 1024×768 idle + collecting no scroll, table ${L.wrap[0]}px, fonts ≥ 12 px`);
    // 断开 → 重连：清空再读
    await s.page.click("#disconnectBtn"); await s.waitFor(() => !window.__gyro.state().running, "disc"); await sleep(400);
    c = await s.card(); assert.equal(c.btnDisabled, true, "读取 disabled while disconnected");
    await s.ev(() => { window.__mock.bh.count = 5; window.__mock.bh.log.length = 0; document.getElementById("connectBtn").click(); });
    await s.waitFor(() => window.__gyro.biasHist.state.result === "ok", "reread on reconnect"); c = await s.card();
    assert.equal(c.rows, 5); assert.equal(await s.ev(() => window.__mock.bh.log.length), 2);
    step("disconnect disables 读取; reconnect resets and re-reads (5 条 → 2 frames)");
    await s.page.close();
  }
  for (const [name, bh, check] of [
    ["old fw ACK 0x01", { supported: false }, async (s, c) => { assert.equal(c.hidden, true); assert.equal(await s.ev(() => window.__mock.bh.log.length), 1, "asked once"); }],
    ["no reply 3 s", { silent: true }, async (s, c) => { assert.equal(c.hidden, true); assert.match(await s.ev(() => document.getElementById("log").textContent), /3 s 内无 0x34/); }],
    ["count 0", { count: 0 }, async (s, c) => { assert.equal(c.hidden, false); assert.equal(c.empty, true); assert.equal(c.rows, 0); assert.equal(c.meta, "0 条 · 无记录 · 序号 129"); assert.match(c.msg, /还没有启动零偏记录/); }],
    ["old 15-slot + corrupt", { count: 4, recordVersion: 2, corrupt: 1 }, async (s, c) => { assert.equal(c.rows, 4); assert.equal(c.corrupt, true); assert.equal(c.meta, "4 条 · 旧格式（15 条槽） · 序号 129"); }],
    ["page drop", { dropAt: 9 }, async (s, c) => { assert.equal(c.result, "unconfirmed"); assert.match(c.msg, /未读完：第 9 条起的一页没有回复（已读 9 \/ 36）/); assert.equal(c.rows, 0); }],
    ["59 B frame", { bad: "short", badAt: 3 }, async (s, c) => { assert.equal(c.result, "fail"); assert.match(c.msg, /无法识别的历史帧/); }],
    ["version 2", { bad: "version" }, async (s, c) => { assert.equal(c.hidden, true, "never confirmed → hidden after 3 s"); }],
    ["NaN entry", { bad: "nan", badAt: 6 }, async (s, c) => { assert.equal(c.result, "fail"); assert.equal(c.rows, 0); }],
    ["50 条", { count: 50 }, async (s, c) => { assert.equal(c.rows, 50); assert.equal(await s.ev(() => window.__mock.bh.log.length), 17); }],
  ]) {
    const s = await session({ bh }); await s.connect(); await s.done(); await sleep(name === "no reply 3 s" || name === "version 2" ? 3300 : 300);
    await s.nav("calib"); const c = await s.card(); await check(s, c); noErrToast(c);
    assert.ok(!(await s.cmds()).some((id) => WRITES.includes(id)), "read-only");
    const L = await s.layout(); assert.ok(L.scroll.every((n) => n <= 0), name + " no scroll " + L.scroll);
    if (name === "old fw ACK 0x01") await shot(s, "1440x900-calib-biashist-unsupported");
    if (name === "old 15-slot + corrupt") await shot(s, "1440x900-calib-biashist-corrupt");
    step(`${name}: ${c.hidden ? "card hidden" : c.result + " · " + (c.msg || c.meta)}`);
    await s.page.close();
  }
  assert.deepEqual(errors, []); step("no page / console errors, no failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e); process.exitCode = 1; }
finally { await browser.close(); }
