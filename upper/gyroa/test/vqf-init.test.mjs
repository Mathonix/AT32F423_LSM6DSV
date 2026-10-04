// vqfinit1：静置初始化 VQF（vqf-init.js）单元测试。node --test test/vqf-init.test.mjs
// 规格 /workspace/zaru/host-agent-vqf-static-init.md；帧经 app.js GYRO-CORE 的 buildFrame / createStreamParser / decodePayload 往返。NOT real hardware.
import test, { mock } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const here = dirname(fileURLToPath(import.meta.url));
const pub = process.env.PUBLIC_DIR || join(here, "../dist/public");
vm.runInThisContext(readFileSync(join(pub, "vqf-init.js"), "utf8"));
const V = globalThis.GyroVqf;
const appSrc = readFileSync(join(pub, "app.js"), "utf8");
const a = appSrc.indexOf("// ==== GYRO-CORE BEGIN ===="), b = appSrc.indexOf("// ==== GYRO-CORE END ====");
const core = new Function(`"use strict";\n${appSrc.slice(appSrc.indexOf("\n", a) + 1, b)}\nreturn { MSG, CMD, buildFrame, decodePayload, createStreamParser, ackDetailText };`)();

function crc(bytes) { let c = 0xffff; for (const x of bytes) { c ^= x << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; }
function pack(id, seq, payload) { const body = [id, payload.length, seq & 255, ...payload]; const c = crc(body); return Uint8Array.from([0xaa, 0x55, ...body, c & 255, c >> 8]); }
const u32 = (x) => [x & 255, (x >>> 8) & 255, (x >>> 16) & 255, (x >>> 24) & 255];
const f32 = (x) => [...new Uint8Array(new Float32Array([x]).buffer)];
const statusPl = ({ version = 1, state = 0, error = 0, source = 0, el = 0, rem = 0, n = 0, rate = 0.1, acc = 0.02, temp = 30 } = {}) => [version, state, error, source, ...u32(el), ...u32(rem), ...u32(n), ...f32(rate), ...f32(acc), ...f32(temp)];
const DEF = [0.5, 0.035, 0.6, 0.15];
const settingsPl = ({ version = 1, source = 0, cur = DEF, def = DEF, bias = [0.3, 0.038, -0.391], temp = 0 } = {}) => [version, source, source, 0, ...bias.flatMap(f32), ...cur.flatMap(f32), ...def.flatMap(f32), ...f32(temp)];
function roundTrip(id, pl) { const out = []; const p = core.createStreamParser({ onBinary: (m) => out.push(m) }); p.push(pack(id, 7, pl)); return out; }
const dec = (id, pl) => core.decodePayload(id, Uint8Array.from(pl));

test("IDs and lengths match the spec", () => {
  assert.equal(core.MSG.VQF_STATUS, 0x0d); assert.equal(core.MSG.VQF_SETTINGS, 0x0e);
  assert.deepEqual([core.CMD.QUERY_VQF_STATUS, core.CMD.START_VQF, core.CMD.CANCEL_VQF, core.CMD.QUERY_VQF, core.CMD.RESTORE_VQF], [0x29, 0x2a, 0x2b, 0x2c, 0x2d]);
  assert.equal(statusPl().length, 28); assert.equal(settingsPl().length, 52);
  const f = core.buildFrame(0x2a, 5, []); assert.deepEqual([...f.slice(0, 5)], [0xaa, 0x55, 0x2a, 0, 5]);
  assert.equal(crc([...f.slice(2, 5)]), f[5] | (f[6] << 8));
});

test("0x0D decode (strict) incl. stream parser round trip", () => {
  const m = dec(0x0d, statusPl({ state: 3, el: 12000, rem: 53000, n: 14000, rate: 0.25, acc: 0.05, temp: 31.5 }));
  assert.equal(m.type, "vqfStatus"); assert.equal(m.state, 3); assert.equal(m.remainingMs, 53000); assert.equal(m.elapsedMs, 12000); assert.equal(m.samples, 14000);
  assert.equal(m.gyroRateDps, Math.fround(0.25)); assert.equal(m.tempC, 31.5);
  const viaParser = roundTrip(0x0d, statusPl({ state: 2 }));
  if (viaParser.length) assert.equal(core.decodePayload(viaParser[0].id, viaParser[0].payload).state, 2);
  assert.equal(dec(0x0d, statusPl().slice(0, 27)).type, "badLength");
  assert.equal(dec(0x0d, [...statusPl(), 0]).type, "badLength");
  assert.equal(dec(0x0d, statusPl({ version: 2 })).type, "unknown");
  assert.equal(dec(0x0d, statusPl({ state: 7 })).type, "unknown");
  assert.equal(dec(0x0d, statusPl({ error: 10 })).type, "unknown");
  assert.equal(dec(0x0d, statusPl({ source: 2 })).type, "unknown");
  assert.equal(dec(0x0d, statusPl({ rate: NaN })).type, "unknown");
  assert.equal(dec(0x0d, statusPl({ temp: Infinity })).type, "unknown");
});

test("0x0E decode: current / defaults only from the frame", () => {
  const m = dec(0x0e, settingsPl({ source: 1, cur: [0.1, 0.04, 0.7, 0.2], def: [0.55, 0.03, 0.65, 0.16], temp: 31.2 }));
  assert.equal(m.type, "vqfSettings"); assert.equal(m.source, 1); assert.equal(m.calValid, 1);
  assert.deepEqual(m.defaults, { sigmaInit: Math.fround(0.55), sigmaRest: Math.fround(0.03), restGyr: Math.fround(0.65), restAcc: Math.fround(0.16) });
  assert.equal(m.current.sigmaInit, Math.fround(0.1)); assert.equal(m.calTempC, Math.fround(31.2));
  assert.equal(dec(0x0e, settingsPl().slice(0, 51)).type, "badLength");
  assert.equal(dec(0x0e, settingsPl({ version: 0 })).type, "unknown");
  assert.equal(dec(0x0e, settingsPl({ source: 2 })).type, "unknown");
  assert.equal(dec(0x0e, settingsPl({ cur: [NaN, 0, 0, 0] })).type, "unknown");
  const src = readFileSync(join(pub, "vqf-init.js"), "utf8");
  assert.ok(!/0\.035|0\.15\b|0\.50?\b(?!\s*s)/.test(src.replace(/\/\/.*$/gm, "")), "no host copy of the default numbers in vqf-init.js code");
});

test("error / state texts are exactly the spec table", () => {
  assert.deepEqual(V.ERROR_TEXT, {
    1: "采集前一直没放稳，或采集中移动了", 2: "陀螺噪声过大", 3: "加速度噪声过大", 4: "零偏超出 ±2 °/s", 5: "60 秒内温度变化达到或超过 2 °C",
    6: "零偏在 60 秒内仍明显漂移", 7: "有效样本不够", 8: "出现非法数值", 9: "校验通过，但 Flash 写入失败。旧记录保留，运行参数不变" });
  assert.deepEqual(V.STATE_TEXT, ["空闲", "等待放稳", "预稳定计时中", "正在采集", "正在检查", "成功", "失败"]);
  assert.deepEqual(V.SOURCE_TEXT, ["默认参数", "静置初始化"]);
  assert.match(core.ackDetailText(0x2f, 3, 0x0703), /0x0703/);
  assert.match(core.ackDetailText(0x2a, 3, 0x0702), /主循环/);
  assert.match(core.ackDetailText(0x2a, 3, 0x0701), /六面校准/);
});

function ctl(opts = {}) {
  const sent = [], said = [], logs = [];
  const c = V.createController({ send: async (cmd, pl, o) => { sent.push({ cmd, quiet: !!o?.quiet }); return opts.sendOk ?? true; }, editable: () => opts.editable ?? true, say: (t) => said.push(t), log: (t) => logs.push(t), changed: () => {} });
  return { c, sent, said, logs };
}
const S = (o) => dec(0x0d, statusPl(o)), E = (o) => dec(0x0e, settingsPl(o));
const flush = () => new Promise((r) => setImmediate(r));

test("query on connect: 0x2C then 0x29; any data frame → supported; 3 s silence → unsupported", async () => {
  mock.timers.enable({ apis: ["setTimeout"] });
  try {
    let { c, sent } = ctl(); await c.query();
    assert.deepEqual(sent.map((s) => s.cmd), [0x2c, 0x29]);
    c.onSettings(E()); mock.timers.tick(3500); assert.equal(c.state.supported, true);
    ({ c } = ctl()); await c.query(); mock.timers.tick(2900); assert.equal(c.state.supported, null); mock.timers.tick(200); assert.equal(c.state.supported, false);
    ({ c } = ctl()); await c.query(); c.onAck(0x2c, 1, 0); assert.equal(c.state.supported, false, "ACK 0x01 → hidden");
    assert.equal(await c.start(), false);
  } finally { mock.timers.reset(); }
});

test("start: only ACK 0 + 0x0D switches to active; failures keep idle with exact detail text", async () => {
  for (const [st, det, re] of [[3, 0x0701, /0x0701/], [3, 0x0702, /主循环还没开始/], [3, 0, /设置模式/], [2, 0, /载荷/]]) {
    const { c, sent } = ctl(); c.onSettings(E()); c.onStatus(S());
    assert.equal(await c.start(), true); assert.equal(sent.at(-1).cmd, 0x2a);
    c.onAck(0x2a, st, det); assert.equal(c.state.pending, null); assert.equal(c.active(), false); assert.match(c.state.message, re); assert.equal(c.state.result.ok, false);
  }
  const { c, said } = ctl(); c.onSettings(E()); c.onStatus(S());
  await c.start(); c.onAck(0x2a, 0, 0); assert.ok(c.state.pending, "ACK alone is not completion"); c.onStatus(S({ state: 1, rem: 65000 }));
  assert.equal(c.state.pending, null); assert.equal(c.active(), true); assert.match(said.at(-1), /^已开始/);
  assert.equal(await c.start(), false, "no second start while active");
});

test("progress uses remaining_ms only; success/failure texts; 0x0E after success", async () => {
  const { c, said } = ctl(); c.onSettings(E()); c.onStatus(S());
  await c.start(); c.onAck(0x2a, 0, 0); c.onStatus(S({ state: 2, rem: 64000 }));
  assert.equal(c.state.collectTotal, 0);
  c.onStatus(S({ state: 3, rem: 59800 })); assert.equal(c.state.collectTotal, 59800);
  c.onStatus(S({ state: 3, rem: 29900 })); assert.equal(c.state.collectTotal, 59800);
  c.onStatus(S({ state: 4 })); c.onStatus(S({ state: 5, source: 1 }));
  assert.equal(c.state.message, "静置初始化成功：已写入 Flash 并立即生效"); assert.equal(said.at(-1), c.state.message); assert.equal(c.state.result.ok, true);
  c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15], temp: 31 })); assert.equal(c.state.settings.source, 1); assert.equal(c.canRestore(), true);
  for (let e = 1; e <= 9; e++) {
    const x = ctl(); x.c.onStatus(S()); await x.c.start(); x.c.onAck(0x2a, 0, 0); x.c.onStatus(S({ state: 3, rem: 30000 })); x.c.onStatus(S({ state: 6, error: e }));
    assert.equal(x.c.state.message, `静置初始化失败：${V.ERROR_TEXT[e]}`); assert.equal(x.c.state.result.ok, false);
  }
});

test("cancel: ACK + 0x0D idle → 已取消，未写 Flash", async () => {
  const { c, sent } = ctl(); c.onSettings(E()); c.onStatus(S()); await c.start(); c.onAck(0x2a, 0, 0); c.onStatus(S({ state: 3, rem: 40000 }));
  assert.equal(await c.cancel(), true); assert.equal(sent.at(-1).cmd, 0x2b); c.onAck(0x2b, 0, 0); c.onStatus(S({ state: 0 }));
  assert.equal(c.active(), false); assert.match(c.state.message, /^已取消：未写 Flash，参数不变/);
});

test("restore: only when source=1, two-click arm, success = source 0 and current == defaults (same frame)", async () => {
  mock.timers.enable({ apis: ["setTimeout"] });
  try {
    let { c, sent } = ctl(); c.onStatus(S()); c.onSettings(E());
    assert.equal(c.canRestore(), false); assert.equal(c.armRestore(), false);
    c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15], temp: 31 }));
    assert.equal(c.armRestore(), true); assert.equal(c.state.restoreArmed, true); assert.equal(sent.length, 0, "arming sends nothing");
    mock.timers.tick(4100); assert.equal(c.state.restoreArmed, false, "arm window 4 s");
    c.armRestore(); await c.restore(); assert.equal(sent.at(-1).cmd, 0x2d);
    c.onAck(0x2d, 0, 0); assert.ok(c.state.pending); c.onSettings(E({ source: 0, def: [0.52, 0.036, 0.61, 0.16], cur: [0.52, 0.036, 0.61, 0.16] }));
    assert.equal(c.state.result.ok, true); assert.match(c.state.message, /^已恢复默认 VQF 参数/);
    // mismatch
    ({ c } = ctl()); c.onStatus(S()); c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15] })); c.armRestore(); await c.restore(); c.onAck(0x2d, 0, 0); c.onSettings(E({ source: 0, cur: [0.1, 0.035, 0.6, 0.15] }));
    assert.equal(c.state.result.ok, false); assert.match(c.state.message, /回读不一致/);
    // Flash failure: ACK 3 + unchanged 0x0E
    ({ c } = ctl()); c.onStatus(S()); c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15] })); c.armRestore(); await c.restore(); c.onAck(0x2d, 3, 0); assert.ok(c.state.pending);
    c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15] })); assert.equal(c.state.result.ok, false); assert.match(c.state.message, /Flash 写入失败.*未变/);
    // busy 0x0703 without 0x0E → fails immediately; and with a late 0x0E → stays failed, settings updated
    ({ c } = ctl()); c.onStatus(S()); c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15] })); c.armRestore(); await c.restore(); c.onAck(0x2d, 3, 0x0703);
    assert.equal(c.state.pending, null); assert.match(c.state.message, /0x0703/);
    c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15] })); assert.match(c.state.message, /0x0703/); assert.equal(c.state.settings.source, 1);
    // no reply at all → 未确认 after 3 s
    ({ c } = ctl()); c.onStatus(S()); c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15] })); c.armRestore(); await c.restore(); mock.timers.tick(3100);
    assert.equal(c.state.pending, null); assert.match(c.state.message, /^未确认/);
    // not while active
    ({ c } = ctl()); c.onSettings(E({ source: 1, cur: [0.1, 0.035, 0.6, 0.15] })); c.onStatus(S({ state: 3, rem: 1000 })); assert.equal(c.canRestore(), false);
  } finally { mock.timers.reset(); }
});

test("tick: quiet 0x29 only while active and stale > 1 s; dropped frames counted", () => {
  const { c, sent } = ctl(); c.onSettings(E()); c.onStatus(S());
  assert.equal(c.tick(Date.now() + 5000), false);
  c.onStatus(S({ state: 3, rem: 30000 })); assert.equal(c.tick(Date.now() + 200), false);
  assert.equal(c.tick(Date.now() + 1500), true); assert.deepEqual(sent.at(-1), { cmd: 0x29, quiet: true });
  c.onDropped(dec(0x0d, statusPl().slice(0, 20))); assert.equal(c.state.dropped, 1);
  c.reset(); assert.equal(c.state.supported, null); assert.equal(c.state.status, null);
});
