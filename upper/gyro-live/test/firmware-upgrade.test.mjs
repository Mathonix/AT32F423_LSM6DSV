import test from 'node:test';
import assert from 'node:assert/strict';
import '../src/public/firmware-upgrade.js';
const fw=globalThis.GyroFirmware;
function image(size=777) {
  const data=new Uint8Array(size).fill(0xa5),v=new DataView(data.buffer);
  v.setUint32(0,0x2000bff0,true);v.setUint32(4,fw.APP_BASE+9,true);return data;
}
function ack(command,value=0,status=0) {
  const data=new Uint8Array(14),v=new DataView(data.buffer);data.set([0x42,0x4c,1,command|0x80,status,0]);
  v.setUint32(6,value,true);v.setUint32(10,fw.crc32(data.subarray(0,10)),true);return data;
}
test('firmware CRC32 matches IEEE check vector and invalid vectors/partitions are rejected',()=>{
  assert.equal(fw.crc32(new TextEncoder().encode('123456789')),0xcbf43926);
  assert.equal(fw.validateImage(image()).bytes,777);
  for(const [offset,value] of [[0,0x2000c000],[0,0x20000000],[0,0x2000bff4],[4,0x08000001],[4,fw.APP_BASE+8],[4,fw.APP_BASE+1001]]) {
    const data=image();new DataView(data.buffer).setUint32(offset,value,true);assert.throws(()=>fw.validateImage(data));
  }
  assert.throws(()=>fw.validateImage(new Uint8Array(7)));
  assert.throws(()=>fw.validateImage(new Uint8Array(fw.APP_SIZE+1)));
});
test('BEGIN/END carry length without payload, DATA uses little-endian address and block CRC',()=>{
  const begin=fw.packet(2,1,fw.APP_BASE,777,123);assert.equal(begin.length,18);
  assert.equal(new DataView(begin.buffer).getUint32(10,true),777);
  const payload=new Uint8Array([1,2,3]),data=fw.packet(3,513,fw.APP_BASE,3,fw.crc32(payload),payload);
  assert.equal(new DataView(data.buffer).getUint16(4,true),513);assert.deepEqual([...data.slice(18)],[1,2,3]);
  assert.throws(()=>fw.packet(2,0,0,3,0,payload));assert.throws(()=>fw.packet(3,0,0,2,0,payload));
});
function fixture(reply) {
  let controller;const writes=[];
  const port={readable:new ReadableStream({start(c){controller=c;}}),writable:new WritableStream({write(data){writes.push(data);reply?.(data,controller);}}),async close(){assert.equal(this.readable.locked,false);assert.equal(this.writable.locked,false);}};
  return {client:new fw.BootClient(port).start(),writes,controller};
}
test('ACK parser resynchronizes noise/corruption, supports split replies, ignores other commands',async()=>{
  const {client,controller}=fixture();
  try {
    const pending=client.request(1,0,0,0,0,new Uint8Array(),100);
    const bad=ack(1,0);bad[11]^=1;
    controller.enqueue(Uint8Array.from([99,0x42,...bad,...ack(2)]));
    const valid=ack(1,fw.APP_BASE);controller.enqueue(valid.slice(0,5));controller.enqueue(valid.slice(5));
    assert.equal(await pending,fw.APP_BASE);
  } finally {await client.close();}
});
test('ACK rejection and timeout fail once, without retrying a write',async()=>{
  const {client,writes}=fixture((data,c)=>c.enqueue(ack(data[3],0,4)));
  await assert.rejects(client.request(2),/Flash/);assert.equal(writes.length,1);await client.close();
  const silent=fixture();await assert.rejects(silent.client.request(3,0,fw.APP_BASE,1,0,new Uint8Array(1),15),/超时/);
  assert.equal(silent.writes.length,1);await silent.client.close();
});
test('transfer sends contiguous aligned blocks, final partial word, and END CRC without BOOT',async()=>{
  const bytes=image(),chunks=[],calls=[];let total=0;
  const client={async request(cmd,seq,address,length,crc,payload){calls.push(cmd);
    if(cmd===2) {assert.equal(length,777);assert.equal(crc,fw.crc32(bytes));return 0;}
    if(cmd===3) {assert.equal(address,fw.APP_BASE+total);assert.equal(crc,fw.crc32(payload));chunks.push(...payload);total+=length;return total;}
    if(cmd===4) {assert.equal(total,777);assert.equal(crc,fw.crc32(Uint8Array.from(chunks)));return total;}
    throw new Error('unexpected command');}};
  const progress=[];await fw.transferImage(client,bytes,{onProgress:p=>progress.push(p)});
  assert.deepEqual(calls,[2,3,3,3,3,4]);assert.deepEqual(Uint8Array.from(chunks),bytes);
  assert.equal(progress.at(-1).phase,'verify');assert.equal(progress.filter(p=>p.phase==='write').at(-1).offset,777);
});
test('stalled serial write times out once and bounded cleanup releases the stream lock',async()=>{
  let controller,closed=false;
  const port={readable:new ReadableStream({start(c){controller=c;}}),
    writable:new WritableStream({write(){return new Promise(()=>{});}}),
    async close(){closed=true;assert.equal(this.readable.locked,false);assert.equal(this.writable.locked,false);}};
  const client=new fw.BootClient(port).start();
  await assert.rejects(client.request(1,0,0,0,0,new Uint8Array(),15),/超时/);
  await assert.rejects(client.close(15),/停止写入超时/);
  assert.equal(closed,true);
});
test('wrong DATA offset or failed END never BOOT or retry an ambiguous mutating request',async()=>{
  for(const failure of ['offset','end','timeout']) {
    const calls=[];const client={async request(cmd,_seq,_addr,len){calls.push(cmd);
      if(cmd===2)return 0;if(cmd===3){if(failure==='timeout')throw new Error('timeout');return failure==='offset'?len-1:len;}
      if(cmd===4)throw new Error('CRC failure');throw new Error('unexpected BOOT');}};
    await assert.rejects(fw.transferImage(client,image(32)));
    assert.equal(calls.includes(6),false);assert.equal(calls.filter(x=>x===2).length,1);assert.equal(calls.filter(x=>x===3).length,1);
  }
});
test('stop after a confirmed block sends ABORT, retains maintenance mode, and skips END/BOOT',async()=>{
  const calls=[];let stop=false;
  const client={async request(cmd,_seq,_addr,len){calls.push(cmd);return cmd===3?len:0;}};
  await assert.rejects(fw.transferImage(client,image(),{shouldStop:()=>stop,onProgress:p=>{if(p.phase==='write')stop=true;}}),/停止/);
  assert.deepEqual(calls,[2,3,5]);
});
