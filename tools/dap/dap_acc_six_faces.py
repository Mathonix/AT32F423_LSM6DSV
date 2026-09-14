"""Interactive six-face accelerometer capture; read-only, non-halting.
Uses uncorrected main.c ax/ay/az = raw integer * nominal 0.000122g/LSB.
Never applies coefficients automatically. Resume preserves completed faces.
"""
import argparse,csv,json,struct,subprocess,time
from pathlib import Path
import numpy as np
from pyocd.core.helpers import ConnectHelper
from dap_stationary_30min import NAMES,FMT,MAGIC

def main():
 p=argparse.ArgumentParser();p.add_argument('--resume',type=Path);p.add_argument('--idle-timeout',type=float,default=60);p.add_argument('--frequency',type=int,default=100000);args=p.parse_args()
 root=Path(__file__).resolve().parents[2]
 out=args.resume or root/'build/logs'/('acc_six_faces_'+time.strftime('%Y%m%d_%H%M%S')+'.json')
 meta=json.loads(out.read_text(encoding='utf-8')) if args.resume else {'faces':{},'complete':False,'units':'g nominal','nominal_g_per_lsb':.000122,'calibration_applied':False}
 nm=Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
 syms=subprocess.check_output([str(nm),str(root/'build/lsm6dsv_spi_test.elf')],text=True)
 def addr(name):return next(int(s.split()[0],16) for s in syms.splitlines() if s.endswith(' '+name))
 address=addr('vqf_tune_live');rawaddr=addr('mag_raw_live')
 logfile=out.with_name(out.stem+'_samples_'+time.strftime('%H%M%S')+'.csv')
 meta.setdefault('sample_files',[]).append(str(logfile));meta['error']=None
 def save():out.write_text(json.dumps(meta,indent=2),encoding='utf-8')
 save();print('DATASET '+str(out),flush=True)
 session=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'attach','frequency':args.frequency,'resume_on_disconnect':False})
 try:
  if session is None:raise RuntimeError('No probe')
  with session:
   target=session.target
   if target.get_state().name!='RUNNING':raise RuntimeError('MCU not running')
   def read(a,fmt,magic):
    for _ in range(100):
     before=target.read32(a+4)
     if before&1:continue
     v=struct.unpack(fmt,bytes(target.read_memory_block8(a,struct.calcsize(fmt))))
     after=target.read32(a+4)
     if before==v[1]==after and not after&1:
      if v[0]!=magic:raise RuntimeError('ELF/ABI mismatch')
      return v
    raise RuntimeError('Coherent read timeout')
   if read(rawaddr,'<5I3i',0x4D524157)[4]!=0:raise RuntimeError('Need 6D mode')
   start=time.monotonic();last_new=start;last_progress=start;last_seq=None;last_ms=None
   candidate=None;settle=None;capture=None;block=[];report=0
   with logfile.open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=['host_s','face','phase']+NAMES);w.writeheader()
    while len(meta['faces'])<6:
     v=read(address,FMT,MAGIC);now=time.monotonic()
     if v[1]==last_seq:
      if now-last_progress>2:raise RuntimeError('Stale telemetry')
      continue
     d=dict(zip(NAMES,v));last_seq=v[1];last_progress=now
     if last_ms is not None and d['millis']<=last_ms:raise RuntimeError('MCU reset/timestamp reversal')
     last_ms=d['millis']
     if d['mag_updates']!=0:raise RuntimeError('Unexpected mag fusion')
     acc=np.array([d[k] for k in ['ax','ay','az']]);gyr=np.array([d['g'+a]-d['bias_'+a] for a in 'xyz'])
     axis=int(np.argmax(abs(acc)));face=('+' if acc[axis]>0 else '-')+'XYZ'[axis]
     aligned=.85<abs(acc[axis])<1.15 and np.max(abs(np.delete(acc,axis)))<.10
     still=aligned and bool(d['rest_detected']) and np.linalg.norm(gyr)<1.0
     if not still or face in meta['faces']:
      candidate=None;settle=None;capture=None;block=[]
     elif candidate!=face:
      candidate=face;settle=now;capture=None;block=[]
      print(f'DETECT {face}: keep still, settling 5s then recording 10s',flush=True)
     elif now-settle>=5:
      if capture is None:capture=now
      block.append(d)
      if now-capture>=10:
       data=np.array([[r[k] for k in ['ax','ay','az']] for r in block]);mean=data.mean(axis=0);std=data.std(axis=0)
       if len(block)>=100 and max(std)<.006 and np.max(abs(np.delete(mean,axis)))<.06:
        meta['faces'][face]={'count':len(block),'mean_g':mean.tolist(),'std_g':std.tolist(),'mean_raw_counts':(mean/.000122).tolist(),'mcu_ms_start':block[0]['millis'],'mcu_ms_end':block[-1]['millis'],'max_gyro_speed_dps':float(max(np.linalg.norm([r['g'+a]-r['bias_'+a] for a in 'xyz']) for r in block))}
        meta['complete']=len(meta['faces'])==6;save();last_new=now
        print(f"FACE COMPLETE {face} ({len(meta['faces'])}/6) mean={mean.tolist()} std={std.tolist()} -- TURN TO ANOTHER FACE NOW",flush=True)
       else:
        print(f'REJECT {face}: n={len(block)} mean={mean.tolist()} std={std.tolist()}; improve support/alignment',flush=True)
       candidate=None;settle=None;capture=None;block=[]
     phase='capture' if capture is not None else 'settling' if candidate else 'waiting'
     w.writerow(dict(host_s=now-start,face=face,phase=phase,**d));f.flush()
     if now-start>=report:
      print(f"{now-start:.0f}s {phase} face={face} acc={acc.round(4).tolist()} rest={d['rest_detected']} completed={list(meta['faces'])}",flush=True);report+=10
     if now-last_new>args.idle_timeout:
      print('PAUSED: no additional completed face before idle timeout; resume dataset for remaining faces.',flush=True);break
     time.sleep(.006)
 except Exception as e:
  meta['error']=repr(e);raise
 finally:
  save();print(json.dumps({'dataset':str(out),'complete':meta['complete'],'completed':list(meta['faces']),'error':meta['error']},indent=2),flush=True)

if __name__=='__main__':main()
