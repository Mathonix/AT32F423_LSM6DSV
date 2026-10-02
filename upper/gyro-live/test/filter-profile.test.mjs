import {test} from 'node:test';
import assert from 'node:assert/strict';
import {loadCore,fwPackFrame,feedChunked,rng} from './load-core.mjs';
const C=loadCore();
test('滤波配置：运行/保存分开回读，分包和参数校验',()=>{
  const p=new Uint8Array(16),v=new DataView(p.buffer);p.set([1,2,1,3]);
  v.setUint16(4,1000,true);v.setFloat32(8,6,true);v.setFloat32(12,1.5,true);
  const got=[],parser=C.createStreamParser({onMessage:m=>got.push(m)});
  parser.configure('binary',3);feedChunked(b=>parser.push(b),fwPackFrame(11,9,p),rng(17),1,3);
  assert.equal(got.length,1);assert.equal(got[0].active,2);assert.equal(got[0].saved,1);
  assert.equal(got[0].estimatorHz,1000);assert.equal(C.decodePayload(11,p.slice(0,15)).type,'badLength');
  p[1]=3;assert.equal(C.decodePayload(11,p).type,'unknown');p[1]=2;
  v.setFloat32(8,NaN,true);assert.equal(C.decodePayload(11,p).type,'unknown');
  assert.deepEqual([...C.buildFrame(C.CMD.SET_FILTER,3,[2,1])],[...fwPackFrame(0x27,3,[2,1])]);
});
test('融合诊断：完整时间戳、三轴零偏/残余、原始姿态和不确定度',()=>{
  const p=new Uint8Array(60),v=new DataView(p.buffer);p.set([1,1,1,3]);v.setUint32(4,0xfffffff0,true);
  for(let i=0;i<12;i++) v.setFloat32(8+4*i,i*.1,true);v.setFloat32(56,.05,true);
  const got=C.decodePayload(12,p);assert.equal(got.type,'fusionDiagnostic');assert.equal(got.timestamp,0xfffffff0);
  assert.equal(got.raw.length,3);assert.ok(Math.abs(got.bias[0]-.3)<1e-6);assert.ok(Math.abs(got.residual[0]-.6)<1e-6);
  assert.ok(Math.abs(got.rawEuler[0]-.9)<1e-6);assert.ok(Math.abs(got.sigma-.05)<1e-6);
  v.setFloat32(56,-1,true);assert.equal(C.decodePayload(12,p).type,'unknown');
  v.setFloat32(56,.05,true);v.setFloat32(20,Infinity,true);assert.equal(C.decodePayload(12,p).type,'unknown');
});
