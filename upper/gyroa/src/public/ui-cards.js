// Presentation-only enhancement: turn existing live text targets into
// labelled, themed notice cards without replacing their IDs or update flow.
(() => {
  'use strict';
  const groups = {
    status: [
      ['qsMsg','快捷设置反馈'],['qsEmpty','快捷设置提示','warn']
    ],
    output: [
      ['outputSummary0','UART 配置'],['outputSummary1','USB 配置'],
      ['rateState','输出频率'],['canFrequencyHint','CAN 频率提示'],
      ['canConfigSummary','当前 CAN 配置'],['canSavedSummary','已保存的 CAN 配置'],
      ['canHint','兼容性提示','warn']
    ],
    calib: [
      ['accStatus','六面校准状态'],['accAdvice','放置建议','warn'],
      ['accPlacement','放置方向'],['accParams','校准参数'],
      ['magCalMode','融合模式提示','warn'],['magCalMessage','磁力计校准状态'],
      ['magCalQuality','校准质量'],['magCalParams','拟合结果'],
      ['vqfState','静置初始化状态'],['vqfMsg','静置初始化反馈'],
      ['vqfLive','实时诊断'],['bhCorrupt','历史数据警告','warn'],
      ['bhUsedNote','本次启动零偏'],['bhMsg','历史读取状态'],
      ['startupBiasSource','零偏来源']
    ],
    tuning: [
      ['vqfTuningMessage','VQF 参数反馈'],
      ['gyroScaleMessage','比例因子反馈']
    ],
    test: [
      ['driftConfig','当前测试配置'],['driftHoldWarning','零角速保持提示','warn'],
      ['driftNineWarning','九轴模式提示','warn'],
      ['driftMessage','测试状态'],['insMessage','惯导估计状态'],
      ['magChartMessage','磁场采样状态']
    ],
    capture: [
      ['captureMessage','采集状态']
    ],
    upgrade: [
      ['firmwareStatus','升级进度'],
      ['firmwareInfo','固件文件信息'],
      ['fwResult','升级结果']
    ],
    settings: [
      ['algorithmMessage','算法设置反馈'],
      ['restBiasMessage','静置零偏反馈'],
      ['magRejectionMessage','磁干扰拒绝反馈'],
      ['ws2812Message','指示灯反馈'],
      ['filterHint','滤波档位说明'],['filterMsg','姿态稳定性反馈'],
      ['zaruNote','阈值说明'],['zaruMsg','阈值保存反馈'],
      ['restartHint','设备重启提示','warn'],
      ['accRangeHint','加速度量程说明'],
      ['gyroInitHint','启动窗口说明'],
      ['biasFlags','运动零偏状态'],['biasVec','运动零偏数据'],
      ['biasNote','使用限制'],['biasMsg','运动零偏读取']
    ]
  };

  const noteTone = (message, hint) => {
    const t = (message || '').trim();
    if (/失败|出错|错误|超时|损坏|断开|中断|无效|拒绝/.test(t)) return 'error';
    if (/成功|已完成|已应用|已校准|已保存|准备就绪/.test(t)) return 'good';
    if (/不支持|请升级|需升级|风险|警告|注意|限制|未完成|不合格/.test(t)) return 'warn';
    return hint || 'info';
  };

  const decorate = (id, label, hint) => {
    const node = document.getElementById(id);
    // Avoid invalid parent changes in table/grid controls and duplicate wrappers.
    if (!node || node.closest('.legacy-state') || node.closest('.ui-notice')) return;
    const holder = document.createElement('div');
    holder.className = 'ui-notice';
    holder.dataset.tone = noteTone(node.textContent, hint);
    if (node.matches('small,span') || node.closest('.inline-form,.kv,.actions,.qs-h')) {
      holder.classList.add('ui-notice--compact');
    }
    const body = document.createElement('div');
    body.className = 'ui-notice__body';
    const heading = document.createElement('span');
    heading.className = 'ui-notice__title';
    heading.textContent = label;
    node.before(holder);
    holder.append(body);
    body.append(heading, node);
    node.classList.add('ui-notice__text');
    // Keep dynamic validation and async protocol feedback semantically accurate.
    // Only visual metadata changes; the original status node remains untouched.
    let tone = holder.dataset.tone;
    const update = () => {
      const next = noteTone(node.textContent, hint);
      if (tone !== next) { tone = next; holder.dataset.tone = next; }
    };
    new MutationObserver(update).observe(node, {
      childList: true, characterData: true, subtree: true
    });
  };
  Object.values(groups).forEach(entries => entries.forEach(entry => decorate(...entry)));
})();
