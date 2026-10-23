(() => {
  'use strict';
  const QUERY=0x50,SET=0x51,MSG=0x52,NAMES=['VQF','EKF','ESKF'];
  const LAST=NAMES.length-1;
  function decode(p) {
    if(p.length!==4)return {type:'badLength',id:MSG,length:p.length,expected:4};
    if(p[0]!==1||p[1]>LAST||p[2]>LAST||p[3]!==Number(p[1]!==p[2]))return {type:'unknown',id:MSG};
    return {type:'fusionAlgorithm',active:p[1],saved:p[2],restartRequired:!!p[3]};
  }
  function createController(io) {
    const state={supported:null,data:null,draft:null,pending:null,message:'请先连接设备'};
    let timer=null;
    const changed=()=>io.changed?.();
    function finish(message){clearTimeout(timer);timer=null;state.pending=null;state.message=message;changed();}
    function reset(){clearTimeout(timer);timer=null;Object.assign(state,{supported:null,data:null,draft:null,pending:null,message:'请先连接设备'});changed();}
    function request(kind) {
      if(state.pending||!io.connected()||(kind==='set'&&(state.supported!==true||io.busy()||!([0,1,2].includes(state.draft)))))return false;
      const req=state.pending={kind,seq:io.nextSeq(),value:state.draft,acked:false};
      state.message=kind==='set'?'正在保存融合算法…':'正在读取融合算法…';changed();
      timer=setTimeout(()=>{if(state.pending===req)finish(kind==='set'?'保存未确认，请读取核对；设备尚未自动重启':'未收到融合算法，请读取重试');},3000);
      Promise.resolve(io.send(kind==='set'?SET:QUERY,kind==='set'?[req.value]:[])).then(ok=>{if(!ok&&state.pending===req)finish('融合算法指令发送失败');},()=>{if(state.pending===req)finish('融合算法指令发送失败');});
      return true;
    }
    function onAck(cmd,status,seq) {
      const p=state.pending;if(!p||seq!==p.seq||cmd!==(p.kind==='set'?SET:QUERY))return;
      if(status===1){state.supported=false;finish('当前固件不支持融合算法切换，请升级 APP');return;}
      if(status!==0){finish('保存或读取失败，保留当前算法；请确认设备空闲后重试');return;}
      if(p.kind==='set')p.acked=true;
    }
    function onState(data,seq) {
      const p=state.pending;if(!p||p.seq!==seq||(p.kind==='set'&&!p.acked))return;
      state.supported=true;state.data=data;
      if(state.draft===null)state.draft=data.saved;
      if(p.kind==='set'&&data.saved!==p.value){finish('算法回读不一致，保存未确认；未自动重启');return;}
      if(p.kind==='set'&&data.restartRequired) {
        finish(`已保存 ${NAMES[data.saved]}，正在重启并重连…`);
        Promise.resolve().then(()=>io.restart()).then(ok=>{if(!ok&&io.connected()) {state.message='算法已保存，暂未重启；请在设备空闲时重启';changed();}},()=>{state.message='算法已保存，自动重启未完成；请重启设备';changed();});
      } else finish(`${p.kind==='set'?'已保存，':''}当前 ${NAMES[data.active]}${data.restartRequired?'；已保存 '+NAMES[data.saved]+'，重启后生效':''}`);
    }
    return {state,reset,query:()=>request('query'),choose:value=>{if(!state.pending&&[0,1,2].includes(value)){state.draft=value;changed();}},apply:()=>request('set'),onAck,onState};
  }
  function createView(controller,context,doc=document) {
    const select=doc.getElementById('algorithmSelect'),apply=doc.getElementById('algorithmApply'),read=doc.getElementById('algorithmRead'),status=doc.getElementById('algorithmMessage'),current=doc.getElementById('algorithmState');
    function render() {
      const s=controller.state,c=context();if(s.draft!==null)select.value=String(s.draft);
      select.disabled=!c.connected||c.busy||!!s.pending||s.supported!==true;
      apply.disabled=select.disabled;read.disabled=!c.connected||c.busy||!!s.pending;
      current.textContent=s.data?`当前 ${NAMES[s.data.active]} · 已保存 ${NAMES[s.data.saved]}`:'当前 -- · 已保存 --';
      status.textContent=!c.connected?'请先连接设备':c.dirty?'请先保存或撤销其他配置修改':s.message;
      globalThis.GyroVqfTuning?.syncCards?.(doc);
    }
    select.onchange=()=>controller.choose(Number(select.value));apply.onclick=()=>controller.apply();read.onclick=()=>controller.query();
    return {render};
  }
  globalThis.GyroFusionAlgorithm={QUERY,SET,MSG,NAMES,decode,createController,createView};
})();
