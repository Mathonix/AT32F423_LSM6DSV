// AT32 vendor interface adapter: SerialPort-shaped streams for shared protocol/OTA.
(function(root) {
  function create(usb) {
    if (!usb) return null;
    const ports = new WeakMap(), listeners = new Map();
    function wrap(device) {
      if (ports.has(device)) return ports.get(device);
      let active=false, closing=null;
      const port={webusb:true, device, readable:null, writable:null,
        getInfo:()=>({usbVendorId:device.vendorId,usbProductId:device.productId,serialNumber:device.serialNumber}),
        async open() {
          if (closing) await closing;
          if (active) throw new Error('WebUSB 已打开');
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
            active=true;
            port.readable=new ReadableStream({async pull(controller){
              try {const r=await device.transferIn(input.endpointNumber,input.packetSize);
                if(!active)return;
                if(r.status!=='ok')throw new Error('WebUSB 接收失败：'+r.status);
                if(r.data?.byteLength)controller.enqueue(new Uint8Array(r.data.buffer,r.data.byteOffset,r.data.byteLength));
              } catch(e){if(active)controller.error(e);}
            },cancel(){return port.close();}});
            port.writable=new WritableStream({async write(data){
              for(let off=0;off<data.byteLength;off+=output.packetSize){
                if(!active)throw new Error('WebUSB 已断开');
                const chunk=data.subarray(off,off+output.packetSize);
                const r=await device.transferOut(output.endpointNumber,chunk);
                if(r.status!=='ok'||r.bytesWritten!==chunk.byteLength)throw new Error('WebUSB 发送不完整');
              }
            }});
          }catch(e){active=false;port.readable=port.writable=null;try{await device.close();}catch{}throw e;}
        },
        async close(){
          if(closing)return closing;
          active=false;port.readable=port.writable=null;
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
