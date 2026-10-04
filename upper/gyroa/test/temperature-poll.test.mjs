// temp1：10 Hz 温度轮询（createTempPoller，app.js GYRO-CORE）单元测试。node --test test/temperature-poll.test.mjs — NOT real hardware.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const here = dirname(fileURLToPath(import.meta.url));
const pub = process.env.PUBLIC_DIR || join(here, "../dist/public");
const appSrc = readFileSync(join(pub, "app.js"), "utf8");
const a = appSrc.indexOf("// ==== GYRO-CORE BEGIN ===="), b = appSrc.indexOf("// ==== GYRO-CORE END ====");
const core = new Function(`"use strict";\n${appSrc.slice(appSrc.indexOf("\n", a) + 1, b)}\nreturn { MSG, CMD, buildFrame, decodePayload, createStreamParser, createTempPoller, TEMP_POLL };`)();
const { TEMP_POLL: P } = core;

function crc(bytes) { let c = 0xffff; for (const x of bytes) { c ^= x << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; }
function pack(id, seq, payload) { const body = [id, payload.length, seq & 255, ...payload]; const c = crc(body); return Uint8Array.from([0xaa, 0x55, ...body, c & 255, c >> 8]); }
function sysinfo(tempX100) { const p = new Uint8Array(16), v = new DataView(p.buffer); v.setUint32(0, 2000, true); v.setUint32(4, 1000, true); v.setUint16(8, 2, true); v.setInt16(10, tempX100, true); p[12] = 0; p[13] = 1; return p; }

// fake host: clock + seq + recorded sends; device replies are fed manually
function rig({ allow = true, sendOk = true } = {}) {
  const r = { t: 0, seq: 0, sent: [], logs: [], temps: [], allow, sendOk };
  r.p = core.createTempPoller({
    now: () => r.t, canPoll: () => r.allow, nextSeq: () => r.seq & 0xff,
    send: async () => { if (!r.sendOk) return false; r.sent.push({ seq: r.seq++ & 0xff, t: r.t }); return true; },
    onTemp: (temp, at) => r.temps.push([temp, at]), log: (x) => r.logs.push(x),
  });
  r.run = async (ms, step = P.TICK_MS, reply = null) => { const end = r.t + ms; while (r.t < end) { r.p.tick(); await null; if (reply) reply(r); r.t += step; } };
  r.reply = (seq, temp = 31.5) => r.p.onSysinfo({ type: "sysinfo", seq, temp });
  return r;
}

test("SYSINFO 0x05：temp_c_x100 @ offset 10，i16 / 100（负温度、解析器往返）", () => {
  assert.equal(core.CMD.QUERY, 0x14); assert.equal(core.MSG.SYSINFO, 0x05);
  assert.equal(core.decodePayload(0x05, sysinfo(3150)).temp, 31.5);
  assert.equal(core.decodePayload(0x05, sysinfo(-525)).temp, -5.25);
  const got = []; const ps = core.createStreamParser({ onMessage: (m) => got.push(m) }); ps.configure("binary", 3);
  ps.push(pack(0x05, 77, sysinfo(2734)));
  assert.equal(got[0].type, "sysinfo"); assert.equal(got[0].seq, 77); assert.equal(got[0].temp, 27.34);
  assert.deepEqual([...core.buildFrame(core.CMD.QUERY, 5, [])], [...pack(0x14, 5, [])], "request: AA 55 14 00 seq crc");
});

test("10 Hz：每 90~110 ms 一帧，一次只有一个在途", async () => {
  const r = rig();
  await r.run(2000, P.TICK_MS, (r) => { const last = r.sent.at(-1); if (last && r.p.state.inFlight && r.t - last.t >= 20) r.reply(last.seq); });
  assert.ok(r.sent.length >= 18 && r.sent.length <= 21, `sent ${r.sent.length} in 2 s`);
  const gaps = r.sent.slice(1).map((s, i) => s.t - r.sent[i].t);
  assert.ok(gaps.every((g) => g >= 90 && g <= 120), JSON.stringify(gaps));
  assert.equal(r.temps.length, r.sent.length - (r.p.state.inFlight ? 1 : 0));
  // in-flight: no reply → no second request before timeout
  const r2 = rig(); await r2.run(290); assert.equal(r2.sent.length, 1, "one in flight until 300 ms timeout");
});

test("超时：5 次 → 1 Hz；30 次 → 停止 + 恰好一行日志；有效回复恢复 10 Hz", async () => {
  const r = rig();
  await r.run(5 * 400); // 5 timeouts (each 300 ms + up to 100 ms gap)
  assert.equal(r.p.state.misses >= 5, true); assert.equal(r.p.state.mode, "slow");
  const n = r.sent.length; await r.run(3000);
  assert.ok(r.sent.length - n <= 3 + 1, `1 Hz while slow (${r.sent.length - n} in 3 s)`);
  await r.run(40000);
  assert.equal(r.p.state.mode, "stopped"); assert.equal(r.p.state.misses, P.STOP_AFTER);
  assert.equal(r.logs.filter((x) => /温度轮询已停止/.test(x)).length, 1, "one log line");
  const m = r.sent.length; await r.run(3000); assert.equal(r.sent.length, m, "no polls when stopped");
  // a manual / connect 0x14 reply (not a poll seq) recovers
  assert.equal(r.reply(200, 30.25), false, "manual reply is not swallowed");
  assert.equal(r.p.state.mode, "fast"); assert.equal(r.p.state.misses, 0); assert.ok(r.logs.some((x) => /已恢复/.test(x)));
  await r.run(500); assert.ok(r.sent.length > m, "polling again");
});

test("slow 状态收到轮询回复立即回到 10 Hz；迟到的轮询回复仍按轮询静默处理", async () => {
  const r = rig();
  await r.run(6 * 400); assert.equal(r.p.state.mode, "slow");
  const late = r.sent[0].seq;
  assert.equal(r.reply(late, 29.5), true, "late poll reply still quiet");
  assert.equal(r.p.state.mode, "fast"); assert.deepEqual(r.temps.at(-1)[0], 29.5);
});

test("ACK 0x01 → 本次连接停止（reset 后恢复）；ACK 0x02 只记一次；非轮询 seq 的 ACK 不拦截", async () => {
  const r = rig(); await r.run(20);
  assert.equal(r.p.onAck(0x14, 2, r.sent[0].seq), true); await r.run(200);
  assert.equal(r.p.onAck(0x14, 2, r.sent.at(-1).seq), true);
  assert.equal(r.logs.filter((x) => /ACK 0x02/.test(x)).length, 1);
  assert.equal(r.p.onAck(0x14, 1, 250), false, "manual query ACK not swallowed");
  assert.equal(r.p.onAck(0x26, 1, r.sent.at(-1).seq), false, "other command");
  await r.run(150);
  assert.equal(r.p.onAck(0x14, 1, r.sent.at(-1).seq), true); assert.equal(r.p.state.mode, "unsupported");
  const n = r.sent.length; await r.run(2000); assert.equal(r.sent.length, n);
  r.p.reset(); await r.run(50); assert.equal(r.sent.length, n + 1, "reset (new connection) polls again");
});

test("门控：canPoll=false 不发；发送失败清掉在途；onSysinfo 只认轮询 seq", async () => {
  const r = rig({ allow: false }); await r.run(1000); assert.equal(r.sent.length, 0);
  r.allow = true; await r.run(20); assert.equal(r.sent.length, 1);
  const f = rig({ sendOk: false }); await f.run(30); await null; assert.equal(f.p.state.inFlight, null, "failed write → not in flight");
  const g = rig(); await g.run(20); assert.equal(g.reply(g.sent[0].seq + 1), false); assert.ok(g.p.state.inFlight);
  assert.equal(g.reply(g.sent[0].seq, NaN), true); assert.equal(g.temps.length, 0, "non-finite temp ignored");
});
