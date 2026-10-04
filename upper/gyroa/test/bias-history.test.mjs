// biashist1：启动零偏历史 0x33 → 0x34（60 B）解码 + 翻页控制器单元测试。node --test test/bias-history.test.mjs（规格 host-agent-bias-history-list.md）。NOT real hardware.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const pub = process.env.PUBLIC_DIR || join(here, "../dist/public");
new Function(readFileSync(join(pub, "bias-history.js"), "utf8")).call(globalThis);
const H = globalThis.GyroBiasHist;
const appSrc = readFileSync(join(pub, "app.js"), "utf8");
const a = appSrc.indexOf("// ==== GYRO-CORE BEGIN ===="), b = appSrc.indexOf("// ==== GYRO-CORE END ====");
const core = new Function(`"use strict";\n${appSrc.slice(appSrc.indexOf("\n", a) + 1, b)}\nreturn { MSG, CMD, decodePayload };`)();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const ent = (i) => [0.01 * i, -0.02, 0.003 * i, 25 + i];
function pl({ version = 1, rv = 3, corrupt = 0, count = 36, offset = 0, n = null, reserved = 0, seq = 129, size = 60, nanAt = -1 } = {}) {
  const u = new Uint8Array(size), d = new DataView(u.buffer); n = n ?? Math.max(0, Math.min(3, count - offset));
  u.set([version, rv, corrupt, count]); d.setUint16(4, offset, true); u[6] = n; u[7] = reserved; d.setUint32(8, seq, true);
  for (let k = 0; k < Math.min(n, 3) && 28 + 16 * k <= size; k++) ent(offset + k).forEach((x, j) => d.setFloat32(12 + 16 * k + 4 * j, k === nanAt ? NaN : x, true));
  return u;
}
test("ids and wiring", () => {
  assert.equal(core.CMD.QUERY_BIAS_HISTORY, 0x33); assert.equal(core.MSG.BIAS_HISTORY, 0x34); assert.equal(H.CMD.QUERY, 0x33); assert.equal(H.MSG.HISTORY, 0x34);
  assert.equal(H.LEN, 60); assert.deepEqual(H.offsetPayload(3), [3, 0]); assert.deepEqual(H.offsetPayload(300), [44, 1]);
  assert.equal(core.MSG.MOTION_BIAS, 0x09, "0x09 stays motion bias");
});
test("valid 0x34 decode (via app decodePayload)", () => {
  const m = core.decodePayload(0x34, pl());
  assert.equal(m.type, "biasHistory"); assert.deepEqual([m.version, m.recordVersion, m.corrupt, m.count, m.offset, m.entryCount, m.sequence], [1, 3, 0, 36, 0, 3, 129]);
  assert.equal(m.entries.length, 3); assert.equal(m.entries[2].index, 2); assert.ok(Math.abs(m.entries[2].bias[0] - 0.02) < 1e-6); assert.ok(Math.abs(m.entries[2].tempC - 27) < 1e-5);
  const last = core.decodePayload(0x34, pl({ offset: 33 })); assert.equal(last.entryCount, 3); assert.equal(last.entries[0].index, 33);
  const e = core.decodePayload(0x34, pl({ rv: 0, count: 0 })); assert.equal(e.type, "biasHistory"); assert.equal(e.entries.length, 0);
  assert.equal(core.decodePayload(0x34, pl({ rv: 2, count: 15, offset: 12 })).recordVersion, 2, "旧 15 条槽");
  assert.equal(core.decodePayload(0x34, pl({ corrupt: 1, count: 4 })).corrupt, 1);
  assert.equal(core.decodePayload(0x34, pl({ count: 50, offset: 48 })).entryCount, 2);
});
test("invalid 0x34 dropped", () => {
  assert.equal(core.decodePayload(0x34, pl({ size: 59 })).type, "badLength");
  assert.equal(core.decodePayload(0x34, pl({ size: 61 })).type, "badLength");
  for (const [o, why] of [[{ version: 2 }, "version"], [{ rv: 1 }, "record_version 1"], [{ rv: 4 }, "record_version 4"], [{ corrupt: 2 }, "corrupt 2"], [{ count: 51 }, "count 51"],
    [{ n: 4, count: 36 }, "entry_count 4"], [{ reserved: 1 }, "reserved"], [{ count: 2, n: 3 }, "entries beyond count"], [{ rv: 0, count: 3 }, "rv 0 with count"], [{ nanAt: 1 }, "NaN"], [{ offset: 51, count: 50, n: 0 }, "offset 51"]])
    assert.equal(core.decodePayload(0x34, pl(o)).type, "unknown", why);
});
function harness(device) {
  const sent = [], logs = []; let changes = 0;
  const ctl = H.createController({ send: async (cmd, payload) => { sent.push({ cmd, payload }); queueMicrotask(() => device(cmd, payload, ctl)); return true; }, log: (t) => logs.push(t), changed: () => changes++ });
  return { ctl, sent, logs, changes: () => changes };
}
const dev = (opts = {}) => { let seq = opts.seq ?? 129, n = 0; return (cmd, p, ctl) => { const off = p.length ? p[0] | (p[1] << 8) : 0; n++;
  if (opts.bumpAt === off && !opts.bumped) { opts.bumped = true; seq++; }
  if (opts.always && n > 1) seq++;
  if (opts.drop === off) return;
  ctl.onMessage(core.decodePayload(0x34, pl({ count: opts.count ?? 36, offset: off, seq, rv: opts.count === 0 ? 0 : 3 }))); }; };
test("pages 0,3,…,33 → 36 entries old→new; first request empty payload", async () => {
  const h = harness(dev()); await h.ctl.read(); await sleep(20);
  assert.equal(h.sent.length, 12); assert.deepEqual(h.sent[0].payload, []); assert.deepEqual(h.sent[1].payload, [3, 0]); assert.deepEqual(h.sent[11].payload, [33, 0]);
  assert.ok(h.sent.every((s) => s.cmd === 0x33));
  assert.equal(h.ctl.state.list.length, 36); assert.deepEqual(h.ctl.state.list.map((e) => e.index), [...Array(36).keys()]);
  assert.equal(h.ctl.state.result, "ok"); assert.equal(h.ctl.state.reading, false); assert.equal(h.ctl.state.supported, true);
  assert.deepEqual(h.ctl.state.meta, { recordVersion: 3, corrupt: 0, count: 36, sequence: 129 }); assert.match(h.ctl.state.message, /已读取 36 条/);
});
test("50 entries → 17 frames; count 0 → one frame, empty list", async () => {
  let h = harness(dev({ count: 50 })); await h.ctl.read(); await sleep(30); assert.equal(h.sent.length, 17); assert.equal(h.ctl.state.list.length, 50);
  h = harness(dev({ count: 0 })); await h.ctl.read(); await sleep(10); assert.equal(h.sent.length, 1); assert.deepEqual(h.ctl.state.list, []); assert.match(h.ctl.state.message, /还没有/);
});
test("sequence change mid-read → full reread from 0", async () => {
  const h = harness(dev({ bumpAt: 9 })); await h.ctl.read(); await sleep(30);
  assert.equal(h.ctl.state.list.length, 36); assert.equal(h.ctl.state.meta.sequence, 130); assert.equal(h.ctl.state.restarts, 1);
  const offs = h.sent.map((s) => (s.payload.length ? s.payload[0] : "e")); assert.deepEqual(offs.slice(0, 5), ["e", 3, 6, 9, 0]); assert.ok(h.logs.some((l) => /整表重读/.test(l)));
});
test("sequence keeps changing → gives up after 3 restarts", async () => {
  const h = harness(dev({ always: true })); await h.ctl.read(); await sleep(30);
  assert.equal(h.ctl.state.result, "fail"); assert.match(h.ctl.state.message, /一直在变化/); assert.equal(h.ctl.state.list, null);
});
test("ACK 0x01 → unsupported/hidden; ACK 0x02 → fail; unrelated cmd ignored", async () => {
  let h = harness((c, p, ctl) => ctl.onAck(0x33, 1)); await h.ctl.read(); await sleep(5);
  assert.equal(h.ctl.state.supported, false); assert.equal(h.ctl.state.reading, false); assert.equal(await h.ctl.read(), false, "no more reads");
  h = harness((c, p, ctl) => ctl.onAck(0x33, 2)); await h.ctl.read(); await sleep(5); assert.equal(h.ctl.state.result, "fail"); assert.match(h.ctl.state.message, /0x02：offset/);
  assert.equal(h.ctl.onAck(0x30, 1), false);
});
test("stale / unrequested frame ignored; dropped frame fails the read", async () => {
  const h = harness(() => {}); await h.ctl.read();
  h.ctl.onMessage(core.decodePayload(0x34, pl({ offset: 6 }))); assert.equal(h.ctl.state.reading, true); assert.ok(h.logs.some((l) => /忽略/.test(l)));
  h.ctl.onDropped({ type: "badLength", length: 59, expected: 60 }); assert.equal(h.ctl.state.result, "fail"); assert.equal(h.ctl.state.reading, false);
  h.ctl.reset(); assert.equal(h.ctl.state.supported, null); assert.equal(h.ctl.state.list, null);
});
