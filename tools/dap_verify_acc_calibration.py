"""Read-only non-halting accelerator calibration verification, 30s."""
import argparse,json,struct,subprocess,time
from pathlib import Path
import numpy as np
from pyocd.core.helpers import ConnectHelper
from dap_stationary_30min import NAMES,FMT,MAGIC

def main():
 parser=argparse.ArgumentParser()
 parser.add_argument("--probe", help="Explicit CMSIS-DAP unique ID when multiple probes are attached")
 parser.add_argument("--frequency", type=int, default=100000, help="SWD clock in Hz")
 args=parser.parse_args()
 root=Path(__file__).resolve().parents[1]
 fit=json.loads((root/'build/logs/acc_six_faces_20260912_231659_recheck_z_234958_fit.json').read_text())
 nm=Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
 symbols=subprocess.check_output([str(nm),str(root/'build/lsm6dsv_spi_test.elf')],text=True)
 def addr(n):return next(int(s.split()[0],16) for s in symbols.splitlines() if s.endswith(' '+n))
 a=addr('acc_cal_live');b=addr('vqf_tune_live');m=addr('mag_raw_live')
 rows=[];result={'samples':rows,'complete':False,'error':None,'probe':args.probe,'swd_frequency_hz':args.frequency}
 out=root/'build/logs'/('acc_cal_verified_'+time.strftime('%Y%m%d_%H%M%S')+'.json')
 try:
  session=ConnectHelper.session_with_chosen_probe(unique_id=args.probe, target_override='cortex_m',options={'connect_mode':'attach','frequency':args.frequency,'resume_on_disconnect':False})
  if session is None:raise RuntimeError('No probe')
  with session:
   t=session.target
   if t.get_state().name!='RUNNING':raise RuntimeError('MCU not running')
   image=(root/'build/lsm6dsv_spi_test.bin').read_bytes()
   if bytes(t.read_memory_block8(0x08000000,len(image)))!=image:raise RuntimeError('Flash mismatch')
   result['full_flash_verified_bytes']=len(image)
   def read(address,fmt,magic):
    for _ in range(100):
     before=t.read32(address+4)
     if before&1:continue
     v=struct.unpack(fmt,bytes(t.read_memory_block8(address,struct.calcsize(fmt))))
     after=t.read32(address+4)
     if before==v[1]==after and not after&1:
      if v[0]!=magic:raise RuntimeError('ABI mismatch')
      return v
    raise RuntimeError('Coherent read failed')
   if read(m,'<5I3i',0x4D524157)[4]:raise RuntimeError('Not six-axis')
   start=time.monotonic();last=start;seq=None;ms=None
   while time.monotonic()-start<30:
    v=read(a,'<3I6f',0x4143434C)
    if v[1]==seq:
     if time.monotonic()-last>2:raise RuntimeError('Stale data')
     continue
    if ms is not None and v[2]<=ms:raise RuntimeError('MCU time reversal')
    ms=v[2];seq=v[1];last=time.monotonic()
    raw=np.array(v[3:6]);corrected=np.array(v[6:9])
    expected=(raw-fit['bias_g'])*fit['gain']
    err=float(max(abs(expected-corrected)))
    if not np.isfinite(corrected).all() or err>1e-6:raise RuntimeError('Calibration arithmetic mismatch')
    tune=dict(zip(NAMES,read(b,FMT,MAGIC)))
    if tune['mag_updates']:raise RuntimeError('Mag fusion enabled')
    rows.append({'millis':ms,'raw_g':raw.tolist(),'corrected_g':corrected.tolist(),'max_error_g':err,'tune':tune})
    time.sleep(.015)
   if len(rows)<100:raise RuntimeError('Insufficient samples')
   r=np.array([x['raw_g'] for x in rows]);c=np.array([x['corrected_g'] for x in rows])
   result.update(complete=True,count=len(rows),raw_mean_g=r.mean(0).tolist(),corrected_mean_g=c.mean(0).tolist(),raw_norm_mean_g=float(np.linalg.norm(r,axis=1).mean()),corrected_norm_mean_g=float(np.linalg.norm(c,axis=1).mean()),corrected_std_g=c.std(0).tolist(),max_error_g=max(x['max_error_g'] for x in rows),rest_fraction=float(np.mean([x['tune']['rest_detected'] for x in rows])),fusion_hz_range=[min(x['tune']['fusion_hz'] for x in rows),max(x['tune']['fusion_hz'] for x in rows)],skip_delta=rows[-1]['tune']['skip_n']-rows[0]['tune']['skip_n'],mag_fusion_enabled=False)
 except Exception as e:
  result['error']=repr(e);raise
 finally:
  out.write_text(json.dumps(result,indent=2),encoding='utf-8')
  print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2));print(out)
if __name__=='__main__':main()
