"""Offline validation; never fits/replaces calibration or writes the target.
Axis candidates map calibrated IST8310 column vectors into the IMU frame.
Scores are diagnostics, not proof of physical board-axis orientation.
"""
import argparse,csv,itertools,json
from pathlib import Path
import numpy as np

def analyze(path):
    with path.open(encoding='utf-8-sig') as f: rows=list(csv.DictReader(f))
    if len(rows)<20: raise ValueError('Not enough samples')
    def cols(names): return np.array([[float(r[k]) for k in names.split()] for r in rows])
    t=cols('millis')[:,0]*.001
    m=cols('mx my mz'); acc=cols('ax ay az')
    gyro=cols('gx gy gz')-cols('bias_x bias_y bias_z')
    if not all(np.isfinite(x).all() for x in (t,m,acc,gyro)) or np.any(np.diff(t)<=0): raise ValueError('Invalid/nonmonotonic data')
    norm=np.linalg.norm(m,axis=1); an=np.linalg.norm(acc,axis=1)
    if np.min(norm)<=0 or np.min(an)<=0: raise ValueError('Zero sensor vectors')
    speed=np.linalg.norm(gyro,axis=1)
    u=m/norm[:,None]; gravity=acc/an[:,None]
    az=((np.arctan2(u[:,1],u[:,0])+np.pi)/(2*np.pi)*12).astype(int)%12
    zz=np.clip(((u[:,2]+1)*3).astype(int),0,5)
    bins=np.bincount(zz*12+az,minlength=72)
    valid=(abs(an-1)<.08)&(speed<60)
    # Central difference across four intervals reduces quantization noise.
    # Use only contiguous time windows; the 20Hz telemetry has asynchronous mag data.
    k=2; dt=t[2*k:]-t[:-2*k]
    omega=np.deg2rad(sum(gyro[i:len(t)-2*k+i] for i in range(2*k+1))/(2*k+1))
    mm=m[k:-k]
    dm=(m[2*k:]-m[:-2*k])/dt[:,None]
    moving=(speed[k:-k]>8)&(speed[k:-k]<120)&(abs(an[k:-k]-1)<.12)&(dt>.12)&(dt<.35)
    scores=[]
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((-1,1),repeat=3):
            R=np.eye(3)[list(perm)]*np.array(signs)[:,None]
            det=round(np.linalg.det(R))
            mapped=m@R.T
            dots=np.sum(mapped/norm[:,None]*gravity,axis=1)
            pred=-np.cross(omega,mm@R.T)
            measured=dm@R.T
            residual=measured-pred
            rms=lambda x: float(np.sqrt(np.mean(np.sum(x*x,axis=1))))
            dyn=rms(residual[moving]) if moving.any() else None
            denom=rms(measured[moving]) if moving.any() else 0
            label=[('+' if s>0 else '-')+'xyz'[i] for s,i in zip(signs,perm)]
            scores.append({'map_to_imu':label,'det':det,'matrix':R.astype(int).tolist(),
                'gravity_dot_std':float(np.std(dots[valid])) if valid.any() else None,
                'gravity_dot_mean':float(np.mean(dots[valid])) if valid.any() else None,
                'dynamic_residual_uT_s':dyn,'dynamic_relative_residual':dyn/denom if denom>0 else None})
    proper=[s for s in scores if s['det']==1]
    key=lambda s:s['dynamic_residual_uT_s'] if s['dynamic_residual_uT_s'] is not None else float('inf')
    identity=next(s for s in proper if s['map_to_imu']==['+x','+y','+z'])
    result={'csv':str(path),'samples':len(rows),'duration_s':float(t[-1]-t[0]),
        'norm_uT':{'mean':float(norm.mean()),'std':float(norm.std()),'cv_pct':float(norm.std()/norm.mean()*100),'min':float(norm.min()),'max':float(norm.max()),'p5_p50_p95':np.percentile(norm,[5,50,95]).tolist()},
        'reference_fit_radius_uT':36.530349,'rms_relative_to_fit_radius_pct':float(np.sqrt(np.mean((norm/36.530349-1)**2))*100),
        'coverage_bins_occupied_of_72':int(sum(bins>0)),'coverage_bins_at_least10':int(sum(bins>=10)),
        'octants_of_8':len(set(tuple(v) for v in (m>0))),
        'gyro_rms_each_axis_dps':np.sqrt(np.mean(gyro**2,axis=0)).tolist(),
        'gyro_max_abs_each_axis_dps':np.max(abs(gyro),axis=0).tolist(),
        'gravity_eligible_samples':int(valid.sum()),'dynamic_eligible_samples':int(moving.sum()),
        'sample_dt_median_max_s':[float(np.median(np.diff(t))),float(np.max(np.diff(t)))],
        'mag_error_values':sorted(set(int(r['mag_err']) for r in rows)),
        'skip_delta':int(rows[-1]['skip_n'])-int(rows[0]['skip_n']),
        'mag_updates_last':int(rows[-1]['mag_updates']),
        'identity':identity,'proper_candidates_by_dynamic':sorted(proper,key=key),
        'proper_candidates_by_gravity':sorted(proper,key=lambda s:s['gravity_dot_std'] if s['gravity_dot_std'] is not None else float('inf')),
        'best_improper_diagnostic':min((s for s in scores if s['det']==-1),key=key),
        'limitations':['20Hz asynchronous sensor snapshot; slow rotations required','Magnitude is invariant to orthogonal axis mapping','Insufficient multi-axis motion cannot validate mapping','No automatic acceptance threshold or firmware change']}
    out=path.with_name(path.stem+'_analysis.json');out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    short={k:v for k,v in result.items() if not k.startswith('proper_candidates')}
    short['top3_dynamic']=result['proper_candidates_by_dynamic'][:3]
    short['top3_gravity']=result['proper_candidates_by_gravity'][:3]
    print(json.dumps(short,indent=2));return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('csv',type=Path);a=p.parse_args();analyze(a.csv)
