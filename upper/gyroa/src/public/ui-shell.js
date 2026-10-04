// AT32 AHRS · app1 UI shell: navigation, toasts, connection status mirror and the automatic
// settings-mode wrapper. It only calls existing app.js functions (enterSettings / send / CMD) and reads
// app.js state; protocol, parsing and command structure stay in app.js.
(() => {
  const $ = (id) => document.getElementById(id);
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  async function until(fn, timeout = 3000, step = 50) {
    const t0 = performance.now();
    for (;;) { if (fn()) return true; if (performance.now() - t0 > timeout) return false; await sleep(step); }
  }

  // ---------------- navigation ----------------
  // app3: CAN 并入「输出」页（UART / USB / CAN 分段）；旧链接 #/can 进入输出页的 CAN 分段
  const VIEWS = ['status', 'output', 'calib', 'upgrade', 'settings'];
  let currentView = 'status';
  function route() {
    const raw = (location.hash.match(/^#\/(\w+)/) || [])[1];
    const name = raw === 'can' ? 'output' : raw;
    const view = VIEWS.includes(name) ? name : 'status';
    const changed = currentView !== view;
    currentView = view;
    document.querySelectorAll('.view').forEach((v) => {
      v.hidden = v.dataset.view !== view;
      if (v.hidden) v.classList.remove('view-enter');
      else if (changed && routed) { v.classList.remove('view-enter'); void v.offsetWidth; v.classList.add('view-enter'); }
    });
    routed = true;
    document.querySelectorAll('.nav-item').forEach((a) => {
      const on = a.dataset.nav === view; a.classList.toggle('active', on);
      if (on) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
    });
    document.body.dataset.view = view;
    if (view === 'status' && typeof scheduleDraw === 'function') scheduleDraw();
    window.GyroUI?.pages?.syncTab?.();
  }
  let routed = false;
  document.addEventListener('animationend', (e) => { if (e.animationName === 'view-in') e.target.classList.remove('view-enter'); });
  window.addEventListener('hashchange', route);
  route();

  // ---------------- toasts ----------------
  const recent = new Map();
  function toast(text, kind = 'ok', ms = 2400) {
    const now = performance.now();
    if (recent.get(text) > now - 1500) return;
    recent.set(text, now);
    const box = $('toasts'); if (!box) return;
    while (box.children.length >= 3) box.firstElementChild.remove();
    const node = document.createElement('div');
    node.className = `toast ${kind}`; node.setAttribute('role', kind === 'error' ? 'alert' : 'status');
    node.textContent = text; box.append(node);
    setTimeout(() => { node.classList.add('out'); setTimeout(() => node.remove(), 260); }, ms);
  }

  // app.js reports every result through say() → #message (and the log). Map them to short toasts;
  // the full technical text stays in the communication log.
  let connectFailed = false;
  const short = (text) => text.split(/[（(：:，,；;]/)[0].trim().slice(0, 24);
  const RULES = [
    [/设置模式|^状态：|等待|^串口已连接，等待数据|^原串口已自动重连，等待|^数据已恢复|解析器已同步|^已选择恢复端口|^固件文件检查通过/, null],
    [/^请先连接|请先连接串口/, () => ['请先连接设备', 'warn']],
    [/^连接失败/, () => { connectFailed = true; return ['连接失败', 'error']; }],
    [/不支持 Web Serial/, () => ['浏览器不支持 Web Serial', 'error']],
    [/^串口已连接，(姿态数据正常|已收到数据)/, () => ['✓ 已连接', 'ok']],
    [/^串口已连接，但尚未收到数据/, () => ['等待设备数据…', 'warn']],
    [/^数据超时/, () => ['数据超时', 'warn']],
    [/重连超时|设备未响应/, () => ['重连失败', 'error']],
    [/已自动重连/, () => ['✓ 已重新连接', 'ok']],
    [/^串口已断开/, (t) => [/拔出/.test(t) ? '设备已拔出' : '已断开', 'warn']],
    [/六面标定完成并已保存/, () => ['✓ 校准完成', 'ok']],
    [/六面标定已开始/, () => ['校准已开始', 'ok']],
    [/已取消标定|已接受取消请求/, () => ['已取消校准', 'warn']],
    [/^静置初始化成功/, () => ['✓ 静置初始化成功', 'ok']],
    [/^已开始：请保持设备静止/, () => ['静置初始化已开始', 'ok']],
    [/^已取消：未写 Flash/, () => ['已取消静置初始化', 'warn']],
    [/^已恢复默认 VQF 参数/, () => ['✓ 已恢复默认 VQF 参数', 'ok']],
    [/^静置初始化失败/, () => ['静置初始化失败', 'error']],
    [/^PONG/, () => (firmwareRestoring ? [null] : ['设备在线', 'ok'])],
    [/固件升级完成/, () => ['✓ 升级完成', 'ok']],
    [/标定失败|校准失败|未收到校准开始确认/, (t) => [/已在进行中/.test(t) ? '校准已在进行中' : '校准失败', 'error']],
    [/配置失败|保存失败|设置失败/, (t) => [/^(融合模式|启动设置|CAN|输出|输出频率|CAN ID|CAN 节点)/.test(t) ? '保存失败' : short(t), 'error']],
    [/失败|无效|超出|须为|须在|须能|未确认|未收到|不支持|暂未实现|请选择|请先|范围为/, (t) => [short(t), 'error']],
    [/仅本次运行/, () => ['✓ 已应用', 'ok']],
    [/已保存|已应用|已切换|已设为/, () => ['✓ 已保存', 'ok']],
  ];
  function onMessage(text) {
    for (const [re, fn] of RULES) {
      if (!re.test(text)) continue;
      if (fn) { const [msg, kind] = fn(text); if (msg) toast(msg, kind); }
      return;
    }
  }
  new MutationObserver(() => onMessage($('message').textContent || '')).observe($('message'), { childList: true, characterData: true, subtree: true });

  // ---------------- automatic settings mode ----------------
  // Firmware only accepts STARTUP / CAN_CONFIG / CAN_ID / ACC_6FACE inside settings mode. The visible save
  // buttons stay one-click: enter (existing enterSettings: EXIT probe + ENTER), run the original handler, exit.
  const GATED = { applyBtn: 'startup', canApply: 'can', acc6Btn: 'acc', canIdBtn: 'canId', filterApply: 'filter', zaruSave: 'zaru', zaruRestore: 'zaruRestore', vqfStart: 'vqfStart', vqfRestore: 'vqfRestore' };
  // bias1：表单不合法时不进设置模式，直接调用原处理函数（只提示、不发送任何帧）
  const PRECHECK = { startup: () => !startupValidationError(),
    zaru: () => !!globalThis.GyroZaru && GyroZaru.parseForm(zaruLimits?.state.draft).ok,
    // restore1：「恢复默认」第一次点击只上膛（不进设置模式、不发送），确认点击才走 0x17 → 0x31 → 0x18
    zaruRestore: () => !!zaruLimits?.state.restoreArmed,
    // vqfinit1：开始（0x2A）/ 恢复默认（0x2D）需设置模式；恢复默认第一次点击只上膛
    vqfStart: () => !!vqfInit && vqfInit.state.supported === true && !vqfInit.busy(),
    vqfRestore: () => !!vqfInit?.state.restoreArmed };
  const gate = { busy: null, passthrough: false, entered: false };
  const accBusy = () => accStartPending || accCalibration?.status === 1;
  async function waitDone(kind) {
    if (kind === 'startup') { await until(() => !pendingStartup, 4000); await sleep(700); return; }
    if (kind === 'can') { await until(() => !pendingCan, 4000); return; }
    if (kind === 'canId') { await sleep(800); return; }
    // 姿态稳定性：等 ACK + 0x26 回读核对结束（filter-profile.js 状态机），随后退出设置模式；不重启
    if (kind === 'filter') { await until(() => !window.__gyro?.filter?.state.pending, 4000); return; }
    // 零角速保持阈值：等 ACK + 0x0F 回读核对（zaru-limits.js，3 s 超时 → 未确认）结束，随后退出设置模式
    if (kind === 'zaru' || kind === 'zaruRestore') { await until(() => !zaruLimits?.state.pending, 4000); return; }
    // vqfinit1：开始只等 ACK + 首帧 0x0D（固件 EXIT_SETTINGS 不取消采集，采集期间不占设置模式）；恢复默认等 ACK + 0x0E
    if (kind === 'vqfStart' || kind === 'vqfRestore') { await until(() => !vqfInit?.state.pending, 4000); return; }
    if (kind === 'acc') {
      await until(() => !accStartPending, 4000);
      while (running && accBusy()) await sleep(500);
    }
  }
  async function runGated(btn, kind) {
    if (!running) { toast('请先连接设备', 'warn'); return; }
    gate.busy = kind; refresh();
    try {
      await enterSettings();
      if (!await until(() => setting || !running, 2500) || !running) { toast('设备未进入设置状态', 'error'); return; }
      gate.entered = true;
      // call the original app.js handler directly (the button itself is shown as busy/disabled meanwhile)
      gate.passthrough = true;
      try { await btn.onclick?.call(btn, new MouseEvent('click')); } finally { gate.passthrough = false; }
      await waitDone(kind);
      if (running && setting) await send(CMD.EXIT);
    } finally { gate.busy = null; gate.entered = false; refresh(); }
  }
  // vqfinit1：静置采集期间（state 1–4）设备对这些命令回 ACK 0x03 / 0x0703 —— 前端直接锁住，不发送；查询照常
  const VQF_LOCK = ['applyBtn', 'acc6Btn', 'outputApplyAll', 'rateApplyBtn', 'filterApply', 'zaruSave', 'zaruRestore', 'vqfRestore'];
  const VQF_LOCK_SEL = VQF_LOCK.map((id) => `#${id}`).concat('#qsFusion button', '#qsFilter button').join(',');
  const vqfLocked = () => !!vqfInit?.busy?.();
  document.addEventListener('click', (event) => {
    if (!vqfLocked()) return;
    const btn = event.target.closest?.(VQF_LOCK_SEL);
    if (!btn || gate.passthrough) return;
    event.preventDefault(); event.stopImmediatePropagation();
    toast('静置初始化进行中，请先等待结束或取消', 'warn');
  }, true);
  function syncVqfLock() {
    const on = vqfLocked();
    document.body.classList.toggle('vqf-busy', on);
    document.querySelectorAll(VQF_LOCK_SEL).forEach((b) => {
      if (on) { if (!b.hasAttribute('data-vqf-lock')) { b.setAttribute('data-vqf-lock', ''); b.setAttribute('aria-disabled', 'true'); if (!b.dataset.vqfTitle) b.dataset.vqfTitle = b.title || ''; b.title = '静置初始化进行中（设备会回 0x0703）'; } }
      else if (b.hasAttribute('data-vqf-lock')) { b.removeAttribute('data-vqf-lock'); b.removeAttribute('aria-disabled'); b.title = b.dataset.vqfTitle || ''; delete b.dataset.vqfTitle; }
    });
  }
  document.addEventListener('click', (event) => {
    const btn = event.target.closest?.('button');
    if (!btn || !GATED[btn.id] || gate.passthrough || btn.disabled) return;
    if (gate.busy) { event.preventDefault(); event.stopImmediatePropagation(); return; }
    if (setting) return; // already inside settings mode
    event.preventDefault(); event.stopImmediatePropagation();
    if (PRECHECK[GATED[btn.id]] && !PRECHECK[GATED[btn.id]]()) { void btn.onclick?.call(btn, new MouseEvent('click')); return; }
    void runGated(btn, GATED[btn.id]);
  }, true);

  // ---------------- status mirroring ----------------
  const labels = new Map();
  function label(btn, busyText) {
    if (!btn) return;
    const node = btn.querySelector('.lbl') || btn;
    // restore1：只在“忙”与“刚结束忙”时改文字，空闲时不覆盖模块自己的标签（如「确认恢复」）
    if (busyText) { if (!labels.has(btn)) labels.set(btn, node.textContent); if (node.textContent !== busyText) node.textContent = busyText; return; }
    if (labels.has(btn)) { const text = labels.get(btn); labels.delete(btn); if (node.textContent !== text) node.textContent = text; }
  }
  const lockedByShell = new Set();
  function busyButton(id, busyText, isBusy) {
    const btn = $(id); if (!btn) return;
    label(btn, isBusy ? busyText : null);
    btn.classList.toggle('is-busy', !!isBusy);
    if (isBusy && !btn.disabled) { btn.disabled = true; lockedByShell.add(btn); }
    else if (!isBusy && lockedByShell.has(btn)) {
      // hand the enabled state back to app.js' own rules
      lockedByShell.delete(btn); btn.disabled = false;
      if (typeof updateSettingUI === 'function') updateSettingUI();
    }
  }
  function refresh() {
    const connected = !!running;
    if (connected || connecting) connectFailed = false;
    const reconnecting = !!reconnectPlan;
    let state = 'off', text = '未连接';
    if (connecting) { state = 'busy'; text = '连接中…'; }
    else if (connected) {
      state = 'on'; text = '已连接';
      if (deviceConfig) text = `${deviceConfig.source ? 'USB' : 'UART'} · 已连接`;
    } else if (reconnecting) { state = 'busy'; text = '等待设备…'; }
    else if (connectFailed) { state = 'fail'; text = '连接失败'; }
    if (firmwareBusy) { state = 'busy'; text = '升级中…'; }
    const chip = $('connChip');
    if (chip.dataset.state !== state) chip.dataset.state = state;
    if ($('connStatus').textContent !== text) $('connStatus').textContent = text;
    const showDisconnect = connected || reconnecting || (firmwareBusy && !!port);
    $('connectBtn').hidden = showDisconnect;
    $('disconnectBtn').hidden = !showDisconnect;
    label($('connectBtn'), connecting ? '连接中…' : null);
    document.body.classList.toggle('is-connected', connected);
    busyButton('applyBtn', '保存中…', !!pendingStartup || gate.busy === 'startup');
    busyButton('canApply', '保存中…', !!pendingCan || gate.busy === 'can');
    busyButton('canIdBtn', '保存中…', gate.busy === 'canId');
    busyButton('filterApply', '应用中…', !!window.__gyro?.filter?.state.pending || gate.busy === 'filter');
    busyButton('zaruSave', '保存中…', (!!zaruLimits?.state.pending && zaruLimits.state.pending.kind !== 'restore') || gate.busy === 'zaru');
    busyButton('zaruRestore', '恢复中…', zaruLimits?.state.pending?.kind === 'restore' || gate.busy === 'zaruRestore');
    busyButton('acc6Btn', '启动中…', gate.busy === 'acc' && !accBusy());
    busyButton('vqfStart', '启动中…', vqfInit?.state.pending?.kind === 'start' || gate.busy === 'vqfStart');
    busyButton('vqfRestore', '恢复中…', vqfInit?.state.pending?.kind === 'restore' || gate.busy === 'vqfRestore');
    syncVqfLock();
    busyButton('firmwareStartBtn', '升级中…', !!firmwareBusy);
    window.GyroUI?.pages?.refresh?.();
  }
  setInterval(refresh, 150);

  window.GyroUI = { toast, until, sleep, refresh, route, gate, vqfChanged: () => syncVqfLock(), get view() { return currentView; } };
  refresh();
})();
