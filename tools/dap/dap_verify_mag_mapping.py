"""Read-only post-flash verification of the magnetic mounting map."""
import json,re,struct,subprocess,time
from pathlib import Path
import numpy as np
from pyocd.core.helpers import ConnectHelper
from dap_stationary_30min import NAMES,FMT,MAGIC

def main():
 root=Path(__file__).resolve().parents[2]
 nm=Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
 syms=subprocess.check_output([str(nm),str(root/'build/lsm6dsv_spi_test.elf')],text=True)
 def addr(n):return next(int(s.split()[0],16) for s in syms.splitlines() if s.endswith(' '+n))
 rawaddr=addr('mag_raw_live');tuneaddr=addr('vqf_tune_live')
 header=(root/'inc/calibration/mag_calibration.h').read_text()
 def floats(n):
  block=re.search(n+r'\[[^;]+?=\s*(\{.*?\});',header,re.S).group(1)
  return np.array([float(v) for v in re.findall(r'([-+]?\d+\.\d+)f',block)])
 offset=floats('mag_cal_offset');matrix=floats('mag_cal_matrix').reshape(3,3)
 result={'matched_samples':0,'checked_pairs':0,'max_component_error_uT':0.0,'samples':[]}
 session=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'attach','frequency':100000,'resume_on_disconnect':False})
 if session is None:raise RuntimeError('No probe')
 with session:
  t=session.target
  if t.get_state().name!='RUNNING':raise RuntimeError('Not running')
  image=(root/'build/lsm6dsv_spi_test.bin').read_bytes()
  actual=bytes(t.read_memory_block8(0x08000000,len(image)))
  if actual!=image:raise RuntimeError('Full flash readback differs from BIN')
  result['full_flash_verified_bytes']=len(image)
  def read(address,fmt,magic):
   for _ in range(100):
    a=t.read32(address+4)
    if a&1:continue
    d=struct.unpack(fmt,bytes(t.read_memory_block8(address,struct.calcsize(fmt))))
    b=t.read32(address+4)
    if a==d[1]==b and not b&1:
     if d[0]!=magic:raise RuntimeError('ABI mismatch')
     return d
   raise RuntimeError('Snapshot busy')
  start=time.monotonic();seen=set()
  while time.monotonic()-start<15:
   raw=read(rawaddr,'<5I3i',0x4D524157)
   tune=dict(zip(NAMES,read(tuneaddr,FMT,MAGIC)))
   if raw[4] or tune['mag_updates']:raise RuntimeError('Mag fusion not OFF')
   if tune['seq'] in seen:continue
   # Require raw reading to precede the tune snapshot by < one mag period.
   age=tune['millis']-raw[2]
   if not 0<=age<19:continue
   result['checked_pairs']+=1;seen.add(tune['seq'])
   calibrated=matrix@(np.array(raw[5:])*.3-offset)
   expected=calibrated[[1,0,2]]*np.array([1,1,-1])
   observed=np.array([tune[n] for n in ['mx','my','mz']])
   error=float(np.max(abs(expected-observed)))
   result['max_component_error_uT']=max(error,result['max_component_error_uT'])
   if error>0.0001:raise RuntimeError(f'Mapping mismatch: {error} uT')
   result['matched_samples']+=1
   result['samples'].append({'millis':tune['millis'],'raw':list(raw[5:]),'expected':expected.tolist(),'observed':observed.tolist(),'norm':tune['mag_norm'],'fusion_hz':tune['fusion_hz'],'skip_n':tune['skip_n'],'mag_err':tune['mag_err'],'mag_updates':tune['mag_updates']})
   time.sleep(.015)
  if result['matched_samples']<10:raise RuntimeError('Insufficient time-matched pairs')
  if any(s['mag_err'] for s in result['samples']):raise RuntimeError('Mag error observed')
  result['mag_fusion_enabled']=False
  result['skip_delta']=result['samples'][-1]['skip_n']-result['samples'][0]['skip_n']
 out=root/'build/logs'/('mag_axis_applied_'+time.strftime('%Y%m%d_%H%M%S')+'.json')
 out.write_text(json.dumps(result,indent=2),encoding='utf-8')
 print(json.dumps({**result,'samples':result['samples'][-3:],'log':str(out)},indent=2))

if __name__=='__main__':main()
