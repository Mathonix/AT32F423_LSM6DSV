(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const { AXES } = GyroDrift;
  const session = GyroDrift.session = GyroDrift.createSession();
  let diagSupported = true, diagnosticPending = false, lastQuery = 0, lastDraw = 0, wakeLock = null;
  GyroDrift.unsupportedDiag = () => { diagSupported = false; };
  const num = (n, digits = 5) => Number.isFinite(n) ? n.toFixed(digits) : '—';
  const clock = ms => { const seconds = Math.ceil(ms / 1000); return `${String(Math.floor(seconds / 60)).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`; };
  const context = () => GyroDriftIO.context();
  function metadataText(m) {
    const modes = ['六轴', '九轴', '九轴相对角'], profiles = ['响应优先', '均衡', '静态稳定', '零角速保持'];
    const flag = v => v == null ? '未知' : v ? '开' : '关';
    const algorithms = globalThis.GyroFusionAlgorithm?.NAMES || ['VQF', 'EKF', 'ESKF'];
    return `${m.mock ? '模拟设备 · ' : ''}${m.firmware || '版本未知'} · ${algorithms[m.algorithm] || '算法未知'} · ${modes[m.mode] || '融合模式未知'} · ±${m.rangeDps ?? '?'} dps · ${m.outputHz ?? '?'} Hz · ${profiles[m.profile] || '滤波档位未知'} · 静置零偏 ${flag(m.restBiasEnabled)} · 运动零偏 ${flag(m.motionBiasEnabled)}`;
  }
  async function keepAwake() {
    try { if (session.active && document.visibilityState === 'visible' && !wakeLock) wakeLock = await navigator.wakeLock?.request('screen'); }
    catch { /* unsupported or denied: acquisition still checks data coverage */ }
  }
  function releaseAwake() { void wakeLock?.release(); wakeLock = null; }
  $('driftStart').onclick = () => {
    const c = context();
    if (!c.ready || c.busy) return;
    if (session.state.frames && !$('driftReplace').checked) { GyroUI.toast('请先导出结果，或勾选「覆盖上次结果」', 'warn'); return; }
    if (!session.start(Number($('driftDuration').value), c.metadata)) return;
    $('driftReplace').checked = false; diagSupported = true; lastQuery = 0;
    void keepAwake(); render();
  };
  $('driftStop').onclick = () => { session.stop(); render(); };
  function download(content, type, suffix) {
    const url = URL.createObjectURL(new Blob([content], { type }));
    const a = document.createElement('a'); a.href = url;
    a.download = `gyroa-drift-${session.summary().requestedSeconds / 60}min-${session.state.startedAt?.replace(/[:.]/g, '-') || 'partial'}.${suffix}`;
    a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  $('driftCsv').onclick = () => download(session.csv(), 'text/csv;charset=utf-8', 'csv');
  $('driftJson').onclick = () => download(JSON.stringify({ ...session.summary(), seconds: session.rows() }, null, 2), 'application/json', 'json');
  // Read-only queries are allowed. Mutating controls are blocked while measuring, including navigation to other pages.
  const MUTATING = '#vqfTuningApply,#vqfTuningRestore,#vqfTuningFields input,#magCalStart,#magCalVerify,#magCalSave,#magCalRestore,#ws2812Enable,#magRejectionEnable,#algorithmSelect,#algorithmApply,#statusRestartBtn,#testRestartBtn,#applyBtn,#outputApplyAll,#outputApply0,#outputApply1,#rateApplyBtn,#canApply,#canIdBtn,#filterApply,#zaruSave,#zaruRestore,#vqfStart,#vqfRestore,#acc6Btn,#streamApplyBtn,#firmwareStartBtn,#qsFusion button,#qsFilter button,#enterBtn,#exitBtn';
  document.addEventListener('click', e => {
    if (!session.active || !e.target.closest?.(MUTATING)) return;
    e.preventDefault(); e.stopImmediatePropagation(); GyroUI.toast('请先停止零漂测试，再修改设备', 'warn');
  }, true);
  window.addEventListener('beforeunload', e => { if (session.active) { e.preventDefault(); e.returnValue = ''; } });
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') { wakeLock = null; void keepAwake(); } });
  const states = { idle: '未开始', waiting: '等待首帧', running: '测试中', complete: '已完成', cancelled: '已停止', interrupted: '测试中断' };
  function render() {
    const c = context(), s = session.state, r = session.summary();
    $('driftState').textContent = `${c.metadata.mock ? '模拟 · ' : ''}${states[s.status]}`;
    $('driftState').className = `chip ${s.status === 'complete' ? 'on' : s.status === 'interrupted' ? 'warn' : ''}`;
    $('driftStart').disabled = session.active || !c.ready || c.busy;
    $('driftStop').hidden = !session.active;
    $('driftDuration').disabled = session.active;
    $('driftCsv').disabled = $('driftJson').disabled = !s.frames || session.active;
    $('driftReplaceBox').hidden = !s.frames || session.active;
    $('driftConfig').textContent = metadataText(s.metadata || c.metadata);
    const m = s.metadata || c.metadata;
    $('driftHoldWarning').hidden = m.profile !== 3 || m.mode !== 0;
    $('driftNineWarning').hidden = m.mode == null || m.mode === 0;
    $('driftElapsed').textContent = clock(session.elapsed());
    $('driftRemaining').textContent = clock(Math.max(0, (s.durationMs || Number($('driftDuration').value) * 1000) - session.elapsed()));
    $('driftProgress').value = s.durationMs ? session.elapsed() / s.durationMs * 100 : 0;
    $('driftFrames').textContent = `${r.frames.toLocaleString()} 帧 · ${r.secondsWithData} 秒有数据 · 最大间隔 ${num(r.maxGapMs, 0)} ms`;
    $('driftMessage').textContent = s.reason ? `${s.reason}；结果为未完成测试，可导出已采集数据。` : s.status === 'complete' ? '测试完成。请导出结果后再开始下一轮。'
      : session.active ? '保持板卡固定静止，保持页面打开。离开此页仍继续记录。' : !c.connected ? '请先连接设备。'
      : !c.ready ? '等待完整姿态数据；输出内容需包含 Yaw、Pitch、Roll。' : c.busy ? '设备正在设置、校准或升级。' : '准备好后点击开始，计时从第一帧新数据开始。';
    AXES.forEach(k => {
      const a = r.axes[k];
      ['meanChangeDeg', 'netDeg', 'peakToPeakDeg', 'slopeDegPerMin', 'maxDeviationDeg'].forEach(metric => $(`drift-${k}-${metric}`).textContent = num(a?.[metric]));
    });
    $('driftTemp').textContent = r.temperature ? `${num(r.temperature.first, 2)} → ${num(r.temperature.last, 2)} °C（${num(r.temperature.min, 2)}～${num(r.temperature.max, 2)} °C）` : '无有效温度数据';
    $('driftRest').textContent = r.restFraction == null ? '静置检测：无诊断数据' : `静置检测占比：${num(r.restFraction * 100, 1)}%（设备判断，不能证明板卡始终未移动）`;
    if (!session.active) releaseAwake();
    if (performance.now() - lastDraw > 800 && !document.querySelector('[data-view="test"]').hidden) { lastDraw = performance.now(); draw(); }
  }
  function draw() {
    const canvas = $('driftChart'), rect = canvas.getBoundingClientRect();
    if (!rect.width) return;
    const ratio = devicePixelRatio || 1;
    canvas.width = Math.round(rect.width * ratio); canvas.height = Math.round(280 * ratio);
    const g = canvas.getContext('2d'); g.scale(ratio, ratio);
    const w = rect.width, h = 280, left = 62, top = 18, pw = w - left - 20, ph = h - top - 40;
    const rows = session.rows(), r = session.summary(), base = AXES.map(k => r.axes[k]?.baselineDeg || 0);
    const values = rows.flatMap(row => row.angles.map((v, i) => v - base[i]));
    const scale = Math.max(.005, ...values.map(Math.abs)) * 1.1;
    const xmax = Math.max(60, session.elapsed() / 1000);
    // style1：颜色取自主题 CSS 变量（Yaw / Pitch / Roll = Z / Y / X 轴色）
    const TC = (name, fallback) => window.GyroTheme?.color(name, fallback) || fallback;
    g.font = '12px system-ui'; g.strokeStyle = TC('--chart-grid', '#e4e7ec'); g.fillStyle = TC('--chart-label', '#667085');
    for (let i = 0; i <= 4; i++) { const y = top + ph * i / 4; g.beginPath(); g.moveTo(left, y); g.lineTo(w - 20, y); g.stroke();
      g.fillText(num(scale * (1 - i / 2), 3), 5, y + 4);
      const x = left + pw * i / 4; g.fillText(`${num(xmax * i / 240, 1)} min`, Math.min(x, w - 60), h - 10); }
    g.fillText('°', 8, 12);
    [TC('--c-yaw', '#635bff'), TC('--c-pitch', '#22a06b'), TC('--c-roll', '#f79009')].forEach((color, i) => {
      g.strokeStyle = color; g.lineWidth = 1.6; g.beginPath(); let previous = null;
      rows.forEach(row => { const x = left + (row.second + .5) / xmax * pw, y = top + ph / 2 - (row.angles[i] - base[i]) / scale * ph / 2;
        if (previous == null || row.second !== previous + 1) g.moveTo(x, y); else g.lineTo(x, y); previous = row.second; }); g.stroke();
    });
  }
  setInterval(() => {
    const c = context(); session.tick(c);
    if (session.active && diagSupported && !diagnosticPending && performance.now() - lastQuery >= 100 && document.visibilityState === 'visible') {
      lastQuery = performance.now(); diagnosticPending = true;
      void GyroDriftIO.queryDiagnostic().finally(() => { diagnosticPending = false; });
    }
    render();
  }, 200);
  window.addEventListener('resize', draw);
  render();
})();
