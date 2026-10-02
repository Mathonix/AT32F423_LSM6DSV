// 数据流同步、写入队列恢复、输出频率校验、ACK detail 解码
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadCore, justFrame, concat, poseValues, fw } from "./load-core.mjs";

const C = loadCore();

test("stream_mode → 解析配置：0=VOFA_3CH，1..3=二进制，4=VOFA_6CH，其余无效", () => {
  assert.deepEqual(C.streamParserConfig(0), { parseMode: "justfloat", justChannels: 3 });
  for (const m of [1, 2, 3]) assert.deepEqual(C.streamParserConfig(m), { parseMode: "binary", justChannels: null });
  assert.deepEqual(C.streamParserConfig(4), { parseMode: "justfloat", justChannels: 6 });
  assert.equal(C.streamParserConfig(5), null);
});

test("STREAM ACK 在同一数据块中间到达：回调里 reset+configure 后，ACK 之后的新数据流按新配置解析", () => {
  const pose3 = [], pose6 = [], msgs = [];
  let p;
  p = C.createStreamParser({
    onJust: (v) => (v.length === 3 ? pose3 : pose6).push(v),
    onMessage: (m) => {
      msgs.push(m);
      if (m.type === "ack" && m.cmd === C.CMD.STREAM && m.status === 0) {
        const cfg = C.streamParserConfig(m.detail & 0xff);
        p.configure(cfg.parseMode, cfg.justChannels);  // app.js syncParserToStream → resetParser()
        p.reset();
      }
    },
  });
  p.configure("justfloat", 3);
  const s3 = concat(Array.from({ length: 10 }, (_, i) => justFrame(poseValues(i, 3))));
  const s6 = concat(Array.from({ length: 10 }, (_, i) => justFrame(poseValues(100 + i, 6))));
  const half = justFrame(poseValues(50, 3)).subarray(0, 7);   // ACK 前残留的半帧必须被清掉
  p.push(concat([s3, half, fw.ack(1, C.CMD.STREAM, 0, 4), s6]));
  assert.equal(pose3.length, 9);
  assert.equal(p.channels, 6);
  // reset 后未同步：丢弃至第一个帧尾（第 100 帧），之后 9 帧精确
  assert.equal(pose6.length, 9);
  pose6.forEach((v, i) => assert.deepEqual(v, poseValues(101 + i, 6)));
  const f = C.justValuesToFields(pose6[0]);
  assert.equal(f.temp, poseValues(101, 6)[5]);
  assert.equal(f.az, poseValues(101, 6)[4]);
  assert.equal(f.gz, poseValues(101, 6)[3]);
});

test("切到二进制后 JustFloat 字节不再产生姿态，二进制帧照常解析", () => {
  const got = [];
  const p = C.createStreamParser({ onJust: (v) => got.push(["just", v]), onMessage: (m) => got.push([m.type, m]) });
  p.configure("binary", 3);
  p.reset();
  p.push(concat([justFrame([1, 2, 3]), justFrame([1, 2, 3]), fw.attitude(1, 1, 2, 3, 0, 0)]));
  assert.deepEqual(got.map((g) => g[0]), ["attitude"]);
});

test("writeChain：一次写失败后，后续写入恢复（旧实现会永久拒绝）", async () => {
  let fail = true;
  const calls = [];
  const writer = { write: async (f) => { calls.push(f); if (fail) { fail = false; throw new Error("device lost"); } } };
  let current = writer;
  const q = C.createWriteQueue(() => current);
  await assert.rejects(q.write(Uint8Array.of(1)), /device lost/);
  await q.write(Uint8Array.of(2));
  await q.write(Uint8Array.of(3));
  assert.equal(calls.length, 3);
  current = null;
  await assert.rejects(q.write(Uint8Array.of(4)), /串口未连接/);
  current = writer;
  await q.write(Uint8Array.of(5));
  assert.equal(calls.length, 4);

  // 旧实现（原线上 99 行）：writeChain = writeChain.then(() => writer.write(frame)) —— 一次拒绝后永远拒绝
  let legacy = Promise.resolve();
  let legacyFail = true;
  const legacyWrite = (f) => { legacy = legacy.then(() => { if (legacyFail) { legacyFail = false; throw new Error("device lost"); } return f; }); return legacy; };
  await assert.rejects(legacyWrite(1));
  await assert.rejects(legacyWrite(2), /device lost/, "旧实现：第二次写入也被同一个错误拒绝");
});

test("writeChain.reset()：拔出时挂起的写入不会阻塞重连后的写入", async () => {
  let current = { write: () => new Promise(() => {}) };  // 永不完成（拔出时卡住的写）
  const q = C.createWriteQueue(() => current);
  void q.write(Uint8Array.of(1));
  q.reset();                                              // connect / disconnect 时复位
  let done = false;
  current = { write: async () => { done = true; } };
  await q.write(Uint8Array.of(2));
  assert.ok(done);
});

test("输出频率校验：整数、1..fusionHz、fusionHz % hz === 0", () => {
  const ok = (t, f = 2000) => C.validateOutputHz(t, f);
  for (const hz of ["2000", "1000", "500", "400", "250", "200", "100", "1", " 50 "]) assert.equal(ok(hz).ok, true, hz);
  assert.equal(ok("200").hz, 200);
  for (const bad of ["0", "3", "300", "2001", "4000", "1.5", "-5", "abc", "", "1e3"]) assert.equal(ok(bad).ok, false, bad);
  assert.match(ok("300").error, /整除融合率 2000 Hz/);
  assert.match(ok("2001").error, /1~2000/);
  // 融合率来自 SYSINFO（动态）
  assert.equal(ok("2000", 1000).ok, false);
  assert.equal(ok("300", 1200).ok, true);
  assert.equal(C.DEFAULT_FUSION_HZ, 2000);
});

test("ACK detail 解码：CAL=1、GYRO_60=0x0601、ACC_6FACE=0x0600/0x0100/…", () => {
  assert.match(C.ackDetailText(C.CMD.CAL, 3, 1), /未实现/);
  assert.match(C.ackDetailText(C.CMD.GYRO_60, 3, 0x0601), /未实现/);
  assert.match(C.ackDetailText(C.CMD.ACC_6FACE, 3, 0x0600), /APP_ACC_CAL_ENABLE=0/);
  assert.match(C.ackDetailText(C.CMD.ACC_6FACE, 0, 0x0100), /已开始/);
  assert.match(C.ackDetailText(C.CMD.ACC_6FACE, 0, 0), /完成/);
  assert.equal(C.ackDetailText(C.CMD.PING, 0, 0), "");
});
