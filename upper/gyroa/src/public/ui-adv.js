// AT32 AHRS · app2 高级设置: structured device status + raw-data table.
// Reads the same values app.js already computes (deviceConfig, parser.stats, pose/imu/quat …);
// the original text elements (#streamLabel, #statsLabel, #yaw2 …) stay in the DOM, hidden.
(() => {
  const $ = (id) => document.getElementById(id);
  const setText = (node, text) => { if (node && node.textContent !== text) node.textContent = text; };
  const setClass = (node, cls) => { if (node && node.className !== cls) node.className = cls; };
  const num = (x, d) => (Number.isFinite(x) ? x.toFixed(d) : '--');

  function streamInfo() {
    const c = deviceConfig;
    if (c) {
      const o = c.outputs[c.source], port = c.source ? 'USB' : 'UART';
      if (o.format === 2) return [`${port} · ${streamModeName(o.legacyMode)}`, 'chip on'];
      if (!o.mask) return [`${port} · 输出关闭`, 'chip'];
      return [`${port} · ${o.format ? 'AA55' : 'JustFloat'} ${selectedCount(o.mask)} 通道`, 'chip on'];
    }
    if (streamMode !== null && streamMode !== undefined) return [streamModeName(streamMode), STREAM_MODES[streamMode] ? 'chip on' : 'chip warn'];
    return [running ? '等待设备' : '未知', 'chip'];
  }
  function canInfo() {
    if (canConfig) return !canConfig.ready ? ['初始化失败', 'chip off'] : canConfig.busOff ? ['总线离线', 'chip off'] : ['就绪', 'chip on'];
    if (canOk) return ['就绪', 'chip on'];
    return [running ? '未就绪' : '未知', 'chip'];
  }
  let flagsKey = '';
  function renderFlags() {
    const text = ($('flagsLabel').textContent || '').replace(/^状态位：/, '').trim();
    if (text === flagsKey) return; flagsKey = text;
    const box = $('stFlags'); box.textContent = '';
    for (const part of text && text !== '-' ? text.split('/') : ['-']) {
      const chip = document.createElement('em');
      chip.className = /异常/.test(part) ? 'chip off' : /扰动/.test(part) ? 'chip warn' : part === '-' ? 'chip' : 'chip on';
      chip.textContent = part; box.append(chip);
    }
  }
  const startupRows = {
    mode: (c) => [fusionModeName(c.activeMode), fusionModeName(c.savedMode)],
    fast: (c) => [c.activeFast ? '开' : '关', c.savedFast ? '开' : '关'],
    init: (c) => [c.activeInitMs, c.savedInitMs].map((ms) => (ms === null ? '--' : `${(ms / 1000).toFixed(1)} s`)),
    range: (c) => [c.activeRangeDps, c.savedRangeDps].map((r) => (r === null ? '--' : `±${r} dps`)),
    rate: (c) => [`${c.outHz} Hz`, c.savedOutputHz === null ? '--' : `${c.savedOutputHz} Hz`],
  };
  function renderStartup() {
    document.querySelectorAll('#stStartup tbody tr').forEach((tr) => {
      const combined = deviceConfig?.version >= 4;
      if (tr.dataset.k === 'fast') tr.hidden = !!combined;
      if (tr.dataset.k === 'init') setText(tr.cells[0], combined ? '启动窗口' : '零偏时间');
      const [now, saved] = deviceConfig ? startupRows[tr.dataset.k](deviceConfig) : ['--', '--'];
      const differ = deviceConfig && saved !== '--' && now !== saved;
      const key = `${now}|${saved}|${differ}`;
      if (tr.dataset.v === key) return; tr.dataset.v = key;
      tr.cells[1].textContent = now;
      tr.cells[2].textContent = saved;
      if (differ) { const b = document.createElement('em'); b.className = 'chip warn'; b.textContent = '重启生效'; tr.cells[2].append(b); }
      tr.classList.toggle('differ', !!differ);
    });
  }
  function renderCounters() {
    const s = parser.stats;
    document.querySelectorAll('#stCounters [data-k]').forEach((chip) => {
      const v = s[chip.dataset.k] || 0;
      setText(chip.querySelector('b'), v.toLocaleString('en-US'));
      setClass(chip, chip.dataset.bad && v > 0 ? `bad-${chip.dataset.bad}` : '');
    });
  }
  function renderRaw() {
    const rows = {
      euler: [null, num(pose.roll, 2), num(pose.pitch, 2), num(pose.yaw, 2)],
      gyr: [null, num(imu.gx, 3), num(imu.gy, 3), num(imu.gz, 3)],
      acc: [null, num(imu.ax, 3), num(imu.ay, 3), num(imu.az, 3)],
      quat: quat ? quat.map((q) => num(q, 4)) : ['--', '--', '--', '--'],
    };
    document.querySelectorAll('#rawTable tbody tr').forEach((tr) => {
      rows[tr.dataset.k].forEach((v, i) => { if (v !== null) setText(tr.cells[i + 1], v); });
    });
  }
  function refresh() {
    const panel = $('advPanel');
    if (!panel.open || window.GyroUI?.view !== 'settings') return;
    const [st, sc] = streamInfo(); setText($('stStream'), st); setClass($('stStream'), sc);
    const [ct, cc] = canInfo(); setText($('stCan'), ct); setClass($('stCan'), cc);
    setText($('stFusion'), fusionHz ? String(fusionHz) : '--');
    const out = deviceConfig?.outHz ?? Number(($('modeLabel').textContent.match(/输出：(\d+)/) || [])[1]);
    setText($('stOut'), Number.isFinite(out) && out ? String(out) : '--');
    renderFlags(); renderStartup(); renderCounters(); renderRaw();
  }
  setInterval(refresh, 200);
  $('advPanel').addEventListener('toggle', refresh);
  // bias1：运动零偏只在「高级设置」展开且位于设置页时 2 Hz 静默刷新（门控同温度轮询）；展开时先读一次
  const biasVisible = () => $('advPanel').open && window.GyroUI?.view === 'settings';
  let biasWas = false;
  setInterval(() => { const v = biasVisible(); motionBias?.setAuto(v); if (v && !biasWas && running) void motionBias?.query(); biasWas = v; }, 200);
  window.GyroUI.adv = { refresh };
})();
