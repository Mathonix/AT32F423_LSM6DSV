// app1 test copy of gyro-1001f-compact/test/mock-serial.js + six-face ACC_CAL simulation + Bootloader v1 simulation + usb1 multi-port / USB CDC re-enumeration simulation (mock only)
// 注入到页面（evaluateOnNewDocument）：模拟 navigator.serial + 按固件 main.c protocol_frame_received 行为应答的设备
(() => {
  const crc16 = (b, s, e) => { let c = 0xffff; for (let i = s; i < e; i++) { c ^= b[i] << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; };
  const frame = (id, seq, payload) => { const f = new Uint8Array(7 + payload.length); f.set([0xaa, 0x55, id, payload.length, seq & 0xff]); f.set(payload, 5); const c = crc16(f, 2, 5 + payload.length); f[5 + payload.length] = c & 0xff; f[6 + payload.length] = c >> 8; return f; };
  const le = (n, fn) => { const b = new Uint8Array(n); fn(new DataView(b.buffer)); return b; };
  const crc32 = (bytes) => { let c = 0xffffffff; for (const b of bytes) { c ^= b; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ ((c & 1) ? 0xedb88320 : 0); } return (c ^ 0xffffffff) >>> 0; };
  const i16 = (x) => Math.trunc(Math.fround(x)) & 0xffff;
  const dev = {
    streamMode: 0, settings: 0, dirty: 0, outHz: 1000, fusionHz: 2000, canId: 1, emitting: true, seq: 0,
    pose: { yaw: 12.5, pitch: -3.25, roll: 45.5, gz: 1.5, az: 9.75, temp: 31.5, gx: 0.25, gy: -0.5, ax: 0.125, ay: -0.25 },
    commands: [], rx: [],
    extended: false, source: 1, capabilities: 7, activeMode: 1, savedMode: 1, activeFast: 0, savedFast: 0, failSave: false,
    configVersion: 1, activeInitMs: 2000, savedInitMs: 2000,
    activeRangeDps:1000, savedRangeDps:1000, savedOutputHz:1000,
    outputs: [{ format: 2, legacyMode: 0, mask: 7 }, { format: 2, legacyMode: 0, mask: 7 }],
    savedOutputs: [{ format: 2, legacyMode: 0, mask: 7 }, { format: 2, legacyMode: 0, mask: 7 }],
    canExtended: false,
    can: { nodeId: 1, masterId: 1791, periodMs: 1, baud: 0, active: 1, mask: 4, reserved: 0 },
    savedCan: { nodeId: 1, masterId: 1791, periodMs: 1, baud: 0, active: 1, mask: 4, reserved: 0 },
    canConfig() { return frame(8, this.seq++, le(24, (v) => {
      v.setUint8(0, 1); v.setUint8(1, 1);
      [this.can, this.savedCan].forEach((c, i) => { const o = 2 + 10 * i;
        v.setUint16(o, c.nodeId, true); v.setUint16(o + 2, c.masterId, true); v.setUint16(o + 4, c.periodMs, true);
        [c.baud, c.active, c.mask, 0].forEach((x, k) => v.setUint8(o + 6 + k, x));
      });
    })); },
    config() { return frame(7, this.seq++, le({1:18,2:22,3:28,4:28}[this.configVersion], (v) => {
      [this.configVersion, this.source, this.activeMode, this.savedMode, this.activeFast, this.savedFast, this.capabilities, 0].forEach((x, i) => v.setUint8(i, x));
      v.setUint16(8, this.outHz, true);
      this.outputs.forEach((o, i) => { const at = 10 + 4 * i; v.setUint8(at, o.format); v.setUint8(at + 1, o.legacyMode); v.setUint16(at + 2, o.mask, true); });
      if (this.configVersion >= 2) { v.setUint16(18, this.activeInitMs, true); v.setUint16(20, this.savedInitMs, true); }
      if (this.configVersion >= 3) { v.setUint16(22,this.activeRangeDps,true); v.setUint16(24,this.savedRangeDps,true); v.setUint16(26,this.savedOutputHz,true); }
    })); },
    reboot() { this.activeMode = this.savedMode; this.activeFast = this.savedFast; this.activeInitMs = this.savedInitMs; if(this.configVersion>=3) {this.activeRangeDps=this.savedRangeDps;this.outHz=this.savedOutputHz;} this.outputs = this.savedOutputs.map((o) => ({ ...o })); this.can = { ...this.savedCan }; this.dirty = 0; this.settings = 0; },
    ack(cmd, status, detail) { return frame(0x90, this.seq++, le(4, (v) => { v.setUint8(0, cmd); v.setUint8(1, status); v.setUint16(2, detail, true); })); },
    telemetry() {
      const p = this.pose;
      const o = this.outputs[this.source];
      if (this.extended && o.format !== 2) {
        const values = ['yaw', 'pitch', 'roll', 'ax', 'ay', 'az', 'gx', 'gy', 'gz'].filter((_, i) => o.mask & (1 << i)).map((k) => p[k]);
        if (!values.length) return new Uint8Array(0);
        if (o.format === 1) return frame(6, this.seq++, le(4 + values.length * 4, (v) => {
          v.setUint16(0, o.mask, true); v.setUint16(2, 1234, true); values.forEach((x, i) => v.setFloat32(4 + 4 * i, x, true));
        }));
        const b = le(values.length * 4 + 4, (v) => values.forEach((x, i) => v.setFloat32(4 * i, x, true)));
        b.set([0, 0, 0x80, 0x7f], values.length * 4); return b;
      }
      if (this.streamMode === 0 || this.streamMode === 4) {
        const ch = this.streamMode === 4 ? [p.yaw, p.pitch, p.roll, p.gz, p.az, p.temp] : [p.yaw, p.pitch, p.roll];
        const b = new Uint8Array(ch.length * 4 + 4); const v = new DataView(b.buffer);
        ch.forEach((x, i) => v.setFloat32(i * 4, x, true)); b.set([0, 0, 0x80, 0x7f], ch.length * 4); return b;
      }
      if (this.streamMode === 1) return frame(0x01, this.seq++, le(16, (v) => { v.setFloat32(0, p.roll, true); v.setFloat32(4, p.pitch, true); v.setFloat32(8, p.yaw, true); v.setUint8(12, 1); v.setUint16(14, 1234, true); }));
      if (this.streamMode === 2) return frame(0x04, this.seq++, le(12, (v) => { v.setUint16(0, i16(p.roll * 100), true); v.setUint16(2, i16(p.pitch * 100), true); v.setUint16(4, i16(p.yaw * 100), true); v.setUint16(6, i16(p.gz * 10), true); v.setUint8(8, 1); v.setUint16(10, 1, true); }));
      return frame(0x03, this.seq++, le(28, (v) => { [p.gx, p.gy, p.gz, p.ax, p.ay, p.az].forEach((x, i) => v.setFloat32(i * 4, x, true)); v.setUint16(24, i16(p.temp * 100), true); v.setUint16(26, 7, true); }));
    },
    accEnabled: true, accFaceMs: 600, accFailFit: false, accTimer: null,
    acc: { status: 0, phase: 0, face: 0, mask: 0, valid: 0, progress: 0, error: 0, raw: [0.01, -0.02, 0.99], bias: [0.002, -0.001, 0.003], scale: [1.001, 0.999, 1.002] },
    accFrame() { const a = this.acc; return frame(0x0a, this.seq++, le(60, (v) => {
      [1, a.status, a.phase, a.face, a.mask, 1, a.valid, 0].forEach((x, i) => v.setUint8(i, x));
      v.setUint16(8, a.progress, true); v.setUint16(10, a.error, true); v.setUint32(12, 120, true); v.setUint32(16, 1500, true); v.setUint32(20, a.status === 1 ? 45000 : 0, true);
      [...a.bias, ...a.scale, ...a.raw].forEach((x, i) => v.setFloat32(24 + 4 * i, x, true));
    })); },
    accStep() {
      const a = this.acc; clearTimeout(this.accTimer);
      if (a.status !== 1) return;
      const next = [0, 1, 2, 3, 4, 5].find((i) => !(a.mask & (1 << i)));
      if (next === undefined) {
        if (this.accFailFit) Object.assign(a, { status: 3, phase: 0, face: 0, progress: 0, error: 0x0606 });
        else Object.assign(a, { status: 2, phase: 0, face: 0, progress: 1000, error: 0, valid: 1 });
        port.push(Uint8Array.from([...(a.status === 2 ? this.ack(0x1c, 0, 0) : this.ack(0x1c, 3, 0x0606)), ...this.accFrame()]));
        return;
      }
      a.face = next + 1; a.raw = [0, 0, 0]; a.raw[next >> 1] = next & 1 ? 1.002 : -0.998;
      a.progress = Math.min(1000, a.progress + 250); a.phase = a.progress < 500 ? 1 : 2;
      if (a.progress >= 1000) { a.mask |= 1 << next; a.progress = 0; a.phase = 0; }
      port.push(this.accFrame());
      this.accTimer = setTimeout(() => this.accStep(), this.accFaceMs / 4);
    },
    // ---- Bootloader v1 (firmware-upgrade.js) ----
    boot: false, bootPending: false, bootRx: [], bootImage: null, bootLog: [], bootBooted: 0,
    bootHandle(chunk) {
      this.bootRx.push(...chunk);
      while (this.bootRx.length >= 18) {
        if (this.bootRx[0] !== 0x42 || this.bootRx[1] !== 0x4c) { this.bootRx.shift(); continue; }
        const head = Uint8Array.from(this.bootRx.slice(0, 18)), v = new DataView(head.buffer);
        const cmd = head[3], seq = v.getUint16(4, true), addr = v.getUint32(6, true), len = v.getUint32(10, true), crc = v.getUint32(14, true);
        const total = 18 + (cmd === 3 ? len : 0);
        if (this.bootRx.length < total) return;
        const payload = Uint8Array.from(this.bootRx.slice(18, total)); this.bootRx.splice(0, total);
        this.bootLog.push(cmd);
        let status = 0, value = 0;
        if (cmd === 1) value = 0x08008000;
        else if (cmd === 2) { this.bootImage = { size: len, crc, data: new Uint8Array(len) }; value = 0; }
        else if (cmd === 3) { const off = addr - 0x08008000; if (crc32(payload) !== crc) status = 3; else { this.bootImage.data.set(payload, off); value = off + len; } }
        else if (cmd === 4) { status = crc32(this.bootImage.data) === crc ? 0 : 3; value = len; }
        else if (cmd === 6) { this.bootBooted++; this.boot = false; }
        const r = new Uint8Array(14), rv = new DataView(r.buffer);
        r.set([0x42, 0x4c, 1, cmd | 0x80, status, 0]); rv.setUint32(6, value, true); rv.setUint32(10, crc32(r.subarray(0, 10)), true);
        setTimeout(() => port.push(r), 2);
      }
    },
    sysinfoFrame(seq) { return frame(0x05, seq, le(16, (v) => { v.setUint32(0, this.fusionHz, true); v.setUint32(4, this.outHz, true); v.setUint16(8, this.fusionHz / this.outHz, true); v.setUint16(10, i16(this.pose.temp * 100), true); v.setUint8(12, this.extended && this.outputs[this.source].format !== 2 ? 255 : this.streamMode); v.setUint8(13, 1); })); },
    seqAck(cmd, seq, status, detail) { return frame(0x90, seq, le(4, (v) => { v.setUint8(0, cmd); v.setUint8(1, status); v.setUint16(2, detail, true); })); },
    handle(id, seq, pl) {
      this.commands.push({ id, payload: Array.from(pl) });
      const S = 0, BAD = 2, FAIL = 3;
      switch (id) {
        case 0x35: return this.modelEnabled ? (pl.length ? this.ack(id,BAD,0) : frame(0x36,seq,Uint8Array.from([65,84,51,50]))) : this.ack(id,1,0);
        case 0x21: return this.canExtended ? this.canConfig() : this.ack(id, 1, 0);
        case 0x22: {
          if (!this.canExtended) return this.ack(id, 1, 0);
          if (!this.settings || (pl[10] && this.failSave)) return this.ack(id, FAIL, 0);
          const v = new DataView(pl.buffer, pl.byteOffset, pl.length);
          const c = { nodeId: v.getUint16(0, true), masterId: v.getUint16(2, true), periodMs: v.getUint16(4, true), baud: pl[6], active: pl[7], mask: pl[8], reserved: pl[9] };
          this.can = c; if (pl[10]) this.savedCan = { ...c };
          return Uint8Array.from([...this.ack(id, S, 0), ...this.canConfig()]);
        }
        case 0x10: return this.ack(id, S, 0);
        case 0x11: return this.ack(id, pl.length ? BAD : S, 0);
        case 0x12: return this.ack(id, FAIL, 1);
        case 0x13: if (pl.length !== 1 || pl[0] > 4) return this.ack(id, BAD, this.streamMode); this.streamMode = pl[0]; if (this.extended) this.outputs.forEach((o) => { o.format = 2; o.legacyMode = pl[0]; }); return this.ack(id, S, this.streamMode);
        case 0x1d: { const hz = pl[0] | (pl[1] << 8); if (pl.length !== 2 || !hz || hz > this.fusionHz || this.fusionHz % hz) return this.ack(id, BAD, this.outHz); if(this.configVersion>=3 && this.failSave) return this.ack(id,FAIL,this.outHz); this.outHz = hz; if(this.configVersion>=3) {this.savedOutputHz=hz;return Uint8Array.from([...this.ack(id,S,hz),...this.config()]);} return this.ack(id, S, hz); }
        case 0x14: {
          // temp1 温度轮询模拟（NOT real hardware）：tempDrift 每次查询累加（默认 0，不影响旧检查）；dropSysinfo=N 丢弃接下来 N 次回复（模拟固件回复队列满）；
          // sysinfoUnsupported → ACK 0x01；sysinfoDelayMs → 延迟回复。ACK 回显请求 seq（固件 protocol_reply_ack）。
          this.sysinfoQueries = (this.sysinfoQueries || 0) + 1;
          if (this.sysinfoUnsupported) return this.seqAck(id, seq, 1, 0);
          if (pl.length) return this.seqAck(id, seq, BAD, 0);
          if (this.tempDrift) this.pose.temp = Math.round((this.pose.temp + this.tempDrift) * 1000) / 1000;
          if (this.dropSysinfo > 0) { this.dropSysinfo--; this.sysinfoDropped = (this.sysinfoDropped || 0) + 1; return null; }
          const r = this.sysinfoFrame(seq);
          if (this.sysinfoDelayMs) r.delayMs = this.sysinfoDelayMs;
          return r;
        }
        case 0x17: this.settings = 1; return this.ack(id, S, 0);
        case 0x18: this.settings = 0; return this.ack(id, S, this.dirty);
        case 0x19: if (!this.settings) return this.ack(id, FAIL, pl[0]); this.dirty = 1; this.savedMode = pl[0]; return this.ack(id, S, pl[0]);
        case 0x1a: if (!this.settings) return this.ack(id, FAIL, this.canId); this.canId = pl[0] | (pl[1] << 8); this.dirty = 1; return this.ack(id, S, this.canId);
        case 0x1b: return this.ack(id, FAIL, 0x0601);
        case 0x1c: {
          if (!this.accEnabled) return this.ack(id, FAIL, 0x0600);
          if (!this.settings) return this.ack(id, FAIL, 0x0602);
          if (this.acc.status === 1) return this.ack(id, FAIL, 0x0604);
          Object.assign(this.acc, { status: 1, phase: 0, face: 0, mask: 0, progress: 0, error: 0 });
          this.accStep();
          return this.ack(id, S, 0x0100);
        }
        case 0x24: return this.accEnabled ? this.accFrame() : this.ack(id, 1, 0);
        case 0x25: {
          if (this.acc.status !== 1) return this.ack(id, FAIL, 0);
          clearTimeout(this.accTimer); Object.assign(this.acc, { status: 4, phase: 0, face: 0, progress: 0, error: 0x0608 });
          return Uint8Array.from([...this.ack(id, S, 0), ...this.accFrame()]);
        }
        case 0x16: this.bootPending = true; return null;
        case 0x1f: return this.extended ? this.config() : this.ack(id, 1, 0);
        case 0x1e: {
          if (!this.extended) return this.ack(id, 1, 0);
          const duration = pl.length >= 5 ? pl[3] | (pl[4] << 8) : this.savedInitMs;
          const range = pl.length===7 ? pl[5]|(pl[6]<<8) : this.savedRangeDps;
          if (![3, ...(this.configVersion >= 2 ? [5] : []), ...(this.configVersion>=3 ? [7] : [])].includes(pl.length) || ![125,250,500,1000,2000,4000].includes(range) || duration < (this.configVersion>=4 ? 0 : 100) || duration > 60000 || pl[0] > 2 || pl[1] > 1 || pl[2] > 1 || (!(this.capabilities & 1) && pl[0])) return this.ack(id, BAD, 0);
          if (!this.settings || this.failSave) return this.ack(id, FAIL, 0);
          this.savedMode = pl[0]; this.savedFast = pl[1]; this.savedInitMs = duration; this.savedRangeDps=range; this.dirty = +(this.activeMode !== this.savedMode || this.activeFast !== this.savedFast || this.activeInitMs !== this.savedInitMs || this.activeRangeDps!==this.savedRangeDps);
          if (pl[2]) this.reboot();
          const ack = this.ack(id, S, pl[0]); const cfg = this.config(); return Uint8Array.from([...ack, ...cfg]);
        }
        case 0x20: {
          if (!this.extended) return this.ack(id, 1, 0);
          const mask = pl[2] | (pl[3] << 8);
          if (pl.length !== 5 || pl[0] > 1 || pl[1] > 1 || mask > 511 || pl[4] > 1) return this.ack(id, BAD, 0);
          if (pl[4] && this.failSave) return this.ack(id, FAIL, pl[0]);
          const o = { format: pl[1], legacyMode: 0, mask };
          this.outputs[pl[0]] = o;
          if (pl[4]) this.savedOutputs[pl[0]] = { ...o };
          const ack = this.ack(id, S, pl[0]); const cfg = this.config(); return Uint8Array.from([...ack, ...cfg]);
        }
        default: return this.ack(id, 1, 0);
      }
    },
    onHostBytes(chunk) {
      this.rx.push(...chunk);
      for (;;) {
        const i = this.rx.findIndex((b, k) => b === 0xaa && this.rx[k + 1] === 0x55);
        if (i < 0) { this.rx = []; return; }
        this.rx.splice(0, i);
        if (this.rx.length < 7) return;
        const len = this.rx[3]; if (this.rx.length < 7 + len) return;
        const f = Uint8Array.from(this.rx.splice(0, 7 + len));
        if (crc16(f, 2, 5 + len) !== (f[5 + len] | (f[6 + len] << 8))) continue;
        const reply = this.handle(f[2], f[4], f.subarray(5, 5 + len));
        if (reply) setTimeout(() => port.push(reply), reply.delayMs || 2);
      }
    },
  };
  // ---- 多端口 navigator.serial 模拟（usb1）：mock only — NOT real hardware ----
  // 默认：单个 2E3C:5740 对象（按 UART 处理：0x16 后同一对象重开即进入 Bootloader，与旧版 mock 一致）。
  // setup({ transport:'usb' }) 后板子信息为 2E3C:F401：0x16 / BOOT 会让当前 SerialPort 对象失效（open 抛 NetworkError，
  // 从 getPorts 消失），随后 getPorts() 给出同 VID/PID 的新对象（Chrome 重新枚举行为）。
  const cfg = {
    transport: 'uart', info: { usbVendorId: 0x2e3c, usbProductId: 0x5740 },
    uartIgnoreEnter: false,
    usbEnter: { delayMs: 30, appearMs: 300, reuse: false, never: false, extraFresh: 0, transientMs: 0, hangOld: false },
    usbBoot: { delayMs: 30, appearMs: 300, reuse: false, extraFresh: 0, stale: false, firstOpenDelayMs: 0 },
    getPortsThrows: false, failDataAt: -1,
    // usb2：eventStyle 'target'（默认，event.target=端口）或 'port'（event.port=端口，event.target=navigator.serial）
    eventStyle: 'target', dropBootAck: false, dropPing: false, pingStatus: 0,
  };
  const calls = { requestPort: 0, getPorts: 0, writes: 0, open: [], events: [] };
  const all = [];
  let nextId = 0;
  class FakePort {
    constructor(info, role = 'board') {
      this.id = nextId++; this.info = { ...info }; this.role = role; this.dead = false; this.listed = true;
      this.readable = null; this.writable = null; this.openCalls = []; this.closeCalls = 0; this.ctrl = null;
      this.hangClose = false; this.hangCancel = false; this.hangOpen = false; this.openDelayOnce = 0; this.lateCloses = 0;
      all.push(this);
    }
    async open(opts) {
      calls.open.push({ id: this.id, t: performance.now(), role: this.role });
      if (this.hangOpen) return new Promise(() => {}); // 已消失的旧 CDC 对象：open() 永不 resolve
      if (this.opening) throw new DOMException('A call to open() is already in progress.', 'InvalidStateError');
      if (this.openDelayOnce) { const d = this.openDelayOnce; this.openDelayOnce = 0; this.opening = true; await new Promise((r) => setTimeout(r, d)); this.opening = false; this.lateOpenAt = performance.now(); }
      if (this.dead) throw new DOMException('Failed to open serial port.', 'NetworkError');
      if (this.readable) throw new DOMException('The port is already open.', 'InvalidStateError');
      this.openCalls.push(opts);
      this.readable = new ReadableStream({ start: (c) => { this.ctrl = c; }, cancel: () => { this.ctrl = null; if (this.hangCancel) return new Promise(() => {}); } });
      if (this === port && cfg.transport === 'uart' && dev.bootPending) { dev.bootPending = false; dev.boot = true; dev.settings = 0; }
      this.writable = new WritableStream({ write: (chunk) => {
        calls.writes++;
        if (this.dead || this.role !== 'board' || this !== port) return;
        return dev.boot ? dev.bootHandle(chunk) : dev.onHostBytes(chunk);
      } });
    }
    async close() {
      if (this.hangClose) return new Promise(() => {});
      if ((this.readable && this.readable.locked) || (this.writable && this.writable.locked)) throw new TypeError('Cannot close a port with locked streams.');
      this.closeCalls++; this.readable = null; this.writable = null; this.ctrl = null;
    }
    push(bytes) { try { if (this.ctrl && !this.dead) this.ctrl.enqueue(bytes); } catch { /* closed */ } }
    getInfo() { return { ...this.info }; }
  }
  let port = new FakePort(cfg.info);
  const listeners = { connect: [], disconnect: [] };
  const fire = (type, target) => {
    calls.events.push({ type, id: target.id, t: performance.now() });
    const event = cfg.eventStyle === 'port' ? { type, port: target, target: serial } : { type, target };
    for (const fn of [...listeners[type]]) fn(event);
  };
  // 旧对象失效：读流报 NetworkError（hangOld 时模拟句柄卡住：不报错、cancel/close 不返回），从 getPorts 消失
  function kill(p, hang) {
    p.dead = true; p.listed = false;
    if (hang) { p.hangClose = true; p.hangCancel = true; }
    else { const c = p.ctrl; p.ctrl = null; try { c && c.error(new DOMException('The device has been lost.', 'NetworkError')); } catch { /* */ } }
    fire('disconnect', p);
  }
  function appear(p) { p.dead = false; p.listed = true; fire('connect', p); }
  function reenumerate(o, toBoot) {
    const old = port;
    if (o.hangOld) { old.hangClose = true; old.hangCancel = true; } // 旧 CDC 句柄在 teardown 时就卡住
    setTimeout(() => {
      kill(old, !!o.hangOld);
      dev.boot = toBoot; dev.settings = 0;
      if (o.never) return;
      if (o.stale) {
        // staleReconnect：旧 Bootloader 对象仍留在 getPorts()，open() 永不 resolve；新应用口不进 getPorts()，由测试 emitConnect
        old.listed = true; old.dead = false; old.hangOpen = true; old.hangSince = performance.now();
        const np = new FakePort(cfg.info); np.listed = false; port = np; pendingStale = np;
        return;
      }
      const finish = () => {
        if (o.reuse) { old.hangClose = false; old.hangCancel = false; old.readable = null; old.writable = null; old.ctrl = null; port = old; if (o.firstOpenDelayMs) old.openDelayOnce = o.firstOpenDelayMs; appear(old); }
        else { port = new FakePort(cfg.info); if (o.firstOpenDelayMs) port.openDelayOnce = o.firstOpenDelayMs; fire('connect', port); }
        for (let i = 0; i < (o.extraFresh || 0); i++) fire('connect', new FakePort(cfg.info, 'silent'));
      };
      if (o.transientMs) {
        const t = new FakePort(cfg.info, 'silent'); fire('connect', t);
        setTimeout(() => { kill(t, false); finish(); }, o.transientMs);
      } else setTimeout(finish, o.appearMs);
    }, o.delayMs);
  }
  const baseHandle = dev.handle.bind(dev);
  dev.handle = function (id, seq, pl) {
    if (id === 0x16) {
      this.commands.push({ id, payload: Array.from(pl) });
      if (cfg.transport === 'uart') {
        if (cfg.uartIgnoreEnter) return this.ack(id, 1, 0);
        this.bootPending = true; return null;
      }
      reenumerate(cfg.usbEnter, true); return this.ack(id, 0, 0);
    }
    if (id === 0x10 && this.bootBooted > 0 && (cfg.dropPing || cfg.pingStatus)) {
      this.commands.push({ id, payload: Array.from(pl) });
      return cfg.dropPing ? null : this.ack(id, cfg.pingStatus, 0);
    }
    if (id === 0x10 && this.bootBooted > 0) this.pingAckAt = this.pingAckAt || performance.now();
    return baseHandle(id, seq, pl);
  };
  const baseBoot = dev.bootHandle.bind(dev);
  dev.bootHandle = function (chunk) {
    // failDataAt：第 N 个 DATA 回 Flash 错误（模拟 BEGIN 之后失败）
    if (cfg.failDataAt >= 0 && chunk[0] === 0x42 && chunk[3] === 3 && this.bootLog.filter((c) => c === 3).length === cfg.failDataAt) {
      this.bootLog.push(3); const r = new Uint8Array(14), rv = new DataView(r.buffer);
      r.set([0x42, 0x4c, 1, 0x83, 4, 0]); rv.setUint32(6, 0, true); rv.setUint32(10, crc32(r.subarray(0, 10)), true);
      setTimeout(() => port.push(r), 2); return;
    }
    if (cfg.dropBootAck && chunk[0] === 0x42 && chunk[3] === 6) {
      // BOOT 已执行但 ACK 丢失
      this.bootLog.push(6); this.bootBooted++; this.boot = false; this.bootDoneAt = performance.now();
      if (cfg.transport === 'usb') { this.boot = true; setTimeout(() => reenumerate(cfg.usbBoot, false), 20); }
      return;
    }
    const before = this.bootBooted; baseBoot(chunk);
    if (this.bootBooted > before) this.bootDoneAt = performance.now();
    if (this.bootBooted > before && cfg.transport === 'usb') { this.boot = true; setTimeout(() => reenumerate(cfg.usbBoot, false), 20); }
  };
  const serial = {
    requestPort: async () => { calls.requestPort++; return port; },
    getPorts: async () => { calls.getPorts++; if (cfg.getPortsThrows) throw new DOMException('getPorts failed (mock)', 'SecurityError'); return all.filter((p) => p.listed); },
    addEventListener: (type, fn) => { if (listeners[type] && !listeners[type].includes(fn)) listeners[type].push(fn); },
    removeEventListener: (type, fn) => { const l = listeners[type]; const i = l ? l.indexOf(fn) : -1; if (i >= 0) l.splice(i, 1); },
  };
  let pendingStale = null;
  Object.defineProperty(Navigator.prototype, 'serial', { get: () => serial, configurable: true });
  setInterval(() => { if (dev.emitting && !dev.boot && port.ctrl && !port.dead) { const parts = [dev.telemetry(), dev.telemetry()]; const b = new Uint8Array(parts[0].length + parts[1].length); b.set(parts[0]); b.set(parts[1], parts[0].length); port.push(b); } }, 10);
  window.__mock = {
    dev, frame, cfg, calls, all, FakePort,
    get port() { return port; },
    get pendingStale() { return pendingStale; },
    listenerCount: (type) => (listeners[type] || []).length,
    // 测试主动发 connect 事件（不放进 getPorts）
    emitConnect(id) { const p = all.find((x) => x.id === id); fire('connect', p); return performance.now(); },
    setup(o = {}) {
      for (const k of ['usbEnter', 'usbBoot']) if (o[k]) Object.assign(cfg[k], o[k]);
      for (const [k, v] of Object.entries(o)) if (k !== 'usbEnter' && k !== 'usbBoot') cfg[k] = v;
      if (o.transport === 'usb' && !o.info) cfg.info = { usbVendorId: 0x2e3c, usbProductId: 0xf401 };
      port.info = { ...cfg.info };
      return cfg;
    },
    // 点击前已授权的另一块同 VID/PID 板子（不应被使用）
    addOther(info = cfg.info) { const p = new FakePort(info, 'other'); return p.id; },
    byId(id) { return all.find((p) => p.id === id); },
    unplug() {
      const c = port.ctrl; port.ctrl = null;
      try { c && c.error(new DOMException('The device has been lost.', 'NetworkError')); } catch { /* */ }
      fire('disconnect', port);
    },
  };
})();
