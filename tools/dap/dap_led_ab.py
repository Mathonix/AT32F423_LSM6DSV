"""Non-halting WS2812 stationary A/B capture. Writes ONLY LED test controls.
Four 65s stages, first 5s excluded. Always restore breathing, with MCU timeout.
"""
import csv,json,struct,subprocess,time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper
from dap_stationary_30min import NAMES,FMT,MAGIC

def main():
 root=Path(__file__).resolve().parents[2]
 nm=Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
 syms=subprocess.check_output([str(nm),str(root/'build/lsm6dsv_spi_test.elf')],text=True)
 def addr(n):return next(int(s.split()[0],16) for s in syms.splitlines() if s.endswith(' '+n))
 tuneaddr=addr('vqf_tune_live'); rawaddr=addr('mag_raw_live')
 modeaddr=addr('ws2812_test_mode'); deadlineaddr=addr('ws2812_test_until_ms'); leveladdr=addr('ws2812_green_level')
 out=root/'build/logs'/('led_ab_'+time.strftime('%Y%m%d_%H%M%S')+'.csv');out.parent.mkdir(exist_ok=True,parents=True)
 meta={'csv':str(out),'complete':False,'error':None,'restored':False,'samples':0,'stages':['off_before','fixed50','breathing','off_after'],'settling_s':5,'recording_s':60,'swd_hz':100000}
 session=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'attach','frequency':100000,'resume_on_disconnect':False})
 try:
  if session is None:raise RuntimeError('No DAP probe')
  with session:
   t=session.target
   if t.get_state().name!='RUNNING':raise RuntimeError('MCU not running')
   def read(address,fmt,magic):
    for _ in range(100):
     a=t.read32(address+4)
     if a&1:continue
     d=struct.unpack(fmt,bytes(t.read_memory_block8(address,struct.calcsize(fmt))))
     b=t.read32(address+4)
     if a==d[1]==b and not b&1:
      if d[0]!=magic:raise RuntimeError('ELF/ABI mismatch')
      return d
    raise RuntimeError('Cannot read coherent snapshot')
   image=(root/'build/lsm6dsv_spi_test.bin').read_bytes()
   if bytes(t.read_memory_block8(0x08000000,len(image)))!=image:raise RuntimeError('Flash mismatch')
   meta['full_flash_verified_bytes']=len(image)
   raw=read(rawaddr,'<5I3i',0x4D524157)
   if raw[4] or not raw[3]:raise RuntimeError('Require mag active and fusion OFF')
   first=dict(zip(NAMES,read(tuneaddr,FMT,MAGIC)))
   t.write32(deadlineaddr,(first['millis']+360000)&0xffffffff)
   start=time.monotonic();last=None;previous_ms=first['millis'];last_progress=start
   try:
    with out.open('w',newline='',encoding='utf-8') as f:
     w=csv.DictWriter(f,fieldnames=['stage','phase_s','include','host_s','led_mode','led_green']+NAMES);w.writeheader()
     for stage,mode in [('off_before',1),('fixed50',2),('breathing',0),('off_after',1)]:
      t.write32(modeaddr,mode)
      if t.read32(modeaddr)!=mode:raise RuntimeError('LED mode write failed')
      phase_start=time.monotonic();report=0
      print(f'STAGE {stage}: stay still, 5s settling + 60s recording',flush=True)
      while time.monotonic()-phase_start<65:
       d=dict(zip(NAMES,read(tuneaddr,FMT,MAGIC)))
       now=time.monotonic();phase_s=now-phase_start
       if d['seq']!=last:
        if d['millis']<previous_ms:raise RuntimeError('MCU reset detected')
        if d['mag_updates'] or d['mag_err']:raise RuntimeError('Mag fusion or sensor error')
        led=t.read32(leveladdr);active=t.read32(modeaddr)
        if active!=mode:raise RuntimeError('Unexpected LED mode/expired lease')
        w.writerow(dict(stage=stage,phase_s=phase_s,include=int(phase_s>=5),host_s=now-start,led_mode=active,led_green=led,**d));f.flush()
        meta['samples']+=1;last=d['seq'];previous_ms=d['millis'];last_progress=now
       if now-last_progress>2:raise RuntimeError('Stale telemetry')
       if phase_s>=report:
        r=read(rawaddr,'<5I3i',0x4D524157)
        if r[4] or (report and r[3]==last_raw_n):raise RuntimeError('Mag disabled/stale')
        last_raw_n=r[3]
        print(f"{stage} {phase_s:.0f}s norm={d['mag_norm']:.4f}uT rest={d['rest_detected']} hz={d['fusion_hz']}",flush=True);report+=15
       time.sleep(.005)
    meta['complete']=True
   finally:
    t.write32(modeaddr,0);t.write32(deadlineaddr,0)
    meta['restored']=t.read32(modeaddr)==0
 except Exception as e:
  meta['error']=repr(e);raise
 finally:
  out.with_suffix('.json').write_text(json.dumps(meta,indent=2),encoding='utf-8');print(json.dumps(meta,indent=2),flush=True)

if __name__=='__main__':main()
