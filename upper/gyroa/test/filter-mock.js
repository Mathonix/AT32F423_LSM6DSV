// 姿态稳定性模拟（追加到 mock-serial.js 之后注入）：按配套固件 0x26 / 0x27 / 0x28 行为应答；ACK 与 0x0B/0x0C 均回显请求 seq。NOT real hardware.
(() => {
  const m = window.__mock; if (!m || m.filter) return;
  const dev = m.dev, frame = m.frame;
  const le = (n, fn) => { const b = new Uint8Array(n); fn(new DataView(b.buffer)); return b; };
  const TAU = [[2, 0.15], [4, 0.5], [6, 1.5], [4, 0.5]]; // tau_mag / rest_tau：[2,4,6,4] / [.15,.5,1.5,.5]
  const f = {
    supported: true,  // false → 旧固件：0x26/0x27/0x28 回 ACK 0x01
    active: 0, saved: 0, nineAxis: false, magReady: false,
    profileCount: 4,  // 0x0B capabilities：4 新固件（含零角速保持）；3 → 旧固件，0x27 profile=3 回 ACK 0x02
    forceStatus: null, // 测试：强制 0x27 回指定 ACK 状态（仍附 0x0B）
    noApply: false,    // 测试：ACK 成功但设备未真正切换（回读核对应失败）
    log: [],
  };
  const ack = (cmd, seq, status, detail) => frame(0x90, seq, le(4, (v) => { v.setUint8(0, cmd); v.setUint8(1, status); v.setUint16(2, detail, true); }));
  const config = (seq) => frame(0x0b, seq, le(16, (v) => {
    [1, f.active, f.saved, f.profileCount].forEach((x, i) => v.setUint8(i, x));
    v.setUint16(4, 1000, true); v.setUint16(6, 0, true);
    v.setFloat32(8, TAU[f.active][0], true); v.setFloat32(12, TAU[f.active][1], true);
  }));
  const diag = (seq) => frame(0x0c, seq, le(60, (v) => {
    [1, f.active, 1, f.nineAxis && f.magReady ? 1 : 0].forEach((x, i) => v.setUint8(i, x));
    v.setUint32(4, 123456, true);
    [0.01, -0.02, 0.03, 0.001, -0.002, 0.003, 0.05, 0.04, 0.03, 1.5, -2.5, 30].forEach((x, i) => v.setFloat32(8 + 4 * i, x, true));
    v.setFloat32(56, 0.004, true);
  }));
  const cat = (...parts) => { const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let o = 0; for (const p of parts) { out.set(p, o); o += p.length; } return out; };
  const orig = dev.handle.bind(dev);
  dev.handle = function (id, seq, pl) {
    if (id !== 0x26 && id !== 0x27 && id !== 0x28) return orig(id, seq, pl);
    this.commands.push({ id, payload: Array.from(pl) }); f.log.push({ id, seq, payload: Array.from(pl), settings: this.settings });
    if (!f.supported) return ack(id, seq, 1, 0);
    if (id === 0x26) return pl.length ? ack(id, seq, 2, f.active) : config(seq);
    if (id === 0x28) return pl.length ? ack(id, seq, 2, 0) : diag(seq);
    let status = 0;
    if (pl.length !== 2 || pl[0] > 3 || pl[0] >= f.profileCount || pl[1] > 1) status = 2;
    else if (!this.settings) status = 3;
    if (f.forceStatus !== null) status = f.forceStatus;
    if (status === 0 && !f.noApply) { f.active = pl[0]; if (pl[1]) f.saved = pl[0]; }
    return cat(ack(id, seq, status, f.active), config(seq));
  };
  const reboot = dev.reboot.bind(dev);
  dev.reboot = function () { reboot(); f.active = f.saved; };
  m.filter = f;
})();
