"""Offline 9D stationary analysis; report observations, not true heading accuracy."""
import argparse,csv,json
from pathlib import Path
import numpy as np

def analyze(path):
 with path.open(encoding='utf-8-sig') as f: allrows=list(csv.DictReader(f))
 rows=[r for r in allrows if r['phase']=='stability']
 if len(rows)<30:raise RuntimeError('Insufficient stable observation')
 def col(key,subset=rows):return np.array([float(r[key]) for r in subset])
 t=col('millis')*.001;t-=t[0]
 if t[-1]<299.9:raise RuntimeError('Five minute capture incomplete')
 def stats(v):
  slope,intercept=np.polyfit(t,v,1)
  detrended=v-(intercept+slope*t)
  return {'start':float(v[0]),'end':float(v[-1]),'endpoint_delta':float(v[-1]-v[0]),'mean':float(v.mean()),'std':float(v.std()),'peak_to_peak':float(np.ptp(v)),'linear_slope_per_min':float(slope*60),'detrended_std':float(detrended.std()),'p5_p95':np.percentile(v,[5,95]).tolist(),'minute_means':[float(v[(t>=i*60)&(t<(i+1)*60)].mean()) for i in range(5)]}
 angles={}
 for k in ['yaw','yaw6','roll','pitch']:
  angles[k]=stats(np.rad2deg(np.unwrap(np.deg2rad(col(k)))))
 mag=np.linalg.norm(np.column_stack([col(k) for k in ['mx','my','mz']]),axis=1)
 gyro=np.column_stack([col('g'+a)-col('bias_'+a) for a in 'xyz'])
 ready=[r for r in allrows if float(r['mag_ref_norm'])>0 and int(r['mag_disturbed'])==0]
 result={'csv':str(path),'total_samples':len(allrows),'stability_samples':len(rows),'stability_duration_s':float(t[-1]),'first_captured_mcu_ms':int(allrows[0]['millis']),
 'first_observed_undisturbed_reference_mcu_ms':int(ready[0]['millis']) if ready else None,'angles_deg':angles,
 'mag_norm_uT':stats(mag),'mag_norm_cv_pct':float(mag.std()/mag.mean()*100),
 'mag_ref_norm_start_end':[float(col('mag_ref_norm')[0]),float(col('mag_ref_norm')[-1])],
 'mag_ref_dip_deg_start_end':[float(col('mag_ref_dip_deg')[0]),float(col('mag_ref_dip_deg')[-1])],
 'rest_fraction':float(np.mean(col('rest_detected')!=0)), 'disturbed_fraction':float(np.mean(col('mag_disturbed')!=0)),
 'mag_update_rate_hz':float((col('mag_updates')[-1]-col('mag_updates')[0])/t[-1]),
 'mag_correction_rate_dps_rms_max':[float(np.sqrt(np.mean(col('mag_corr_dps')**2))),float(np.max(abs(col('mag_corr_dps'))))],
 'nonzero_mag_correction_fraction':float(np.mean(abs(col('mag_corr_dps'))>1e-8)),
 'bias_dps_start':{a:float(col('bias_'+a)[0]) for a in 'xyz'},'bias_dps_end':{a:float(col('bias_'+a)[-1]) for a in 'xyz'},
 'bias_sigma_dps_start_end':[float(col('bias_sigma_dps')[0]),float(col('bias_sigma_dps')[-1])],
 'gyro_corrected_speed_dps_rms_max':[float(np.sqrt(np.mean(np.sum(gyro**2,axis=1)))),float(np.max(np.linalg.norm(gyro,axis=1)))],
 'fusion_hz_min_max':[int(col('fusion_hz').min()),int(col('fusion_hz').max())],
 'skip_delta_stability':int(col('skip_n')[-1]-col('skip_n')[0]),'skip_first_last_entire_capture':[int(allrows[0]['skip_n']),int(allrows[-1]['skip_n'])],
 'mag_error_values':np.unique(col('mag_err')).astype(int).tolist(),'max_snapshot_gap_s':float(np.max(np.diff(t))),
 'limitations':['Static stability is not absolute heading accuracy or cross-orientation validation','Six-axis comparison is simultaneous output of same VQF, not a separate firmware baseline','Mag reference first observed timestamp is not exact establishment time','Default magnetic disturbance rejection retained; existing magNewMinGyr=0 retained','LED restored after capture; magnetic fusion remains enabled']}
 path.with_name(path.stem+'_analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
 print(json.dumps(result,indent=2));return result

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('csv',type=Path);a=p.parse_args();analyze(a.csv)
