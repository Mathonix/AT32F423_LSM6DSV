// AT32 AHRS · 启动零偏历史（只读，biashist1）：0x33 [offset u16 LE] → 0x34（60 B）。规格 /workspace/zaru/host-agent-bias-history-list.md。
// 这是开机采集写进 Flash（0x0803E800 / 0x0803F800）的历史：不是 0x09 运动零偏，也不是 0x0E 里的当前零偏；静置初始化结果不进此表。
// 翻页：从 offset 0 开始，下一次 offset + entry_count；entry_count = 0 或 offset + entry_count >= count 时结束；
// 两次回复 sequence 不同 → 中间又保存过，整表重读（最多 3 次）。成功不回 ACK；旧固件 ACK 0x01 或 3 s 无回复 → 隐藏；ACK 0x02 = offset / 长度无效。
(function (root) {
  'use strict';
  const CMD = { QUERY: 0x33 };
  const MSG = { HISTORY: 0x34 };
  const LEN = 60, VERSION = 1, MAX = 50, PER = 3, PAGE_TIMEOUT_MS = 1500, PROBE_MS = 3000, MAX_RESTARTS = 3;
  const RECORD_TEXT = { 0: '无记录', 2: '旧格式（15 条槽）', 3: '50 条槽' };

  function decode(id, payload) {
    if (id !== MSG.HISTORY) return { type: 'unknown', id, length: payload.length };
    if (payload.length !== LEN) return { type: 'badLength', id, length: payload.length, expected: LEN };
    const v = new DataView(payload.buffer, payload.byteOffset, payload.length);
    const [version, recordVersion, corrupt, count] = [payload[0], payload[1], payload[2], payload[3]];
    const offset = v.getUint16(4, true), entryCount = payload[6], reserved = payload[7], sequence = v.getUint32(8, true);
    const bad = (reason) => ({ type: 'unknown', id, length: payload.length, reason });
    if (version !== VERSION) return bad(`version ${version}`);
    if (![0, 2, 3].includes(recordVersion) || corrupt > 1 || count > MAX || entryCount > PER || reserved !== 0 || offset > MAX) return bad('字段超出范围');
    if (recordVersion === 0 && (count !== 0 || entryCount !== 0)) return bad('无记录却有条目');
    if (entryCount && offset + entryCount > count) return bad('条目超出 count');
    const entries = [];
    for (let k = 0; k < entryCount; k++) {
      const o = 12 + 16 * k, bias = [0, 4, 8].map((d) => v.getFloat32(o + d, true)), temp = v.getFloat32(o + 12, true);
      if (![...bias, temp].every(Number.isFinite)) return bad('非有限数');
      entries.push({ index: offset + k, bias, tempC: temp });
    }
    return { type: 'biasHistory', version, recordVersion, corrupt, count, offset, entryCount, sequence, entries };
  }
  const offsetPayload = (n) => [n & 0xff, (n >> 8) & 0xff];

  // io: { send(cmd, payload, { quiet }) → Promise<bool>, log(text), changed() }
  function createController(io) {
    const s = { supported: null, reading: false, list: null, meta: null, message: '', result: null, pages: 0, restarts: 0, dropped: 0, readAt: 0, expect: null };
    let pageTimer = null, probe = null, seq0 = null, buf = [];
    const changed = () => io.changed?.();
    function stop(msg, kind) { clearTimeout(pageTimer); pageTimer = null; s.reading = false; s.expect = null; s.message = msg; s.result = kind; changed(); }
    async function request(offset) {
      s.expect = offset; s.pages++;
      clearTimeout(pageTimer);
      pageTimer = setTimeout(() => {
        if (!s.reading) return;
        if (s.supported !== true) { stop('', null); return; } // 探测阶段交给 3 s 探测计时
        stop(`未读完：第 ${offset} 条起的一页没有回复（已读 ${buf.length} / ${s.meta?.count ?? '?'}）`, 'unconfirmed');
      }, PAGE_TIMEOUT_MS);
      const ok = await io.send(CMD.QUERY, offset === 0 && !s.restarts && !buf.length ? [] : offsetPayload(offset), { quiet: true });
      if (!ok && s.reading) stop('发送失败', 'fail');
      return ok;
    }
    async function read() {
      if (s.supported === false || s.reading) return false;
      s.reading = true; s.restarts = 0; s.message = '正在读取…'; s.result = null; seq0 = null; buf = []; s.pages = 0; changed();
      if (s.supported !== true && !probe) probe = setTimeout(() => { probe = null; if (s.supported !== true) { s.supported = false; stop('', null); io.log?.('启动零偏历史：3 s 内无 0x34，按旧固件隐藏'); } }, PROBE_MS);
      return request(0);
    }
    function onMessage(m) {
      clearTimeout(probe); probe = null; s.supported = true;
      if (!s.reading || m.offset !== s.expect) { io.log?.(`忽略未请求的启动零偏历史帧（offset ${m.offset}）`); changed(); return; }
      if (seq0 === null) seq0 = m.sequence;
      else if (m.sequence !== seq0) {
        if (++s.restarts > MAX_RESTARTS) { stop('读取期间记录一直在变化，请稍后重读', 'fail'); return; }
        io.log?.(`启动零偏历史：sequence ${seq0} → ${m.sequence}，中间又保存过，整表重读`);
        seq0 = null; buf = []; void request(0); return;
      }
      buf.push(...m.entries);
      s.meta = { recordVersion: m.recordVersion, corrupt: m.corrupt, count: m.count, sequence: m.sequence };
      const next = m.offset + m.entryCount;
      if (m.entryCount === 0 || next >= m.count) {
        s.list = buf.slice(); s.readAt = Date.now();
        stop(m.count === 0 ? '设备上还没有启动零偏记录' : `已读取 ${s.list.length} 条（从旧到新），sequence ${m.sequence}`, 'ok');
        return;
      }
      void request(next);
    }
    function onAck(cmd, status) {
      if (cmd !== CMD.QUERY) return false;
      if (status === 1) { clearTimeout(probe); probe = null; s.supported = false; stop('', null); io.log?.('启动零偏历史：旧固件（ACK 0x01），隐藏'); return true; }
      stop(`读取失败（ACK 0x${status.toString(16).padStart(2, '0')}${status === 2 ? '：offset / 长度无效' : ''}）`, 'fail');
      return true;
    }
    function onDropped(m) { s.dropped++; io.log?.(`丢弃无法识别的启动零偏历史帧（${m.type === 'badLength' ? `len=${m.length}，应为 ${m.expected}` : m.reason || '内容无效'}）`); if (s.reading) stop('收到无法识别的历史帧，已丢弃；请重读', 'fail'); }
    function reset() { clearTimeout(pageTimer); clearTimeout(probe); pageTimer = probe = null; seq0 = null; buf = [];
      Object.assign(s, { supported: null, reading: false, list: null, meta: null, message: '', result: null, pages: 0, restarts: 0, dropped: 0, readAt: 0, expect: null }); changed(); }
    return { state: s, read, onMessage, onAck, onDropped, reset };
  }

  const f3 = (x) => (x >= 0 ? '+' : '') + x.toFixed(3);
  // DOM：#bhPanel、#bhMeta、#bhBody（tbody）、#bhMsg、#bhRead、#bhWrap、#bhEmpty
  function bindDom(ctl, { doc = document, connected = () => true } = {}) {
    const $ = (id) => doc.getElementById(id);
    const set = (n, k, v) => { if (n && n[k] !== v) n[k] = v; };
    if ($('bhRead')) $('bhRead').onclick = () => ctl.read();
    let drawn = null;
    function render() {
      const s = ctl.state, p = $('bhPanel'); if (!p) return;
      set(p, 'hidden', s.supported === false);
      const res = s.reading ? 'reading' : s.result || '';
      if (p.dataset.result !== res) p.dataset.result = res;
      const m = s.meta;
      set($('bhMeta'), 'textContent', m ? `${m.count} 条 · ${RECORD_TEXT[m.recordVersion] || m.recordVersion} · 序号 ${m.sequence}` : '--');
      set($('bhCorrupt'), 'hidden', !(m && m.corrupt));
      set($('bhMsg'), 'textContent', s.message);
      set($('bhRead'), 'disabled', !connected() || s.reading || s.supported === false);
      set($('bhRead'), 'textContent', s.reading ? '读取中…' : '读取');
      const list = s.list || [];
      set($('bhEmpty'), 'hidden', !(s.list && !list.length));
      set($('bhWrap'), 'hidden', !list.length);
      if (drawn !== s.list && $('bhBody')) {
        drawn = s.list;
        const last = list.length - 1;
        // 最新在上；# 为设备序号（0 = 最旧）
        $('bhBody').innerHTML = list.slice().reverse().map((e) => `<tr${e.index === list[last].index ? ' class="bh-new"' : ''}><td>${e.index}</td>${e.bias.map((b) => `<td>${f3(b)}</td>`).join('')}<td>${e.tempC.toFixed(1)}</td></tr>`).join('');
      }
    }
    return { render };
  }

  root.GyroBiasHist = { CMD, MSG, LEN, VERSION, MAX, PER, PAGE_TIMEOUT_MS, PROBE_MS, MAX_RESTARTS, RECORD_TEXT, decode, offsetPayload, createController, bindDom };
})(typeof globalThis !== 'undefined' ? globalThis : this);
