// Read-only magnetometer queries, independent of the configured telemetry output.
(() => {
  'use strict';
  function decode(payload) {
    if (payload.length !== 24) return { type: 'badLength', id: 0x43, length: payload.length, expected: 24 };
    const v = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    const b = [12, 16, 20].map(i => v.getFloat32(i, true));
    if (payload[0] !== 1 || payload[1] > 3 || payload[2] > 2 || payload[3] || !b.every(Number.isFinite)) return { type: 'unknown', id: 0x43 };
    return { type: 'magnetometer', flags: payload[1], mode: payload[2], sample: v.getUint32(4, true), ms: v.getUint32(8, true), b };
  }
  function createPoller(io, clock = () => performance.now()) {
    let epoch = null, pending = null, lastQuery = -Infinity, latest = null, supported = null, message = '';
    const enabled = c => c.connected && [1, 2].includes(c.mode);
    function tick() {
      const c = io.context(), now = clock(), key = `${c.epoch}:${c.mode}:${c.connected}`;
      if (key !== epoch) { epoch = key; pending = latest = null; supported = null; lastQuery = -Infinity; message = ''; }
      if (!enabled(c) || !c.visible || c.busy || supported === false) return;
      if (pending && now - pending.at < 1000) return;
      if (pending) { pending = null; message = '磁力计数据无回复'; }
      if (now - lastQuery < 50) return;
      const request = pending = { seq: io.nextSeq(), at: now };
      lastQuery = now;
      Promise.resolve(io.send()).then(ok => { if (!ok && pending === request) { pending = null; message = '磁力计查询发送失败'; } });
    }
    function onFrame(frame, seq) {
      const c = io.context();
      if (!pending || pending.seq !== seq || !enabled(c) || frame.mode !== c.mode) return;
      pending = null; supported = true;
      if (!(frame.flags & 1)) { latest = null; message = '等待有效磁力计采样'; return; }
      message = frame.flags & 2 ? '检测到磁场扰动' : '';
      if (!latest || latest.sample !== frame.sample) latest = { ...frame, at: clock() };
    }
    function onAck(status, seq) {
      if (!pending || pending.seq !== seq) return;
      pending = null;
      if (status === 1) { supported = false; latest = null; message = '当前固件不支持磁力计曲线，请升级 APP'; }
      else message = '磁力计读取失败';
    }
    function state() {
      const c = io.context();
      const active = enabled(c), fresh = active && latest && clock() - latest.at < 600;
      return { enabled: active, b: fresh ? latest.b : null, sample: fresh ? latest.sample : null, at: fresh ? latest.at : 0,
        message: !active ? '' : message || (fresh ? '最近 10 秒 · 已校准机体坐标' : '等待磁力计数据') };
    }
    return { tick, onFrame, onAck, state };
  }
  globalThis.GyroMag = { decode, createPoller };
})();
