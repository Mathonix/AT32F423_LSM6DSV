"""9D stationary initialization + 300s observation; never halt/reset capture.
Only target writes are temporary LED override, restored with timeout fallback.
"""
import csv,json,struct,subprocess,time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper
from dap_stationary_30min import NAMES
EXTRA='yaw6 mag_ref_norm mag_ref_dip_deg mag_reject_s mag_candidate_s mag_corr_dps mag_disagreement_deg bias_sigma_dps'.split()
FMT='<2I6I19fi4I8f'

def main():
 root=Path(__file__).resolve().parents[1]
 nm=Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
 syms=subprocess.check_output([str(nm),str(root/'build/lsm6dsv_spi_test.elf')],text=True)
 def addr(n):return next(int(s.split()[0],16) for s in syms.splitlines() if s.endswith(' '+n))
 address=addr('vqf_nine_live');rawaddr=addr('mag_raw_live');modeaddr=addr('ws2812_test_mode');deadlineaddr=addr('ws2812_test_until_ms')
 out=root/'build/logs'/('nine_static_'+time.strftime('%Y%m%d_%H%M%S')+'.csv')
 meta={'csv':str(out),'complete':False,'restored_led':False,'error':None,'samples':0,'stable_duration_requested_s':300,'initialization_min_s':30,'initialization_timeout_s':120,'frequency':100000}
 session=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'attach','frequency':100000,'resume_on_disconnect':False})
 try:
  if session is None:raise RuntimeError('No probe')
  with session:
   t=session.target
   if t.get_state().name!='RUNNING':raise RuntimeError('MCU not running')
   def read(a,fmt,magic):
    for _ in range(100):
     before=t.read32(a+4)
     if before&1:continue
     d=struct.unpack(fmt,bytes(t.read_memory_block8(a,struct.calcsize(fmt))))
     after=t.read32(a+4)
     if before==d[1]==after and not after&1:
      if d[0]!=magic:raise RuntimeError('ELF/ABI mismatch')
      return d
    raise RuntimeError('No coherent diagnostic snapshot')
   raw=read(rawaddr,'<5I3i',0x4D524157)
   if raw[4]!=1:raise RuntimeError('Mag fusion is not enabled')
   d0=read(address,FMT,0x39565146)
   t.write32(deadlineaddr,(d0[4]+600000)&0xffffffff);t.write32(modeaddr,1)
   try:
    image=(root/'build/lsm6dsv_spi_test.bin').read_bytes()
    if bytes(t.read_memory_block8(0x08000000,len(image)))!=image:raise RuntimeError('Full flash mismatch')
    meta['full_flash_verified_bytes']=len(image)
    start=time.monotonic();last_seq=None;last_progress=start;ready_since=None;stable_start=None;stable_mcu=None;report=0;last_ms=None
    with out.open('w',newline='',encoding='utf-8') as f:
     w=csv.DictWriter(f,fieldnames=['host_s','phase','phase_s']+NAMES+EXTRA);w.writeheader()
     while True:
      vals=read(address,FMT,0x39565146);now=time.monotonic()
      if vals[1]==last_seq:
       if now-last_progress>2:raise RuntimeError('Stale telemetry')
       continue
      d=dict(zip(NAMES+EXTRA,vals[2:]));last_seq=vals[1];last_progress=now
      if last_ms is not None and d['millis']<=last_ms:raise RuntimeError('Timestamp reset/nonmonotonic')
      last_ms=d['millis']
      if d['mag_err']:raise RuntimeError('Mag sensor error')
      ready=d['mag_ref_norm']>0 and d['mag_disturbed']==0 and d['rest_detected'] and d['mag_updates']>0
      if ready:
       if ready_since is None:ready_since=now
      else:ready_since=None
      if stable_start is None and now-start>=30 and ready_since is not None and now-ready_since>=10:
       stable_start=now;stable_mcu=d['millis'];meta['stable_start_mcu_ms']=stable_mcu
       print('REFERENCE READY: begin full 300s observation; keep still.',flush=True)
      phase='initialization' if stable_start is None else 'stability'
      phase_s=(now-start) if stable_start is None else (d['millis']-stable_mcu)/1000
      w.writerow(dict(host_s=now-start,phase=phase,phase_s=phase_s,**d));f.flush();meta['samples']+=1
      if now-start>=report:
       raw=read(rawaddr,'<5I3i',0x4D524157)
       if raw[4]!=1 or t.read32(modeaddr)!=1:raise RuntimeError('Fusion/LED state changed')
       if report and raw[3]==prev_raw_n:raise RuntimeError('Mag sensor stale')
       prev_raw_n=raw[3]
       print(f"{phase} {phase_s:.0f}s yaw9={d['yaw']:.5f} yaw6={d['yaw6']:.5f} ref={d['mag_ref_norm']:.3f} disturbed={d['mag_disturbed']} rest={d['rest_detected']} skip={d['skip_n']} hz={d['fusion_hz']}",flush=True);report+=30
      if stable_start is not None and phase_s>=300:
       meta['complete']=True;meta['stable_elapsed_mcu_s']=phase_s;break
      if stable_start is None and now-start>120:raise RuntimeError('No stable magnetic reference/rest within 120s')
      time.sleep(.006)
   finally:
    t.write32(modeaddr,0);t.write32(deadlineaddr,0);meta['restored_led']=t.read32(modeaddr)==0
 except Exception as e:
  meta['error']=repr(e);raise
 finally:
  out.with_suffix('.json').write_text(json.dumps(meta,indent=2),encoding='utf-8');print(json.dumps(meta,indent=2),flush=True)

if __name__=='__main__':main()
