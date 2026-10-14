(function(root) {
  'use strict';
  const G=9.80665, norm=v=>Math.hypot(...v), copy=v=>[...v];
  const rotate=(R,v)=>R.map(row=>row.reduce((sum,x,i)=>sum+x*v[i],0));
  function rotation(q) {
    const [w,x,y,z]=q;
    return [[1-2*(y*y+z*z),2*(x*y-w*z),2*(x*z+w*y)],
      [2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)],
      [2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)]];
  }
  const angle=(a,b)=>2*Math.acos(Math.min(1,Math.abs(a.reduce((sum,x,i)=>sum+x*b[i],0))))*180/Math.PI;
  const block=()=>({dt:0,n:0,sum:Array(6).fill(0),square:Array(6).fill(0),q:null,gross:false});
  function decode(id,payload) {
    if(payload.length!==60) return {type:'badLength',id,length:payload.length,expected:60};
    const d=new DataView(payload.buffer,payload.byteOffset,payload.length), f=o=>d.getFloat32(o,true);
    const q=[20,24,28,32].map(f), acc=[36,40,44].map(f), gyro=[48,52,56].map(f);
    const token=d.getUint32(4,true), periodUs=d.getUint32(16,true);
    if(payload[0]!==1 || (payload[1]&~3) || d.getUint16(2,true) || !token || periodUs<2000 || periodUs>20000 ||
        ![...q,...acc,...gyro].every(Number.isFinite) || Math.abs(norm(q)-1)>.01 || norm(acc)>200 || norm(gyro)>10000)
      return {type:'unknown',id,length:payload.length};
    return {type:'inertial',token,flags:payload[1],sampleSeq:d.getUint32(8,true),tsUs:d.getUint32(12,true),periodUs,
      q:q.map(x=>x/norm(q)),acc,gyro};
  }
  function create() {
    let s;
    function start(zupt=false,{token=1,filterTau=.015}={}) {
      if(!Number.isInteger(token) || token<1 || token>0xffffffff || !Number.isFinite(filterTau) || filterTau<0 || filterTau>.1) throw Error('Invalid INS parameters');
      s={active:true,phase:'calibrating',message:'等待独立惯导数据，请保持静止 2 秒…',token,zupt,filterTau,
        p:[0,0,0],v:[0,0,0],filtered:[0,0,0],previous:[0,0,0],last:null,lastQ:null,lastGyro:null,arrival:null,
        periodUs:null,lastSeq:null,block:block(),quiet:0,count:0,originQ:null,yaw:0,gravity:G,
        sum:[0,0,0],diagnostics:null,initialResidual:null,resting:false,frames:0,quality:'等待数据',startedAt:null};
    }
    start();s.active=false;s.phase='idle';s.message='连接设备后建立原点。';
    function stop(message='已停止') {s.active=false;s.phase='stopped';s.message=message;s.quality='已停止，结果仅覆盖停止前';}
    function stationary() {
      if(!s.active || s.phase!=='tracking')return false;
      s.v.fill(0);s.previous.fill(0);s.filtered.fill(0);s.message='已按确认归零速度';return true;
    }
    function ingest(m) {
      if(!s.active || m.token!==s.token)return; // stale packets from a prior start are harmless
      const {acc,gyro,q,tsUs,sampleSeq,periodUs,flags,at}=m;
      if(!Array.isArray(q) || q.length!==4 || !Array.isArray(acc) || acc.length!==3 || !Array.isArray(gyro) || gyro.length!==3 ||
        ![...q,...acc,...gyro,at].every(Number.isFinite) || Math.abs(norm(q)-1)>.01 || norm(acc)>200 || norm(gyro)>10000 ||
        ![tsUs,sampleSeq,periodUs].every(Number.isInteger) || tsUs<0 || tsUs>0xffffffff || sampleSeq<0 || sampleSeq>0xffffffff || periodUs<2000 || periodUs>20000) {
        stop('惯导数据无效，请重新建立原点');return;
      }
      if(!(flags&1)) {stop('需要有效的六面加速度校准，请先完成校准');return;}
      if(s.arrival!==null && (at<s.arrival || at-s.arrival>2000)) {stop('接收中断，请重新建立原点');return;}
      const qn=q.map(x=>x/norm(q)), R=rotation(qn);
      const dt=s.last===null?0:((tsUs-s.last)>>>0)/1e6;
      if(s.last!==null && ((sampleSeq-s.lastSeq)>>>0)!==1) {stop('惯导丢帧或设备重启，已停止积分');return;}
      if(s.periodUs!==null && s.periodUs!==periodUs) {stop('惯导采样频率变化，已停止积分');return;}
      if(s.last!==null && (!dt || Math.abs(dt*1e6-periodUs)>Math.max(1000,periodUs*.25))) {stop('惯导采样时间不连续，已停止积分');return;}
      if(s.lastQ && angle(s.lastQ,qn)>Math.max(norm(gyro),norm(s.lastGyro))*dt+.5) {stop('融合姿态发生跳变，请重新建立原点');return;}
      const hostGap=s.arrival===null?0:at-s.arrival;
      s.arrival=at;s.last=tsUs;s.lastSeq=sampleSeq;s.periodUs=periodUs;s.lastQ=qn;s.lastGyro=copy(gyro);s.frames++;
      s.quality=hostGap>500?'接收延迟，采样时间连续':'采样连续';
      if(s.startedAt===null)s.startedAt=at;
      if(s.phase==='calibrating' && at-s.startedAt>30000) {stop('30 秒内未能确认静止，请检查校准或放置状态');return;}
      if(!dt)return;
      const world=rotate(R,acc), linear=[world[0],world[1],world[2]-s.gravity];
      const b=s.block, values=[...linear,...gyro];if(!b.q)b.q=qn;if(!s.originQ)s.originQ=qn;
      b.dt+=dt;b.n++;values.forEach((x,i)=>{b.sum[i]+=x*dt;b.square[i]+=x*x*dt;});
      b.gross ||= norm(gyro)>5 || Math.abs(norm(acc)-G)>3;
      let completed=null;
      if(b.dt>=.1-1e-9) {
        const mean=b.sum.map(x=>x/b.dt),sigma=b.square.map((x,i)=>Math.sqrt(Math.max(0,x/b.dt-mean[i]**2)));
        const accMean=norm(mean.slice(0,3)),gyroMean=norm(mean.slice(3)),accStd=Math.max(...sigma.slice(0,3)),gyroStd=Math.max(...sigma.slice(3));
        const poseChange=angle(b.q,qn);
        let reason=b.gross?'明显运动 / 冲击':b.n<3?'采样不足':gyroMean>=.5?'角速度过大':gyroStd>=.35?'角速度波动过大':accStd>=.25?'加速度波动过大':
          Math.hypot(mean[0],mean[1])>=.05 || Math.abs(mean[2])>=.05?'校准或姿态残差过大':null;
        if(!reason && s.phase==='calibrating' && angle(s.originQ,qn)>.5)reason='初始化期间姿态持续变化';
        completed={mean,dt:b.dt,quiet:!reason};s.diagnostics={accMean,gyroMean,accStd,gyroStd,poseChange,reason};
        s.resting=!reason && poseChange<.08 && accMean<.08 && gyroMean<.25;
        if(s.phase==='tracking')s.quiet=s.resting?s.quiet+b.dt:0;
        s.block=block();
      }
      if(b.gross) {s.resting=false;s.quiet=0;}
      if(s.phase==='calibrating') {
        if(!completed)return;
        if(!completed.quiet) {s.quiet=0;s.count=0;s.sum.fill(0);s.originQ=qn;s.message=`${s.diagnostics.reason}，请保持静止`;return;}
        s.quiet+=completed.dt;s.count++;completed.mean.slice(0,3).forEach((x,i)=>s.sum[i]+=x*completed.dt);
        s.message=`静止采集中 ${Math.min(2,s.quiet).toFixed(1)} / 2.0 秒`;
        if(s.quiet>=2-1e-9 && s.count>=20) {
          s.initialResidual=s.sum.map(x=>x/s.quiet);
          // A single pose cannot distinguish gravity, bias, scale and attitude errors.
          // Keep certified six-face calibration and the declared gravity constant; only establish axes here.
          s.yaw=Math.atan2(R[1][0],R[0][0]);
          if(Math.hypot(R[0][0],R[1][0])<.1) {stop('初始前方接近竖直，请平放后建立原点');return;}
          s.phase='tracking';s.quiet=.5;s.resting=true;s.message='估计中';
        }return;
      }
      const c=Math.cos(s.yaw),sn=Math.sin(s.yaw),input=[c*linear[0]+sn*linear[1],-sn*linear[0]+c*linear[1],linear[2]];
      const alpha=s.filterTau?-Math.expm1(-dt/s.filterTau):1;s.filtered=input.map((x,i)=>s.filtered[i]+alpha*(x-s.filtered[i]));
      const a=s.filtered;
      if(norm(a)>.12 || norm(gyro)>1.5){s.resting=false;s.quiet=0;}
      if(s.zupt && s.resting && s.quiet>=.5-1e-9){s.v.fill(0);s.previous.fill(0);s.message='停靠模式：速度归零（可能误判匀速）';return;}
      s.message='估计中';
      a.forEach((x,i)=>{const next=s.v[i]+(x+s.previous[i])*.5*dt;s.p[i]+=(s.v[i]+next)*.5*dt;s.v[i]=next;});s.previous=copy(a);
    }
    return {start,stop,ingest,stationary,state:()=>s};
  }
  root.GyroInertial={create,decode,rotation,session:create()};
})(globalThis);
