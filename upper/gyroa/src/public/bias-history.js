// AT32 AHRS · 启动零偏历史（只读，biashist1）：0x33 [offset u16 LE] → 0x34（60 B）。规格见内部文档 host-agent-bias-history-list.md（不随仓库发布）。
// 这是开机采集写进 Flash（0x0803E800 / 0x0803F800）的历史：不是 0x09 运动零偏，也不是 0x0E 里的当前零偏；静置初始化结果不进此表。
// 翻页：从 offset 0 开始，下一次 offset + entry_count；entry_count = 0 或 offset + entry_count >= count 时结束；
// 两次回复 sequence 不同 → 中间又保存过，整表重读（最多 3 次）。成功不回 ACK；旧固件 ACK 0x01 → 隐藏；ACK 0x02 = offset / 长度无效。
// calfix1：3 s 无回复不是「旧固件」——固件回复队列满会丢帧、上电启动窗口内也可能不回；只提示未收到并自动重试（最多 PROBE_RETRIES 次），卡片保留。
(function (root) {
  'use strict';
  const CMD = { QUERY: 0x33 };
  const MSG = { HISTORY: 0x34 };
  const LEN = 60, VERSION = 1, MAX = 50, PER = 3, PAGE_TIMEOUT_MS = 1500, PROBE_MS = 3000, MAX_RESTARTS = 3, PROBE_RETRIES = 3, RETRY_MS = 1000;
  const NO_REPLY_TEXT = '未收到设备回复（可能仍在启动或回复被丢弃），可点「读取」重试';
  const RECORD_TEXT = { 0: '无记录', 2: '旧格式（15 条槽）', 3: '50 条槽' };

  function decode(id, payload) {
    if (id !== MSG.HISTORY) return { type: 'unknown', id, length: payload.length };
    if (payload.length !== LEN) return { type: 'badLength', id, length: payload.length, expected: LEN };
    const v = new DataView(payload.buffer, payload.byteOffset, payload.length);
    const [version, recordVersion, corrupt, count] = [payload[0], payload[1], payload[2], payload[3]];
    const offset = v.getUint16(4, true), entryCount = payload[6], reserved = payload[7], sequence = v.getUint32(8, true);
    const bad = (reason) => ({ type: 'unknown', id, length: payload.length, reason });
    if (version !== VERSION && version !== 2) return bad(`version ${version}`);
    if (![0, 2, 3].includes(recordVersion) || corrupt > 1 || count > MAX || entryCount > PER || reserved !== 0 || offset > MAX) return bad('字段超出范围');
    if (recordVersion === 0 && (count !== 0 || entryCount !== 0)) return bad('无记录却有条目');
    if (entryCount && offset + entryCount > count) return bad('条目超出 count');
    const entries = [];
    for (let k = 0; k < entryCount; k++) {
      const o = 12 + 16 * k, bias = [0, 4, 8].map((d) => v.getFloat32(o + d, true)), temp = v.getFloat32(o + 12, true);
      if (version === 1) for (let axis = 0; axis < 3; axis++) bias[axis] *= 180 / Math.PI;
      if (![...bias, temp].every(Number.isFinite)) return bad('非有限数');
      entries.push({ index: offset + k, bias, tempC: temp });
    }
    return { type: 'biasHistory', version, recordVersion, corrupt, count, offset, entryCount, sequence, entries };
  }
  const offsetPayload = (n) => [n & 0xff, (n >> 8) & 0xff];

  // io: { send(cmd, payload, { quiet }) → Promise<bool>, log(text), changed() }
  function createController(io) {
    const s = { supported: null, reading: false, list: null, meta: null, message: '', result: null, pages: 0, restarts: 0, dropped: 0, readAt: 0, expect: null };
    let pageTimer = null, probe = null, retry = null, tries = 0, auto = false, seq0 = null, buf = [];
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
      if (!ok && s.reading) { if (auto) { clearTimeout(probe); probe = null; stop('', null); } else stop('发送失败', 'fail'); } // 自动重试时已断开：静默停止
      return ok;
    }
    // 只有 ACK 0x01 是确定性的「不支持」；超时只算未确认，保留卡片并有限次自动重试
    function onProbeTimeout() {
      probe = null;
      if (s.supported !== null) return;
      stop(NO_REPLY_TEXT, 'unconfirmed');
      io.log?.(`启动零偏历史：3 s 内无 0x34（未确认，不按旧固件处理）${tries < PROBE_RETRIES ? `，${RETRY_MS / 1000} s 后自动重试（${tries + 1}/${PROBE_RETRIES}）` : ''}`);
      if (tries < PROBE_RETRIES) { tries++; clearTimeout(retry); retry = setTimeout(() => { retry = null; if (s.supported === null && !s.reading) void read({ auto: true }); }, RETRY_MS); }
    }
    async function read({ auto: isAuto = false } = {}) {
      if (s.supported === false || s.reading) return false;
      clearTimeout(retry); retry = null; auto = isAuto;
      s.reading = true; s.restarts = 0; s.message = '正在读取…'; s.result = null; seq0 = null; buf = []; s.pages = 0; changed();
      if (s.supported !== true && !probe) probe = setTimeout(onProbeTimeout, PROBE_MS);
      return request(0);
    }
    function onMessage(m) {
      const late = s.supported !== true;
      clearTimeout(probe); probe = null; s.supported = true;
      if (!s.reading || m.offset !== s.expect) {
        io.log?.(`忽略未请求的启动零偏历史帧（offset ${m.offset}）`);
        // 晚到 / 乱序的首帧证明固件支持：还没有表就立即整表重读（不必等用户点「读取」）
        if (late && !s.reading && !s.list) { clearTimeout(retry); retry = null; void read({ auto: true }); return; }
        changed(); return;
      }
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
      if (status === 1) { clearTimeout(probe); clearTimeout(retry); probe = retry = null; s.supported = false; stop('', null); io.log?.('启动零偏历史：旧固件（ACK 0x01），隐藏'); return true; }
      stop(`读取失败（ACK 0x${status.toString(16).padStart(2, '0')}${status === 2 ? '：offset / 长度无效' : ''}）`, 'fail');
      return true;
    }
    function onDropped(m) { s.dropped++; io.log?.(`丢弃无法识别的启动零偏历史帧（${m.type === 'badLength' ? `len=${m.length}，应为 ${m.expected}` : m.reason || '内容无效'}）`); if (s.reading) stop('收到无法识别的历史帧，已丢弃；请重读', 'fail'); }
    function reset() { clearTimeout(pageTimer); clearTimeout(probe); clearTimeout(retry); pageTimer = probe = retry = null; tries = 0; auto = false; seq0 = null; buf = [];
      Object.assign(s, { supported: null, reading: false, list: null, meta: null, message: '', result: null, pages: 0, restarts: 0, dropped: 0, readAt: 0, expect: null }); changed(); }
    return { state: s, read, onMessage, onAck, onDropped, reset };
  }

  const formatBias = (x) => (x >= 0 ? '+' : '') + x.toFixed(5);
  // Match the immutable boot value, not the continuously adjusted VQF bias.
  function usedEntries(list, startup) {
    if (![1, 2].includes(startup?.source) || !Array.isArray(startup.initial)) return [];
    return list.filter(e => e.bias.every((b, i) => Number.isFinite(startup.initial[i]) &&
      Math.abs(b - startup.initial[i]) <= Math.max(1e-6, Math.abs(b) * 2e-7)) &&
      Number.isFinite(startup.selectedTemp) && Math.abs(e.tempC - startup.selectedTemp) <= .001).map(e => e.index);
  }
  // DOM：#bhPanel、#bhMeta、#bhBody（tbody）、#bhMsg、#bhRead、#bhWrap、#bhEmpty
  function bindDom(ctl, { doc = document, connected = () => true, startup = () => null } = {}) {
    const $ = (id) => doc.getElementById(id);
    const set = (n, k, v) => { if (n && n[k] !== v) n[k] = v; };
    if ($('bhRead')) $('bhRead').onclick = () => ctl.read();
    let drawn = null, drawnUsage = null;
    function render() {
      const s = ctl.state, p = $('bhPanel'); if (!p) return;
      set(p, 'hidden', s.supported === false && !p.dataset.overview);
      const res = s.reading ? 'reading' : s.result || '';
      if (p.dataset.result !== res) p.dataset.result = res;
      const m = s.meta;
      set($('bhMeta'), 'textContent', m ? `${m.count} 条 · ${RECORD_TEXT[m.recordVersion] || m.recordVersion} · 序号 ${m.sequence}` : '--');
      set($('bhCorrupt'), 'hidden', !(m && m.corrupt));
      set($('bhMsg'), 'textContent', s.supported === false && p.dataset.overview ? '当前固件不支持读取零偏历史，请升级固件后使用。' : s.message);
      set($('bhRead'), 'disabled', !connected() || s.reading || s.supported === false);
      set($('bhRead'), 'textContent', s.reading ? '读取中…' : '读取');
      const list = s.list || [];
      const actual = connected() ? startup() : null, used = usedEntries(list, actual);
      const usage = JSON.stringify([actual?.source ?? null, used]);
      let note = '尚未确认本次启动采用的零偏。';
      if (actual?.source === 3) note = '本次使用默认零偏，未采用历史记录。';
      else if ([1, 2].includes(actual?.source)) {
        if (used.length > 1) note = '多条记录与本次初值及温度相同，已高亮匹配项；设备未提供具体记录序号。';
        else if (used.length === 1) note = `本次${actual.source === 1 ? '新采集' : '历史回退'}：采用 #${used[0]}。`;
        else note = `本次使用${actual.source === 1 ? '新采集' : '历史'}零偏，当前列表中未找到匹配记录。`;
      }
      set($('bhUsedNote'), 'textContent', note);
      set($('bhEmpty'), 'hidden', !(s.list && !list.length));
      set($('bhWrap'), 'hidden', !list.length);
      if ((drawn !== s.list || drawnUsage !== usage) && $('bhBody')) {
        drawn = s.list; drawnUsage = usage;
        const last = list.length - 1;
        // 最新在上；# 为设备序号（0 = 最旧）
        $('bhBody').innerHTML = list.slice().reverse().map((e) => {
          const current = used.includes(e.index), latest = e.index === list[last].index;
          const label = current ? (used.length === 1 ? '当前采用' : '匹配本次初值') : latest ? '最新' : '';
          return `<tr data-index="${e.index}" class="${latest ? 'bh-new ' : ''}${current ? 'bh-used' : ''}"${current ? ' aria-label="本次启动零偏"' : ''}><td>${e.index}</td>${e.bias.map((b) => `<td>${formatBias(b)}</td>`).join('')}<td>${e.tempC.toFixed(1)}</td><td>${label}</td></tr>`;
        }).join('');
      }
    }
    return { render };
  }

  root.GyroBiasHist = { CMD, MSG, LEN, VERSION, MAX, PER, PAGE_TIMEOUT_MS, PROBE_MS, MAX_RESTARTS, PROBE_RETRIES, RETRY_MS, NO_REPLY_TEXT, RECORD_TEXT, decode, offsetPayload, createController, bindDom, formatBias, usedEntries };
})(typeof globalThis !== 'undefined' ? globalThis : this);
