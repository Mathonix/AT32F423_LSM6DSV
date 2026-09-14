"""Automatic multi-pose stationary accelerometer capture via non-halting DAPLink.
No pose labels are needed. First N accepted stationary poses are calibration data,
following M are held-out validation data. Each accepted pose contains 10 s samples.
"""
import argparse,csv,json,struct,subprocess,time
from pathlib import Path
import numpy as np
from pyocd.core.helpers import ConnectHelper
from dap_stationary_30min import NAMES,FMT,MAGIC

ACC_MAGIC=0x4143434C
ACC_FMT='<3I6f'

def main():
 p=argparse.ArgumentParser()
 p.add_argument('--cal-poses',type=int,default=20);p.add_argument('--val-poses',type=int,default=8)
 p.add_argument('--seconds',type=float,default=10.0);p.add_argument('--settle',type=float,default=5.0)
 p.add_argument('--idle-timeout',type=float,default=300.0);p.add_argument('--frequency',type=int,default=50000)
 p.add_argument('--probe');p.add_argument('--min-change-deg',type=float,default=20.0);p.add_argument('--resume',type=Path)
 args=p.parse_args()
 if args.cal_poses<8 or args.val_poses<1: p.error('cal-poses >= 8 and val-poses >= 1 required')
 root=Path(__file__).resolve().parents[2]; folder=root/'build/logs';folder.mkdir(exist_ok=True)
 if args.resume:
  jsonpath=args.resume; meta=json.loads(jsonpath.read_text(encoding='utf-8')); csvpath=Path(meta['csv']);
  if not csvpath.is_absolute(): csvpath=root/csvpath
  if meta.get('complete'): raise RuntimeError('dataset already complete')
  if int(meta['cal_poses'])!=args.cal_poses or int(meta['val_poses'])!=args.val_poses: raise RuntimeError('resume pose counts differ from dataset')
 else:
  stem=folder/('acc_multipose_'+time.strftime('%Y%m%d_%H%M%S')); csvpath=stem.with_suffix('.csv'); jsonpath=stem.with_suffix('.json')
  meta={'complete':False,'cal_poses':args.cal_poses,'val_poses':args.val_poses,'seconds':args.seconds,'settle':args.settle,'min_change_deg':args.min_change_deg,'csv':str(csvpath),'poses':[],'error':None}
 def save():jsonpath.write_text(json.dumps(meta,indent=2),encoding='utf-8')
 nm=Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
 syms=subprocess.check_output([str(nm),str(root/'build/lsm6dsv_spi_test.elf')],text=True)
 def addr(name):return next(int(x.split()[0],16) for x in syms.splitlines() if x.endswith(' '+name))
 accaddr=addr('acc_cal_live'); tuneaddr=addr('vqf_tune_live'); magaddr=addr('mag_raw_live')
 session=ConnectHelper.session_with_chosen_probe(unique_id=args.probe,target_override='cortex_m',options={'connect_mode':'attach','frequency':args.frequency,'resume_on_disconnect':False})
 if session is None: raise RuntimeError('No DAPLink probe')
 save();print('DATASET '+str(jsonpath),flush=True)
 try:
  with session:
   t=session.target
   if t.get_state().name!='RUNNING':raise RuntimeError('MCU is not running')
   def read(a,fmt,magic):
    for _ in range(100):
     before=t.read32(a+4)
     if before&1:continue
     v=struct.unpack(fmt,bytes(t.read_memory_block8(a,struct.calcsize(fmt))))
     after=t.read32(a+4)
     if before==v[1]==after and not after&1:
      if v[0]!=magic:raise RuntimeError(f'ABI mismatch at 0x{a:X}')
      return v
    raise RuntimeError('coherent read timeout')
   if read(magaddr,'<5I3i',0x4D524157)[4]!=0: raise RuntimeError('Mag fusion is enabled; expected six-axis mode')
   field=['host_s','phase','pose','acc_seq','tune_seq']+list(NAMES)+['raw_ax_g','raw_ay_g','raw_az_g','corrected_ax_g','corrected_ay_g','corrected_az_g']
   start=time.monotonic();last_tune=None;last_ms=None;last_progress=start;pose_id=len(meta['poses']);last_dir=None
   if meta['poses']:
    last_dir=np.array(meta['poses'][-1]['mean_raw_g'],float);last_dir/=np.linalg.norm(last_dir)
   append=bool(args.resume and csvpath.exists())
   with csvpath.open('a' if append else 'w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=field)
    if not append:w.writeheader()
    def write_row(tv,av,d,raw,phase,pose):
     row=dict(host_s=time.monotonic()-start,phase=phase,pose=pose,acc_seq=av[1],tune_seq=tv[1],**d)
     row.update(raw_ax_g=float(raw[0]),raw_ay_g=float(raw[1]),raw_az_g=float(raw[2]),corrected_ax_g=float(av[6]),corrected_ay_g=float(av[7]),corrected_az_g=float(av[8]))
     w.writerow(row);f.flush()
    while pose_id<args.cal_poses+args.val_poses:
     tv=read(tuneaddr,FMT,MAGIC); av=read(accaddr,ACC_FMT,ACC_MAGIC); now=time.monotonic()
     if tv[1]==last_tune: 
      if now-last_progress>3:raise RuntimeError('stale tune telemetry')
      continue
     last_tune=tv[1];last_progress=now
     d=dict(zip(NAMES,tv)); raw=np.array(av[3:6]); corr=np.array(av[6:9]); norm=np.linalg.norm(raw)
     gyr=np.array([d['gx']-d['bias_x'],d['gy']-d['bias_y'],d['gz']-d['bias_z']])
     static=bool(d['rest_detected']) and norm>.85 and norm<1.15 and np.linalg.norm(gyr)<1.0
     direction=raw/norm if norm else np.zeros(3)
     changed=last_dir is None or float(np.dot(direction,last_dir))<np.cos(np.deg2rad(args.min_change_deg))
     phase='calibration' if pose_id<args.cal_poses else 'validation'
     write_row(tv,av,d,raw,phase,pose_id)
     if static and changed:
      print(f'DETECT new {phase} pose {pose_id+1}/{args.cal_poses+args.val_poses} raw={raw.round(3).tolist()} -- settling',flush=True)
      settle_start=now; block=[]; candidate_dir=direction.copy()
      while time.monotonic()-settle_start<args.settle:
       tv=read(tuneaddr,FMT,MAGIC); av=read(accaddr,ACC_FMT,ACC_MAGIC); d=dict(zip(NAMES,tv)); raw=np.array(av[3:6]); norm=np.linalg.norm(raw); gyr=np.array([d['gx']-d['bias_x'], d['gy']-d['bias_y'], d['gz']-d['bias_z']])
       write_row(tv,av,d,raw,phase,pose_id)
       if not (bool(d['rest_detected']) and .85<norm<1.15 and np.linalg.norm(gyr)<1.0 and np.dot(raw/norm,candidate_dir)>.985): break
       time.sleep(.006)
      else:
       cap_start=time.monotonic();samples=[]
       while time.monotonic()-cap_start<args.seconds:
        tv=read(tuneaddr,FMT,MAGIC); av=read(accaddr,ACC_FMT,ACC_MAGIC); d=dict(zip(NAMES,tv)); raw=np.array(av[3:6]); norm=np.linalg.norm(raw); gyr=np.array([d['gx']-d['bias_x'], d['gy']-d['bias_y'], d['gz']-d['bias_z']])
        write_row(tv,av,d,raw,phase,pose_id)
        if not (bool(d['rest_detected']) and .85<norm<1.15 and np.linalg.norm(gyr)<1.0 and np.dot(raw/norm,candidate_dir)>.985): break
        samples.append((tv,av,d,raw));time.sleep(.006)
       if len(samples)>=max(30,int(args.seconds*8)):
        xyz=np.array([x[3] for x in samples]);mean=xyz.mean(0);std=xyz.std(0);pose_id+=1;last_dir=mean/np.linalg.norm(mean)
        meta['poses'].append({'pose':pose_id-1,'phase':phase,'count':len(samples),'mean_raw_g':mean.tolist(),'std_raw_g':std.tolist(),'norm_mean_g':float(np.linalg.norm(xyz,axis=1).mean()),'mcu_ms_start':samples[0][0][2],'mcu_ms_end':samples[-1][0][2]});save()
        print(f'POSE COMPLETE {phase} {pose_id}/{args.cal_poses+args.val_poses} mean={mean.round(5).tolist()} std={std.round(5).tolist()}',flush=True)
        continue
     if now-start>args.idle_timeout:raise TimeoutError('idle timeout; dataset is resumable only by restarting')
     time.sleep(.006)
   meta['complete']=True;save();print(json.dumps(meta,indent=2),flush=True)
 except Exception as e:
  meta['error']=repr(e);save();raise

if __name__=='__main__':main()

