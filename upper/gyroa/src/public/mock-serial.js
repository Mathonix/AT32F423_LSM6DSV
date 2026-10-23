// mock-serial.js — 仅 ?mock=1 时由 mobile-mock.js 加载；= test/mock-serial.js + test/filter-mock.js（Mock 演示，NOT real hardware）
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
    magCal: {valid:false,offset:[.561956977,-3.455820552,4.206353964],matrix:[[.978002793,-.009591232,.015064551],[-.009591232,1.067653364,.005627912],[.015064551,.005627912,.958047992]]}, magCalN:0,
    magCalFrame(seq) {return frame(0x4a,seq,le(52,v=>{v.setUint8(0,1);v.setUint8(1,this.magCal.valid?1:0);[...this.magCal.offset,...this.magCal.matrix.flat()].forEach((x,i)=>v.setFloat32(4+i*4,x,true));}));},
    magRejectionEnabled:true,magRejectionSaved:true,
    restBiasEnabled:true,restBiasSaved:true,
    prefilterEnabled:false,prefilterSaved:false,prefilterHz:2,prefilterQ:Math.fround(Math.SQRT1_2),prefilterUnsupported:false,
    prefilterFrame(seq){return frame(0x66,seq,le(28,v=>{v.setUint8(0,1);v.setUint8(1,this.prefilterEnabled?1:0);v.setUint8(2,this.prefilterSaved?1:0);v.setFloat32(4,this.prefilterHz,true);v.setFloat32(8,this.prefilterQ,true);v.setFloat32(12,this.prefilterHz,true);v.setFloat32(16,this.prefilterQ,true);}));},
    scaleSaved:[1,1,1],scaleActive:[1,1,1],scaleUnsupported:false,scaleSkips:0,scaleClipped:0,
    scaleFrame(seq){return frame(0x5f,seq,le(36,v=>{v.setUint8(0,1);v.setUint8(1,Number(this.scaleActive.some((x,i)=>x!==this.scaleSaved[i])));[...this.scaleActive,...this.scaleSaved].forEach((x,i)=>v.setFloat32(4+4*i,x,true));v.setUint32(28,this.scaleSkips,true);v.setUint32(32,this.scaleClipped,true);}));},
    eskfSaved:null,eskfCustom:false,eskfUnsupported:false,
    eskfDefaults(){return (globalThis.GyroEskfTuning?.DEFAULTS||[12,1,3,Math.fround(.1091),.4,.4,1,.1,.2,.087,.175,.05,8,800,500,240]).slice();},
    eskfFrames(seq) {
      const defaults=this.eskfDefaults(),saved=this.eskfCustom&&this.eskfSaved?this.eskfSaved:defaults,active=saved;
      const flags=this.eskfCustom?1:0;
      const body=(values,v)=>{values.forEach((x,i)=>i<12?v.setFloat32(4+i*4,x,true):v.setUint16(52+(i-12)*2,x,true));};
      return Uint8Array.from([saved,defaults,active].flatMap((values,page)=>[...frame(0x6a,seq,le(60,v=>{
        [1,page,flags,0].forEach((x,i)=>v.setUint8(i,x));body(values,v);
      }))]));
    },
    tuningDefaults:[2.5,4,.5,.035,.1,2,100,.5,1,.6,.15,.05,.1,10].map(Math.fround),
    tuningSaved:null,tuningActive:null,tuningCustom:false,tuningActiveCustom:false,tuningUnsupported:false,
    tuningFrames(seq) {
      const active=this.tuningActive||this.tuningDefaults,saved=this.tuningSaved||active;
      const flags=Number(this.tuningCustom)|(this.tuningCustom!==this.tuningActiveCustom||active.some((v,i)=>v!==saved[i])?2:0);
      return Uint8Array.from([saved,this.tuningDefaults,active].flatMap((values,page)=>[...frame(0x5b,seq,le(60,v=>{
        [1,page,flags,this.algorithm].forEach((x,i)=>v.setUint8(i,x));values.forEach((x,i)=>v.setFloat32(4+4*i,x,true));
      }))]));
    },
    algorithm:0,savedAlgorithm:0,algorithmUnsupported:false,
    algorithmFrame(seq){return frame(0x52,seq,Uint8Array.of(1,this.algorithm,this.savedAlgorithm,Number(this.algorithm!==this.savedAlgorithm)));},
    wsEnabled: true, wsSaved: true,
    restIndicatorEnabled: true, restIndicatorSaved: true,
    magGateEnabled: true, magGateSaved: true,magGateUnsupported: false,
    wsFrame(seq) {return frame(0x46,seq,Uint8Array.of(1,this.wsEnabled?1:0,this.wsSaved?1:0,0));},
    restIndicatorFrame(seq) {return frame(0x6d,seq,Uint8Array.of(1,this.restIndicatorEnabled?1:0,this.restIndicatorSaved?1:0,0));},
    magGateFrame(seq) {return frame(0x70,seq,Uint8Array.of(1,this.magGateEnabled?1:0,this.magGateSaved?1:0,0));},
    extended: false, source: 1, capabilities: 7, activeMode: 1, savedMode: 1, activeFast: 0, savedFast: 0, failSave: false,
    configVersion: 1, activeInitMs: 2000, savedInitMs: 2000,
    activeRangeDps:1000, savedRangeDps:1000, savedOutputHz:1000, activeAccRangeG:4, savedAccRangeG:4,
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
    config() { return frame(7, this.seq++, le({1:18,2:22,3:28,4:28,5:30}[this.configVersion], (v) => {
      [this.configVersion, this.source, this.activeMode, this.savedMode, this.activeFast, this.savedFast, this.capabilities, 0].forEach((x, i) => v.setUint8(i, x));
      v.setUint16(8, this.outHz, true);
      this.outputs.forEach((o, i) => { const at = 10 + 4 * i; v.setUint8(at, o.format); v.setUint8(at + 1, o.legacyMode); v.setUint16(at + 2, o.mask, true); });
      if (this.configVersion >= 2) { v.setUint16(18, this.activeInitMs, true); v.setUint16(20, this.savedInitMs, true); }
      if (this.configVersion >= 3) { v.setUint16(22,this.activeRangeDps,true); v.setUint16(24,this.savedRangeDps,true); v.setUint16(26,this.savedOutputHz,true); }
      if (this.configVersion >= 5) { v.setUint8(28,this.activeAccRangeG); v.setUint8(29,this.savedAccRangeG); }
    })); },
    reboot() { if(this.configVersion>=5)this.activeAccRangeG=this.savedAccRangeG; this.scaleActive=this.scaleSaved.slice(); if(this.tuningSaved)this.tuningActive=this.tuningSaved.slice(); this.tuningActiveCustom=this.tuningCustom; this.restBiasEnabled=this.restBiasSaved; this.algorithm=this.savedAlgorithm; this.magRejectionEnabled=this.magRejectionSaved; this.wsEnabled=this.wsSaved; this.restIndicatorEnabled=this.restIndicatorSaved; this.magGateEnabled=this.magGateSaved; this.activeMode = this.savedMode; this.activeFast = this.savedFast; this.activeInitMs = this.savedInitMs; if(this.configVersion>=3) {this.activeRangeDps=this.savedRangeDps;this.outHz=this.savedOutputHz;} this.outputs = this.savedOutputs.map((o) => ({ ...o })); this.can = { ...this.savedCan }; this.dirty = 0; this.settings = 0; },
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
      if(id===0x74){
        this.commands.push({id,payload:Array.from(pl)});
        if(this.runtimeUnsupported)return this.seqAck(id,seq,1,0);
        if(pl.length)return this.seqAck(id,seq,2,0);
        const ms=Math.floor(performance.now()),n=Math.floor(ms*8);
        return frame(0x75,seq,le(60,v=>[1,150000000,ms,n,0,0,n,n,Math.floor(n/4),Math.floor(n/16),Math.floor(n/8),Math.floor(n/8),this.runtimeMaxAgeCycles??499686,8,0].forEach((x,i)=>v.setUint32(i*4,x,true))));
      }
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
        case 0x49:
          return this.magCalUnsupported?this.seqAck(id,seq,1,0):pl.length?this.seqAck(id,seq,BAD,0):this.magCalFrame(seq);
        case 0x47: {
          if(this.magCalUnsupported)return this.seqAck(id,seq,1,0);
          if(pl.length)return this.seqAck(id,seq,BAD,0);
          const n=++this.magCalN,z=1-2*((n*137)%720+.5)/720,a=n*Math.PI*(3-Math.sqrt(5)),h=Math.sqrt(1-z*z),u=[h*Math.cos(a),h*Math.sin(a),z];
          const b=u.map((x,i)=>45*x*[1.15,.9,1.05][i]+[8,-6,4][i]),body=[u[1],u[0],-u[2]],w=Math.sqrt((1+body[2])/2),q=w>1e-6?[w,body[1]/(2*w),-body[0]/(2*w),0]:[0,1,0,0];
          return frame(0x48,seq,le(40,v=>{v.setUint8(0,1);v.setUint8(1,this.activeMode && !this.magInvalid?1:0);v.setUint32(4,n,true);v.setUint32(8,Math.floor(performance.now()),true);[...b,...q].forEach((x,i)=>v.setFloat32(12+i*4,x,true));}));
        }
        case 0x4b:
        case 0x4c: {
          if(this.magCalUnsupported)return this.seqAck(id,seq,1,0);
          let status=S;
          if(id===0x4b && (pl.length!==52 || pl[0]!==1 || pl[1]!==1 || pl[2] || pl[3]) || id===0x4c && pl.length)status=BAD;
          else if(this.failSave || this.acc.status===1)status=FAIL;
          else if(id===0x4b){const v=new DataView(pl.buffer,pl.byteOffset,pl.byteLength),x=Array.from({length:12},(_,i)=>v.getFloat32(4+i*4,true)),c={valid:true,offset:x.slice(0,3),matrix:[x.slice(3,6),x.slice(6,9),x.slice(9,12)]};if(!globalThis.GyroMagCal?.valid(c))status=BAD;else this.magCal=c;}
          else this.magCal={valid:false,offset:[.561956977,-3.455820552,4.206353964],matrix:[[.978002793,-.009591232,.015064551],[-.009591232,1.067653364,.005627912],[.015064551,.005627912,.958047992]]};
          return Uint8Array.from([...this.seqAck(id,seq,status,0),...this.magCalFrame(seq)]);
        }
        case 0x4d:
          return pl.length?this.seqAck(id,seq,2):frame(0x4f,seq,Uint8Array.of(1,+this.magRejectionEnabled,+this.magRejectionSaved,0));
        case 0x4e: {
          const status=pl.length!==1||pl[0]>1?2:this.failSave?3:0;
          if(!status)this.magRejectionEnabled=this.magRejectionSaved=!!pl[0];
          return Uint8Array.from([...this.seqAck(id,seq,status),...frame(0x4f,seq,Uint8Array.of(1,+this.magRejectionEnabled,+this.magRejectionSaved,0))]);
        }
        case 0x44:
          return this.wsUnsupported ? this.seqAck(id,seq,1,0) : pl.length ? this.seqAck(id,seq,BAD,0) : this.wsFrame(seq);
        case 0x45: {
          if(this.wsUnsupported)return this.seqAck(id,seq,1,0);
          let status=S;
          if(pl.length!==1 || pl[0]>1)status=BAD;
          else if(this.failSave || this.acc.status===1)status=FAIL;
          else this.wsSaved=this.wsEnabled=!!pl[0];
          return Uint8Array.from([...this.seqAck(id,seq,status,this.wsEnabled?1:0),...this.wsFrame(seq)]);
        }
        case 0x6e:
          return this.magGateUnsupported ? this.seqAck(id,seq,1,0)
            : pl.length ? this.seqAck(id,seq,BAD,0) : this.magGateFrame(seq);
        case 0x6f: {
          if(this.magGateUnsupported)return this.seqAck(id,seq,1,0);
          let status=S;
          if(pl.length!==1 || pl[0]>1)status=BAD;
          else if(this.failSave || this.acc.status===1)status=FAIL;
          else this.magGateSaved=this.magGateEnabled=!!pl[0];
          return Uint8Array.from([...this.seqAck(id,seq,status,this.magGateEnabled?1:0),...this.magGateFrame(seq)]);
        }
        case 0x6b:
          return this.restIndicatorUnsupported ? this.seqAck(id,seq,1,0)
            : pl.length ? this.seqAck(id,seq,BAD,0) : this.restIndicatorFrame(seq);
        case 0x6c: {
          if(this.restIndicatorUnsupported)return this.seqAck(id,seq,1,0);
          let status=S;
          if(pl.length!==1 || pl[0]>1)status=BAD;
          else if(this.failSave || this.acc.status===1)status=FAIL;
          else this.restIndicatorSaved=this.restIndicatorEnabled=!!pl[0];
          return Uint8Array.from([...this.seqAck(id,seq,status,this.restIndicatorEnabled?1:0),...this.restIndicatorFrame(seq)]);
        }
        case 0x42: {
          if (pl.length) return this.seqAck(id, seq, BAD, 0);
          if (this.magUnsupported) return this.seqAck(id, seq, 1, 0);
          const at = Math.floor(performance.now() / 20) * 20;
          return frame(0x43,seq,le(24,v => {
            v.setUint8(0,1); v.setUint8(1,this.activeMode && !this.magInvalid ? (this.magDisturbed ? 3 : 1) : 0); v.setUint8(2,this.activeMode);
            v.setUint32(4,at/20,true); v.setUint32(8,at,true);
            (this.mag || [25+Math.sin(at/900)*3,-12+Math.cos(at/1100)*2,38+Math.sin(at/1300)]).forEach((x,i)=>v.setFloat32(12+i*4,x,true));
          }));
        }
        case 0x11: return this.ack(id, pl.length ? BAD : S, 0);
        case 0x15: {
          if (pl.length || this.resetStatus) return this.seqAck(id, seq, pl.length ? BAD : this.resetStatus, 0);
          this.resets = (this.resets || 0) + 1;
          this.reboot(); this.pose.yaw = 0;
          if (cfg.transport === 'usb') reenumerate(cfg.usbBoot, false);
          return this.dropResetAck ? null : this.seqAck(id, seq, S, 0);
        }
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
        case 0x64: return this.prefilterUnsupported?this.seqAck(id,seq,1,0):pl.length?this.seqAck(id,seq,BAD,0):this.prefilterFrame(seq);
        case 0x65: {
          if(this.prefilterUnsupported)return this.seqAck(id,seq,1,0);
          if(pl.length!==12||pl[0]!==1||pl[1]>1||pl[2]||pl[3])return this.seqAck(id,seq,BAD,0);
          const view=new DataView(pl.buffer,pl.byteOffset,pl.byteLength),hz=view.getFloat32(4,true),q=view.getFloat32(8,true);
          if(!(hz>=.2&&hz<=200&&q>=.2&&q<=5))return this.seqAck(id,seq,BAD,0);
          if(this.failSave||this.acc.status===1||this.insActive)return this.seqAck(id,seq,FAIL,0);
          this.prefilterEnabled=this.prefilterSaved=!!pl[1];this.prefilterHz=hz;this.prefilterQ=q;
          return Uint8Array.from([...this.seqAck(id,seq,S,0),...this.prefilterFrame(seq)]);
        }
        case 0x55: return this.restBiasUnsupported?this.seqAck(id,seq,1,0):pl.length?this.seqAck(id,seq,BAD,0):frame(0x57,seq,Uint8Array.of(1,+this.restBiasEnabled,+this.restBiasSaved,0));
        case 0x56: {
          if(this.restBiasUnsupported)return this.seqAck(id,seq,1,0);
          if(pl.length!==1||pl[0]>1)return this.seqAck(id,seq,BAD,0);
          if(this.failSave||this.acc.status===1||this.insActive)return this.seqAck(id,seq,FAIL,0);
          this.restBiasEnabled=this.restBiasSaved=!!pl[0];
          return Uint8Array.from([...this.seqAck(id,seq,S,0),...frame(0x57,seq,Uint8Array.of(1,+this.restBiasEnabled,+this.restBiasSaved,0))]);
        }
        case 0x5c:
          return this.scaleUnsupported?this.seqAck(id,seq,1,0):pl.length?this.seqAck(id,seq,BAD,0):this.scaleFrame(seq);
        case 0x5d:
        case 0x5e: {
          if(this.scaleUnsupported)return this.seqAck(id,seq,1,0);
          let values=[1,1,1];
          if(id===0x5d){if(pl.length!==16||pl[0]!==1||pl[1]||pl[2]||pl[3])return this.seqAck(id,seq,BAD,0);const v=new DataView(pl.buffer,pl.byteOffset,pl.byteLength);values=[4,8,12].map(o=>v.getFloat32(o,true));if(values.some(x=>!Number.isFinite(x)||x<Math.fround(.95)||x>Math.fround(1.05)))return this.seqAck(id,seq,BAD,0);}else if(pl.length)return this.seqAck(id,seq,BAD,0);
          if(this.failSave||this.acc.status===1||this.insActive)return this.seqAck(id,seq,FAIL,0);
          this.scaleSaved=values;return Uint8Array.from([...this.seqAck(id,seq,S,0),...this.scaleFrame(seq)]);
        }
        case 0x67:
          return this.eskfUnsupported?this.seqAck(id,seq,1,0):pl.length?this.seqAck(id,seq,BAD,0):this.eskfFrames(seq);
        case 0x68:
        case 0x69: {
          if(this.eskfUnsupported)return this.seqAck(id,seq,1,0);
          const api=globalThis.GyroEskfTuning;
          let values=this.eskfDefaults();
          if(id===0x68) {
            if(pl.length!==60||pl[0]!==1||pl[1]||pl[2]||pl[3]||!api)return this.seqAck(id,seq,BAD,0);
            const decoded=api.decode(pl);
            if(decoded.type!=='eskfTuning')return this.seqAck(id,seq,BAD,0);
            values=decoded.values;
          } else if(pl.length)return this.seqAck(id,seq,BAD,0);
          if(this.failSave||this.acc.status===1||this.insActive)return this.seqAck(id,seq,FAIL,0);
          const same=api?api.equal(values,api.DEFAULTS):false;
          this.eskfCustom=id===0x68&&!same;this.eskfSaved=this.eskfCustom?values.slice():null;
          return Uint8Array.from([...this.seqAck(id,seq,S,0),...this.eskfFrames(seq)]);
        }
        case 0x58:
          return this.tuningUnsupported?this.seqAck(id,seq,1,0):pl.length?this.seqAck(id,seq,BAD,0):this.tuningFrames(seq);
        case 0x59:
        case 0x5a: {
          if(this.tuningUnsupported)return this.seqAck(id,seq,1,0);
          let values=this.tuningDefaults.slice();
          if(id===0x59) {
            if(pl.length!==60||pl[0]!==1||pl[1]||pl[2]||pl[3])return this.seqAck(id,seq,BAD,0);
            const v=new DataView(pl.buffer,pl.byteOffset,pl.byteLength);values=values.map((_,i)=>v.getFloat32(4+i*4,true));
            const lo=[.1,.2,.001,.001,.001,.1,1,.01,.1,.01,.01,.01,.01,1].map(Math.fround);
            const hi=[100,100,10,10,10,20,10000,10,60,20,10,10,.5,90];
            if(values.some((x,i)=>!Number.isFinite(x)||x<lo[i]||x>hi[i])||values[3]>values[2]||values[4]>values[2]||values[2]>values[5])return this.seqAck(id,seq,BAD,0);
          } else if(pl.length)return this.seqAck(id,seq,BAD,0);
          if(this.failSave||this.algorithm!==0||this.acc.status===1||this.insActive||this.vqf?.state>=1&&this.vqf?.state<=4)return this.seqAck(id,seq,FAIL,0);
          this.tuningSaved=values;this.tuningCustom=true;
          return Uint8Array.from([...this.seqAck(id,seq,S,0),...this.tuningFrames(seq)]);
        }
        case 0x50: return this.algorithmUnsupported?this.seqAck(id,seq,1,0):pl.length?this.seqAck(id,seq,BAD,0):this.algorithmFrame(seq);
        case 0x51: {
          if(this.algorithmUnsupported)return this.seqAck(id,seq,1,0);
          if(pl.length!==1||pl[0]>2)return this.seqAck(id,seq,BAD,0);
          if(this.failSave||this.acc.status===1||this.insActive)return this.seqAck(id,seq,FAIL,0);
          this.savedAlgorithm=pl[0];return Uint8Array.from([...this.seqAck(id,seq,S,0),...this.algorithmFrame(seq)]);
        }
        case 0x17: this.settings = 1; return this.ack(id, S, 0);
        case 0x18: this.settings = 0; return this.ack(id, S, this.dirty);
        case 0x19: if (!this.settings) return this.ack(id, FAIL, pl[0]); this.dirty = 1; this.savedMode = pl[0]; return this.ack(id, S, pl[0]);
        case 0x1a: if (!this.settings) return this.ack(id, FAIL, this.canId); this.canId = pl[0] | (pl[1] << 8); this.dirty = 1; return this.ack(id, S, this.canId);
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
          const range = pl.length>=7 ? pl[5]|(pl[6]<<8) : this.savedRangeDps;
          const accRange = pl.length===8 ? pl[7] : this.savedAccRangeG;
          if (![2,4,8,16].includes(accRange)) return this.ack(id, BAD, 0);
          if (![3, ...(this.configVersion >= 2 ? [5] : []), ...(this.configVersion>=3 ? [7] : []), ...(this.configVersion>=5 ? [8] : [])].includes(pl.length) || ![125,250,500,1000,2000,4000].includes(range) || duration < (this.configVersion>=4 ? 0 : 100) || duration > 60000 || pl[0] > 2 || pl[1] > 1 || pl[2] > 1 || (!(this.capabilities & 1) && pl[0])) return this.ack(id, BAD, 0);
          if (!this.settings || this.failSave) return this.ack(id, FAIL, 0);
          this.savedMode = pl[0]; this.savedFast = pl[1]; this.savedInitMs = duration; this.savedRangeDps=range; this.savedAccRangeG=accRange; this.dirty = +(this.activeMode !== this.savedMode || this.activeFast !== this.savedFast || this.activeInitMs !== this.savedInitMs || this.activeRangeDps!==this.savedRangeDps || this.activeAccRangeG!==this.savedAccRangeG);
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

// 姿态稳定性模拟（追加到 mock-serial.js 之后注入）：按配套固件 0x26 / 0x27 / 0x28 行为应答；ACK 与 0x0B/0x0C 均回显请求 seq。NOT real hardware.
(() => {
  const m = window.__mock; if (!m || m.filter) return;
  const dev = m.dev, frame = m.frame;
  const le = (n, fn) => { const b = new Uint8Array(n); fn(new DataView(b.buffer)); return b; };
  const TAU = [[2, 0.15], [4, 0.5], [6, 2.5], [4, 0.5]]; // tau_mag / rest_tau：[2,4,6,4] / [.15,.5,2.5,.5]
  const f = {
    supported: true,  // false → 旧固件：0x26/0x27/0x28 回 ACK 0x01
    active: 0, saved: 0, nineAxis: false, magReady: false,
    magInterference: false, magDegraded: false, magCalSuspect: false,
    /* 0x71/0x72/0x73：标定基准与当前场强 */
    magExpected: 41.5, magCurrent: 41.5, magHealthUnsupported: false,
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
    const flags = ((f.nineAxis && f.magReady) ? 1 : 0) | (f.magInterference ? 2 : 0) | (f.magDegraded ? 4 : 0) | (f.magCalSuspect ? 8 : 0);
    [1, f.active, 1, flags].forEach((x, i) => v.setUint8(i, x));
    v.setUint32(4, 123456, true);
    [0.01, -0.02, 0.03, 0.001, -0.002, 0.003, 0.05, 0.04, 0.03, 1.5, -2.5, 30].forEach((x, i) => v.setFloat32(8 + 4 * i, x, true));
    v.setFloat32(56, 0.004, true);
  }));
  /* 0x71/0x72/0x73：标定基准。和拒磁诊断一起归在 filter 模块，因为它们共享
   * 同一张卡片、同一份状态。 */
  const magHealth = (seq) => frame(0x73, seq, le(16, (v) => {
    v.setUint8(0, 1);
    v.setUint8(1, (f.magExpected > 0 ? 1 : 0) | (f.magCalSuspect ? 2 : 0) | (f.magCurrent < 20 ? 4 : 0));
    v.setFloat32(2, f.magExpected, true);
    v.setFloat32(6, f.magCurrent, true);
  }));
  const cat = (...parts) => { const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let o = 0; for (const p of parts) { out.set(p, o); o += p.length; } return out; };
  const orig = dev.handle.bind(dev);
  dev.handle = function (id, seq, pl) {
    if (id !== 0x26 && id !== 0x27 && id !== 0x28 && id !== 0x71 && id !== 0x72) return orig(id, seq, pl);
    this.commands.push({ id, payload: Array.from(pl) }); f.log.push({ id, seq, payload: Array.from(pl), settings: this.settings });
    if (id === 0x71) {
      if (f.magHealthUnsupported) return ack(id, seq, 1, 0);
      return pl.length ? ack(id, seq, 2, 0) : magHealth(seq);
    }
    if (id === 0x72) {
      if (f.magHealthUnsupported) return ack(id, seq, 1, 0);
      let status = 0;
      if (pl.length !== 5 || pl[0] > 2) status = 2;
      else if (pl[0] === 2) {
        if (!(f.magCurrent >= 20 && f.magCurrent <= 80)) status = 2; else f.magExpected = f.magCurrent;
      } else if (pl[0] === 0) f.magExpected = 0;
      else {
        const v = new DataView(pl.buffer, pl.byteOffset, pl.length), ut = v.getFloat32(1, true);
        if (!(ut >= 20 && ut <= 80)) status = 2; else f.magExpected = ut;
      }
      if (status === 0) f.magCalSuspect = false;
      return cat(ack(id, seq, status, 0), magHealth(seq));
    }
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

// timestamped capture mock — real firmware never uses this simulation.
(() => {
  const m=window.__mock,d=m.dev,rates=[1,5,10,20,50,100,200,500];
  const crc=b=>{let c=65535;for(const x of b){c^=x<<8;for(let k=0;k<8;k++)c=c&32768?((c<<1)^0x1021)&65535:(c<<1)&65535;}return c;};
  const frame=(id,seq,p)=>{const f=new Uint8Array(p.length+7);f.set([170,85,id,p.length,seq]);f.set(p,5);const c=crc(f.slice(2,-2));f[f.length-2]=c&255;f[f.length-1]=c>>8;return f;};
  const le=(n,fn)=>{const p=new Uint8Array(n);fn(new DataView(p.buffer));return p;};
  const ack=(seq,status)=>frame(0x90,seq,Uint8Array.of(0x60,status,0,0));
  const cat=(a,b)=>{const p=new Uint8Array(a.length+b.length);p.set(a);p.set(b,a.length);return p;};
  d.capture={token:0,hz:0,seq:0,ts:0,deadline:0,next:0,values:null};
  d.captureUnsupported=false;
  const config=seq=>frame(0x63,seq,le(12,v=>{v.setUint8(0,1);v.setUint8(1,d.capture.token?1:0);v.setUint32(4,d.capture.token,true);v.setUint16(8,d.capture.hz,true);v.setUint16(10,500,true);}));
  const original=d.handle.bind(d);
  d.handle=function(id,seq,pl){
    if(id===0x23&&!this.captureUnsupported)return frame(0x32,seq,le(16,v=>{v.setUint8(0,1);v.setUint8(1,9);[...new TextEncoder().encode('20261012b')].forEach((b,i)=>v.setUint8(i+2,b));}));
    if(id===0x35&&!this.captureUnsupported)return frame(0x36,seq,Uint8Array.of(65,84,51,50));
    if(id!==0x60&&id!==0x62)return original(id,seq,pl);
    this.commands.push({id,seq,payload:[...pl]});
    if(this.captureUnsupported)return frame(0x90,seq,Uint8Array.of(id,1,0,0));
    if(id===0x62)return config(seq);
    if(pl.length!==7||pl[0]>1)return ack(seq,2);
    const v=new DataView(pl.buffer,pl.byteOffset,pl.length),hz=v.getUint16(1,true),token=v.getUint32(3,true),now=performance.now();
    if(!token||(pl[0]&&!rates.includes(hz)))return ack(seq,2);
    if(pl[0]&&(this.settings||this.capture.token&&(this.capture.token!==token||this.capture.hz!==hz)))return cat(ack(seq,3),config(seq));
    if(pl[0]){if(!this.capture.token)Object.assign(this.capture,{token,hz,seq:0,ts:0,next:now});this.capture.deadline=now+10000;}
    else if(this.capture.token===token)Object.assign(this.capture,{token:0,hz:0});
    return cat(ack(seq,0),config(seq));
  };
  const reboot=d.reboot.bind(d);d.reboot=function(){Object.assign(this.capture,{token:0,hz:0});reboot();};
  setInterval(()=>{
    const c=d.capture,now=performance.now();if(!c.token||d.boot||!m.port.ctrl||m.port.dead)return;
    if(now>=c.deadline){c.token=c.hz=0;return;}
    let n=0;while(now>=c.next&&n++<500){const p=d.pose,values=c.values||[p.ax/9.80665,p.ay/9.80665,p.az/9.80665,p.gx,p.gy,p.gz,p.yaw,p.pitch,p.roll];
      const seq=c.seq++,packet=frame(0x61,seq&255,le(56,v=>{v.setUint8(0,1);v.setUint32(4,c.token,true);v.setUint32(8,seq,true);v.setUint32(12,c.ts>>>0,true);v.setUint32(16,1e6/c.hz,true);values.forEach((x,i)=>v.setFloat32(20+i*4,x,true));}));
      c.ts+=1e6/c.hz;c.next+=1000/c.hz;m.port.push(packet);
    }
  },10);
})();
