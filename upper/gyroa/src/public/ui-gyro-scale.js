(() => {
  'use strict';
  const api=GyroScale,io=GyroScaleIO,c=api.control,$=id=>document.getElementById(id),cap=api.capture=api.createCapture();
  let stage='idle',before=null,baseline=null,signature=null,started=0,leaseAt=0,streamPending=null,stopPending=null,result=null,capturedMeasurement=false;
  const context=()=>io.context();
  const metadata=()=>JSON.stringify(context().metadata);
  const active=()=>['preparing','capturing','arming','checking'].includes(stage)||!!stopPending;
  api.busy=()=>active()||!!c.state.pending;
  function payload(enable){const p=new Uint8Array(7),v=new DataView(p.buffer);p[0]=enable;v.setUint16(1,200,true);v.setUint32(3,cap.state().token,true);return p;}
  async function stream(enable){
    const p={seq:io.nextSeq()&255,at:performance.now()};if(enable)streamPending=p;else stopPending=p;
    const ok=await io.stream(payload(enable));
    if(!ok){if(enable)fail('采集请求发送失败');else{stopPending=null;c.state.message='停止采集未确认，请重连设备后再保存';}}
  }
  function clearCapturedMeasurement(){if(capturedMeasurement){$('gyroScaleMeasured').value='';$('gyroScaleMeasuredFactor').value='1';capturedMeasurement=false;}}
  function fail(message){cap.fail(message);stage='failed';result=null;clearCapturedMeasurement();if(!stopPending&&(streamPending||cap.state().token))void stream(0);streamPending=null;}
  api.onStreamAck=(status,seq)=>{
    if(stopPending?.seq===seq){stopPending=null;if(status){cap.fail('设备拒绝停止采集，请重连设备');stage='failed';c.state.supported=false;}return;}
    if(streamPending?.seq!==seq)return;streamPending=null;if(status)fail('设备拒绝采集请求，请确认固件版本和设备状态');
  };
  api.onFrame=m=>{cap.ingest({...m,at:performance.now()});};
  api.onDropped=()=>{if(stage==='capturing')fail('收到损坏数据，请重新采集');};
  $('gyroScalePrepare').onclick=()=>{
    const ctx=context();if(!ctx.connected||ctx.busy||!ctx.ready||ctx.metadata.mode!==0||c.state.supported!==true||c.state.data?.restartRequired||c.state.dirty||active())return;
    const axis=Number($('gyroScaleAxis').value),circles=Number($('gyroScaleCircles').value),token=new Uint32Array(1);crypto.getRandomValues(token);
    try{cap.start(axis,circles,token[0]||1,ctx.metadata.rangeDps);}catch(e){c.state.message=e.message;render();return;}
    clearCapturedMeasurement();cap.state().active=false;before=c.state.data;stage='preparing';started=performance.now();signature=metadata();result=null;
    if(!c.query())fail('参数读取暂不可用，请稍后重试');render();
  };
  $('gyroScaleBegin').onclick=()=>{
    if(stage!=='capturing'||cap.state().phase!=='ready')return;
    before=c.state.data;stage='arming';cap.state().message='正在确认转动前采样状态，请等待转动提示…';
    if(!c.query())fail('转动前采样状态读取失败，请重新采集');render();
  };
  $('gyroScaleEnd').onclick=()=>{cap.end();render();};
  $('gyroScaleCancel').onclick=()=>fail('已取消采集，未保存任何系数');
  $('gyroScaleRead').onclick=()=>{stage='idle';c.query();};$('gyroScaleApply').onclick=()=>{stage='idle';c.apply();};$('gyroScaleRestore').onclick=()=>{stage='idle';c.restore();};$('gyroScaleDiscard').onclick=()=>{stage='idle';c.discard();};
  for(let i=0;i<3;i++)$('gyroScaleFactor'+i).oninput=e=>c.choose(i,e.target.value);
  $('gyroScaleMeasured').oninput=()=>{
    // Editing an auto-filled angle starts a manual, uncompensated measurement.
    if(capturedMeasurement)$('gyroScaleMeasuredFactor').value='1';
    capturedMeasurement=false;result=null;stage='idle';
  };
  $('gyroScaleMeasuredFactor').oninput=()=>{capturedMeasurement=false;result=null;};
  $('gyroScaleAxis').onchange=()=>{clearCapturedMeasurement();};
  $('gyroScaleCircles').oninput=()=>{clearCapturedMeasurement();};
  $('gyroScaleCalculate').onclick=()=>{
    if(active()||c.state.pending||context().busy||!c.state.data||c.state.data.restartRequired)return;
    stage='idle';
    try{
      const axis=Number($('gyroScaleAxis').value),circles=Number($('gyroScaleCircles').value),measurementFactor=Number($('gyroScaleMeasuredFactor').value);
      if(!Number.isInteger(circles))throw Error('圈数须为整数');
      if(!Number.isInteger(axis)||axis<0||axis>2)throw Error('请选择有效转轴');
      const measured=capturedMeasurement&&result?.axis===axis&&result.target===circles*360?result.measured:Number($('gyroScaleMeasured').value);
      const k=api.factor(measurementFactor,circles*360,measured);c.choose(axis,k);
      c.state.message=`${'XYZ'[axis]} 轴计算系数 ${k.toFixed(7)}（测量时系数 ${measurementFactor.toFixed(7)}），点击保存并应用后生效`;
    }catch(e){c.state.message=e.message;}render();
  };
  function render(){
    const ctx=context(),s=cap.state(),now=performance.now(),data=c.state.data;
    if(active()&&(!ctx.connected||ctx.busy||metadata()!==signature)){if(ctx.connected)fail('连接或配置变化，请重新采集');else{cap.fail('设备已断开，请重新采集');stage='failed';streamPending=stopPending=null;result=null;}}
    if(stage==='preparing'&&!c.state.pending){
      if(data===before||c.state.supported!==true||data?.restartRequired)fail('未能读取当前生效系数，请检查固件版本或先重启设备');
      else{baseline=data;s.active=true;stage='capturing';started=leaseAt=now;void stream(1);}
    }
    if(stage==='arming'&&!c.state.pending){
      if(data===before||data?.restartRequired||!api.equal(data.active,baseline.active)||!api.equal(data.saved,baseline.saved))fail('转动前参数未确认，请重新采集');
      else{baseline=data;stage='capturing';if(!cap.begin())s.message='静止窗口已重置，请放稳后再次点击开始转动';}
    }
    if(stage==='capturing'){
      if(!s.active){
        if(s.phase!=='done')fail(s.message);
        else{result=s.result;stage='checking';before=data;void stream(0);if(!c.query())fail('采样状态核对请求失败');}
      }else if((streamPending&&now-streamPending.at>3000)||(s.arrival===null?now-started>3000:now-s.arrival>2000))fail('采集数据中断，请重新采集');
      else if(!streamPending&&now-leaseAt>=4000){leaseAt=now;void stream(1);}
    }
    if(stage==='checking'&&!c.state.pending){
      if(data===before||data.skips!==baseline.skips||data.clipped!==baseline.clipped||!api.equal(data.active,baseline.active)||!api.equal(data.saved,baseline.saved))fail('采集期间出现漏采、超量程或系数变化，请重新采集');
      else{
        stage='done';$('gyroScaleMeasured').value=result.measured.toFixed(5);$('gyroScaleMeasuredFactor').value=baseline.active[result.axis].toString();capturedMeasurement=true;
        try{const k=api.factor(baseline.active[result.axis],result.target,result.measured);c.choose(result.axis,k);c.state.message=`采集完成：测得 ${result.measured.toFixed(3)}°，计算系数 ${k.toFixed(7)}。点击保存并应用，重启后反向复测`;}
        catch(e){fail(e.message);}
      }
    }
    if(stopPending&&now-stopPending.at>4000){stopPending=null;stage='failed';c.state.message='停止采集未确认，请重连设备后再保存';c.state.supported=false;}
    const busy=ctx.busy||active()||!!c.state.pending,editable=ctx.connected&&!busy&&c.state.supported===true;
    for(let i=0;i<3;i++){
      const input=$('gyroScaleFactor'+i);input.disabled=!editable;
      if(c.state.draft&&document.activeElement!==input)input.value=Number.isFinite(Number(c.state.draft[i]))?Number(c.state.draft[i]).toFixed(7):c.state.draft[i];
      $('gyroScaleActive'+i).textContent=data?data.active[i].toFixed(7):'—';
    }
    $('gyroScaleRead').disabled=!ctx.connected||busy;$('gyroScaleApply').disabled=!editable||!c.state.dirty||!api.valid(c.state.draft);
    $('gyroScaleRestore').disabled=!editable;$('gyroScaleDiscard').disabled=busy||!c.state.dirty;
    $('gyroScalePrepare').disabled=!editable||!ctx.ready||ctx.metadata.mode!==0||data?.restartRequired||c.state.dirty;
    $('gyroScaleBegin').disabled=stage!=='capturing'||s.phase!=='ready'||!!streamPending;
    $('gyroScaleEnd').disabled=stage!=='capturing'||s.phase!=='turn';$('gyroScaleCancel').disabled=!active();
    for(const id of ['gyroScaleAxis','gyroScaleCircles','gyroScaleMeasured','gyroScaleMeasuredFactor'])$(id).disabled=busy;
    $('gyroScaleCalculate').disabled=!editable||data?.restartRequired;
    $('gyroScaleAngle').textContent=`累计角度：${s.angle?.toFixed(3)??'—'}° · ${s.frames??0} 帧`;
    $('gyroScaleMessage').textContent=!ctx.connected?'请先连接设备':active()?s.message:stage==='failed'?s.message:c.state.message;
    $('gyroScaleStatus').textContent=data?.restartRequired?'等待重启生效':c.state.dirty?'未保存':data?'已生效':'—';
  }
  let connected=false;
  setInterval(()=>{if(!context().connected&&(connected||c.state.pending)){c.reset();cap.fail('设备已断开，请重新采集');stage='idle';streamPending=stopPending=null;result=null;}connected=context().connected;render();},100);
  render();
})();
