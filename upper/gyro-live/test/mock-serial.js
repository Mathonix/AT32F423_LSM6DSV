// 注入到页面（evaluateOnNewDocument）：模拟 navigator.serial + 按固件 main.c protocol_frame_received 行为应答的设备
(() => {
  const crc16 = (b, s, e) => { let c = 0xffff; for (let i = s; i < e; i++) { c ^= b[i] << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff; } return c; };
  const frame = (id, seq, payload) => { const f = new Uint8Array(7 + payload.length); f.set([0xaa, 0x55, id, payload.length, seq & 0xff]); f.set(payload, 5); const c = crc16(f, 2, 5 + payload.length); f[5 + payload.length] = c & 0xff; f[6 + payload.length] = c >> 8; return f; };
  const le = (n, fn) => { const b = new Uint8Array(n); fn(new DataView(b.buffer)); return b; };
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
    filterExtended:false, filterProfile:1, savedFilterProfile:1,
    filterConfig() { return frame(11,this.seq++,le(16,v=>{
      [1,this.filterProfile,this.savedFilterProfile,4].forEach((x,i)=>v.setUint8(i,x));
      v.setUint16(4,1000,true);v.setFloat32(8,[2,4,6,4][this.filterProfile],true);
      v.setFloat32(12,[.15,.5,1.5,.5][this.filterProfile],true);
    })); },
    diagnostic() {return frame(12,this.seq++,le(60,v=>{
      [1,this.filterProfile,1,1].forEach((x,i)=>v.setUint8(i,x));v.setUint32(4,123456,true);
      [1,2,3,.01,.02,.03,.001,.002,.003,this.pose.roll,this.pose.pitch,this.pose.yaw,.05]
        .forEach((x,i)=>v.setFloat32(8+4*i,x,true));
    }));},
    can: { nodeId: 1, masterId: 1791, periodMs: 1, baud: 0, active: 1, mask: 4, reserved: 0 },
    savedCan: { nodeId: 1, masterId: 1791, periodMs: 1, baud: 0, active: 1, mask: 4, reserved: 0 },
    canConfig() { return frame(8, this.seq++, le(24, (v) => {
      v.setUint8(0, 1); v.setUint8(1, 1);
      [this.can, this.savedCan].forEach((c, i) => { const o = 2 + 10 * i;
        v.setUint16(o, c.nodeId, true); v.setUint16(o + 2, c.masterId, true); v.setUint16(o + 4, c.periodMs, true);
        [c.baud, c.active, c.mask, 0].forEach((x, k) => v.setUint8(o + 6 + k, x));
      });
    })); },
    config() { return frame(7, this.seq++, le({1:18,2:22,3:28}[this.configVersion], (v) => {
      [this.configVersion, this.source, this.activeMode, this.savedMode, this.activeFast, this.savedFast, this.capabilities, 0].forEach((x, i) => v.setUint8(i, x));
      v.setUint16(8, this.outHz, true);
      this.outputs.forEach((o, i) => { const at = 10 + 4 * i; v.setUint8(at, o.format); v.setUint8(at + 1, o.legacyMode); v.setUint16(at + 2, o.mask, true); });
      if (this.configVersion >= 2) { v.setUint16(18, this.activeInitMs, true); v.setUint16(20, this.savedInitMs, true); }
      if (this.configVersion === 3) { v.setUint16(22,this.activeRangeDps,true); v.setUint16(24,this.savedRangeDps,true); v.setUint16(26,this.savedOutputHz,true); }
    })); },
    reboot() { this.filterProfile=this.savedFilterProfile; this.activeMode = this.savedMode; this.activeFast = this.savedFast; this.activeInitMs = this.savedInitMs; if(this.configVersion===3) {this.activeRangeDps=this.savedRangeDps;this.outHz=this.savedOutputHz;} this.outputs = this.savedOutputs.map((o) => ({ ...o })); this.can = { ...this.savedCan }; this.dirty = 0; this.settings = 0; },
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
    handle(id, seq, pl) {
      this.commands.push({ id, payload: Array.from(pl) });
      const S = 0, BAD = 2, FAIL = 3;
      switch (id) {
        case 0x26: return this.filterExtended ? this.filterConfig() : this.ack(id,1,0);
        case 0x27: {
          if(!this.filterExtended) return this.ack(id,1,0);
          if(!this.settings || (pl[1] && this.failSave)) return this.ack(id,FAIL,this.filterProfile);
          if(pl.length!==2 || pl[0]>3 || pl[1]>1) return this.ack(id,BAD,this.filterProfile);
          this.filterProfile=pl[0];if(pl[1]) this.savedFilterProfile=pl[0];
          return Uint8Array.from([...this.ack(id,S,this.filterProfile),...this.filterConfig()]);
        }
        case 0x28: return this.filterExtended ? this.diagnostic() : this.ack(id,1,0);
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
        case 0x1d: { const hz = pl[0] | (pl[1] << 8); if (pl.length !== 2 || !hz || hz > this.fusionHz || this.fusionHz % hz) return this.ack(id, BAD, this.outHz); if(this.configVersion===3 && this.failSave) return this.ack(id,FAIL,this.outHz); this.outHz = hz; if(this.configVersion===3) {this.savedOutputHz=hz;return Uint8Array.from([...this.ack(id,S,hz),...this.config()]);} return this.ack(id, S, hz); }
        case 0x14: return frame(0x05, seq, le(16, (v) => { v.setUint32(0, this.fusionHz, true); v.setUint32(4, this.outHz, true); v.setUint16(8, this.fusionHz / this.outHz, true); v.setUint16(10, i16(this.pose.temp * 100), true); v.setUint8(12, this.extended && this.outputs[this.source].format !== 2 ? 255 : this.streamMode); v.setUint8(13, 1); }));
        case 0x17: this.settings = 1; return this.ack(id, S, 0);
        case 0x18: this.settings = 0; return this.ack(id, S, this.dirty);
        case 0x19: if (!this.settings) return this.ack(id, FAIL, pl[0]); this.dirty = 1; this.savedMode = pl[0]; return this.ack(id, S, pl[0]);
        case 0x1a: if (!this.settings) return this.ack(id, FAIL, this.canId); this.canId = pl[0] | (pl[1] << 8); this.dirty = 1; return this.ack(id, S, this.canId);
        case 0x1b: return this.ack(id, FAIL, 0x0601);
        case 0x1c: return this.ack(id, FAIL, 0x0600);
        case 0x1f: return this.extended ? this.config() : this.ack(id, 1, 0);
        case 0x1e: {
          if (!this.extended) return this.ack(id, 1, 0);
          const duration = pl.length >= 5 ? pl[3] | (pl[4] << 8) : this.savedInitMs;
          const range = pl.length===7 ? pl[5]|(pl[6]<<8) : this.savedRangeDps;
          if (![3, ...(this.configVersion >= 2 ? [5] : []), ...(this.configVersion===3 ? [7] : [])].includes(pl.length) || ![125,250,500,1000,2000,4000].includes(range) || duration < 100 || duration > 60000 || pl[0] > 2 || pl[1] > 1 || pl[2] > 1 || (!(this.capabilities & 1) && pl[0])) return this.ack(id, BAD, 0);
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
    onHostBytes(chunk, source) {
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
        const target = source && typeof source.push === 'function' ? source : port;
        if (reply) setTimeout(() => target.push(reply), 2);
      }
    },
  };
  class FakePort {
    constructor(info) {
      this.readable = null; this.writable = null; this.openCalls = []; this.closeCalls = 0; this.ctrl = null;
      this.info = info || { usbVendorId: 0x2e3c, usbProductId: 0x5740 };
    }
    async open(opts) {
      if (this.readable) throw new DOMException('The port is already open.', 'InvalidStateError');
      this.openCalls.push(opts);
      this.readable = new ReadableStream({ start: (c) => { this.ctrl = c; }, cancel: () => { this.ctrl = null; } });
      this.writable = new WritableStream({ write: (chunk) => dev.onHostBytes(chunk, this) });
    }
    async close() {
      if ((this.readable && this.readable.locked) || (this.writable && this.writable.locked)) throw new TypeError('Cannot close a port with locked streams.');
      this.closeCalls++; this.readable = null; this.writable = null; this.ctrl = null;
    }
    push(bytes) { try { if (this.ctrl) this.ctrl.enqueue(bytes); } catch { /* closed */ } }
    getInfo() { return this.info; }
  }
  const port = new FakePort();
  const listeners = { disconnect: [], connect: [] };
  const serial = {
    requestPort: async () => port, getPorts: async () => [port],
    addEventListener: (type, fn) => { (listeners[type] || (listeners[type] = [])).push(fn); },
    removeEventListener: (type, fn) => {
      const list = listeners[type];
      if (!list) return;
      const index = list.indexOf(fn);
      if (index >= 0) list.splice(index, 1);
    },
  };
  Object.defineProperty(Navigator.prototype, 'serial', { get: () => serial, configurable: true });
  setInterval(() => { if (dev.emitting && port.ctrl) { const parts = [dev.telemetry(), dev.telemetry()]; const b = new Uint8Array(parts[0].length + parts[1].length); b.set(parts[0]); b.set(parts[1], parts[0].length); port.push(b); } }, 10);
  window.__mock = {
    dev, port, frame,
    unplug() {
      const c = port.ctrl; port.ctrl = null;
      try { c && c.error(new DOMException('The device has been lost.', 'NetworkError')); } catch { /* */ }
      for (const fn of listeners.disconnect) fn({ type: 'disconnect', target: port });
    },
    makePort: (info) => new FakePort(info),
    emit: (type, target) => { for (const fn of listeners[type] || []) fn({ type, target }); },
    listenerCount: (type) => (listeners[type] || []).length,
  };
})();
