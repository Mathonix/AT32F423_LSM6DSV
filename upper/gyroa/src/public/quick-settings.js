// AT32 AHRS · gyro3 quick1/quick2：首页「快捷设置」。只复用已验证的现有路径，不自建协议帧：
//  · 融合模式：填好设置页启动表单（其余字段 = 设备已保存值）+ 勾选「保存后重启」→ 点击原 #applyBtn
//    → ui-shell 自动设置模式门（EXIT 探测 + ENTER）→ applyMode() 发 0x1e STARTUP[mode,fast,1,…] → ACK → 设备重启
//    → 原串口自动重连 → 0x1f 回读 activeMode 核对。旧固件（无 0x07 CONFIG）隐藏此项。
//  · 姿态稳定性：filterProfile.setDraft(p, persist=true) → 点击原 #filterApply → 同一设置模式门 → 0x27[p,1] → ACK → 0x26 回读核对。
(() => {
  const $ = (id) => document.getElementById(id);
  const { toast, gate } = window.GyroUI;
  const FUSION = ['六轴', '九轴'];
  const FUSION_TIMEOUT_MS = 95000; // 重连计划本身 90 s 截止
  const q = { fusion: null, filter: null, msg: '' }; // fusion: { target, phase:'saving'|'restarting', t0, restartNow }
  const setText = (n, t) => { if (n && n.textContent !== t) n.textContent = t; };
  const setProp = (n, k, v) => { if (n && n[k] !== v) n[k] = v; };
  const say2 = (text, kind) => { q.msg = text; if (kind) toast(text, kind); };
  const deviceIdle = () => running && !connecting && !firmwareBusy && !firmwareRestoring && !reconnectPlan && !gate.busy &&
    !pendingStartup && !pendingOutput && !pendingCan && !pendingRate && !pendingImmediateReset && !accStartPending && !filterProfile?.state.pending;
  const oldFirmware = () => running && !deviceConfig && performance.now() - connectedAt > 1500;

  function fusionBlocked(mode) {
    if (q.fusion || q.filter) return '请等待当前设置完成';
    if (!deviceIdle()) return running ? '设备忙，请稍候' : '请先连接设备';
    if (!deviceConfig) return '当前固件不支持';
    if (startupFormDirty) return '设置页有未保存的启动修改';
    if (mode === 1 && !(deviceConfig.capabilities & 1)) return '当前设备不支持九轴';
    return '';
  }
  function setFusion(mode) {
    if (deviceConfig && deviceConfig.activeMode === mode && deviceConfig.savedMode === mode) return;
    const why = fusionBlocked(mode); if (why) { say2(why, 'warn'); render(); return; }
    const radio = document.querySelector(`input[name="fusion"][value="${mode}"]`);
    if (!radio || $('applyBtn').disabled) { say2('设备忙，请稍候', 'warn'); return; }
    fillStartupForm(deviceConfig);               // 快速启动 / 零偏时长 / 量程 = 设备已保存值
    radio.checked = true;
    q.fusion = { target: mode, phase: 'saving', t0: performance.now(), restartNow: $('restartNow').checked };
    $('restartNow').checked = true;
    say2(`正在切换${FUSION[mode]}，设备将重启…`);
    render();
    $('applyBtn').click();                       // capture 监听器进入自动设置模式后调用原 applyMode()
  }
  function fusionTick() {
    const f = q.fusion; if (!f) return;
    if (f.phase === 'saving') {
      if (reconnectPlan && reconnectPlan.phase !== 'armed') f.phase = 'restarting';
      else if (!gate.busy && !pendingStartup && !reconnectPlan) return fusionDone(false, '融合模式切换失败，未重启');
      return;
    }
    if (running && deviceConfig && !reconnectPlan) {
      return deviceConfig.activeMode === f.target
        ? fusionDone(true, `已切换为${FUSION[f.target]}（已重启）`)
        : fusionDone(false, `回读不一致：当前${fusionModeName(deviceConfig.activeMode)}`);
    }
    if (!reconnectPlan && !running && !connecting) return fusionDone(false, '设备未重连，请重新连接后确认');
    if (performance.now() - f.t0 > FUSION_TIMEOUT_MS) fusionDone(false, '未确认融合模式回读');
  }
  function fusionDone(ok, text) {
    const f = q.fusion; q.fusion = null;
    $('restartNow').checked = f.restartNow;
    if (!ok && deviceConfig && !pendingStartup) { startupFormDirty = false; startupFormRevision++; fillStartupForm(deviceConfig); updateSettingUI(); }
    say2(text, ok ? 'ok' : 'error');
  }

  function setFilter(profile) {
    const s = filterProfile?.state; if (!s) return;
    if (s.config && s.config.active === profile && s.config.saved === profile) return;
    if (q.fusion || q.filter) { say2('请等待当前设置完成', 'warn'); return; }
    if (!deviceIdle()) { say2(running ? '设备忙，请稍候' : '请先连接设备', 'warn'); return; }
    if (s.supported !== true || !filterProfile.available(profile)) { say2('当前固件不支持', 'warn'); return; }
    filterProfile.setDraft(profile, true);       // 重启后保留（persist=1）
    if ($('filterApply').disabled) { say2('设备忙，请稍候', 'warn'); return; }
    q.filter = { target: profile, t0: performance.now(), sawPending: false };
    say2(`正在切换${GyroFilter.profileName(profile)}…`);
    render();
    $('filterApply').click();                    // 自动设置模式 → 0x27[p,1] → ACK → 0x26 回读 → 退出
  }
  function filterTick() {
    const f = q.filter; if (!f) return;
    const s = filterProfile.state;
    if (s.pending) f.sawPending = true;
    if (gate.busy || s.pending) { if (performance.now() - f.t0 < 10000) return; }
    if (!f.sawPending && performance.now() - f.t0 < 600 && !gate.busy) return; // 门尚未接手
    q.filter = null;
    const ok = !!s.result?.ok && s.config?.active === f.target && s.config?.saved === f.target;
    say2(ok ? `已切换为${GyroFilter.profileName(f.target)}（重启后保留）` : (s.result?.text || '姿态稳定性切换失败'), ok ? 'ok' : 'error');
  }

  function render() {
    const card = $('quickSet'); if (!card) return;
    const connected = !!running && !firmwareBusy;
    const busy = !!(q.fusion || q.filter) || !!gate.busy || !!reconnectPlan;
    const fRow = $('qsFusionRow'), pRow = $('qsFilterRow');
    setProp(fRow, 'hidden', oldFirmware());
    const active = deviceConfig?.activeMode;
    document.querySelectorAll('#qsFusion button').forEach((b) => {
      const m = Number(b.dataset.mode), target = q.fusion?.target;
      b.classList.toggle('on', target !== undefined ? m === target : m === active);
      setProp(b, 'disabled', !connected || busy || !deviceConfig || (m === 1 && !(deviceConfig.capabilities & 1)));
    });
    setProp($('qsFusionNote'), 'title', active === 2 ? '切换即重启（当前：九轴相对角）' : '切换即重启'); // quick2：无小字，仅提示
    const s = filterProfile?.state;
    setProp(pRow, 'hidden', s?.supported === false);
    document.querySelectorAll('#qsFilter button').forEach((b) => {
      const p = Number(b.dataset.profile), target = q.filter?.target;
      setProp(b, 'hidden', !filterProfile?.available(p));
      b.classList.toggle('on', target !== undefined ? p === target : p === s?.config?.active);
      setProp(b, 'disabled', !connected || busy || s?.supported !== true || !filterProfile.available(p));
    });
    card.dataset.busy = String(busy);
    if (!running && !reconnectPlan && !q.fusion) q.msg = '';
    setText($('qsMsg'), q.msg);
    setProp($('qsEmpty'), 'hidden', !(fRow.hidden && pRow.hidden));
  }
  $('qsFusion').addEventListener('click', (e) => { const b = e.target.closest('button[data-mode]'); if (b && !b.disabled) setFusion(Number(b.dataset.mode)); });
  $('qsFilter').addEventListener('click', (e) => { const b = e.target.closest('button[data-profile]'); if (b && !b.disabled) setFilter(Number(b.dataset.profile)); });
  setInterval(() => { fusionTick(); filterTick(); render(); }, 150);
  render();
  window.GyroUI.quick = { state: q, setFusion, setFilter, render };
})();
