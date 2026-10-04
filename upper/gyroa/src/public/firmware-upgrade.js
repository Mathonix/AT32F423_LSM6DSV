// Shared browser/test implementation of the board-verified Bootloader v1 protocol.
(() => {
  const APP_BASE = 0x08008000, APP_SIZE = 0x34000, MAX_CHUNK = 256;
  const COMMAND = {HELLO:1, BEGIN:2, DATA:3, END:4, ABORT:5, BOOT:6};
  const STATUS = ['成功', '帧或命令无效', '参数无效', 'CRC 错误', 'Flash 操作失败', '无有效应用', '接口被占用'];
  function crc32(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) {
      crc ^= byte;
      for (let bit=0; bit<8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
  }
  function validateImage(bytes) {
    if (!(bytes instanceof Uint8Array) || bytes.length < 8 || bytes.length > APP_SIZE)
      throw new Error('固件大小须为 8～212992 字节，只能选择应用 .bin。');
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const sp = view.getUint32(0,true), reset = view.getUint32(4,true), address = reset & ~1;
    if (sp <= 0x20000000 || sp > 0x2000bff0 || sp % 8 || !(reset & 1) ||
        address < APP_BASE || address >= APP_BASE + bytes.length)
      throw new Error('应用向量无效：固件须链接到 0x08008000，并预留 Bootloader 启动邮箱。');
    return {bytes:bytes.length, sp, reset, crc32:crc32(bytes)};
  }
  function packet(command, seq, address=0, length=0, crc=0, payload=new Uint8Array()) {
    if (command === COMMAND.DATA ? payload.length !== length || length < 1 || length > MAX_CHUNK : payload.length !== 0)
      throw new Error('升级请求长度无效。');
    const bytes = new Uint8Array(18 + payload.length), view = new DataView(bytes.buffer);
    bytes.set([0x42,0x4c,1,command]); view.setUint16(4,seq,true);
    view.setUint32(6,address,true); view.setUint32(10,length,true); view.setUint32(14,crc,true);
    bytes.set(payload,18); return bytes;
  }
  const timeoutError = command => new Error(`等待 Bootloader 0x${command.toString(16).padStart(2,'0')} 应答超时。`);
  class BootClient {
    constructor(port) { this.port=port; this.reader=null; this.writer=null; this.pending=null; this.buffer=[]; this.closed=false; }
    start() {
      try { this.writer=this.port.writable.getWriter(); this.reader=this.port.readable.getReader(); }
      catch (error) { try {this.writer?.releaseLock();} catch {} throw error; }
      this.reading=this.readLoop(); return this;
    }
    async readLoop() {
      const reader=this.reader;
      try {
        while (!this.closed) {
          const {value,done}=await reader.read();
          if (done) { if(!this.closed) this.fail(new Error('升级串口数据流已结束。')); break; }
          if(value) this.feed(value);
        }
      } catch(error) { if(!this.closed) this.fail(new Error(`升级连接中断：${error.message}`)); }
      finally {try {reader.releaseLock();} catch {} }
    }
    feed(bytes) {
      for (const byte of bytes) {
        this.buffer.push(byte);
        while(this.buffer.length >= 14) {
          if(this.buffer[0]!==0x42 || this.buffer[1]!==0x4c || this.buffer[2]!==1 || this.buffer[5]!==0) {this.buffer.shift();continue;}
          const frame=Uint8Array.from(this.buffer.slice(0,14)), view=new DataView(frame.buffer);
          if(crc32(frame.subarray(0,10))!==view.getUint32(10,true)) {this.buffer.shift();continue;}
          this.buffer.splice(0,14);
          const pending=this.pending;
          if(!pending || frame[3] !== (pending.command|0x80)) continue;
          clearTimeout(pending.timer);this.pending=null;
          if(frame[4]) pending.reject(new Error(`Bootloader 拒绝请求：${STATUS[frame[4]]||`状态 ${frame[4]}`}（0x${pending.command.toString(16)}）。`));
          else pending.resolve(view.getUint32(6,true));
        }
      }
    }
    fail(error) {
      if(!this.pending) return;
      const pending=this.pending;this.pending=null;clearTimeout(pending.timer);pending.reject(error);
    }
    request(command, seq=0, address=0, length=0, crc=0, payload=new Uint8Array(), timeout=3000) {
      if(this.closed || !this.writer) return Promise.reject(new Error('升级串口未打开。'));
      if(this.pending) return Promise.reject(new Error('升级请求尚未完成。'));
      const frame=packet(command,seq,address,length,crc,payload);
      // Never retry a mutating v1 request: BEGIN erases and DATA is not idempotent.
      return new Promise((resolve,reject)=>{
        const pending={command,resolve,reject,timer:null};
        pending.timer=setTimeout(()=>{if(this.pending===pending)this.fail(timeoutError(command));},timeout);
        this.pending=pending;
        this.writer.write(frame).catch(error=>{if(this.pending===pending)this.fail(error);});
      });
    }
    async close(timeout=1500) {
      if(this.closed) return;this.closed=true;this.fail(new Error('升级连接已关闭。'));
      const errors=[];
      const bounded=async(promise,label)=>{
        let timer;
        try {await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error(`${label}超时`)),timeout);})]);}
        catch(error) {errors.push(error.message);}
        finally {clearTimeout(timer);}
      };
      await bounded((async()=>{try {await this.reader?.cancel();} catch {} await this.reading;})(),'停止读取');
      try {this.reader?.releaseLock();} catch {}
      await bounded(this.writer?.abort().catch(()=>{}),'停止写入');
      try {this.writer?.releaseLock();} catch {}
      await bounded(this.port.close(),'关闭端口');
      if(errors.length) throw new Error(`无法关闭升级串口：${errors.join('；')}。请释放串口后重新连接。`);
    }
  }
  async function transferImage(client, bytes, {onProgress=()=>{}, shouldStop=()=>false}={}) {
    const image=validateImage(bytes), checkStop=async()=>{
      if(!shouldStop()) return;
      // ABORT never boots the partially written image; it also releases port ownership.
      await client.request(COMMAND.ABORT);
      throw new Error('已停止上传，设备保留在升级模式，请重新完整上传。');
    };
    await checkStop();
    onProgress({phase:'erase',offset:0,total:bytes.length});
    if(await client.request(COMMAND.BEGIN,1,APP_BASE,bytes.length,image.crc32,new Uint8Array(),30000)!==0)
      throw new Error('Bootloader 擦除确认偏移无效。');
    for(let offset=0;offset<bytes.length;offset+=MAX_CHUNK) {
      await checkStop();
      const chunk=bytes.subarray(offset,offset+MAX_CHUNK);
      const next=await client.request(COMMAND.DATA,offset/MAX_CHUNK,APP_BASE+offset,chunk.length,crc32(chunk),chunk);
      if(next!==offset+chunk.length) throw new Error(`固件确认偏移错误：期望 ${offset+chunk.length}，收到 ${next}。`);
      onProgress({phase:'write',offset:next,total:bytes.length});
    }
    await checkStop();
    onProgress({phase:'verify',offset:bytes.length,total:bytes.length});
    if(await client.request(COMMAND.END,0,APP_BASE,bytes.length,image.crc32,new Uint8Array(),10000)!==bytes.length)
      throw new Error('整镜像校验确认长度错误。');
    return image;
  }
  globalThis.GyroFirmware={APP_BASE,APP_SIZE,MAX_CHUNK,COMMAND,crc32,validateImage,packet,BootClient,transferImage};
})();
