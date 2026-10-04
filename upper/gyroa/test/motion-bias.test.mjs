// bias1：VQF 运动零偏状态（GYRO-CORE decodePayload 0x09 + motion-bias.js 控制器）单元测试。node --test test/motion-bias.test.mjs — NOT real hardware.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
const here = dirname(fileURLToPath(import.meta.url));
const pub = process.env.PUBLIC_DIR || join(here, "../dist/public");
vm.runInThisContext(readFileSync(join(pub, "motion-bias.js"), "utf8"));
const B = globalThis.GyroBias;
const appSrc = readFileSync(join(pub, "app.js"), "utf8");
const a = appSrc.indexOf("// ==== GYRO-CORE BEGIN ===="), b = appSrc.indexOf("// ==== GYRO-CORE END ====");
const core = new Function(`"use strict";\n${appSrc.slice(appSrc.indexOf("\n", a) + 1, b)}\nreturn { MSG, CMD, PAYLOAD_LEN, buildFrame, decodePayload, createStreamParser };`)();

function crc(bytes) { let c = 0xffff; for (const x of bytes) { c ^= x << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; }
function pack(id, seq, payload) { const body = [id, payload.length, seq & 255, ...payload]; const c = crc(body); return Uint8Array.from([0xaa, 0x55, ...body, c & 255, c >> 8]); }
const f32 = (x) => [...new Uint8Array(new Float32Array([x]).buffer)];
const fr = Math.fround;
function payload({ version = 1, motion = 1, rest = 1, restDet = 1, tauAcc = 2.5, bias = [0.01, -0.02, 0.003], residual = 0.008, hold = 1, zen = 1, floats = null } = {}) {
  const fl = floats || [0.1, 0.0001, 100, 2.0, 0.035, tauAcc, ...bias, residual];
  return [version, motion, rest, restDet, ...fl.flatMap(f32), hold, zen, 0, 0];
}

test("GYRO-CORE constants", () => {
  assert.equal(core.MSG.MOTION_BIAS, 0x09); assert.equal(core.PAYLOAD_LEN[0x09], 48); assert.equal(core.CMD.QUERY_MOTION_BIAS, 0x30);
  assert.equal(payload().length, 48);
});

test("decode 0x09: every field at its offset", () => {
  const m = core.decodePayload(0x09, Uint8Array.from(payload({ tauAcc: 4, bias: [0.5, -0.25, 0.125], residual: 0.75, restDet: 0, hold: 0, zen: 1 })));
  assert.equal(m.type, "motionBias");
  assert.deepEqual([m.version, m.motionBiasEnabled, m.restBiasEnabled, m.restDetected], [1, 1, 1, false]);
  assert.deepEqual([m.biasSigmaMotion, m.biasVerticalForgettingFactor, m.biasForgettingTime, m.biasClip, m.biasSigmaRest, m.tauAcc], [fr(0.1), fr(0.0001), 100, 2, fr(0.035), 4]);
  assert.deepEqual(m.bias, [0.5, -0.25, 0.125]); assert.equal(m.residualNorm, 0.75);
  assert.deepEqual([m.zaruHold, m.zaruEnabled, m.reserved], [false, true, 0]);
  assert.ok(B.ok(m));
});

test("decode 0x09: length ≠ 48 → badLength; version ≠ 1 / flag > 1 / non-finite → unknown", () => {
  for (const n of [0, 44, 47, 49, 60]) assert.equal(core.decodePayload(0x09, new Uint8Array(n)).type, "badLength", `len ${n}`);
  for (const v of [0, 2]) assert.equal(core.decodePayload(0x09, Uint8Array.from(payload({ version: v }))).type, "unknown");
  assert.equal(core.decodePayload(0x09, Uint8Array.from(payload({ motion: 2 }))).type, "unknown");
  assert.equal(core.decodePayload(0x09, Uint8Array.from(payload({ hold: 5 }))).type, "unknown");
  assert.equal(core.decodePayload(0x09, Uint8Array.from(payload({ residual: NaN }))).type, "unknown");
  assert.equal(core.decodePayload(0x09, Uint8Array.from(payload({ tauAcc: Infinity }))).type, "unknown");
  const msgs = []; const p = core.createStreamParser({ onMessage: (x) => msgs.push(x) });
  p.push(pack(0x09, 1, payload())); p.push(pack(0x09, 2, payload().slice(0, 44)));
  assert.deepEqual(msgs.map((x) => x.type), ["motionBias", "badLength"]);
});

test("success requires version 1 + motion 1 + rest 1", () => {
  const d = (o) => core.decodePayload(0x09, Uint8Array.from(payload(o)));
  assert.equal(B.ok(d({})), true); assert.equal(B.ok(d({ motion: 0 })), false); assert.equal(B.ok(d({ rest: 0 })), false);
  assert.equal(B.ok({ type: "ack" }), false); assert.equal(B.ok(null), false);
});

function rig(reply) {
  const r = { sent: [], logs: [] };
  r.ctl = B.createController({ send: async (cmd, pl, o) => { r.sent.push({ cmd, pl, quiet: !!o?.quiet }); queueMicrotask(() => reply?.(r, cmd, pl)); return true; }, canAuto: () => r.allow ?? true, log: (t) => r.logs.push(t) });
  return r;
}
const tick = () => new Promise((res) => setImmediate(res));

test("controller: 0x09 → ok; disabled flags → not ok; ACK 0 alone never success", async () => {
  const r = rig((r) => r.ctl.onMessage(core.decodePayload(0x09, Uint8Array.from(payload()))));
  await r.ctl.query(); await tick();
  assert.deepEqual(r.sent, [{ cmd: 0x30, pl: [], quiet: false }]);
  assert.equal(r.ctl.state.ok, true); assert.equal(r.ctl.state.supported, true); assert.match(r.ctl.headline(), /^运动零偏已开 · 零偏 0\.010\/-0\.020\/0\.003 °\/s$/);
  r.ctl.onMessage(core.decodePayload(0x09, Uint8Array.from(payload({ motion: 0 }))));
  assert.equal(r.ctl.state.ok, false); assert.match(r.ctl.state.message, /未全部开启/);
  const r2 = rig((r) => r.ctl.onAck(0x30, 0)); await r2.ctl.query(); await tick();
  assert.equal(r2.ctl.state.ok, false); assert.equal(r2.ctl.state.data, null);
});

test("controller: ACK 0x01 → hidden silently, no further queries; ACK 0x02 → message", async () => {
  const r = rig((r) => r.ctl.onAck(0x30, 1)); await r.ctl.query(); await tick();
  assert.equal(r.ctl.state.supported, false); assert.equal(r.ctl.state.message, ""); assert.deepEqual(r.logs, []);
  assert.equal(await r.ctl.query(), false); r.ctl.setAuto(true); assert.equal(r.ctl.tick(1e9), false); assert.equal(r.sent.length, 1);
  const r2 = rig((r) => r.ctl.onAck(0x30, 2)); await r2.ctl.query(); await tick(); assert.match(r2.ctl.state.message, /ACK 0x02/);
  assert.equal(r.ctl.onAck(0x2e, 1), false, "other cmds not consumed");
});

test("auto refresh: only when enabled + gate open, ≤ 2 Hz, one in flight, quiet", async () => {
  const r = rig(); // no reply → stays in flight
  assert.equal(r.ctl.tick(1000), false, "auto off by default");
  r.ctl.setAuto(true); r.allow = false; assert.equal(r.ctl.tick(1000), false, "gate closed");
  r.allow = true; assert.equal(r.ctl.tick(1000), true); assert.equal(r.sent.at(-1).quiet, true);
  assert.equal(r.ctl.tick(2000), false, "one in flight");
  r.ctl.onMessage(core.decodePayload(0x09, Uint8Array.from(payload())));
  assert.equal(r.ctl.tick(1200), false, "≤ 2 Hz"); assert.equal(r.ctl.tick(1500), true);
});

test("dropped frames logged at most 3 times", () => {
  const r = rig(); for (let i = 0; i < 6; i++) r.ctl.onDropped({ type: "badLength", length: 44 });
  assert.equal(r.logs.length, 3); assert.match(r.logs[0], /len=44，应为 48/);
});
