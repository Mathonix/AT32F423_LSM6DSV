"""Fit only diagonal accelerometer bias/scale from all six static faces.
No firmware writes. Six face fitting is not independent accuracy validation.
"""
import argparse,json
from pathlib import Path
import numpy as np

def fit(path):
 d=json.loads(path.read_text(encoding='utf-8'));faces=d['faces']
 required=[s+a for a in 'XYZ' for s in '+-']
 missing=[f for f in required if f not in faces]
 if missing:raise ValueError('Missing faces: '+', '.join(missing))
 bias=[];gain=[]
 for i,a in enumerate('XYZ'):
  plus=faces['+'+a]['mean_g'][i];minus=faces['-'+a]['mean_g'][i]
  if plus-minus<=0:raise ValueError('Invalid face sign')
  bias.append((plus+minus)*.5);gain.append(2/(plus-minus))
 bias=np.array(bias);gain=np.array(gain)
 residuals={};errors=[]
 for name in required:
  v=(np.array(faces[name]['mean_g'])-bias)*gain
  target=np.zeros(3);target['XYZ'.index(name[1])]=1 if name[0]=='+' else -1
  residuals[name]={'corrected_mean_g':v.tolist(),'corrected_norm_g':float(np.linalg.norm(v)),'vector_residual_g':(v-target).tolist()}
  errors.append(np.linalg.norm(v)-1)
 warnings=[]
 if max(abs(bias))>.1:warnings.append('Offset exceeds provisional 0.1g plausibility gate')
 if np.any((gain<.9)|(gain>1.1)):warnings.append('Scale outside provisional [0.9,1.1] gate')
 for name in required:
  if max(faces[name]['std_g'])>.006:warnings.append(name+' unstable')
  off=np.delete(residuals[name]['vector_residual_g'],'XYZ'.index(name[1]))
  if max(abs(off))>.05:warnings.append(name+' residual cross-axis >0.05g; check fixture/alignment')
 result={'source':str(path),'bias_g':bias.tolist(),'gain':gain.tolist(),'convention':'corrected_g[i] = (nominal_g[i] - bias_g[i]) * gain[i]',
 'face_results':residuals,'training_norm_rms_g':float(np.sqrt(np.mean(np.array(errors)**2))),'warnings':warnings,'applied':False,
 'limitations':['Reference gravity assumed 1 standard g, not independently measured local gravity','Diagonal model only; no nonorthogonality or board mounting correction','Face tilt can bias fitted scale; same six samples are not validation','Require independent multi-pose check before claiming accuracy']}
 out=path.with_name(path.stem+'_fit.json');out.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2));return result

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('dataset',type=Path);a=p.parse_args();fit(a.dataset)
