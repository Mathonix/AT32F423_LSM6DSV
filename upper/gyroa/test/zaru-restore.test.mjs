// restore1：零角速保持「恢复默认」0x31（zaru-limits.js）单元测试。node --test test/zaru-restore.test.mjs
// 规格 /workspace/zaru/host-agent-zaru-restore.md；设备端为本文件独立模拟（默认值 = zaru_limits_default() 宏表）。NOT real hardware.
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
  const d = { settings: 0, run: { ...DEF }, saved: { ...DEF }, supported: true, restore: true, wrong: false, savedWrong: false, flag: 1, flashFail: false, dropReply: false, silent: false, sent: [], ...opts };
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
    if (id === 0x31) {
      if (!d.restore) return [d.ack(id, seq, 1)];          // 旧固件：未知命令，只回 ACK 0x01
      if (d.silent) return [];
      let st = 0;
      if (!d.settings) st = 3; else if (pl.length !== 2 || pl[0] > 1 || pl[1] !== 0) st = 2; else if (pl[0] && d.flashFail) st = 3;
      if (st === 0) { d.run = d.wrong ? { ...DEF, enterDps: 0.31 } : { ...DEF }; if (pl[0] && !d.savedWrong) d.saved = { ...DEF }; }
      return d.dropReply ? [d.ack(id, seq, st)] : [d.ack(id, seq, st), d.limits(seq)];
    }
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


const CUSTOM = { enterDps: 0.5, exitDps: 1.2, accDevMs2: 0.4, enterFilterMs: 0, enterConfirmMs: 200, exitConfirmMs: 20 };
async function ready(devOpts = {}, o = {}) {
  const r = rig({ run: { ...CUSTOM }, saved: { ...CUSTOM }, settings: 1, ...devOpts }, o);
  await r.ctl.query(); await tick();
  assert.equal(r.ctl.state.supported, true); assert.ok(Z.same(r.ctl.state.runtime, CUSTOM));
  return r;
}
const restores = (r) => r.dev.sent.filter((x) => x.id === 0x31);

test("0x31 id + payload: persist, reserved 0; DEFAULTS == firmware macro table", () => {
  assert.equal(Z.CMD.RESTORE, 0x31);
  assert.deepEqual(Z.encodeRestore(1), [1, 0]); assert.deepEqual(Z.encodeRestore(0), [0, 0]); assert.deepEqual(Z.encodeRestore(true), [1, 0]);
  assert.equal(Z.encodeRestore(2), null);
  assert.deepEqual({ ...Z.DEFAULTS }, DEF, "verification table = doc table (0.30/0.70/0.15/10/50/3)");
  assert.ok(Z.isDefault(DEF)); assert.ok(!Z.isDefault({ ...DEF, exitConfirmMs: 4 })); assert.ok(Z.isDefault({ ...DEF, enterDps: fr(0.3) }), "f32 compare");
});

test("restore persist=1: ACK 0 + 0x0F (current & saved == defaults) → success; display from readback; no 0x2F", async () => {
  const r = await ready();
  r.fill({ enterDps: "0.9" }); assert.equal(r.ctl.state.dirty, true);
  assert.equal(await r.ctl.restore({ persist: 1 }), true); await tick();
  assert.deepEqual(restores(r).map((x) => x.pl), [[1, 0]]);
  assert.equal(r.writes().length, 0, "never writes defaults via 0x2F");
  const s = r.ctl.state;
  assert.equal(s.pending, null); assert.equal(s.result.ok, true); assert.match(s.message, /已恢复默认阈值（重启后保留），已回读核对/);
  assert.equal(s.dirty, false); assert.deepEqual(s.draft, Z.toDraft(DEF)); assert.ok(Z.same(s.saved, DEF));
  assert.equal(s.restoreSupported, true);
});

test("restore persist=0 (API only): saved unchanged is fine", async () => {
  const r = await ready();
  await r.ctl.restore({ persist: 0 }); await tick();
  assert.equal(r.ctl.state.result.ok, true); assert.match(r.ctl.state.message, /仅本次运行/);
  assert.ok(Z.same(r.ctl.state.saved, CUSTOM)); assert.ok(Z.same(r.ctl.state.runtime, DEF));
});

test("readback not defaults → failure (current / saved), display shows device values", async () => {
  let r = await ready({ wrong: true });
  await r.ctl.restore(); await tick();
  assert.equal(r.ctl.state.result.ok, false); assert.match(r.ctl.state.message, /回读不一致：设备当前值不是固件默认值/);
  assert.equal(r.ctl.state.draft.enterDps, "0.31", "display = readback");
  r = await ready({ savedWrong: true });
  await r.ctl.restore(); await tick();
  assert.equal(r.ctl.state.result.ok, false); assert.match(r.ctl.state.message, /设备已保存值不是固件默认值/);
});

test("old firmware ACK 0x01 → not restored, restore hidden, editor still supported, no re-send", async () => {
  const r = await ready({ restore: false });
  r.fill({ exitConfirmMs: "30" });
  await r.ctl.restore(); await tick();
  const s = r.ctl.state;
  assert.equal(s.result.ok, false); assert.equal(s.message, Z.RESTORE_UNSUPPORTED_TEXT); assert.equal(s.restoreSupported, false);
  assert.equal(s.supported, true, "threshold editor unaffected"); assert.equal(s.draft.exitConfirmMs, "30", "edits kept");
  assert.equal(await r.ctl.restore(), false); assert.equal(restores(r).length, 1, "not sent again");
  assert.equal(r.ctl.armRestore(), false);
  // 0x2F still works
  r.fill({ exitConfirmMs: "30" }); assert.equal(await r.ctl.save(), true); await tick(); assert.equal(r.ctl.state.result.ok, true);
});

test("ACK 0x03 (not in settings / flash fail) and 0x02 → failure; runtime unchanged; edits kept", async () => {
  for (const [opts, re] of [[{ settings: 0 }, /恢复失败（ACK 0x03/], [{ flashFail: true }, /Flash 写入失败/]]) {
    const r = await ready(opts);
    r.fill({ enterDps: "0.6" });
    await r.ctl.restore(); await tick();
    assert.equal(r.ctl.state.result.ok, false); assert.match(r.ctl.state.message, re);
    assert.ok(Z.same(r.ctl.state.runtime, CUSTOM), "runtime from trailing 0x0F unchanged"); assert.equal(r.ctl.state.draft.enterDps, "0.6");
  }
  const r = await ready();
  r.ctl.onAck(0x31, 0, 0); assert.match(r.logs.at(-1), /忽略未请求的恢复默认 ACK/);
  r.ctl.state.pending = { kind: "restore", persist: 1, acked: false }; r.ctl.onAck(0x31, 2, 0);
  assert.match(r.ctl.state.message, /恢复命令参数无效（ACK 0x02）/);
});

test("timeout: no 0x0F after ACK → 未确认, display keeps previous values", async (t) => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const r = await ready({ dropReply: true });
  await r.ctl.restore(); await tick();
  assert.ok(r.ctl.state.pending?.acked);
  t.mock.timers.tick(Z.ACK_TIMEOUT_MS + 1);
  assert.equal(r.ctl.state.result.kind, "unconfirmed"); assert.match(r.ctl.state.message, /未确认：未收到恢复默认的回读，界面保留原值/);
  assert.deepEqual(r.ctl.state.draft, Z.toDraft(CUSTOM)); assert.ok(Z.same(r.ctl.state.runtime, CUSTOM));
});

test("arm → confirm: first click only arms (no frame), expires after RESTORE_CONFIRM_MS; not editable → refused", async (t) => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const r = await ready();
  const n = r.dev.sent.length;
  assert.equal(r.ctl.armRestore(), true); assert.equal(r.ctl.state.restoreArmed, true); assert.equal(r.ctl.state.message, Z.RESTORE_CONFIRM_TEXT);
  assert.equal(r.dev.sent.length, n, "arming sends nothing");
  t.mock.timers.tick(Z.RESTORE_CONFIRM_MS + 1); assert.equal(r.ctl.state.restoreArmed, false); assert.equal(r.ctl.state.message, "");
  r.editable = false;
  assert.equal(await r.ctl.restore(), false); assert.match(r.ctl.state.message, /请先进入设置模式再恢复默认/); assert.equal(restores(r).length, 0);
  // pending save blocks restore
  r.editable = true; r.ctl.state.pending = { kind: "set" }; assert.equal(await r.ctl.restore(), false); assert.equal(r.ctl.armRestore(), false);
});

test("pending save is not finished by restore ACK; reset clears restore state", async () => {
  const r = await ready({ restore: false });
  await r.ctl.restore(); await tick(); assert.equal(r.ctl.state.restoreSupported, false);
  r.ctl.reset(); assert.equal(r.ctl.state.restoreSupported, null); assert.equal(r.ctl.state.restoreArmed, false);
  const r2 = await ready();
  r2.fill({ exitConfirmMs: "30" }); r2.ctl.state.pending = { kind: "set", values: { ...CUSTOM, exitConfirmMs: 30 }, persist: 1, acked: false };
  r2.ctl.onAck(0x31, 0, 0); assert.equal(r2.ctl.state.pending.kind, "set", "foreign ACK ignored");
});
