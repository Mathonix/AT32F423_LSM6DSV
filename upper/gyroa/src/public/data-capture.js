(function(root){
  'use strict';
  const RATES=[1,5,10,20,50,100,200,500],COMMAND=0x60,QUERY=0x62,DATA=0x61,CONFIG=0x63,MAX_BYTES=64*1024*1024;
  const HEADER='received_at_utc,elapsed_s,device_time_us,sample_seq,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,yaw_deg,pitch_deg,roll_deg,missing_samples,timing_gap_us,flags,sample_hz,firmware,mode,algorithm,gyro_range_dps,acc_range_g\r\n';
  const escape=x=>/[",\r\n]/.test(String(x??''))?'"'+String(x).replaceAll('"','""')+'"':String(x??'');
  function decode(id,p){
    const length=id===DATA?56:12;
    if(p.length!==length)return {type:'badLength',id,length:p.length,expected:length};
    const d=new DataView(p.buffer,p.byteOffset,p.length);
    if(id===CONFIG){const token=d.getUint32(4,true),hz=d.getUint16(8,true),maxHz=d.getUint16(10,true);
      if(p[0]!==1||p[1]>1||d.getUint16(2,true)||maxHz!==500||(p[1]?(!token||!RATES.includes(hz)):(token||hz)))return {type:'unknown',id};
      return {type:'captureConfig',active:!!p[1],token,hz,maxHz};
    }
    const values=Array.from({length:9},(_,i)=>d.getFloat32(20+i*4,true)),token=d.getUint32(4,true),periodUs=d.getUint32(16,true);
    if(p[0]!==1||(p[1]&~15)||d.getUint16(2,true)||!token||!RATES.some(h=>1e6/h===periodUs)||!values.every(Number.isFinite))return {type:'unknown',id};
    return {type:'captureData',token,flags:p[1],sampleSeq:d.getUint32(8,true),tsUs:d.getUint32(12,true),periodUs,values};
  }
  function payload(enable,hz,token){
    if(!RATES.includes(hz)||!Number.isInteger(token)||token<1||token>0xffffffff)throw Error('采集参数无效');
    const p=new Uint8Array(7),d=new DataView(p.buffer);p[0]=enable?1:0;d.setUint16(1,hz,true);d.setUint32(3,token,true);return p;
  }
  function createSession({now=()=>performance.now(),wall=()=>Date.now(),maxBytes=MAX_BYTES}={}){
    let s,chunks,lines,points,nextPreview;
    function clear(){if(s?.active)return false;s={active:false,status:'idle',rows:0,bytes:3+HEADER.length,elapsedUs:0,lost:0,bad:0,timingGaps:0,clipped:0,latest:null,metadata:null,hz:null,token:null,arrival:null,last:null,firstWall:null,reason:''};chunks=[];lines=[];points=[];nextPreview=0;return true;}
    clear();
    function start({hz,token,metadata={}}){
      if(s.active||s.rows)throw Error('请先导出并清空上一轮数据');payload(true,hz,token);
      s={...s,active:true,status:'waiting',hz,token,metadata:JSON.parse(JSON.stringify(metadata)),requestedAt:now(),reason:''};
    }
    function stop(reason='采集已停止'){s.active=false;s.status=s.rows?'stopped':'idle';s.reason=reason;}
    function dropped(){if(s.active)s.bad++;}
    function ingest(m){
      if(!s.active||m.token!==s.token)return false;
      if(m.periodUs!==1e6/s.hz||!Number.isInteger(m.tsUs)||m.tsUs<0||m.tsUs>0xffffffff||!Number.isInteger(m.sampleSeq)||m.sampleSeq<0||m.sampleSeq>0xffffffff||!Array.isArray(m.values)||m.values.length!==9||!m.values.every(Number.isFinite)||(m.flags&~15)){dropped();return false;}
      let lost=0,gap=0,elapsed=s.elapsedUs;
      if(s.last){const seqDelta=(m.sampleSeq-s.last.seq)>>>0,dt=(m.tsUs-s.last.ts)>>>0;
        if(!seqDelta)return false;
        if(seqDelta>=0x80000000||!dt||dt>=0x80000000){stop('设备时钟或序号重置，已保留重置前的数据');return false;}
        lost=seqDelta-1;gap=dt-m.periodUs*seqDelta;elapsed+=dt;
      }
      const at=now(),received=wall(),meta=s.metadata;
      const row=[new Date(received).toISOString(),(elapsed/1e6).toFixed(6),m.tsUs,m.sampleSeq,...m.values.map(x=>x.toFixed(6)),lost,gap,m.flags,s.hz,meta.firmware,meta.mode,meta.algorithm===1?'EKF':'VQF',meta.rangeDps,meta.accRangeG].map(escape).join(',')+'\r\n';
      const bytes=new TextEncoder().encode(row).length;
      if(s.bytes+bytes>maxBytes){stop('已达到文件大小上限，请导出数据后开始下一轮');return false;}
      lines.push(row);if(lines.length>=256){chunks.push(lines.join(''));lines=[];}
      s.bytes+=bytes;s.rows++;s.lost+=lost;s.elapsedUs=elapsed;s.arrival=at;s.last={seq:m.sampleSeq,ts:m.tsUs};s.latest=m.values.slice();s.status='recording';
      if(s.firstWall===null)s.firstWall=received;
      if(Math.abs(gap)>Math.max(1000,m.periodUs*.25))s.timingGaps++;
      if(m.flags&12)s.clipped++;
      if(elapsed>=nextPreview){points.push({t:elapsed/1e6,values:m.values.slice(),gap:lost>0||Math.abs(gap)>Math.max(1000,m.periodUs*.25)});nextPreview=elapsed+Math.max(20000,m.periodUs);}
      while(points.length>800 || (points.length>1 && points[0].t<elapsed/1e6-15))points.shift();
      return true;
    }
    function tick(connected=true){if(!s.active)return;if(!connected)stop('连接已断开，已保留采集数据');else if(now()-(s.arrival??s.requestedAt)>Math.max(3500,3e3/s.hz))stop(s.rows?'采集数据中断，已保留已有数据':'未收到采集数据，请检查设备和连接');}
    return {start,stop,clear,ingest,dropped,tick,state:()=>s,preview:()=>points,parts:()=>['\ufeff'+HEADER,...chunks,lines.join('')],csv:()=>['\ufeff'+HEADER,...chunks,lines.join('')].join('')};
  }
  function createController(io){
    const state={supported:null,config:null,pending:null,result:null,message:'请先连接设备'};let timer,generation=0;
    const changed=()=>io.changed?.();
    function finish(ok,message){clearTimeout(timer);const p=state.pending;state.pending=null;state.result={kind:p?.kind,ok};state.message=message;changed();}
    function reset(){generation++;clearTimeout(timer);Object.assign(state,{supported:null,config:null,pending:null,result:null,message:'请先连接设备'});changed();}
    function request(kind,hz,token){
      if(!io.connected()||state.pending)return false;
      if(kind!=='query'&&(state.supported!==true||!RATES.includes(hz)))return false;
      const cmd=kind==='query'?QUERY:COMMAND,p=state.pending={kind,cmd,seq:io.nextSeq()&255,hz,token,acked:false,reply:null,generation};state.result=null;
      state.message=kind==='query'?'正在读取采集能力…':kind==='stop'?'正在停止采集…':'正在确认采集频率…';changed();
      timer=setTimeout(()=>{if(state.pending===p)finish(false,'采集指令未获确认，请重新连接或读取设备');},4000);
      Promise.resolve(io.send(cmd,kind==='query'?[]:payload(kind!=='stop',hz,token))).then(ok=>{if(!ok&&state.pending===p)finish(false,'采集指令发送失败');},()=>{if(state.pending===p)finish(false,'采集指令发送失败');});return true;
    }
    function complete(p){if(state.pending!==p||!p.reply||(p.kind!=='query'&&!p.acked))return;
      state.supported=true;state.config=p.reply;
      if(p.kind!=='query' && (p.kind==='stop'?p.reply.active:!p.reply.active||p.reply.token!==p.token||p.reply.hz!==p.hz)){finish(false,'采集状态回读不一致，请重新连接设备');return;}
      finish(true,p.kind==='query'?'采集功能就绪':p.kind==='stop'?'采集已停止':'采集中');
    }
    function onAck(cmd,status,seq){const p=state.pending;if(!p||p.cmd!==cmd||p.seq!==seq)return;
      if(status){if(status===1)state.supported=false;finish(false,status===1?'请升级到 20261012b 或更新固件后采集':'设备拒绝采集请求，请先结束设置、校准或其他采集');return;}p.acked=true;complete(p);}
    function onConfig(m,seq){const p=state.pending;if(!p||p.seq!==seq)return;p.reply=m;complete(p);}
    return {state,reset,onAck,onConfig,query:()=>request('query'),start:(hz,token)=>request('start',hz,token),renew:(hz,token)=>request('renew',hz,token),stop:(hz,token)=>request('stop',hz,token)};
  }
  root.GyroCapture={RATES,COMMAND,QUERY,DATA,CONFIG,MAX_BYTES,HEADER,decode,payload,createSession,createController,session:createSession(),
    busy:()=>!!root.GyroCapture.session.state().active||['start','renew','stop'].includes(root.GyroCapture.control?.state.pending?.kind)};
})(globalThis);
