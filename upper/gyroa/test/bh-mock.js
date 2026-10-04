// 启动零偏历史模拟（追加在 mock-serial.js 之后注入）：按 host-agent-bias-history-list.md 应答 0x33 → 0x34（60 B，成功不回 ACK）。NOT real hardware.
// 选项（window.__mock.bh）：supported（false → ACK 0x01）、count（0..50，默认 36）、recordVersion（默认 3；count 0 时自动 0）、corrupt（0/1）、
// sequence（默认 129）、silent（不回）、dropAt（该 offset 不回）、bumpAt（第一次请求该 offset 时先把 sequence +1，模拟中间又保存）、
// bad：'short'（59 B）| 'version'（version=2）| 'nan'（首条 NaN）、badAt（只对该 offset 生效，默认 0）、delayMs（每页延迟）。
(() => {
  const m = window.__mock; if (!m || m.bh) return;
  const dev = m.dev, frame = m.frame;
  const v = { supported: true, count: 36, recordVersion: 3, corrupt: 0, sequence: 129, silent: false, dropAt: null, bumpAt: null, bumped: false, bad: null, badAt: 0, delayMs: 0, log: [] };
  const entry = (i) => [0.012 * Math.sin(i) - 0.05, 0.03 + 0.001 * i, -0.02 - 0.0005 * i, 24 + 0.25 * i];
  v.entry = entry;
  const ack = (cmd, seq, status) => frame(0x90, seq, Uint8Array.from([cmd, status, 0, 0]));
  const reply = (seq, off) => {
    const b = new Uint8Array(v.bad === 'short' && off === v.badAt ? 59 : 60), d = new DataView(b.buffer);
    const count = v.count, n = Math.max(0, Math.min(3, count - off));
    b[0] = v.bad === 'version' && off === v.badAt ? 2 : 1; b[1] = count ? v.recordVersion : 0; b[2] = v.corrupt; b[3] = count;
    d.setUint16(4, off, true); b[6] = n; b[7] = 0;
    d.setUint32(8, v.sequence, true);
    for (let k = 0; k < n && 12 + 16 * k + 16 <= b.length; k++) entry(off + k).forEach((x, j) => d.setFloat32(12 + 16 * k + 4 * j, v.bad === 'nan' && off === v.badAt && k === 0 && j === 0 ? NaN : x, true));
    return frame(0x34, seq, b);
  };
  const orig = dev.handle.bind(dev);
  dev.handle = function (id, seq, pl) {
    if (id !== 0x33) return orig(id, seq, pl);
    this.commands.push({ id, payload: Array.from(pl) }); v.log.push({ id, seq, payload: Array.from(pl) });
    if (!v.supported) return ack(id, seq, 1);
    if (pl.length !== 0 && pl.length !== 2) return ack(id, seq, 2);
    const off = pl.length ? pl[0] | (pl[1] << 8) : 0;
    if (off > 50) return ack(id, seq, 2);
    if (v.silent || v.dropAt === off) return null;
    if (v.bumpAt === off && !v.bumped) { v.bumped = true; v.sequence++; }
    const f = reply(seq, off);
    if (v.delayMs) f.delayMs = v.delayMs;
    return f;
  };
  m.bh = v;
})();
