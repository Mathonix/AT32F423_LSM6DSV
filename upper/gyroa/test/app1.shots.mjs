// Screenshots of every view (mock serial device connected so values are visible; NOT real hardware).
// BASE=https://gyro3.233688.xyz OUT=/workspace/gyro3-shots node test/app1.shots.mjs
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
await page.evaluateOnNewDocument(() => Object.assign(window.__mock.dev, { extended: true, configVersion: 3, capabilities: 127, canExtended: true,
  outputs: [{ format: 0, legacyMode: 0, mask: 7 }, { format: 0, legacyMode: 0, mask: 7 }], savedOutputs: [{ format: 0, legacyMode: 0, mask: 7 }, { format: 0, legacyMode: 0, mask: 7 }] }));
const errors = []; page.on("pageerror", (e) => errors.push(e.message)); page.on("requestfailed", (r) => errors.push("failed " + r.url()));
await page.goto(BASE + "/", { waitUntil: "networkidle0" });
const wait = async (fn, t = 30000) => { const t0 = Date.now(); while (!(await page.evaluate(fn))) { if (Date.now() - t0 > t) throw new Error("timeout " + fn); await sleep(50); } };
await wait(() => window.__gyro.model()?.ready === true);
const model = await page.evaluate(() => window.__gyro.model());
await page.screenshot({ path: join(OUT, "home-disconnected-1440x900.png") });
await page.click("#connectBtn");
await wait(() => document.getElementById("connStatus").textContent.includes("已连接") && document.getElementById("yaw").textContent !== "--");
await page.evaluate(() => { Object.assign(window.__mock.dev.pose, { yaw: 37.42, pitch: -8.15, roll: 21.6 }); window.__mock.dev.acc.valid = 1; });
await page.addStyleTag({ content: ".toasts{display:none!important}" });
await sleep(1500);
const shoot = async (view, name) => { await page.click(`.nav-item[data-nav="${view}"]`); await sleep(700); await page.screenshot({ path: join(OUT, name) }); };
for (const v of ["status", "output", "calib", "can", "upgrade", "settings"]) await shoot(v, `${v === "status" ? "home" : v}-1440x900.png`);
await page.click("#advPanel > summary"); await page.click("#logPanel > summary"); await sleep(300);
await page.screenshot({ path: join(OUT, "settings-advanced-log-1440x900.png") });
await page.click("#logPanel > summary"); await page.click("#advPanel > summary");
await page.setViewport({ width: 1024, height: 768 });
await shoot("status", "home-1024x768.png");
console.log(JSON.stringify({ base: BASE, modelReady: model.ready, triangles: model.triangles, errors }));
await browser.close();
