// app2 screenshots (mock serial device: acc + gyro enabled with sine motion; NOT real hardware).
// BASE=https://gyro3.233688.xyz OUT=/workspace/gyro3-shots node test/app2.shots.mjs
import puppeteer from "puppeteer-core";
import { readFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "https://gyro3.233688.xyz", OUT = process.env.OUT || "/workspace/gyro3-shots";
mkdirSync(OUT, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
const page = await browser.newPage();
await page.setViewport({ width: 1440, height: 900 });
await page.evaluateOnNewDocument(readFileSync(join(here, "mock-serial.js"), "utf8"));
await page.evaluateOnNewDocument(() => Object.assign(window.__mock.dev, { extended: true, configVersion: 3, capabilities: 127, canExtended: true, savedInitMs: 3000,
  outputs: [{ format: 0, legacyMode: 0, mask: 7 }, { format: 1, legacyMode: 0, mask: 511 }], savedOutputs: [{ format: 0, legacyMode: 0, mask: 7 }, { format: 1, legacyMode: 0, mask: 511 }] }));
const errors = []; page.on("pageerror", (e) => errors.push(e.message)); page.on("requestfailed", (r) => errors.push("failed " + r.url()));
await page.goto(BASE + "/", { waitUntil: "networkidle0" });
const wait = async (fn, t = 30000) => { const t0 = Date.now(); while (!(await page.evaluate(fn))) { if (Date.now() - t0 > t) throw new Error("timeout " + fn); await sleep(50); } };
await wait(() => window.__gyro.model()?.ready === true);
const model = await page.evaluate(() => window.__gyro.model());
await page.click("#connectBtn");
await wait(() => document.getElementById("connStatus").textContent.includes("已连接") && document.getElementById("yaw").textContent !== "--");
await page.evaluate(() => { const d = window.__mock.dev; Object.assign(d.pose, { yaw: 37.42, pitch: -8.15, roll: 21.6 }); let t = 0;
  setInterval(() => { t += 0.01; Object.assign(d.pose, { ax: 1.6 * Math.sin(t * 2.1), ay: 0.9 * Math.cos(t * 1.4), az: 9.78 + 0.6 * Math.sin(t * 3.3),
    gx: 85 * Math.sin(t * 1.7), gy: 48 * Math.cos(t * 2.6), gz: 26 * Math.sin(t * 3.1), roll: 21.6 + 6 * Math.sin(t * 1.7), pitch: -8.15 + 3 * Math.cos(t * 2.6) }); }, 10); });
await page.addStyleTag({ content: ".toasts{display:none!important}" });
await wait(() => !document.getElementById("gyroChart").hidden && !document.getElementById("accChart").hidden, 6000);
await sleep(10500); // fill the ~10 s window
await page.screenshot({ path: join(OUT, "app2-home-1440x900.png") });
const charts = await page.evaluate(() => window.GyroUI.charts.state());
await page.setViewport({ width: 1024, height: 768 }); await sleep(1200);
await page.screenshot({ path: join(OUT, "app2-home-1024x768.png") });
await page.setViewport({ width: 1440, height: 900 });
await page.click('.nav-item[data-nav="settings"]'); await sleep(500);
await page.click("#advPanel > summary"); await sleep(400);
await page.$eval("#advPanel", (e) => e.scrollIntoView({ block: "start" })); await sleep(500);
await page.screenshot({ path: join(OUT, "app2-settings-advanced-1440x900.png") });
console.log(JSON.stringify({ base: BASE, modelReady: model.ready, triangles: model.triangles, charts, errors }));
await browser.close();
