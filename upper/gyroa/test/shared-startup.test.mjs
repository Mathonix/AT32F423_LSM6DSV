import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const source=readFileSync(new URL('../dist/public/app.js',import.meta.url),'utf8');
const start=source.indexOf('// ==== GYRO-CORE BEGIN ===='),end=source.indexOf('// ==== GYRO-CORE END ====');
const C=new Function(source.slice(source.indexOf('\n',start)+1,end)+'\nreturn {MSG,CMD,decodePayload,buildFrame,createStreamParser};')();
function config(duration,version=4) {
  const p=new Uint8Array(28),v=new DataView(p.buffer);
  p.set([version,1,1,1,+(duration===0),+(duration===0),127,0]);
  v.setUint16(8,500,true);p[10]=2;p[12]=7;p[14]=2;p[16]=7;
  [duration,duration,1000,1000,500].forEach((x,i)=>v.setUint16(18+2*i,x,true));
  return p;
}
test('v4 shared window 0–60000ms; range/rate and fragmented frames',()=>{
  for(const duration of [0,1,99,100,2000,60000]) {
    const got=[],parser=C.createStreamParser({onMessage:m=>got.push(m)});
    parser.configure('binary',3);
    const frame=C.buildFrame(7,9,config(duration));
    for(let i=0;i<frame.length;i+=3) parser.push(frame.slice(i,i+3));
    assert.equal(got.length,1);assert.equal(got[0].type,'config');
    assert.equal(got[0].activeInitMs,duration);assert.equal(got[0].savedInitMs,duration);
    assert.equal(got[0].activeRangeDps,1000);assert.equal(got[0].savedOutputHz,500);
  }
  assert.equal(C.decodePayload(7,config(60001)).type,'unknown');
  assert.equal(C.decodePayload(7,config(0,3)).type,'unknown');
  assert.equal(C.decodePayload(7,config(2000,3)).type,'config');
  assert.equal(C.decodePayload(7,config(0).slice(0,22)).type,'badLength');
});
test('model command 0x35 and exact four ASCII bytes in 0x36',()=>{
  assert.equal(C.CMD.QUERY_DEVICE_MODEL,0x35);assert.equal(C.MSG.DEVICE_MODEL,0x36);
  assert.deepEqual(C.decodePayload(0x36,Uint8Array.from([65,84,51,50])),{type:'deviceModel',text:'AT32'});
  for(const bytes of [[65,84,51],[65,84,51,50,0]]) assert.equal(C.decodePayload(0x36,Uint8Array.from(bytes)).type,'badLength');
  for(const bytes of [[65,84,51,0],[65,84,51,255]]) assert.equal(C.decodePayload(0x36,Uint8Array.from(bytes)).type,'unknown');
  const frame=C.buildFrame(0x35,7,[]);assert.deepEqual([...frame.slice(0,5)],[0xaa,0x55,0x35,0,7]);
});
