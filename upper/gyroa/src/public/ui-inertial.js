(() => {
  const $=id=>document.getElementById(id), session=GyroInertial.session, io=GyroInertialIO;
  let signature=null,startedAt=0,pending=null,leaseAt=0,wasActive=false;
  const stableConfig=()=>{const m=io.context().metadata;return JSON.stringify([m.algorithm,m.mode,m.rangeDps,m.profile,m.vqfSettings,m.zaruSettings,m.restBiasEnabled,m.motionBiasEnabled]);};
  function payload(enable,token) {const p=new Uint8Array(7),v=new DataView(p.buffer);p[0]=enable;v.setUint16(1,200,true);v.setUint32(3,token,true);return p;}
  async function command(enable) {
    const token=session.state().token;
    if(enable)pending={seq:io.nextSeq(),at:performance.now()};
    const sent=await io.send(payload(enable,token));
    if(enable && session.state().token===token && session.state().active && !sent)session.stop('惯导数据请求发送失败');
  }
  function finish(message) {pending=null;session.stop(message);void command(0);wasActive=false;render();}
  $('insStart').onclick=()=>{
    if(session.state().active)void command(0);
    const bytes=new Uint32Array(1);crypto.getRandomValues(bytes);const token=bytes[0]||1;
    startedAt=leaseAt=performance.now();signature=stableConfig();session.start($('insZupt').checked,{token});
    wasActive=true;void command(1);render();
  };
  $('insStop').onclick=()=>finish('已停止');
  $('insStationary').onclick=()=>{session.stationary();render();};
  $('insZupt').onchange=()=>{session.state().zupt=$('insZupt').checked;};
  GyroInertial.onAck=(status,seq)=>{
    if(!pending || pending.seq!==seq || !session.state().active)return;
    pending=null;
    if(status)finish(status===1?'固件不支持独立惯导数据，请升级到 20261005i 或更新版本':'设备拒绝惯导请求，请退出设置或校准状态');
  };
  GyroInertial.onFrame=m=>{
    session.ingest({...m,at:performance.now()});
    if(wasActive && !session.state().active)finish(session.state().message);
  };
  GyroInertial.onDropped=()=>{if(session.state().active)finish('收到损坏的惯导数据，已停止积分');};
  function render() {
    const c=io.context(),s=session.state(),now=performance.now();
    if(s.active && (!c.connected || c.busy || stableConfig()!==signature)) {pending=null;session.stop('连接或融合配置变化，请重新建立原点');}
    if(s.active && pending && now-pending.at>3000) {pending=null;session.stop('惯导请求未获确认，请检查固件版本和连接');}
    if(s.active && s.arrival!==null && now-s.arrival>2000)session.stop('惯导数据中断，请重新建立原点');
    if(s.active && s.arrival===null && now-startedAt>3000)session.stop('未收到独立惯导数据，请检查固件与连接');
    if(wasActive && !s.active) {wasActive=false;pending=null;void command(0);}
    if(s.active && !pending && now-leaseAt>=4000) {leaseAt=now;void command(1);}
    $('insStart').disabled=!c.connected || c.busy;$('insStop').disabled=!s.active;
    $('insStationary').disabled=!s.active || s.phase!=='tracking';
    $('insMessage').textContent=s.message;
    $('insQuality').textContent=`数据质量：${s.quality} · ${s.frames} 帧${s.periodUs?` · ${Math.round(1e6/s.periodUs)} Hz`:''}`;
    const d=s.diagnostics;
    $('insDiagnostics').textContent=d?`100 ms 判定：重力残差 ${d.accMean.toFixed(3)} m/s² · 加速度波动 ${d.accStd.toFixed(3)} m/s² · 角速度 ${d.gyroMean.toFixed(3)} °/s`:'';
    const origin=!!s.initialResidual;
    $('insVelocity').textContent=origin?`速度 X / Y / Z：${s.v.map(x=>(x*100).toFixed(2)).join(' / ')} cm/s`:'';
    ['insX','insY','insZ'].forEach((id,i)=>{$(id).textContent=origin?(s.p[i]*100).toFixed(2):'—';});
  }
  setInterval(render,100);render();
})();
