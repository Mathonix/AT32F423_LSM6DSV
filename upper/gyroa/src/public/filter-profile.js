// AT32 AHRS · 姿态稳定性（滤波档位）协议与状态机。纯逻辑 + 可选 DOM 绑定，三个上位机共用同一份文件。
// 帧：AA 55 | id | len | seq | payload | CRC16-CCITT(LE)，由 app.js 的 send()/解析器负责；本文件只定义
// 0x26 查询 / 0x27 设置 / 0x28 诊断命令的 payload，0x0B / 0x0C 回复的解码，以及“ACK 为准 + 回读核对”的状态机。
// 档位只改变输出平滑与磁航向收敛时间，立即生效；从不发送重启或融合模式命令。
(function (root) {
  'use strict';
  const CMD = { QUERY: 0x26, SET: 0x27, DIAG: 0x28 };
  const MSG = { CONFIG: 0x0b, DIAG: 0x0c };
  const LEN = { [MSG.CONFIG]: 16, [MSG.DIAG]: 60 };
  const ST = { OK: 0, UNSUPPORTED: 1, BAD: 2, FAIL: 3 };
  // 时间常数仅作说明（实时 FILTER_CONFIG 只携带 tau_mag / rest_tau，不含运动时间）；界面不可编辑
  const PROFILES = [
    { id: 0, name: '响应优先', magS: 2, restS: 0.15, motionS: 0.04, hint: '响应优先：较少静态平滑。' },
    { id: 1, name: '均衡', magS: 4, restS: 0.5, motionS: 0.1, hint: '均衡：兼顾响应与静态平滑。' },
    { id: 2, name: '静态稳定', magS: 6, restS: 1.5, motionS: 0.2, hint: '静态稳定：更强平滑，停转后的修正更慢。' },
    // 第 4 档（固件 capabilities=4 才有）：滤波参数与均衡相同；仅六轴在静止时锁定对外航向
    { id: 3, name: '零角速保持', en: 'ZARU / Stationary Heading Hold', magS: 4, restS: 0.5, motionS: 0.1,
      hint: '零角速保持：静止时锁定航向，检测到运动后立即恢复更新。滤波参数与均衡相同；仅六轴锁定航向，九轴不锁磁力计航向。',
      hintNineAxis: '零角速保持：当前为九轴融合，本档不锁定航向（不锁磁力计航向）；滤波参数与均衡相同。' },
  ];
  const ZARU = 3;
  const LEGACY_PROFILE_COUNT = 3;          // 旧固件 0x0B capabilities=3：只有前三档
  const PROFILE_COUNTS = [3, 4];           // 0x0B capabilities 合法值（= 档位数）
  const DEFAULT_PROFILE = 1;
  const NOTE = '只改变输出平滑和磁航向收敛时间，立即生效，无需重启。';
  const UNSUPPORTED_TEXT = '当前固件不支持滤波模式，请升级配套固件';
  const ACK_TIMEOUT_MS = 3000;
  const profileName = (p) => PROFILES[p]?.name ?? `未知(${p})`;

  // count = 设备 0x0B 报告的档位数；旧固件 3 → profile=3 不可编码（不会发出会被拒绝的 0x27）
  function encodeSet(profile, persist, count = PROFILES.length) {
    const n = Math.min(PROFILES.length, PROFILE_COUNTS.includes(count) ? count : LEGACY_PROFILE_COUNT);
    if (!Number.isInteger(profile) || profile < 0 || profile > ZARU || profile >= n) return null;
    if (persist !== 0 && persist !== 1 && persist !== true && persist !== false) return null;
    return [profile, persist ? 1 : 0];
  }

  // 0x0B / 0x0C 严格长度解码；长度不符 → badLength（与 app.js decodePayload 同一约定）
  function decode(id, payload) {
    const expected = LEN[id];
    if (expected === undefined) return { type: 'unknown', id, length: payload.length };
    if (payload.length !== expected) return { type: 'badLength', id, length: payload.length, expected };
    const v = new DataView(payload.buffer, payload.byteOffset, payload.length);
    const f32 = (o) => v.getFloat32(o, true);
    if (id === MSG.CONFIG) {
      // version 1；profile/saved 0..3 且 < capabilities；capabilities = 档位数（3 旧固件 / 4 新固件，不是能力位）；estimator_hz 1000；reserved 0
      const count = payload[3], hz = v.getUint16(4, true), reserved = v.getUint16(6, true), tauMag = f32(8), restTau = f32(12);
      if (payload[0] !== 1 || !PROFILE_COUNTS.includes(count) || payload[1] > ZARU || payload[2] > ZARU || payload[1] >= count || payload[2] >= count ||
          hz !== 1000 || reserved !== 0 || !Number.isFinite(tauMag) || !Number.isFinite(restTau)) return { type: 'unknown', id, length: payload.length };
      return { type: 'filterConfig', version: payload[0], active: payload[1], saved: payload[2], profileCount: count,
        estimatorHz: hz, reserved, tauMag, restTau };
    }
    const vec = (o) => [f32(o), f32(o + 4), f32(o + 8)];
    if (payload[1] > ZARU) return { type: 'unknown', id, length: payload.length }; // 诊断 profile 0..3；其后 rest / mag_flags 范围不变
    return { type: 'filterDiag', version: payload[0], profile: payload[1], rest: !!payload[2], magFlags: payload[3],
      magReady: !!(payload[3] & 1), magInterference: !!(payload[3] & 2), timestampMs: v.getUint32(4, true),
      rawGyro: vec(8), bias: vec(20), residual: vec(32), rawEuler: vec(44), biasSigma: f32(56) };
  }

  // io: { send(cmd, payload) → Promise<seq|null>, say(text), log(text), changed() }
  function createController(io) {
    const s = {
      supported: null,     // null 未知 / true / false（旧固件 0x26/0x28 回 0x01）
      config: null,        // 最近一次 0x0B
      profileCount: null,  // 0x0B capabilities（档位数）：3 旧固件只显示前三档；4 可选「零角速保持」
      fusionMode: null,    // 当前运行融合模式（来自 0x07 CONFIG activeMode，0 六轴 / 1 九轴 / 2 九轴相对角）；不看 LED
      diag: null,          // 最近一次 0x0C
      draft: { profile: DEFAULT_PROFILE, persist: true },
      dirty: false,        // 用户本地未应用的修改：回读不覆盖
      pending: null,       // { profile, persist, setSeq, acked, verifying, ackSeq, querySeq }
      message: '',
      result: null,        // { ok, text }
    };
    let timer = null;
    const changed = () => io.changed?.();
    const note = (text, ok = null) => { s.message = text; if (ok !== null) s.result = { ok, text }; changed(); };
    function finish(ok, text) {
      clearTimeout(timer); timer = null; s.pending = null;
      if (ok) s.dirty = false;
      note(text, ok); io.say?.(text);
    }
    function arm() {
      clearTimeout(timer);
      timer = setTimeout(() => { if (s.pending) finish(false, '未确认姿态稳定性设置回读，修改已保留，请点“读取”核对'); }, ACK_TIMEOUT_MS);
    }
    async function query() { return io.send(CMD.QUERY, []); }
    async function queryDiag() { return io.send(CMD.DIAG, []); }
    async function apply() {
      if (s.pending || s.supported === false) return false;
      const payload = encodeSet(s.draft.profile, s.draft.persist, s.profileCount ?? LEGACY_PROFILE_COUNT);
      if (!payload) { note('请选择有效的姿态稳定性档位', false); return false; }
      s.pending = { profile: payload[0], persist: payload[1], setSeq: null, acked: false, verifying: false, ackSeq: null, querySeq: null };
      s.result = null; note(`正在应用 ${profileName(payload[0])}…`);
      arm();
      const seq = await io.send(CMD.SET, payload);
      if (seq === null || seq === undefined || seq === false) { clearTimeout(timer); s.pending = null; note('发送失败，修改已保留', false); return false; }
      if (s.pending) s.pending.setSeq = seq;
      return true;
    }
    function setDraft(profile, persist) {
      if (s.pending) return;
      if (Number.isInteger(profile) && PROFILES[profile] && profile < (s.profileCount ?? PROFILES.length)) s.draft.profile = profile;
      if (persist !== undefined) s.draft.persist = !!persist;
      s.dirty = true; changed();
    }
    function discard() {
      if (s.pending) return;
      s.dirty = false;
      if (s.config) s.draft.profile = s.config.active;
      note('');
    }
    // 返回 true 表示该 ACK 属于本模块
    function onAck(cmd, status, detail, seq = null) {
      if (cmd === CMD.QUERY || cmd === CMD.DIAG) {
        if (status === ST.UNSUPPORTED && (cmd === CMD.QUERY || !s.config)) {
          s.supported = false; s.config = null; note(UNSUPPORTED_TEXT); io.log?.(UNSUPPORTED_TEXT);
        } else if (status !== ST.OK) note(`${cmd === CMD.QUERY ? '滤波模式查询' : '滤波诊断查询'}失败（${status === ST.BAD ? '参数无效' : `状态 0x${status.toString(16)}`}）`);
        return true;
      }
      if (cmd !== CMD.SET) return false;
      if (!s.pending) { io.log?.(`忽略未请求的姿态稳定性 ACK（status=${status}）`); return true; }
      if (status === ST.OK) {
        // ACK 成功 ≠ 完成：重新查询 0x26，读回 active / saved 一致才算完成
        Object.assign(s.pending, { acked: true, ackDetail: detail, ackSeq: seq, verifying: true }); arm(); changed();
        void query().then((q) => { if (s.pending && q !== null && q !== undefined && q !== false) s.pending.querySeq = q; });
        return true;
      }
      if (status === ST.UNSUPPORTED) { s.supported = false; finish(false, UNSUPPORTED_TEXT); return true; }
      const why = status === ST.FAIL ? '未进入设置模式' : status === ST.BAD ? '参数无效' : `状态 0x${status.toString(16)}`;
      finish(false, `姿态稳定性设置失败（${why}），修改已保留；设备当前：${profileName(detail)}`);
      return true;
    }
    function onConfig(msg) {
      s.config = msg; s.supported = true;
      if (PROFILE_COUNTS.includes(msg.profileCount)) s.profileCount = msg.profileCount;
      if (s.draft.profile >= (s.profileCount ?? PROFILES.length)) { s.draft.profile = msg.active; s.dirty = false; }
      const p = s.pending;
      // 设置回复里随 ACK 附带的 0x0B（seq = setSeq）只更新显示，不作为完成依据
      if (p && p.verifying && msg.seq !== p.ackSeq && msg.seq !== p.setSeq) {
        const ok = msg.active === p.profile && (!p.persist || msg.saved === p.profile);
        if (ok) finish(true, p.persist ? `姿态稳定性已切换为 ${profileName(p.profile)}，已保存（重启后保留）`
          : `姿态稳定性已切换为 ${profileName(p.profile)}（仅本次运行，重启后恢复为已保存的 ${profileName(msg.saved)}）`);
        else finish(false, `姿态稳定性回读不一致：当前 ${profileName(msg.active)}，已保存 ${profileName(msg.saved)}；修改已保留`);
      }
      if (!s.dirty && !s.pending) s.draft.profile = msg.active;
      changed();
    }
    function onDiag(msg) { s.diag = msg; changed(); }
    // 0x07 CONFIG 回读后由 app.js 调用：运行融合模式决定第 4 档说明
    function setFusion(mode) { const m = Number.isInteger(mode) ? mode : null; if (s.fusionMode !== m) { s.fusionMode = m; changed(); } }
    function available(profile) { return Number.isInteger(profile) && profile >= 0 && profile < Math.min(PROFILES.length, s.profileCount ?? PROFILES.length); }
    // en=true：名称后带英文（ZARU / Stationary Heading Hold），用于说明行较宽的布局
    function hint(profile = s.draft.profile, { en = false } = {}) {
      const p = PROFILES[profile]; if (!p) return '';
      const text = p.hintNineAxis && s.fusionMode !== null && s.fusionMode !== 0 ? p.hintNineAxis : p.hint;
      return en && p.en ? text.replace(`${p.name}：`, `${p.name}（${p.en}）：`) : text;
    }
    function reset() { clearTimeout(timer); timer = null; Object.assign(s, { supported: null, config: null, diag: null, pending: null, dirty: false, message: '', result: null, profileCount: null, fusionMode: null }); s.draft.profile = DEFAULT_PROFILE; changed(); }
    function statusText() {
      if (s.supported === false) return UNSUPPORTED_TEXT;
      const c = s.config;
      if (!c) return '当前：-- · 已保存：-- · 内部 -- Hz';
      return `当前：${profileName(c.active)} · 已保存：${profileName(c.saved)} · 内部 ${c.estimatorHz} Hz`;
    }
    return { state: s, query, queryDiag, apply, setDraft, discard, onAck, onConfig, onDiag, setFusion, available, hint, reset, statusText };
  }

  // DOM 绑定：#filterPanel 内 input[name=filterProfile]、#filterPersist、#filterApply、#filterDiscard、#filterRead、
  // #filterState、#filterHint、#filterMsg。editable() 决定是否可编辑（设置模式）；note=false 时“立即生效”说明不进提示行（紧凑布局放到 title）。按钮用 onclick，便于 gyro3 自动设置模式包装。
  function bindDom(ctl, { doc = document, editable = () => true, connected = () => true, note = true, en = false } = {}) {
    const $ = (id) => doc.getElementById(id);
    const radios = () => [...doc.querySelectorAll('input[name="filterProfile"]')];
    const set = (node, k, v) => { if (node && node[k] !== v) node[k] = v; };
    radios().forEach((r) => r.addEventListener('change', () => { if (r.checked) ctl.setDraft(Number(r.value)); }));
    $('filterPersist')?.addEventListener('change', (e) => ctl.setDraft(undefined, e.target.checked));
    $('filterApply').onclick = () => ctl.apply();
    $('filterDiscard').onclick = () => ctl.discard();
    $('filterRead').onclick = () => ctl.query();
    function render() {
      const s = ctl.state, on = connected(), edit = on && editable() && s.supported === true && !s.pending;
      radios().forEach((r) => {
        // 旧固件（profileCount=3）：隐藏并禁用「零角速保持」，不可能选中 → 不会发出 profile=3
        const ok = ctl.available(Number(r.value)), label = r.closest('label');
        set(r, 'checked', Number(r.value) === s.draft.profile); set(r, 'disabled', !edit || !ok);
        if (label) set(label, 'hidden', !ok);
      });
      set($('filterPersist'), 'checked', s.draft.persist); set($('filterPersist'), 'disabled', !edit);
      set($('filterApply'), 'hidden', s.supported === false);
      set($('filterApply'), 'disabled', !edit);
      set($('filterDiscard'), 'disabled', !s.dirty || !!s.pending);
      set($('filterRead'), 'disabled', !on || !!s.pending);
      set($('filterState'), 'textContent', ctl.statusText());
      set($('filterHint'), 'textContent', `${ctl.hint(s.draft.profile, { en })}${note ? NOTE : ''}`);
      const msg = s.supported === false ? UNSUPPORTED_TEXT : s.message || (s.dirty ? '有未应用的修改' : '');
      set($('filterMsg'), 'textContent', msg);
      const panel = $('filterPanel');
      if (panel) {
        const state = s.supported === false ? 'unsupported' : s.pending ? 'pending' : s.dirty ? 'dirty' : 'idle';
        const result = s.pending || !s.result || s.message !== s.result.text ? '' : s.result.ok ? 'ok' : 'fail';
        if (panel.dataset.state !== state) panel.dataset.state = state;
        if (panel.dataset.result !== result) panel.dataset.result = result;
        const prof = String(s.draft.profile); if (panel.dataset.profile !== prof) panel.dataset.profile = prof;
      }
    }
    return { render };
  }

  root.GyroFilter = { CMD, MSG, LEN, ST, PROFILES, ZARU, LEGACY_PROFILE_COUNT, PROFILE_COUNTS, DEFAULT_PROFILE, NOTE, UNSUPPORTED_TEXT, ACK_TIMEOUT_MS, profileName, encodeSet, decode, createController, bindDom };
})(typeof globalThis !== 'undefined' ? globalThis : this);
