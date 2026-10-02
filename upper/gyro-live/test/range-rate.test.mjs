import {test} from 'node:test';
import assert from 'node:assert/strict';
import {loadCore,fwPackFrame,feedChunked,rng} from './load-core.mjs';
const C=loadCore();
test('配置v3分片回读运行/保存量程和频率，并拒绝无效或截断数据',()=>{
  const p=new Uint8Array(28),v=new DataView(p.buffer);
  p.set([3,1,1,1,0,0,127,0]);v.setUint16(8,500,true);p[10]=2;p[12]=7;p[14]=2;p[16]=7;
  [2000,2500,125,4000,500].forEach((x,i)=>v.setUint16(18+2*i,x,true));
  const got=[],parser=C.createStreamParser({onMessage:m=>got.push(m)});
  parser.configure('binary',3);feedChunked(b=>parser.push(b),fwPackFrame(7,9,p),rng(23),1,3);
  assert.equal(got.length,1);assert.equal(got[0].activeRangeDps,125);assert.equal(got[0].savedRangeDps,4000);assert.equal(got[0].savedOutputHz,500);
  assert.equal(C.decodePayload(7,p.slice(0,22)).type,'badLength');
  v.setUint16(24,750,true);assert.equal(C.decodePayload(7,p).type,'unknown');
  v.setUint16(24,1000,true);v.setUint16(26,300,true);assert.equal(C.decodePayload(7,p).type,'unknown');
});
test('CAN频率按毫秒转换，边界和无效输入不生成可应用间隔',()=>{
  for(const [hz,ms] of [[1000,1],[200,5],[50,20],[.1,10000],[60,17]]) assert.equal(C.canPeriodFromHz(hz),ms);
  for(const bad of [0,'',-1,.09,1001,Infinity,'bad']) assert.equal(C.canPeriodFromHz(bad),null);
});
