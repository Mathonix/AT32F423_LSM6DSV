"""Fit full 3x3 accelerometer ellipsoid calibration and validate held-out poses."""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares

def main():
 p=argparse.ArgumentParser();p.add_argument('dataset',type=Path);p.add_argument('--use-samples',action='store_true');args=p.parse_args()
 d=json.loads(args.dataset.read_text(encoding='utf-8')); csvp=Path(d['csv']);
 if not csvp.is_absolute(): csvp=Path(__file__).resolve().parents[2]/csvp
 import csv
 rows=list(csv.DictReader(csvp.open(encoding='utf-8'))); caln=int(d['cal_poses'])
 poses={int(x['pose']):x for x in d['poses']}; calposes=list(range(caln)); valposes=list(range(caln,caln+int(d['val_poses'])))
 X=np.array([[float(r['raw_ax_g']),float(r['raw_ay_g']),float(r['raw_az_g'])] for r in rows if int(r['pose'])<caln])
 if len(X)<caln*30 or np.linalg.matrix_rank(X-X.mean(0))<3:raise RuntimeError('insufficient calibration samples or pose diversity')
 # symmetric positive-definite A: corrected = sqrt(A) @ (raw-b), norm=1
 def unpack(q):
  b=q[:3]; A=np.array([[q[3],q[4],q[5]],[q[4],q[6],q[7]],[q[5],q[7],q[8]]]); return b,A
 q0=np.array([*X.mean(0),1,0,0,1,0,1.],float)
 def fun(q):
  b,A=unpack(q); return np.einsum('ni,ij,nj->n',X-b,A,X-b)-1
 fit=least_squares(fun,q0,loss='soft_l1',f_scale=.003,max_nfev=5000)
 b,A=unpack(fit.x);A=(A+A.T)/2;eig,V=np.linalg.eigh(A)
 if np.min(eig)<=0:raise RuntimeError(f'non-positive ellipsoid matrix eigenvalues {eig}')
 M=V@np.diag(np.sqrt(eig))@V.T
 def stats(indices):
  Y=np.array([[float(r['raw_ax_g']),float(r['raw_ay_g']),float(r['raw_az_g'])] for r in rows if int(r['pose']) in indices]);C=(M@(Y-b).T).T; n=np.linalg.norm(C,axis=1)
  return {'samples':len(Y),'poses':len(indices),'corrected_mean_g':C.mean(0).tolist(),'corrected_norm_mean_g':float(n.mean()),'norm_rmse_mg':float(np.sqrt(np.mean((n-1)**2))*1000),'norm_bias_mg':float((n.mean()-1)*1000),'corrected_std_g':C.std(0).tolist()}
 out={'source':str(args.dataset),'model':'corrected_g = M @ (raw_g - bias_g)','bias_g':b.tolist(),'matrix':M.tolist(),'ellipsoid_A':A.tolist(),'eigenvalues_A':eig.tolist(),'optimizer_cost':float(2*fit.cost),'optimality':float(fit.optimality),'calibration':stats(calposes),'validation':stats(valposes),'training_poses':calposes,'validation_poses':valposes,'warnings':[]}
 if out['validation']['norm_rmse_mg']>out['calibration']['norm_rmse_mg']*1.5:out['warnings'].append('held-out validation error is >1.5x training error')
 if out['validation']['norm_rmse_mg']>5:out['warnings'].append('validation norm RMSE >5 mg')
 path=args.dataset.with_name(args.dataset.stem+'_fit.json');path.write_text(json.dumps(out,indent=2),encoding='utf-8');print(json.dumps(out,indent=2));print('FIT '+str(path))
if __name__=='__main__':main()
