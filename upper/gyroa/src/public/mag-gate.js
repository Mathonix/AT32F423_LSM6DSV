(() => {
  'use strict';
  /* 拒磁策略开关：1 = VQF 式（拒绝期间完全不融合，磁场恢复后继续）；
   * 0 = 旧版（拒绝满 500 次后按 4 倍噪声降级接受）。设置扇区独立保存。 */
  const QUERY = 0x6e, SET = 0x6f, MSG = 0x70;
  function decode(p) {
    if (p.length !== 4) return { type: 'badLength', id: MSG, length: p.length, expected: 4 };
    if (p[0] !== 1 || p[1] > 1 || p[2] > 1 || p[3]) return { type: 'unknown', id: MSG };
    return { type: 'magGate', enabled: !!p[1], saved: !!p[2] };
  }
  function createController(io) {
    const state = { supported: null, data: null, pending: null, message: '请先连接设备' };
    let timer = null;
    const changed = () => io.changed?.();
    function reset() { clearTimeout(timer); timer = null; state.supported = null; state.data = null; state.pending = null; state.message = '请先连接设备'; changed(); }
    function finish(message) { clearTimeout(timer); timer = null; state.pending = null; state.message = message; changed(); }
    function request(kind, value) {
      if (state.pending || !io.connected() || (kind === 'set' && (state.supported !== true || io.busy()))) return false;
      const cmd = kind === 'set' ? SET : QUERY;
      const req = state.pending = { kind, seq: io.nextSeq(), value, acked: false };
      state.message = kind === 'set' ? '正在保存…' : '正在读取…'; changed();
      timer = setTimeout(() => {
        if (state.pending === req) finish(kind === 'set' ? '未确认保存结果，请点击读取核对' : '未读到拒磁策略，请点击读取重试');
      }, 3000);
      Promise.resolve(io.send(cmd, kind === 'set' ? [value ? 1 : 0] : [])).then(
        (ok) => { if (!ok && state.pending === req) finish('拒磁策略指令发送失败'); },
        () => { if (state.pending === req) finish('拒磁策略指令发送失败'); },
      );
      return true;
    }
    function onAck(cmd, status, seq) {
      const p = state.pending;
      if (!p || p.seq !== seq || cmd !== (p.kind === 'set' ? SET : QUERY)) return;
      if (status === 1) { state.supported = false; finish('当前固件不支持 拒磁策略开关，请升级 APP'); return; }
      if (status !== 0) { finish('拒磁策略 保存或读取失败，设备状态未确认改变'); return; }
      if (p.kind === 'set') p.acked = true;
    }
    function onState(data, seq) {
      const p = state.pending;
      if (!p || p.seq !== seq) return;
      if (p.kind === 'set' && !p.acked) return;
      state.supported = true; state.data = data;
      if (p.kind === 'set' && (data.enabled !== p.value || data.saved !== p.value)) { finish('拒磁策略 回读与设置不一致，保存未确认'); return; }
      finish(`${p.kind === 'set' ? '已保存，' : ''}拒磁策略 已切换为${data.enabled ? 'VQF 式（等待恢复）' : '旧版（降级接受）'}`);
    }
    return { state, reset, query: () => request('query'), apply: (value) => typeof value === 'boolean' && request('set', value), onAck, onState };
  }
  function createView(controller, context, doc = document) {
    const toggle = doc.getElementById('magGateEnable'), read = doc.getElementById('magGateRead'), status = doc.getElementById('magGateMessage');
    if (!toggle || !read || !status) return { render() {} };
    function render() {
      const s = controller.state, c = context();
      toggle.checked = s.pending?.kind === 'set' ? s.pending.value : s.data?.enabled ?? true;
      toggle.disabled = !c.connected || c.busy || !!s.pending || s.supported !== true;
      read.disabled = !c.connected || c.busy || !!s.pending;
      status.textContent = !c.connected ? '请先连接设备' : s.message;
    }
    toggle.onchange = () => { controller.apply(toggle.checked); render(); };
    read.onclick = () => controller.query();
    return { render };
  }
  globalThis.GyroMagGate = { QUERY, SET, MSG, decode, createController, createView };
})();
