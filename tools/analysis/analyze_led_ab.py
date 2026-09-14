"""Analyze stationary LED A/B capture without fitting/changing calibration.
No significance claims: adjacent samples are correlated; one fixed-order trial.
"""
import argparse,csv,json
from pathlib import Path
import numpy as np

def analyze(path):
 with path.open(encoding='utf-8-sig') as f:allrows=list(csv.DictReader(f))
 stages=['off_before','fixed50','breathing','off_after'];results={}
 for stage in stages:
  rows=[r for r in allrows if r['stage']==stage and int(r['include'])]
  if len(rows)<30:raise RuntimeError('Incomplete stage: '+stage)
  def col(n):return np.array([float(r[n]) for r in rows])
  t=col('millis')*.001;t-=t[0]
  m=np.column_stack([col(n) for n in ['mx','my','mz']]);norm=np.linalg.norm(m,axis=1)
  acc=np.column_stack([col(n) for n in ['ax','ay','az']])
  g=np.column_stack([col('g'+axis)-col('bias_'+axis) for axis in 'xyz'])
  speed=np.linalg.norm(g,axis=1);level=col('led_green')
  # Fit linear drift and a 3s sinusoid jointly, not using FFT-bin rounding.
  X=np.column_stack([np.ones(len(t)),t-t.mean(),np.sin(2*np.pi*t/3),np.cos(2*np.pi*t/3)])
  base=X[:,:2];b=np.linalg.lstsq(base,norm,rcond=None)[0]
  detrended=norm-base@b
  y=np.column_stack([m,norm]);p=np.linalg.lstsq(X,y,rcond=None)[0]
  amp=np.hypot(p[2],p[3])
  corr=[];slopes=[]
  for v in y.T:
   vr=v-base@np.linalg.lstsq(base,v,rcond=None)[0]
   lr=level-base@np.linalg.lstsq(base,level,rcond=None)[0]
   corr.append(float(np.corrcoef(vr,lr)[0,1]) if np.std(level)>0 and np.std(vr)>0 else None)
   slopes.append(float(lr@vr/(lr@lr)) if np.std(level)>0 else None)
  results[stage]={'samples':len(rows),'duration_s':float(t[-1]),'norm_mean_uT':float(norm.mean()),'norm_std_uT':float(norm.std()),'norm_cv_pct':float(norm.std()/norm.mean()*100),
   'norm_min_max_uT':[float(norm.min()),float(norm.max())],'norm_p5_p95_uT':np.percentile(norm,[5,95]).tolist(),'norm_detrended_std_uT':float(detrended.std()),'norm_linear_slope_uT_min':float(b[1]*60),
   'mag_mean_xyz_uT':m.mean(axis=0).tolist(),'mag_std_xyz_uT':m.std(axis=0).tolist(),
   'three_second_amplitude_xyz_norm_uT':amp.tolist(),'led_detrended_correlation_xyz_norm':corr,'led_slope_xyz_norm_uT_per_code':slopes,
   'led_code_min_max':[float(level.min()),float(level.max())],'rest_fraction':float(np.mean(col('rest_detected')!=0)),
   'gyro_speed_rms_max_dps':[float(np.sqrt(np.mean(speed**2))),float(speed.max())],'acc_mean_xyz_g':acc.mean(axis=0).tolist(),'acc_std_xyz_g':acc.std(axis=0).tolist(),
   'mag_err_values':np.unique(col('mag_err')).astype(int).tolist(),'fusion_hz_min_max':[float(col('fusion_hz').min()),float(col('fusion_hz').max())],
   'skip_delta':int(col('skip_n')[-1]-col('skip_n')[0]),'max_snapshot_gap_s':float(np.max(np.diff(t)))}
 first=results['off_before'];last=results['off_after']
 result={'csv':str(path),'stages':results,
 'off_return_norm_mean_delta_uT':last['norm_mean_uT']-first['norm_mean_uT'],
 'off_return_vector_delta_xyz_uT':(np.array(last['mag_mean_xyz_uT'])-first['mag_mean_xyz_uT']).tolist(),
 'fixed_minus_initial_off_mean_uT':results['fixed50']['norm_mean_uT']-first['norm_mean_uT'],
 'breathing_minus_initial_off_mean_uT':results['breathing']['norm_mean_uT']-first['norm_mean_uT'],
 'limitations':['Single orientation and fixed stage order; does not prove behavior in all orientations','Commanded brightness is not measured LED current/light output','LED and sensor fields are asynchronously read; 3s correlations are approximate','MCU snapshot about20Hz cannot rule out higher-frequency PWM effects','No temperature/current telemetry; slow drift is a confounder','Brightness values are 8-bit channel codes, not percentages']}
 out=path.with_name(path.stem+'_analysis.json');out.write_text(json.dumps(result,indent=2),encoding='utf-8')
 print(json.dumps(result,indent=2))
 return result

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('csv',type=Path);a=p.parse_args();analyze(a.csv)
