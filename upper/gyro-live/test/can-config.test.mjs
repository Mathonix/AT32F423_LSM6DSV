import test from 'node:test';
import assert from 'node:assert/strict';
import { loadCore } from './load-core.mjs';
const core = loadCore();
const initial = { nodeId: 1, masterId: 1791, periodMs: 1, baud: 0, active: 1, mask: 4, reserved: 0 };
test('CAN encoder preserves all 11 bits and exact wire byte order', () => {
  const c = { ...initial, nodeId: 2047, masterId: 0x345, periodMs: 300, baud: 7, mask: 15 };
  assert.deepEqual([...core.encodeCanConfig(c, true)], [255, 7, 69, 3, 44, 1, 7, 1, 15, 0, 1]);
  assert.equal(core.encodeCanConfig({ ...c, nodeId: 2048 }, false), null);
});
test('CAN validation rejects overload, bad enums and reserved bits', () => {
  assert.ok(core.validCanConfig({ ...initial, mask: 15 }));
  for (const [key, value] of [['baud', 8], ['active', 2], ['mask', 16], ['periodMs', 0], ['periodMs', 10001], ['reserved', 1], ['nodeId', NaN]]) {
    assert.equal(core.validCanConfig({ ...initial, [key]: value }), false);
  }
  assert.equal(core.validCanConfig({ ...initial, baud: 7, mask: 15 }), false);
  assert.ok(core.validCanConfig({ ...initial, baud: 7, mask: 15, periodMs: 30 }));
  assert.ok(core.validCanConfig({ ...initial, baud: 7, mask: 15, active: 0 }));
});
test('CAN config response decodes current/saved independently and rejects malformed frames', () => {
  const p = new Uint8Array(24); p.set([1, 1]);
  p.set(core.encodeCanConfig(initial, false).slice(0, 10), 2);
  p.set(core.encodeCanConfig({ ...initial, baud: 7, active: 0 }, false).slice(0, 10), 12);
  const d = core.decodePayload(8, p);
  assert.equal(d.type, 'canConfig'); assert.equal(d.active.baud, 0); assert.equal(d.saved.baud, 7);
  assert.equal(core.decodePayload(8, p.slice(1)).type, 'badLength');
  p[21] = 1; assert.equal(core.decodePayload(8, p).type, 'unknown');
});
