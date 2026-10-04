// AT32 AHRS · 零角速保持（ZARU）阈值读写：协议 + 状态机 + 可选 DOM 绑定。纯逻辑与 DOM 分开，供 gyro1 / gyro3 共用。
// 规格：/workspace/zaru/host-agent-zaru-limits.md（固件尚未烧录；本文件只按规格实现，未经硬件验证）。
// 帧仍为 AA 55 | id | len | seq | payload | CRC16-CCITT(LE)，由 app.js 的 send()/解析器负责；本文件只定义：
//   0x2E 查询（空载荷；成功只回 0x0F，不回 ACK；非空 → ACK 0x02）
//   0x2F 写入（20 B，需设置模式；总是先 ACK detail=0，再回 0x0F）
//   0x0F 阈值消息（40 B，version=1；当前值 @4..21，已保存值 @22..39）
// 成功 = ACK 0 + 随后 0x0F 的当前值等于写入值（f32 比较）；persist=1 时已保存值也须相等。旧固件 ACK 0x01 → 隐藏编辑区。
// restore1：0x31 恢复默认（2 B：persist, 0；需设置模式；先 ACK detail=0 再回 0x0F）。规格 /workspace/zaru/host-agent-zaru-restore.md。
//   默认值只来自固件 zaru_limits_default()：上位机不用 0x2F 写默认数，界面显示用回读的 0x0F 填充；
//   DEFAULTS 只用于核对回读（ACK 0 + 当前值 == 默认表；persist=1 时已保存值也 == 默认表）。旧固件 ACK 0x01 → 只隐藏「恢复默认」。
// 改阈值不切换姿态稳定性档位，不发送 0x26 / 0x27 / 0x0B 以外的任何滤波命令（实际上一条也不发）。
(function (root) {
  'use strict';
  const CMD = { QUERY: 0x2e, SET: 0x2f, RESTORE: 0x31 };
  const MSG = { LIMITS: 0x0f };
  const LEN = 40, WRITE_LEN = 20, VERSION = 1;
  const ST = { OK: 0, UNSUPPORTED: 1, BAD: 2, FAIL: 3 };
  const ACK_TIMEOUT_MS = 3000;
  // 允许范围（含端点）；exit 另须 > enter。float=true 按 f32 传输
  const FIELDS = [
    { key: 'enterDps', label: '进入', unit: '°/s', min: 0.05, max: 2.0, def: 0.30, float: true, step: 0.01, digits: 2,
      title: '进入阈值：低通后的角速度 RMS 低于此值才累计进入（0.05～2.00 °/s）' },
    { key: 'exitDps', label: '退出', unit: '°/s', min: 0.05, max: 5.0, def: 0.70, float: true, step: 0.01, digits: 2, minExclusive: true,
      title: '退出阈值：未滤波角速度模长高于此值才累计退出；须大于进入阈值，且 ≤ 5.00 °/s' },
    { key: 'accDevMs2', label: '加速度偏差', unit: 'm/s²', min: 0.02, max: 2.0, def: 0.15, float: true, step: 0.01, digits: 2,
      title: '加速度模长相对重力的偏差小于此值才允许进入（不参与退出；0.02～2.00 m/s²）' },
    { key: 'enterFilterMs', label: '进入低通', unit: 'ms', min: 0, max: 200, def: 10, step: 1,
      title: '进入用的速率平方低通时间（0～200 ms，0 表示不低通）' },
    { key: 'enterConfirmMs', label: '进入确认', unit: 'ms', min: 0, max: 2000, def: 50, step: 1,
      title: '满足进入条件持续多久才锁定（0～2000 ms）' },
    { key: 'exitConfirmMs', label: '退出确认', unit: 'ms', min: 0, max: 500, def: 3, step: 1,
      title: '超过退出阈值持续多久才解除（0～500 ms）' },
  ];
  const KEYS = FIELDS.map((f) => f.key);
  const DEFAULTS = Object.freeze(Object.fromEntries(FIELDS.map((f) => [f.key, f.def])));
  const fr = Math.fround;
  const RESTORE_UNSUPPORTED_TEXT = '当前固件不支持恢复默认（旧固件，ACK 0x01），未恢复；已隐藏「恢复默认」';
  const RESTORE_CONFIRM_MS = 4000;
  const RESTORE_CONFIRM_TEXT = '再次点击「确认恢复」：六个阈值恢复为固件默认值并写入 Flash（不改融合模式 / 档位 / 量程 / 输出速率 / CAN）';
  const UNSUPPORTED_TEXT = '当前固件不支持零角速保持阈值（旧固件），已隐藏编辑区';
  const DISABLED_TEXT = '当前固件未启用零角速保持，阈值只读、不可编辑';
  const NOTE = '静止时锁定航向，检测到运动后解除。仅六轴融合 + 第 4 档「零角速保持」按这些阈值锁定航向；九轴下可读写，但不锁磁力计航向。';
  const NOTE_NINE = '静止时锁定航向，检测到运动后解除。当前为九轴融合：阈值可读写，但不锁磁力计航向；只有六轴 + 第 4 档「零角速保持」才按这些阈值锁定航向。';
  const NOTE_SHORT = '修改立即生效，不切换档位；不勾选「重启后保留」则断电后恢复已保存值。';

  // 显示用：f32 → 去掉单精度尾数噪声（0.30000001 → "0.3"）
  function fmt(f, v) { if (!Number.isFinite(v)) return '--'; return f.float ? String(Number(v.toFixed(4))) : String(v); }
  function fmtFixed(f, v) { if (!Number.isFinite(v)) return '--'; return f.float ? v.toFixed(f.digits) : String(v); }

  // 范围检查（数值已解析）。返回 { key, error }；全部合法 → null。浮点按设备侧单精度（f32）比较
  function checkFields(set) {
    if (!set || typeof set !== 'object') return { key: null, error: '缺少阈值' };
    for (const f of FIELDS) {
      const v = set[f.key];
      if (typeof v !== 'number' || !Number.isFinite(v)) return { key: f.key, error: `${f.label}不是有效数字` };
      if (!f.float && !Number.isInteger(v)) return { key: f.key, error: `${f.label}须为整数 ${f.unit}` };
      const x = f.float ? fr(v) : v, lo = f.float ? fr(f.min) : f.min, hi = f.float ? fr(f.max) : f.max;
      if (f.key === 'exitDps') { if (!(x <= hi)) return { key: f.key, error: `退出阈值须 ≤ ${f.max.toFixed(2)} °/s` }; continue; } // 下限由 exit > enter 保证
      if (x < lo || x > hi) return { key: f.key, error: `${f.label}范围 ${fmtFixed(f, f.min)}～${fmtFixed(f, f.max)} ${f.unit}` };
    }
    if (!(fr(set.exitDps) > fr(set.enterDps))) return { key: 'exitDps', error: `退出阈值必须大于进入阈值（进入 ${fmtFixed(FIELDS[0], set.enterDps)} °/s）` };
    return null;
  }
  const checkSet = (set) => checkFields(set)?.error || '';
  // 表单文本 → { ok, values } | { ok:false, error, key }；不发送
  function parseForm(texts) {
    const values = {};
    for (const f of FIELDS) {
      const raw = String(texts?.[f.key] ?? '').trim();
      if (!raw) return { ok: false, key: f.key, error: `请填写${f.label}（${f.unit}）` };
      const ok = f.float ? /^[+]?(\d+(\.\d*)?|\.\d+)$/.test(raw) : /^\d+$/.test(raw);
      if (!ok) return { ok: false, key: f.key, error: f.float ? `${f.label}须为数字（${f.unit}）` : `${f.label}须为整数 ${f.unit}` };
      values[f.key] = Number(raw);
    }
    const bad = checkFields(values);
    if (bad) return { ok: false, key: bad.key, error: bad.error, values };
    return { ok: true, values };
  }
  // 0x2F 载荷：f32 enter, f32 exit, f32 acc_dev, u16 enter_filter, u16 enter_confirm, u16 exit_confirm, u8 persist, u8 0
  function encodeWrite(values, persist) {
    if (checkSet(values)) return null;
    if (persist !== 0 && persist !== 1 && persist !== true && persist !== false) return null;
    const b = new Uint8Array(WRITE_LEN), v = new DataView(b.buffer);
    v.setFloat32(0, values.enterDps, true); v.setFloat32(4, values.exitDps, true); v.setFloat32(8, values.accDevMs2, true);
    v.setUint16(12, values.enterFilterMs, true); v.setUint16(14, values.enterConfirmMs, true); v.setUint16(16, values.exitConfirmMs, true);
    b[18] = persist ? 1 : 0; b[19] = 0;
    return Array.from(b);
  }
  // 0x31 载荷：u8 persist（0/1）, u8 0
  function encodeRestore(persist = 1) {
    if (persist !== 0 && persist !== 1 && persist !== true && persist !== false) return null;
    return [persist ? 1 : 0, 0];
  }
  function readSet(v, o) {
    return { enterDps: v.getFloat32(o, true), exitDps: v.getFloat32(o + 4, true), accDevMs2: v.getFloat32(o + 8, true),
      enterFilterMs: v.getUint16(o + 12, true), enterConfirmMs: v.getUint16(o + 14, true), exitConfirmMs: v.getUint16(o + 16, true) };
  }
  // 0x0F 严格解码：长度不符 → badLength；version≠1 / supported∉{0,1} / 非有限数 / 超范围 → unknown（整帧丢弃）
  // supported=0：固件无零角速保持，数值不解读（可能全 0），只报告 supported=false
  function decode(id, payload) {
    if (id !== MSG.LIMITS) return { type: 'unknown', id, length: payload.length };
    if (payload.length !== LEN) return { type: 'badLength', id, length: payload.length, expected: LEN };
    const v = new DataView(payload.buffer, payload.byteOffset, payload.length);
    if (payload[0] !== VERSION) return { type: 'unknown', id, length: payload.length, reason: `version ${payload[0]}` };
    if (payload[1] > 1) return { type: 'unknown', id, length: payload.length, reason: `supported ${payload[1]}` };
    const reserved = v.getUint16(2, true);
    if (payload[1] === 0) return { type: 'zaruLimits', version: 1, supported: false, reserved, runtime: null, saved: null };
    const runtime = readSet(v, 4), saved = readSet(v, 22);
    const bad = checkSet(runtime) || checkSet(saved);
    if (bad) return { type: 'unknown', id, length: payload.length, reason: `range: ${bad}` };
    return { type: 'zaruLimits', version: 1, supported: true, reserved, runtime, saved };
  }
  // 设备回读 == 写入：浮点按 f32 舍入后精确比较，整数精确比较
  function same(a, b) { return !!a && !!b && FIELDS.every((f) => (f.float ? fr(a[f.key]) === fr(b[f.key]) : a[f.key] === b[f.key])); }
  const isDefault = (set) => same(set, DEFAULTS);
  function toDraft(set) { return Object.fromEntries(FIELDS.map((f) => [f.key, set ? fmt(f, set[f.key]) : ''])); }
  function summary(set) {
    if (!set) return '--';
    const [e, x, a, lf, ec, xc] = FIELDS.map((f) => fmtFixed(f, set[f.key]));
    return `进入 ${e} / 退出 ${x} °/s · 加速度 ${a} m/s² · ${lf}/${ec}/${xc} ms`;
  }
  // 窄行用：进入/退出 °/s · 加速度偏差 · 低通/进入确认/退出确认 ms
  function summaryShort(set) {
    if (!set) return '--';
    const [e, x, a, lf, ec, xc] = FIELDS.map((f) => fmtFixed(f, set[f.key]));
    return `${e}/${x} °/s · ${a} m/s² · ${lf}/${ec}/${xc} ms`;
  }

  // io: { send(cmd, payload) → Promise<seq|null>, editable() → bool（设置模式）, say(text), log(text), changed() }
  function createController(io) {
    const s = {
      supported: null,      // null 未知 / true / false
      unsupportedBy: null,  // 'ack'（旧固件 ACK 0x01 → 隐藏）/ 'flag'（0x0F supported=0 → 只读）
      runtime: null, saved: null, at: 0,
      draft: toDraft(null), persist: true,
      dirty: false,         // 表单有未保存修改：被动回读不覆盖
      loadNext: false,      // 用户点「读取」：下一帧 0x0F 覆盖表单
      pending: null,        // { kind: 'set'|'restore', values, persist, seq, acked }
      restoreSupported: null, // null 未知（0x31 只能在点击后探知）/ true / false（ACK 0x01 → 隐藏「恢复默认」）
      restoreArmed: false,  // 「恢复默认」需在 4 s 内再点一次确认（第一次点击不发送任何帧）
      fusionMode: null,
      message: '', result: null, // result: { ok, text, kind: 'ok'|'fail'|'unconfirmed' }
      dropped: 0,
    };
    let timer = null, armTimer = null;
    const changed = () => io.changed?.();
    const note = (text, ok = null, kind = null) => { s.message = text; if (ok !== null) s.result = { ok, text, kind: kind || (ok ? 'ok' : 'fail') }; changed(); };
    function finish(ok, text, kind) {
      clearTimeout(timer); timer = null; s.pending = null;
      if (ok) { s.dirty = false; s.draft = toDraft(s.runtime); }
      note(text, ok, kind); io.say?.(text);
    }
    function arm() {
      clearTimeout(timer);
      timer = setTimeout(() => {
        if (!s.pending) return;
        finish(false, s.pending.kind === 'restore'
          ? '未确认：未收到恢复默认的回读，界面保留原值；请点「读取」核对设备当前值'
          : '未确认：未收到零角速保持阈值回读，修改已保留；请点「读取」核对设备当前值', 'unconfirmed');
      }, ACK_TIMEOUT_MS);
    }
    function markUnsupported(by) {
      s.supported = false; s.unsupportedBy = by;
      if (by === 'ack') { s.runtime = null; s.saved = null; }
      const text = by === 'ack' ? UNSUPPORTED_TEXT : DISABLED_TEXT;
      if (s.pending) finish(false, text); else note(text);
      io.log?.(text);
    }
    // load=true：用户主动读取，回读覆盖表单（丢弃未保存修改）
    async function query({ load = false } = {}) {
      if (s.pending) return false;
      if (load) s.loadNext = true;
      const seq = await io.send(CMD.QUERY, []);
      if (seq === null || seq === undefined || seq === false) { s.loadNext = false; return false; }
      return true;
    }
    async function save() {
      if (s.pending || s.supported !== true) return false;
      if (io.editable && !io.editable()) { note('请先进入设置模式再保存阈值', false); return false; }
      const parsed = parseForm(s.draft);
      if (!parsed.ok) { s.invalidKey = parsed.key || null; note(parsed.error, false); return false; }
      s.invalidKey = null;
      const persist = s.persist ? 1 : 0;
      const payload = encodeWrite(parsed.values, persist);
      if (!payload) { note('阈值无效，未发送', false); return false; }
      disarmRestore(false);
      s.pending = { kind: 'set', values: parsed.values, persist, seq: null, acked: false };
      s.result = null; note(persist ? '正在保存零角速保持阈值（写入 Flash）…' : '正在应用零角速保持阈值（仅本次运行）…');
      arm();
      const seq = await io.send(CMD.SET, payload);
      if (seq === null || seq === undefined || seq === false) { clearTimeout(timer); timer = null; s.pending = null; note('发送失败，修改已保留', false); return false; }
      if (s.pending) s.pending.seq = seq;
      return true;
    }
    // 恢复默认：第一次点击只“上膛”（提示再点一次），不发送；restore() 本身发送 0x31（界面固定 persist=1）
    function disarmRestore(render = true) { clearTimeout(armTimer); armTimer = null; if (!s.restoreArmed) return; s.restoreArmed = false; if (s.message === RESTORE_CONFIRM_TEXT) s.message = ''; if (render) changed(); }
    function armRestore() {
      if (s.pending || s.supported !== true || s.restoreSupported === false) return false;
      s.restoreArmed = true; s.result = null; note(RESTORE_CONFIRM_TEXT);
      clearTimeout(armTimer); armTimer = setTimeout(() => disarmRestore(), RESTORE_CONFIRM_MS);
      return true;
    }
    async function restore({ persist = 1 } = {}) {
      disarmRestore(false);
      if (s.pending || s.supported !== true || s.restoreSupported === false) { changed(); return false; }
      if (io.editable && !io.editable()) { note('请先进入设置模式再恢复默认', false); return false; }
      const payload = encodeRestore(persist);
      if (!payload) { note('恢复参数无效，未发送', false); return false; }
      s.invalidKey = null;
      s.pending = { kind: 'restore', values: null, persist: payload[0], seq: null, acked: false };
      s.result = null; note(payload[0] ? '正在恢复默认阈值（写入 Flash）…' : '正在恢复默认阈值（仅本次运行）…');
      arm();
      const seq = await io.send(CMD.RESTORE, payload);
      if (seq === null || seq === undefined || seq === false) { clearTimeout(timer); timer = null; s.pending = null; note('发送失败，未恢复', false); return false; }
      if (s.pending) s.pending.seq = seq;
      return true;
    }
    function setDraft(key, text) {
      if (s.pending || !KEYS.includes(key)) return;
      const t = String(text ?? '');
      if (s.draft[key] === t) return;
      s.draft = { ...s.draft, [key]: t }; s.dirty = true; s.invalidKey = null;
      if (s.result && !s.result.ok && s.message === s.result.text && s.result.kind !== 'unconfirmed') s.message = '';
      changed();
    }
    function setPersist(v) { if (s.pending) return; s.persist = !!v; changed(); }
    function discard() { if (s.pending) return; s.dirty = false; s.invalidKey = null; s.draft = toDraft(s.runtime); note(''); }
    // 0x31 的 ACK：0x01 只表示旧固件不认识恢复命令 → 隐藏「恢复默认」，编辑区照常
    function onRestoreAck(status, seq) {
      const p = s.pending && s.pending.kind === 'restore' ? s.pending : null;
      if (status === ST.UNSUPPORTED) {
        s.restoreSupported = false; disarmRestore(false); io.log?.(RESTORE_UNSUPPORTED_TEXT);
        if (p) finish(false, RESTORE_UNSUPPORTED_TEXT); else changed();
        return true;
      }
      s.restoreSupported = true;
      if (!p) { io.log?.(`忽略未请求的恢复默认 ACK（status=${status}）`); return true; }
      if (status === ST.OK) { p.acked = true; p.ackSeq = seq; arm(); changed(); return true; } // 等随后的 0x0F 核对
      if (status === ST.FAIL) { finish(false, p.persist ? '恢复失败（ACK 0x03：未处于设置模式或 Flash 写入失败），运行中的阈值保持原值' : '恢复失败（ACK 0x03：未处于设置模式），运行中的阈值保持原值'); return true; }
      finish(false, status === ST.BAD ? '设备拒绝：恢复命令参数无效（ACK 0x02）' : `设备拒绝恢复（状态 0x${status.toString(16)}）`);
      return true;
    }
    // 返回 true 表示该 ACK 属于本模块（0x2E / 0x2F / 0x31）
    function onAck(cmd, status, detail, seq = null) {
      if (cmd === CMD.RESTORE) return onRestoreAck(status, seq);
      if (cmd !== CMD.QUERY && cmd !== CMD.SET) return false;
      if (status === ST.UNSUPPORTED) { markUnsupported('ack'); return true; }
      if (cmd === CMD.QUERY) {
        s.loadNext = false;
        if (status !== ST.OK) note(`零角速保持阈值查询失败（${status === ST.BAD ? '参数无效' : `状态 0x${status.toString(16)}`}）`);
        return true;
      }
      const p = s.pending;
      if (!p || p.kind !== 'set') { io.log?.(`忽略未请求的零角速保持阈值 ACK（status=${status}）`); return true; }
      if (status === ST.OK) { p.acked = true; p.ackSeq = seq; arm(); changed(); return true; } // ACK 成功 ≠ 完成：等随后的 0x0F 核对
      if (status === ST.FAIL) {
        finish(false, p.persist
          ? '保存失败（ACK 0x03：未处于设置模式或 Flash 写入失败），运行中的阈值保持原值；修改已保留'
          : '应用失败（ACK 0x03：未处于设置模式），运行中的阈值保持原值；修改已保留');
        return true;
      }
      finish(false, status === ST.BAD ? '设备拒绝：阈值参数无效（ACK 0x02），修改已保留' : `设备拒绝（状态 0x${status.toString(16)}），修改已保留`);
      return true;
    }
    function onLimits(msg) {
      s.at = Date.now();
      if (!msg.supported) { s.runtime = null; s.saved = null; s.loadNext = false; if (s.supported !== false || s.unsupportedBy !== 'flag') markUnsupported('flag'); else changed(); return; }
      const wasUnsupported = s.supported === false;
      s.supported = true; s.unsupportedBy = null; s.runtime = msg.runtime; s.saved = msg.saved;
      if (wasUnsupported && (s.message === DISABLED_TEXT || s.message === UNSUPPORTED_TEXT)) s.message = '';
      const p = s.pending;
      // 只有 ACK 0 之后到达的 0x0F 才作为完成依据（串口按序，早先查询的回复一定先于本次 ACK 到达）
      if (p && p.acked && p.kind === 'restore') {
        // 显示一律用回读值；核对：当前值 == 固件默认表，persist=1 时已保存值也 == 默认表
        const runOk = isDefault(msg.runtime), saveOk = !p.persist || isDefault(msg.saved);
        s.draft = toDraft(msg.runtime); s.dirty = false;
        if (runOk && saveOk) finish(true, p.persist ? '已恢复默认阈值（重启后保留），已回读核对' : '已恢复默认阈值（仅本次运行），已回读核对');
        else finish(false, `回读不一致：设备${runOk ? '已保存值' : '当前值'}不是固件默认值，界面显示设备回读值`);
        return;
      }
      if (p && p.acked) {
        const runOk = same(msg.runtime, p.values), saveOk = !p.persist || same(msg.saved, p.values);
        if (runOk && saveOk) finish(true, p.persist ? '零角速保持阈值已保存（重启后保留），已回读核对' : '零角速保持阈值已生效（仅本次运行，重启后恢复已保存值），已回读核对');
        else finish(false, `回读不一致：设备${runOk ? '已保存值' : '当前值'}与写入值不同，修改已保留`);
        return;
      }
      if (!p && (s.loadNext || !s.dirty)) { s.draft = toDraft(msg.runtime); s.dirty = false; s.invalidKey = null; if (s.loadNext && s.result && s.message === s.result.text) s.message = ''; s.loadNext = false; }
      changed();
    }
    // 0x0F 被丢弃（长度 / 版本 / 范围）：不算成功，也不改显示；写入中则等超时 → 未确认
    function onDropped(msg) { s.dropped++; io.log?.(`丢弃无法识别的零角速保持阈值帧 0x0F（${msg.type === 'badLength' ? `len=${msg.length}，应为 ${LEN}` : msg.reason || '内容无效'}）`); changed(); }
    function setFusion(mode) { const m = Number.isInteger(mode) ? mode : null; if (s.fusionMode !== m) { s.fusionMode = m; changed(); } }
    function noteText() { return s.fusionMode !== null && s.fusionMode !== 0 ? NOTE_NINE : NOTE; }
    function statusText() {
      if (s.supported === false) return s.unsupportedBy === 'ack' ? '旧固件不支持' : '固件未启用';
      return s.runtime ? `当前 ${summaryShort(s.runtime)}` : '当前 --';
    }
    function savedText() {
      if (!s.saved) return '已保存 --';
      return same(s.saved, s.runtime) ? '已保存：与当前相同' : `已保存 ${summaryShort(s.saved)}`;
    }
    function reset() {
      clearTimeout(timer); timer = null;
      clearTimeout(armTimer); armTimer = null;
      Object.assign(s, { restoreSupported: null, restoreArmed: false, supported: null, unsupportedBy: null, runtime: null, saved: null, at: 0, draft: toDraft(null), dirty: false, loadNext: false, pending: null, fusionMode: null, message: '', result: null, invalidKey: null });
      changed();
    }
    return { state: s, query, save, restore, armRestore, disarmRestore, setDraft, setPersist, discard, onAck, onLimits, onDropped, setFusion, noteText, statusText, savedText, reset };
  }

  // DOM 绑定：容器 #zaruPanel（建议 <details>）内：#zaruSummary（一行状态）、#zaruFields（空 div，由这里生成 6 个输入 #zaru_<key>）、
  // #zaruNote、#zaruSaved、#zaruPersist、#zaruRead、#zaruSave、#zaruDiscard（可选）、#zaruMsg。
  // editable() = 设置模式；connected() = 已连接且空闲。按钮用 onclick，便于 gyro3 自动设置模式包装。
  function bindDom(ctl, { doc = document, editable = () => true, connected = () => true } = {}) {
    const $ = (id) => doc.getElementById(id);
    const set = (node, k, v) => { if (node && node[k] !== v) node[k] = v; };
    const box = $('zaruFields');
    if (box && !box.children.length) {
      box.innerHTML = FIELDS.map((f) => `<label class="zaru-f" title="${f.title}"><span>${f.label}</span><input id="zaru_${f.key}" type="text" inputmode="${f.float ? 'decimal' : 'numeric'}" autocomplete="off" spellcheck="false" placeholder="${fmtFixed(f, f.def)}" aria-label="${f.label}（${f.unit}）" disabled><em>${f.unit}</em></label>`).join('');
    }
    for (const f of FIELDS) $(`zaru_${f.key}`)?.addEventListener('input', (e) => ctl.setDraft(f.key, e.target.value));
    $('zaruPersist')?.addEventListener('change', (e) => ctl.setPersist(e.target.checked));
    if ($('zaruRead')) $('zaruRead').onclick = () => ctl.query({ load: true });
    if ($('zaruSave')) $('zaruSave').onclick = () => ctl.save();
    if ($('zaruDiscard')) $('zaruDiscard').onclick = () => ctl.discard();
    // 恢复默认：第一次点击上膛（不发送），4 s 内再点一次才发 0x31（persist=1）。gyro3 自动设置模式门只包第二次点击
    if ($('zaruRestore')) $('zaruRestore').onclick = () => (ctl.state.restoreArmed ? ctl.restore({ persist: 1 }) : ctl.armRestore());
    function render() {
      const s = ctl.state, on = connected(), edit = on && editable() && s.supported === true && !s.pending;
      const panel = $('zaruPanel');
      set(panel, 'hidden', s.supported === false && s.unsupportedBy === 'ack');
      for (const f of FIELDS) {
        const input = $(`zaru_${f.key}`); if (!input) continue;
        if (input.value !== s.draft[f.key] && (doc.activeElement !== input || !s.dirty)) input.value = s.draft[f.key];
        set(input, 'disabled', !edit); set(input, 'placeholder', s.supported === false ? '' : fmtFixed(f, f.def)); // 不支持时不显示默认值占位，避免误读
        const bad = s.invalidKey === f.key; if (input.getAttribute('aria-invalid') !== String(bad)) input.setAttribute('aria-invalid', String(bad));
      }
      set($('zaruPersist'), 'checked', s.persist); set($('zaruPersist'), 'disabled', !edit);
      set($('zaruSave'), 'hidden', s.supported === false);
      set($('zaruSave'), 'disabled', !edit);
      set($('zaruDiscard'), 'hidden', s.supported === false);
      set($('zaruRestore'), 'hidden', s.supported !== true || s.restoreSupported === false);
      set($('zaruRestore'), 'disabled', !edit);
      set($('zaruRestore'), 'textContent', s.restoreArmed ? '确认恢复' : '恢复默认');
      if ($('zaruRestore')) { const armed = String(!!s.restoreArmed); if ($('zaruRestore').dataset.armed !== armed) $('zaruRestore').dataset.armed = armed; }
      set($('zaruDiscard'), 'disabled', !s.dirty || !!s.pending);
      set($('zaruRead'), 'disabled', !on || !!s.pending || (s.supported === false && s.unsupportedBy === 'ack'));
      set($('zaruSummary'), 'textContent', ctl.statusText());
      set($('zaruSaved'), 'textContent', ctl.savedText()); set($('zaruSaved'), 'title', s.saved ? `已保存：${summary(s.saved)}` : '');
      set($('zaruSummary'), 'title', s.runtime ? `当前：${summary(s.runtime)}` : '');
      set($('zaruNote'), 'textContent', ctl.noteText());
      const msg = s.supported === false ? (s.unsupportedBy === 'ack' ? UNSUPPORTED_TEXT : DISABLED_TEXT)
        : s.message || (s.dirty ? (on && !editable() ? '有未保存的修改（进入设置模式后可保存）' : '有未保存的修改') : '');
      set($('zaruMsg'), 'textContent', msg);
      if (panel) {
        const state = s.supported === false ? (s.unsupportedBy === 'ack' ? 'unsupported' : 'readonly') : s.pending ? 'pending' : s.dirty ? 'dirty' : 'idle';
        const result = s.pending || !s.result || s.message !== s.result.text ? '' : s.result.kind;
        if (panel.dataset.state !== state) panel.dataset.state = state;
        if (panel.dataset.result !== result) panel.dataset.result = result;
      }
    }
    return { render };
  }

  root.GyroZaru = { CMD, MSG, LEN, WRITE_LEN, VERSION, ST, FIELDS, KEYS, DEFAULTS, ACK_TIMEOUT_MS, RESTORE_CONFIRM_MS, RESTORE_UNSUPPORTED_TEXT, RESTORE_CONFIRM_TEXT, UNSUPPORTED_TEXT, DISABLED_TEXT, NOTE, NOTE_NINE, NOTE_SHORT,
    checkSet, checkFields, parseForm, encodeWrite, encodeRestore, isDefault, decode, same, toDraft, summary, summaryShort, createController, bindDom };
})(typeof globalThis !== 'undefined' ? globalThis : this);
