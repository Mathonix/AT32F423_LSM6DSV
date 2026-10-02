import { test } from 'node:test';
import assert from 'node:assert/strict';
import { loadCore, fwPackFrame, justFrame, concat, rng, feedChunked } from './load-core.mjs';
const C = loadCore();
const fields = [12, 23, 34, 1, 2, 3, 45, 56, 67];
function selectedPayload(mask) {
  const values = fields.filter((_, i) => mask & (1 << i));
  const p = new Uint8Array(4 + 4 * values.length);
  const v = new DataView(p.buffer);
  v.setUint16(0, mask, true); v.setUint16(2, 4321, true);
  values.forEach((x, i) => v.setFloat32(4 + i * 4, x, true));
  return { p, values };
}
test('自定义协议：全部 511 种非空组合都按统一顺序解码，分片不丢帧', () => {
  for (let mask = 1; mask <= 0x1ff; mask++) {
    const { p, values } = selectedPayload(mask);
    const got = [];
    const parser = C.createStreamParser({ onMessage: m => got.push(m) });
    parser.configure('binary', 3);
    feedChunked(b => parser.push(b), fwPackFrame(6, 7, p), rng(mask), 1, 7);
    assert.deepEqual(got, [{ type: 'selected', mask, values, ts: 4321, seq: 7 }]);
    const expected = Object.fromEntries(C.FIELD_NAMES.filter((_, bit) => mask & (1 << bit)).map((key, i) => [key, values[i]]));
    assert.deepEqual(C.selectedFields(values, mask), expected);
  }
});
test('JustFloat：1~9 通道和稀疏掩码均支持，ACK 不成为数据通道', () => {
  for (let mask = 1; mask <= 0x1ff; mask++) {
    const values = fields.filter((_, i) => mask & (1 << i));
    const got = [], messages = [];
    const parser = C.createStreamParser({ onJust: v => got.push(v), onMessage: m => messages.push(m) });
    parser.configure('justfloat', values.length, mask);
    const stream = concat([justFrame(values), justFrame(values), fwPackFrame(0x90, 1, [0x10, 0, 0, 0]), justFrame(values)]);
    feedChunked(b => parser.push(b), stream, rng(mask), 1, 5);
    assert.deepEqual(got, [values, values]); // 首帧用于同步
    assert.equal(messages.length, 1);
  }
});
test('字段掩码、长度、非有限数和 CRC 错误不会产生有效自选数据', () => {
  for (const mask of [0, 0x200, 0xffff]) {
    const m = C.decodePayload(6, selectedPayload(mask).p);
    assert.equal(m.type, 'badLength');
  }
  assert.equal(C.decodePayload(6, Uint8Array.of(1, 0, 0)).type, 'badLength');
  const { p } = selectedPayload(0x1ff);
  assert.equal(C.decodePayload(6, p.slice(0, -1)).type, 'badLength');
  assert.equal(C.selectedFields([NaN], 1), null);
  const got = [], parser = C.createStreamParser({ onMessage: m => got.push(m) });
  const corrupt = fwPackFrame(6, 1, p); corrupt[10] ^= 1;
  parser.push(concat([corrupt, fwPackFrame(6, 2, p)]));
  assert.equal(got.length, 1); assert.equal(got[0].seq, 2);
});
test('设备配置反馈包含当前接口、运行/保存模式和独立的 UART/USB 配置', () => {
  const p = new Uint8Array([1, 1, 1, 0, 0, 1, 7, 0, 0xe8, 3, 0, 0, 1, 0, 1, 0, 0xff, 1]);
  const m = C.decodePayload(7, p);
  assert.equal(m.source, 1); assert.equal(m.activeMode, 1); assert.equal(m.savedMode, 0);
  assert.equal(m.savedFast, 1); assert.equal(m.outHz, 1000);
  assert.deepEqual(m.outputs, [{format: 0, legacyMode: 0, mask: 1}, {format: 1, legacyMode: 0, mask: 0x1ff}]);
  p[0] = 99; assert.equal(C.decodePayload(7, p).type, 'unknown');
});
test('启动配置 v2 回读当前/保存零偏时长，兼容 v1 并拒绝非法时长和截断', () => {
  const p = new Uint8Array([2, 1, 1, 0, 1, 0, 31, 0, 0xe8, 3, 0, 0, 7, 0, 1, 0, 0xff, 1, 0xd0, 7, 0xc4, 9]);
  const m = C.decodePayload(7, p);
  assert.equal(m.activeInitMs, 2000); assert.equal(m.savedInitMs, 2500);
  assert.equal(m.activeFast, 1); assert.equal(m.savedFast, 0);
  assert.equal(C.decodePayload(7, p.slice(0, 18)).type, 'badLength');
  p[20] = 0; p[21] = 0; assert.equal(C.decodePayload(7, p).type, 'unknown');
  p[20] = 0xff; p[21] = 0xff; assert.equal(C.decodePayload(7, p).type, 'unknown');
  const v1 = p.slice(0, 18); v1[0] = 1;
  assert.equal(C.decodePayload(7, v1).savedInitMs, null);
});
