(() => {
  'use strict';
  const QUERY=0x58, SET=0x59, RESTORE=0x5a, MSG=0x5b;
  // Must match vqf_tuning.h. Defaults always come from the connected firmware.
  const PARAMS=[
    ['tauAcc','加速度滤波时间','s',.1,100,.1,'滤波', '增大可减少加速度扰动，但姿态响应变慢。'],
    ['tauMag','磁航向收敛时间','s',.2,100,.1,'滤波','增大可减弱磁场扰动对航向的影响，收敛也会变慢。'],
    ['biasSigmaInit','初始零偏不确定度','°/s',.001,10,.001,'零偏','启动时陀螺仪零偏估计的不确定度。'],
    ['biasSigmaRest','静止零偏不确定度','°/s',.001,10,.001,'零偏','静止观测的零偏不确定度。'],
    ['biasSigmaMotion','运动零偏不确定度','°/s',.001,10,.001,'零偏','运动观测的零偏不确定度。'],
    ['biasClip','零偏估计限幅','°/s',.1,20,.1,'零偏','允许估计的陀螺仪零偏上限。'],
    ['biasForgettingTime','零偏遗忘时间','s',1,10000,1,'零偏','增大后已学习的零偏保持更久。'],
    ['restFilterTau','静止检测滤波时间','s',.01,10,.01,'静止检测','静止检测中陀螺仪和加速度的滤波时间。'],
    ['restMinT','最短静止时间','s',.1,60,.1,'静止检测','连续满足条件多久才判定为静止。'],
    ['restThGyr','陀螺仪静止阈值','°/s',.01,20,.01,'静止检测','阈值越大，静止判定越宽松。'],
    ['restThAcc','加速度静止阈值','m/s²',.01,10,.01,'静止检测','加速度变化允许的范围。'],
    ['magCurrentTau','磁场检测滤波时间','s',.01,10,.01,'磁场判定','用于磁场模长和倾角检测的滤波时间。'],
    ['magNormTh','磁场模长相对阈值','比例',.01,.5,.01,'磁场判定','0.1 表示允许参考模长附近 10% 的偏差。'],
    ['magDipTh','磁场倾角阈值','°',1,90,1,'磁场判定','与参考磁倾角相比允许的偏差。'],
  ];
  function formatFloat(value) {
    const v=Math.fround(value);
    for(let digits=1;digits<=9;digits++) {
      const text=String(Number(v.toPrecision(digits)));
      if(Math.fround(Number(text))===v)return text;
    }
    return String(v);
  }
  const equal=(a,b)=>a?.length===PARAMS.length && b?.length===PARAMS.length && a.every((v,i)=>Math.fround(v)===Math.fround(b[i]));
  function validate(values) {
    if(!Array.isArray(values)||values.length!==PARAMS.length)return '参数数量不正确';
    for(let i=0;i<PARAMS.length;i++) {
      const p=PARAMS[i],v=values[i];
      if(v===null||String(v).trim()===''||!Number.isFinite(Number(v)))return `${p[1]}请输入有限数值`;
      const n=Math.fround(Number(v));
      if(n<Math.fround(p[3])||n>Math.fround(p[4]))return `${p[1]}范围为 ${p[3]}～${p[4]} ${p[2]}`;
    }
    const v=values.map(Number);
    if(v[3]>v[2]||v[4]>v[2])return '静止和运动零偏不确定度不能大于初始不确定度';
    if(v[2]>v[5])return '初始零偏不确定度不能大于零偏估计限幅';
    return '';
  }
  function encode(values) {
    const error=validate(values);if(error)throw new Error(error);
    const p=new Uint8Array(60),view=new DataView(p.buffer);p[0]=1;
    values.forEach((v,i)=>view.setFloat32(4+i*4,Number(v),true));return [...p];
  }
  function decode(p) {
    if(p.length!==60)return {type:'badLength',id:MSG,length:p.length,expected:60};
    if(p[0]!==1||p[1]>2||p[2]>3||p[3]>1)return {type:'unknown',id:MSG};
    const view=new DataView(p.buffer,p.byteOffset,p.byteLength),values=PARAMS.map((_,i)=>view.getFloat32(4+i*4,true));
    if(validate(values))return {type:'unknown',id:MSG};
    return {type:'vqfTuning',page:p[1],custom:!!(p[2]&1),restartRequired:!!(p[2]&2),algorithm:p[3],values};
  }
  function createController(io) {
    const state={supported:null,data:null,draft:null,dirty:false,pending:null,message:'请先连接设备'};
    let timer=null,generation=0;
    const changed=()=>io.changed?.();
    const finish=message=>{clearTimeout(timer);timer=null;state.pending=null;state.message=message;changed();};
    function reset(){generation++;clearTimeout(timer);timer=null;Object.assign(state,{supported:null,data:null,draft:null,dirty:false,pending:null,message:'请先连接设备'});changed();}
    function request(kind) {
      if(!io.connected()||state.pending)return false;
      if(kind!=='query' && (state.supported!==true||state.data?.algorithm!==0||io.busy()))return false;
      if(kind==='set') {const error=validate(state.draft);if(error){state.message=error;changed();return false;}}
      const command=kind==='query'?QUERY:kind==='set'?SET:RESTORE;
      const expected=kind==='set'?state.draft.map(v=>Math.fround(Number(v))):kind==='restore'?state.data.defaults.slice():null;
      const p=state.pending={kind,command,expected,seq:io.nextSeq()&255,acked:false,pages:{},generation};
      state.message=kind==='query'?'正在读取参数…':kind==='set'?'正在保存参数…':'正在恢复默认参数…';changed();
      timer=setTimeout(()=>{if(state.pending===p)finish(kind==='query'?'读取超时，请重试':'保存未确认，请读取设备参数核对');},4000);
      Promise.resolve(io.send(command,kind==='set'?encode(expected):[])).then(ok=>{if(!ok&&state.pending===p)finish('参数指令发送失败');},()=>{if(state.pending===p)finish('参数指令发送失败');});
      return true;
    }
    function complete(p) {
      if(state.pending!==p||!p.pages[0]||!p.pages[1]||!p.pages[2]||(p.kind!=='query'&&!p.acked))return;
      const saved=p.pages[0],defaults=p.pages[1],active=p.pages[2];
      if([defaults,active].some(page=>page.custom!==saved.custom||page.restartRequired!==saved.restartRequired||page.algorithm!==saved.algorithm)) {
        finish('参数回读状态不一致，请重新读取');return;
      }
      state.supported=true;
      state.data={saved:saved.values,defaults:defaults.values,active:active.values,custom:saved.custom,restartRequired:saved.restartRequired,algorithm:saved.algorithm};
      if(p.kind!=='query'&&!equal(saved.values,p.expected)) {finish('参数回读不一致，保存未确认');return;}
      if(p.kind!=='query'||!state.dirty){state.draft=saved.values.map(formatFloat);state.dirty=false;}
      const label=p.kind==='restore'?'已恢复默认参数':p.kind==='set'?'参数已保存':'参数已读取';
      const restart=p.kind!=='query'&&saved.restartRequired;
      finish(label+(restart?'，正在重启并重连…':saved.restartRequired?'，重启后生效':''));
      if(restart)Promise.resolve().then(()=>p.generation===generation&&io.restart?.()).then(ok=>{
        if(!ok&&p.generation===generation){state.message='参数已保存，请在设备空闲时重启使其生效';changed();}
      },()=>{if(p.generation===generation){state.message='参数已保存，重启未完成，请重启设备';changed();}});
    }
    function onAck(cmd,status,seq) {
      const p=state.pending;if(!p||p.command!==cmd||p.seq!==seq)return;
      if(status===1){state.supported=false;finish('当前固件不支持 VQF 调参，请升级固件');return;}
      if(status!==0){finish(status===2?'设备拒绝参数，请检查输入范围':'保存失败，原参数保留；请确认设备空闲后重试');return;}
      p.acked=true;complete(p);
    }
    function onState(data,seq) {
      const p=state.pending;if(!p||p.seq!==seq||data.type!=='vqfTuning')return;
      p.pages[data.page]=data;complete(p);
    }
    function choose(index,value) {
      if(state.pending||!state.draft||index<0||index>=PARAMS.length)return;
      state.draft[index]=value;state.dirty=!equal(state.draft.map(Number),state.data.saved);changed();
    }
    function discard(){if(state.pending||!state.data)return;state.draft=state.data.saved.map(formatFloat);state.dirty=false;state.message='已撤销未保存的修改';changed();}
    return {state,reset,query:()=>request('query'),apply:()=>request('set'),restore:()=>request('restore'),choose,discard,onAck,onState};
  }
  function createView(controller,context,doc=document) {
    const $=id=>doc.getElementById(id),grid=$('vqfTuningFields');
    const fmt=formatFloat;
    for(const group of ['滤波','零偏','静止检测','磁场判定']) {
      const section=doc.createElement('fieldset');section.className='tuning-group';
      const legend=doc.createElement('legend');legend.textContent=group;section.append(legend);
      PARAMS.forEach((p,i)=>{if(p[6]!==group)return;
        const row=doc.createElement('div');row.className='tuning-row';
        row.innerHTML=`<label for="vqfParam${i}">${p[1]}<small>${p[0]}</small></label><div class="tuning-input"><input id="vqfParam${i}" type="number" min="${p[3]}" max="${p[4]}" step="${p[5]}" inputmode="decimal" disabled><span>${p[2]}</span></div><div class="tuning-values"><span>当前 <b data-active="${i}">--</b></span><span>默认 <b data-default="${i}">--</b></span></div><p>${p[7]}</p>`;
        row.querySelector('input').oninput=e=>controller.choose(i,e.target.value);section.append(row);
      });grid.append(section);
    }
    function render() {
      const s=controller.state,c=context(),busy=!!s.pending||c.busy;
      const disabled=!c.connected||busy||s.supported!==true||s.data?.algorithm!==0;
      PARAMS.forEach((_,i)=>{const input=$('vqfParam'+i);input.disabled=disabled;
        if(s.draft&&doc.activeElement!==input&&input.value!==s.draft[i])input.value=s.draft[i];
        grid.querySelector(`[data-active="${i}"]`).textContent=s.data?fmt(s.data.active[i]):'--';
        grid.querySelector(`[data-default="${i}"]`).textContent=s.data?fmt(s.data.defaults[i]):'--';
      });
      $('vqfTuningApply').disabled=disabled||!s.dirty||!!validate(s.draft);
      $('vqfTuningRestore').disabled=disabled;
      $('vqfTuningRead').disabled=!c.connected||busy;
      $('vqfTuningDiscard').disabled=busy||!s.dirty;
      $('vqfTuningState').textContent=s.data?(s.data.algorithm===1?'当前 EKF · 切换 VQF 后可调参':s.data.restartRequired?'已保存 · 等待重启生效':s.data.custom?'自定义参数':'档位 / 静置初始化参数'):'--';
      $('vqfTuningMessage').textContent=!c.connected?'请先连接设备':s.pending?s.message:c.busy?'请先结束其他操作或保存、撤销其他配置修改':validate(s.draft)&&s.dirty?validate(s.draft):s.message;
      $('vqfTuningDirty').hidden=!s.dirty;
    }
    $('vqfTuningRead').onclick=()=>controller.query();$('vqfTuningApply').onclick=()=>controller.apply();
    $('vqfTuningRestore').onclick=()=>controller.restore();$('vqfTuningDiscard').onclick=()=>controller.discard();
    return {render};
  }
  globalThis.GyroVqfTuning={QUERY,SET,RESTORE,MSG,PARAMS,validate,encode,decode,equal,createController,createView};
})();
