// JustFloat 严格帧长：N*4 + 帧尾 00 00 80 7F 必须在精确位置；失步后搜索下一个帧尾重新同步
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadCore, justFrame, concat, rng, poseValues, feedChunked, fw, TAIL } from "./load-core.mjs";

const C = loadCore();
function mk(mode, ch) {
  const frames = [];
  const p = C.createStreamParser({ onJust: (v) => frames.push(v), onMessage: () => {} });
  p.configure(mode, ch);
  return { p, frames };
}
const stream = (n, count, from = 0) => concat(Array.from({ length: count }, (_, i) => justFrame(poseValues(from + i, n))));

for (const n of [3, 6]) {
  test(`${n} 通道：首个帧尾之前的字节被丢弃，之后每帧精确解出`, () => {
    const { p, frames } = mk("justfloat", n);
    p.push(stream(n, 50));
    // 未同步时找到第一个帧尾并丢弃它及之前的字节 → 第 0 帧不解（绝不从帧尾往前截取）
    assert.equal(frames.length, 49);
    frames.forEach((v, i) => assert.deepEqual(v, poseValues(i + 1, n)));
    assert.equal(p.stats.justResyncs, 0);
  });

  test(`${n} 通道：随机分包（帧尾跨包）结果与整包一致`, () => {
    const r = rng(n);
    const { p, frames } = mk("justfloat", n);
    feedChunked((c) => p.push(c), stream(n, 2000), r, 1, 13);
    assert.equal(frames.length, 1999);
    frames.forEach((v, i) => assert.deepEqual(v, poseValues(i + 1, n)));
  });

  test(`${n} 通道：插入垃圾 → 受影响的帧丢弃、重同步，解出的帧全部是完整原始帧`, () => {
    const r = rng(100 + n);
    const parts = [justFrame(poseValues(0, n))];
    let damaged = 0;
    for (let s = 1; s < 3000; s++) {
      if (r() < 0.05) { const g = new Uint8Array(r.int(1, 37)); for (let k = 0; k < g.length; k++) g[k] = r.int(0, 255); parts.push(g); damaged++; }
      parts.push(justFrame(poseValues(s, n)));
    }
    const { p, frames } = mk("justfloat", n);
    feedChunked((c) => p.push(c), concat(parts), r, 1, 300);
    const seen = new Set();
    for (const v of frames) {
      const s = Math.round(v[0] + 180) + 0; // ch0 编码序号（mod 360）
      const idx = frames.indexOf(v);
      assert.ok(v.length === n);
      // 每个解出的帧都必须与某个原始帧完全相同
      let ok = false;
      for (let k = s; k < 3000; k += 360) if (poseValues(k, n).every((x, j) => Object.is(x, v[j]))) { ok = true; seen.add(k); break; }
      assert.ok(ok, `frame #${idx} is not an exact original frame: ${v}`);
    }
    assert.ok(frames.length > 3000 - 1 - damaged * 2, `decoded ${frames.length}, damaged ${damaged}`);
    assert.ok(p.stats.justResyncs > 0);
  });
}

test("帧尾不在精确位置（多 2 字节）→ 该帧不解析，计失步，下一帧恢复", () => {
  const { p, frames } = mk("justfloat", 3);
  p.push(stream(3, 3));                 // 同步，解出 2 帧
  assert.equal(frames.length, 2);
  const f = justFrame(poseValues(10, 3));
  p.push(concat([f.subarray(0, 6), Uint8Array.from([1, 2]), f.subarray(6)])); // 18 字节，帧尾在 14
  p.push(justFrame(poseValues(11, 3)));
  p.push(justFrame(poseValues(12, 3)));
  assert.equal(p.stats.justResyncs, 1);
  assert.deepEqual(frames.slice(2), [poseValues(11, 3), poseValues(12, 3)]);
});

test("少 4 字节的帧：不会把「帧尾前 12 字节」当成一帧", () => {
  const { p, frames } = mk("justfloat", 3);
  p.push(stream(3, 2));  // 解出 1 帧
  const short = justFrame(poseValues(20, 3));
  p.push(concat([short.subarray(0, 4), short.subarray(8)]));  // 缺 4 字节
  p.push(justFrame(poseValues(21, 3)));
  p.push(justFrame(poseValues(22, 3)));
  for (const v of frames) assert.ok([1, 21, 22].some((k) => poseValues(k, 3).every((x, j) => Object.is(x, v[j]))), `bogus frame ${v}`);
  assert.ok(frames.some((v) => Object.is(v[0], poseValues(22, 3)[0])));
});

test("通道数不匹配：6 通道流按 3 通道 / 3 通道流按 6 通道解析 → 0 帧（不产生错位数据）", () => {
  const a = mk("justfloat", 3); a.p.push(stream(6, 200));
  assert.equal(a.frames.length, 0);
  assert.ok(a.p.stats.justResyncs > 100);
  const b = mk("justfloat", 6); b.p.push(stream(3, 200));
  // 3 通道帧 16 字节，6 通道需 28 字节：帧尾永远不在 24 → 不产生帧
  assert.equal(b.frames.length, 0);
});

test("6 通道映射：CH0 Yaw CH1 Pitch CH2 Roll CH3 Gz CH4 Az CH5 Temp", () => {
  assert.deepEqual(C.justValuesToFields([1, 2, 3, 4, 5, 6]), { yaw: 1, pitch: 2, roll: 3, gz: 4, az: 5, temp: 6 });
  assert.deepEqual(C.justValuesToFields([1, 2, 3]), { yaw: 1, pitch: 2, roll: 3 });
});

test("非有限值帧被拒绝（计 justInvalid），不影响同步", () => {
  const { p, frames } = mk("justfloat", 3);
  p.push(justFrame([0, 0, 0]));
  p.push(justFrame([NaN, 1, 2]));
  p.push(justFrame([3, 4, 5]));
  assert.deepEqual(frames, [[3, 4, 5]]);
  assert.equal(p.stats.justInvalid, 1);
});

test("JustFloat 流中插入 ACK 二进制帧：ACK 被解析，JustFloat 帧不丢", () => {
  const acks = [], frames = [];
  const p = C.createStreamParser({ onJust: (v) => frames.push(v), onMessage: (m) => acks.push(m) });
  p.configure("justfloat", 3);
  p.push(concat([stream(3, 5), fw.ack(1, 0x10, 0, 0), stream(3, 5, 5)]));
  assert.equal(acks.length, 1);
  assert.equal(frames.length, 9);
});

test("数据中偶然出现 AA 55（CRC 不符）不吞掉 JustFloat 帧", () => {
  const { p, frames } = mk("justfloat", 3);
  const f = justFrame([1, 2, 3]);
  const g = justFrame([4, 5, 6]);
  g.set([0xaa, 0x55, 0x01, 0x03], 0);   // ch0 字节 = AA 55 01 03（很小的非零 float）
  const v0 = new DataView(g.buffer).getFloat32(0, true);
  p.push(concat([f, g, f, f]));
  assert.equal(frames.length, 3);
  assert.ok(Object.is(frames[0][0], v0));
});

test("binary 模式下不解析 JustFloat", () => {
  const { p, frames } = mk("binary", 3);
  p.push(stream(3, 20));
  assert.equal(frames.length, 0);
});
