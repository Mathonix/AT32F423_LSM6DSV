// 静置初始化 VQF 模拟（追加在 mock-serial.js 及其他 mock 之后注入）：按 host-agent-vqf-static-init.md + vqf-init-src/main.c 应答 0x29–0x2D。NOT real hardware.
// 时间按比例压缩：collectMs（实际毫秒）对应设备的 60 s，preMs 对应 5 s，waitMs = 等待放稳时长；上报的 elapsed/remaining 按设备毫秒换算。
// 选项（window.__mock.vqf）：supported（false → 旧固件 ACK 0x01）、silent（0x29/0x2C 不回任何帧 → 3 s 隐藏）、
// startStatus/startDetail（强制开始失败）、error（1–9：采集结束判失败的原因）、noPush（采集中不主动推送，只靠 0x29 补查）、
// restoreFlashFail（0x2D → ACK 0x03 + 原值 0x0E）、busyRestoreSettings（采集中 0x2D 回 0x0703 后仍发一帧 0x0E，覆盖固件可能的行为）、
// source / current / bias / calTempC（初始 0x0E 内容）。
(() => {
  const m = window.__mock; if (!m || m.vqf) return;
  const dev = m.dev, frame = m.frame;
  const le = (n, fn) => { const b = new Uint8Array(n); fn(new DataView(b.buffer)); return b; };
  const DEF = { sigmaInit: 0.5, sigmaRest: 0.035, restGyr: 0.6, restAcc: 0.15 };
  const RESULT = { sigmaInit: 0.1, sigmaRest: 0.035, restGyr: 0.6, restAcc: 0.15 };
  const BLOCKED = [0x19, 0x1c, 0x1d, 0x1e, 0x27, 0x2d, 0x2f, 0x31];
  const v = {
    supported: true, silent: false, startStatus: null, startDetail: 0, error: 0, noPush: false, restoreFlashFail: false, busyRestoreSettings: false,
    waitMs: 300, preMs: 125, collectMs: 1500, pushMs: 100, checkMs: 120,
    source: 0, current: { ...DEF }, defaults: { ...DEF }, bias: [0.3, 0.038, -0.391], calTempC: 0, preBias: null,
    state: 0, err: 0, t0: 0, phaseAt: 0, samples: 0, log: [], pushes: 0, flashWrites: 0, timer: null,
    rate: 0.12, accDev: 0.03, temp: 31.4,
  };
  const DEV_COLLECT = 60000, DEV_PRE = 5000, k = () => DEV_COLLECT / v.collectMs;
  const ack = (cmd, seq, status, detail = 0) => frame(0x90, seq, le(4, (d) => { d.setUint8(0, cmd); d.setUint8(1, status); d.setUint16(2, detail, true); }));
  const cat = (...parts) => { const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let o = 0; for (const p of parts) { out.set(p, o); o += p.length; } return out; };
  const active = () => v.state >= 1 && v.state <= 4;
  function times(now = performance.now()) {
    const el = Math.round((now - v.t0) * k());
    if (v.state === 2) return [el, Math.round((v.preMs - (now - v.phaseAt) + v.collectMs) * k())];
    if (v.state === 3) return [el, Math.max(0, Math.round((v.collectMs - (now - v.phaseAt)) * k()))];
    if (v.state === 1) return [el, Math.round((v.preMs + v.collectMs) * k())];
    return [v.state ? el : 0, 0];
  }
  const status = (seq) => frame(0x0d, seq, le(28, (d) => {
    const [el, rem] = times();
    [1, v.state, v.err, v.source].forEach((x, i) => d.setUint8(i, x));
    d.setUint32(4, Math.max(0, el), true); d.setUint32(8, Math.max(0, rem), true); d.setUint32(12, v.samples, true);
    d.setFloat32(16, v.rate, true); d.setFloat32(20, v.accDev, true); d.setFloat32(24, v.temp, true);
  }));
  const settings = (seq) => frame(0x0e, seq, le(52, (d) => {
    [1, v.source, v.source, 0].forEach((x, i) => d.setUint8(i, x));
    v.bias.forEach((x, i) => d.setFloat32(4 + 4 * i, x, true));
    ['sigmaInit', 'sigmaRest', 'restGyr', 'restAcc'].forEach((key, i) => { d.setFloat32(16 + 4 * i, v.current[key], true); d.setFloat32(32 + 4 * i, v.defaults[key], true); });
    d.setFloat32(48, v.source ? v.calTempC : 0, true);
  }));
  const push = (bytes) => { v.pushes++; m.port.push(bytes); };
  function step() {
    const now = performance.now();
    if (!active()) return;
    if (v.state === 1 && now - v.phaseAt >= v.waitMs) { v.state = 2; v.phaseAt = now; }
    else if (v.state === 2 && now - v.phaseAt >= v.preMs) { v.state = 3; v.phaseAt = now; v.samples = 0; }
    else if (v.state === 3) {
      v.samples = Math.round(Math.min(1, (now - v.phaseAt) / v.collectMs) * 120000);
      if (v.error === 1 && now - v.phaseAt >= v.collectMs / 2) { v.rate = 3.5; v.state = 6; v.err = 1; return end(); }
      if (now - v.phaseAt >= v.collectMs) { v.state = 4; v.phaseAt = now; }
    } else if (v.state === 4 && now - v.phaseAt >= v.checkMs) {
      if (v.error) { v.state = 6; v.err = v.error; } else {
        v.state = 5; v.err = 0; v.source = 1; v.current = { ...RESULT }; v.bias = [0.2971, 0.0402, -0.3887]; v.calTempC = v.temp; v.flashWrites++;
      }
      return end();
    }
    if (!v.noPush) push(status(0));
    v.timer = setTimeout(step, v.pushMs);
  }
  function end() { clearTimeout(v.timer); v.timer = null; push(v.state === 5 ? cat(status(0), settings(0)) : status(0)); }
  function begin() { v.state = 1; v.err = 0; v.t0 = v.phaseAt = performance.now(); v.samples = 0; v.rate = 0.12; clearTimeout(v.timer); v.timer = setTimeout(step, v.pushMs); }
  const orig = dev.handle.bind(dev);
  dev.handle = function (id, seq, pl) {
    if (active() && BLOCKED.includes(id)) {
      this.commands.push({ id, payload: Array.from(pl) }); v.log.push({ id, seq, blocked: true, settings: this.settings });
      return id === 0x2d && v.busyRestoreSettings ? cat(ack(id, seq, 3, 0x0703), settings(seq)) : ack(id, seq, 3, 0x0703);
    }
    if (id < 0x29 || id > 0x2d) return orig(id, seq, pl);
    this.commands.push({ id, payload: Array.from(pl) }); v.log.push({ id, seq, payload: Array.from(pl), settings: this.settings, state: v.state });
    if (!v.supported) return ack(id, seq, 1);
    if (id === 0x29) return v.silent ? null : pl.length ? ack(id, seq, 2) : status(seq);
    if (id === 0x2c) return v.silent ? null : pl.length ? ack(id, seq, 2) : settings(seq);
    if (id === 0x2a) {
      if (pl.length) return ack(id, seq, 2);
      if (v.startStatus !== null) return ack(id, seq, v.startStatus, v.startDetail);
      if (!this.settings) return ack(id, seq, 3, 0);
      if (active()) return ack(id, seq, 3, 0x0701);
      if (!v.preBias) v.preBias = [...v.bias];
      begin(); return cat(ack(id, seq, 0), status(seq));
    }
    if (id === 0x2b) {
      if (pl.length) return ack(id, seq, 2);
      clearTimeout(v.timer); v.timer = null; v.state = 0; v.err = 0; v.samples = 0;
      return cat(ack(id, seq, 0), status(seq));
    }
    // 0x2D（main.c：status 不为 0 时仍回 0x0E）
    let st = 0;
    if (pl.length) st = 2; else if (!this.settings) st = 3; else if (v.restoreFlashFail) st = 3;
    if (st === 0) { v.source = 0; v.current = { ...v.defaults }; v.calTempC = 0; if (v.preBias) v.bias = [...v.preBias]; v.flashWrites++; }
    return cat(ack(id, seq, st), settings(seq));
  };
  m.vqf = v;
})();
