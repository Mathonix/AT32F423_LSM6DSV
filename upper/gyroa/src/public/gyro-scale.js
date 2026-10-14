(() => {
  'use strict';
  const QUERY=0x5c,SET=0x5d,RESTORE=0x5e,MSG=0x5f;
  const equal=(a,b)=>a?.length===b?.length&&a.every((v,i)=>Math.fround(Number(v))===Math.fround(Number(b[i])));
  function valid(a){return Array.isArray(a)&&a.length===3&&a.every(v=>String(v).trim()!==''&&Number.isFinite(Number(v))&&Math.fround(Number(v))>=Math.fround(.95)&&Math.fround(Number(v))<=Math.fround(1.05));}
  function encode(a){if(!valid(a))throw Error('比例系数须在 0.95～1.05 之间');const p=new Uint8Array(16),v=new DataView(p.buffer);p[0]=1;a.forEach((x,i)=>v.setFloat32(4+4*i,Number(x),true));return p;}
  function decode(p){
    if(p.length!==36)return {type:'badLength',id:MSG};
    const v=new DataView(p.buffer,p.byteOffset,p.length),active=[4,8,12].map(o=>v.getFloat32(o,true)),saved=[16,20,24].map(o=>v.getFloat32(o,true));
    if(p[0]!==1||(p[1]&~1)||p[2]||p[3]||!valid(active)||!valid(saved))return {type:'unknown',id:MSG};
    return {type:'gyroScale',active,saved,restartRequired:!!p[1],skips:v.getUint32(28,true),clipped:v.getUint32(32,true)};
  }
  function factor(measurementFactor,actual,measured){
    // The baseline belongs to the measurement, never to the latest device
    // readback. Uncompensated manual measurements use a baseline of 1.
    if(!valid([measurementFactor,1,1]))throw Error('测量时比例系数须在 0.95～1.05 之间');
    if(!Number.isFinite(actual)||!Number.isFinite(measured)||actual<360||actual>36000||Math.abs(measured)<360*.9)throw Error('请完成至少 1 圈的已知转动');
    const k=measurementFactor*actual/Math.abs(measured);
    if(!valid([k,1,1]))throw Error('计算系数超出 0.95～1.05，请检查圈数、转轴和起止位置');
    return Math.fround(k);
  }
  function createController(io){
    const state={supported:null,data:null,draft:null,dirty:false,pending:null,message:'请先连接设备'};let timer,generation=0;
    const changed=()=>io.changed?.();
    function finish(message){clearTimeout(timer);state.pending=null;state.message=message;changed();}
    function reset(){generation++;clearTimeout(timer);Object.assign(state,{supported:null,data:null,draft:null,dirty:false,pending:null,message:'请先连接设备'});changed();}
    function request(kind){
      if(!io.connected()||state.pending)return false;
      if(kind!=='query'&&(state.supported!==true||io.busy()||!valid(state.draft)))return false;
      const expected=kind==='restore'?[1,1,1]:kind==='set'?state.draft.map(x=>Math.fround(Number(x))):null;
      const command=kind==='query'?QUERY:kind==='set'?SET:RESTORE,p=state.pending={kind,command,seq:io.nextSeq()&255,expected,acked:false,reply:null,generation};
      state.message=kind==='query'?'正在读取比例系数…':'正在保存比例系数…';changed();
      timer=setTimeout(()=>{if(state.pending===p)finish('操作未确认，请重新读取设备核对');},4000);
      Promise.resolve(io.send(command,kind==='set'?encode(expected):[])).then(ok=>{if(!ok&&state.pending===p)finish('比例系数指令发送失败');},()=>{if(state.pending===p)finish('比例系数指令发送失败');});return true;
    }
    function complete(p){
      if(state.pending!==p||!p.reply||(p.kind!=='query'&&!p.acked))return;
      state.supported=true;state.data=p.reply;
      if(p.expected&&!equal(p.expected,p.reply.saved)){finish('比例系数回读不一致，保存未确认');return;}
      if(p.kind!=='query'||!state.dirty){state.draft=p.reply.saved.slice();state.dirty=false;}
      const restart=p.kind!=='query'&&p.reply.restartRequired;
      finish(p.kind==='query'?'比例系数已读取':restart?'比例系数已保存，正在重启并重连…':'比例系数已保存并生效');
      if(restart)Promise.resolve().then(()=>p.generation===generation&&io.restart?.()).then(ok=>{if(!ok&&p.generation===generation){state.message='比例系数已保存，请重启设备使其生效';changed();}},()=>{if(p.generation===generation){state.message='比例系数已保存，重启未完成，请重启设备';changed();}});
    }
    function onAck(cmd,status,seq){const p=state.pending;if(!p||p.command!==cmd||p.seq!==seq)return;
      if(status){if(status===1)state.supported=false;finish(status===1?'当前固件不支持比例校准，请升级到 20261011b 或更新版本':'设备拒绝保存，原系数保留；请结束其他操作后重试');return;}p.acked=true;complete(p);}
    function onState(m,seq){const p=state.pending;if(!p||p.seq!==seq)return;p.reply=m;complete(p);}
    function choose(i,v){if(state.pending||!state.draft||i<0||i>2)return;state.draft[i]=v;state.dirty=!equal(state.draft,state.data.saved);changed();}
    function discard(){if(state.pending||!state.data)return;state.draft=state.data.saved.slice();state.dirty=false;state.message='已撤销未保存的系数';changed();}
    return {state,reset,query:()=>request('query'),apply:()=>request('set'),restore:()=>request('restore'),choose,discard,onAck,onState};
  }
  function createCapture(){
    let s={active:false,phase:'idle',message:'选择转轴和圈数后准备采集'},quiet=[],post=[];
    const fail=message=>{s.active=false;s.phase='failed';s.message=message;};
    function start(axis,circles,token,range){
      if(!Number.isInteger(axis)||axis<0||axis>2||!Number.isInteger(circles)||circles<1||circles>100||!Number.isFinite(range)||range<125)throw Error('请选择转轴和 1～100 圈');
      s={active:true,phase:'pre',message:'保持静止 2 秒，测量残余零偏…',axis,target:circles*360,token,range,last:null,arrival:null,frames:0,angle:0,travel:0,other:0,duration:0,readyBias:0,preBias:0,result:null};quiet=[];post=[];
    }
    function begin(){if(s.phase!=='ready'||!s.active)return false;s.preBias=s.readyBias;s.phase='turn';s.angle=s.travel=s.other=s.duration=0;s.message='按同一方向转满指定圈数，回到起始标记后点击结束转动';return true;}
    function end(){if(s.phase!=='turn'||!s.active)return false;s.phase='post';s.message='放稳并保持静止 2 秒，核对零偏…';post=[];return true;}
    function ingest(m){
      if(!s.active||m.token!==s.token)return;
      const {gyro,acc,tsUs,sampleSeq,periodUs,at}=m;
      if(!Array.isArray(gyro)||gyro.length!==3||!Array.isArray(acc)||acc.length!==3||![...gyro,...acc,at].every(Number.isFinite)||!Number.isInteger(sampleSeq)||sampleSeq<0||sampleSeq>0xffffffff||!Number.isInteger(tsUs)||tsUs<0||tsUs>0xffffffff||periodUs!==5000){fail('采样数据无效，请重新采集');return;}
      if(s.last!==null){
        const gap=(tsUs-s.last.tsUs)>>>0;
        const lost=((sampleSeq-s.last.sampleSeq)>>>0)!==1,clockGap=Math.abs(gap-periodUs)>1000;
        if((lost||clockGap)&&(s.phase==='pre'||s.phase==='ready')){
          // No angle has been integrated yet. Discard the rest window and
          // require a new complete contiguous window before enabling motion.
          quiet=[];s.phase='pre';s.message='采样暂不连续，重新等待 2 秒静止';
        }else{
          if(lost){fail(`采样丢帧（${s.last.sampleSeq} → ${sampleSeq}），请重新采集`);return;}
          if(clockGap){fail(`采样时间不连续（${gap} μs），请重新采集`);return;}
        }
        if(at-s.arrival>2000||at<s.arrival){fail('接收中断，请重新采集');return;}
      }
      s.last={sampleSeq,tsUs};s.arrival=at;s.frames++;
      if(gyro.some(x=>Math.abs(x)>=s.range*.95)){fail('接近陀螺仪量程，请减慢转速后重采');return;}
      const dt=periodUs/1e6,rest=Math.hypot(...gyro)<.5&&Math.abs(Math.hypot(...acc)-9.80665)<.5;
      if(s.phase==='pre'||s.phase==='ready'){
        if(!rest){quiet=[];s.phase='pre';s.message='准备期间检测到运动，请放稳 2 秒';return;}
        quiet.push(gyro[s.axis]);if(quiet.length>400)quiet.shift();
        if(quiet.length===400){s.readyBias=quiet.reduce((a,b)=>a+b,0)/quiet.length;s.phase='ready';s.message='静止采集完成。点击开始转动，再转满指定圈数';}return;
      }
      s.angle+=gyro[s.axis]*dt;s.travel+=Math.abs(gyro[s.axis]-s.preBias)*dt;s.duration+=dt;
      s.other+=Math.hypot(...gyro.filter((_,i)=>i!==s.axis))*dt;
      if(s.duration>600){fail('采集超过 10 分钟，请重新采集');return;}
      if(s.phase==='post'){
        if(!rest){post=[];s.message='结束后仍在运动，请放稳 2 秒';return;}
        post.push(gyro[s.axis]);if(post.length<400)return;
        const postBias=post.reduce((a,b)=>a+b,0)/post.length;
        if(Math.abs(postBias-s.preBias)>.1){fail('采集前后零偏变化过大，请热机后重新采集');return;}
        const measured=s.angle-(s.preBias+postBias)*.5*s.duration;
        if(Math.abs(measured)<s.target*.9||Math.abs(measured)>s.target*1.1){fail('转动角度与圈数不符，请检查圈数和起止标记');return;}
        if(s.other>Math.abs(measured)*.05||s.travel>Math.abs(measured)*1.03){fail('转轴不稳或中途反转，请固定转轴并单向重采');return;}
        s.result={measured,target:s.target,axis:s.axis,preBias:s.preBias,postBias,duration:s.duration};s.active=false;s.phase='done';s.message='采集完成，正在核对板卡采样状态…';
      }
    }
    return {start,begin,end,ingest,fail,state:()=>s};
  }
  globalThis.GyroScale={QUERY,SET,RESTORE,MSG,valid,equal,encode,decode,factor,createController,createCapture};
})();
