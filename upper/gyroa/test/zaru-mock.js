// 零角速保持阈值模拟（追加在 mock-serial.js + filter-mock.js 之后注入）：按 host-agent-zaru-limits.md 规格应答 0x2E / 0x2F。NOT real hardware.
// 选项（window.__mock.zaru）：supported（false → 旧固件 ACK 0x01）、flag（0x0F supported 字节，0 → 固件未启用）、
// flashFail（persist=1 时 Flash 写失败 → ACK 0x03，运行值不变）、dropReply（0x2F 只回 ACK，不回 0x0F）、dropAll（0x2F 无任何应答）、
// version / badLength / corrupt（构造异常 0x0F）、noApply（ACK 0 但设备不改值）。
// restore1 · 0x31 恢复默认：restore（false → 旧固件 ACK 0x01，不回 0x0F）、restoreWrong（ACK 0 但回读不是默认值）、
// restoreSavedWrong（persist=1 时已保存值未变）；flashFail / dropReply / dropAll 同样作用于 0x31。log 中 0x31 也记录 settings。
(() => {
  const m = window.__mock; if (!m || m.zaru) return;
  const dev = m.dev, frame = m.frame;
  const le = (n, fn) => { const b = new Uint8Array(n); fn(new DataView(b.buffer)); return b; };
  const DEF = { enterDps: 0.3, exitDps: 0.7, accDevMs2: 0.15, enterFilterMs: 10, enterConfirmMs: 50, exitConfirmMs: 3 };
  const fr = Math.fround;
  const z = {
    supported: true, restore: true, restoreWrong: false, restoreSavedWrong: false, flag: 1, flashFail: false, dropReply: false, dropAll: false, noApply: false,
    version: 1, badLength: false, corrupt: null, // corrupt: 'nan' | 'range'（运行值 enter=NaN / 9.0）
    runtime: { ...DEF }, saved: { ...DEF }, log: [], flashWrites: 0,
  };
  const ack = (cmd, seq, status) => frame(0x90, seq, le(4, (v) => { v.setUint8(0, cmd); v.setUint8(1, status); v.setUint16(2, 0, true); }));
  const put = (v, o, s) => { v.setFloat32(o, s.enterDps, true); v.setFloat32(o + 4, s.exitDps, true); v.setFloat32(o + 8, s.accDevMs2, true); v.setUint16(o + 12, s.enterFilterMs, true); v.setUint16(o + 14, s.enterConfirmMs, true); v.setUint16(o + 16, s.exitConfirmMs, true); };
  const limits = (seq) => frame(0x0f, seq, le(z.badLength ? 38 : 40, (v) => {
    v.setUint8(0, z.version); v.setUint8(1, z.flag); v.setUint16(2, 0, true);
    if (!z.flag) return;
    const r = { ...z.runtime }; if (z.corrupt === 'nan') r.enterDps = NaN; if (z.corrupt === 'range') r.enterDps = 9;
    if (v.byteLength >= 40) { put(v, 4, r); put(v, 22, z.saved); } else put(v, 4, r);
  }));
  const cat = (...parts) => { const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let o = 0; for (const p of parts) { out.set(p, o); o += p.length; } return out; };
  const valid = (s) => [s.enterDps, s.exitDps, s.accDevMs2].every(Number.isFinite) &&
    s.enterDps >= fr(0.05) && s.enterDps <= fr(2) && s.exitDps > s.enterDps && s.exitDps <= fr(5) && s.accDevMs2 >= fr(0.02) && s.accDevMs2 <= fr(2) &&
    s.enterFilterMs <= 200 && s.enterConfirmMs <= 2000 && s.exitConfirmMs <= 500;
  const orig = dev.handle.bind(dev);
  dev.handle = function (id, seq, pl) {
    if (id === 0x31) {
      this.commands.push({ id, payload: Array.from(pl) }); z.log.push({ id, seq, payload: Array.from(pl), settings: this.settings });
      if (!z.supported || !z.restore) return ack(id, seq, 1); // 旧固件：未知命令，只回 ACK
      if (z.dropAll) return null;
      let status = 0;
      if (!z.flag) status = 3;
      else if (!this.settings) status = 3;
      else if (pl.length !== 2 || pl[0] > 1 || pl[1] !== 0) status = 2;
      else if (pl[0] && z.flashFail) status = 3;
      if (status === 0) {
        z.runtime = z.restoreWrong ? { ...DEF, exitConfirmMs: 4 } : { ...DEF };
        if (pl[0] && !z.restoreSavedWrong) { z.saved = { ...DEF }; z.flashWrites++; }
      }
      return z.dropReply ? ack(id, seq, status) : cat(ack(id, seq, status), limits(seq));
    }
    if (id !== 0x2e && id !== 0x2f) return orig(id, seq, pl);
    this.commands.push({ id, payload: Array.from(pl) }); z.log.push({ id, seq, payload: Array.from(pl), settings: this.settings });
    if (!z.supported) return ack(id, seq, 1);
    if (id === 0x2e) return pl.length ? ack(id, seq, 2) : limits(seq);
    if (z.dropAll) return null;
    const v = new DataView(pl.buffer, pl.byteOffset, pl.length);
    let status = 0, next = null;
    if (pl.length !== 20 || pl[19] !== 0 || pl[18] > 1) status = 2;
    else {
      next = { enterDps: v.getFloat32(0, true), exitDps: v.getFloat32(4, true), accDevMs2: v.getFloat32(8, true), enterFilterMs: v.getUint16(12, true), enterConfirmMs: v.getUint16(14, true), exitConfirmMs: v.getUint16(16, true) };
      if (!z.flag || !valid(next)) status = 2;
      else if (!this.settings) status = 3;
      else if (pl[18] && z.flashFail) status = 3; // Flash 写入失败：运行值保持原值
    }
    if (status === 0 && !z.noApply) { z.runtime = { ...next }; if (pl[18]) { z.saved = { ...next }; z.flashWrites++; } }
    return z.dropReply ? ack(id, seq, status) : cat(ack(id, seq, status), limits(seq));
  };
  const reboot = dev.reboot.bind(dev);
  dev.reboot = function () { reboot(); z.runtime = { ...z.saved }; };
  m.zaru = z;
})();
