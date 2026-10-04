import {test} from 'node:test';
import assert from 'node:assert/strict';
import {loadCore,fwPackFrame,feedChunked,rng} from './load-core.mjs';
const C=loadCore();
function config(duration,version=4) {
  const p=new Uint8Array(28),v=new DataView(p.buffer);
  p.set([version,1,1,1,+(duration===0),+(duration===0),127,0]);
  v.setUint16(8,500,true); p[10]=2;p[12]=7;p[14]=2;p[16]=7;
  [duration,duration,1000,1000,500].forEach((x,i)=>v.setUint16(18+2*i,x,true));
  return p;
}
test('配置v4接受0～60秒，与v3保持相同量程/频率布局',()=>{
  for(const duration of [0,1,99,100,2000,60000]) {
    const p=config(duration),got=[];
    const parser=C.createStreamParser({onMessage:m=>got.push(m)});
    parser.configure('binary',3);feedChunked(b=>parser.push(b),fwPackFrame(7,9,p),rng(23),1,3);
    assert.equal(got.length,1);assert.equal(got[0].type,'config');
    assert.equal(got[0].activeInitMs,duration);assert.equal(got[0].savedInitMs,duration);
    assert.equal(got[0].activeRangeDps,1000);assert.equal(got[0].savedOutputHz,500);
  }
  assert.equal(C.decodePayload(7,config(60001)).type,'unknown');
  assert.equal(C.decodePayload(7,config(0,3)).type,'unknown');
  assert.equal(C.decodePayload(7,config(2000,3)).type,'config');
  assert.equal(C.decodePayload(7,config(0).slice(0,22)).type,'badLength');
});
