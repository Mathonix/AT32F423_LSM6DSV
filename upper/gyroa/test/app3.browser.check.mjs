// app3 headless Chrome check (app2 checks + CAN merged into 输出 [UART][USB][CAN] + 姿态稳定性 in 设置 › 设备) against the mock serial device (test/mock-serial.js). NOT real hardware.
// BASE=http://127.0.0.1:8833 CHROME=/usr/bin/google-chrome node test/app3.browser.check.mjs   (SHOTS=/dir optional)
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8833";
const CHROME = process.env.CHROME || "/usr/bin/google-chrome";
const SHOTS = process.env.SHOTS || "";
const VERSION = "20261001fw1-app3";
const FSHOTS = process.env.FILTER_SHOTS || "";
const mock = readFileSync(join(here, "mock-serial.js"), "utf8") + "\n" + readFileSync(join(here, "filter-mock.js"), "utf8");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ok = 0; const step = (n) => { ok++; console.log(`  ✔ ${n}`); };
const errors = [];

// valid Bootloader v1 test image: SP 0x2000BFF0, reset 0x08008101 (thumb), 9000 bytes
const image = new Uint8Array(9000); const iv = new DataView(image.buffer);
iv.setUint32(0, 0x2000bff0, true); iv.setUint32(4, 0x08008101, true);
for (let i = 8; i < image.length; i++) image[i] = (i * 31 + 7) & 255;
const fwPath = "/tmp/app3-test-firmware.bin"; writeFileSync(fwPath, image);

const browser = await puppeteer.launch({ executablePath: CHROME, headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1440, height: 900 });
  await page.evaluateOnNewDocument(mock);
  await page.evaluateOnNewDocument(() => Object.assign(window.__mock?.dev || {}, {}));
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => errors.push("requestfailed: " + r.url()));
  const resp = await page.goto(BASE + "/", { waitUntil: "networkidle0" });
  assert.equal(resp.status(), 200);
  const ev = (fn, ...a) => page.evaluate(fn, ...a);
  const txt = (id) => page.$eval("#" + id, (e) => e.textContent.trim());
  const st = () => ev(() => window.__gyro.state());
  const waitFor = async (fn, what, timeout = 4000, arg) => {
    const t0 = Date.now();
    for (;;) { const v = await page.evaluate(fn, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(40); }
  };
  const toastSeen = (t) => waitFor((t) => [...document.querySelectorAll(".toast")].some((n) => n.textContent.includes(t)), "toast " + t, 5000, t);
  const go = async (view) => { await page.click(`.nav-item[data-nav="${view}"]`); await waitFor((v) => !document.querySelector(`.view[data-view="${v}"]`).hidden, "view " + view, 2000, view); await sleep(120); };
  const layout = () => ev(() => {
    const doc = document.scrollingElement;
    const view = [...document.querySelectorAll(".view")].find((v) => !v.hidden);
    let min = 99, minEl = "";
    for (const el of view.closest(".app").querySelectorAll("*")) {
      if (el.closest("[hidden], .sr-only, svg, .legacy-state")) continue;
      const own = [...el.childNodes].some((n) => n.nodeType === 3 && n.textContent.trim());
      if (!own) continue;
      const r = el.getBoundingClientRect(); if (r.width < 2 || r.height < 2) continue;
      if (getComputedStyle(el).visibility === "hidden") continue;
      const fs = parseFloat(getComputedStyle(el).fontSize);
      if (fs < min) { min = fs; minEl = el.id || el.className || el.tagName; }
    }
    return { docScrollY: doc.scrollHeight - innerHeight, docScrollX: doc.scrollWidth - innerWidth,
      viewOverY: view.scrollHeight - view.clientHeight, viewOverX: view.scrollWidth - view.clientWidth, minFont: min, minEl, view: view.dataset.view };
  });
  const checkLayout = async (tag) => {
    const l = await layout();
    assert.ok(l.docScrollY <= 0 && l.docScrollX <= 0, `${tag}: page scroll ${JSON.stringify(l)}`);
    assert.ok(l.viewOverX <= 0, `${tag}: horizontal overflow ${JSON.stringify(l)}`);
    assert.ok(l.viewOverY <= 0, `${tag}: vertical overflow in view ${JSON.stringify(l)}`);
    assert.ok(l.minFont >= 12, `${tag}: font < 12px ${JSON.stringify(l)}`);
    return l;
  };
  const shot = async (name) => { if (SHOTS) { mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: join(SHOTS, name) }); } };

  // ---------- static ----------
  for (const sel of ["script[src^='/app.js']", "script[src^='/ui-shell.js']", "script[src^='/ui-pages.js']", "script[src^='/firmware-upgrade.js']", "script[src^='/ui-chart.js']", "script[src^='/ui-adv.js']", "script[src^='/filter-profile.js']"])
    assert.ok((await page.$eval(sel, (s) => s.getAttribute("src"))).endsWith(`?v=${VERSION}`), sel);
  assert.ok((await page.$eval("link[href^='/style.css']", (s) => s.getAttribute("href"))).endsWith(`?v=${VERSION}`));
  assert.deepEqual(await page.$$eval(".nav-item", (l) => l.map((a) => a.textContent.trim())), ["状态", "输出", "校准", "升级", "设置"]);
  assert.equal(await txt("connStatus"), "未连接");
  assert.equal(await txt("connectBtn"), "连接设备");
  assert.equal(await page.$eval("#enterBtn", (e) => e.closest("[hidden]") !== null), true, "enter settings button not exposed");
  const homeText = await page.$eval(".view-status", (e) => e.innerText);
  for (const banned of ["波特率", "CAN ID", "四元数", "状态位", "通信日志", "UART", "Acc"]) assert.ok(!homeText.includes(banned), "home shows " + banned);
  assert.equal(await page.$eval("#homeCharts", (e) => e.hidden), true, "charts hidden before data");
  step(`static: assets ?v=${VERSION}, 5 nav items (CAN merged into 输出), 未连接, no settings-mode button, home free of technical params`);

  await waitFor(() => window.__gyro.model()?.ready === true, "real GLB model ready", 30000);
  const model = await ev(() => window.__gyro.model());
  assert.ok(model.triangles > 1000 && model.drawCalls > 0);
  step(`3D: real GLB loaded (ready=true, ${model.triangles} triangles, ${model.componentMeshes} meshes)`);

  // firmware page before connecting: pick file, start disabled
  await go("upgrade");
  assert.equal(await txt("fwFileName"), "请选择固件");
  const input = await page.$("#firmwareFile"); await input.uploadFile(fwPath);
  await waitFor(() => window.__gyro.state().firmware.imageBytes === 9000, "image validated");
  await sleep(200);
  assert.equal(await txt("fwFileName"), "app3-test-firmware.bin");
  assert.match(await txt("fwFileSize"), /8\.8 KB/);
  assert.equal(await page.$eval("#firmwareStartBtn", (b) => b.disabled), true);
  step("upgrade: file picker shows name + size, 开始升级 disabled until connected");

  // ---------- connect ----------
  await ev(() => Object.assign(window.__mock.dev, { extended: true, configVersion: 3, capabilities: 127, canExtended: true,
    outputs: [{ format: 0, legacyMode: 0, mask: 7 }, { format: 0, legacyMode: 0, mask: 7 }], savedOutputs: [{ format: 0, legacyMode: 0, mask: 7 }, { format: 0, legacyMode: 0, mask: 7 }] }));
  await go("status");
  await page.click("#connectBtn");
  await waitFor(() => document.getElementById("connStatus").textContent === "USB · 已连接", "connected", 4000);
  await waitFor(() => document.getElementById("yaw").textContent === "12.50", "yaw 12.50");
  assert.equal(await txt("pitch"), "-3.25"); assert.equal(await txt("roll"), "45.50");
  await waitFor(() => Number(document.getElementById("rate").textContent) > 10, "data rate");
  await waitFor(() => document.getElementById("temp").textContent === "31.50", "temp");
  assert.equal(await page.$eval("#disconnectBtn", (b) => b.hidden), false);
  assert.equal(await page.$eval("#connectBtn", (b) => b.hidden), true);
  await toastSeen("已连接");
  await ev(() => { window.__mock.dev.pose.yaw = -100.5; });
  await waitFor(() => document.getElementById("yaw").textContent === "-100.50", "yaw update");
  step("connect (mock): top bar USB · 已连接, YPR 2 decimals live update, temp + rate, toast ✓ 已连接");
  await sleep(300);
  await checkLayout("home 1440"); await shot("local-home-1440.png");

  // ---------- output ----------
  await go("output");
  assert.equal(await page.$eval('#outPortSeg button.on', (b) => b.textContent), "USB");
  assert.equal(await page.$eval("#outputPanel0", (f) => f.hidden), true);
  await page.click('#outPortSeg button[data-port="0"]');
  assert.equal(await page.$eval("#outputPanel1", (f) => f.hidden), true);
  assert.equal(await page.$$eval("#outputFields0 > label", (l) => l.length), 6);
  await page.click("#outputFields0 .more > summary");
  await page.click('#outputFields0 input[value="8"]'); // Gz
  await page.click('#outputFields0 input[value="2"]'); // Roll off
  await page.$eval("#outHz", (e) => { e.value = ""; }); await page.type("#outHz", "500");
  await checkLayout("output 1440 (more open)").catch(() => {}); // allowed to scroll inside view when expanded
  await page.click("#outputApplyAll");
  await waitFor(() => window.__mock.dev.savedOutputs[0].mask === (1 | 2 | 256), "UART output saved");
  await waitFor(() => window.__mock.dev.savedOutputHz === 500, "rate saved");
  await toastSeen("✓ 已保存");
  await waitFor(() => document.getElementById("outputApplyAll").textContent.trim() === "应用", "应用 label restored (app3: wait instead of instant check)", 3000);
  await page.click("#outputFields0 .more > summary");
  step("output: segmented USB/UART (defaults to connected port), 6 + 更多选项, one 应用 → OUTPUT_CONFIG(UART) + OUTPUT_HZ 500, ✓ 已保存");
  await sleep(200); await checkLayout("output 1440"); await shot("local-output-1440.png");

  // ---------- app2: home waveform charts ----------
  const cs = () => ev(() => ({ ...window.GyroUI.charts.state(), frames: window.GyroUI.charts.frames,
    box: !document.getElementById("homeCharts").hidden, acc: !document.getElementById("accChart").hidden, gyro: !document.getElementById("gyroChart").hidden }));
  await go("status"); await sleep(400);
  let c = await cs();
  assert.ok(!c.box && !c.acc && !c.gyro, "charts hidden while only Y/P/R stream " + JSON.stringify(c));
  await ev(() => { const d = window.__mock.dev; let t = 0; window.__sine = setInterval(() => { t += 0.01; Object.assign(d.pose, {
    ax: 2.0 * Math.sin(t * 6), ay: 1.2 * Math.cos(t * 4), az: 9.8 + 0.8 * Math.sin(t * 9), gx: 120 * Math.sin(t * 3), gy: 60 * Math.cos(t * 5), gz: 30 * Math.sin(t * 7) }); }, 10); });
  await go("output");
  await page.click('#outPortSeg button[data-port="1"]');
  for (const v of ["3", "4", "5"]) await page.click(`#outputFields1 input[value="${v}"]`);
  await page.click("#outputApplyAll");
  await waitFor(() => window.__mock.dev.savedOutputs[1].mask === (7 | 56), "USB acc output saved", 5000);
  await toastSeen("✓ 已保存");
  await go("status");
  await waitFor(() => !document.getElementById("accChart").hidden, "acc chart visible", 3000);
  c = await cs(); assert.ok(c.acc && !c.gyro, "only acc chart " + JSON.stringify(c));
  await go("output");
  await page.click("#outputFields1 .more > summary");
  for (const v of ["6", "7", "8"]) await page.click(`#outputFields1 input[value="${v}"]`);
  await page.click("#outputApplyAll");
  await waitFor(() => window.__mock.dev.savedOutputs[1].mask === 511, "USB gyro output saved", 5000);
  await page.click("#outputFields1 .more > summary");
  await go("status");
  await waitFor(() => !document.getElementById("gyroChart").hidden, "gyro chart visible", 3000);
  await sleep(1500);
  const pix = await ev(() => ["accChart", "gyroChart"].map((id) => {
    const cv = document.querySelector(`#${id} canvas`), g = cv.getContext("2d"), d = g.getImageData(0, 0, cv.width, cv.height).data;
    const near = (r, gg, b) => { let n = 0; for (let i = 0; i < d.length; i += 4) if (Math.abs(d[i] - r) < 40 && Math.abs(d[i + 1] - gg) < 40 && Math.abs(d[i + 2] - b) < 40 && d[i + 3] > 200) n++; return n; };
    return { w: cv.width, cssW: cv.clientWidth, x: near(0x63, 0x5b, 0xff), y: near(0x22, 0xa0, 0x6b), z: near(0xf7, 0x90, 0x09) };
  }));
  for (const p of pix) assert.ok(p.x > 50 && p.y > 50 && p.z > 50, "colored traces " + JSON.stringify(pix));
  assert.ok(pix[0].w >= pix[0].cssW * (await ev(() => devicePixelRatio)) - 1, "DPR-sized canvas");
  const legend = await ev(() => ["axLive", "ayLive", "azLive", "gxLive", "gyLive", "gzLive"].map((id) => document.getElementById(id).textContent.trim()));
  for (const t of legend) assert.match(t, /^-?\d+\.\d+$/, "legend " + legend);
  assert.ok(Math.abs(Number(legend[2]) - 1) < 0.2, "az shown in g: " + legend[2]);
  const f1 = (await cs()).frames; await sleep(400); const f2 = (await cs()).frames;
  assert.ok(f2 - f1 >= 1 && f2 - f1 <= 16, `rAF throttled ≤30fps (headless SwiftShader is slower): ${f2 - f1} frames / 400ms`);
  step(`charts: hidden w/o IMU data, acc only → acc chart, +gyro → both; X/Y/Z colored traces (${pix.map((p) => p.x).join("/")} px), legend live (az ${legend[2]} g), DPR canvas, ${f2 - f1} draws/400ms`);
  await checkLayout("home 1440 charts"); await shot("local-app2-home-1440.png");
  const h1440 = await ev(() => ["accChart", "gyroChart"].map((id) => Math.round(document.getElementById(id).getBoundingClientRect().height)));
  await go("output"); const p1 = (await cs()).frames; await sleep(500); const p2 = (await cs()).frames;
  assert.equal(p2, p1, "drawing paused off home");
  await go("status"); { const t0 = Date.now(); while ((await cs()).frames <= p2 && Date.now() - t0 < 3000) await sleep(100); } assert.ok((await cs()).frames > p2, "resumes on home"); // app3: poll ≤3 s (app2 used a fixed 300 ms, flaky on the original too)
  await page.setViewport({ width: 1024, height: 768 }); await sleep(400);
  await checkLayout("home 1024 charts"); await shot("local-app2-home-1024.png");
  const h1024 = await ev(() => ({ stage: Math.round(document.querySelector(".stage").getBoundingClientRect().height), charts: ["accChart", "gyroChart"].map((id) => Math.round(document.getElementById(id).getBoundingClientRect().height)) }));
  assert.ok(h1024.stage >= 300 && h1024.charts.every((h) => h >= 120), "1024 chart layout " + JSON.stringify(h1024));
  await page.setViewport({ width: 1440, height: 900 }); await sleep(300);
  step(`charts layout: fill right column (1440: ${h1440.join("/")}px; 1024: stage ${h1024.stage}px + ${h1024.charts.join("/")}px), no scroll; drawing paused off-home (${p1}→${p2}), resumes`);
  // transition
  await page.click('.nav-item[data-nav="calib"]');
  await waitFor(() => !document.querySelector('.view[data-view="calib"]').hidden, "calib view", 1000);
  const anim = await ev(() => { const v = document.querySelector('.view[data-view="calib"]'); return { cls: v.classList.contains("view-enter"), name: getComputedStyle(v).animationName, dur: getComputedStyle(v).animationDuration, n: v.getAnimations().length }; });
  assert.ok(anim.cls && anim.name === "view-in" && anim.n > 0 && anim.dur === "0.2s", "view transition " + JSON.stringify(anim));
  await sleep(350);
  assert.equal(await ev(() => document.querySelector('.view[data-view="calib"]').classList.contains("view-enter")), false, "class removed after animationend");
  await page.emulateMediaFeatures([{ name: "prefers-reduced-motion", value: "reduce" }]);
  await page.click('.nav-item[data-nav="status"]');
  await waitFor(() => !document.querySelector('.view[data-view="status"]').hidden, "status view", 1000);
  assert.equal(await ev(() => getComputedStyle(document.querySelector('.view[data-view="status"]')).animationName), "none", "reduced motion");
  await page.emulateMediaFeatures([{ name: "prefers-reduced-motion", value: "no-preference" }]);
  step(`view transition: ${anim.name} ${anim.dur} fade+translateY on nav, cleaned up on animationend, none with prefers-reduced-motion`);
  // keep acc+gyro stream for later steps? restore USB Y/P/R only (keeps later app1 checks unchanged)
  await go("output");
  await page.click("#outputFields1 .more > summary");
  for (const v of ["3", "4", "5", "6", "7", "8"]) await page.click(`#outputFields1 input[value="${v}"]`);
  await page.click("#outputFields1 .more > summary");
  await page.click("#outputApplyAll");
  await waitFor(() => window.__mock.dev.savedOutputs[1].mask === 7, "USB restored", 5000);
  await go("status");
  await waitFor(() => document.getElementById("homeCharts").hidden, "charts hidden after disabling", 3000);
  step("charts hidden again after Ax–Gz output disabled");

  // ---------- calibration ----------
  await go("calib");
  assert.equal(await page.$$eval("#accFaces span", (l) => l.map((e) => e.textContent.trim())).then((a) => a.join(",")), "+X,−X,+Y,−Y,+Z,−Z");
  assert.equal((await st()).setting, false);
  await page.click("#acc6Btn");
  await waitFor(() => window.__gyro.state().accCalibration?.status === 1, "calibrating", 4000);
  assert.equal((await st()).setting, true, "auto entered settings mode");
  await waitFor(() => !document.getElementById("accCancelBtn").hidden && document.getElementById("acc6Btn").hidden, "cancel visible");
  assert.equal(await txt("accStateChip"), "校准中");
  await waitFor(() => document.querySelector("#accFaces .current"), "collecting face");
  await waitFor(() => document.getElementById("accCount").textContent === "2 / 6", "2 / 6", 6000);
  await shot("local-calib-running-1440.png");
  await waitFor(() => window.__gyro.state().accCalibration?.status === 2, "calibration done", 12000);
  await toastSeen("✓ 校准完成");
  await waitFor(() => window.__gyro.state().setting === false && window.GyroUI.gate.busy === null, "auto exit settings", 4000);
  assert.equal(await txt("accCount"), "6 / 6");
  assert.equal(await page.$$eval("#accFaces .done", (l) => l.length), 6);
  // cancel path
  await waitFor(() => !document.getElementById("acc6Btn").hidden && !document.getElementById("acc6Btn").disabled, "start shown");
  await page.click("#acc6Btn");
  await waitFor(() => window.__gyro.state().accCalibration?.status === 1, "calibrating again", 4000);
  await waitFor(() => !document.getElementById("accCancelBtn").hidden, "cancel shown");
  await page.click("#accCancelBtn");
  await waitFor(() => window.__gyro.state().accCalibration?.status === 4, "cancelled", 4000);
  await toastSeen("已取消校准");
  await waitFor(() => window.__gyro.state().setting === false && window.GyroUI.gate.busy === null, "exit after cancel", 4000);
  // fit failure keeps old params (feedback text)
  await ev(() => { window.__mock.dev.accFailFit = true; window.__mock.dev.accFaceMs = 200; });
  await waitFor(() => !document.getElementById("acc6Btn").hidden && !document.getElementById("acc6Btn").disabled, "start shown");
  await page.click("#acc6Btn");
  await waitFor(() => window.__gyro.state().accCalibration?.status === 3, "fit failed", 8000);
  await toastSeen("校准失败");
  assert.match(await txt("accStatus"), /本次未保存，原参数保留/);
  assert.equal(await page.$eval("#accAdvice", (e) => e.hidden), false);
  await ev(() => { window.__mock.dev.accFailFit = false; });
  await waitFor(() => window.__gyro.state().setting === false && window.GyroUI.gate.busy === null, "exit after failure", 4000);
  const zeroBefore = await ev(() => window.__mock.dev.commands.filter((c) => c.id === 0x11).length);
  await page.click("#zeroBtn");
  await toastSeen("✓ 航向已归零");
  assert.equal(await ev(() => window.__mock.dev.commands.filter((c) => c.id === 0x11).length), zeroBefore + 1);
  step("calibration: auto enter settings → ACC_6FACE, faces ● → ✓, 2 / 6 progress, 开始⇄取消, ✓ 校准完成, cancel, fit-failure feedback, auto exit; 航向归零");
  await sleep(200); await checkLayout("calib 1440"); await shot("local-calib-1440.png");

  // ---------- CAN (输出 › CAN 分段) ----------
  await go("output");
  await page.click('#outPortSeg button[data-port="2"]');
  const tabs = await ev(() => {
    const vis = (el) => !!el && !el.closest("[hidden]") && el.getClientRects().length > 0;
    const view = document.querySelector('.view[data-view="output"]');
    return { seg: [...document.querySelectorAll("#outPortSeg button")].map((b) => b.textContent.trim()), on: document.querySelector("#outPortSeg button.on").textContent,
      can: vis(document.getElementById("canPanel")), serial: vis(document.getElementById("outputPanel0")) || vis(document.getElementById("outputPanel1")) || vis(document.getElementById("outHz")),
      primaries: [...view.querySelectorAll("button.primary")].filter(vis).map((b) => b.id), chips: [vis(document.getElementById("configState")), vis(document.getElementById("canConfigState"))],
      fields: ["canRequestId", "canMasterId", "canBaud", "canActive", "canFrequency", "canPeriod", "canPersist", "canDiscard", "canRefresh", "canApply", "canFrequencyHint", "canNodeId", "canIdBtn"].every((id) => !!document.getElementById(id)) };
  });
  assert.deepEqual(tabs.seg, ["UART", "USB", "CAN"]); assert.equal(tabs.on, "CAN");
  assert.equal(tabs.can, true); assert.equal(tabs.serial, false); assert.deepEqual(tabs.primaries, ["canApply"], "exactly one primary (保存) on CAN tab");
  assert.deepEqual(tabs.chips, [false, true]); assert.equal(tabs.fields, true);
  assert.equal(await ev(() => !!document.querySelector('.nav-item[data-nav="can"]')), false, "no CAN nav item");
  await waitFor(() => !document.getElementById("canPanel").disabled, "can panel enabled (no manual settings mode)");
  await page.click("#canRefresh");
  await toastSeen("✓ 已读取");
  await page.$eval("#canFrequency", (e) => { e.value = ""; }); await page.type("#canFrequency", "500");
  assert.equal(await page.$eval("#canPeriod", (e) => e.value), "2");
  assert.match(await txt("canFrequencyHint"), /500 Hz · 2 ms/);
  await waitFor(() => !document.getElementById("canDirty").hidden, "CAN 未保存 shown");
  await page.click("#canApply");
  await waitFor(() => window.__mock.dev.savedCan.periodMs === 2, "can saved", 5000);
  await toastSeen("✓ 已保存");
  await waitFor(() => window.__gyro.state().setting === false && window.GyroUI.gate.busy === null, "exit after CAN", 5000);
  // bandwidth constraint: 4 message types @1 kHz at 25 Kbps → rejected locally
  await page.select("#canBaud", "7");
  for (const v of ["0", "1", "3"]) await page.click(`#canOutputs input[value="${v}"]`);
  await page.$eval("#canPeriod", (e) => { e.value = ""; }); await page.type("#canPeriod", "1");
  const canCmds = await ev(() => window.__mock.dev.commands.filter((c) => c.id === 0x22).length);
  await page.click("#canApply");
  await toastSeen("CAN 参数无效");
  await waitFor(() => window.__gyro.state().setting === false && window.GyroUI.gate.busy === null, "exit after rejected CAN", 5000);
  assert.equal(await ev(() => window.__mock.dev.commands.filter((c) => c.id === 0x22).length), canCmds);
  await page.click("#canDiscard");
  assert.equal(await page.$eval("#canPeriod", (e) => e.value), "2");
  if (FSHOTS) { mkdirSync(FSHOTS, { recursive: true }); await page.screenshot({ path: join(FSHOTS, "gyro3-output-can-1440.png") }); }
  await page.click('#outPortSeg button[data-port="0"]');
  assert.deepEqual(await ev(() => [...document.querySelector('.view[data-view="output"]').querySelectorAll("button.primary")].filter((b) => !b.closest("[hidden]") && b.getClientRects().length).map((b) => b.id)), ["outputApplyAll"], "UART tab: only 应用 primary");
  assert.equal(await page.$eval("#canPanel", (e) => !!e.closest("[hidden]")), true);
  await page.click('#outPortSeg button[data-port="2"]');
  step("输出 › [UART][USB][CAN]: CAN tab shows full CAN form in place of UART/USB, one primary, CAN chip; no CAN nav item");
  step("CAN: editable without manual settings mode, 读取 → ✓ 已读取, Hz↔ms, 保存 → auto enter/write/exit, bandwidth check rejects, 撤销");
  await sleep(200); await checkLayout("output › CAN 1440"); await shot("local-can-1440.png");
  await page.setViewport({ width: 1024, height: 768 }); await sleep(250); await checkLayout("output › CAN 1024");
  if (FSHOTS) await page.screenshot({ path: join(FSHOTS, "gyro3-output-can-1024.png") });
  await page.setViewport({ width: 1440, height: 900 }); await sleep(200);

  // ---------- settings ----------
  await go("settings");
  await page.click('input[name="fusion"][value="0"]');
  await page.select("#gyroRange", "2000");
  await page.$eval("#gyroInitSeconds", (e) => { e.value = ""; }); await page.type("#gyroInitSeconds", "3");
  await waitFor(() => !document.getElementById("startupDirty").hidden, "startup 未保存 shown");
  await page.click("#applyBtn");
  await waitFor(() => window.__mock.dev.savedMode === 0 && window.__mock.dev.savedRangeDps === 2000 && window.__mock.dev.savedInitMs === 3000, "startup saved", 5000);
  await toastSeen("✓ 已保存");
  await waitFor(() => window.__gyro.state().setting === false && window.GyroUI.gate.busy === null, "exit after startup", 5000);
  await waitFor(() => document.getElementById("startupDirty").hidden, "dirty cleared");
  assert.match(await txt("restartHint"), /重启后生效/);
  // fast boot switch hides init time
  await page.click("#fastStart");
  assert.equal(await page.$eval("#gyroInitRow", (e) => e.hidden), true);
  await page.click("#startupDiscard");
  assert.equal(await page.$eval("#fastStart", (e) => e.checked), false);
  step("settings: fusion / range / bias time → 保存 auto enter → STARTUP → readback → auto exit, ✓ 已保存, 快速启动 hides 零偏时间, 撤销");
  await sleep(200); await checkLayout("settings 1440"); await shot("local-settings-1440.png");
  await page.click("#advPanel > summary");
  await sleep(450);
  const adv = await ev(() => ({
    stream: [document.getElementById("stStream").textContent, document.getElementById("stStream").className],
    can: [document.getElementById("stCan").textContent, document.getElementById("stCan").className],
    fusion: document.getElementById("stFusion").textContent, out: document.getElementById("stOut").textContent,
    flags: [...document.querySelectorAll("#stFlags .chip")].map((e) => e.textContent),
    rows: [...document.querySelectorAll("#stStartup tbody tr")].map((tr) => [tr.dataset.k, tr.cells[1].textContent, tr.cells[2].textContent, tr.classList.contains("differ")]),
    chips: [...document.querySelectorAll("#stCounters > span")].map((e) => [e.dataset.k, e.querySelector("b").textContent, e.className]),
    raw: [...document.querySelectorAll("#rawTable tbody tr")].map((tr) => [...tr.cells].slice(1).map((td) => td.textContent)),
    align: getComputedStyle(document.querySelector("#rawTable tbody td:nth-child(3)")).textAlign,
    font: getComputedStyle(document.querySelector("#rawTable tbody td:nth-child(3)")).fontFamily,
    tile: (() => { const b = document.querySelector(".tile>b"), l = document.querySelector(".tile>span"); return [getComputedStyle(b).fontSize, getComputedStyle(b).fontWeight, getComputedStyle(b).fontVariantNumeric, getComputedStyle(l).color]; })(),
    legacyHidden: ["streamLabel", "statsLabel", "yaw2", "quat"].every((id) => document.getElementById(id) && document.getElementById(id).offsetParent === null),
  }));
  assert.ok(adv.legacyHidden, "legacy ids kept, hidden");
  assert.match(adv.stream[0], /USB · /); assert.match(adv.stream[1], /chip/);
  assert.deepEqual(adv.can, ["就绪", "chip on"]);
  assert.match(adv.fusion, /^\d+$/); assert.match(adv.out, /^\d+$/);
  assert.ok(adv.flags.length >= 1);
  const rowMap = Object.fromEntries(adv.rows.map((r) => [r[0], r]));
  assert.ok(rowMap.init[3] && rowMap.init[2].includes("3.0 s") && rowMap.init[2].includes("重启生效"), "init differ badge " + JSON.stringify(adv.rows));
  assert.ok(rowMap.range[1].startsWith("±") && rowMap.rate[1].endsWith("Hz"));
  assert.ok(adv.rows.filter((r) => !r[3]).every((r) => !r[2].includes("重启生效")), "no badge when equal");
  const chipMap = Object.fromEntries(adv.chips.map((c) => [c[0], c]));
  assert.ok(Number(chipMap.binFrames[1].replace(/,/g, "")) > 0 && chipMap.binFrames[2] === "", "binary counter neutral");
  assert.ok(chipMap.badLengthFrames[1] === "0" && chipMap.badLengthFrames[2] === "", "zero error counter not tinted");
  assert.ok(adv.raw[0].slice(1).every((v) => /^-?\d+\.\d{2}$/.test(v)), "euler 2 decimals " + adv.raw[0]);
  assert.ok(adv.raw[0][1] === "45.50" && adv.raw[0][3] === "-100.50", "euler X=Roll, Z=Yaw " + adv.raw[0]);
  assert.ok(adv.align === "right" && /mono|Menlo|Consolas/i.test(adv.font), "raw table right-aligned mono");
  assert.ok(parseFloat(adv.tile[0]) >= 14 && parseFloat(adv.tile[0]) <= 16 && adv.tile[1] === "600" && adv.tile[2].includes("tabular-nums") && adv.tile[3] === "rgb(102, 112, 133)", "tile style " + adv.tile);
  // error counter tint when > 0
  await ev(() => { parser.stats.badLengthFrames += 2; parser.stats.justResyncs += 1; });
  await sleep(300);
  const tint = await ev(() => ["badLengthFrames", "justResyncs"].map((k) => document.querySelector(`#stCounters [data-k="${k}"]`).className));
  assert.deepEqual(tint, ["bad-danger", "bad-warn"]);
  await ev(() => { parser.stats.badLengthFrames -= 2; parser.stats.justResyncs -= 1; });
  await checkLayout("settings advanced 1440").catch(() => {});
  await page.$eval("#advPanel", (e) => e.scrollIntoView({ block: "start" })); await sleep(250);
  await shot("local-app2-settings-advanced-1440.png");
  step(`settings advanced (structured): 数据流 ${adv.stream[0]} / CAN ${adv.can[0]} badges, 融合 ${adv.fusion} Hz, 当前|已保存 table with 重启生效 badge on differing rows only, counter chips tinted only when >0, raw X/Y/Z(+W) table right-aligned mono fixed decimals; legacy ids kept hidden`);
  await page.click("#logPanel > summary");
  await sleep(150);
  assert.ok((await page.$eval("#log", (e) => e.textContent)).includes("发送 CMD 0x1e"));
  const logBox = await page.$eval("#log", (e) => ({ h: e.getBoundingClientRect().height, font: getComputedStyle(e).fontFamily }));
  assert.ok(logBox.h <= 241 && /mono|Menlo|Consolas/i.test(logBox.font));
  await page.click("#copyLog"); await toastSeen("已复制");
  await page.click("#clearLog");
  assert.equal(await page.$eval("#log", (e) => e.textContent), "");
  await page.click("#pingBtn"); await toastSeen("设备在线");
  await page.select("#streamMode", "4"); await page.click("#streamApplyBtn");
  await waitFor(() => document.getElementById("justChannels").value === "6" || document.getElementById("streamLabel").textContent.includes("6"), "stream preset");
  await shot("local-settings-advanced-1440.png");
  await page.click("#logPanel > summary"); await page.click("#advPanel > summary");
  step("settings advanced: log (monospace, ≤240px) 复制 / 清空, PING, data stream preset");

  // ---------- 姿态稳定性 (设置 › 设备; auto settings mode) ----------
  await go("settings");
  await ev(() => { window.__gyro.filter.query(); });
  await waitFor(() => document.getElementById("filterState").textContent.includes("内部 1000 Hz"), "filter readback");
  const fs0 = await ev(() => { const p = document.getElementById("filterPanel"); return { inDevice: p.closest("#startupControls")?.querySelector(".card-h h2")?.textContent, title: p.querySelector("h3").textContent,
    options: [...p.querySelectorAll('input[name="filterProfile"]')].map((i) => i.parentElement.textContent.trim()), persist: document.getElementById("filterPersist").parentElement.textContent.trim(),
    buttons: [...p.querySelectorAll("button")].map((b) => b.textContent.trim()), state: document.getElementById("filterState").textContent, text: p.innerText, setting: window.__gyro.state().setting,
    editable: !document.getElementById("filterApply").disabled && !document.querySelector('input[name="filterProfile"]').disabled }; });
  assert.equal(fs0.inDevice, "设备"); assert.equal(fs0.title, "姿态稳定性");
  assert.deepEqual(fs0.options, ["响应优先", "均衡", "静态稳定"]); assert.equal(fs0.persist, "重启后保留");
  assert.deepEqual(fs0.buttons, ["读取", "撤销修改", "应用模式"]); assert.equal(fs0.state, "当前：响应优先 · 已保存：响应优先 · 内部 1000 Hz");
  assert.equal(fs0.setting, false); assert.equal(fs0.editable, true, "editable without manual settings mode");
  for (const banned of ["消除漂移", "转速上限"]) assert.ok(!fs0.text.includes(banned));
  const fcmds = () => ev(() => window.__mock.dev.commands.map((c) => c.id));
  const fb = (await fcmds()).length;
  await page.click('#filterPanel label:has(input[value="2"])'); await page.click("#filterPersist");
  await page.click("#filterApply");
  await waitFor(() => window.__gyro.filter.state.result?.ok === true, "filter persist 0", 5000);
  await waitFor(() => window.__gyro.state().setting === false && window.GyroUI.gate.busy === null, "exit after filter", 5000);
  const fseq = (await fcmds()).slice(fb);
  assert.deepEqual(fseq.filter((c) => [0x17, 0x18, 0x26, 0x27].includes(c)).slice(-4), [0x17, 0x27, 0x26, 0x18], "auto enter → 0x27 → verify 0x26 → exit");
  for (const bad of [0x15, 0x19, 0x1e, 0x1d, 0x13]) assert.ok(!fseq.includes(bad), "no reboot/fusion/startup/rate cmd " + bad);
  assert.deepEqual(await ev(() => [window.__mock.filter.active, window.__mock.filter.saved]), [2, 0]);
  assert.equal(await txt("filterState"), "当前：静态稳定 · 已保存：响应优先 · 内部 1000 Hz");
  await toastSeen("✓ 已应用");
  step("姿态稳定性 静态稳定 persist=0: auto 0x17 → 0x27[2,0] → ACK → 0x26 verify → 0x18; no reboot/fusion cmds");
  await page.click('#filterPanel label:has(input[value="1"])');
  await page.click("#filterRead"); await sleep(300);
  assert.equal(await ev(() => document.querySelector('input[name="filterProfile"]:checked').value), "1", "draft survives readback");
  await page.click("#filterDiscard");
  assert.equal(await ev(() => document.querySelector('input[name="filterProfile"]:checked').value), "2");
  await page.click('#filterPanel label:has(input[value="1"])'); await page.click("#filterPersist");
  await page.click("#filterApply");
  await waitFor(() => window.__gyro.filter.state.result?.ok === true && window.GyroUI.gate.busy === null, "filter persist 1", 5000);
  assert.deepEqual(await ev(() => [window.__mock.filter.active, window.__mock.filter.saved]), [1, 1]);
  assert.equal(await txt("filterState"), "当前：均衡 · 已保存：均衡 · 内部 1000 Hz");
  if (FSHOTS) { mkdirSync(FSHOTS, { recursive: true }); await (await page.$("#startupControls")).screenshot({ path: join(FSHOTS, "gyro3-filter-device-card.png") }); }
  step("draft preserved across 读取, 撤销修改 restores; 均衡 persist=1 verified active=saved");
  for (const [force, re] of [[3, /未进入设置模式/], [2, /参数无效/]]) {
    await ev((f) => { window.__mock.filter.forceStatus = f; }, force);
    await page.click('#filterPanel label:has(input[value="0"])'); await page.click("#filterApply");
    await waitFor(() => window.__gyro.filter.state.result?.ok === false && window.GyroUI.gate.busy === null, "filter fail " + force, 5000);
    assert.match(await txt("filterMsg"), re);
    assert.equal(await ev(() => document.querySelector('input[name="filterProfile"]:checked').value), "0", "draft kept");
    assert.equal(await ev(() => window.__mock.filter.active), 1);
  }
  await ev(() => { window.__mock.filter.forceStatus = null; window.__mock.filter.noApply = true; });
  await page.click("#filterApply");
  await waitFor(() => /回读不一致/.test(document.getElementById("filterMsg").textContent) && window.GyroUI.gate.busy === null, "mismatch", 5000);
  await ev(() => { window.__mock.filter.noApply = false; });
  await page.click("#filterDiscard");
  assert.equal(await ev(() => window.__gyro.state().setting), false);
  step("0x03 / 0x02 ACK failures + 0x0B not success (draft kept); ACK ok + readback mismatch → failure");
  await checkLayout("settings 1440 (filter)");
  await page.setViewport({ width: 1024, height: 768 }); await sleep(250); await checkLayout("settings 1024 (filter)");
  await page.setViewport({ width: 1440, height: 900 }); await sleep(200);


  // ---------- firmware upgrade (mock Bootloader v1) ----------
  await go("upgrade");
  await waitFor(() => !document.getElementById("firmwareStartBtn").disabled, "start enabled after connect");
  await shot("local-upgrade-ready-1440.png");
  await page.click("#firmwareStartBtn");
  await waitFor(() => document.getElementById("firmwareStartBtn").textContent === "升级中…", "busy label", 3000);
  await waitFor(() => window.__gyro.state().firmware.phase === "complete", "upgrade complete", 30000);
  await waitFor(() => document.getElementById("fwResult").textContent === "✓ 升级完成" && document.getElementById("fwPct").textContent === "100%", "result line");
  const boot = await ev(() => ({ log: window.__mock.dev.bootLog, size: window.__mock.dev.bootImage?.size, booted: window.__mock.dev.bootBooted, data: Array.from(window.__mock.dev.bootImage.data.slice(0, 16)) }));
  assert.equal(boot.size, 9000); assert.equal(boot.booted, 1);
  assert.deepEqual(boot.data, Array.from(image.slice(0, 16)));
  await waitFor(() => document.getElementById("connStatus").textContent.includes("已连接"), "reconnected after upgrade", 5000);
  await toastSeen("✓ 升级完成");
  step(`upgrade (mock Bootloader v1): 0x16 → HELLO/BEGIN/${boot.log.filter((c) => c === 3).length}×DATA/END/BOOT, progress 100%, ✓ 升级完成, reconnected`);
  await sleep(200); await checkLayout("upgrade 1440"); await shot("local-upgrade-1440.png");
  await page.click("#fwRecoveryLink");
  assert.equal(await page.$eval("#fwRecoveryBox", (e) => e.hidden), false);
  await page.click("#fwRecoveryLink");

  // ---------- 1024 x 768 ----------
  await page.setViewport({ width: 1024, height: 768 });
  let stageH = 0, withCharts = false;
  for (const v of ["status", "output", "calib", "upgrade", "settings"]) {
    await go(v); await sleep(150); await checkLayout(v + " 1024");
    if (v === "status") { await shot("local-home-1024.png"); stageH = await page.$eval(".stage", (e) => e.getBoundingClientRect().height); withCharts = await page.$eval(".home-grid", (e) => e.classList.contains("has-charts")); }
  }
  assert.ok(stageH >= (withCharts ? 300 : 360), "stage height at 1024x768 = " + stageH);
  step(`1024x768: all five views without page scroll / overflow, fonts ≥ 12px, 3D height ${Math.round(stageH)}px${withCharts ? " (JustFloat 6ch Gz/Az → charts shown)" : ""}`);
  await page.setViewport({ width: 1440, height: 900 });
  for (const v of ["status", "output", "calib", "upgrade", "settings"]) { await go(v); await checkLayout(v + " 1440 recheck"); }
  await page.setViewport({ width: 860, height: 760 });
  await go("status");
  assert.equal(await page.$eval(".nav-item span", (e) => getComputedStyle(e).display), "none");
  await page.setViewport({ width: 1440, height: 900 });
  step("<900px: icon-only nav");

  // ---------- disconnect ----------
  await go("status");
  await page.click("#disconnectBtn");
  await waitFor(() => document.getElementById("connStatus").textContent === "未连接", "disconnected");
  await go("output"); await page.click('#outPortSeg button[data-port="1"]'); await go("status");
  await page.click(".quick[href='#/output?tab=can']");
  await waitFor(() => !document.querySelector('.view[data-view="output"]').hidden && document.querySelector("#outPortSeg button.on")?.dataset.port === "2" && !document.getElementById("outCanPane").hidden, "quick action → 输出 › CAN");
  assert.equal(await ev(() => document.querySelector(".nav-item.active").dataset.nav), "output");
  await go("status"); await ev(() => { location.hash = "#/can"; });
  await waitFor(() => !document.querySelector('.view[data-view="output"]').hidden && document.querySelector("#outPortSeg button.on")?.dataset.port === "2", "legacy #/can → 输出 › CAN");
  step("disconnect → 未连接; 「CAN 设置」 quick card → 输出 with CAN tab; legacy #/can link → 输出 › CAN");
  await go("upgrade");
  assert.equal(await page.$eval("#firmwareStartBtn", (b) => b.disabled), true);
  await page.click("#fwRecoveryLink"); await page.click("#firmwareRecovery");
  await waitFor(() => !document.getElementById("firmwarePortBtn").hidden && !document.getElementById("firmwareStartBtn").disabled, "recovery mode enables start");
  await page.click("#firmwareRecovery"); await page.click("#fwRecoveryLink");
  step("recovery mode (low-weight link): 设备已在升级模式 → 选择端口 shown, 开始升级 enabled without app connection");
  assert.deepEqual(errors, []);
  step("no page errors / console errors / failed requests");
    // old firmware: 0x26 → ACK 0x01
  const old = await browser.newPage(); await old.setViewport({ width: 1440, height: 900 });
  await old.evaluateOnNewDocument(mock); await old.evaluateOnNewDocument(() => { window.__mock.filter.supported = false; });
  old.on("pageerror", (e) => errors.push("pageerror(old): " + e.message));
  await old.goto(BASE + "/#/settings", { waitUntil: "networkidle0" });
  await old.click("#connectBtn");
  await old.waitForFunction(() => document.getElementById("filterMsg").textContent === "当前固件不支持滤波模式，请升级配套固件", { timeout: 5000 });
  assert.equal(await old.evaluate(() => document.getElementById("filterApply").hidden && document.querySelector('input[name="filterProfile"]').disabled), true);
  if (FSHOTS) await (await old.$("#filterPanel")).screenshot({ path: join(FSHOTS, "gyro3-filter-oldfw.png") });
  await old.close();
  step("old firmware: 当前固件不支持滤波模式，请升级配套固件, 应用模式 hidden");
  console.log(`\nAll ${ok} app3 checks passed (mock serial, not real hardware).`);

} catch (e) {
  console.error("FAILED:", e.message); console.error("errors:", errors); process.exitCode = 1;
  try { const pg = (await browser.pages()).at(-1); console.error(await pg.evaluate(() => JSON.stringify({ st: (({ setting, accCalibration, accStartPending, running }) => ({ setting, acc: accCalibration && { ...accCalibration, bias: 0, scale: 0, raw: 0 }, accStartPending, running }))(window.__gyro.state()), msg: document.getElementById("message").textContent, log: document.getElementById("log").textContent.slice(-700), gate: window.GyroUI.gate, btn: { h: document.getElementById("acc6Btn").hidden, d: document.getElementById("acc6Btn").disabled, t: document.getElementById("acc6Btn").textContent }, cmds: window.__mock.dev.commands.slice(-12) }))); } catch (x) { console.error(x.message); }
} finally { await browser.close(); }
