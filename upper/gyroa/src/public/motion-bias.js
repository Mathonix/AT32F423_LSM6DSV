// AT32 AHRS · VQF 运动零偏状态（只读）：0x30 查询 → 0x09（48 B，由 app.js GYRO-CORE decodePayload 严格按长度解码为 type 'motionBias'）。
// 规格：/workspace/zaru/host-agent-motion-bias.md（固件尚未烧录；只按规格实现，未经硬件验证）。gyro1 / gyro3 共用。
// 成功 = 收到 0x09 且 version===1 && motion_bias_enabled===1 && rest_bias_enabled===1；单独的 ACK 永远不算成功。
// 0x30 回 ACK 0x01（旧固件）→ 静默隐藏本项（不报通信故障）；ACK 0x02 → 查询参数无效。没有任何写入命令。
// tauAcc 随当前滤波档变化，只显示设备回报的当前值，不写成常数。
(function (root) {
  'use strict';
  const CMD = { QUERY: 0x30 };
  const MSG = { MOTION_BIAS: 0x09 };
  const LEN = 48;
  const REPLY_TIMEOUT_MS = 1500;
  const AUTO_MS = 500;          // 可选低速刷新：仅在展开且门控允许时 2 Hz
  const NOTE = '6 轴没有绝对航向：运动零偏不能消除航向漂移。';
  // 参数表：label 短中文，title 带固件字段名
  const PARAMS = [
    { key: 'biasSigmaMotion', label: 'σ运动', unit: '°/s', fmt: (v) => v.toFixed(3) },
    { key: 'biasVerticalForgettingFactor', label: '竖直遗忘', unit: '', fmt: (v) => String(Number(v.toPrecision(3))) },
    { key: 'biasForgettingTime', label: '遗忘时间', unit: 's', fmt: (v) => String(Number(v.toPrecision(4))) },
    { key: 'biasClip', label: '限幅', unit: '°/s', fmt: (v) => v.toFixed(2) },
    { key: 'biasSigmaRest', label: 'σ静止', unit: '°/s', fmt: (v) => v.toFixed(3) },
    { key: 'tauAcc', label: 'tauAcc（当前档）', unit: 's', fmt: (v) => String(Number(v.toPrecision(4))) },
  ];
  const ok = (m) => !!m && m.type === 'motionBias' && m.version === 1 && m.motionBiasEnabled === 1 && m.restBiasEnabled === 1;
  const f4 = (v) => (Number.isFinite(v) ? (v >= 0 ? '+' : '') + v.toFixed(4) : '--');

  // io: { send(cmd, payload, { quiet }) → Promise<bool>, canAuto() → bool, log(text), changed() }
  function createController(io) {
    const s = { supported: null, data: null, ok: false, at: 0, message: '', inFlight: null, auto: false, replies: 0, sent: 0 };
    let timer = null;
    const changed = () => io.changed?.();
    function clearFlight() { clearTimeout(timer); timer = null; s.inFlight = null; }
    async function query({ manual = false, quiet = false } = {}) {
      if (s.supported === false || s.inFlight) return false;
      const req = { manual, at: Date.now() }; s.inFlight = req; s.sent++;
      clearTimeout(timer);
      timer = setTimeout(() => { if (s.inFlight === req) { s.inFlight = null; if (req.manual) { s.message = '未收到运动零偏状态回复'; changed(); } } }, REPLY_TIMEOUT_MS);
      const sent = await io.send(CMD.QUERY, [], { quiet });
      if (!sent && s.inFlight === req) { clearFlight(); if (manual) { s.message = '发送失败'; changed(); } return false; }
      return true;
    }
    // 返回 true 表示该 ACK 属于本模块（0x30）
    function onAck(cmd, status) {
      if (cmd !== CMD.QUERY) return false;
      clearFlight();
      if (status === 1) { s.supported = false; s.data = null; s.ok = false; s.message = ''; } // 旧固件：静默隐藏
      else if (status === 2) s.message = '运动零偏查询参数无效（ACK 0x02）';
      else s.message = `运动零偏查询应答 0x${status.toString(16).padStart(2, '0')}（无数据）`; // ACK 0 也不算成功
      changed(); return true;
    }
    function onMessage(m) {
      clearFlight();
      s.supported = true; s.data = m; s.ok = ok(m); s.at = Date.now(); s.replies++;
      s.message = s.ok ? '' : `固件报告零偏估计未全部开启（运动 ${m.motionBiasEnabled}，静止 ${m.restBiasEnabled}）`;
      changed();
    }
    // 0x09 长度 / 版本 / 内容不符：整帧丢弃，只记一次日志
    let droppedLogged = 0;
    function onDropped(m) { if (droppedLogged++ < 3) io.log?.(`丢弃无法识别的运动零偏帧 0x09（${m.type === 'badLength' ? `len=${m.length}，应为 ${LEN}` : '版本或内容无效'}）`); changed(); }
    // 低速刷新：由宿主 20~100 ms 定时调用；auto=true（区块展开）且门控允许时每 500 ms 静默查询一次
    let lastAuto = 0;
    function tick(now = Date.now()) {
      if (!s.auto || s.supported === false || s.inFlight || !io.canAuto?.()) return false;
      if (now - lastAuto < AUTO_MS) return false;
      lastAuto = now; void query({ quiet: true }); return true;
    }
    function setAuto(on) { s.auto = !!on; }
    function headline() {
      if (!s.data) return '--';
      const d = s.data;
      return s.ok ? `运动零偏已开 · 零偏 ${d.bias.map((v) => v.toFixed(3)).join('/')} °/s` : '零偏估计未全部开启';
    }
    function reset() { clearFlight(); Object.assign(s, { supported: null, data: null, ok: false, at: 0, message: '' }); droppedLogged = 0; changed(); }
    return { state: s, query, onAck, onMessage, onDropped, tick, setAuto, headline, reset };
  }

  // DOM：容器 #biasPanel（建议 <details>）内 #biasSummary、#biasFlags、#biasParams（空 div，自动生成）、#biasVec、#biasNote、#biasRead、#biasMsg
  function bindDom(ctl, { doc = document, connected = () => true } = {}) {
    const $ = (id) => doc.getElementById(id);
    const set = (node, k, v) => { if (node && node[k] !== v) node[k] = v; };
    const box = $('biasParams');
    if (box && !box.children.length) box.innerHTML = PARAMS.map((p) => `<span class="bias-p" title="${p.key}"><i>${p.label}</i><b id="bias_${p.key}">--</b>${p.unit ? `<em>${p.unit}</em>` : ''}</span>`).join('');
    const panel = $('biasPanel');
    if ($('biasRead')) $('biasRead').onclick = () => ctl.query({ manual: true });
    panel?.addEventListener('toggle', () => { ctl.setAuto(panel.open); if (panel.open && connected()) void ctl.query(); });
    function render() {
      const s = ctl.state, d = s.data, on = connected();
      set(panel, 'hidden', s.supported === false);
      set($('biasRead'), 'disabled', !on || !!s.inFlight);
      set($('biasSummary'), 'textContent', ctl.headline());
      for (const p of PARAMS) set($(`bias_${p.key}`), 'textContent', d ? p.fmt(d[p.key]) : '--');
      set($('biasVec'), 'textContent', d ? `零偏 XYZ ${d.bias.map(f4).join(' / ')} °/s · 残差 ${d.residualNorm.toFixed(4)} °/s` : '零偏 XYZ -- · 残差 --');
      const flags = d ? [
        d.motionBiasEnabled === 1 ? '运动零偏已开' : '运动零偏未开', d.restBiasEnabled === 1 ? '静止零偏已开' : '静止零偏未开',
        d.restDetected ? '静止：是' : '静止：否',
        d.zaruEnabled ? (d.zaruHold ? '零角速保持：锁定中' : '零角速保持：未锁定') : '零角速保持：本档 / 九轴未启用',
      ].join(' · ') : '--';
      set($('biasFlags'), 'textContent', flags);
      set($('biasMsg'), 'textContent', s.message);
      if (panel) { const st = s.supported === false ? 'unsupported' : !d ? 'empty' : s.ok ? 'ok' : 'off'; if (panel.dataset.state !== st) panel.dataset.state = st; }
    }
    return { render };
  }

  root.GyroBias = { CMD, MSG, LEN, REPLY_TIMEOUT_MS, AUTO_MS, NOTE, PARAMS, ok, createController, bindDom };
})(typeof globalThis !== 'undefined' ? globalThis : this);
