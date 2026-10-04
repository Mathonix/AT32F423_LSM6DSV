// 应用固件版本模拟（追加在 mock-serial.js 之后注入）：按 host-agent-firmware-version.md 应答 0x23 → 0x32（16 B，不回 ACK）。NOT real hardware.
// 选项（window.__mock.fwver）：supported（false → 旧固件 ACK 0x01）、text（默认 20261002b）、silent（不回复）、
// corrupt：'format'（format=2）| 'len'（长度 14，无 0 结尾）| 'ascii'（非 ASCII）| 'short'（15 字节）。
(() => {
  const m = window.__mock; if (!m || m.fwver) return;
  const dev = m.dev, frame = m.frame;
  const v = { supported: true, text: '20261002b', silent: false, corrupt: null, log: [] };
  const ack = (cmd, seq, status) => frame(0x90, seq, Uint8Array.from([cmd, status, 0, 0]));
  const reply = (seq) => {
    const b = new Uint8Array(v.corrupt === 'short' ? 15 : 16), t = [...v.text].map((c) => c.charCodeAt(0));
    b[0] = v.corrupt === 'format' ? 2 : 1; b[1] = v.corrupt === 'len' ? 14 : t.length; b.set(t.slice(0, 14), 2);
    if (v.corrupt === 'len') b.fill(0x41, 2, 16);
    if (v.corrupt === 'ascii') b[3] = 0xc3;
    return frame(0x32, seq, b);
  };
  const orig = dev.handle.bind(dev);
  dev.handle = function (id, seq, pl) {
    if (id !== 0x23) return orig(id, seq, pl);
    this.commands.push({ id, payload: Array.from(pl) }); v.log.push({ id, seq, payload: Array.from(pl) });
    if (!v.supported) return ack(id, seq, 1);
    if (pl.length) return ack(id, seq, 2);
    return v.silent ? null : reply(seq);
  };
  m.fwver = v;
})();
