// AT32 AHRS · app1 page glue: output form switching, calibration / CAN / firmware / settings view state.
// Reads app.js state and clicks the original (hidden) controls; never builds protocol frames itself.
(() => {
  const $ = (id) => document.getElementById(id);
  const { toast } = window.GyroUI;
  const setText = (node, text) => { if (node && node.textContent !== text) node.textContent = text; };
  const setHidden = (node, hidden) => { if (node && node.hidden !== hidden) node.hidden = hidden; };

  // ---------------- 输出：one visible form, UART / USB / CAN segmented ----------------
  // app3: port 2 = CAN（原 CAN 页的完整表单原样移入 #outCanPane；只显示当前分段的主按钮）
  let activePort = 0, portPickedFor = null;
  function showPort(port) {
    activePort = port;
    document.querySelectorAll('#outPortSeg button').forEach((b) => {
      const on = Number(b.dataset.port) === port; b.classList.toggle('on', on); b.setAttribute('aria-selected', String(on));
    });
    for (let i = 0; i < 2; i++) setHidden($('outputPanel' + i), i !== port);
    setHidden($('outSerialPane'), port === 2); setHidden($('outCanPane'), port !== 2);
    setHidden($('configState'), port === 2); setHidden($('canConfigState'), port !== 2);
  }
  // #/output?tab=can（首页「CAN 设置」）或旧链接 #/can → CAN 分段
  function syncTab() {
    const h = location.hash;
    if (/^#\/can\b/.test(h) || /[?&]tab=can\b/.test(h)) { showPort(2); if (/^#\/can\b/.test(h)) history.replaceState(null, '', '#/output?tab=can'); }
  }
  $('outPortSeg').addEventListener('click', (e) => { const b = e.target.closest('button[data-port]'); if (b) showPort(Number(b.dataset.port)); });
  document.querySelector('.quick[href="#/output?tab=can"]')?.addEventListener('click', () => setTimeout(() => showPort(2)));
  // 9 output fields: first 6 visible, the rest behind "更多选项"
  for (let i = 0; i < 2; i++) {
    const box = $('outputFields' + i), extra = [...box.querySelectorAll(':scope > label')].slice(6);
    if (!extra.length) continue;
    const more = document.createElement('details'); more.className = 'more';
    more.innerHTML = '<summary>更多选项 ›</summary><div class="more-grid"></div>';
    extra.forEach((node) => more.lastElementChild.append(node));
    box.append(more);
    more.addEventListener('toggle', () => { more.firstElementChild.textContent = more.open ? '收起选项' : '更多选项 ›'; });
  }
  $('outputApplyAll').addEventListener('click', () => {
    if (!running) { toast('请先连接设备', 'warn'); return; }
    if (activePort === 2) return;
    const panelOn = !$('outputPanel' + activePort).disabled, rateOn = !$('rateApplyBtn').disabled;
    if (panelOn) $('outputApply' + activePort).click();
    if (rateOn && (rateFormDirty || !panelOn)) $('rateApplyBtn').click();
    if (!panelOn && !rateOn) toast('请稍候，设备正在处理', 'warn');
  });
  function refreshOutput() {
    if (running && deviceConfig && portPickedFor !== connectedAt) { portPickedFor = connectedAt; if (activePort !== 2) showPort(deviceConfig.source ? 1 : 0); }
    const busy = !!pendingOutput || !!pendingRate;
    const btn = $('outputApplyAll');
    btn.disabled = !running || busy || activePort === 2 || ($('outputPanel' + activePort).disabled && $('rateApplyBtn').disabled);
    setText(btn, busy ? '应用中…' : '应用');
    if ($('outHz').disabled !== !running) $('outHz').disabled = !running;
  }

  // ---------------- 校准 ----------------
  function refreshCalib() {
    const s = accCalibration, busy = accStartPending || s?.status === 1;
    const count = s?.mask ? [0, 1, 2, 3, 4, 5].filter((i) => s.mask & (1 << i)).length : 0;
    const progress = s?.status === 2 ? 1 : busy ? Math.min(1, (count * 1000 + (s?.status === 1 ? (s.progress || 0) : 0)) / 6000) : count / 6;
    const width = `${(progress * 100).toFixed(1)}%`;
    if ($('accOverallBar').style.width !== width) $('accOverallBar').style.width = width;
    setText($('accCount'), `${s?.status === 2 ? 6 : count} / 6`);
    setHidden($('acc6Btn'), busy);
    setHidden($('accCancelBtn'), !busy);
    const chip = $('accStateChip');
    const [text, cls] = !running ? ['未连接', 'chip'] : busy ? ['校准中', 'chip busy'] : s?.status === 2 ? ['已完成', 'chip on']
      : s?.status === 3 || s?.status === 4 ? ['未完成', 'chip warn'] : s?.valid ? ['已校准', 'chip on'] : s?.enabled === false ? ['不支持', 'chip'] : ['未校准', 'chip'];
    setText(chip, text); if (chip.className !== cls) chip.className = cls;
  }

  // ---------------- CAN ----------------
  let canRead = null;
  $('canRefresh').addEventListener('click', () => { canRead = { ref: canConfig, at: performance.now() }; }, true);
  function refreshCan() {
    setHidden($('canDirty'), !canFormDirty);
    if (canRead) {
      if (canConfig && canConfig !== canRead.ref) { canRead = null; toast('✓ 已读取', 'ok'); }
      else if (!running || performance.now() - canRead.at > 3000) { canRead = null; if (running) toast('读取失败', 'error'); }
    }
    setText($('canRefresh'), canRead ? '读取中…' : '读取');
  }

  // ---------------- 升级 ----------------
  const kib = (n) => n >= 1024 ? `${(n / 1024).toFixed(1)} KB` : `${n} B`;
  $('fwRecoveryLink').addEventListener('click', () => { $('fwRecoveryBox').hidden = !$('fwRecoveryBox').hidden; });
  function refreshFirmware() {
    const file = $('firmwareFile').files?.[0];
    setText($('fwFileName'), file ? file.name : '请选择固件');
    setText($('fwFileSize'), file ? `${kib(file.size)}${firmwareImage ? ` · CRC32 ${firmwareImage.crc32.toString(16).padStart(8, '0')}` : ''}` : '');
    setText($('fwPct'), `${$('firmwareProgress').value}%`);
    setHidden($('firmwareStopBtn'), !firmwareBusy);
    if ($('firmwareRecovery').checked) setHidden($('fwRecoveryBox'), false);
    const phase = firmwarePhase;
    const [text, cls] = firmwareBusy ? ['升级中…', 'busy'] : phase === 'complete' ? ['✓ 升级完成', 'ok'] : phase === 'failed' ? ['升级失败', 'fail']
      : phase === 'invalid' ? ['固件文件无效', 'fail'] : phase === 'unconfirmed' ? ['未确认应用启动', 'warn'] : ['', ''];
    const node = $('fwResult');
    setText(node, text); setHidden(node, !text);
    if (node.className !== `result ${cls}`) node.className = `result ${cls}`;
  }
  let lastPhase = 'idle';
  function firmwareToasts() {
    if (firmwarePhase === lastPhase) return;
    if (firmwarePhase === 'failed') toast('升级失败', 'error');
    if (firmwarePhase === 'invalid') toast('固件文件无效', 'error');
    lastPhase = firmwarePhase;
  }

  // ---------------- 设置 ----------------
  $('copyLog').addEventListener('click', async () => {
    const text = $('log').textContent;
    try { await navigator.clipboard.writeText(text); toast('✓ 已复制', 'ok'); }
    catch {
      const area = Object.assign(document.createElement('textarea'), { value: text });
      document.body.append(area); area.select();
      const ok = document.execCommand('copy'); area.remove();
      toast(ok ? '✓ 已复制' : '复制失败', ok ? 'ok' : 'error');
    }
  });
  function refreshSettings() { setHidden($('startupDirty'), !startupFormDirty); }

  window.GyroUI.pages = {
    showPort, syncTab, get activePort() { return activePort; },
    refresh() { refreshOutput(); refreshCalib(); refreshCan(); refreshFirmware(); firmwareToasts(); refreshSettings(); },
  };
  syncTab();
  window.GyroUI.pages.refresh();
})();
