(() => {
  const $=id=>document.getElementById(id),control=GyroMagCal.control;
  if(!control)return;
  let armed=false,lastCount=-1,lastPhase='',lastCoverageAt=-Infinity,cov={bins:0,total:32,percent:0};
  const download=(name,data)=>{const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
  $('magCalRead').onclick=()=>control.read();
  $('magCalStart').onclick=()=>{armed=false;control.start();};
  $('magCalStop').onclick=()=>control.stop();
  $('magCalCancel').onclick=()=>control.stop(true);
  $('magCalVerify').onclick=()=>control.start(true);
  $('magCalSave').onclick=()=>{armed=false;control.save();};
  $('magCalRestore').onclick=()=>{if(!armed){armed=true;render();return;}armed=false;control.restore();};
  $('magCalExport').onclick=()=>download('mag-calibration.json',{format:'AT32-mag-cal-v1',note:'原始IST坐标；安装映射沿用固件，不自动校准安装轴',saved:control.state.data,candidate:control.state.candidate,validationQuality:control.state.quality,fitDiagnostics:control.state.fitDiagnostics,samples:control.state.samples,validation:control.state.validation});
  // Keep conflicting device mutations out of an active acquisition/transaction.
  const MUTATING='#applyBtn,#outputApplyAll,#rateApplyBtn,#canApply,#canIdBtn,#filterApply,#zaruSave,#zaruRestore,#vqfStart,#vqfRestore,#acc6Btn,#gyroCalBtn,#zeroBtn,#resetBtn,#streamApplyBtn,#firmwareStartBtn,#qsFusion button,#qsFilter button,#enterBtn,#exitBtn,#statusRestartBtn,#testRestartBtn,#insStart,#driftStart,#ws2812Enable,#magRejectionEnable';
  document.addEventListener('click',e=>{if(control.busy() && e.target.closest?.(MUTATING)){e.preventDefault();e.stopImmediatePropagation();GyroUI?.toast('请先结束磁校准操作','warn');}},true);
  window.addEventListener('beforeunload',e=>{if(control.busy()){e.preventDefault();e.returnValue='';}});
  function render() {
    const s=control.state,c=magCalContext(),active=['collect','validate'].includes(s.phase),available=c.connected && !c.busy && s.supported===true;
    const samples=s.phase==='validate'?s.validation:s.samples;
    if((samples.length!==lastCount && performance.now()-lastCoverageAt>=250) || s.phase!==lastPhase){cov=s.phase!=='validate' && s.candidate?s.candidate.coverage:GyroMagCal.coverage(samples,s.candidate?.cal);lastCount=samples.length;lastPhase=s.phase;lastCoverageAt=performance.now();}
    if(!c.connected)armed=false;
    $('magCalSource').textContent=s.data?.valid?'已保存校准':s.supported===false?'需升级 APP':'试用系数';
    $('magCalMessage').textContent=!c.connected?'请先连接设备':c.mode===undefined?'正在等待设备配置，请刷新设备状态':![1,2].includes(c.mode)?'请切换到九轴模式后采集':s.message;
    $('magCalCount').textContent=`${samples.length} 样本 · ${cov.bins}/32 方向`;
    $('magCalProgress').value=cov.percent;
    $('magCalStart').disabled=!available || control.busy() || ![1,2].includes(c.mode);
    $('magCalStart').textContent=s.samples.length?'重新采集':'开始采集';
    $('magCalRead').disabled=!c.connected || c.busy || control.busy();
    $('magCalStop').hidden=$('magCalCancel').hidden=!active;
    $('magCalStop').textContent=s.phase==='validate'?'完成验证':'结束并拟合';
    $('magCalStop').disabled=s.phase==='collect'?(samples.length<600 || cov.bins<24):samples.length<300;
    $('magCalVerify').disabled=!available || control.busy() || !s.candidate;
    $('magCalSave').disabled=!available || control.busy() || !s.verified;
    $('magCalRestore').disabled=!available || control.busy();$('magCalRestore').textContent=armed?'确认恢复试用系数':'恢复试用系数';
    $('magCalExport').disabled=control.busy() || !s.samples.length && !s.data;
    const q=s.quality || s.candidate?.quality,d=s.fitDiagnostics;
    const rejected=d?.rejectedSamples && s.phase!=='validate'?` · 异常 ${d.rejectedSamples}/${d.totalSamples}（${(d.rejectedFraction*100).toFixed(1)}%）`:'';
    $('magCalQuality').textContent=q?`磁场 ${q.radius.toFixed(2)} µT · 模长 RMS ${(q.rms*100).toFixed(2)}% · P95 ${(q.p95*100).toFixed(2)}%${q.dipP95!==null && q.dipP95!==undefined?` · 倾角 P95 ${q.dipP95.toFixed(2)}°`:''}${rejected}`:'采集 → 拟合 → 新数据验证 → 保存';
    $('magCalParams').textContent=s.candidate?`候选偏置 / µT：${s.candidate.cal.offset.map(x=>x.toFixed(3)).join(' / ')}`:s.data?`已用偏置 / µT：${s.data.cal.offset.map(x=>x.toFixed(3)).join(' / ')}`:'';
    $('magCalMode').hidden=![0].includes(c.mode);
  }
  GyroMagCal.render=render;
  setInterval(()=>{control.tick();if(running && !control.state.queried)control.read();render();},50);render();
})();
