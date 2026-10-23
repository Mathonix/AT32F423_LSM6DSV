(() => {
  'use strict';
  /* 静置指示器：呼吸速度反映动静（静置 3 s 一次，运动 0.75 s 一次）。
   * 与 LED 总开关分开存储、分开启用，旧固件回 ACK 0x01 时提示升级。 */
  const QUERY = 0x6b, SET = 0x6c, MSG = 0x6d;
  function decode(p) {
    if (p.length !== 4) return { type: 'badLength', id: MSG, length: p.length, expected: 4 };
    if (p[0] !== 1 || p[1] > 1 || p[2] > 1 || p[3]) return { type: 'unknown', id: MSG };
    return { type: 'restIndicator', enabled: !!p[1], saved: !!p[2] };
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
        if (state.pending === req) finish(kind === 'set' ? '未确认保存结果，请点击读取核对' : '未读到静置指示状态，请点击读取重试');
      }, 3000);
      Promise.resolve(io.send(cmd, kind === 'set' ? [value ? 1 : 0] : [])).then(
        (ok) => { if (!ok && state.pending === req) finish('静置指示指令发送失败'); },
        () => { if (state.pending === req) finish('静置指示指令发送失败'); },
      );
      return true;
    }
    function onAck(cmd, status, seq) {
      const p = state.pending;
      if (!p || p.seq !== seq || cmd !== (p.kind === 'set' ? SET : QUERY)) return;
      if (status === 1) { state.supported = false; finish('当前固件不支持 静置指示，请升级 APP'); return; }
      if (status !== 0) { finish('静置指示 保存或读取失败，设备状态未确认改变'); return; }
      if (p.kind === 'set') p.acked = true;
    }
    function onState(data, seq) {
      const p = state.pending;
      if (!p || p.seq !== seq) return;
      if (p.kind === 'set' && !p.acked) return;
      state.supported = true; state.data = data;
      if (p.kind === 'set' && (data.enabled !== p.value || data.saved !== p.value)) { finish('静置指示 回读与设置不一致，保存未确认'); return; }
      finish(`${p.kind === 'set' ? '已保存，' : ''}静置指示 已${data.enabled ? '开启' : '关闭'}`);
    }
    return { state, reset, query: () => request('query'), apply: (value) => typeof value === 'boolean' && request('set', value), onAck, onState };
  }
  function createView(controller, context, doc = document) {
    const toggle = doc.getElementById('restIndicatorEnable'), read = doc.getElementById('restIndicatorRead'), status = doc.getElementById('restIndicatorMessage');
    if (!toggle || !read || !status) return { render() {} };
    function render() {
      const s = controller.state, c = context();
      toggle.checked = s.pending?.kind === 'set' ? s.pending.value : s.data?.enabled ?? false;
      toggle.disabled = !c.connected || c.busy || !!s.pending || s.supported !== true;
      read.disabled = !c.connected || c.busy || !!s.pending;
      status.textContent = !c.connected ? '请先连接设备' : s.message;
    }
    toggle.onchange = () => { controller.apply(toggle.checked); render(); };
    read.onclick = () => controller.query();
    return { render };
  }
  globalThis.GyroRestIndicator = { QUERY, SET, MSG, decode, createController, createView };
})();
