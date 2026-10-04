// usb1/usb2 firmware-upgrade headless Chrome check: UART path (unchanged) + USB CDC re-enumeration path.
// MOCK SERIAL ONLY (test/mock-serial.js) — NOT real hardware, no real serial ports are opened.
// BASE=http://127.0.0.1:8871 CHROME=/usr/bin/google-chrome node test/firmware-upgrade.browser.check.mjs   (ONLY=name1,name2 optional)
import puppeteer from "puppeteer-core";
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const BASE = process.env.BASE || "http://127.0.0.1:8871";
const CHROME = process.env.CHROME || "/usr/bin/google-chrome";
const ONLY = (process.env.ONLY || "").split(",").filter(Boolean);
const mock = readFileSync(join(here, "mock-serial.js"), "utf8") + "\n" + readFileSync(join(here, "filter-mock.js"), "utf8");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const UART_OK = "升级完成：整镜像校验通过，应用已响应，原串口已重新连接。";
const USB_OK = "升级完成：整镜像校验通过，应用已响应，USB 已重新连接。";
const UART_TIMEOUT = "原串口未响应 Bootloader。请确认板上已安装配套 Bootloader；USB 端口变化时可手动选择恢复端口。";
const USB_TIMEOUT = "USB 重新枚举后未连上同一块板子。应用尚未擦除；请只保留这块板子，或勾选恢复模式并手动选择新出现的 USB 串口。";
const USB_MULTI = "检测到多块相同的 USB 设备，已停止。请只连接当前板子后再升级。";
const IDENTITY = "原串口身份发生变化，已停止升级。";
const UNCONFIRMED = "固件已完整校验，但尚未确认应用启动。请保持供电并重新连接检查；上传完成不代表应用已运行。固件已写完，不要断电，不要再点升级；刷新后重连即可。";

// images
const mk = (n, sp, reset) => { const b = new Uint8Array(n), v = new DataView(b.buffer); v.setUint32(0, sp, true); v.setUint32(4, reset, true); for (let i = 8; i < n; i++) b[i] = (i * 31 + 7) & 255; return b; };
const appImg = mk(3000, 0x2000bff0, 0x08008101);
writeFileSync("/tmp/usb1-app.bin", appImg);
writeFileSync("/tmp/usb1-app.hex", ":020000040800F2\n:00000001FF\n");
writeFileSync("/tmp/usb1-bootloader.bin", mk(6000, 0x2000bff0, 0x08000101));
writeFileSync("/tmp/usb1-fullchip.bin", mk(256 * 1024, 0x2000bff0, 0x08000101));

let ok = 0, failed = 0; const results = [];
const browser = await puppeteer.launch({ executablePath: CHROME, headless: true, args: ["--no-sandbox", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });

async function scenario(name, fn) {
  if (ONLY.length && !ONLY.includes(name)) return;
  const page = await browser.newPage(); await page.setViewport({ width: 1440, height: 900 });
  const errors = [];
  await page.evaluateOnNewDocument(mock);
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error" && !m.text().startsWith("Failed to load resource")) errors.push("console.error: " + m.text()); });
  page.on("response", (r) => { if (r.status() >= 400 && !r.url().endsWith("/favicon.ico")) errors.push(`http ${r.status()}: ${r.url()}`); });
  await page.setCacheEnabled(false);
  const resp = await page.goto(BASE + "/", { waitUntil: "networkidle0" }); assert.equal(resp.status(), 200);
  const ev = (f, ...a) => page.evaluate(f, ...a);
  const waitFor = async (f, what, timeout = 6000, arg) => { const t0 = Date.now(); for (;;) { const v = await page.evaluate(f, arg); if (v) return v; if (Date.now() - t0 > timeout) throw new Error("timeout: " + what); await sleep(50); } };
  const h = {
    page, ev, waitFor,
    setup: (o) => ev((o) => window.__mock.setup(o), o),
    status: () => ev(() => document.getElementById("firmwareStatus").textContent.trim()),
    link: () => ev(() => document.getElementById("firmwareLink").textContent.trim()),
    fw: () => ev(() => window.__gyro.state().firmware),
    calls: () => ev(() => ({ requestPort: window.__mock.calls.requestPort, getPorts: window.__mock.calls.getPorts, writes: window.__mock.calls.writes, opens: window.__mock.calls.open.length })),
    boot: () => ev(() => window.__mock.dev.bootLog.slice()),
    cmds: () => ev(() => window.__mock.dev.commands.map((c) => c.id)),
    recovery: () => ev(() => document.getElementById("firmwareRecovery").checked),
    logText: () => ev(() => document.getElementById("log")?.textContent || ""),
    // 记录每次写进度条时的值与 mock 状态（BL 命令日志、BOOT 后是否已对 PING 回 status 0）
    recordProgress: () => ev(() => {
      const el = document.getElementById("firmwareProgress"), d = Object.getOwnPropertyDescriptor(HTMLProgressElement.prototype, "value");
      window.__prog = [];
      Object.defineProperty(el, "value", { configurable: true, get() { return d.get.call(el); }, set(v) {
        window.__prog.push({ v: Number(v), phase: window.__gyro.state().firmware.phase, boot: window.__mock.dev.bootLog.slice(), pingAck: !!window.__mock.dev.pingAckAt });
        d.set.call(el, v);
      } });
    }),
    async connect() {
      await ev(() => document.getElementById("connectBtn").click());
      await waitFor(() => window.__gyro.state().running && !document.getElementById("firmwareLink").textContent.includes("未连接"), "connected");
    },
    async load(path) {
      const input = await page.$("#firmwareFile"); await input.uploadFile(path);
      await waitFor(() => ["ready", "invalid"].includes(window.__gyro.state().firmware.phase) && document.getElementById("firmwareInfo").textContent !== "尚未选择固件。", "file checked");
    },
    async start() {
      await waitFor(() => !document.getElementById("firmwareStartBtn").disabled, "start enabled");
      await ev(() => {
        window.__t0 = performance.now(); window.__linkMut = 0;
        new MutationObserver((m) => { window.__linkMut += m.length; }).observe(document.getElementById("firmwareLink"), { childList: true, characterData: true, subtree: true, attributes: true });
        document.getElementById("firmwareStartBtn").click();
      });
      await waitFor(() => window.__gyro.state().firmware.busy, "busy", 3000);
    },
    async done(timeout = 20000) {
      await waitFor(() => !window.__gyro.state().firmware.busy && ["complete", "failed", "unconfirmed"].includes(window.__gyro.state().firmware.phase), "upgrade finished", timeout);
      return (await h.fw()).phase;
    },
  };
  try {
    const detail = await fn(h);
    assert.deepEqual(errors, [], "no page errors");
    ok++; results.push(`  ✔ ${name}${detail ? " — " + detail : ""}`); console.log(results.at(-1));
  } catch (e) {
    failed++; results.push(`  ✘ ${name}: ${e.message}`); console.log(results.at(-1));
  } finally { await page.close(); }
}

const usbInfo = { usbVendorId: 0x2e3c, usbProductId: 0xf401 };

await scenario("uart-success-2E3C:5740", async (h) => {
  await h.connect();
  assert.equal(await h.link(), "当前链路：UART");
  assert.equal(await h.ev(() => window.__gyro.state().firmware.transport), "uart");
  await h.load("/tmp/usb1-app.bin"); const c0 = await h.calls();
  await h.start(); assert.equal(await h.done(), "complete");
  const c1 = await h.calls();
  assert.equal(await h.status(), UART_OK);
  assert.equal(c1.getPorts, 0, "UART path never calls getPorts (portLists=0)");
  assert.ok((await h.fw()).openBoundedCalls >= 1, "upgrade reconnect uses openPortBounded");
  assert.equal(await h.ev(() => window.__mock.listenerCount("connect")), 0, "UART: no connect listener");
  assert.equal(await h.ev(() => Number(document.getElementById("firmwareProgress").value)), 100);
  assert.equal(c1.requestPort - c0.requestPort, 0, "no requestPort during upgrade");
  const objs = await h.ev(() => window.__mock.all.map((p) => ({ id: p.id, bauds: p.openCalls.map((o) => o.baudRate) })));
  assert.equal(objs.length, 1, "single SerialPort object");
  assert.ok(objs[0].bauds.includes(2000000) && objs[0].bauds.length >= 3, "same object reopened for BL and app: " + objs[0].bauds);
  assert.ok((await h.cmds()).includes(0x16));
  const b = await h.boot(); assert.equal(b[0], 1); assert.equal(b[1], 2); assert.equal(b.at(-2), 4); assert.equal(b.at(-1), 6);
  assert.equal(await h.ev(() => window.__linkMut), 0, "#firmwareLink not rewritten");
  return `0x16 → same object @2M HELLO/BEGIN/${b.filter((x) => x === 3).length}×DATA/END/BOOT → reopen; getPorts=0`;
});

await scenario("uart-1A86:8012-and-transport-helper", async (h) => {
  await h.setup({ info: { usbVendorId: 0x1a86, usbProductId: 0x8012 } });
  const t = await h.ev(() => [[0x1a86, 0x8012], [0x2e3c, 0x5740], [0x2e3c, 0xf401], [0x2e3c, 0xf402], [undefined, undefined]].map(([v, p]) => window.__gyro.firmwareTransport({ usbVendorId: v, usbProductId: p })));
  assert.deepEqual(t, ["uart", "uart", "usb", "uart", "uart"]);
  await h.connect(); assert.equal(await h.link(), "当前链路：UART");
  await h.load("/tmp/usb1-app.bin"); await h.start(); assert.equal(await h.done(), "complete");
  assert.equal(await h.status(), UART_OK); assert.equal((await h.calls()).getPorts, 0);
  return "firmwareTransport: 1A86:8012/2E3C:5740/2E3C:F402 → uart, 2E3C:F401 → usb; WCH upgrade on UART path";
});

await scenario("uart-identity-change", async (h) => {
  // 0x16 后原对象先不应答 Bootloader，约 1.5 s 后同一对象 getInfo() 变成另一种 VID/PID
  await h.setup({ uartIgnoreEnter: true });
  await h.ev(() => { const d = window.__mock.dev, base = d.handle; d.handle = function (id, seq, pl) { if (id === 0x16) setTimeout(() => { window.__mock.port.info = { usbVendorId: 0x1a86, usbProductId: 0x7523 }; }, 1500); return base.call(this, id, seq, pl); }; });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(), "failed");
  const s = await h.status(); assert.ok(s.startsWith(IDENTITY), s);
  assert.ok(!(await h.boot()).includes(2)); assert.equal((await h.calls()).getPorts, 0); assert.equal(await h.recovery(), true);
  return s;
});

await scenario("uart-timeout", async (h) => {
  await h.setup({ uartIgnoreEnter: true });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); const c0 = await h.calls(); await h.start();
  assert.equal(await h.done(22000), "failed");
  const s = await h.status(); assert.ok(s.startsWith(UART_TIMEOUT) && s.includes("应用尚未擦除"), s);
  const el = await h.ev(() => performance.now() - window.__t0);
  assert.ok(el > 14500 && el < 21000, "~15 s window, got " + el);
  assert.equal(await h.recovery(), true); const c1 = await h.calls();
  assert.equal(c1.getPorts, 0); assert.equal(c1.requestPort, c0.requestPort);
  return `${Math.round(el)} ms → ${s}`;
});

await scenario("usb-reenumeration-success", async (h) => {
  await h.setup({ transport: "usb" });
  await h.connect(); assert.equal(await h.link(), "当前链路：USB");
  await h.load("/tmp/usb1-app.bin"); const c0 = await h.calls(); await h.start();
  const linkDuring = await h.link();
  assert.equal(await h.done(), "complete"); const c1 = await h.calls();
  assert.equal(await h.status(), USB_OK);
  assert.equal(c1.requestPort - c0.requestPort, 0, "requestPort not called during upgrade");
  assert.ok(c1.getPorts > 0);
  const objs = await h.ev(() => window.__mock.all.map((p) => ({ id: p.id, dead: p.dead, bauds: p.openCalls.map((o) => o.baudRate) })));
  assert.equal(objs.length, 3, "origin → BL object → app object");
  assert.ok(objs[0].dead && objs[1].dead && !objs[2].dead, "origin and BL objects died on re-enumeration");
  assert.ok(objs[1].bauds.length >= 1 && objs[2].bauds.length >= 1, "BL on 2nd object, app on 3rd object");
  assert.equal(await h.ev(() => window.__gyro.state().running), true);
  assert.equal(await h.ev(() => window.__mock.port.id), 2);
  assert.equal(linkDuring, "当前链路：USB"); assert.equal(await h.ev(() => window.__linkMut), 0, "#firmwareLink not rewritten while busy");
  assert.equal(await h.link(), "当前链路：USB"); assert.equal(await h.recovery(), false);
  const b = await h.boot(); assert.equal(b.at(-1), 6);
  return `new BL object + new app object adopted, requestPort Δ=0, getPorts=${c1.getPorts}, #firmwareLink mutations=0`;
});

await scenario("usb-browser-reuses-same-object", async (h) => {
  await h.setup({ transport: "usb", usbEnter: { reuse: true }, usbBoot: { reuse: true } });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(), "complete"); assert.equal(await h.status(), USB_OK);
  assert.equal(await h.ev(() => window.__mock.all.length), 1);
  return "same SerialPort revived for BL and app";
});

await scenario("usb-origin-fallback", async (h) => {
  await h.setup({ transport: "usb", usbEnter: { transientMs: 1500, reuse: true } });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(), "complete"); assert.equal(await h.status(), USB_OK);
  const o = await h.ev(() => window.__mock.all.map((p) => ({ id: p.id, role: p.role, n: p.openCalls.length, bauds: p.openCalls.map((x) => x.baudRate) })));
  const transient = o.find((p) => p.role === "silent");
  assert.ok(transient && transient.n >= 1, "transient fresh port was tried");
  assert.ok(o[0].bauds.includes(2000000), "origin object reopened for BL after transient vanished");
  assert.ok((await h.boot()).includes(2));
  return `transient fresh tried ${transient.n}×, then origin used`;
});

await scenario("usb-two-fresh-multi-device", async (h) => {
  await h.setup({ transport: "usb", usbEnter: { extraFresh: 1 } });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(), "failed");
  const el = await h.ev(() => performance.now() - window.__t0);
  const s = await h.status();
  assert.ok(s.startsWith(USB_MULTI) && s.includes("应用尚未擦除"), s);
  assert.ok(el < 3000, "thrown immediately, not retried: " + el);
  const b = await h.boot(); assert.equal(b.filter((x) => x === 2).length, 0, "erases == 0");
  const fresh = await h.ev(() => window.__mock.all.slice(1).map((p) => p.openCalls.length));
  assert.deepEqual(fresh, [0, 0], "neither fresh port opened");
  assert.equal(await h.recovery(), true);
  return `${Math.round(el)} ms, erases=0 → ${s}`;
});

await scenario("usb-preauthorized-other-board-never-used", async (h) => {
  await h.setup({ transport: "usb", usbEnter: { appearMs: 2500 } });
  const other = await h.ev(() => window.__mock.addOther());
  await h.connect(); await h.load("/tmp/usb1-app.bin"); const c0 = await h.calls(); await h.start();
  assert.equal(await h.done(), "complete"); assert.equal(await h.status(), USB_OK);
  assert.equal(await h.ev((id) => window.__mock.byId(id).openCalls.length, other), 0, "other board never opened");
  assert.equal((await h.calls()).requestPort - c0.requestPort, 0);
  return "other 2E3C:F401 board (authorized before click) untouched while waiting 2.5 s for re-enumeration";
});

await scenario("usb-getPorts-throws-falls-back", async (h) => {
  await h.setup({ transport: "usb", getPortsThrows: true, usbEnter: { reuse: true }, usbBoot: { reuse: true } });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(), "complete"); assert.equal(await h.status(), USB_OK);
  const c = await h.calls(); assert.ok(c.getPorts >= 2, "getPorts attempted " + c.getPorts);
  return `getPorts threw ${c.getPorts}× → kept current port, success`;
});

await scenario("usb-timeout", async (h) => {
  await h.setup({ transport: "usb", usbEnter: { never: true } });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); const c0 = await h.calls(); await h.start();
  assert.equal(await h.done(32000), "failed");
  const el = await h.ev(() => performance.now() - window.__t0);
  const s = await h.status(); assert.equal(s, USB_TIMEOUT);
  assert.ok(el > 24500 && el < 31000, "~25 s window, got " + el);
  assert.equal(await h.recovery(), true); assert.equal((await h.calls()).requestPort - c0.requestPort, 0);
  assert.equal((await h.boot()).length, 0);
  return `${Math.round(el)} ms → ${s}`;
});

await scenario("usb-post-write-multi-device-swallowed", async (h) => {
  await h.setup({ transport: "usb", usbBoot: { reuse: true, extraFresh: 2 } });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(), "complete"); assert.equal(await h.status(), USB_OK);
  const o = await h.ev(() => window.__mock.all.map((p) => ({ role: p.role, n: p.openCalls.length })));
  assert.deepEqual(o.filter((p) => p.role === "silent").map((p) => p.n), [0, 0], "extra fresh boards never opened");
  assert.equal(await h.ev(() => window.__mock.port.id), 1, "stayed on last confirmed (BL) object");
  return "2 new same-VID/PID ports after BOOT ignored; success kept on last confirmed port";
});

await scenario("usb-teardown-budget-close-hangs", async (h) => {
  await h.setup({ transport: "usb", usbEnter: { hangOld: true } });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(), "complete"); assert.equal(await h.status(), USB_OK);
  const firstBl = await h.ev(() => window.__mock.calls.open.find((o) => o.id === 1).t - window.__t0);
  const tl = await h.ev(() => JSON.stringify({ open: window.__mock.calls.open.map((o) => [o.id, Math.round(o.t - window.__t0)]), ev: window.__mock.calls.events.map((e) => [e.type, e.id, Math.round(e.t - window.__t0)]), log: document.getElementById("log")?.textContent.slice(-1500) }));
  assert.ok(firstBl < 1800, "BL port opened within budget despite hung cancel/close: " + firstBl + " " + tl);
  return `old CDC cancel/close hang; BL opened after ${Math.round(firstBl)} ms`;
});

await scenario("usb-recovery-still-tears-down", async (h) => {
  await h.setup({ transport: "usb" });
  await h.ev(() => { window.__mock.dev.boot = true; }); // 上次失败后仍停在 Bootloader，应用未擦除
  await h.connect(); assert.equal(await h.link(), "当前链路：USB");
  await h.ev(() => { const c = document.getElementById("firmwareRecovery"); c.checked = true; c.dispatchEvent(new Event("change")); });
  await h.load("/tmp/usb1-app.bin"); const c0 = await h.calls(); await h.start();
  assert.equal(await h.done(), "complete"); assert.equal(await h.status(), USB_OK);
  assert.ok(!(await h.cmds()).includes(0x16), "no 0x16 in recovery");
  const o = await h.ev(() => ({ close: window.__mock.byId(0).closeCalls, bauds: window.__mock.byId(0).openCalls.map((x) => x.baudRate) }));
  assert.ok(o.close >= 1 && o.bauds.length >= 2 && o.bauds[1] === 2000000, "app handle torn down, then reopened @2M: " + JSON.stringify(o));
  assert.equal((await h.calls()).requestPort - c0.requestPort, 0);
  return `closeCalls=${o.close}, opens ${JSON.stringify(o.bauds)}, no 0x16`;
});

await scenario("usb-failure-after-begin", async (h) => {
  await h.setup({ transport: "usb", failDataAt: 2 });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(), "failed");
  const s = await h.status(); assert.ok(s.includes("未发送应用启动命令") && s.includes("保持供电") && !s.includes("应用尚未擦除"), s);
  const b = await h.boot(); assert.ok(b.includes(2) && !b.includes(6), "BEGIN sent, BOOT not sent");
  assert.equal(await h.recovery(), true);
  assert.equal(await h.ev(() => window.__gyro.state().firmware.recoveryPortSet), true);
  return s;
});

await scenario("reject-hex-bootloader-fullimage", async (h) => {
  const out = [];
  for (const f of ["/tmp/usb1-app.hex", "/tmp/usb1-bootloader.bin", "/tmp/usb1-fullchip.bin"]) {
    await h.load(f);
    assert.equal((await h.fw()).phase, "invalid", f);
    assert.equal(await h.ev(() => document.getElementById("firmwareStartBtn").disabled), true);
    out.push((await h.status()).slice(0, 24));
  }
  const c = await h.calls(); assert.equal(c.writes, 0); assert.equal(c.opens, 0); assert.equal(c.requestPort, 0);
  return "writes=0 opens=0; " + out.join(" | ");
});


// ---------------- usb2 ----------------
const staleSetup = (extra = {}) => ({ transport: "usb", usbBoot: { stale: true }, ...extra });
// 等页面挂上 connect 监听、并已开始打开那个 open() 永不 resolve 的 Bootloader 口
const waitDeadOpen = (h) => h.waitFor(() => {
  const m = window.__mock, dead = m.all.find((p) => p.hangOpen);
  return m.pendingStale && dead && dead.hangOpen && m.listenerCount("connect") >= 1 && m.calls.open.some((o) => o.id === dead.id && o.t >= dead.hangSince);
}, "page opening the dead BL port with connect listener armed", 8000);

await scenario("staleReconnect", async (h) => {
  await h.setup(staleSetup());
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  await waitDeadOpen(h);
  const deadId = await h.ev(() => window.__mock.all.find((p) => p.hangOpen).id), newId = await h.ev(() => window.__mock.pendingStale.id);
  const listed = await h.ev((id) => (window.__mock.all.find((p) => p.id === id).listed), newId);
  assert.equal(listed, false, "new app port not in getPorts()");
  const emitAt = await h.ev((id) => window.__mock.emitConnect(id), newId);
  const t0 = Date.now();
  assert.equal(await h.done(20000), "complete");
  assert.ok(Date.now() - t0 < 20000);
  const fw = await h.fw();
  assert.equal(fw.progress, 100); assert.ok((await h.status()).includes("USB 已重新连接"));
  assert.equal(await h.status(), USB_OK);
  const c = await h.calls(); assert.equal(c.requestPort, 1, "only the initial user connect");
  const opens = await h.ev(([d, n]) => [d, window.__mock.all.find((p) => p.id === n).openCalls.length], [deadId, newId]);
  const deadOpens = await h.ev((d) => window.__mock.calls.open.filter((o) => o.id === d).length, deadId);
  assert.ok(deadOpens >= 2, "dead BL port opened at least once after BOOT (HELLO open + reconnect open): " + deadOpens);
  assert.ok(opens[1] >= 1, "new port opened");
  const abort = fw.lastBoundedAbort;
  assert.ok(abort && abort.reason === "已发现重新枚举的 USB 端口" && abort.t - emitAt < 120, "pending open aborted within ~50-100 ms: " + JSON.stringify(abort) + " emit " + emitAt);
  assert.equal(await h.ev(() => window.__mock.listenerCount("connect")), 0, "connect listener removed (mock removeEventListener really detaches)");
  assert.ok((await h.logText()).includes("升级重连暂不可用，继续等待：已发现重新枚举的 USB 端口"));
  return `dead-port open aborted ${Math.round(abort.t - emitAt)} ms after connect event; requestPort=1; listener removed`;
});

await scenario("staleReconnect-event.port-variant", async (h) => {
  await h.setup(staleSetup({ eventStyle: "port" }));
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  await waitDeadOpen(h);
  await h.ev(() => window.__mock.emitConnect(window.__mock.pendingStale.id));
  assert.equal(await h.done(20000), "complete"); assert.equal(await h.status(), USB_OK);
  return "event.port (target = navigator.serial) adopted";
});

await scenario("reconnect-other-board-connect-event-ignored", async (h) => {
  await h.setup(staleSetup());
  const other = await h.ev(() => window.__mock.addOther());
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  await waitDeadOpen(h);
  await h.ev((id) => window.__mock.emitConnect(id), other);
  await sleep(700);
  assert.equal((await h.fw()).reconnectPortSet, false, "other board not adopted");
  assert.equal(await h.ev((id) => window.__mock.byId(id).openCalls.length, other), 0);
  assert.equal((await h.fw()).busy, true);
  await h.ev(() => window.__mock.emitConnect(window.__mock.pendingStale.id));
  assert.equal(await h.done(20000), "complete");
  assert.equal(await h.ev((id) => window.__mock.byId(id).openCalls.length, other), 0, "other board never opened");
  return "connect event for pre-authorized (usbPortsBefore, not origin) board ignored";
});

await scenario("progress-semantics", async (h) => {
  await h.setup({ transport: "usb" });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.recordProgress(); await h.start();
  assert.equal(await h.done(), "complete");
  const prog = await h.ev(() => window.__prog);
  const beforeEnd = prog.filter((p) => !p.boot.includes(4));
  assert.ok(beforeEnd.every((p) => p.v <= 90), "≤90 until END: " + JSON.stringify(beforeEnd.map((p) => p.v)));
  assert.equal(Math.max(...prog.filter((p) => p.phase === "write").map((p) => p.v)), 90);
  const i95 = prog.findIndex((p) => p.v === 95), i100 = prog.findIndex((p) => p.v === 100);
  assert.ok(i95 > 0 && prog[i95].boot.includes(4) && !prog[i95].boot.includes(6), "95 after END verified, before BOOT");
  assert.ok(i100 > i95 && prog[i100].pingAck && prog[i100].boot.includes(6), "100 only after BOOT + PING ACK status 0");
  assert.ok(!prog.some((p) => p.v > 95 && !p.pingAck));
  return `values ${[...new Set(prog.map((p) => p.v))].join(",")}`;
});

for (const [name, cfg] of [["no-ping-keeps-95", { dropPing: true }], ["ping-nonzero-status-keeps-95", { pingStatus: 3 }]]) {
  await scenario(name, async (h) => {
    await h.setup({ transport: "usb", ...cfg });
    await h.ev(() => { globalThis.__gyroTest = { restoreMs: 4000 }; }); // 测试钩子缩短 90 s；生产保持 90 s
    await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
    assert.equal(await h.done(20000), "unconfirmed");
    assert.equal((await h.fw()).progress, 95, "stays at 95 (not failure, not 100)");
    assert.equal(await h.status(), UNCONFIRMED);
    assert.ok(!(await h.boot()).slice(-1).includes(2) && (await h.boot()).filter((c) => c === 2).length === 1, "no re-erase / re-upload");
    return "phase unconfirmed, 95%, " + UNCONFIRMED.slice(-26);
  });
}

await scenario("boot-ack-lost-still-100", async (h) => {
  await h.setup({ transport: "usb", dropBootAck: true });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(25000), "complete"); assert.equal((await h.fw()).progress, 100);
  assert.ok((await h.logText()).includes("启动应答未确认，改为检查应用响应"));
  return "BOOT ACK timeout logged, app PING → 100";
});

await scenario("user-connect-unbounded", async (h) => {
  await h.setup({ transport: "usb" });
  await h.ev(() => { window.__mock.port.hangOpen = true; });
  await h.ev(() => document.getElementById("connectBtn").click());
  await sleep(3000);
  const fw = await h.fw();
  assert.equal(fw.openBoundedCalls, 0, "openPortBounded not used for user connect");
  assert.equal(await h.ev(() => window.__gyro.state().running), false);
  assert.equal(await h.ev(() => document.getElementById("connectBtn").disabled), true, "still waiting on plain open()");
  assert.ok(!(await h.logText()).includes("打开串口超时"));
  return "plain port.open() still pending after 3 s, no timeout wrapper";
});

await scenario("reconnect-late-open-closed", async (h) => {
  await h.setup({ transport: "usb", usbBoot: { firstOpenDelayMs: 2600 } });
  await h.connect(); await h.load("/tmp/usb1-app.bin"); await h.start();
  assert.equal(await h.done(25000), "complete"); assert.equal(await h.status(), USB_OK);
  const app = await h.ev(() => { const p = window.__mock.port; return { close: p.closeCalls, late: !!p.lateOpenAt, opens: p.openCalls.length }; });
  const log = await h.logText();
  assert.ok(log.includes("升级重连暂不可用，继续等待：打开串口超时（2000 ms）"), "2 s bound hit");
  assert.ok(log.includes("迟到打开的串口已关闭") && app.late && app.close >= 1, "late open closed: " + JSON.stringify(app));
  return `late success closed (closeCalls=${app.close}), reopened → complete`;
});

await browser.close();
console.log(`\n${ok} passed, ${failed} failed (mock serial only — NOT real hardware)`);
process.exit(failed ? 1 : 0);
