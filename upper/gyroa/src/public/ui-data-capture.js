(() => {
  'use strict';
  const $=id=>document.getElementById(id),api=GyroCapture,session=api.session,control=api.control,io=GyroCaptureIO;
  let signature=null,leaseAt=0,lastResult=null,wakeLock=null,lastDraw=0;
  const size=n=>n<1024?`${n} B`:n<1024**2?`${(n/1024).toFixed(1)} KB`:`${(n/1024**2).toFixed(2)} MB`;
  const signatureOf=c=>JSON.stringify([c.metadata.firmware,c.metadata.mode,c.metadata.algorithm,c.metadata.rangeDps,c.metadata.accRangeG,c.metadata.profile,c.metadata.restBiasEnabled,c.metadata.motionBiasEnabled,c.metadata.vqfSettings,c.metadata.zaruSettings,c.metadata.gyroScale,c.metadata.vqfTuning]);
  async function awake(){if(!session.state().active||!navigator.wakeLock||document.visibilityState!=='visible'||wakeLock)return;try{wakeLock=await navigator.wakeLock.request('screen');wakeLock.addEventListener('release',()=>wakeLock=null);}catch{}}
  function finish(reason){const s=session.state();session.stop(reason);if(io.context().connected&&!control.state.pending)control.stop(s.hz,s.token);void wakeLock?.release();wakeLock=null;updateSettingUI();render();}
  api.onDisconnect=()=>{session.stop('连接已断开，已保留采集数据');control.reset();void wakeLock?.release();wakeLock=null;};
  api.onData=m=>{const was=session.state().active;session.ingest(m);if(was&&!session.state().active)finish(session.state().reason);};
  api.onDropped=()=>session.dropped();
  $('captureStart').onclick=()=>{
    const c=io.context(),hz=Number($('captureHz').value);if(!c.connected||c.busy||!c.ready||control.state.supported!==true||session.state().rows)return;
    const bytes=new Uint32Array(1);crypto.getRandomValues(bytes);const token=bytes[0]||1;
    signature=signatureOf(c);leaseAt=performance.now();session.start({hz,token,metadata:c.metadata});
    if(!control.start(hz,token))session.stop('采集请求未发送，请稍后重试');else void awake();updateSettingUI();render();
  };
  $('captureStop').onclick=()=>{if(session.state().active)finish('采集已停止，可导出 CSV');else if(control.state.config?.active)control.stop(control.state.config.hz,control.state.config.token);};
  $('captureRead').onclick=()=>control.query();
  $('captureClear').onclick=()=>{if(api.busy())return;session.clear();lastResult=control.state.result;render();draw();};
  $('captureCsv').onclick=()=>{
    const s=session.state();if(!s.rows||api.busy())return;
    const blob=new Blob(session.parts(),{type:'text/csv;charset=utf-8'}),url=URL.createObjectURL(blob),a=document.createElement('a');
    a.href=url;a.download=`imu_${new Date(s.firstWall).toISOString().replace(/[:.]/g,'-')}_${s.hz}Hz.csv`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
  };
  // Keep configuration stable across all views while retaining read-only queries.
  const mutating='#gyroScalePrepare,#gyroScaleApply,#gyroScaleRestore,#insStart,#driftStart,#vqfTuningApply,#vqfTuningRestore,#magCalStart,#magCalVerify,#magCalSave,#magCalRestore,#ws2812Enable,#magRejectionEnable,#restBiasEnable,#algorithmApply,#statusRestartBtn,#testRestartBtn,#applyBtn,#outputApplyAll,#outputApply0,#outputApply1,#rateApplyBtn,#canApply,#canIdBtn,#filterApply,#zaruSave,#zaruRestore,#vqfStart,#vqfRestore,#acc6Btn,#gyroCalBtn,#zeroBtn,#resetBtn,#streamApplyBtn,#firmwareStartBtn,#qsFusion button,#qsFilter button,#enterBtn,#exitBtn';
  document.addEventListener('click',e=>{if(api.busy()&&e.target.closest?.(mutating)){e.preventDefault();e.stopImmediatePropagation();GyroUI.toast('请先停止数据采集，再修改设备','warn');}},true);
  window.addEventListener('beforeunload',e=>{if(session.state().active){e.preventDefault();e.returnValue='';}});
  document.addEventListener('visibilitychange',()=>{if(document.visibilityState==='visible')void awake();});
  function render(){
    const c=io.context(),s=session.state(),v=control.state,now=performance.now();
    if(v.result!==lastResult){lastResult=v.result;if(lastResult&&!lastResult.ok&&lastResult.kind!=='query'&&s.active)session.stop(v.message);}
    if(s.active&&signatureOf(c)!==signature)session.stop('设备配置发生变化，已保留变化前的数据');
    const was=s.active;session.tick(c.connected);if(was&&!s.active&&!v.pending&&c.connected)control.stop(s.hz,s.token);
    if(!s.active&&v.config?.active&&v.config.token===s.token&&!v.pending&&v.result?.kind!=='stop'&&c.connected)control.stop(s.hz,s.token);
    if(s.active&&!v.pending&&now-leaseAt>=4000){leaseAt=now;control.renew(s.hz,s.token);}
    const busy=api.busy();$('captureStart').disabled=busy||s.rows>0||!c.ready||c.busy||v.supported!==true||!!v.pending||!!v.config?.active;
    $('captureStop').disabled=(!s.active&&!v.config?.active)||!!v.pending;$('captureHz').disabled=busy;
    $('captureRead').disabled=!c.connected||busy||!!v.pending;$('captureCsv').disabled=!s.rows||busy;$('captureClear').disabled=!s.rows||busy;
    const hz=s.active||s.rows?s.hz:Number($('captureHz').value),estimate=s.rows?s.bytes/s.rows:240;
    $('captureEstimate').textContent=`约 ${size(Math.round(estimate*hz*60))} / 分钟 · 单文件上限 64 MB`;
    $('captureState').textContent=s.active?'采集中':s.rows?'已停止':v.supported===false?'需升级固件':'未开始';
    $('captureState').className=`chip ${s.active?'on':s.rows?'warn':''}`;
    $('captureMessage').textContent=s.active?(v.pending?'正在确认采集状态…':'采集中，可以移动板卡；切换页面仍会继续记录。'):s.reason||(!c.connected?'请先连接设备。':v.config?.active?'设备上已有采集，请先停止后开始新记录。':v.message);
    $('captureRows').textContent=s.rows.toLocaleString();$('captureElapsed').textContent=(s.elapsedUs/1e6).toFixed(1)+' s';$('captureSize').textContent=size(s.bytes);
    $('captureQuality').textContent=`丢失 ${s.lost} 帧 · 损坏 ${s.bad} 帧 · 时间间断 ${s.timingGaps} 次 · 接近量程 ${s.clipped} 帧`;
    const meta=s.metadata||c.metadata;
    $('captureConfig').textContent=`${meta.mode===0?'六轴':meta.mode===2?'九轴相对角':meta.mode===1?'九轴':'—'} · ${meta.algorithm===1?'EKF':'VQF'} · 陀螺仪 ±${meta.rangeDps??'—'} °/s · 加速度 ±${meta.accRangeG??'—'} g · ${meta.firmware||'—'}`;
    if(s.latest)s.latest.forEach((x,i)=>$('captureValue'+i).textContent=x.toFixed(i<3?4:3));
    else for(let i=0;i<9;i++)$('captureValue'+i).textContent='—';
    if(!s.active&&wakeLock){void wakeLock.release();wakeLock=null;}
    if(now-lastDraw>200&&!document.querySelector('[data-view="capture"]').hidden){lastDraw=now;draw();}
  }
  function draw(){
    const points=session.preview(),TC=(name,fallback)=>window.GyroTheme?.color(name,fallback)||fallback;
    for(const [id,offset,unit] of [['captureAccChart',0,'g'],['captureGyroChart',3,'°/s'],['capturePoseChart',6,'°']]){
      const canvas=$(id),rect=canvas.getBoundingClientRect();if(!rect.width)continue;
      const colors=(offset===6?['--c-yaw','--c-pitch','--c-roll']:['--c-x','--c-y','--c-z']).map((name,i)=>TC(name,['#3abff8','#6bcf9e','#efa649'][i]));
      const ratio=devicePixelRatio||1,w=rect.width,h=200;canvas.width=Math.round(w*ratio);canvas.height=Math.round(h*ratio);
      const g=canvas.getContext('2d');g.scale(ratio,ratio);const left=54,top=18,pw=w-left-14,ph=h-top-30;
      const values=points.flatMap(p=>p.values.slice(offset,offset+3));let min=values.length?Math.min(...values):0,max=values.length?Math.max(...values):1;
      const span=Math.max(max-min,offset===0?.05:offset===3?1:1);min-=span*.15;max=min+span*1.3;
      const end=points.at(-1)?.t||15,start=Math.max(0,end-15),timeSpan=Math.max(1,end-start);
      const css=getComputedStyle(canvas);g.font='11px system-ui';g.fillStyle=css.color;g.strokeStyle=TC('--chart-grid','#334155');
      for(let i=0;i<5;i++){const y=top+i*ph/4;g.beginPath();g.moveTo(left,y);g.lineTo(w-14,y);g.stroke();g.fillText((max-i*(max-min)/4).toFixed(2),2,y+4);}
      g.fillText(unit,3,10);g.fillText(`${start.toFixed(1)} s`,left,h-5);g.fillText(`${end.toFixed(1)} s`,Math.max(left,w-55),h-5);
      colors.forEach((color,i)=>{g.strokeStyle=color;g.lineWidth=1.5;g.beginPath();let first=true;for(const p of points){const x=left+(p.t-start)/timeSpan*pw,y=top+(max-p.values[offset+i])/(max-min)*ph;if(first||p.gap)g.moveTo(x,y);else g.lineTo(x,y);first=false;}g.stroke();});
    }
  }
  api.render=render;setInterval(render,100);window.addEventListener('resize',draw);$('captureHz').onchange=render;render();
})();
