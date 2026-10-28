// AT32 vendor interface adapter: SerialPort-shaped streams for shared protocol/OTA.
(function(root) {
  function create(usb) {
    if (!usb) return null;
    const ports = new WeakMap(), listeners = new Map();
    function wrap(device) {
      if (ports.has(device)) return ports.get(device);
      const READ_AHEAD=4;
      const now=()=>root.performance?.now?.()??Date.now();
      let active=false, opening=false, closing=null, generation=0, readController=null, latestStats=null;
      const port={webusb:true, device, readable:null, writable:null,
        getInfo:()=>({usbVendorId:device.vendorId,usbProductId:device.productId,serialNumber:device.serialNumber}),
        getTransferStats:()=>latestStats?{...latestStats,active,readAhead:READ_AHEAD}:null,
        async open() {
          if (closing) await closing;
          if (active||opening) throw new Error('WebUSB 已打开或正在连接');
          opening=true;
          const session=++generation;
          const isCurrent=()=>active&&generation===session;
          try {
            await device.open();
            if (!device.configuration) await device.selectConfiguration(1);
            const intf=device.configuration.interfaces.find(i=>i.interfaceNumber===2);
            const alt=intf?.alternates.find(a=>a.interfaceClass===255 && a.endpoints.some(e=>e.direction==='in'&&e.type==='bulk') && a.endpoints.some(e=>e.direction==='out'&&e.type==='bulk'));
            if(!alt) throw new Error('设备缺少 AT32 WebUSB 接口，请检查固件');
            await device.claimInterface(2);
            if(intf.alternate?.alternateSetting!==alt.alternateSetting) await device.selectAlternateInterface(2,alt.alternateSetting);
            const input=alt.endpoints.find(e=>e.direction==='in'&&e.type==='bulk');
            const output=alt.endpoints.find(e=>e.direction==='out'&&e.type==='bulk');
            if(generation!==session)throw new Error('WebUSB 连接已取消');
            active=true;
            const stats=latestStats={generation:session,openedAtMs:now(),readRequests:0,readCompletions:0,
              readBytes:0,emptyReads:0,readErrors:0,writeBytes:0,writeErrors:0,inFlight:0,maxInFlight:0,maxReadWaitMs:0};
            const pending=[];
            function receive(){
              const started=now();stats.readRequests++;stats.inFlight++;
              stats.maxInFlight=Math.max(stats.maxInFlight,stats.inFlight);
              let request;
              try{request=device.transferIn(input.endpointNumber,input.packetSize);}catch(error){request=Promise.reject(error);}
              // Handle rejections immediately, including queued requests that
              // are aborted by close. Deliver results in submission order.
              return Promise.resolve(request).then(result=>{
                stats.inFlight--;stats.readCompletions++;stats.maxReadWaitMs=Math.max(stats.maxReadWaitMs,now()-started);
                return {result};
              },error=>{stats.inFlight--;return {error};});
            }
            port.readable=new ReadableStream({
              start(controller){readController=controller;for(let i=0;i<READ_AHEAD;i++)pending.push(receive());},
              async pull(controller){
                const outcome=await pending.shift();
                if(!isCurrent())return;
                const r=outcome.result;
                if(outcome.error||r.status!=='ok'){
                  stats.readErrors++;active=false;
                  controller.error(outcome.error||new Error('WebUSB 接收失败：'+r.status));return;
                }
                // Keep the native endpoint supplied before parsing/rendering
                // this packet. Both native and JS buffering remain bounded.
                pending.push(receive());
                if(r.data?.byteLength){
                  stats.readBytes+=r.data.byteLength;
                  controller.enqueue(new Uint8Array(r.data.buffer,r.data.byteOffset,r.data.byteLength));
                }else stats.emptyReads++;
              },
              cancel(){if(generation===session)return port.close();}
            });
            port.writable=new WritableStream({async write(data){
              for(let off=0;off<data.byteLength;off+=output.packetSize){
                if(!isCurrent())throw new Error('WebUSB 已断开');
                const chunk=data.subarray(off,off+output.packetSize);
                let r;
                try{r=await device.transferOut(output.endpointNumber,chunk);}catch(error){stats.writeErrors++;throw error;}
                if(!isCurrent())throw new Error('WebUSB 已断开');
                if(r.status!=='ok'||r.bytesWritten!==chunk.byteLength){stats.writeErrors++;throw new Error('WebUSB 发送不完整');}
                stats.writeBytes+=r.bytesWritten;
              }
            }});
          }catch(e){active=false;port.readable=port.writable=null;try{await device.close();}catch{}throw e;}
          finally{opening=false;}
        },
        async close(){
          if(closing)return closing;
          active=false;generation++;port.readable=port.writable=null;
          try{readController?.close();}catch{}readController=null;
          closing=(async()=>{try{await device.close();}finally{closing=null;}})();
          return closing;
        }
      };
      ports.set(device,port);return port;
    }
    return {
      requestPort:async()=>wrap(await usb.requestDevice({filters:[{vendorId:0x2e3c,productId:0xf401}]})),
      getPorts:async()=>(await usb.getDevices()).filter(d=>d.vendorId===0x2e3c&&d.productId===0xf401).map(wrap),
      addEventListener(type,fn){let map=listeners.get(type);if(!map)listeners.set(type,map=new Map());if(map.has(fn))return;const cb=e=>fn({port:wrap(e.device)});map.set(fn,cb);usb.addEventListener(type,cb);},
      removeEventListener(type,fn){const map=listeners.get(type),cb=map?.get(fn);if(cb){usb.removeEventListener(type,cb);map.delete(fn);}}
    };
  }
  root.GyroWebUSB={create};
})(globalThis);
