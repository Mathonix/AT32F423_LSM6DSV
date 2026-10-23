(() => {
  'use strict';
  const QUERY=0x4d, SET=0x4e, MSG=0x4f;
  /* Calibration health: 0x71 query, 0x72 set (mode,value), 0x73 reply. Kept in
   * this module because it shares the same card and the same 0.8 s tick. */
  const HQUERY=0x71, HSET=0x72, HMSG=0x73;
  function decodeHealth(p) {
    if(p.length!==16) return {type:'badLength',id:HMSG,length:p.length,expected:16};
    if(p[0]!==1) return {type:'unknown',id:HMSG};
    const v=new DataView(p.buffer,p.byteOffset,p.byteLength);
    const expected=v.getFloat32(2,true),current=v.getFloat32(6,true);
    if((p[1]&0xf8) || p.slice(10).some(x=>x!==0) ||
       !Number.isFinite(expected) || !Number.isFinite(current) || current<0 ||
       !(expected===0 || (expected>=20 && expected<=80)) || !!(p[1]&1)!==(expected>0))
      return {type:'unknown',id:HMSG};
    return {type:'magHealth',version:1,stored:!!(p[1]&1),suspect:!!(p[1]&2),low:!!(p[1]&4),
      expected,current};
  }
  function decode(p) {
    if(p.length!==4) return {type:'badLength',id:MSG,length:p.length,expected:4};
    if(p[0]!==1 || p[1]>1 || p[2]>1 || p[3]) return {type:'unknown',id:MSG};
    return {type:'magRejection',enabled:!!p[1],saved:!!p[2]};
  }
  function createController(io) {
    const state={supported:null,data:null,pending:null,message:'请先连接设备',
      diag:null,diagAt:0,diagUnsupported:false,
      health:null,healthAt:0,healthUnsupported:false,healthMessage:'请先连接设备'};
    let timer=null;
    const changed=()=>io.changed?.();
    function reset() { clearTimeout(timer);timer=null;state.supported=null;state.data=null;state.pending=null;state.message='请先连接设备';
      state.diag=null;state.diagAt=0;state.diagUnsupported=false;
      state.health=null;state.healthAt=0;state.healthUnsupported=false;state.healthMessage='请先连接设备';changed(); }
    /* 0x28 只在调参页可见时按 0.8 s 静默查询；失败不写消息，不打断用户操作。 */
    function pollDiagnostic() {
      if(state.diagUnsupported || !io.connected() || io.busy()) return false;
      return io.diagnostic?.() !== false;
    }
    function onDiag(msg) {
      state.diag={ready:!!msg.magReady,interference:!!msg.magInterference,degraded:!!msg.magDegraded,
        calSuspect:!!msg.magCalSuspect};
      state.diagAt=Date.now();changed();
    }
    /* 基准查询和诊断帧一起轮询；没有 pending 时静默更新数值。 */
    function pollHealth() {
      if(state.healthUnsupported || !io.connected() || io.busy()) return false;
      return io.health?.() !== false;
    }
    function onHealth(msg,seq) {
      state.health={stored:!!msg.stored,suspect:!!msg.suspect,low:!!msg.low,
        expected:msg.expected,current:msg.current};
      state.healthAt=Date.now();
      const p=state.pending;
      if(p && p.kind==='health' && p.seq===seq) {p.readback=state.health;finishHealth();return;}
      changed();
    }
    function finishHealth() {
      const p=state.pending;if(!p || p.kind!=='health' || !p.acked || !p.readback)return;
      const h=p.readback;
      if(!h.stored || !Number.isFinite(h.expected) || !Number.isFinite(h.current) ||
         h.expected<20 || h.expected>80 || Math.abs(h.expected-h.current)>.5)
        finish('基准回读不一致，保存未确认');
      else finish('已把当前场强记录为基准');
    }
    function healthText(connected,nineAxis,now=Date.now()) {
      if(!connected) return '请先连接设备';
      if(nineAxis===false) return '六轴模式不读磁力计，无法评估标定';
      if(state.healthUnsupported) return '当前固件不支持标定健康检查（0x71），请升级 APP';
      if(!state.health || now-state.healthAt>4000) return state.healthMessage;
      const h=state.health;
      if(!h.stored) return `未记录标定基准，无法判断标定是否失效。当前 ${h.current.toFixed(1)} µT。`;
      const dev=(h.current-h.expected)/h.expected*100;
      const base=`标定基准 ${h.expected.toFixed(1)} µT · 当前 ${h.current.toFixed(1)} µT（${dev>=0?'+':''}${dev.toFixed(0)}%）`;
      return h.suspect ? `${base}。偏差已超阈值，标定可能失效。` : `${base}。`;
    }
    function capture() {
      if(state.pending || !io.connected() || io.busy() || state.healthUnsupported) return false;
      const req=state.pending={kind:'health',seq:io.nextSeq(),value:null,acked:false};
      state.healthMessage='正在记录…';changed();
      timer=setTimeout(()=>{if(state.pending===req)finish('未确认基准记录结果，请点击读取重试');},3000);
      Promise.resolve(io.send(HSET,[2,0,0,0,0])).then(ok=>{
        if(!ok && state.pending===req) finish('基准记录指令发送失败');},()=>{if(state.pending===req)finish('基准记录指令发送失败');});
      return true;
    }
    function onDiagUnsupported() { state.diagUnsupported=true;state.diag=null;state.diagAt=0;changed(); }
    /* 4 s 内没有新帧就退回“未知”，避免设备停机后指示灯停在旧结论。 */
    function diagStatus(now=Date.now()) {
      if(state.diagUnsupported) return {text:'固件不支持',cls:''};
      if(!state.diag || now-state.diagAt>4000) return {text:'等待设备上报',cls:''};
      /* Before "not locked": a calibration that moved often falls outside the
       * plausibility window and never locks, so the wait state would hide the
       * actual cause. */
      if(state.diag.calSuspect) return {text:'磁标定可能已失效',cls:'warn'};
      if(state.diag.degraded) {
        const gate=globalThis.GyroMagGate?.control?.state?.data?.enabled;
        return {text:gate===false?'干扰持续 · 降级接受':'干扰持续 · 已暂停磁融合',cls:'warn'};
      }
      if(state.diag.interference) return {text:'拒磁中',cls:'warn'};
      if(!state.diag.ready) return {text:'等待磁北锁定',cls:''};
      return {text:'磁场正常',cls:'on'};
    }
    function finish(message) {
      clearTimeout(timer);timer=null;
      const kind=state.pending?.kind;
      state.pending=null;
      if(kind==='health') state.healthMessage=message; else state.message=message;
      changed();
    }
    function request(kind,value) {
      if(state.pending || !io.connected() || (kind==='set' && (state.supported!==true || io.busy()))) return false;
      const cmd=kind==='set'?SET:QUERY;
      const req=state.pending={kind,seq:io.nextSeq(),value,acked:false};
      state.message=kind==='set'?'正在保存…':'正在读取…';changed();
      timer=setTimeout(()=>{if(state.pending===req)finish(kind==='set'?'未确认保存结果，请点击读取核对':'未读到磁干扰拒绝状态，请点击读取重试');},3000);
      Promise.resolve(io.send(cmd,kind==='set'?[value?1:0]:[])).then(ok=>{if(!ok && state.pending===req)finish('磁干扰拒绝指令发送失败');},()=>{if(state.pending===req)finish('磁干扰拒绝指令发送失败');});
      return true;
    }
    function onAck(cmd,status,seq) {
      const p=state.pending;if(!p || p.seq!==seq)return;
      if(cmd===HSET) {
        if(p.kind!=='health')return;
        if(status===1) {state.healthUnsupported=true;finish('当前固件不支持 标定健康检查，请升级 APP');return;}
        if(status!==0) {finish('基准记录失败，设备状态未确认改变');return;}
        p.acked=true;finishHealth();return;
      }
      if(cmd!==(p.kind==='set'?SET:QUERY))return;
      if(status===1) {state.supported=false;finish('当前固件不支持 磁干扰拒绝 开关，请升级 APP');return;}
      if(status!==0) {finish('磁干扰拒绝 保存或读取失败，设备状态未确认改变');return;}
      if(p.kind==='set') p.acked=true;
    }
    function onState(data,seq) {
      const p=state.pending;if(!p || p.seq!==seq)return;
      if(p.kind==='set' && !p.acked)return;
      state.supported=true;state.data=data;
      if(p.kind==='set' && (data.enabled!==p.value || data.saved!==p.value)) {finish('磁干扰拒绝 回读与设置不一致，保存未确认');return;}
      finish(`${p.kind==='set'?'已保存，':''}磁干扰拒绝 已${data.enabled?'开启':'关闭'}`);
    }
    return {state,reset,query:()=>request('query'),apply:value=>typeof value==='boolean' && request('set',value),onAck,onState,
      pollDiagnostic,onDiag,onDiagUnsupported,diagStatus,
      pollHealth,onHealth,onHealthUnsupported:()=>{state.healthUnsupported=true;state.health=null;
        state.healthMessage='当前固件不支持标定健康检查，请升级 APP';changed();},
      capture,healthText};
  }
  function createView(controller,context,doc=document) {
    const toggle=doc.getElementById('magRejectionEnable'),read=doc.getElementById('magRejectionRead'),status=doc.getElementById('magRejectionMessage');
    const chip=doc.getElementById('magInterferenceChip'),detail=doc.getElementById('magInterferenceState');
    const health=doc.getElementById('magHealthMessage'),captureBtn=doc.getElementById('magHealthCapture');
    function render() {
      const s=controller.state,c=context();
      toggle.checked=s.pending?.kind==='set'?s.pending.value:s.data?.enabled??false;
      toggle.disabled=!c.connected || c.busy || !!s.pending || s.supported!==true;
      read.disabled=!c.connected || c.busy || !!s.pending;
      status.textContent=!c.connected?'请先连接设备':s.message;
      if(chip) {
        const show=c.connected;
        const six=show && c.nineAxis===false;
        const d=six?{text:'六轴不使用磁力计',cls:''}:controller.diagStatus();
        if(chip.hidden===show) chip.hidden=!show;
        const cls='chip'+(d.cls?' '+d.cls:'');
        if(chip.className!==cls) chip.className=cls;
        if(chip.textContent!==d.text) chip.textContent=d.text;
      }
      if(detail) {
        const show=c.connected;
        if(detail.hidden===show) detail.hidden=!show;
        const text=!show?'':(show && c.nineAxis===false
          ?'六轴模式关闭磁力计，切到九轴后这里显示拒磁状态。'
          :s.diagUnsupported
          ?'当前固件没有磁干扰状态查询（0x28），升级 APP 后可显示。'
          :`当前磁场：${controller.diagStatus().text}。本页可见时每 0.8 秒查询一次。`);
        if(detail.textContent!==text) detail.textContent=text;
      }
      if(health) {
        const text=controller.healthText(c.connected,c.nineAxis);
        if(health.textContent!==text) health.textContent=text;
      }
      if(captureBtn) {
        const h=s.health;
        captureBtn.disabled=!c.connected || c.nineAxis===false || c.busy || !!s.pending || s.healthUnsupported===true ||
          !h || !(h.current>=20);
      }
    }
    toggle.onchange=()=>{controller.apply(toggle.checked);render();};read.onclick=()=>{controller.query();controller.pollHealth();};
    if(captureBtn) captureBtn.onclick=()=>{controller.capture();render();};
    return {render};
  }
  globalThis.GyroMagRejection={QUERY,SET,MSG,HQUERY,HSET,HMSG,decode,decodeHealth,createController,createView};
})();
