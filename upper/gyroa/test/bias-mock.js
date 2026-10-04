// VQF 运动零偏状态模拟（追加在 mock-serial.js + filter-mock.js [+ zaru-mock.js] 之后注入）：按 host-agent-motion-bias.md 应答 0x30。NOT real hardware.
// 选项（window.__mock.bias）：supported（false → 旧固件 ACK 0x01）、motion / rest（0 → 未开启）、version、badLength、nonfinite、drop（不回复）、restDetected、zaruHold。
// tauAcc 按 filter-mock 当前档：只有「均衡 2.5 s」来自规格，其它档数值为 mock 假设。
(() => {
  const m = window.__mock; if (!m || m.bias) return;
  const dev = m.dev, frame = m.frame;
  const le = (n, fn) => { const b = new Uint8Array(n); fn(new DataView(b.buffer)); return b; };
  const TAU_ACC = [1.5, 2.5, 4.0, 2.5]; // mock 假设（均衡 = 2.5 s 为规格值）
  const b = { supported: true, motion: 1, rest: 1, version: 1, badLength: false, nonfinite: false, drop: false, restDetected: 1, zaruHold: 0,
    bias: [0.0123, -0.0045, 0.0011], residual: 0.0087, log: [] };
  const ack = (cmd, seq, status) => frame(0x90, seq, le(4, (v) => { v.setUint8(0, cmd); v.setUint8(1, status); }));
  const reply = (seq) => frame(0x09, seq, le(b.badLength ? 44 : 48, (v) => {
    [b.version, b.motion, b.rest, b.restDetected].forEach((x, i) => v.setUint8(i, x));
    const fl = [0.10, 0.0001, 100, 2.0, 0.035, TAU_ACC[m.filter?.active ?? 1], ...b.bias, b.nonfinite ? NaN : b.residual];
    fl.forEach((x, i) => { if (4 + 4 * i + 4 <= v.byteLength) v.setFloat32(4 + 4 * i, x, true); });
    if (v.byteLength >= 48) { v.setUint8(44, b.zaruHold); v.setUint8(45, dev.activeMode === 0 && m.filter?.active === 3 ? 1 : 0); v.setUint16(46, 0, true); }
  }));
  const orig = dev.handle.bind(dev);
  dev.handle = function (id, seq, pl) {
    if (id !== 0x30) return orig(id, seq, pl);
    this.commands.push({ id, payload: Array.from(pl) }); b.log.push({ id, seq, payload: Array.from(pl), t: performance.now() });
    if (!b.supported) return ack(id, seq, 1);
    if (pl.length) return ack(id, seq, 2);
    if (b.drop) return null;
    b.bias = b.bias.map((x) => x + 0.0001); // 每次查询略有变化，便于看到刷新
    return reply(seq);
  };
  m.bias = b;
})();
