// 姿态稳定性（filter-profile.js）协议 / 状态机单元测试。node --test test/filter-profile.test.mjs
// 设备端为本文件内按配套固件规格独立实现的模拟（不复用被测代码），帧经 app.js GYRO-CORE 的 buildFrame / createStreamParser 往返。NOT real hardware.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const here = dirname(fileURLToPath(import.meta.url));
const pub = process.env.PUBLIC_DIR || join(here, "../dist/public");
vm.runInThisContext(readFileSync(join(pub, "filter-profile.js"), "utf8"));
const F = globalThis.GyroFilter;
const appSrc = readFileSync(join(pub, "app.js"), "utf8");
const a = appSrc.indexOf("// ==== GYRO-CORE BEGIN ===="), b = appSrc.indexOf("// ==== GYRO-CORE END ====");
const core = new Function(`"use strict";\n${appSrc.slice(appSrc.indexOf("\n", a) + 1, b)}\nreturn { MSG, CMD, buildFrame, crc16, decodePayload, createStreamParser };`)();

// ---- independent device simulation (firmware spec) ----
function crc(bytes) { let c = 0xffff; for (const x of bytes) { c ^= x << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; }
function pack(id, seq, payload) { const body = [id, payload.length, seq & 255, ...payload]; const c = crc(body); return Uint8Array.from([0xaa, 0x55, ...body, c & 255, c >> 8]); }
const u16 = (x) => [x & 255, x >> 8];
const f32 = (x) => [...new Uint8Array(new Float32Array([x]).buffer)];
const TAU = [[2, 0.15], [4, 0.5], [6, 1.5], [4, 0.5]];
// caps = 0x0B capabilities（档位数）：4 新固件；3 旧固件（0x27 profile>=3 → ACK 0x02）
function makeDevice(opts = {}) {
  const d = { settings: 0, active: 0, saved: 0, caps: 4, supported: true, noApply: false, sent: [], ...opts };
  d.cfg = (seq) => pack(0x0b, seq, [1, d.active, d.saved, d.caps, ...u16(1000), 0, 0, ...f32(TAU[d.active][0]), ...f32(TAU[d.active][1])]);
  d.ack = (cmd, seq, st, det) => pack(0x90, seq, [cmd, st, ...u16(det)]);
  d.rx = (frame) => {
    assert.equal(frame[0], 0xaa); assert.equal(frame[1], 0x55);
    const id = frame[2], len = frame[3], seq = frame[4], pl = [...frame.slice(5, 5 + len)];
    assert.equal(crc(frame.slice(2, 5 + len)), frame[5 + len] | (frame[6 + len] << 8), "host CRC16-CCITT LE");
    d.sent.push({ id, seq, pl });
    if (id === 0x17) { d.settings = 1; return [d.ack(id, seq, 0, 0)]; }
    if (id === 0x18) { d.settings = 0; return [d.ack(id, seq, 0, 0)]; }
    if (!d.supported) return [d.ack(id, seq, 1, 0)];
    if (id === 0x26) return pl.length ? [d.ack(id, seq, 2, d.active)] : [d.cfg(seq)];
    if (id === 0x27) {
      let st = pl.length !== 2 || pl[0] > 3 || pl[0] >= d.caps || pl[1] > 1 ? 2 : !d.settings ? 3 : 0;
      if (d.forceStatus !== undefined) st = d.forceStatus;
      if (st === 0 && !d.noApply) { d.active = pl[0]; if (pl[1]) d.saved = pl[0]; }
      return [d.ack(id, seq, st, d.active), d.cfg(seq)];
    }
    if (id === 0x28) return pl.length ? [d.ack(id, seq, 2, 0)] : [pack(0x0c, seq, [1, d.active, 1, 0, 0, 0, 0, 0, ...Array(13).fill(0.5).flatMap(f32)])];
    return [d.ack(id, seq, 1, 0)];
  };
  d.reboot = () => { d.active = d.saved; d.settings = 0; };
  return d;
}
// host side: controller + real parser; the device replies asynchronously (like the serial link)
function makeHost(dev) {
  let seq = 0; const said = []; const logs = []; const messages = [];
  const tick = () => new Promise((r) => setTimeout(r, 1));
  const parser = core.createStreamParser({ onMessage: (m) => {
    messages.push(m);
    if (m.type === "ack") ctl.onAck(m.cmd, m.status, m.detail, m.seq);
    if (m.type === "filterConfig") ctl.onConfig(m);
    if (m.type === "filterDiag") ctl.onDiag(m);
  } });
  parser.configure("binary", 3);
  const raw = async (cmd, payload = []) => { const s = seq++ & 255; const replies = dev.rx(core.buildFrame(cmd, s, payload)); await tick(); for (const r of replies) parser.push(r); return s; };
  const ctl = F.createController({ send: async (cmd, payload) => { const s = seq++ & 255; const replies = dev.rx(core.buildFrame(cmd, s, payload)); setTimeout(() => replies.forEach((r) => parser.push(r)), 1); return s; },
    say: (t) => said.push(t), log: (t) => logs.push(t) });
  const settle = async (ms = 15) => { await new Promise((r) => setTimeout(r, ms)); };
  return { ctl, said, logs, messages, raw, settle, parser };
}

test("PROFILES：四档名称（第 4 档零角速保持）、默认均衡、说明文案不含禁用句", () => {
  assert.deepEqual(F.PROFILES.map((p) => p.name), ["响应优先", "均衡", "静态稳定", "零角速保持"]);
  assert.deepEqual(F.PROFILES.map((p) => p.id), [0, 1, 2, 3]);
  assert.deepEqual(F.PROFILES.map((p) => [p.magS, p.restS, p.motionS]), [[2, 0.15, 0.04], [4, 0.5, 0.1], [6, 1.5, 0.2], [4, 0.5, 0.1]]);
  assert.equal(F.PROFILES[3].en, "ZARU / Stationary Heading Hold");
  for (const t of ["零角速保持：", "静止时锁定航向，检测到运动后立即恢复更新", "滤波参数与均衡相同", "仅六轴锁定航向，九轴不锁磁力计航向"]) assert.ok(F.PROFILES[3].hint.includes(t), t);
  assert.ok(F.PROFILES[3].hintNineAxis.includes("本档不锁定航向"));
  assert.equal(F.DEFAULT_PROFILE, 1);
  const text = F.PROFILES.map((p) => p.hint).join("") + F.NOTE;
  for (const banned of ["消除漂移", "转速上限"]) assert.ok(!text.includes(banned), banned);
  assert.match(F.PROFILES[0].hint, /较少静态平滑/); assert.match(F.PROFILES[1].hint, /兼顾/); assert.match(F.PROFILES[2].hint, /更强平滑，停转后的修正更慢/);
  assert.equal(F.UNSUPPORTED_TEXT, "当前固件不支持滤波模式，请升级配套固件");
});

test("encodeSet：profile 0..3（且 < 档位数）、persist 0/1，其余拒绝", () => {
  assert.deepEqual(F.encodeSet(0, 0), [0, 0]); assert.deepEqual(F.encodeSet(2, true), [2, 1]);
  assert.deepEqual(F.encodeSet(3, 1, 4), [3, 1], "caps 4: ZARU encodable");
  assert.equal(F.encodeSet(3, 1, 3), null, "caps 3 (old fw): profile 3 never encoded");
  assert.deepEqual(F.encodeSet(2, 0, 3), [2, 0]);
  for (const [p, s] of [[4, 0], [-1, 0], [1.5, 0], [1, 2], [1, "1"]]) assert.equal(F.encodeSet(p, s, 4), null);
});

test("0x0B 解码：16 字节，capabilities=档位数，长度不符 badLength", () => {
  const pl = pack(0x0b, 7, [1, 0, 1, 3, ...u16(1000), 0, 0, ...f32(2), ...f32(0.15)]).slice(5, 21);
  const m = F.decode(0x0b, pl);
  assert.deepEqual({ ...m, tauMag: +m.tauMag.toFixed(3), restTau: +m.restTau.toFixed(3) },
    { type: "filterConfig", version: 1, active: 0, saved: 1, profileCount: 3, estimatorHz: 1000, reserved: 0, tauMag: 2, restTau: 0.15 });
  assert.deepEqual(F.decode(0x0b, pl.slice(0, 15)), { type: "badLength", id: 0x0b, length: 15, expected: 16 });
  assert.equal(F.decode(0x0b, Uint8Array.from([2, ...pl.slice(1)])).type, "unknown", "version != 1");
  assert.equal(F.decode(0x0b, Uint8Array.from([1, 3, 0, 3, ...pl.slice(4)])).type, "unknown", "active >= count");
  // 旧固件帧 [1,2,1,3] 仍合法
  const old = F.decode(0x0b, Uint8Array.from([1, 2, 1, 3, ...pl.slice(4)]));
  assert.equal(old.type, "filterConfig"); assert.deepEqual([old.active, old.saved, old.profileCount], [2, 1, 3]);
  // profile 4 → unknown（caps 4 时也不行）
  for (const b of [[1, 4, 1, 4], [1, 1, 4, 4], [1, 4, 0, 3]]) assert.equal(F.decode(0x0b, Uint8Array.from([...b, ...pl.slice(4)])).type, "unknown", String(b));
  // capabilities 只能是 3 或 4
  for (const c of [0, 1, 2, 5, 255]) assert.equal(F.decode(0x0b, Uint8Array.from([1, 0, 0, c, ...pl.slice(4)])).type, "unknown", "caps " + c);
  // estimator_hz 必须 1000，reserved 必须 0，tau 必须是有限浮点
  assert.equal(F.decode(0x0b, Uint8Array.from([1, 0, 0, 4, ...u16(500), 0, 0, ...f32(2), ...f32(0.15)])).type, "unknown", "hz");
  assert.equal(F.decode(0x0b, Uint8Array.from([1, 0, 0, 4, ...u16(1000), 1, 0, ...f32(2), ...f32(0.15)])).type, "unknown", "reserved");
  assert.equal(F.decode(0x0b, Uint8Array.from([1, 0, 0, 4, ...u16(1000), 0, 0, ...f32(NaN), ...f32(0.15)])).type, "unknown", "tau NaN");
  // profile 3 + caps 4 + tauMag 4 + restTau 0.5
  const z = F.decode(0x0b, Uint8Array.from([1, 3, 3, 4, ...u16(1000), 0, 0, ...f32(4), ...f32(0.5)]));
  assert.deepEqual(z, { type: "filterConfig", version: 1, active: 3, saved: 3, profileCount: 4, estimatorHz: 1000, reserved: 0, tauMag: 4, restTau: 0.5 });
});

test("0x0C 解码：60 字节 = 4 + 4 + 13*4；mag_flags bit0 ready / bit1 interference", () => {
  const body = [1, 2, 1, 0b10, 0x40, 0xe2, 1, 0, ...[0.1, 0.2, 0.3, 1, 2, 3, 0.01, 0.02, 0.03, 10, 20, 30, 0.5].flatMap(f32)];
  assert.equal(body.length, 60);
  const m = F.decode(0x0c, Uint8Array.from(body));
  assert.equal(m.type, "filterDiag"); assert.equal(m.profile, 2); assert.equal(m.rest, true);
  assert.equal(m.magReady, false, "6-axis: bit0 = 0"); assert.equal(m.magInterference, true);
  assert.equal(m.timestampMs, 123456);
  assert.deepEqual(m.rawEuler.map((x) => +x.toFixed(3)), [10, 20, 30]); assert.equal(+m.biasSigma.toFixed(3), 0.5);
  assert.deepEqual(F.decode(0x0c, Uint8Array.from(body.slice(0, 59))).type, "badLength");
  const d3 = F.decode(0x0c, Uint8Array.from([1, 3, ...body.slice(2)])); assert.equal(d3.type, "filterDiag"); assert.equal(d3.profile, 3);
  assert.equal(F.decode(0x0c, Uint8Array.from([1, 4, ...body.slice(2)])).type, "unknown", "diag profile 4");
  assert.equal(F.decode(0x0c, Uint8Array.from([1, 1, 1, 3, ...body.slice(4)])).magFlags, 3, "mag flags 后续字段范围不变");
});

test("app.js decodePayload 通过 GyroFilter 解码 0x0B/0x0C（解析器往返，seq 保留）", () => {
  const got = [];
  const p = core.createStreamParser({ onMessage: (m) => got.push(m) }); p.configure("binary", 3);
  p.push(pack(0x0b, 9, [1, 0, 0, 3, ...u16(1000), 0, 0, ...f32(2), ...f32(0.15)]));
  p.push(pack(0x0b, 10, [1, 0, 0, 3, ...u16(1000), 0, 0, ...f32(2)]));
  assert.equal(got[0].type, "filterConfig"); assert.equal(got[0].seq, 9);
  assert.equal(got[1].type, "badLength"); assert.equal(p.stats.badLengthFrames, 1);
  assert.equal(core.MSG.FILTER_CONFIG, 0x0b); assert.equal(core.CMD.SET_FILTER, 0x27); assert.equal(core.CMD.QUERY_FILTER, 0x26);
});

test("查询成功只回 0x0B（无 ACK），状态行显示当前 / 已保存 / 内部 1000 Hz", async () => {
  const dev = makeDevice(); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  assert.deepEqual(h.messages.map((m) => m.type), ["filterConfig"]);
  assert.equal(h.ctl.state.supported, true);
  assert.equal(h.ctl.statusText(), "当前：响应优先 · 已保存：响应优先 · 内部 1000 Hz");
  assert.equal(h.ctl.state.draft.profile, 0, "readback fills the form when not dirty");
});

test("查询带 payload → ACK 0x02（不改变状态）", async () => {
  const dev = makeDevice(); const h = makeHost(dev);
  await h.raw(0x26, [0]); await h.settle();
  assert.deepEqual(h.messages.map((m) => [m.type, m.cmd, m.status]), [["ack", 0x26, 2]]);
  assert.match(h.ctl.state.message, /参数无效/);
  assert.notEqual(h.ctl.state.supported, false);
});

test("未进入设置模式 → ACK 0x03 + 0x0B，不算成功，草稿保留", async () => {
  const dev = makeDevice(); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  h.ctl.setDraft(2, true); await h.ctl.apply(); await h.settle(30);
  assert.equal(h.ctl.state.pending, null); assert.equal(h.ctl.state.result.ok, false);
  assert.match(h.ctl.state.result.text, /未进入设置模式/);
  assert.equal(h.ctl.state.dirty, true); assert.equal(h.ctl.state.draft.profile, 2, "draft kept after failure + 0x0B");
  assert.equal(h.ctl.state.config.active, 0);
  assert.ok(!h.said.some((t) => /已切换/.test(t)));
});

test("参数无效 → ACK 0x02 + 0x0B，不算成功", async () => {
  const dev = makeDevice({ settings: 1 }); const h = makeHost(dev);
  await h.raw(0x27, [5, 0]); await h.settle();
  assert.deepEqual(h.messages.filter((m) => m.type === "ack").map((m) => m.status), [2]);
  // controller path with a forced invalid status
  dev.forceStatus = 2; h.ctl.setDraft(1, false); await h.ctl.apply(); await h.settle(30);
  assert.equal(h.ctl.state.result.ok, false); assert.match(h.ctl.state.result.text, /参数无效/);
  assert.equal(dev.active, 0);
});

test("成功：ACK 0x00 + 同 seq 0x0B，再查询 0x26 核对 active（persist=0 仅本次运行，重启恢复）", async () => {
  const dev = makeDevice({ settings: 1 }); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  h.ctl.setDraft(2, false); await h.ctl.apply(); await h.settle(40);
  const set = dev.sent.find((s) => s.id === 0x27);
  assert.deepEqual(set.pl, [2, 0]);
  const afterSet = h.messages.filter((m) => m.seq === set.seq).map((m) => m.type);
  assert.deepEqual(afterSet, ["ack", "filterConfig"], "ACK then 0x0B with the same seq");
  const ids = dev.sent.map((s) => s.id);
  assert.equal(ids.lastIndexOf(0x26) > ids.indexOf(0x27), true, "re-query 0x26 after ACK");
  assert.equal(h.ctl.state.result.ok, true); assert.equal(h.ctl.state.dirty, false);
  assert.match(h.ctl.state.result.text, /仅本次运行，重启后恢复为已保存的 响应优先/);
  assert.equal(h.ctl.statusText(), "当前：静态稳定 · 已保存：响应优先 · 内部 1000 Hz");
  dev.reboot(); h.ctl.reset(); await h.ctl.query(); await h.settle();
  assert.equal(h.ctl.state.config.active, 0, "runtime-only profile reverts after reboot");
});

test("成功 persist=1：读回 active 与 saved 都等于请求档位", async () => {
  const dev = makeDevice({ settings: 1 }); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  h.ctl.setDraft(1, true); await h.ctl.apply(); await h.settle(40);
  assert.equal(h.ctl.state.result.ok, true); assert.match(h.ctl.state.result.text, /已保存（重启后保留）/);
  assert.deepEqual([h.ctl.state.config.active, h.ctl.state.config.saved], [1, 1]);
  dev.reboot(); h.ctl.reset(); await h.ctl.query(); await h.settle();
  assert.equal(h.ctl.state.config.active, 1);
});

test("ACK 成功但回读不一致 → 失败（不以 ACK 或 0x0B 出现当作完成）", async () => {
  const dev = makeDevice({ settings: 1, noApply: true }); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  h.ctl.setDraft(2, true); await h.ctl.apply(); await h.settle(40);
  assert.equal(h.ctl.state.result.ok, false); assert.match(h.ctl.state.result.text, /回读不一致/);
  assert.equal(h.ctl.state.dirty, true);
});

test("set 回复中的 0x0B（同 seq）不触发完成判定：仅 ACK 失败 + 0x0B 时不成功", async () => {
  const ctl = F.createController({ send: async () => 5, say() {}, log() {} });
  ctl.onConfig({ type: "filterConfig", active: 0, saved: 0, estimatorHz: 1000, seq: 1 });
  ctl.setDraft(2, true); await ctl.apply();
  ctl.onConfig({ type: "filterConfig", active: 2, saved: 2, estimatorHz: 1000, seq: 5 }); // 0x0B before/without ACK
  assert.ok(ctl.state.pending, "0x0B alone is not success");
  ctl.onAck(0x27, 3, 0, 5);
  ctl.onConfig({ type: "filterConfig", active: 2, saved: 2, estimatorHz: 1000, seq: 5 });
  assert.equal(ctl.state.result.ok, false);
});

test("旧固件：0x26 回 ACK 0x01 → 不支持提示，apply 不发送", async () => {
  const dev = makeDevice({ supported: false }); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  assert.equal(h.ctl.state.supported, false);
  assert.equal(h.ctl.statusText(), "当前固件不支持滤波模式，请升级配套固件");
  const n = dev.sent.length; assert.equal(await h.ctl.apply(), false); assert.equal(dev.sent.length, n);
  // 0x28 unsupported before any config → same result
  const h2 = makeHost(makeDevice({ supported: false })); await h2.ctl.queryDiag(); await h2.settle();
  assert.equal(h2.ctl.state.supported, false);
});

test("草稿：未应用的本地修改不被下一次回读覆盖；撤销后跟随设备", async () => {
  const dev = makeDevice(); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  h.ctl.setDraft(2);
  await h.ctl.query(); await h.settle();
  assert.equal(h.ctl.state.draft.profile, 2); assert.equal(h.ctl.state.dirty, true);
  h.ctl.discard(); assert.equal(h.ctl.state.draft.profile, 0); assert.equal(h.ctl.state.dirty, false);
  dev.active = 1; await h.ctl.query(); await h.settle();
  assert.equal(h.ctl.state.draft.profile, 1);
});

test("超时：ACK 未到 → 3 s 后放弃并保留修改", async (t) => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const ctl = F.createController({ send: async () => 1, say() {}, log() {} });
  ctl.onConfig({ type: "filterConfig", active: 0, saved: 0, estimatorHz: 1000, seq: 0 });
  ctl.setDraft(1, true); await ctl.apply();
  t.mock.timers.tick(F.ACK_TIMEOUT_MS + 1);
  assert.equal(ctl.state.pending, null); assert.equal(ctl.state.dirty, true); assert.match(ctl.state.message, /未确认/);
});

test("只发送 0x26 / 0x27 / 0x28：不发重启、融合模式、量程、输出频率命令", async () => {
  const dev = makeDevice({ settings: 1 }); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  for (const [p, s] of [[2, 0], [1, 1], [0, 1]]) { h.ctl.setDraft(p, s); await h.ctl.apply(); await h.settle(40); }
  await h.ctl.queryDiag(); await h.settle();
  const ids = new Set(dev.sent.map((s) => s.id));
  assert.deepEqual([...ids].sort(), [0x26, 0x27, 0x28]);
  for (const bad of [0x15, 0x16, 0x19, 0x1d, 0x1e]) assert.ok(!ids.has(bad));
  assert.equal(h.ctl.state.diag.type, "filterDiag"); assert.equal(h.ctl.state.diag.magReady, false, "6-axis board: mag bit0 = 0");
});

// ---- 第 4 档「零角速保持」(ZARU) ----
test("旧固件 capabilities=3：第 4 档不可选、apply 不发 0x27（零写入）", async () => {
  const dev = makeDevice({ settings: 1, caps: 3 }); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  assert.equal(h.ctl.state.profileCount, 3, "capabilities kept in UI state");
  assert.equal(h.ctl.available(3), false); assert.equal(h.ctl.available(2), true);
  h.ctl.setDraft(3, true); assert.equal(h.ctl.state.draft.profile, 0, "setDraft(3) ignored on caps 3");
  // 即便草稿被强行改成 3，apply 也不编码
  h.ctl.state.draft.profile = 3; h.ctl.state.dirty = true;
  assert.equal(await h.ctl.apply(), false); await h.settle(30);
  assert.equal(dev.sent.filter((s) => s.id === 0x27).length, 0, "zero 0x27 writes");
  assert.match(h.ctl.state.message, /有效/);
  assert.equal(dev.active, 0);
});

test("新固件 capabilities=4：应用并回读第 4 档（persist=1），profile=4 被拒绝", async () => {
  const dev = makeDevice({ settings: 1, caps: 4 }); const h = makeHost(dev);
  await h.ctl.query(); await h.settle();
  assert.equal(h.ctl.state.profileCount, 4); assert.equal(h.ctl.available(3), true);
  h.ctl.setDraft(3, true); assert.equal(await h.ctl.apply(), true); await h.settle(40);
  const set = dev.sent.filter((s) => s.id === 0x27); assert.deepEqual(set.map((s) => s.pl), [[3, 1]]);
  const ids = dev.sent.map((s) => s.id); assert.ok(ids.lastIndexOf(0x26) > ids.indexOf(0x27), "readback 0x26 after ACK");
  assert.equal(h.ctl.state.result.ok, true); assert.match(h.ctl.state.result.text, /零角速保持，已保存/);
  assert.deepEqual([h.ctl.state.config.active, h.ctl.state.config.saved, h.ctl.state.config.tauMag, +h.ctl.state.config.restTau.toFixed(3)], [3, 3, 4, 0.5]);
  assert.equal(h.ctl.statusText(), "当前：零角速保持 · 已保存：零角速保持 · 内部 1000 Hz");
  // profile 4：控制器不接受；设备直接收到也回 ACK 0x02 且不改变
  h.ctl.setDraft(4, true); assert.equal(h.ctl.state.draft.profile, 3);
  const n27 = dev.sent.filter((s) => s.id === 0x27).length;
  h.ctl.state.draft.profile = 4; assert.equal(await h.ctl.apply(), false); assert.equal(dev.sent.filter((s) => s.id === 0x27).length, n27);
  await h.raw(0x27, [4, 0]); await h.settle();
  assert.deepEqual(h.messages.filter((m) => m.type === "ack" && m.cmd === 0x27).map((m) => m.status).slice(-1), [2]);
  assert.equal(dev.active, 3);
  // 运行时 profile=3（persist=0）重启后回到已保存
  dev.saved = 1; dev.reboot(); h.ctl.reset(); await h.ctl.query(); await h.settle();
  assert.equal(h.ctl.state.config.active, 1);
});

test("第 4 档说明：融合模式来自 0x07（setFusion），九轴/九轴相对角写明「本档不锁定航向」", () => {
  const ctl = F.createController({ send: async () => 1 });
  ctl.onConfig({ type: "filterConfig", active: 3, saved: 3, profileCount: 4, estimatorHz: 1000, seq: 0 });
  assert.match(ctl.hint(), /仅六轴锁定航向，九轴不锁磁力计航向/, "fusion unknown → generic hint");
  ctl.setFusion(0); assert.ok(!ctl.hint().includes("本档不锁定航向"));
  for (const m of [1, 2]) { ctl.setFusion(m); assert.match(ctl.hint(), /本档不锁定航向/); assert.match(ctl.hint(), /滤波参数与均衡相同/); }
  assert.equal(ctl.hint(1), F.PROFILES[1].hint, "first three hints unchanged under nine-axis");
  ctl.setFusion(0); assert.match(ctl.hint(3, { en: true }), /^零角速保持（ZARU \/ Stationary Heading Hold）：静止时锁定航向/);
  ctl.reset(); assert.equal(ctl.state.fusionMode, null); assert.equal(ctl.state.profileCount, null);
});
