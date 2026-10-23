(() => {
  'use strict';
  const QUERY=0x74,MSG=0x75;
  const FIELDS=['abi','coreHz','uptimeMs','irq','overruns','spiErrors','consumed','predictions',
    'accelCalls','accelAttempts','poseDue','poseGenerated','maxAgeCycles','maxBatch','sourceCycles'];
  function decode(p) {
    if(p.length!==60)return {type:'badLength',id:MSG,length:p.length,expected:60};
    const v=new DataView(p.buffer,p.byteOffset,p.byteLength);
    const d=Object.fromEntries(FIELDS.map((k,i)=>[k,v.getUint32(i*4,true)]));
    if(d.abi!==1||!d.coreHz)return {type:'unknown',id:MSG};
    return {type:'eskfRuntime',...d};
  }
  const diff=(a,b)=>(a-b)>>>0;
  function rates(a,b) {
    if(!a)return null;
    const ms=diff(b.uptimeMs,a.uptimeMs);
    if(ms<100||ms>5000||b.coreHz!==a.coreHz)return null;
    const n=diff(b.consumed,a.consumed);
    // Reject a discontinuity/reset instead of plotting a wrapped giant rate.
    if(n>ms*20)return null;
    const per=k=>diff(b[k],a[k])*1000/ms;
    return {consumed:per('consumed'),predictions:per('predictions'),accelCalls:per('accelCalls'),
      accelAttempts:per('accelAttempts'),poseDue:per('poseDue'),poseGenerated:per('poseGenerated')};
  }
  function createController(io) {
    const state={supported:null,data:null,rates:null,at:0,pending:null,message:'请先连接设备',history:[]};
    let timer=null,session=0;
    const changed=()=>io.changed?.();
    function reset(){clearTimeout(timer);session++;Object.assign(state,{supported:null,data:null,rates:null,at:0,pending:null,message:'请先连接设备',history:[]});changed();}
    function finish(text){clearTimeout(timer);state.pending=null;state.message=text;changed();}
    function query(){
      if(!io.connected()||io.busy()||state.pending||state.supported===false)return false;
      const p=state.pending={seq:io.nextSeq()&255,session};state.message='正在读取…';
      timer=setTimeout(()=>{if(state.pending===p){state.rates=null;finish('读取超时，请重试');}},2500);
      Promise.resolve(io.send(QUERY,[])).then(ok=>{if(!ok&&state.pending===p)finish('读取发送失败');},()=>{if(state.pending===p)finish('读取发送失败');});changed();return true;
    }
    function onState(d,seq){
      const p=state.pending;if(!p||p.seq!==seq||p.session!==session)return;
      state.rates=rates(state.data,d);state.data=d;state.at=Date.now();state.supported=true;
      state.history.push({hostTime:new Date(state.at).toISOString(),...d});
      if(state.history.length>300)state.history.shift();
      finish('已读取；速率按相邻设备快照估算');
    }
    function onAck(cmd,status,seq){
      if(cmd!==QUERY||!state.pending||state.pending.seq!==seq||!status)return;
      if(status===1){state.supported=false;state.data=null;state.rates=null;finish('当前固件不支持运行诊断，请升级 APP');}
      else finish('设备拒绝读取，请稍后重试');
    }
    function status(now=Date.now()){
      if(!io.connected())return '请先连接设备';
      if(state.supported===false)return state.message;
      if(state.at&&now-state.at>4000)return '数据已过期，请重新读取';
      return state.message;
    }
    return {state,reset,query,onState,onAck,status};
  }
  function createView(c,context,doc=document){
    const $=id=>doc.getElementById(id),s=c.state;
    const text=(id,v)=>{const el=$(id);if(el)el.textContent=v;};
    function render(){
      const ctx=context(),fresh=ctx.connected&&s.at&&Date.now()-s.at<=4000;
      const d=fresh?s.data:null,r=fresh?s.rates:null;
      text('eskfRuntimeMessage',c.status());
      $('eskfRuntimeRead').disabled=!ctx.connected||ctx.busy||!!s.pending||s.supported===false;
      $('eskfRuntimeExport').disabled=!s.history.length;
      text('eskfRuntimeCounts',d?`${d.consumed.toLocaleString()} / ${d.predictions.toLocaleString()}`:'--');
      text('eskfRuntimeRates',r?`${r.consumed.toFixed(1)} / ${r.predictions.toFixed(1)} Hz`:'等待相邻快照');
      text('eskfRuntimeAccel',r?`${r.accelCalls.toFixed(1)} / ${r.accelAttempts.toFixed(1)} Hz`:'等待相邻快照');
      text('eskfRuntimePose',d?`${d.poseDue.toLocaleString()} / ${d.poseGenerated.toLocaleString()}`:'--');
      text('eskfRuntimeErrors',d?`${d.overruns} / ${d.spiErrors}`:'--');
      text('eskfRuntimeBatch',d?`${d.maxBatch} 帧`:'--');
      const age=d?d.maxAgeCycles*1e6/d.coreHz:null;
      text('eskfRuntimeAge',age===null?'--':`${age.toFixed(1)} µs${age>1000?' · 超过 1 ms 目标':''}`);
      $('eskfRuntimeAge').classList.toggle('warn',age!==null&&age>1000);
      text('eskfRuntimeLedger',d?(d.consumed===d.predictions?'当前累计预测与消费一致':'当前累计预测与消费不一致'):'等待设备快照');
    }
    $('eskfRuntimeRead').onclick=()=>c.query();
    $('eskfRuntimeExport').onclick=()=>{
      const blob=new Blob([JSON.stringify({abi:1,scope:'最近300次只读快照；不是逐帧记录或严格同窗验收',samples:s.history},null,2)],{type:'application/json'});
      const url=URL.createObjectURL(blob),a=doc.createElement('a');a.href=url;a.download='eskf-runtime.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
    };
    return {render};
  }
  globalThis.GyroEskfRuntime={QUERY,MSG,FIELDS,decode,rates,createController,createView};
})();
