// 无头 Chrome 检查（模拟 navigator.serial + 按固件行为应答的设备，见 mock-serial.js）
// 用法：BASE=http://127.0.0.1:8798 node test/browser.check.mjs   （先 npm run dev）
//       BASE=https://gyro.233688.xyz SHOT=/workspace/gyro-live-prod.png node test/browser.check.mjs
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8798";
const chromeCandidates = process.platform === "win32"
  ? [
      join(process.env.PROGRAMFILES || "C:/Program Files", "Google/Chrome/Application/chrome.exe"),
      join(process.env["PROGRAMFILES(X86)"] || "C:/Program Files (x86)", "Google/Chrome/Application/chrome.exe"),
      join(process.env.LOCALAPPDATA || "", "Google/Chrome/Application/chrome.exe"),
      join(process.env["PROGRAMFILES(X86)"] || "C:/Program Files (x86)", "Microsoft/Edge/Application/msedge.exe"),
      join(process.env.PROGRAMFILES || "C:/Program Files", "Microsoft/Edge/Application/msedge.exe"),
    ]
  : process.platform === "darwin"
    ? ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"]
    : ["/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser"];
const CHROME = process.env.CHROME || chromeCandidates.find(existsSync);
if (!CHROME || !existsSync(CHROME)) throw new Error("未找到 Chrome/Edge；请设置 CHROME 为浏览器可执行文件的完整路径");
const SHOT = process.env.SHOT || "";
const VERSION = process.env.VERSION || "20261001f";
const mock = readFileSync(join(here, "mock-serial.js"), "utf8");

const browser = await puppeteer.launch({ executablePath: CHROME, headless: true, args: ["--no-sandbox"] });
const errors = [];
let ok = 0;
const step = (name) => { ok++; console.log(`  ✔ ${name}`); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1400, height: 1250 });
  await page.evaluateOnNewDocument(mock);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console.error: " + m.text()); });
  page.on("requestfailed", (r) => errors.push("requestfailed: " + r.url()));
  page.on("dialog", (d) => d.accept());
  const resp = await page.goto(BASE + "/", { waitUntil: "networkidle0" });
  assert.equal(resp.status(), 200);

  const txt = (id) => page.$eval("#" + id, (e) => e.textContent);
  const prop = (sel, p) => page.$eval(sel, (e, p) => e[p], p);
  const st = () => page.evaluate(() => window.__gyro.state());
  const dev = (fn) => page.evaluate(fn);
  const waitFor = async (fn, what, timeout = 3000) => {
    const t0 = Date.now();
    for (;;) { const v = await page.evaluate(fn); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout waiting for " + what); await sleep(30); }
  };
  const logHas = (s) => page.evaluate((s) => document.getElementById("log").textContent.includes(s), s);

  // ---- 静态检查 ----
  assert.ok((await page.$eval("script[src^='/app.js']", (s) => s.getAttribute("src"))).endsWith(`?v=${VERSION}`));
  assert.ok((await page.$eval("link[href^='/style.css']", (s) => s.getAttribute("href"))).endsWith(`?v=${VERSION}`));
  assert.deepEqual(await page.$$eval("#justChannels option", (l) => l.map((o) => o.value)), ["3", "6"]);
  assert.ok(!(await page.content()).includes("4（含温度）"));
  assert.equal(await prop("#calBtn", "disabled"), true);
  assert.equal(await prop("#gyro60Btn", "disabled"), true);
  assert.match(await txt("calBtn"), /（暂未实现）/);
  assert.match(await txt("gyro60Btn"), /（暂未实现）/);
  assert.match(await txt("acc6Btn"), /需固件启用/);
  assert.equal(await prop("#acc6Btn", "disabled"), false);
  assert.equal(await txt("linkState"), "未连接");
  step(`page 200, assets ?v=${VERSION}, JustFloat 3/6 options, cal buttons disabled/labelled`);
  // Exercise legacy controls that the redesigned page groups in disclosures.
  await page.$$eval('details.advanced', (nodes) => nodes.forEach((node) => { node.open = true; }));

  // ---- 连接 ----
  await page.click("#connectBtn");
  await waitFor(() => document.getElementById("linkState").textContent === "串口已连接", "connected");
  const openCalls = await dev(() => window.__mock.port.openCalls);
  assert.equal(openCalls.length, 1);
  assert.equal(openCalls[0].bufferSize, 65536);
  assert.equal(openCalls[0].baudRate, 2000000);
  await waitFor(() => document.getElementById("modeLabel").textContent.includes("融合率"), "sysinfo");
  assert.equal(await txt("modeLabel"), "融合率：2000 Hz · 输出：1000 Hz");
  assert.ok(!(await txt("modeLabel")).includes("六轴"));
  assert.equal(await prop("#parseMode", "value"), "justfloat");
  assert.equal(await prop("#justChannels", "value"), "3");
  await waitFor(() => window.__gyro.state().pose.yaw === 12.5, "3ch pose");
  let s = await st();
  assert.deepEqual([s.pose.yaw, s.pose.pitch, s.pose.roll], [12.5, -3.25, 45.5]);
  await waitFor(() => document.getElementById("dataState").textContent === "数据正常", "data live");
  step("connect: open({bufferSize:65536, baudRate:2000000}), SYSINFO → modeLabel without mode, parser = JustFloat 3, pose live");

  // ---- 切换到 6 通道：ACK 成功 → 解析器同步 ----
  await page.select("#streamMode", "4");
  await page.click("#streamApplyBtn");
  await waitFor(() => document.getElementById("justChannels").value === "6", "sync to 6ch");
  assert.equal(await prop("#parseMode", "value"), "justfloat");
  await dev(() => { Object.assign(window.__mock.dev.pose, { yaw: -100.5, pitch: 20.25, roll: -30.75, gz: 12.5, az: -9.5, temp: 36.25 }); });
  await waitFor(() => window.__gyro.state().pose.temp === 36.25, "6ch temp");
  s = await st();
  assert.deepEqual([s.pose.yaw, s.pose.pitch, s.pose.roll, s.imu.gz, s.imu.az, s.pose.temp], [-100.5, 20.25, -30.75, 12.5, -9.5, 36.25]);
  await sleep(100);
  assert.equal(await txt("temp"), "36.25");
  assert.equal(await txt("gz"), "12.5000");
  assert.equal(await txt("az"), "-9.5000");
  assert.equal(await txt("yaw"), "-100.5000");
  assert.equal(s.stats.badLengthFrames, 0);
  assert.match(await txt("message"), /数据流已切换：VOFA 6通道/);
  step("STREAM ACK(4) → parser JustFloat 6ch; CH0..5 → yaw/pitch/roll/gz/az/temp");

  // ---- 二进制模式 ----
  await page.select("#streamMode", "1");
  await page.click("#streamApplyBtn");
  await waitFor(() => document.getElementById("parseMode").value === "binary", "sync to binary");
  await dev(() => { Object.assign(window.__mock.dev.pose, { yaw: 33.5, pitch: 1.5, roll: 2.5 }); });
  await waitFor(() => window.__gyro.state().pose.yaw === 33.5, "attitude frames");
  assert.match(await txt("flagsLabel"), /静止/);
  await page.select("#streamMode", "3");
  await page.click("#streamApplyBtn");
  await dev(() => { window.__mock.dev.pose.temp = 41.5; });
  await waitFor(() => window.__gyro.state().pose.temp === 41.5 && window.__gyro.state().imu.gx === 0.25, "imu temp");
  step("binary ATTITUDE / IMU frames decoded (IMU temp /100 → 温度)");

  // ---- 长度错误帧计数 ----
  const before = (await st()).stats.badLengthFrames;
  await page.evaluate(() => window.__gyro.feed(window.__mock.frame(0x01, 1, new Uint8Array(12))));
  s = await st();
  assert.equal(s.stats.badLengthFrames, before + 1);
  await sleep(50);
  assert.match(await txt("statsLabel"), new RegExp(`长度错误 ${before + 1}`));
  step("wrong-length ATTITUDE (12 B) dropped, counter shown");

  // ---- 输出频率校验（融合率来自 SYSINFO = 2000）----
  await page.$eval("#outHz", (e) => { e.value = "300"; });
  await page.click("#rateApplyBtn");
  assert.match(await txt("message"), /整除融合率 2000 Hz/);
  assert.ok(!(await txt("message")).includes("200Hz"));
  await page.$eval("#outHz", (e) => { e.value = "500"; });
  await page.click("#rateApplyBtn");
  await waitFor(() => document.getElementById("message").textContent.startsWith("输出频率已设为 500 Hz"), "rate ack");
  step("output Hz validated against SYSINFO fusion_hz (300 rejected, 500 accepted by device)");

  // ---- 融合模式保存（不立即重启）----
  await page.select("#streamMode", "0");
  await page.click("#streamApplyBtn");
  await waitFor(() => document.getElementById("justChannels").value === "3", "back to 3ch");
  await page.click("#enterBtn");
  await waitFor(() => document.getElementById("settingsState").textContent === "设置模式已进入", "settings entered");
  assert.equal(await prop("#canIdBtn", "disabled"), false);
  await page.click('input[name="fusion"][value="0"]');
  await page.click("#applyBtn");
  await waitFor(() => document.getElementById("settingsState").textContent === "未进入设置模式", "auto exit");
  assert.ok(await logHas("融合模式已保存（六轴），将在下次设备重启后生效"));
  assert.equal(await txt("message"), "已退出设置模式，存在已保存但尚未应用的配置，请重启设备");
  assert.equal(await prop("#canIdBtn", "disabled"), true);
  assert.equal(await prop("#canNodeId", "disabled"), true);
  assert.equal(await txt("canHint"), "融合模式已保存，请先重启设备后再修改 CAN ID");
  assert.equal(await txt("restartHint"), "融合模式已保存，将在下次设备重启后生效");
  // 再次进入设置模式：EXIT 探测 dirty=1 → 仍禁止
  await page.click("#enterBtn");
  await waitFor(() => document.getElementById("settingsState").textContent === "设置模式已进入", "settings again");
  assert.equal(await prop("#canIdBtn", "disabled"), true);
  step("fusion save w/o restart: truthful messages, auto-EXIT reports dirty, CAN ID disabled + hint");

  // ---- 设备重启后（dirty=0）：进入设置模式时探测到无待应用配置 → 恢复 CAN ID ----
  await page.click("#exitBtn");
  await waitFor(() => document.getElementById("settingsState").textContent === "未进入设置模式", "exit");
  await dev(() => { window.__mock.dev.dirty = 0; });
  await page.click("#enterBtn");
  await waitFor(() => !document.getElementById("canIdBtn").disabled, "can re-enabled");
  assert.ok(await logHas("设备无待应用的配置（已重启），CAN ID 设置已恢复"));
  assert.equal(await txt("canHint"), "");
  step("after device reboot (EXIT probe dirty=0) CAN ID re-enabled");

  // ---- 标定 ACK 失败分支 ----
  await page.click("#acc6Btn");
  await waitFor(() => document.getElementById("message").textContent.includes("0x0600"), "acc6 ack");
  assert.match(await txt("message"), /六面加速度计标定失败（执行失败，detail=0x0600：固件未启用六面加速度计标定（APP_ACC_CAL_ENABLE=0））/);
  await page.evaluate(() => window.__gyro.feed(window.__mock.dev.ack(0x12, 3, 1)));
  assert.match(await txt("message"), /陀螺重标定失败（执行失败，detail=0x0001：固件暂未实现运行时陀螺重标定/);
  await page.evaluate(() => window.__gyro.feed(window.__mock.dev.ack(0x1b, 3, 0x0601)));
  assert.match(await txt("message"), /60 s 陀螺标定失败（执行失败，detail=0x0601：固件暂未实现 60 s 运行时陀螺标定）/);
  await page.click("#exitBtn");
  step("CAL / GYRO_60 / ACC_6FACE failure ACKs decoded");

  // ---- 数据超时 ----
  await waitFor(() => document.getElementById("dataState").textContent === "数据正常", "live before timeout");
  await dev(() => { window.__mock.dev.emitting = false; });
  await waitFor(() => document.getElementById("dataState").textContent === "数据超时", "timeout", 1500);
  assert.equal(await txt("linkState"), "串口已连接");
  assert.equal(await page.$eval("#yaw", (e) => e.classList.contains("stale")), false);
  await waitFor(() => document.getElementById("yaw").classList.contains("stale"), "gray-out", 3000);
  for (const id of ["pitch", "roll", "yaw2"]) assert.equal(await page.$eval("#" + id, (e) => e.classList.contains("stale")), true);
  await dev(() => { window.__mock.dev.emitting = true; });
  await waitFor(() => document.getElementById("dataState").textContent === "数据正常" && !document.getElementById("yaw").classList.contains("stale"), "recover");
  step("data timeout: >500 ms 数据超时 (serial still 串口已连接), >2 s Yaw/Pitch/Roll grayed, recovers");

  // ---- 3D：roll 0 vs 90 ----
  await dev(() => { Object.assign(window.__mock.dev.pose, { yaw: 0, pitch: 0, roll: 0 }); });
  await waitFor(() => window.__gyro.state().pose.roll === 0 && window.__gyro.state().pose.yaw === 0, "roll 0");
  await sleep(80);
  const p0 = await page.evaluate(() => window.__gyro.projection());
  await dev(() => { Object.assign(window.__mock.dev.pose, { yaw: 0, pitch: 0, roll: 90 }); });
  await waitFor(() => window.__gyro.state().pose.roll === 90, "roll 90");
  await sleep(80);
  const p90 = await page.evaluate(() => window.__gyro.projection());
  assert.equal(p90.source, "欧拉角 ZYX");
  assert.ok(p0.hullArea > 5000, `area0 ${p0.hullArea}`);
  assert.ok(p90.hullArea > p0.hullArea * 0.2, `roll 90 collapsed? ${p90.hullArea} vs ${p0.hullArea}`);
  const moved = p0.screen.some((pt, i) => Math.hypot(pt[0] - p90.screen[i][0], pt[1] - p90.screen[i][1]) > 20);
  assert.ok(moved);
  // 画布确实有板子像素（非背景色）
  const painted = await page.evaluate(() => { const c = document.getElementById("attitude"); const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 200) n++; return n; });
  if (p90.model?.ready) assert.ok(p90.model.renderedTriangles >= p90.model.triangles, 'CAD geometry rendered');
  else assert.ok(painted > 2000, `painted ${painted}`);
  // QUAT 帧优先
  await page.evaluate(() => { const b = new Uint8Array(18); const v = new DataView(b.buffer); v.setFloat32(0, Math.SQRT1_2, true); v.setFloat32(4, Math.SQRT1_2, true); window.__gyro.feed(window.__mock.frame(0x02, 1, b)); });
  await sleep(50);
  assert.equal((await page.evaluate(() => window.__gyro.projection())).source, "四元数");
  step(`3D render: roll 90° hull ${Math.round(p90.hullArea)} px² vs roll 0° ${Math.round(p0.hullArea)} px² (not collapsed), QUAT preferred`);

  // ---- 拔出 → 自动清理 → 重连 ----
  await page.evaluate(() => window.__mock.unplug());
  await waitFor(() => window.__gyro.state().reconnecting && !window.__gyro.state().running, "unplugged; waiting for original port");
  assert.match(await txt("message"), /串口已断开/);
  await waitFor(() => window.__mock.port.readable === null && window.__mock.port.closeCalls === 1, "port closed after unplug");
  await dev(() => { Object.assign(window.__mock.dev.pose, { yaw: 30, pitch: 12, roll: 90, gz: 3.5, az: 9.81, temp: 33.75 }); window.__mock.dev.streamMode = 4; });
  await waitFor(() => document.getElementById("linkState").textContent === "串口已连接", "reconnected");
  assert.equal((await dev(() => window.__mock.port.openCalls)).length, 2);
  await waitFor(() => document.getElementById("justChannels").value === "6", "SYSINFO stream_mode=4 → 6ch");
  await waitFor(() => window.__gyro.state().pose.temp === 33.75 && window.__gyro.state().pose.yaw === 30, "data after reconnect");
  step("unplug: locks released + port closed; reconnect OK, writes work (QUERY→SYSINFO syncs 6ch)");

  await sleep(300);
  if (SHOT) { await page.screenshot({ path: SHOT, fullPage: true }); console.log(`  screenshot → ${SHOT}`); }

  // ---- 手动断开 ----
  await page.click("#disconnectBtn");
  await waitFor(() => window.__mock.port.readable === null && window.__mock.port.closeCalls === 2, "closed after disconnect");
  assert.equal(await txt("linkState"), "未连接");
  step("disconnect button closes port cleanly");

  assert.deepEqual(errors, [], "no JS errors");
  step("no JS errors / console errors / failed requests");
  console.log(`\nbrowser check: ${ok} steps passed against ${BASE}`);
} catch (e) {
  console.error("FAILED:", e.message);
  if (errors.length) console.error("page errors:", errors);
  process.exitCode = 1;
} finally {
  await browser.close();
}
