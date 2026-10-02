// 二进制协议：CRC、各消息解码、严格 payload 长度（帧按固件 protocol.h / protocol.c 独立打包）
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadCore, fw, fwPackFrame, fwCrc16, concat, rng, feedChunked } from "./load-core.mjs";

const C = loadCore();
const collect = (mode = "auto", ch = 3) => {
  const got = [];
  const p = C.createStreamParser({ onMessage: (m) => got.push(m), onJust: (v) => got.push({ type: "just", v }) });
  p.configure(mode, ch);
  return { p, got };
};

test("CRC16 = 固件 protocol_crc16（多项式 0x1021、初值 0xFFFF → CCITT-FALSE，check=0x29B1）", () => {
  const s = new TextEncoder().encode("123456789");
  assert.equal(fwCrc16(s), 0x29b1);
  assert.equal(C.crc16(s), 0x29b1);
});

test("buildFrame 与固件 protocol_pack_frame 字节一致（命令帧）", () => {
  assert.deepEqual(Array.from(C.buildFrame(C.CMD.STREAM, 7, [4])), Array.from(fwPackFrame(0x13, 7, [4])));
  assert.deepEqual(Array.from(C.buildFrame(C.CMD.OUTPUT_HZ, 255, [0xe8, 0x03])), Array.from(fwPackFrame(0x1d, 255, [0xe8, 0x03])));
  assert.deepEqual(Array.from(C.buildFrame(C.CMD.QUERY, 0)), Array.from(fwPackFrame(0x14, 0, [])));
});

test("payload 长度表与 protocol.h 结构体 sizeof 一致", () => {
  assert.deepEqual({ ...C.PAYLOAD_LEN }, { 1: 16, 2: 18, 3: 28, 4: 12, 5: 16, 7: 18, 8: 24, 10: 60, 11: 16, 12: 60, 0x90: 4 });
});

test("每种消息都能正确解码", () => {
  const { p, got } = collect("binary");
  p.push(fw.ack(1, 0x13, 0, 4));
  p.push(fw.sysinfo(2, 2000, 1000, 2, 31.25, 4, 1));
  p.push(fw.attitude(3, 10.5, -20.25, 170.125, 0x09, 4321));
  p.push(fw.compact(4, 12.345, -45.678, -179.99, 123.45, 0x01, 65535));
  p.push(fw.imu(5, [1.5, -2.5, 3.25], [0.1, -0.2, 9.81], 36.78, 777));
  p.push(fw.quat(6, 0.5, 0.5, -0.5, 0.5, 42));
  assert.equal(got.length, 6);
  assert.deepEqual(got[0], { type: "ack", cmd: 0x13, status: 0, detail: 4, seq: 1 });
  assert.equal(got[1].type, "sysinfo");
  assert.equal(got[1].fusionHz, 2000); assert.equal(got[1].outHz, 1000); assert.equal(got[1].skipN, 2);
  assert.equal(got[1].temp, 31.25); assert.equal(got[1].streamMode, 4); assert.equal(got[1].canOk, 1);
  assert.equal(got[2].type, "attitude");
  assert.equal(got[2].roll, 10.5); assert.equal(got[2].pitch, -20.25); assert.equal(got[2].yaw, 170.125);
  assert.equal(got[2].flags, 0x09); assert.equal(got[2].ts, 4321);
  assert.equal(got[3].type, "compact");
  assert.equal(got[3].roll, 12.34); assert.equal(got[3].pitch, -45.67); assert.equal(got[3].yaw, -179.99);
  assert.equal(got[3].gz, 123.4); assert.equal(got[3].flags, 1); assert.equal(got[3].ts, 65535);
  assert.equal(got[4].type, "imu");
  assert.deepEqual([got[4].gx, got[4].gy, got[4].gz], [1.5, -2.5, 3.25]);
  assert.ok(Math.abs(got[4].az - 9.81) < 1e-5);
  assert.equal(got[4].temp, 36.78, "IMU temp_c_x100 @24 → /100");
  assert.equal(got[4].ts, 777, "IMU timestamp_ms @26");
  assert.deepEqual([got[5].qw, got[5].qx, got[5].qy, got[5].qz, got[5].ts], [0.5, 0.5, -0.5, 0.5, 42]);
  assert.equal(p.stats.binFrames, 6);
  assert.equal(p.stats.badLengthFrames, 0);
});

test("严格长度：CRC 正确但 payload 长度不符的帧被丢弃并计数", () => {
  const { p, got } = collect("binary");
  const bad = [
    fwPackFrame(0x90, 1, [0x13, 0, 4, 0, 0]),   // ACK 5
    fwPackFrame(0x90, 1, [0x13, 0, 4]),         // ACK 3
    fwPackFrame(0x05, 1, new Uint8Array(15)),   // SYSINFO 15
    fwPackFrame(0x05, 1, new Uint8Array(17)),   // SYSINFO 17
    fwPackFrame(0x01, 1, new Uint8Array(12)),   // ATTITUDE 12（旧代码 >=12 即接受）
    fwPackFrame(0x04, 1, new Uint8Array(10)),   // COMPACT 10
    fwPackFrame(0x03, 1, new Uint8Array(24)),   // IMU 24
    fwPackFrame(0x02, 1, new Uint8Array(16)),   // QUAT 16
  ];
  for (const f of bad) p.push(f);
  assert.equal(got.filter((m) => m.type !== "badLength").length, 0);
  assert.equal(p.stats.badLengthFrames, bad.length);
  assert.deepEqual(got.map((m) => [m.id, m.length, m.expected]), [[0x90, 5, 4], [0x90, 3, 4], [5, 15, 16], [5, 17, 16], [1, 12, 16], [4, 10, 12], [3, 24, 28], [2, 16, 18]]);
  // 之后的正确帧不受影响
  p.push(fw.ack(9, 0x10, 0, 0));
  assert.equal(got.at(-1).type, "ack");
});

test("CRC 错误 / 超长 LEN / 未知 MSG：不产生消息，不影响后续帧", () => {
  const { p, got } = collect("binary");
  const f = fw.attitude(1, 1, 2, 3, 0, 0);
  f[8] ^= 0xff;
  p.push(f);
  p.push(Uint8Array.from([0xaa, 0x55, 0x01, 200, 0]));
  p.push(fwPackFrame(0x7e, 3, [1, 2, 3]));
  p.push(fw.attitude(2, 4, 5, 6, 0, 0));
  assert.deepEqual(got.map((m) => m.type), ["unknown", "attitude"]);
  assert.equal(got[1].yaw, 6);
  assert.equal(p.stats.unknownFrames, 1);
  assert.ok(p.stats.crcSkips >= 1);
});

test("二进制帧任意分包 + 垃圾字节：全部帧恰好解出一次", () => {
  const r = rng(7);
  const parts = [];
  const N = 400;
  for (let i = 0; i < N; i++) {
    if (r() < 0.3) { const g = new Uint8Array(r.int(1, 20)); for (let k = 0; k < g.length; k++) g[k] = r.int(0, 255); parts.push(g); }
    parts.push(fw.compact(i, (i % 300) - 150, 1, 2, 3, 0, i));
  }
  const { p, got } = collect("binary");
  feedChunked((c) => p.push(c), concat(parts), r, 1, 40);
  const c = got.filter((m) => m.type === "compact");
  assert.equal(c.length, N);
  c.forEach((m, i) => assert.equal(m.ts, i));
});
