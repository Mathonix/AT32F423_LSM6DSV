(() => {
  'use strict';
  const QUERY=0x67, SET=0x68, RESTORE=0x69, MSG=0x6a;
  // Wire order matches eskf_tuning_t. Defaults are the compiled literals; a
  // connected device still answers with its own default page.
  const PARAMS=[
    ['restTau','静止零偏时间常数','s',0.5,120,0.1,'静止零偏','累计静止学习结束后的时间常数。默认 12 秒。','f'],
    ['restFastTau','启动阶段时间常数','s',0.05,30,0.01,'静止零偏','累计静止还没满启动时间时用这个更快的常数。默认 1 秒，不能大于静止零偏时间常数。','f'],
    ['restBoot','启动学习时间','s',0,60,0.1,'静止零偏','这段时间里用启动阶段时间常数。默认 3 秒。','f'],
    ['restFloor','静止门限下限','°/s',0.02,2,0.0001,'静止零偏','证据充足时的角速度门限。默认 0.1091 °/s。窗口 122 点和回到静止的 0.6 倍比例不能改。','f'],
    ['magLock','磁北锁定时间','s',0,10,0.1,'九轴航向','磁场稳定且板子静止这么久后，一次转到磁北。默认 1 秒，静止条件不满足就重新计时。六轴不使用。','f'],
    ['magStep','航向单次修正','rad',0.05,1.5,0.01,'九轴航向','只限制航向修正的一步，默认 0.4 rad（约 23°）。重力修正仍用固件里单独的 0.4 rad 门，这个旋钮不改它。','f'],
    ['magDrop','航向丢弃门限','rad',0.05,3.14,0.01,'九轴航向','航向误差大于这个值的一次观测整次丢掉。默认 1 rad（约 57°），不能小于单次修正。','f'],
    ['magSoft','软拒绝模长','比例',0.01,0.5,0.01,'磁场拒绝','模长相对偏差达到这个比例进入软拒绝。默认 0.10。','f'],
    ['magHard','硬拒绝模长','比例',0.01,1,0.01,'磁场拒绝','不能小于软拒绝。默认 0.20。','f'],
    ['magSoftDip','软拒绝倾角','rad',0.01,0.5,0.001,'磁场拒绝','默认 0.087 rad，约 5°。','f'],
    ['magHardDip','硬拒绝倾角','rad',0.01,1.5,0.001,'磁场拒绝','不能小于软拒绝倾角。默认 0.175 rad，约 10°。','f'],
    ['tilt','重力可信倾角','rad',0.005,0.5,0.001,'九轴航向','倾角方差小于这个值，才只修航向。默认 0.05 rad。','f'],
    ['restLeave','离开静止计数','点',1,200,1,'静止零偏','连续这么多个 8 kHz 采样超过门限才离开静止。默认 8。','u'],
    ['restReturn','回到静止计数','点',1,8000,1,'静止零偏','连续这么多个采样低于 0.6 倍门限才回到静止。默认 800。0.6 倍不能改。','u'],
    ['magReject','硬拒绝次数','次',1,5000,1,'磁场拒绝','连续硬拒绝这么多次后改为慢速接受。默认 500。','u'],
    ['accelAge','加速度年龄','步',1,8000,1,'九轴航向','加速度超过这么多 8 kHz 步还没更新，就不再相信重力。默认 240 步，约 30 ms。','u'],
  ];
  const DEFAULTS=Object.freeze([12,1,3,0.1091,1.0,0.4,1,0.10,0.20,0.087,0.175,0.05].map(Math.fround).concat([8,800,500,240]));
  const integer=i=>PARAMS[i][8]==='u';
  function formatFloat(value) {
    const v=Math.fround(value);
    for(let digits=1;digits<=9;digits++) {
      const text=String(Number(v.toPrecision(digits)));
      if(Math.fround(Number(text))===v)return text;
    }
    return String(v);
  }
  const formatValue=(value,index)=>integer(index)?String(value|0):formatFloat(value);
  const wire=(value,index)=>integer(index)?Number(value)|0:Math.fround(Number(value));
  const equal=(a,b)=>a?.length===PARAMS.length && b?.length===PARAMS.length && a.every((v,i)=>wire(v,i)===wire(b[i],i));
  function validate(values) {
    if(!Array.isArray(values)||values.length!==PARAMS.length)return '参数数量不正确';
    const n=[];
    for(let i=0;i<PARAMS.length;i++) {
      const p=PARAMS[i],v=values[i];
      if(v===null||String(v).trim()===''||!Number.isFinite(Number(v)))return `${p[1]}请输入有限数值`;
      if(integer(i)&&!Number.isInteger(Number(v)))return `${p[1]}须为整数`;
      const x=wire(v,i);
      if(x<wire(p[3],i)||x>wire(p[4],i))return `${p[1]}范围为 ${p[3]}～${p[4]} ${p[2]}`;
      n.push(x);
    }
    if(n[1]>n[0])return '启动阶段时间常数不能大于静止零偏时间常数';
    if(n[6]<n[5])return '航向丢弃门限不能小于单次修正';
    if(n[8]<n[7])return '硬拒绝模长不能小于软拒绝';
    if(n[10]<n[9])return '硬拒绝倾角不能小于软拒绝倾角';
    return '';
  }
  function encode(values) {
    const error=validate(values);if(error)throw new Error(error);
    const p=new Uint8Array(60),view=new DataView(p.buffer);p[0]=1;
    values.forEach((v,i)=>{
      if(integer(i))view.setUint16(52+(i-12)*2,wire(v,i),true);
      else view.setFloat32(4+i*4,wire(v,i),true);
    });
    return [...p];
  }
  function decode(p) {
    if(p.length!==60)return {type:'badLength',id:MSG,length:p.length,expected:60};
    if(p[0]!==1||p[1]>2||p[2]>3||p[3]!==0)return {type:'unknown',id:MSG};
    const view=new DataView(p.buffer,p.byteOffset,p.byteLength);
    const values=PARAMS.map((_,i)=>integer(i)?view.getUint16(52+(i-12)*2,true):view.getFloat32(4+i*4,true));
    if(validate(values))return {type:'unknown',id:MSG};
    return {type:'eskfTuning',page:p[1],custom:!!(p[2]&1),mismatch:!!(p[2]&2),values};
  }
  function chipText(state=globalThis.GyroEskfTuning?.control?.state) {
    if(!state||(state.supported==null&&!state.data))return 'ESKF';
    if(state.supported===false)return 'ESKF · 当前固件只读';
    if(!state.data)return 'ESKF';
    return state.data.custom?'ESKF · 自定义参数':'ESKF · 默认参数';
  }
  function createController(io) {
    const state={supported:null,data:null,draft:null,dirty:false,pending:null,message:'请先连接设备'};
    let timer=null,generation=0;
    const changed=()=>io.changed?.();
    const finish=message=>{clearTimeout(timer);timer=null;state.pending=null;state.message=message;changed();};
    function reset(){generation++;clearTimeout(timer);timer=null;Object.assign(state,{supported:null,data:null,draft:null,dirty:false,pending:null,message:'请先连接设备'});changed();}
    function request(kind) {
      if(!io.connected()||state.pending)return false;
      if(kind!=='query' && (state.supported!==true||io.busy()))return false;
      if(kind==='set') {const error=validate(state.draft);if(error){state.message=error;changed();return false;}}
      const command=kind==='query'?QUERY:kind==='set'?SET:RESTORE;
      const expected=kind==='set'?state.draft.map(wire):kind==='restore'?state.data.defaults.slice():null;
      const p=state.pending={kind,command,expected,seq:io.nextSeq()&255,acked:false,pages:{},generation};
      state.message=kind==='query'?'正在读取参数…':kind==='set'?'正在保存参数…':'正在恢复默认参数…';changed();
      timer=setTimeout(()=>{if(state.pending===p)finish(kind==='query'?'读取超时，请重试':'保存未确认，请读取设备参数核对');},4000);
      Promise.resolve(io.send(command,kind==='set'?encode(expected):[])).then(ok=>{if(!ok&&state.pending===p)finish('参数指令发送失败');},()=>{if(state.pending===p)finish('参数指令发送失败');});
      return true;
    }
    function complete(p) {
      if(state.pending!==p||!p.pages[0]||!p.pages[1]||!p.pages[2]||(p.kind!=='query'&&!p.acked))return;
      const saved=p.pages[0],defaults=p.pages[1],active=p.pages[2];
      if([defaults,active].some(page=>page.custom!==saved.custom||page.mismatch!==saved.mismatch)) {
        finish('参数回读状态不一致，请重新读取');return;
      }
      state.supported=true;
      state.data={saved:saved.values,defaults:defaults.values,active:active.values,custom:saved.custom,mismatch:saved.mismatch};
      if(p.kind!=='query'&&!equal(saved.values,p.expected)) {finish('参数回读不一致，保存未确认');return;}
      if(p.kind!=='query'||!state.dirty){state.draft=saved.values.map(formatValue);state.dirty=false;}
      const warn=p.kind!=='query'&&saved.mismatch?'。已保存值和当前运行值不一致，请再读取核对':'';
      const label=p.kind==='restore'?'已恢复默认参数，立即生效':p.kind==='set'?'已保存，立即生效':'参数已读取';
      finish(label+warn);
    }
    function onAck(cmd,status,seq) {
      const p=state.pending;if(!p||p.command!==cmd||p.seq!==seq)return;
      if(status===1){state.supported=false;finish('当前固件不能修改 ESKF 参数。需要升级到带这条命令的固件后，保存才会写入板卡。');return;}
      if(status!==0){finish(status===2?'设备拒绝参数，请检查输入范围':'保存失败，原参数保留；请确认设备空闲后重试');return;}
      p.acked=true;complete(p);
    }
    function onState(data,seq) {
      const p=state.pending;if(!p||p.seq!==seq||data.type!=='eskfTuning')return;
      p.pages[data.page]=data;complete(p);
    }
    function choose(index,value) {
      if(state.pending||!state.draft||index<0||index>=PARAMS.length)return;
      state.draft[index]=value;state.dirty=!equal(state.draft.map(Number),state.data.saved);changed();
    }
    function discard(){if(state.pending||!state.data)return;state.draft=state.data.saved.map(formatValue);state.dirty=false;state.message='已撤销未保存的修改';changed();}
    return {state,reset,query:()=>request('query'),apply:()=>request('set'),restore:()=>request('restore'),choose,discard,onAck,onState};
  }
  function createView(controller,context,doc=document) {
    const $=id=>doc.getElementById(id),grid=$('eskfTuningFields');
    for(const group of ['静止零偏','九轴航向','磁场拒绝']) {
      const section=doc.createElement('fieldset');section.className='tuning-group';
      const legend=doc.createElement('legend');legend.textContent=group;section.append(legend);
      PARAMS.forEach((p,i)=>{if(p[6]!==group)return;
        const row=doc.createElement('div');row.className='tuning-row';
        row.innerHTML=`<label for="eskfParam${i}">${p[1]}<small>${p[0]}</small></label><div class="tuning-input"><input id="eskfParam${i}" type="number" min="${p[3]}" max="${p[4]}" step="${p[5]}" inputmode="decimal" disabled><span>${p[2]}</span></div><div class="tuning-values"><span>当前 <b data-eskf-active="${i}">--</b></span><span>默认 <b data-eskf-default="${i}">--</b></span></div><p>${p[7]}</p>`;
        row.querySelector('input').oninput=e=>controller.choose(i,e.target.value);section.append(row);
      });grid.append(section);
    }
    function render() {
      const s=controller.state,c=context(),busy=!!s.pending||c.busy;
      const disabled=!c.connected||busy||s.supported!==true;
      PARAMS.forEach((_,i)=>{const input=$('eskfParam'+i);input.disabled=disabled;
        if(s.draft&&doc.activeElement!==input&&input.value!==s.draft[i])input.value=s.draft[i];
        grid.querySelector(`[data-eskf-active="${i}"]`).textContent=s.data?formatValue(s.data.active[i],i):'--';
        grid.querySelector(`[data-eskf-default="${i}"]`).textContent=s.data?formatValue(s.data.defaults[i],i):'--';
      });
      $('eskfTuningApply').disabled=disabled||!s.dirty||!!validate(s.draft);
      $('eskfTuningRestore').disabled=disabled;
      $('eskfTuningRead').disabled=!c.connected||busy;
      $('eskfTuningDiscard').disabled=busy||!s.dirty;
      $('eskfTuningMessage').textContent=!c.connected?'请先连接设备':s.pending?s.message:c.busy?'请先结束其他操作或保存、撤销其他配置修改':validate(s.draft)&&s.dirty?validate(s.draft):s.message;
      $('eskfTuningDirty').hidden=!s.dirty;
      globalThis.GyroVqfTuning?.syncCards?.(doc);
    }
    $('eskfTuningRead').onclick=()=>controller.query();$('eskfTuningApply').onclick=()=>controller.apply();
    $('eskfTuningRestore').onclick=()=>controller.restore();$('eskfTuningDiscard').onclick=()=>controller.discard();
    return {render};
  }
  globalThis.GyroEskfTuning={QUERY,SET,RESTORE,MSG,PARAMS,DEFAULTS,validate,encode,decode,equal,chipText,createController,createView};
})();
