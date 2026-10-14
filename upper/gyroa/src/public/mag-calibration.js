(() => {
  'use strict';
  const RAW=0x47, RAW_MSG=0x48, QUERY=0x49, STATE_MSG=0x4a, SET=0x4b, RESTORE=0x4c;
  const norm=v=>Math.hypot(...v), dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);
  const OUTLIER_ERROR=.10, MAX_REJECTED=.05;
  const apply=(c,b)=>c.matrix.map(row=>dot(row,b.map((x,i)=>x-c.offset[i])));
  const quantile=(a,p)=>{const b=[...a].sort((x,y)=>x-y);return b[Math.floor((b.length-1)*p)];};
  function solve(A,b) {
    const n=b.length,m=A.map((r,i)=>[...r,b[i]]);
    for(let k=0;k<n;k++) {
      let j=k;for(let i=k+1;i<n;i++)if(Math.abs(m[i][k])>Math.abs(m[j][k]))j=i;
      if(Math.abs(m[j][k])<1e-9)throw Error('转动覆盖不足，无法拟合');
      [m[k],m[j]]=[m[j],m[k]];const v=m[k][k];for(let i=k;i<=n;i++)m[k][i]/=v;
      for(let r=0;r<n;r++)if(r!==k){const f=m[r][k];for(let i=k;i<=n;i++)m[r][i]-=f*m[k][i];}
    }
    return m.map(r=>r[n]);
  }
  function eigen(input) {
    const a=input.map(r=>[...r]),v=[[1,0,0],[0,1,0],[0,0,1]];
    for(let n=0;n<40;n++) {
      let p=0,q=1;for(const [i,j] of [[0,2],[1,2]])if(Math.abs(a[i][j])>Math.abs(a[p][q])){p=i;q=j;}
      if(Math.abs(a[p][q])<1e-12)break;
      const phi=.5*Math.atan2(2*a[p][q],a[q][q]-a[p][p]),c=Math.cos(phi),s=Math.sin(phi);
      const app=a[p][p],aqq=a[q][q],apq=a[p][q];
      for(let i=0;i<3;i++)if(i!==p && i!==q){const x=a[i][p],y=a[i][q];a[i][p]=a[p][i]=c*x-s*y;a[i][q]=a[q][i]=s*x+c*y;}
      a[p][p]=c*c*app-2*s*c*apq+s*s*aqq;a[q][q]=s*s*app+2*s*c*apq+c*c*aqq;a[p][q]=a[q][p]=0;
      for(let i=0;i<3;i++){const x=v[i][p],y=v[i][q];v[i][p]=c*x-s*y;v[i][q]=s*x+c*y;}
    }
    return {values:[a[0][0],a[1][1],a[2][2]],vectors:v};
  }
  function wireValid(c) {
    if(!c || !Array.isArray(c.offset) || c.offset.length!==3 || !Array.isArray(c.matrix) || c.matrix.length!==3 ||
       !c.matrix.every(r=>Array.isArray(r) && r.length===3) || ![...c.offset,...c.matrix.flat()].every(Number.isFinite) || c.offset.some(x=>Math.abs(x)>500))return false;
    for(let i=0;i<3;i++)for(let j=0;j<3;j++)if(Math.abs(c.matrix[i][j])>5 || Math.abs(c.matrix[i][j]-c.matrix[j][i])>1e-4)return false;
    const m=c.matrix,minor=m[0][0]*m[1][1]-m[0][1]*m[1][0],det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    return m.every((r,i)=>r[i]>=.2 && r[i]<=5) && minor>.01 && det>.01;
  }
  function valid(c) {
    if(!wireValid(c))return false;
    const e=eigen(c.matrix).values;
    return Math.min(...e)>=.2 && Math.max(...e)<=5 && Math.max(...e)/Math.min(...e)<=10;
  }
  function coverage(points,c) {
    if(!points.length)return {bins:0,total:32,percent:0,minEigen:0};
    const origin=c?.offset || [0,1,2].map(i=>(Math.max(...points.map(p=>p.b[i]))+Math.min(...points.map(p=>p.b[i])))/2);
    const counts=Array(32).fill(0),cov=Array.from({length:3},()=>[0,0,0]);
    for(const p of points){const x=c?apply(c,p.b):p.b.map((v,i)=>v-origin[i]),n=norm(x);if(n<1e-6)continue;
      const u=x.map(v=>v/n),z=Math.min(3,Math.floor((u[2]+1)*2)),theta=(Math.atan2(u[1],u[0])+2*Math.PI)%(2*Math.PI),az=Math.min(7,Math.floor(theta*4/Math.PI));counts[z*8+az]++;
      for(let i=0;i<3;i++)for(let j=0;j<3;j++)cov[i][j]+=u[i]*u[j]/points.length;
    }
    const bins=counts.filter(n=>n>=3).length;
    return {bins,total:32,percent:Math.round(bins/32*100),minEigen:Math.min(...eigen(cov).values)};
  }
  function fitCore(points,weights) {
    const origin=[0,1,2].map(i=>(Math.max(...points.map(p=>p.b[i]))+Math.min(...points.map(p=>p.b[i])))/2);
    const scale=Math.max(...points.map(p=>norm(p.b.map((x,i)=>x-origin[i]))));if(scale<5)throw Error('转动范围不足');
    const A=Array.from({length:9},()=>Array(9).fill(0)),b=Array(9).fill(0);
    points.forEach((p,n)=>{const [x,y,z]=p.b.map((x,i)=>(x-origin[i])/scale),f=[x*x,y*y,z*z,2*x*y,2*x*z,2*y*z,x,y,z],w=weights?.[n]??1;
      for(let i=0;i<9;i++){b[i]+=w*f[i];for(let j=0;j<9;j++)A[i][j]+=w*f[i]*f[j];}});
    const x=solve(A,b),Q=[[x[0],x[3],x[4]],[x[3],x[1],x[5]],[x[4],x[5],x[2]]],center=solve(Q,x.slice(6).map(v=>-.5*v));
    const k=1+dot(center,Q.map(row=>dot(row,center))),e=eigen(Q.map(row=>row.map(v=>v/k)));
    if(k<=0 || e.values.some(v=>v<=0))throw Error('数据不能形成有效椭球，请扩大三轴转动范围');
    const d=Math.pow(e.values.reduce((p,v)=>p*v,1),1/6),matrix=Array.from({length:3},(_,i)=>Array.from({length:3},(_,j)=>e.values.reduce((s,v,k)=>s+e.vectors[i][k]*Math.sqrt(v)/d*e.vectors[j][k],0)));
    const c={offset:center.map((v,i)=>origin[i]+scale*v),matrix,radius:scale/d};
    if(!valid(c) || c.radius<10 || c.radius>100)throw Error('拟合参数异常，请检查磁干扰或重新采集');
    return c;
  }
  function quality(points,c,tilt=false) {
    const lengths=points.map(p=>norm(apply(c,p.b))),radius=quantile(lengths,.5),errors=lengths.map(n=>Math.abs(n/radius-1)),cov=coverage(points,c);
    const rms=Math.sqrt(errors.reduce((s,v)=>s+v*v,0)/errors.length),p95=quantile(errors,.95),bad=errors.filter(v=>v>.1).length/errors.length;
    let dipP95=null;
    if(tilt){const dips=points.map(p=>{const m=apply(c,p.b),[w,x,y,z]=p.q,b=[m[1],m[0],-m[2]],earthZ=2*(x*z-w*y)*b[0]+2*(y*z+w*x)*b[1]+(1-2*(x*x+y*y))*b[2];return -Math.asin(Math.max(-1,Math.min(1,earthZ/norm(b))))*180/Math.PI;});const med=quantile(dips,.5);dipP95=quantile(dips.map(d=>Math.abs(d-med)),.95);}
    return {...cov,radius,rms,p95,bad,dipP95};
  }
  const acceptable=q=>q.rms<=.03 && q.p95<=.05 && q.bad<=.05;
  const qualityText=q=>`RMS ${(q.rms*100).toFixed(2)}% / P95 ${(q.p95*100).toFixed(2)}% / 超过10%误差 ${(q.bad*100).toFixed(1)}%；要求 ≤3% / ≤5% / ≤5%`;
  function qualityError(message,q,diagnostics) {const e=Error(message);e.quality=q;e.diagnostics=diagnostics;return e;}
  function robustFit(points) {
    let c=fitCore(points);
    for(let n=0;n<2;n++){const lengths=points.map(p=>norm(apply(c,p.b))),r=quantile(lengths,.5),weights=lengths.map(v=>{const e=Math.abs(v/r-1);return e<.03?1:Math.max(.05,.03/e);});c=fitCore(points,weights);}
    return c;
  }
  function fit(points) {
    if(points.length<600)throw Error('至少采集 600 个不同磁样本，请继续慢速三轴转动');
    const train=points.filter((_,i)=>i%5!==0),held=points.filter((_,i)=>i%5===0);
    let c=robustFit(train),all=quality(points,c),check=quality(held,c);
    const diagnostics={totalSamples:points.length,retainedSamples:points.length,rejectedSamples:0,rejectedFraction:0,
      rejectedIndices:[],outlierError:OUTLIER_ERROR,maxRejectedFraction:MAX_REJECTED,rawQuality:check};
    if(all.bins<24 || all.minEigen<.12)throw qualityError(`方向覆盖不足（${all.bins}/32），请多做俯仰、侧倾和翻面`,check,diagnostics);
    if(!acceptable(check)) {
      // Classify once with the training-only model and its median field scale.
      // Held-out values never change the model, threshold, or rejection mask.
      // A small contaminated interval can yield a candidate, but every new
      // independent verification sample must still pass the original gates.
      const radius=quantile(train.map(p=>norm(apply(c,p.b))),.5);
      const keep=points.map(p=>Math.abs(norm(apply(c,p.b))/radius-1)<=OUTLIER_ERROR);
      diagnostics.rejectedIndices=keep.flatMap((v,i)=>v?[]:[i]);
      diagnostics.rejectedSamples=diagnostics.rejectedIndices.length;
      diagnostics.retainedSamples=points.length-diagnostics.rejectedSamples;
      diagnostics.rejectedFraction=diagnostics.rejectedSamples/points.length;
      const keptTrain=points.filter((_,i)=>i%5!==0 && keep[i]),keptHeld=points.filter((_,i)=>i%5===0 && keep[i]);
      const trainRejected=(train.length-keptTrain.length)/train.length,heldRejected=(held.length-keptHeld.length)/held.length;
      diagnostics.trainRejectedFraction=trainRejected;diagnostics.heldRejectedFraction=heldRejected;
      if(diagnostics.rejectedFraction>MAX_REJECTED || trainRejected>MAX_REJECTED || heldRejected>MAX_REJECTED)
        throw qualityError(`异常样本比例过高（采集 ${(diagnostics.rejectedFraction*100).toFixed(1)}% / 拟合 ${(trainRejected*100).toFixed(1)}% / 留出 ${(heldRejected*100).toFixed(1)}%，最多5%）；${qualityText(check)}。请稳定磁场后重新采集`,check,diagnostics);
      if(!diagnostics.rejectedSamples)throw qualityError(`留出样本误差过大（${qualityText(check)}），请慢速完整转动并重新采集`,check,diagnostics);
      if(diagnostics.retainedSamples<600)throw qualityError(`剔除 ${diagnostics.rejectedSamples} 个异常样本后仅剩 ${diagnostics.retainedSamples} 个，至少需要600个有效样本，请重新采集更多数据`,check,diagnostics);
      c=robustFit(keptTrain);all=quality(points.filter((_,i)=>keep[i]),c);check=quality(keptHeld,c);
      diagnostics.rawQuality=quality(held,c);
      if(all.bins<24 || all.minEigen<.12)throw qualityError(`剔除异常样本后方向覆盖不足（${all.bins}/32），请重新采集并多做俯仰、侧倾和翻面`,check,diagnostics);
      if(!acceptable(check) || !acceptable(all))throw qualityError(`留出样本误差过大（${qualityText(check)}），请慢速完整转动并重新采集`,check,diagnostics);
    }
    return {cal:c,quality:check,coverage:all,diagnostics};
  }
  function verify(points,c) {
    if(points.length<300)throw Error('验证至少需要 300 个不同磁样本');
    const q=quality(points,c,true);
    if(Math.abs(q.radius/c.radius-1)>.1)throw qualityError('验证磁场强度与采集时不一致，请重新采集',q);
    if(q.bins<16 || q.minEigen<.12)throw qualityError(`验证覆盖不足（${q.bins}/32），请覆盖更多朝向`,q);
    if(!acceptable(q))throw qualityError(`新数据磁场强度不一致（${qualityText(q)}），请重新校准`,q);
    if(q.dipP95>8)throw qualityError('磁倾角不一致，请检查轴向映射、转动速度或磁干扰',q);
    return q;
  }
  function decode(id,p) {
    const len=id===RAW_MSG?40:52;if(p.length!==len)return {type:'badLength',id,length:p.length,expected:len};
    if(p[0]!==1 || p[1]>1 || p[2] || p[3])return {type:'unknown',id};
    const v=new DataView(p.buffer,p.byteOffset,p.byteLength),floats=Array.from({length:(len-12)/4},(_,i)=>v.getFloat32(12+4*i,true));
    if(id===RAW_MSG){if(!floats.every(Number.isFinite) || Math.abs(norm(floats.slice(3))-1)>.01)return {type:'unknown',id};return {type:'magCalRaw',valid:!!p[1],sample:v.getUint32(4,true),ms:v.getUint32(8,true),b:floats.slice(0,3),q:floats.slice(3)};}
    const values=Array.from({length:12},(_,i)=>v.getFloat32(4+4*i,true)),c={offset:values.slice(0,3),matrix:[values.slice(3,6),values.slice(6,9),values.slice(9,12)]};
    if(!wireValid(c))return {type:'unknown',id};return {type:'magCalState',valid:!!p[1],cal:c};
  }
  function encode(c) {if(!valid(c))return null;const p=new Uint8Array(52),v=new DataView(p.buffer);p.set([1,1,0,0]);[...c.offset,...c.matrix.flat()].forEach((x,i)=>v.setFloat32(4+i*4,x,true));return p;}
  const same=(a,b)=>[...a.offset,...a.matrix.flat()].every((x,i)=>Math.fround(x)===[...b.offset,...b.matrix.flat()][i]);
  function createController(io,clock=()=>performance.now()) {
    const state={supported:null,queried:false,data:null,pending:null,phase:'idle',samples:[],validation:[],candidate:null,verified:false,message:'请先连接设备',quality:null,fitDiagnostics:null};
    let timer=null,rawPending=null,lastPoll=-Infinity,lastSample=null,lastMs=null,startAt=0;
    const busy=()=>!!state.pending || ['collect','validate'].includes(state.phase),changed=()=>io.changed?.();
    function finish(message){clearTimeout(timer);timer=null;state.pending=null;state.message=message;changed();}
    function reset(){clearTimeout(timer);timer=null;rawPending=null;lastSample=lastMs=null;Object.assign(state,{supported:null,queried:false,data:null,pending:null,phase:'idle',samples:[],validation:[],candidate:null,verified:false,quality:null,fitDiagnostics:null,message:'请先连接设备'});changed();}
    function request(kind) {
      const c=io.context();if(busy() || !c.connected || c.busy)return false;
      if(kind==='save' && (!state.verified || !state.candidate))return false;
      const cmd={read:QUERY,save:SET,restore:RESTORE}[kind],payload=kind==='save'?encode(state.candidate.cal):[];
      if(!payload)return false;
      if(kind==='read')state.queried=true;
      const req=state.pending={kind,cmd,seq:io.nextSeq(),acked:false};state.message=kind==='read'?'正在读取…':'正在保存…';changed();
      timer=setTimeout(()=>{if(state.pending===req)finish('设备未确认结果，请重新读取核对');},3000);
      Promise.resolve(io.send(cmd,payload)).then(ok=>{if(!ok && state.pending===req)finish('指令发送失败');},()=>{if(state.pending===req)finish('指令发送失败');});return true;
    }
    function start(validation=false) {
      const c=io.context();if(busy() || !c.connected || c.busy || state.supported!==true || ![1,2].includes(c.mode) || (validation && !state.candidate))return false;
      state.quality=null;
      if(validation){state.validation=[];state.verified=false;}else{state.samples=[];state.validation=[];state.candidate=null;state.fitDiagnostics=null;state.verified=false;}
      state.phase=validation?'validate':'collect';rawPending=null;lastSample=lastMs=null;lastPoll=-Infinity;startAt=clock();state.message=validation?'用新一轮慢速三轴转动验证':'请慢速转动、侧倾和翻面，覆盖各个朝向';changed();return true;
    }
    function stop(cancel=false) {
      if(!['collect','validate'].includes(state.phase))return;
      const phase=state.phase;state.phase='idle';rawPending=null;
      if(cancel){state.message='采集已停止，未修改板卡';changed();return;}
      try{if(phase==='collect'){state.candidate=fit(state.samples);state.fitDiagnostics=state.candidate.diagnostics;state.phase='fitted';
          const d=state.fitDiagnostics;state.message=d.rejectedSamples?`拟合通过：剔除 ${d.rejectedSamples}/${d.totalSamples} 个异常样本（${(d.rejectedFraction*100).toFixed(1)}%），请用新一轮转动验证`:'拟合通过，请用新一轮转动验证';}
        else{state.quality=verify(state.validation,state.candidate.cal);state.verified=true;state.phase='verified';state.message='验证通过，可以保存到板卡';}}
      catch(e){state.message=e.message;state.quality=e.quality || null;if(e.diagnostics)state.fitDiagnostics=e.diagnostics;state.verified=false;state.phase=phase==='validate'?'fitted':'idle';}changed();
    }
    function tick() {
      const c=io.context();if(!c.connected){if(state.data || busy())reset();return;}
      if(!['collect','validate'].includes(state.phase))return;
      if(c.busy || ![1,2].includes(c.mode) || c.hidden){stop(true);state.message='连接、页面或设备状态改变，采集已停止';changed();return;}
      const now=clock();if(now-startAt>300000){stop(true);state.message='采集已满 5 分钟，请拟合或重新采集';changed();return;}
      if(rawPending){if(now-rawPending.at<1000)return;stop(true);state.message='磁力计数据超时，采集已停止';changed();return;}
      if(now-lastPoll<50)return;lastPoll=now;const req=rawPending={seq:io.nextSeq(),at:now};
      Promise.resolve(io.send(RAW,[])).then(ok=>{if(!ok && rawPending===req){stop(true);state.message='磁采集请求发送失败';changed();}},()=>{if(rawPending===req){stop(true);state.message='磁采集请求发送失败';changed();}});
    }
    function onRaw(d,seq) {
      if(!rawPending || rawPending.seq!==seq)return;rawPending=null;if(!d.valid){stop(true);state.message='没有有效磁力计采样，请检查九轴模式';changed();return;}
      if(d.sample===lastSample)return;
      if(lastSample!==null && ((d.sample-lastSample)>>>0)>1000 || lastMs!==null && ((d.ms-lastMs)>>>0)>2000){stop(true);state.message='磁采样中断或设备重启，请重新采集';changed();return;}
      lastSample=d.sample;lastMs=d.ms;
      if(norm(d.b)<1 || norm(d.b)>2000)return;
      const target=state.phase==='collect'?state.samples:state.phase==='validate'?state.validation:null;if(!target)return;
      if(target.length>=6000){stop(false);return;}target.push({b:[...d.b],q:[...d.q],sample:d.sample,ms:d.ms});changed();
    }
    function onAck(cmd,status,seq) {
      if(cmd===RAW && rawPending?.seq===seq){if(status){rawPending=null;stop(true);if(status===1)state.supported=false;state.message='固件不支持原始磁采集，请升级 APP';changed();}return;}
      const p=state.pending;if(!p || p.seq!==seq || p.cmd!==cmd)return;
      if(status===1){state.supported=false;finish('当前固件不支持磁校准，请升级 APP');return;}
      if(status){finish('磁校准保存或读取失败，原校准未确认改变');return;}p.acked=true;
    }
    function onState(d,seq) {
      const p=state.pending;if(!p || p.seq!==seq || p.kind!=='read' && !p.acked)return;
      state.supported=true;state.data=d;
      if(p.kind==='save' && (!d.valid || !same(state.candidate.cal,d.cal))){finish('回读与拟合参数不一致，保存未确认');return;}
      if(p.kind==='restore' && d.valid){finish('恢复结果未确认');return;}
      if(p.kind!=='read'){state.candidate=null;state.verified=false;state.phase='idle';}
      finish(p.kind==='save'?'已保存到板卡，断电保留；磁场参考重新建立中':p.kind==='restore'?'已恢复固件试用系数':d.valid?'板卡使用已保存的磁校准':'板卡使用固件试用系数');
    }
    return {state,busy,reset,tick,start,stop,read:()=>request('read'),save:()=>request('save'),restore:()=>request('restore'),onRaw,onState,onAck};
  }
  globalThis.GyroMagCal={RAW,RAW_MSG,QUERY,STATE_MSG,SET,RESTORE,solve,eigen,apply,valid,coverage,fit,quality,verify,decode,encode,createController};
})();
