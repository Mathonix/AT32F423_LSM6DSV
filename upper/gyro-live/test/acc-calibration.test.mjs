import {test} from 'node:test';
import assert from 'node:assert/strict';
import {loadCore, fwPackFrame, feedChunked, rng} from './load-core.mjs';
const C = loadCore();
function payload() {
  const p=new Uint8Array(60),v=new DataView(p.buffer);
  p.set([1,1,2,6,0x15,1,1,0]); v.setUint16(8,650,true); v.setUint32(12,1300,true);
  v.setUint32(16,9900,true); v.setUint32(20,48000,true);
  [.01,-.02,.03,1.01,.98,1.02,0,0,1].forEach((x,i)=>v.setFloat32(24+4*i,x,true));
  return p;
}
test('六面进度协议与固件 60 字节布局一致，分包仍保留面掩码和校准参数',()=>{
  const p=payload(), got=[],parser=C.createStreamParser({onMessage:m=>got.push(m)});
  feedChunked(bytes=>parser.push(bytes),fwPackFrame(10,17,p),rng(91));
  assert.equal(got.length,1); const s=got[0];
  assert.equal(s.type,'accCalibration'); assert.equal(s.face,6); assert.equal(s.mask,21);
  assert.equal(s.progress,650); assert.equal(s.samples,1300); assert.equal(s.remaining,48000);
  assert.ok(Math.abs(s.bias[1]+.02)<1e-7); assert.equal(s.raw[2],1);
  assert.equal(C.CMD.QUERY_ACC_CAL,0x24); assert.equal(C.CMD.CANCEL_ACC_CAL,0x25);
});
test('校准进度拒绝未知版本、截断、非法状态/面/浮点数，避免伪造完成状态',()=>{
  assert.equal(C.decodePayload(10,payload().slice(0,59)).type,'badLength');
  for(const [offset,value] of [[0,2],[1,5],[2,8],[3,7],[4,64],[5,2],[6,2],[7,1]]) {
    const p=payload();p[offset]=value;assert.equal(C.decodePayload(10,p).type,'unknown');
  }
  for(const offset of [24,36,48]) { const p=payload();new DataView(p.buffer).setFloat32(offset,NaN,true); assert.equal(C.decodePayload(10,p).type,'unknown'); }
  for(const [offset,value] of [[24,.5],[36,0],[48,100]]) { const p=payload();new DataView(p.buffer).setFloat32(offset,value,true); assert.equal(C.decodePayload(10,p).type,'unknown'); }
  const p=payload();new DataView(p.buffer).setUint16(8,1001,true);assert.equal(C.decodePayload(10,p).type,'unknown');
});
test('取消、超时、拟合/保存失败及存储满明确表示旧校准参数保留',()=>{
  for(const error of [0x0603,0x0606,0x0607,0x0608,0x0609]) assert.match(C.ackDetailText(C.CMD.ACC_6FACE,3,error),/保留/);
});
test('六面摆正参考尊重设备已识别方向，原始 g 推断只作参考，空读数不假装实时方向',()=>{
  for(let face=1;face<=6;face++) {
    const raw=[0,0,0], axis=Math.floor((face-1)/2), sign=face%2===0?1:-1; raw[axis]=sign;
    const known=C.accPlacementReference(face,raw),estimated=C.accPlacementReference(0,raw);
    assert.equal(known.axis,axis);assert.equal(known.face,face);assert.equal(known.recognized,true);
    assert.equal(estimated.face,face);assert.equal(estimated.recognized,false);
    assert.deepEqual(known.reference,raw);
  }
  assert.equal(C.accPlacementReference(0,[0,0,0]),null);
  assert.equal(C.accPlacementReference(1,[NaN,0,1]),null);
  assert.equal(C.accPlacementReference(0,null),null);
  assert.equal(C.accPlacementReference(2,[0,0,1]).face,2);
});
