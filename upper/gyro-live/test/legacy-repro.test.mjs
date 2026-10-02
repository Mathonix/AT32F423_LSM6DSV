// 在 vm 中加载修复前的线上 app.js（backup/live-deployed/app.js），复现审计中的缺陷；新代码对应用例见其它测试
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadLegacyApp, justFrame, concat, fw, fwPackFrame } from "./load-core.mjs";

test("旧版：JustFloat 从帧尾往前截取 12 字节 —— 垃圾 + 帧尾 也会被当作姿态", () => {
  const app = loadLegacyApp();
  const fake = new Uint8Array(12);
  new DataView(fake.buffer).setFloat32(0, 123.5, true);
  app.feed(Array.from(concat([Uint8Array.of(9, 9, 9, 9, 9), fake, Uint8Array.from([0, 0, 0x80, 0x7f])])));
  assert.equal(app.state().pose.yaw, 123.5, "旧版把非帧对齐的字节当作了 Yaw");
});

test("旧版：6 通道流按 3 通道解析会得到错位值（Gz/Az 被当成 Yaw/Pitch）", () => {
  const app = loadLegacyApp();
  app.feed(Array.from(concat([justFrame([10, 20, 30, 40, 50, 25]), justFrame([11, 21, 31, 41, 51, 26])])));
  assert.deepEqual([app.state().pose.yaw, app.state().pose.pitch, app.state().pose.roll], [41, 51, 26]);
});

test("旧版：ATTITUDE payload 12 字节（应为 16）也被接受", () => {
  const app = loadLegacyApp();
  const p = new Uint8Array(12);
  new DataView(p.buffer).setFloat32(8, 77, true);
  app.feed(Array.from(fwPackFrame(0x01, 1, p)));
  assert.equal(app.state().pose.yaw, 77);
});

test("旧版：STREAM ACK 成功不会同步解析器（仍按 3 通道）", () => {
  const app = loadLegacyApp();
  app.feed(Array.from(fw.ack(1, 0x13, 0, 4)));
  assert.equal(app.state().streamMode, 4);
  assert.equal(app.el("justChannels").value, "", "justChannels select 未被更新");
});

test("旧版：modeLabel 固定显示“六轴”（SYSINFO 并无融合模式字段）", () => {
  const app = loadLegacyApp();
  app.feed(Array.from(fw.sysinfo(1, 2000, 1000, 2, 30, 0, 1)));
  assert.match(app.el("modeLabel").textContent, /融合：六轴 2000Hz/);
});

test("旧版：IMU 帧温度被忽略", () => {
  const app = loadLegacyApp();
  app.feed(Array.from(fw.imu(1, [1, 2, 3], [4, 5, 6], 41.5, 9)));
  assert.equal(app.state().pose.temp, null);
});
