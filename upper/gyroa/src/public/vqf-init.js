// AT32 AHRS · 静置初始化 VQF 参数（vqfinit1）：协议 + 状态机 + DOM 绑定。规格 /workspace/zaru/host-agent-vqf-static-init.md。
// 帧仍为 AA 55 | id | len | seq | payload | CRC16-CCITT(LE)，由 app.js 负责收发；本文件只定义：
//   0x29 查询状态（空）→ 0x0D 28 B（不回 ACK）       0x2A 开始（空，需设置模式）→ ACK + 0x0D
//   0x2B 取消（空，不需设置模式）→ ACK + 0x0D       0x2C 查询参数（空）→ 0x0E 52 B（不回 ACK）
//   0x2D 恢复默认（空，需设置模式）→ ACK + 0x0E（采集中 ACK 0x03/0x0703 时不回 0x0E）
// 采集中设备约每 200 ms 主动推 0x0D（seq 0），成功结束再推 0x0E（seq 0）；结束以 0x0D 的 state 为准，不只看 ACK。
// 默认值只来自 0x0E 的 default_*：本文件不保存任何默认数字。旧固件 ACK 0x01 或 3 s 内无数据帧 → 整个功能隐藏。
(function (root) {
  'use strict';
  const CMD = { QUERY_STATUS: 0x29, START: 0x2a, CANCEL: 0x2b, QUERY_SETTINGS: 0x2c, RESTORE: 0x2d };
  const MSG = { STATUS: 0x0d, SETTINGS: 0x0e };
  const LEN_STATUS = 28, LEN_SETTINGS = 52, VERSION = 1;
  const ST = { OK: 0, UNSUPPORTED: 1, BAD: 2, FAIL: 3 };
  const DETAIL = { BUSY: 0x0701, NOT_READY: 0x0702, ACTIVE: 0x0703 };
  const PROBE_MS = 3000, ACK_TIMEOUT_MS = 3000, RESTORE_CONFIRM_MS = 4000, STALE_MS = 1000;
  const STATE = { IDLE: 0, WAIT: 1, PRE: 2, COLLECT: 3, CHECK: 4, DONE: 5, FAILED: 6 };
  const STATE_TEXT = ['空闲', '等待放稳', '预稳定计时中', '正在采集', '正在检查', '成功', '失败'];
  const ERROR_TEXT = {
    1: '采集前一直没放稳，或采集中移动了', 2: '陀螺噪声过大', 3: '加速度噪声过大', 4: '零偏超出 ±2 °/s',
    5: '60 秒内温度变化达到或超过 2 °C', 6: '零偏在 60 秒内仍明显漂移', 7: '有效样本不够', 8: '出现非法数值',
    9: '校验通过，但 Flash 写入失败。旧记录保留，运行参数不变',
  };
  const SOURCE_TEXT = ['默认参数', '静置初始化'];
  const UNSUPPORTED_TEXT = '当前固件不支持静置初始化 VQF（旧固件）';
  const RESTORE_CONFIRM_TEXT = '再次点击「确认恢复」：恢复后，这次静置结果会清除（两个 σ 与两个静止门限回到默认值）';
  const ACTIVE_TEXT = '静置初始化进行中，设备未执行（ACK 0x03 / 0x0703）';
  // 参数行：key → 0x0E 偏移（当前值 / default_*）
  const PARAMS = [
    { key: 'sigmaInit', label: '初始 σ', unit: '°/s', cur: 16, def: 32, digits: 3, title: 'biasSigmaInit' },
    { key: 'sigmaRest', label: '静止 σ', unit: '°/s', cur: 20, def: 36, digits: 3, title: 'biasSigmaRest' },
    { key: 'restGyr', label: '静止门限 · 陀螺', unit: '°/s', cur: 24, def: 40, digits: 2, title: 'restThGyr' },
    { key: 'restAcc', label: '静止门限 · 加速度', unit: 'm/s²', cur: 28, def: 44, digits: 2, title: 'restThAcc' },
  ];
  const fr = Math.fround;
  const active = (st) => !!st && st.state >= STATE.WAIT && st.state <= STATE.CHECK;

  function decode(id, payload) {
    if (id !== MSG.STATUS && id !== MSG.SETTINGS) return { type: 'unknown', id, length: payload.length };
    const len = id === MSG.STATUS ? LEN_STATUS : LEN_SETTINGS;
    if (payload.length !== len) return { type: 'badLength', id, length: payload.length, expected: len };
    const v = new DataView(payload.buffer, payload.byteOffset, payload.length), f = (o) => v.getFloat32(o, true);
    if (payload[0] !== VERSION) return { type: 'unknown', id, length: payload.length, reason: `version ${payload[0]}` };
    if (id === MSG.STATUS) {
      const [state, error, source] = [payload[1], payload[2], payload[3]];
      const fl = [16, 20, 24].map(f);
      if (state > 6 || error > 9 || source > 1 || !fl.every(Number.isFinite)) return { type: 'unknown', id, length: payload.length, reason: '内容无效' };
      return { type: 'vqfStatus', version: 1, state, error, source, elapsedMs: v.getUint32(4, true), remainingMs: v.getUint32(8, true), samples: v.getUint32(12, true),
        gyroRateDps: fl[0], accDevMs2: fl[1], tempC: fl[2] };
    }
    const [source, calValid] = [payload[1], payload[2]];
    const fl = [4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48].map(f);
    if (source > 1 || calValid > 1 || !fl.every(Number.isFinite)) return { type: 'unknown', id, length: payload.length, reason: '内容无效' };
    const pick = (k) => Object.fromEntries(PARAMS.map((p) => [p.key, f(p[k])]));
    return { type: 'vqfSettings', version: 1, source, calValid, reserved: payload[3], bias: fl.slice(0, 3), current: pick('cur'), defaults: pick('def'), calTempC: fl[11] };
  }
  const sameParams = (a, b) => !!a && !!b && PARAMS.every((p) => fr(a[p.key]) === fr(b[p.key]));
  function startFailText(status, detail) {
    if (status === ST.BAD) return '开始失败：载荷无效（ACK 0x02）';
    if (status === ST.FAIL && detail === DETAIL.BUSY) return '开始失败：已经在采集，或六面校准还在进行（0x0701）';
    if (status === ST.FAIL && detail === DETAIL.NOT_READY) return '开始失败：主循环还没开始（上电采集零偏期间），请稍后再试（0x0702）';
    if (status === ST.FAIL && detail === 0) return '开始失败：设备未进入设置模式（ACK 0x03）';
    return `开始失败（ACK 0x${status.toString(16).padStart(2, '0')}，detail 0x${detail.toString(16).padStart(4, '0')}）`;
  }
  function detailText(cmd, status, detail) {
    if (status !== ST.FAIL) return '';
    if (detail === DETAIL.ACTIVE) return '静置初始化进行中，设备不执行该命令';
    if (cmd === CMD.START && detail === DETAIL.BUSY) return '已经在采集，或六面校准还在进行';
    if (cmd === CMD.START && detail === DETAIL.NOT_READY) return '主循环还没开始（上电采集零偏期间）';
    return '';
  }

  // io: { send(cmd, payload, { quiet }) → Promise<bool>, editable() → bool, say(text), log(text), changed() }
  function createController(io) {
    const s = { supported: null, status: null, settings: null, statusAt: 0, settingsAt: 0, pending: null, message: '', result: null,
      restoreArmed: false, collectTotal: 0, last: null, dropped: 0, quietSent: 0 };
    let probe = null, ackTimer = null, armTimer = null, polling = false;
    const changed = () => io.changed?.();
    const note = (text, ok = null, kind = null) => { s.message = text; if (ok !== null) s.result = { ok, text, kind: kind || (ok ? 'ok' : 'fail') }; changed(); };
    function finish(ok, text, kind) { clearTimeout(ackTimer); ackTimer = null; s.pending = null; note(text, ok, kind); if (text) io.say?.(text); }
    function markUnsupported() {
      clearTimeout(probe); probe = null; s.supported = false; s.restoreArmed = false;
      if (s.pending) { clearTimeout(ackTimer); ackTimer = null; s.pending = null; }
      s.message = ''; s.result = null; io.log?.(UNSUPPORTED_TEXT); changed();
    }
    function arm(kind) {
      clearTimeout(ackTimer);
      ackTimer = setTimeout(() => {
        const p = s.pending; if (!p) return;
        const what = { start: '开始', cancel: '取消', restore: '恢复默认' }[p.kind];
        finish(false, `未确认：未收到${what}的应答 / 回读，请点「读取」核对设备状态`, 'unconfirmed');
      }, ACK_TIMEOUT_MS);
    }
    async function query({ quiet = false } = {}) {
      if (s.supported === false) return false;
      if (s.supported !== true && !probe) probe = setTimeout(() => { probe = null; if (s.supported !== true) markUnsupported(); }, PROBE_MS);
      const a = await io.send(CMD.QUERY_SETTINGS, [], { quiet });
      const b = await io.send(CMD.QUERY_STATUS, [], { quiet });
      return !!(a && b);
    }
    async function command(kind, cmd, needEdit) {
      if (s.pending || s.supported !== true) return false;
      if (needEdit && io.editable && !io.editable()) { note('请先进入设置模式', false); return false; }
      s.pending = { kind, acked: false, flashFail: false }; s.result = null;
      note({ start: '正在开始静置初始化…', cancel: '正在取消…', restore: '正在恢复默认 VQF 参数…' }[kind]);
      arm(kind);
      if (!await io.send(cmd, [])) { clearTimeout(ackTimer); ackTimer = null; s.pending = null; note('发送失败', false); return false; }
      return true;
    }
    const start = () => (active(s.status) ? Promise.resolve(false) : command('start', CMD.START, true));
    const cancel = () => command('cancel', CMD.CANCEL, false);
    const canRestore = () => s.supported === true && s.settings?.source === 1 && !active(s.status) && !s.pending;
    function disarmRestore(render = true) { clearTimeout(armTimer); armTimer = null; if (!s.restoreArmed) return; s.restoreArmed = false; if (s.message === RESTORE_CONFIRM_TEXT) s.message = ''; if (render) changed(); }
    function armRestore() {
      if (!canRestore()) return false;
      s.restoreArmed = true; s.result = null; note(RESTORE_CONFIRM_TEXT);
      clearTimeout(armTimer); armTimer = setTimeout(() => disarmRestore(), RESTORE_CONFIRM_MS);
      return true;
    }
    async function restore() { disarmRestore(false); if (!canRestore()) { changed(); return false; } return command('restore', CMD.RESTORE, true); }

    function onAck(cmd, status, detail = 0) {
      if (cmd < CMD.QUERY_STATUS || cmd > CMD.RESTORE) return false;
      if (status === ST.UNSUPPORTED) { markUnsupported(); return true; }
      if (cmd === CMD.QUERY_STATUS || cmd === CMD.QUERY_SETTINGS) { note(`静置初始化查询失败（ACK 0x${status.toString(16).padStart(2, '0')}）`); return true; }
      const p = s.pending, kind = { [CMD.START]: 'start', [CMD.CANCEL]: 'cancel', [CMD.RESTORE]: 'restore' }[cmd];
      if (!p || p.kind !== kind) { io.log?.(`忽略未请求的静置初始化 ACK（CMD 0x${cmd.toString(16)}，status=${status}）`); return true; }
      if (status === ST.OK) { p.acked = true; arm(kind); changed(); return true; }
      if (kind === 'start') { finish(false, startFailText(status, detail)); return true; }
      if (kind === 'cancel') { finish(false, `取消失败（ACK 0x${status.toString(16).padStart(2, '0')}）`); return true; }
      // 恢复默认：采集中 0x0703 → 不回 0x0E，直接失败；Flash 失败 0x03 → 随后的 0x0E 确认仍是原值
      if (status === ST.FAIL && detail === DETAIL.ACTIVE) { finish(false, '恢复失败：正在采集，参数不变（0x0703）'); return true; }
      if (status === ST.FAIL) { p.flashFail = true; p.acked = true; p.before = s.settings; arm(kind); changed(); return true; } // 固件随后仍回 0x0E（原值）
      finish(false, `恢复失败（ACK 0x${status.toString(16).padStart(2, '0')}），参数不变`);
      return true;
    }
    function onStatus(m) {
      clearTimeout(probe); probe = null;
      const prev = s.status; s.supported = true; s.status = m; s.statusAt = Date.now(); polling = false;
      if (m.state === STATE.COLLECT && (!prev || prev.state !== STATE.COLLECT)) s.collectTotal = m.remainingMs;
      if (m.state === STATE.COLLECT) s.collectTotal = Math.max(s.collectTotal, m.remainingMs);
      if (active(m)) disarmRestore(false);
      const p = s.pending;
      if (p && p.acked && p.kind === 'start') finish(true, active(m) ? '已开始：请保持设备静止（先静置 5 s，再采集 60 s）' : `已开始（设备状态：${STATE_TEXT[m.state]}）`);
      else if (p && p.acked && p.kind === 'cancel') finish(true, '已取消：未写 Flash，参数不变');
      if (prev && active(prev) && !active(m)) {
        const end = (text, ok) => { note(text, ok); io.say?.(text); };
        if (m.state === STATE.DONE) end('静置初始化成功：已写入 Flash 并立即生效', true);
        else if (m.state === STATE.FAILED) end(`静置初始化失败：${ERROR_TEXT[m.error] || `未知原因 ${m.error}`}`, false);
        else if (!s.message || /^已开始/.test(s.message)) note('已停止，回到空闲');
      }
      changed();
    }
    function onSettings(m) {
      clearTimeout(probe); probe = null;
      s.supported = true; s.settings = m; s.settingsAt = Date.now();
      if (m.source !== 1) disarmRestore(false);
      const p = s.pending;
      if (p && p.acked && p.kind === 'restore') {
        if (p.flashFail) {
          const same = !p.before || (p.before.source === m.source && sameParams(p.before.current, m.current));
          finish(false, same ? '恢复失败（ACK 0x03：Flash 写入失败或未在设置模式）：回读确认参数和静置记录都未变' : '恢复失败（ACK 0x03），但回读与之前不同，请点「读取」核对');
        }
        else if (m.source === 0 && sameParams(m.current, m.defaults)) finish(true, '已恢复默认 VQF 参数：静置记录已作废，下次上电走原来的开机采集');
        else finish(false, `回读不一致：source=${m.source}，当前值${sameParams(m.current, m.defaults) ? '等于' : '不等于'}默认值`);
        return;
      }
      changed();
    }
    function onDropped(m) { s.dropped++; if (s.dropped <= 3) io.log?.(`丢弃无法识别的静置初始化帧 0x${m.id.toString(16).padStart(2, '0')}（${m.type === 'badLength' ? `len=${m.length}，应为 ${m.expected}` : m.reason || '内容无效'}）`); changed(); }
    // 宿主 ~250 ms 调用：采集中若 1 s 没收到推送（例如换了口），静默补查 0x29
    function tick(now = Date.now()) {
      if (s.supported !== true || !active(s.status) || polling || now - s.statusAt < STALE_MS) return false;
      polling = true; s.quietSent++; void io.send(CMD.QUERY_STATUS, [], { quiet: true }).then(() => { setTimeout(() => { polling = false; }, 500); });
      return true;
    }
    function reset() {
      clearTimeout(probe); clearTimeout(ackTimer); clearTimeout(armTimer); probe = ackTimer = armTimer = null; polling = false;
      Object.assign(s, { supported: null, status: null, settings: null, statusAt: 0, settingsAt: 0, pending: null, message: '', result: null, restoreArmed: false, collectTotal: 0, dropped: 0 });
      changed();
    }
    const busy = () => active(s.status) || (!!s.pending && s.pending.kind === 'start');
    return { state: s, query, start, cancel, armRestore, disarmRestore, restore, canRestore, onAck, onStatus, onSettings, onDropped, tick, reset, busy, active: () => active(s.status) };
  }

  const n = (v, d) => (Number.isFinite(v) ? v.toFixed(d) : '--');
  const sec = (ms) => `${Math.ceil(ms / 1000)} s`;
  // DOM：#vqfPanel 内 #vqfSource、#vqfParams（tbody）、#vqfBias、#vqfTemp、#vqfState、#vqfBar、#vqfLive、#vqfMsg、#vqfRead、#vqfRestore、#vqfCancel、#vqfStart
  function bindDom(ctl, { doc = document, editable = () => true, connected = () => true } = {}) {
    const $ = (id) => doc.getElementById(id);
    const set = (node, k, v) => { if (node && node[k] !== v) node[k] = v; };
    const body = $('vqfParams');
    if (body && !body.children.length) body.innerHTML = PARAMS.map((p) => `<tr data-k="${p.key}" title="${p.title}"><th>${p.label} <small>${p.unit}</small></th><td data-c="cur">--</td><td data-c="def">--</td></tr>`).join('');
    if ($('vqfStart')) $('vqfStart').onclick = () => ctl.start();
    if ($('vqfCancel')) $('vqfCancel').onclick = () => ctl.cancel();
    if ($('vqfRead')) $('vqfRead').onclick = () => ctl.query();
    if ($('vqfRestore')) $('vqfRestore').onclick = () => (ctl.state.restoreArmed ? ctl.restore() : ctl.armRestore());
    function render() {
      const s = ctl.state, on = connected(), st = s.status, cfg = s.settings, act = active(st);
      const panel = $('vqfPanel'); if (!panel) return;
      set(panel, 'hidden', s.supported === false);
      const state = s.supported === false ? 'unsupported' : act ? 'active' : st?.state === STATE.DONE ? 'done' : st?.state === STATE.FAILED ? 'failed' : 'idle';
      if (panel.dataset.state !== state) panel.dataset.state = state;
      set($('vqfSource'), 'textContent', cfg ? SOURCE_TEXT[cfg.source] : '--');
      if ($('vqfSource') && $('vqfSource').dataset.source !== String(cfg?.source ?? '')) $('vqfSource').dataset.source = String(cfg?.source ?? '');
      for (const p of PARAMS) {
        const row = body?.querySelector(`tr[data-k="${p.key}"]`); if (!row) continue;
        const c = row.querySelector('[data-c="cur"]'), d = row.querySelector('[data-c="def"]');
        set(c, 'textContent', cfg ? n(cfg.current[p.key], p.digits) : '--'); set(d, 'textContent', cfg ? n(cfg.defaults[p.key], p.digits) : '--');
        const diff = !!cfg && fr(cfg.current[p.key]) !== fr(cfg.defaults[p.key]); if (c && c.dataset.diff !== String(diff)) c.dataset.diff = String(diff);
      }
      set($('vqfBias'), 'textContent', cfg ? `${cfg.bias.map((b) => (b >= 0 ? '+' : '') + b.toFixed(3)).join(' / ')} °/s` : '--');
      set($('vqfBiasK'), 'textContent', cfg?.source === 1 ? '零偏（静置记录）' : '零偏（VQF 当前）');
      set($('vqfBiasK'), 'title', cfg?.source === 1 ? 'Flash 里那次静置结果；实时零偏看运动零偏 0x09 / 诊断 0x0C' : '此刻 VQF 正在用的零偏');
      set($('vqfTempRow'), 'hidden', cfg?.source !== 1);
      set($('vqfTemp'), 'textContent', cfg?.source === 1 ? `${n(cfg.calTempC, 1)} °C` : '--');
      // 进度：只用设备回报的 remaining_ms；采集阶段以首帧 remaining 为总长
      let stText = st ? STATE_TEXT[st.state] : '--', pct = 0;
      if (st?.state === STATE.WAIT) stText = '等待放稳：保持设备静止';
      if (st?.state === STATE.PRE) stText = `预稳定计时中：剩余 ${sec(st.remainingMs)}（含采集）`;
      if (st?.state === STATE.COLLECT) { stText = `正在采集：剩余 ${sec(st.remainingMs)}`; pct = s.collectTotal ? Math.max(0, Math.min(1, 1 - st.remainingMs / s.collectTotal)) : 0; }
      if (st?.state === STATE.CHECK) { stText = '正在检查…'; pct = 1; }
      if (st?.state === STATE.DONE) stText = '上次结果：成功';
      if (st?.state === STATE.FAILED) stText = `上次结果：失败（${ERROR_TEXT[st.error] || st.error}）`;
      set($('vqfState'), 'textContent', stText);
      const bar = $('vqfBar'); if (bar) { const w = `${Math.round(pct * 1000) / 10}%`; if (bar.style.width !== w) bar.style.width = w; }
      set($('vqfProg'), 'hidden', !act);
      const live = act && st ? `角速度 ${n(st.gyroRateDps, 2)} °/s · 加速度偏差 ${n(st.accDevMs2, 2)} m/s² · ${n(st.tempC, 1)} °C · 样本 ${st.samples}` : '';
      set($('vqfLive'), 'textContent', live);
      const moving = act && st && (st.gyroRateDps > 2.0 || st.accDevMs2 > 0.8); // 文档：明显移动门槛（仅用于提示颜色）
      if ($('vqfLive') && $('vqfLive').dataset.warn !== String(!!moving)) $('vqfLive').dataset.warn = String(!!moving);
      set($('vqfMsg'), 'textContent', s.message);
      const res = s.pending || !s.result || s.message !== s.result.text ? '' : s.result.kind;
      if (panel.dataset.result !== res) panel.dataset.result = res;
      const ready = on && s.supported === true && !s.pending;
      set($('vqfStart'), 'hidden', act); set($('vqfStart'), 'disabled', !ready || act || !editable());
      set($('vqfCancel'), 'hidden', !act); set($('vqfCancel'), 'disabled', !on || !!s.pending);
      set($('vqfRestore'), 'hidden', !(cfg?.source === 1) || act);
      set($('vqfRestore'), 'disabled', !ready || !ctl.canRestore() || !editable());
      set($('vqfRestore'), 'textContent', s.restoreArmed ? '确认恢复' : '恢复默认');
      if ($('vqfRestore') && $('vqfRestore').dataset.armed !== String(!!s.restoreArmed)) $('vqfRestore').dataset.armed = String(!!s.restoreArmed);
      set($('vqfRead'), 'disabled', !on || s.supported === false);
    }
    return { render };
  }

  root.GyroVqf = { CMD, MSG, LEN_STATUS, LEN_SETTINGS, VERSION, ST, DETAIL, STATE, STATE_TEXT, ERROR_TEXT, SOURCE_TEXT, PARAMS, PROBE_MS, ACK_TIMEOUT_MS, RESTORE_CONFIRM_MS,
    UNSUPPORTED_TEXT, RESTORE_CONFIRM_TEXT, ACTIVE_TEXT, decode, sameParams, startFailText, detailText, createController, bindDom };
})(typeof globalThis !== 'undefined' ? globalThis : this);
