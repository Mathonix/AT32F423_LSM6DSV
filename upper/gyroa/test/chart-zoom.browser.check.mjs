// restore1 / vqfinit1：首页波形「放大」按钮 → 放大视图（ui-chart.js #chartZoom）headless Chrome 检查 —— gyro3。mock serial only — NOT real hardware.
// BASE=http://127.0.0.1:8991 SHOTS=/workspace/restore1/shots TAG=gyro3 node test/chart-zoom.browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8991", SHOTS = process.env.SHOTS || "", TAG = process.env.TAG || "gyro3";
const VERSION = process.env.VERSION || "20261002vqfinit1";
const mock = ["mock-serial.js", "filter-mock.js", "zaru-mock.js", "bias-mock.js"].map((f) => readFileSync(join(here, f), "utf8")).join("\n");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
const page = await browser.newPage(); await page.setViewport({ width: 1440, height: 900 });
await page.evaluateOnNewDocument(mock);
await page.evaluateOnNewDocument(() => Object.assign(window.__mock.dev, { extended: true, configVersion: 3, capabilities: 127, activeMode: 0, savedMode: 0 }));
page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
page.on("requestfailed", (r) => { if (!/\.glb(\?|$)/.test(r.url())) errors.push("requestfailed: " + r.url()); });
const ev = (fn, ...a) => page.evaluate(fn, ...a);
const waitFor = async (fn, what, timeout = 5000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); } };
const go = async (v) => { await page.click(`.nav-item[data-nav="${v}"]`); await waitFor((v) => !document.querySelector(`.view[data-view="${v}"]`).hidden, "view " + v, 3000, v); await sleep(150); };
// 一次 evaluate 内同时读 DOM 与放大图最近一帧的样本快照（同一帧写入），独立重算最大 / 最小
const zs = () => ev(() => {
  const st = window.GyroUI.charts.zoom.state(), el = document.getElementById("chartZoom");
  const dom = el ? [...el.querySelectorAll(".cz-ax")].map((r) => ({ axis: r.dataset.axis, max: r.querySelector('b[data-k="max"]').textContent, min: r.querySelector('b[data-k="min"]').textContent, cur: r.querySelector('b[data-k="cur"]').textContent })) : null;
  const tip = document.getElementById("czTip"), d = document.scrollingElement, panel = el?.querySelector(".cz-panel")?.getBoundingClientRect();
  const small = el ? [...el.querySelectorAll("*")].filter((n) => n.getClientRects().length && parseFloat(getComputedStyle(n).fontSize) < 12).map((n) => n.tagName + "." + n.className) : [];
  return { ...st, visible: !!el && !el.hidden, title: el?.querySelector("#czTitle")?.textContent, dom, tipVisible: !!tip && !tip.hidden, tipText: tip?.innerText || "",
    small, scroll: [d.scrollHeight - innerHeight, d.scrollWidth - innerWidth], panel: panel && [panel.left, panel.top, panel.right, panel.bottom].map(Math.round), vw: innerWidth, vh: innerHeight,
    axOverflow: el ? [...el.querySelectorAll(".cz-ax")].some((r) => r.scrollWidth > r.clientWidth + 1) : false, focus: document.activeElement?.id || document.activeElement?.tagName };
});
const fmt = (x, d) => (x >= 0 ? " " : "") + x.toFixed(d);
function checkStats(z, what) {
  assert.ok(z.snapshot && z.snapshot.seq.length > 20, `${what}: snapshot has samples (${z.snapshot?.seq.length})`);
  z.snapshot.v.forEach((arr, a) => {
    const fin = arr.filter(Number.isFinite), mx = Math.max(...fin), mn = Math.min(...fin), cur = arr[arr.length - 1];
    assert.equal(z.dom[a].max, fmt(mx, z.digits).trim(), `${what} ${z.dom[a].axis} max`); assert.equal(z.dom[a].min, fmt(mn, z.digits).trim(), `${what} ${z.dom[a].axis} min`);
    assert.equal(z.dom[a].cur, fmt(cur, z.digits).trim(), `${what} ${z.dom[a].axis} cur`);
    assert.ok(mx > mn, `${what}: waveform varies`);
  });
}
try {
  assert.equal((await page.goto(BASE + "/", { waitUntil: "networkidle0" })).status(), 200);
  assert.ok((await page.$eval("script[src^='/ui-chart.js']", (e) => e.getAttribute("src"))).endsWith(`?v=${VERSION}`));
  await page.click("#connectBtn"); await waitFor(() => window.__gyro?.state().running, "connected");
  await ev(() => { const d = window.__mock.dev; let t = 0; window.__sine = setInterval(() => { t += 0.01; Object.assign(d.pose, {
    ax: 2.0 * Math.sin(t * 6), ay: 1.2 * Math.cos(t * 4), az: 9.8 + 0.8 * Math.sin(t * 9), gx: 120 * Math.sin(t * 3), gy: 60 * Math.cos(t * 5), gz: 30 * Math.sin(t * 7) }); }, 10); });
  await go("output");
  await page.click('#outPortSeg button[data-port="1"]');
  await waitFor(() => !document.getElementById("outputPanel1").disabled && !document.querySelector('#outputFields1 input[value="3"]').disabled, "output form enabled", 5000).catch(async (e) => { console.log(JSON.stringify(await ev(() => ({ fs: document.getElementById("outputPanel1").disabled, hid: document.getElementById("outputPanel1").hidden, inp: document.querySelector(`#outputFields1 input[value="3"]`)?.outerHTML, st: window.__gyro.state(), cfg: document.getElementById("configState").textContent })))); throw e; });
  for (const v of ["3", "4", "5"]) await page.click(`#outputFields1 input[value="${v}"]`);
  await page.click("#outputFields1 .more > summary");
  for (const v of ["6", "7", "8"]) await page.click(`#outputFields1 input[value="${v}"]`);
  await page.click("#outputApplyAll");
  await waitFor(() => window.__mock.dev.savedOutputs[1].mask === 511, "USB acc+gyro output saved", 6000).catch(async (e) => { console.log(JSON.stringify(await ev(() => ({ saved: window.__mock.dev.savedOutputs, checked: [...document.querySelectorAll("#outputFields1 input")].map((i) => i.value + ":" + i.checked), msg: document.getElementById("message").textContent })))); throw e; });
  await go("status");
  await waitFor(() => !document.getElementById("accChart").hidden && !document.getElementById("gyroChart").hidden, "both charts", 4000);
  await sleep(2500); // 累积约 2.5 s 波形
  const before = await ev(() => ({ s: window.GyroUI.charts.state(), h: ["accChart", "gyroChart"].map((id) => Math.round(document.getElementById(id).getBoundingClientRect().height)), title: document.getElementById("accChart").title, zoomEl: !!document.getElementById("chartZoom"),
    cursor: getComputedStyle(document.querySelector("#accChart .plot")).cursor,
    btns: ["accZoomBtn", "gyroZoomBtn"].map((id) => { const b = document.getElementById(id), r = b.getBoundingClientRect(), lg = b.parentElement.querySelector(".legend").getBoundingClientRect(), h = b.closest(".chart-h").getBoundingClientRect();
      return { title: b.title, aria: b.getAttribute("aria-label"), icon: b.querySelector("use")?.getAttribute("href"), vis: r.width > 0 && r.height > 0, inHeader: r.top >= h.top - 3 && r.bottom <= h.bottom + 3, rightOfLegend: r.left >= lg.right, w: Math.round(r.width), hh: Math.round(h.height) }; }) }));
  assert.equal(before.title, "", "no 双击放大 tooltip on small chart"); assert.notEqual(before.cursor, "zoom-in", "no zoom-in cursor"); assert.equal(before.zoomEl, false, "overlay built lazily");
  before.btns.forEach((b, i) => { assert.equal(b.title, "放大"); assert.equal(b.aria, i ? "放大角速度波形" : "放大加速度波形"); assert.equal(b.icon, "#i-zoom-in"); assert.ok(b.vis && b.inHeader && b.rightOfLegend, "button next to legend " + JSON.stringify(b)); });
  step(`放大 buttons (magnifier, title 放大) in both chart headers next to legend (${before.btns.map((b) => b.w + "px / header " + b.hh + "px").join(", ")}); no dblclick tooltip / zoom-in cursor`);
  // 双击小图不再打开
  await page.click("#accChart .plot", { count: 2 }); await sleep(300);
  assert.equal(await ev(() => !!document.getElementById("chartZoom") && !document.getElementById("chartZoom").hidden), false, "dblclick on small chart does nothing");
  step("dblclick on small chart no longer opens the zoom");
  if (SHOTS) await page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-home-charts.png`) });

  // ---- 「放大」加速度 → 放大 ----
  await page.hover("#accZoomBtn"); await sleep(100);
  if (SHOTS) await page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-home-zoom-btn-hover.png`) });
  await page.click("#accZoomBtn");
  await waitFor(() => window.GyroUI.charts.zoom.state().open && window.GyroUI.charts.zoom.state().draws > 1, "zoom open");
  let z = await zs();
  assert.equal(z.visible, true); assert.equal(z.group, "acc"); assert.equal(z.title, "加速度g"); assert.equal(z.digits, 3);
  assert.deepEqual(z.dom.map((d) => d.axis), ["X", "Y", "Z"]); checkStats(z, "acc");
  assert.ok(Math.abs(Number(z.dom[2].max) - 1.0) < 0.2 && Number(z.dom[0].max) > 0.1 && Number(z.dom[0].min) < -0.1, "acc g plausible " + JSON.stringify(z.dom));
  assert.deepEqual(z.small, [], "no font < 12px"); assert.ok(z.scroll.every((n) => n <= 0), "no page scroll");
  assert.ok(z.panel[0] >= 0 && z.panel[1] >= 0 && z.panel[2] <= z.vw && z.panel[3] <= z.vh, "panel inside viewport " + z.panel);
  assert.equal(z.axOverflow, false); assert.equal(z.focus, "czClose", "focus on 关闭");
  step(`放大 button 加速度 → overlay (panel ${z.panel}), X/Y/Z 最大/最小/当前 == independent min/max of the ${z.snapshot.seq.length} samples drawn; fonts ≥ 12px`);
  // 实时更新：样本号前进、统计仍正确
  const s0 = z.snapshot.seq.at(-1), d0 = z.draws; await sleep(600); z = await zs();
  assert.ok(z.snapshot.seq.at(-1) > s0 && z.draws >= d0 + 2, `live updating seq ${s0}->${z.snapshot.seq.at(-1)} draws ${d0}->${z.draws}`); checkStats(z, "acc live");
  const small1 = await ev(() => window.GyroUI.charts.state()); assert.ok(small1.acc.draws > before.s.acc.draws, "small charts keep drawing");
  step(`live: newest sample #${s0} → #${z.snapshot.seq.at(-1)}, ${z.draws - d0} redraws in 0.6 s; small charts keep drawing`);
  // 悬停：十字线 + 提示
  const box = await (await page.$("#czCanvas")).boundingBox();
  await page.mouse.move(box.x + box.width * 0.88, box.y + box.height * 0.45);
  await waitFor(() => window.GyroUI.charts.zoom.state().hit && !document.getElementById("czTip").hidden, "tooltip");
  z = await zs();
  assert.ok(z.tipVisible); assert.match(z.tipText, new RegExp(`^−\\d+\\.\\d\\d s · 样本 #${z.hit.seq}`));
  const k = z.snapshot.seq.indexOf(z.hit.seq); assert.ok(k >= 0, "hovered sample is in drawn data");
  z.hit.values.forEach((v, a) => { assert.equal(v, z.snapshot.v[a][k], "hit value == buffer"); assert.ok(z.tipText.includes(`${["X", "Y", "Z"][a]}\n${fmt(v, 3).trim()}`) || z.tipText.includes(fmt(v, 3).trim()), "tip shows " + a); });
  assert.match(z.tipText, /X[\s\S]*Y[\s\S]*Z/); assert.match(z.tipText, /g/);
  const px = await ev(() => { const c = document.getElementById("czCanvas"), h = window.GyroUI.charts.zoom.state(); return c.width; });
  assert.ok(px > 800, "zoom canvas large");
  if (SHOTS) await page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-zoom-acc-hover.png`) });
  step(`hover → crosshair + tooltip "${z.tipText.split("\n")[0]}" with X/Y/Z == buffer sample #${z.hit.seq}`);
  await page.mouse.move(box.x - 40, box.y - 40); await sleep(150); assert.equal((await zs()).tipVisible, false, "tooltip hides on leave");
  // ---- 暂停 / 继续 ----
  assert.equal(await ev(() => document.getElementById("czPause").textContent), "暂停");
  await page.click("#czPause"); await sleep(120);
  let p1 = await zs(); assert.equal(p1.paused, true); checkStats(p1, "paused");
  assert.deepEqual(await ev(() => [document.getElementById("czPause").textContent, document.getElementById("czPause").getAttribute("aria-pressed"), document.getElementById("chartZoom").dataset.paused, /已暂停/.test(document.querySelector(".cz-hint").textContent)]), ["继续", "true", "true", true]);
  const smallA = await ev(() => window.GyroUI.charts.state().acc.draws);
  await sleep(800); let p2 = await zs();
  assert.deepEqual(p2.snapshot.seq, p1.snapshot.seq, "frozen samples unchanged"); assert.deepEqual(p2.dom, p1.dom, "frozen max/min/cur unchanged"); checkStats(p2, "paused 2");
  assert.ok(p2.draws > p1.draws, "zoom still redraws (frozen data)");
  assert.ok((await ev(() => window.GyroUI.charts.state().acc.draws)) > smallA, "small chart keeps updating while paused");
  const liveSeq = await ev(() => window.__gyro && window.GyroUI.charts.state().acc.count); assert.ok(liveSeq > 0);
  // 悬停冻结曲线：同一位置两次读数相同
  await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.5);
  await waitFor(() => window.GyroUI.charts.zoom.state().hit && !document.getElementById("czTip").hidden, "paused tooltip");
  const h1 = await zs(); await sleep(600); const h2 = await zs();
  assert.equal(h2.hit.seq, h1.hit.seq, "hover sample stable"); assert.deepEqual(h2.hit.values, h1.hit.values); assert.equal(h2.tipText, h1.tipText, "tooltip text stable (age frozen too)");
  const kk = p1.snapshot.seq.indexOf(h1.hit.seq); assert.ok(kk >= 0, "hovered sample in frozen data"); h1.hit.values.forEach((v, a) => assert.equal(v, p1.snapshot.v[a][kk]));
  if (SHOTS) await page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-zoom-acc-paused.png`) });
  step(`暂停: zoom frozen (${p1.snapshot.seq.length} samples, last #${p1.snapshot.seq.at(-1)}), max/min over frozen data, hover #${h1.hit.seq} stable; small charts keep updating`);
  await page.mouse.move(box.x - 40, box.y - 40); await sleep(100);
  await page.click("#czPause"); await sleep(400); let p3 = await zs();
  assert.equal(p3.paused, false); assert.ok(p3.snapshot.seq.at(-1) > p1.snapshot.seq.at(-1), "continue → live again"); checkStats(p3, "resumed");
  assert.equal(await ev(() => document.getElementById("czPause").textContent), "暂停");
  // 暂停后关闭 → 再打开为实时
  await page.click("#czPause"); await sleep(100); assert.equal((await zs()).paused, true);
  await page.click("#czClose"); await sleep(150); await page.click("#accZoomBtn"); await sleep(300);
  p3 = await zs(); assert.equal(p3.paused, false, "reopen is live"); assert.equal(await ev(() => document.getElementById("czPause").textContent), "暂停");
  const s1 = p3.snapshot.seq.at(-1); await sleep(400); assert.ok((await zs()).snapshot.seq.at(-1) > s1, "live after reopen");
  step("继续 resumes live (newest sample advances); closing while paused resets to live on reopen");
  // 关闭：按钮
  await page.click("#czClose"); await sleep(150); z = await zs(); assert.equal(z.visible, false); assert.equal(z.open, false);
  // 角速度：放大按钮 → Esc 关闭
  await page.click("#gyroZoomBtn"); await waitFor(() => window.GyroUI.charts.zoom.state().draws > 0 && window.GyroUI.charts.zoom.state().group === "gyro", "gyro zoom");
  await sleep(200); z = await zs(); assert.equal(z.title, "角速度dps"); assert.equal(z.digits, 2); checkStats(z, "gyro");
  assert.ok(Number(z.dom[0].max) > 20 && Number(z.dom[0].min) < -20, "gyro dps plausible");
  const gbox = await (await page.$("#czCanvas")).boundingBox(); await page.mouse.move(gbox.x + gbox.width * 0.8, gbox.y + gbox.height * 0.3); await sleep(250);
  if (SHOTS) await page.screenshot({ path: join(SHOTS, `${TAG}-1440x900-zoom-gyro.png`) });
  await page.keyboard.press("Escape"); await sleep(150); assert.equal((await zs()).visible, false, "Esc closes");
  // 双击放大图不再关闭；点背景关闭
  await page.click("#gyroZoomBtn"); await waitFor(() => window.GyroUI.charts.zoom.state().open, "reopen");
  await page.click("#czCanvas", { count: 2 }); await sleep(150); assert.equal((await zs()).visible, true, "dblclick on zoom no longer closes");
  assert.doesNotMatch(await ev(() => document.querySelector(".cz-hint").textContent), /双击/);
  await page.click("#czClose"); await sleep(150); assert.equal((await zs()).visible, false);
  await page.click("#accZoomBtn"); await waitFor(() => window.GyroUI.charts.zoom.state().open, "reopen 2");
  await page.mouse.click(8, 8); await sleep(150); assert.equal((await zs()).visible, false, "backdrop click closes");
  step("close: 关闭 button / Esc / backdrop (dblclick no longer closes, hint has no 双击); 角速度 zoom (dps, 2 decimals) stats correct");
  // 离开首页 → 关闭；小图尺寸不变
  await page.click("#accZoomBtn"); await waitFor(() => window.GyroUI.charts.zoom.state().open, "reopen 3");
  await ev(() => { location.hash = "#/output"; }); await sleep(400); assert.equal((await zs()).visible, false, "navigating away closes");
  await go("status"); await sleep(300);
  const after = await ev(() => ["accChart", "gyroChart"].map((id) => Math.round(document.getElementById(id).getBoundingClientRect().height)));
  assert.deepEqual(after, before.h, "small chart layout unchanged");
  step(`leaving home closes the zoom; small charts unchanged (${after.join("/")} px)`);
  // ---- 1024×768 ----
  await page.setViewport({ width: 1024, height: 768 }); await sleep(500);
  await page.click("#gyroZoomBtn"); await waitFor(() => window.GyroUI.charts.zoom.state().open, "1024 zoom"); await sleep(300);
  const b2 = await (await page.$("#czCanvas")).boundingBox(); await page.mouse.move(b2.x + b2.width * 0.97, b2.y + b2.height * 0.5); await sleep(250);
  z = await zs(); checkStats(z, "1024");
  assert.ok(z.panel[0] >= 0 && z.panel[2] <= z.vw && z.panel[3] <= z.vh, "1024 panel inside viewport " + z.panel); assert.equal(z.axOverflow, false, "1024 stats fit");
  assert.deepEqual(z.small, []); assert.ok(z.scroll.every((n) => n <= 0));
  const tipBox = await ev(() => { const t = document.getElementById("czTip").getBoundingClientRect(), p = document.querySelector(".cz-plot").getBoundingClientRect(); return [t.left >= p.left - 1, t.right <= p.right + 1, !document.getElementById("czTip").hidden]; });
  assert.deepEqual(tipBox, [true, true, true], "tooltip flips inside plot at right edge");
  if (SHOTS) await page.screenshot({ path: join(SHOTS, `${TAG}-1024x768-zoom-gyro-hover.png`) });
  await page.keyboard.press("Escape");
  step("1024×768: zoom fits, stats rows fit, tooltip flips left at right edge, fonts ≥ 12px");
  await ev(() => clearInterval(window.__sine));
  assert.deepEqual(errors, []); step("no JS errors / failed requests");
  console.log(`PASS ${ok} steps (mock serial only — NOT real hardware)`);
} catch (e) { console.log("FAIL", e.stack || e.message, errors); process.exitCode = 1; } finally { await browser.close(); }
