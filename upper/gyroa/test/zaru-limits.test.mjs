// limits1：零角速保持阈值（zaru-limits.js）协议 / 状态机单元测试。node --test test/zaru-limits.test.mjs
// 设备端为本文件内按 host-agent-zaru-limits.md 独立实现的模拟；帧经 app.js GYRO-CORE 的 buildFrame / createStreamParser 往返。NOT real hardware.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const here = dirname(fileURLToPath(import.meta.url));
const pub = process.env.PUBLIC_DIR || join(here, "../dist/public");
vm.runInThisContext(readFileSync(join(pub, "zaru-limits.js"), "utf8"));
const Z = globalThis.GyroZaru;
const appSrc = readFileSync(join(pub, "app.js"), "utf8");
const a = appSrc.indexOf("// ==== GYRO-CORE BEGIN ===="), b = appSrc.indexOf("// ==== GYRO-CORE END ====");
const core = new Function(`"use strict";\n${appSrc.slice(appSrc.indexOf("\n", a) + 1, b)}\nreturn { MSG, CMD, buildFrame, decodePayload, createStreamParser };`)();

// ---- independent device simulation ----
function crc(bytes) { let c = 0xffff; for (const x of bytes) { c ^= x << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; }
function pack(id, seq, payload) { const body = [id, payload.length, seq & 255, ...payload]; const c = crc(body); return Uint8Array.from([0xaa, 0x55, ...body, c & 255, c >> 8]); }
const u16 = (x) => [x & 255, (x >> 8) & 255];
const f32 = (x) => [...new Uint8Array(new Float32Array([x]).buffer)];
const DEF = { enterDps: 0.3, exitDps: 0.7, accDevMs2: 0.15, enterFilterMs: 10, enterConfirmMs: 50, exitConfirmMs: 3 };
const setBytes = (s) => [...f32(s.enterDps), ...f32(s.exitDps), ...f32(s.accDevMs2), ...u16(s.enterFilterMs), ...u16(s.enterConfirmMs), ...u16(s.exitConfirmMs)];
const limitsPayload = (run = DEF, saved = DEF, { version = 1, supported = 1 } = {}) => [version, supported, 0, 0, ...setBytes(run), ...setBytes(saved)];
const fr = Math.fround;
function makeDevice(opts = {}) {
  const d = { settings: 0, run: { ...DEF }, saved: { ...DEF }, supported: true, flag: 1, flashFail: false, dropReply: false, silent: false, sent: [], ...opts };
  d.ack = (cmd, seq, st) => pack(0x90, seq, [cmd, st, 0, 0]);
  d.limits = (seq) => pack(0x0f, seq, limitsPayload(d.run, d.saved, { supported: d.flag }));
  d.rx = (frame) => {
    const id = frame[2], len = frame[3], seq = frame[4], pl = Uint8Array.from(frame.slice(5, 5 + len));
    assert.equal(crc(frame.slice(2, 5 + len)), frame[5 + len] | (frame[6 + len] << 8), "host CRC16-CCITT LE");
    d.sent.push({ id, seq, pl: [...pl] });
    if (id === 0x17) { d.settings = 1; return [d.ack(id, seq, 0)]; }
    if (id === 0x18) { d.settings = 0; return [d.ack(id, seq, 0)]; }
    if (!d.supported) return [d.ack(id, seq, 1)];
    if (id === 0x2e) return pl.length ? [d.ack(id, seq, 2)] : [d.limits(seq)];
    if (id !== 0x2f) return [d.ack(id, seq, 1)];
    if (d.silent) return [];
    const v = new DataView(pl.buffer);
    let st = 0, next = null;
    if (pl.length !== 20 || pl[19] !== 0 || pl[18] > 1) st = 2;
    else {
      next = { enterDps: v.getFloat32(0, true), exitDps: v.getFloat32(4, true), accDevMs2: v.getFloat32(8, true), enterFilterMs: v.getUint16(12, true), enterConfirmMs: v.getUint16(14, true), exitConfirmMs: v.getUint16(16, true) };
      if (!(next.enterDps >= fr(0.05) && next.enterDps <= fr(2) && next.exitDps > next.enterDps && next.exitDps <= fr(5) && next.accDevMs2 >= fr(0.02) && next.accDevMs2 <= fr(2) && next.enterFilterMs <= 200 && next.enterConfirmMs <= 2000 && next.exitConfirmMs <= 500)) st = 2;
      else if (!d.settings) st = 3;
      else if (pl[18] && d.flashFail) st = 3;
    }
    if (st === 0) { d.run = next; if (pl[18]) d.saved = { ...next }; }
    return d.dropReply ? [d.ack(id, seq, st)] : [d.ack(id, seq, st), d.limits(seq)];
  };
  return d;
}
// host rig: controller + GYRO-CORE parser; device replies delivered async (microtask)
function rig(devOpts = {}, { editable = true } = {}) {
  const dev = makeDevice(devOpts), r = { dev, seq: 0, logs: [], says: [], editable };
  const parser = core.createStreamParser({ onMessage: (m) => {
    if (m.type === "ack") r.ctl.onAck(m.cmd, m.status, m.detail, m.seq);
    else if (m.type === "zaruLimits") r.ctl.onLimits(m);
    else if (m.id === 0x0f) r.ctl.onDropped(m);
  } });
  r.raw = (frame) => parser.push(frame);
  r.ctl = Z.createController({
    send: async (cmd, payload) => { const s = r.seq++ & 255; const f = core.buildFrame(cmd, s, payload); const replies = dev.rx(f); await null; for (const x of replies) parser.push(x); return s; },
    editable: () => r.editable, log: (t) => r.logs.push(t), say: (t) => r.says.push(t),
  });
  r.fill = (vals) => { for (const [k, v] of Object.entries(vals)) r.ctl.setDraft(k, String(v)); };
  r.writes = () => dev.sent.filter((x) => x.id === 0x2f);
  return r;
}
const tick = () => new Promise((res) => setImmediate(res));

test("GYRO-CORE ids and delegation", () => {
  assert.equal(core.MSG.ZARU_LIMITS, 0x0f); assert.equal(core.CMD.QUERY_ZARU, 0x2e); assert.equal(core.CMD.SET_ZARU, 0x2f);
  assert.deepEqual([Z.CMD.QUERY, Z.CMD.SET, Z.MSG.LIMITS, Z.LEN, Z.WRITE_LEN], [0x2e, 0x2f, 0x0f, 40, 20]);
  const m = core.decodePayload(0x0f, Uint8Array.from(limitsPayload()));
  assert.equal(m.type, "zaruLimits"); assert.equal(m.supported, true);
  // filter 0x0B 仍由 filter-profile 路径处理（此处未加载 → unknown），0x0F 不影响其它 id
  assert.equal(core.decodePayload(0x0b, new Uint8Array(16)).type, "unknown");
});

test("encodeWrite: 20 B little-endian layout", () => {
  const p = Z.encodeWrite({ enterDps: 0.5, exitDps: 1.25, accDevMs2: 0.2, enterFilterMs: 200, enterConfirmMs: 2000, exitConfirmMs: 500 }, 1);
  assert.equal(p.length, 20);
  assert.deepEqual(p.slice(0, 4), f32(0.5)); assert.deepEqual(p.slice(4, 8), f32(1.25)); assert.deepEqual(p.slice(8, 12), f32(0.2));
  assert.deepEqual(p.slice(12), [0xc8, 0x00, 0xd0, 0x07, 0xf4, 0x01, 1, 0]);
  assert.deepEqual(Z.encodeWrite(DEF, 0).slice(18), [0, 0]);
  assert.deepEqual(Z.encodeWrite(DEF, true).slice(18), [1, 0]);
  assert.equal(Z.encodeWrite(DEF, 2), null, "persist must be 0/1");
  assert.equal(Z.encodeWrite({ ...DEF, exitDps: 0.3 }, 0), null, "exit == enter refused");
});

test("ranges: boundaries inclusive, exit > enter, integers", () => {
  const ok = (o) => Z.checkSet({ ...DEF, ...o }) === "";
  for (const o of [{ enterDps: 0.05 }, { enterDps: 2, exitDps: 2.01 }, { exitDps: 5 }, { accDevMs2: 0.02 }, { accDevMs2: 2 }, { enterFilterMs: 0 }, { enterFilterMs: 200 },
    { enterConfirmMs: 0 }, { enterConfirmMs: 2000 }, { exitConfirmMs: 0 }, { exitConfirmMs: 500 }, { enterDps: 0.05, exitDps: 0.06 }]) assert.ok(ok(o), JSON.stringify(o));
  for (const o of [{ enterDps: 0.049 }, { enterDps: 2.01, exitDps: 3 }, { exitDps: 5.01 }, { exitDps: 0.3 }, { exitDps: 0.29 }, { accDevMs2: 0.019 }, { accDevMs2: 2.01 },
    { enterFilterMs: 201 }, { enterConfirmMs: 2001 }, { exitConfirmMs: 501 }, { enterFilterMs: 1.5 }, { enterDps: NaN }, { exitDps: Infinity }, { accDevMs2: -Infinity }]) assert.ok(!ok(o), JSON.stringify(o));
  assert.match(Z.checkSet({ ...DEF, exitDps: 0.3 }), /退出阈值必须大于进入阈值/);
});

test("parseForm: text validation, error key, no send needed", () => {
  const t = Object.fromEntries(Object.entries(DEF).map(([k, v]) => [k, String(v)]));
  assert.deepEqual(Z.parseForm(t), { ok: true, values: DEF });
  for (const [k, v, key, re] of [["exitDps", "0.30", "exitDps", /必须大于进入/], ["exitDps", "0.2", "exitDps", /必须大于进入/], ["enterDps", "", "enterDps", /请填写进入/],
    ["enterFilterMs", "10.5", "enterFilterMs", /整数/], ["enterConfirmMs", "-1", "enterConfirmMs", /整数/], ["accDevMs2", "abc", "accDevMs2", /数字/],
    ["exitConfirmMs", "600", "exitConfirmMs", /0～500/], ["enterDps", "1e-1", "enterDps", /数字/], ["exitDps", "5.5", "exitDps", /≤ 5.00/]]) {
    const r = Z.parseForm({ ...t, [k]: v }); assert.equal(r.ok, false, `${k}=${v}`); assert.equal(r.key, key, `${k}=${v} key`); assert.match(r.error, re);
  }
});

test("decode 0x0F: valid, supported=0, version / length / nonfinite / range drop", () => {
  const run = { enterDps: 0.45, exitDps: 1.1, accDevMs2: 0.3, enterFilterMs: 0, enterConfirmMs: 2000, exitConfirmMs: 500 };
  const m = Z.decode(0x0f, Uint8Array.from(limitsPayload(run, DEF)));
  assert.equal(m.type, "zaruLimits"); assert.equal(m.version, 1);
  assert.ok(Z.same(m.runtime, run)); assert.ok(Z.same(m.saved, DEF));
  assert.equal(m.runtime.enterDps, fr(0.45), "f32 value");
  assert.equal(m.runtime.exitConfirmMs, 500);
  // 偏移：当前 @4，已保存 @22
  const p = limitsPayload(DEF, run); assert.deepEqual(p.slice(22, 26), f32(0.45)); assert.deepEqual(p.slice(38, 40), u16(500));
  assert.deepEqual(Z.decode(0x0f, Uint8Array.from(limitsPayload(DEF, DEF, { supported: 0 }))), { type: "zaruLimits", version: 1, supported: false, reserved: 0, runtime: null, saved: null });
  const zeros = new Uint8Array(40); zeros[0] = 1; assert.equal(Z.decode(0x0f, zeros).supported, false, "supported=0 with zero values accepted (not range-checked)");
  for (const v of [0, 2, 255]) assert.equal(Z.decode(0x0f, Uint8Array.from(limitsPayload(DEF, DEF, { version: v }))).type, "unknown", `version ${v}`);
  assert.equal(Z.decode(0x0f, Uint8Array.from(limitsPayload(DEF, DEF, { supported: 2 }))).type, "unknown", "supported 2");
  for (const n of [0, 39, 41, 20]) assert.equal(Z.decode(0x0f, new Uint8Array(n)).type, "badLength", `len ${n}`);
  for (const bad of [{ enterDps: NaN }, { exitDps: Infinity }, { accDevMs2: -Infinity }, { enterDps: 3 }, { exitDps: 0.2 }, { enterFilterMs: 201 }, { exitConfirmMs: 65535 }]) {
    assert.equal(Z.decode(0x0f, Uint8Array.from(limitsPayload({ ...DEF, ...bad }, DEF))).type, "unknown", `runtime ${JSON.stringify(bad)}`);
    assert.equal(Z.decode(0x0f, Uint8Array.from(limitsPayload(DEF, { ...DEF, ...bad }))).type, "unknown", `saved ${JSON.stringify(bad)}`);
  }
  // 经 GYRO-CORE 解析器：长度不符计入 badLengthFrames，版本错计入 unknownFrames
  const msgs = []; const parser = core.createStreamParser({ onMessage: (x) => msgs.push(x) });
  parser.push(pack(0x0f, 7, limitsPayload())); parser.push(pack(0x0f, 8, limitsPayload().slice(0, 39))); parser.push(pack(0x0f, 9, limitsPayload(DEF, DEF, { version: 2 })));
  assert.deepEqual(msgs.map((x) => [x.type, x.seq]), [["zaruLimits", 7], ["badLength", 8], ["unknown", 9]]);
});

test("same(): f32 rounding compare", () => {
  assert.ok(Z.same({ ...DEF, enterDps: fr(0.3) }, DEF));
  assert.ok(!Z.same({ ...DEF, enterDps: 0.30000001 * 1.0001 }, DEF));
  assert.ok(!Z.same({ ...DEF, exitConfirmMs: 4 }, DEF));
  assert.ok(!Z.same(null, DEF));
});

test("connect query loads form; passive readback keeps dirty edits; 读取 overwrites", async () => {
  const r = rig({ run: { ...DEF, enterDps: 0.4 } });
  await r.ctl.query(); await tick();
  assert.equal(r.ctl.state.supported, true); assert.equal(r.ctl.state.draft.enterDps, "0.4"); assert.equal(r.ctl.state.draft.exitConfirmMs, "3");
  assert.deepEqual(r.dev.sent.map((x) => [x.id, x.pl.length]), [[0x2e, 0]]);
  r.ctl.setDraft("enterDps", "0.5"); assert.equal(r.ctl.state.dirty, true);
  await r.ctl.query(); await tick(); assert.equal(r.ctl.state.draft.enterDps, "0.5", "passive read keeps edit");
  await r.ctl.query({ load: true }); await tick(); assert.equal(r.ctl.state.draft.enterDps, "0.4", "explicit read loads device"); assert.equal(r.ctl.state.dirty, false);
});

test("write persist=0 and persist=1: ACK 0 + 0x0F readback match", async () => {
  const r = rig({}, { editable: true });
  await r.ctl.query(); await tick(); r.dev.settings = 1;
  const v1 = { enterDps: 0.12, exitDps: 0.9, accDevMs2: 0.33, enterFilterMs: 25, enterConfirmMs: 120, exitConfirmMs: 7 };
  r.fill(v1); r.ctl.setPersist(false);
  assert.equal(await r.ctl.save(), true); await tick();
  assert.equal(r.ctl.state.pending, null); assert.equal(r.ctl.state.result.ok, true); assert.match(r.ctl.state.message, /仅本次运行/);
  assert.ok(Z.same(r.dev.run, v1)); assert.ok(Z.same(r.dev.saved, DEF), "persist 0 does not touch saved");
  assert.deepEqual(r.writes().at(-1).pl.slice(18), [0, 0]);
  const v2 = { ...v1, exitDps: 4.99, enterConfirmMs: 2000 };
  r.fill(v2); r.ctl.setPersist(true); await r.ctl.save(); await tick();
  assert.equal(r.ctl.state.result.ok, true); assert.match(r.ctl.state.message, /已保存（重启后保留）/);
  assert.ok(Z.same(r.dev.saved, v2)); assert.ok(Z.same(r.ctl.state.saved, v2)); assert.equal(r.ctl.state.dirty, false);
  assert.deepEqual(r.writes().at(-1).pl.slice(18), [1, 0]);
  assert.ok(!r.dev.sent.some((x) => [0x26, 0x27, 0x19, 0x1e, 0x15].includes(x.id)), "no filter/mode/startup/reset commands");
});

test("invalid form (exit <= enter, range) → message, zero writes", async () => {
  const r = rig(); await r.ctl.query(); await tick();
  r.fill({ exitDps: "0.30" }); assert.equal(await r.ctl.save(), false); assert.match(r.ctl.state.message, /退出阈值必须大于进入阈值/); assert.equal(r.ctl.state.invalidKey, "exitDps");
  r.fill({ exitDps: "0.7", enterFilterMs: "201" }); assert.equal(await r.ctl.save(), false); assert.match(r.ctl.state.message, /0～200/);
  assert.equal(r.writes().length, 0);
});

test("not in settings mode: host refuses (no send); device-side ACK 0x03 → failure, runtime unchanged", async () => {
  const r = rig({}, { editable: false }); await r.ctl.query(); await tick();
  r.fill({ enterDps: "0.2" }); assert.equal(await r.ctl.save(), false); assert.match(r.ctl.state.message, /设置模式/); assert.equal(r.writes().length, 0);
  r.editable = true; // host thinks settings, device does not
  assert.equal(await r.ctl.save(), true); await tick();
  assert.equal(r.ctl.state.result.ok, false); assert.match(r.ctl.state.message, /ACK 0x03/); assert.equal(r.ctl.state.draft.enterDps, "0.2", "edits kept");
  assert.ok(Z.same(r.ctl.state.runtime, DEF), "runtime from trailing 0x0F unchanged");
});

test("flash failure (persist=1) → ACK 0x03 error, runtime unchanged, edits kept", async () => {
  const r = rig({ flashFail: true }); r.dev.settings = 1; await r.ctl.query(); await tick();
  r.fill({ enterDps: "0.25", exitDps: "0.8" }); r.ctl.setPersist(true); await r.ctl.save(); await tick();
  assert.equal(r.ctl.state.result.ok, false); assert.match(r.ctl.state.message, /Flash 写入失败/); assert.match(r.ctl.state.message, /保持原值/);
  assert.ok(Z.same(r.ctl.state.runtime, DEF)); assert.ok(Z.same(r.dev.run, DEF)); assert.equal(r.ctl.state.draft.exitDps, "0.8"); assert.equal(r.ctl.state.dirty, true);
});

test("old firmware ACK 0x01 for 0x2E and 0x2F → unsupported, never success", async () => {
  const r = rig({ supported: false }); await r.ctl.query(); await tick();
  assert.equal(r.ctl.state.supported, false); assert.equal(r.ctl.state.unsupportedBy, "ack");
  assert.equal(await r.ctl.save(), false, "no save when unsupported"); assert.equal(r.writes().length, 0);
  // 已认为支持但写入时收到 0x01
  const r2 = rig(); r2.dev.settings = 1; await r2.ctl.query(); await tick(); r2.dev.supported = false;
  r2.fill({ enterDps: "0.2" }); await r2.ctl.save(); await tick();
  assert.equal(r2.ctl.state.supported, false); assert.equal(r2.ctl.state.result.ok, false); assert.equal(r2.ctl.state.pending, null);
});

test("supported=0 → read-only, save refused", async () => {
  const r = rig({ flag: 0 }); r.dev.settings = 1; await r.ctl.query(); await tick();
  assert.equal(r.ctl.state.supported, false); assert.equal(r.ctl.state.unsupportedBy, "flag"); assert.equal(r.ctl.state.runtime, null);
  assert.equal(await r.ctl.save(), false); assert.equal(r.writes().length, 0);
});

test("ACK 0 without 0x0F → timeout 未确认, edits kept; malformed 0x0F never counts as success", async (t) => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const r = rig({ dropReply: true }); r.dev.settings = 1; await r.ctl.query(); await tick();
  r.fill({ enterDps: "0.2" }); await r.ctl.save(); await tick();
  assert.equal(r.ctl.state.pending.acked, true);
  // 版本错 / 长度错 / NaN 的 0x0F：丢弃
  r.raw(pack(0x0f, 1, limitsPayload({ ...DEF, enterDps: 0.2 }, DEF, { version: 2 })));
  r.raw(pack(0x0f, 2, limitsPayload({ ...DEF, enterDps: 0.2 }).slice(0, 38)));
  r.raw(pack(0x0f, 3, limitsPayload({ ...DEF, enterDps: NaN })));
  assert.ok(r.ctl.state.pending, "still pending"); assert.equal(r.ctl.state.dropped, 3);
  t.mock.timers.tick(Z.ACK_TIMEOUT_MS + 1);
  assert.equal(r.ctl.state.pending, null); assert.equal(r.ctl.state.result.ok, false); assert.equal(r.ctl.state.result.kind, "unconfirmed");
  assert.match(r.ctl.state.message, /未确认/); assert.equal(r.ctl.state.draft.enterDps, "0.2"); assert.equal(r.ctl.state.dirty, true);
});

test("readback mismatch → failure; ACK 0x02 → failure; 0x0F before ACK not used as confirmation", async () => {
  const r = rig(); r.dev.settings = 1; await r.ctl.query(); await tick();
  const orig = r.dev.rx; r.dev.rx = (f) => { const out = orig(f); if (f[2] === 0x2f) { r.dev.run = { ...DEF }; return [out[0], r.dev.limits(f[4])]; } return out; }; // ACK 0 但回读旧值
  r.fill({ enterDps: "0.2" }); await r.ctl.save(); await tick();
  assert.equal(r.ctl.state.result.ok, false); assert.match(r.ctl.state.message, /回读不一致/);
  r.dev.rx = orig;
  // 写入中、ACK 之前先到一帧匹配的 0x0F（例如更早查询的迟到回复）：不能当作成功
  const r2 = rig({ silent: true }); r2.dev.settings = 1; await r2.ctl.query(); await tick();
  r2.fill({ enterDps: "0.2" }); await r2.ctl.save(); await tick();
  r2.raw(pack(0x0f, 9, limitsPayload({ ...DEF, enterDps: 0.2 }, DEF)));
  assert.ok(r2.ctl.state.pending, "0x0F without ACK is not success"); assert.equal(r2.ctl.state.result, null);
  r2.raw(pack(0x90, 10, [0x2f, 2, 0, 0]));
  assert.equal(r2.ctl.state.result.ok, false); assert.match(r2.ctl.state.message, /ACK 0x02/);
});

test("query blocked while pending; ACK for unrelated cmd not consumed", async () => {
  const r = rig({ silent: true }); r.dev.settings = 1; await r.ctl.query(); await tick();
  r.fill({ enterDps: "0.2" }); await r.ctl.save();
  assert.equal(await r.ctl.query(), false);
  assert.equal(r.ctl.onAck(0x27, 0, 0), false); assert.equal(r.ctl.onAck(0x14, 1, 0), false);
});
