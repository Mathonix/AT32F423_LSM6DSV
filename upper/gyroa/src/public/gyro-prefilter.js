(() => {
  'use strict';
  const QUERY = 0x64, SET = 0x65, MSG = 0x66;
  const Q_DEFAULT = Math.fround(Math.SQRT1_2);
  const okHz = (hz) => Number.isFinite(hz) && hz >= 0.2 && hz <= 200;
  const okQ = (q) => Number.isFinite(q) && q >= 0.2 && q <= 5;
  function decode(p) {
    if (p.length !== 28) return {type:'badLength', id:MSG, length:p.length, expected:28};
    const view = new DataView(p.buffer, p.byteOffset, p.byteLength);
    const cutoffHz = view.getFloat32(4, true), q = view.getFloat32(8, true);
    const savedCutoffHz = view.getFloat32(12, true), savedQ = view.getFloat32(16, true);
    if (p[0] !== 1 || p[1] > 1 || p[2] > 1 || p[3] || !okHz(cutoffHz) || !okQ(q) || !okHz(savedCutoffHz) || !okQ(savedQ)) return {type:'unknown', id:MSG};
    return {type:'gyroPrefilter', enabled:!!p[1], saved:!!p[2], cutoffHz, q, savedCutoffHz, savedQ};
  }
  function encode(value) {
    const bytes = new Uint8Array(12);
    const view = new DataView(bytes.buffer);
    view.setUint8(0, 1);
    view.setUint8(1, value.enabled ? 1 : 0);
    view.setFloat32(4, value.cutoffHz, true);
    view.setFloat32(8, value.q, true);
    return [...bytes];
  }
  function same(data, value) {
    return !!data && data.enabled === value.enabled && data.saved === value.enabled &&
      data.cutoffHz === Math.fround(value.cutoffHz) && data.savedCutoffHz === Math.fround(value.cutoffHz) &&
      data.q === Math.fround(value.q) && data.savedQ === Math.fround(value.q);
  }
  function createController(io) {
    const state = {supported:null, data:null, pending:null, message:'请先连接设备'};
    let timer = null;
    const changed = () => io.changed?.();
    function reset() { clearTimeout(timer); timer = null; state.supported = null; state.data = null; state.pending = null; state.message = '请先连接设备'; changed(); }
    function finish(message) { clearTimeout(timer); timer = null; state.pending = null; state.message = message; changed(); }
    function request(kind, value) {
      if (state.pending || !io.connected() || (kind === 'set' && (state.supported !== true || io.busy()))) return false;
      if (kind === 'set' && (!okHz(value?.cutoffHz) || !okQ(value?.q) || typeof value.enabled !== 'boolean')) return false;
      const req = state.pending = {kind, seq:io.nextSeq(), value, acked:false};
      state.message = kind === 'set' ? '正在保存…' : '正在读取…';
      changed();
      timer = setTimeout(() => { if (state.pending === req) finish(kind === 'set' ? '未确认保存结果，请点击读取核对' : '未读到前置低通状态，请点击读取重试'); }, 3000);
      Promise.resolve(io.send(kind === 'set' ? SET : QUERY, kind === 'set' ? encode(value) : [])).then(ok => { if (!ok && state.pending === req) finish('前置低通指令发送失败'); }, () => { if (state.pending === req) finish('前置低通指令发送失败'); });
      return true;
    }
    function onAck(cmd, status, seq) {
      const p = state.pending;
      if (!p || p.seq !== seq || cmd !== (p.kind === 'set' ? SET : QUERY)) return;
      if (status === 1) { state.supported = false; finish('当前固件不支持陀螺前置低通，请升级 APP'); return; }
      if (status !== 0) { finish('前置低通保存或读取失败，设备状态未确认改变'); return; }
      if (p.kind === 'set') p.acked = true;
    }
    function onState(data, seq) {
      const p = state.pending;
      if (!p || p.seq !== seq) return;
      if (p.kind === 'set' && !p.acked) return;
      state.supported = true;
      state.data = data;
      if (p.kind === 'set' && !same(data, p.value)) { finish('前置低通回读与设置不一致，保存未确认'); return; }
      finish(`${p.kind === 'set' ? '已保存，' : ''}前置低通已${data.enabled ? '开启' : '关闭'}，截止 ${data.cutoffHz} Hz，Q ${data.q}`);
    }
    return {state, reset, query:() => request('query'), apply:value => request('set', value), onAck, onState, Q_DEFAULT};
  }
  function createView(controller, context, doc = document) {
    const toggle = doc.getElementById('gyroPrefilterEnable');
    const hz = doc.getElementById('gyroPrefilterHz');
    const q = doc.getElementById('gyroPrefilterQ');
    const apply = doc.getElementById('gyroPrefilterApply');
    const read = doc.getElementById('gyroPrefilterRead');
    const status = doc.getElementById('gyroPrefilterMessage');
    function shown() {
      const s = controller.state;
      return s.pending?.kind === 'set' ? s.pending.value : s.data;
    }
    function render() {
      const s = controller.state, c = context(), value = shown();
      const locked = !c.connected || c.busy || !!s.pending || s.supported !== true;
      toggle.checked = value ? value.enabled : false;
      toggle.disabled = locked;
      if (doc.activeElement !== hz) hz.value = value ? String(value.cutoffHz) : '2';
      if (doc.activeElement !== q) q.value = value ? String(value.q) : String(Q_DEFAULT);
      hz.disabled = q.disabled = apply.disabled = locked;
      read.disabled = !c.connected || c.busy || !!s.pending;
      status.textContent = !c.connected ? '请先连接设备' : s.message;
    }
    function current() {
      return {enabled:toggle.checked, cutoffHz:Number(hz.value), q:Number(q.value)};
    }
    apply.onclick = () => {
      const value = current();
      if (!okHz(value.cutoffHz) || !okQ(value.q)) { controller.state.message = '截止频率 0.2～200 Hz，Q 0.2～5'; render(); return; }
      controller.apply(value);
      render();
    };
    toggle.onchange = () => { controller.apply(current()); render(); };
    read.onclick = () => controller.query();
    return {render};
  }
  globalThis.GyroPrefilter = {QUERY, SET, MSG, Q_DEFAULT, decode, encode, createController, createView};
})();
