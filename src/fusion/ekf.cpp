// SPDX-License-Identifier: MIT
// Right-error MEKF: Joan Sola, Quaternion kinematics for the error-state
// Kalman filter, https://arxiv.org/abs/1711.02508 (prediction/injection/reset).
#include "ekf.hpp"
#include "app_config.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <initializer_list>

namespace ahrs_ekf {
namespace {
constexpr float RAD = 0.0174532925199433f, DEG = 1.0f/RAD, G = 9.80665f;
constexpr float OBS_DT = .005f; // 200 Hz covariance/accelerometer; gyro runs at sensor rate.
constexpr float GRAVITY_DIR_SCALE=.05f; // unit-vector chord, about 2.9 degrees
constexpr float GRAVITY_NORM_SCALE=.15f, GRAVITY_TRANSIENT_SCALE=.30f; // m/s^2
constexpr float MIN_GRAVITY_BIAS_WEIGHT=APP_EKF_GRAVITY_BIAS_MIN_WEIGHT;
State s6, s9;
float dt_g=.0005f, dt_a=.0005f, cov_dt[2], gyro_sum[2][3], last_gyr[3];
float first_cov9_dt=OBS_DT;
float bias_acc_elapsed[2];
float health_elapsed[2];
float tau_acc=2.5f, tau_mag=4, rest_gyr=.6f, rest_acc=.15f;
float sigma_init=.5f, sigma_rest=.035f, acc_lpf[3], rest_gyr_mean[3];
float rest_elapsed=0, mag_norm=0, mag_dip=0, candidate_norm=0, candidate_dip=0;
float candidate_time=0, reject_time=0, last_mag_error=0, last_mag_rate=0;
bool tilt_ready=false, acc_ready=false, gyr_ready=false, obs_due[2]={};
bool first_cov9=true;
bool nine_ready=false, ref_valid=false, rejecting=true, disturbed=false;
bool rest_bias_enabled=APP_VQF_REST_BIAS_ENABLE!=0U;
struct Trace {
  fusion_update_diagnostic_t value;
  float lease_s, anchor_yaw, compensation[4];
};
Trace trace[2];
float yaw(const float q[4]);
float wrap(float a);
void trace_begin(unsigned index) {
  const uint32_t generation=trace[index].value.generation+1;
  std::memset(&trace[index].value,0,sizeof(trace[index].value));
  std::memset(trace[index].compensation,0,sizeof(trace[index].compensation));
  trace[index].value.generation=generation;
  trace[index].anchor_yaw=yaw(index?s9.q:s6.q);
}
void trace_increment(unsigned index,unsigned phase) {
  Trace& t=trace[index];if(t.lease_s<=0)return;
  const float current=yaw(index?s9.q:s6.q);
  const float delta=wrap(current-t.anchor_yaw)*DEG-t.compensation[phase];
  const float sum=t.value.yaw_delta_deg[phase]+delta;
  t.compensation[phase]=(sum-t.value.yaw_delta_deg[phase])-delta;
  t.value.yaw_delta_deg[phase]=sum;
  t.anchor_yaw=current;
}

float norm(const float v[3]) { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
bool valid(const float v[3]) {
  return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2])&&norm(v)<10000;
}
void multiply(const float a[4],const float b[4],float out[4]) {
  float r[4]={a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],
    a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],
    a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],
    a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]};
  std::memcpy(out,r,sizeof(r));
}
void normalize(float q[4]) {
  float n=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
  if(n>1e-12f) for(int i=0;i<4;++i)q[i]/=n;
}
void rotation(const float q[4],float r[3][3]) {
  const float w=q[0],x=q[1],y=q[2],z=q[3];
  r[0][0]=1-2*(y*y+z*z);r[0][1]=2*(x*y-w*z);r[0][2]=2*(x*z+w*y);
  r[1][0]=2*(x*y+w*z);r[1][1]=1-2*(x*x+z*z);r[1][2]=2*(y*z-w*x);
  r[2][0]=2*(x*z-w*y);r[2][1]=2*(y*z+w*x);r[2][2]=1-2*(x*x+y*y);
}
void world(const float q[4],const float b[3],float out[3]) {
  float r[3][3];rotation(q,r);
  for(int i=0;i<3;++i)out[i]=r[i][0]*b[0]+r[i][1]*b[1]+r[i][2]*b[2];
}
void body(const float q[4],const float w[3],float out[3]) {
  float r[3][3];rotation(q,r);
  for(int i=0;i<3;++i)out[i]=r[0][i]*w[0]+r[1][i]*w[1]+r[2][i]*w[2];
}
void skew(const float v[3],float m[3][3]) {
  m[0][0]=0;m[0][1]=-v[2];m[0][2]=v[1];
  m[1][0]=v[2];m[1][1]=0;m[1][2]=-v[0];
  m[2][0]=-v[1];m[2][1]=v[0];m[2][2]=0;
}
void rotate_increment(State& s,const float v[3]) {
  const float angle=norm(v), half=.5f*angle;
  const float k=angle>1e-6f?std::sin(half)/angle:.5f;
  const float dq[4]={std::cos(half),k*v[0],k*v[1],k*v[2]};
  multiply(s.q,dq,s.q);normalize(s.q);
}
void covariance_snapshot(State& s) {
  // Only read-only regression getters call this; the embedded loop stores
  // the factor and never reconstructs a badly conditioned float P.
  for(int i=0;i<6;++i)for(int j=0;j<=i;++j){
    double value=0;
    for(int k=0;k<=j;++k)value+=double(s.root[i][k])*s.root[j][k];
    s.p[i][j]=s.p[j][i]=value;
  }
}
bool positive_covariance(const State& s) {
  for(int i=0;i<6;++i){
    if(!(s.root[i][i]>0))return false;
    for(int j=0;j<6;++j)if(!std::isfinite(s.root[i][j])||(j>i&&s.root[i][j]!=0))return false;
  }
  return true;
}
void reset_uncertainty(State& s) {
  std::memset(s.root,0,sizeof(s.root));
  for(int i=0;i<3;++i){
    s.root[i][i]=.15f;
    s.root[i+3][i+3]=sigma_init*RAD;
  }
}
void recover(State& s,bool bias_fault) {
  // A failed estimate is not a stationary observation. Discard the untrusted
  // residual correction and fall back to the independently supplied startup
  // compensation; retain attitude so recovery does not invent a yaw reference.
  if(bias_fault)++s.bias_recoveries;else ++s.covariance_recoveries;
  std::memset(s.bias,0,sizeof(s.bias));reset_uncertainty(s);
}
bool factor_columns(State& s,float c[6][12],int columns) {
  // Right Givens rotations give [L 0] from C, preserving C*C'. Positive
  // diagonal L represents SPD covariance without a subtractive downdate.
  for(int i=0;i<6;++i)for(int j=columns-1;j>i;--j){
    const float b=c[i][j];if(b==0)continue;
    const float a=c[i][i],r=std::sqrt(a*a+b*b);
    if(!(r>0)||!std::isfinite(r)){recover(s,false);return false;}
    const float cosine=a/r,sine=b/r;
    c[i][i]=r;c[i][j]=0;
    for(int k=i+1;k<6;++k){
      const float x=c[k][i],y=c[k][j];
      c[k][i]=cosine*x+sine*y;c[k][j]=-sine*x+cosine*y;
    }
  }
  for(int i=0;i<6;++i)for(int j=0;j<6;++j)s.root[i][j]=j<=i?c[i][j]:0;
  if(!positive_covariance(s)){recover(s,false);return false;}
  return true;
}
void transform(State& s,const float f[6][6],float noise_attitude=0,float noise_bias=0) {
  float c[6][12]={};
  // F=[A B;0 I]; retain the block prediction/reset optimization.
  for(int i=0;i<3;++i)for(int j=0;j<6;++j)for(int k=j;k<6;++k)c[i][j]+=f[i][k]*s.root[k][j];
  for(int i=3;i<6;++i)for(int j=0;j<=i;++j)c[i][j]=s.root[i][j];
  const bool noise=noise_attitude>0||noise_bias>0;
  if(noise)for(int i=0;i<6;++i)c[i][i+6]=i<3?noise_attitude:noise_bias;
  factor_columns(s,c,noise?12:6);
}
void covariance(State& s,float dt,const float rate[3]) {
  // Exact attitude rotation and second-order bias coupling over this interval.
  float v[3],cross[3][3],f[6][6]={};
  for(int i=0;i<3;++i)v[i]=rate[i]-s.bias[i];
  skew(v,cross);
  const float a=norm(v)*dt,k1=a>1e-5f?std::sin(a)/norm(v):dt;
  const float k2=a>1e-5f?(1-std::cos(a))/(norm(v)*norm(v)):.5f*dt*dt;
  for(int i=0;i<6;++i)f[i][i]=1;
  for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
    float c2=0;for(int k=0;k<3;++k)c2+=cross[i][k]*cross[k][j];
    f[i][j]-=k1*cross[i][j];f[i][j]+=k2*c2;
    f[i][j+3]=-(i==j?dt:0)+.5f*dt*dt*cross[i][j];
  }
  const float gyr_noise=rest_bias_enabled?.015f:APP_EKF_GYRO_NOISE_DPS_SQRT_HZ;
  const float bias_walk=rest_bias_enabled?.0005f:APP_EKF_BIAS_RW_DPS_SQRT_S;
  const float time_root=std::sqrt(dt);
  transform(s,f,gyr_noise*RAD*time_root,bias_walk*RAD*time_root);
}
void initialize(State& s) {
  std::memset(&s,0,sizeof(s));s.q[0]=1;
  reset_uncertainty(s);
}
bool inject(State& s,const float dx[6]) {
  for(int i=0;i<6;++i)if(!std::isfinite(dx[i])){recover(s,i>=3);return false;}
  if(!std::isfinite(norm(dx))||norm(dx)>3.14159265f){recover(s,false);return false;}
  float next_bias[3];
  for(int i=0;i<3;++i){
    next_bias[i]=s.bias[i]+dx[i+3];
    if(!std::isfinite(next_bias[i])||std::fabs(next_bias[i])>APP_VQF_BIAS_CLIP_DPS*RAD){recover(s,true);return false;}
  }
  std::memcpy(s.bias,next_bias,sizeof(next_bias));
  // Bias-only observations have no attitude error to inject/reset. Avoid an
  // identity quaternion renormalization changing its least significant bits.
  if(dx[0]==0&&dx[1]==0&&dx[2]==0)return true;
  rotate_increment(s,dx);
  float reset[6][6]={},cross[3][3];skew(dx,cross);
  for(int i=0;i<6;++i)reset[i][i]=1;
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)reset[i][j]-=.5f*cross[i][j];
  transform(s,reset);
  return true;
}
bool scalar(State& s,const float h[6],float residual,float variance,float dx[6],bool learn_bias,
            const float* unobservable_axis=nullptr,bool bias_only=false) {
  float hl[6]={},ph[6]={},gain[6],innovation=variance,error=residual;
  for(int j=0;j<6;++j)for(int i=j;i<6;++i)hl[j]+=h[i]*s.root[i][j];
  for(int i=0;i<6;++i){
    for(int j=0;j<=i;++j)ph[i]+=s.root[i][j]*hl[j];
    innovation+=hl[i]*hl[i];error-=h[i]*dx[i];
  }
  if(!(variance>0)||!(innovation>1e-12f)||!std::isfinite(innovation)||!std::isfinite(error)){
    recover(s,false);std::memset(dx,0,6*sizeof(float));return false;
  }
  for(int i=0;i<6;++i)gain[i]=(i>=3&&!learn_bias)?0:ph[i]/innovation;
  if(bias_only)for(int i=0;i<3;++i)gain[i]=0;
  if(unobservable_axis){
    // Gravity provides only two direction constraints. Keep its gain out of
    // the current body representation of world vertical, including bias.
    // A body-Z mask is wrong when the board is tilted. Apply this constraint
    // to K itself, so the Joseph covariance uses exactly the applied gain.
    float n2=0;for(int i=0;i<3;++i)n2+=unobservable_axis[i]*unobservable_axis[i];
    if(n2>1e-12f)for(int block=0;block<2;++block){
      float component=0;for(int i=0;i<3;++i)component+=gain[3*block+i]*unobservable_axis[i];
      component/=n2;
      for(int i=0;i<3;++i)gain[3*block+i]-=component*unobservable_axis[i];
    }
  }
  // Square-root Joseph: C=[(I-KH)L, K*sqrt(R)]. This supports projected K
  // and bias-only K, unlike an optimal-gain-only Cholesky downdate.
  float c[6][12]={};const float measurement_root=std::sqrt(variance);
  for(int i=0;i<6;++i){
    for(int j=0;j<6;++j)c[i][j]=s.root[i][j]-gain[i]*hl[j];
    c[i][6]=gain[i]*measurement_root;
  }
  if(!factor_columns(s,c,7)){std::memset(dx,0,6*sizeof(float));return false;}
  for(int i=0;i<6;++i)dx[i]+=gain[i]*error;
  return true;
}
void direction(State& s,const float measured[3],const float reference[3],float variance,bool learn_bias,
               bool gravity=false) {
  float predicted[3],cross[3][3],dx[6]={};body(s.q,reference,predicted);skew(predicted,cross);
  for(int i=0;i<3;++i) {
    float h[6]={cross[i][0],cross[i][1],cross[i][2],0,0,0};
    if(!scalar(s,h,measured[i]-predicted[i],variance,dx,learn_bias,gravity?predicted:nullptr))return;
  }
  inject(s,dx);
}
bool zero_rate(State& s) {
  // A zero-rate observation must agree with the current quiet samples.
  // Never feed a filter tail from a preceding turn into the bias estimator.
  for(int i=0;i<3;++i)if(std::fabs(rest_gyr_mean[i]-last_gyr[i])>rest_gyr*RAD)return false;
  float dx[6]={},r=(sigma_rest*RAD)*(sigma_rest*RAD);
  // A stationary observation measures gyro bias. Do not retrospectively
  // rotate the accumulated heading through P(attitude,bias). The zeroed
  // attitude gain also goes through Joseph, preserving its uncertainty.
  for(int i=0;i<3;++i){
    float h[6]={};h[i+3]=1;
    if(!scalar(s,h,rest_gyr_mean[i]-s.bias[i],r,dx,true,nullptr,true))return false;
  }
  return inject(s,dx);
}
void align_tilt(const float acc[3]) {
  float n=norm(acc);if(n<G*.5f||n>G*1.5f)return;
  float q[4]={1+acc[2]/n,acc[1]/n,-acc[0]/n,0};
  if(q[0]<1e-6f){q[0]=0;q[1]=1;q[2]=q[3]=0;}
  normalize(q);std::memcpy(s6.q,q,sizeof(q));if(!nine_ready)std::memcpy(s9.q,q,sizeof(q));tilt_ready=true;
}
float yaw(const float q[4]) { return std::atan2(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]*q[2]+q[3]*q[3])); }
float wrap(float a){while(a>3.14159265f)a-=6.2831853f;while(a<-3.14159265f)a+=6.2831853f;return a;}
}
void init(float gyr_dt,float acc_dt) {
  dt_g=std::isfinite(gyr_dt)&&gyr_dt>0&&gyr_dt<=.02f?gyr_dt:.0005f;
  dt_a=std::isfinite(acc_dt)&&acc_dt>0&&acc_dt<=.02f?acc_dt:dt_g;
  initialize(s6);initialize(s9);rest_elapsed=0;
  for(unsigned i=0;i<2;++i){trace[i].lease_s=0;trace_begin(i);}
  std::memset(cov_dt,0,sizeof(cov_dt));first_cov9=true;first_cov9_dt=OBS_DT;
  std::memset(bias_acc_elapsed,0,sizeof(bias_acc_elapsed));
  std::memset(health_elapsed,0,sizeof(health_elapsed));
  std::memset(gyro_sum,0,sizeof(gyro_sum));std::memset(last_gyr,0,sizeof(last_gyr));
  std::memset(acc_lpf,0,sizeof(acc_lpf));std::memset(rest_gyr_mean,0,sizeof(rest_gyr_mean));
  tilt_ready=acc_ready=gyr_ready=nine_ready=ref_valid=disturbed=false;
  obs_due[0]=obs_due[1]=false;
  candidate_time=reject_time=mag_norm=mag_dip=candidate_norm=candidate_dip=last_mag_error=last_mag_rate=0;
}
void set_profile(float ta,float tm,float rg,float ra) {
  if(std::isfinite(ta)&&ta>0)tau_acc=ta;
  if(std::isfinite(tm)&&tm>0)tau_mag=tm;
  if(std::isfinite(rg)&&rg>0)rest_gyr=rg;
  if(std::isfinite(ra)&&ra>0)rest_acc=ra;
}
void set_bias_sigmas(float init_dps,float rest_dps) {
  if(std::isfinite(init_dps)&&init_dps>0)sigma_init=init_dps;
  if(std::isfinite(rest_dps)&&rest_dps>0)sigma_rest=rest_dps;
}
void set_rest_bias_enabled(bool enabled) {
  if(rest_bias_enabled==enabled)return;
  rest_bias_enabled=enabled;
  rest_elapsed=0;
  std::memset(bias_acc_elapsed,0,sizeof(bias_acc_elapsed));
  std::memset(rest_gyr_mean,0,sizeof(rest_gyr_mean));
}
void seed_bias(float sigma) {
  if(!std::isfinite(sigma)||sigma<=0||sigma>10)return;
  for(State* s:{&s6,&s9})for(int i=0;i<3;++i) {
    s->bias[i]=0;for(int j=0;j<6;++j)s->root[i+3][j]=0;
    s->root[i+3][i+3]=sigma*RAD;
  }
  rest_elapsed=0;gyr_ready=false;
  std::memset(bias_acc_elapsed,0,sizeof(bias_acc_elapsed));
}
void prime(const float acc[3]) {
  if(!valid(acc))return;
  align_tilt(acc);std::memcpy(acc_lpf,acc,sizeof(acc_lpf));acc_ready=tilt_ready;
}
void gyro(const float gyr[3]) {
  if(!valid(gyr)){
    rest_elapsed=0;gyr_ready=false;
    std::memset(bias_acc_elapsed,0,sizeof(bias_acc_elapsed));return;
  }
  for(int i=0;i<3;++i)last_gyr[i]=gyr[i];
  gyr_ready=true;
  for(unsigned index=0;index<2;++index) {
    if(index==1&&!nine_ready)continue;
    State& s=index==0?s6:s9;
    for(int i=0;i<3;++i)if(!std::isfinite(s.bias[i])||std::fabs(s.bias[i])>APP_VQF_BIAS_CLIP_DPS*RAD){recover(s,true);break;}
    if(trace[index].lease_s>0){trace[index].lease_s-=dt_g;++trace[index].value.updates[0];}
    float v[3];for(int i=0;i<3;++i){v[i]=(gyr[i]-s.bias[i])*dt_g;gyro_sum[index][i]+=gyr[i]*dt_g;}
    rotate_increment(s,v);cov_dt[index]+=dt_g;
    const float deadline=(index==1&&first_cov9)?first_cov9_dt:OBS_DT;
    if(cov_dt[index]+1e-7f>=deadline) {
      float rate[3];for(int i=0;i<3;++i)rate[i]=gyro_sum[index][i]/cov_dt[index];
      covariance(s,cov_dt[index],rate);
      cov_dt[index]=0;std::memset(gyro_sum[index],0,sizeof(gyro_sum[index]));obs_due[index]=true;
      if(index==1)first_cov9=false;
    }
  }
}
void accel(const float acc[3]) {
  if(!valid(acc)||norm(acc)<1e-6f){
    rest_elapsed=0;std::memset(bias_acc_elapsed,0,sizeof(bias_acc_elapsed));return;
  }
  if(!tilt_ready)align_tilt(acc);
  if(!acc_ready){std::memcpy(acc_lpf,acc,sizeof(acc_lpf));acc_ready=true;}
  bool stationary=gyr_ready&&std::fabs(norm(acc)-G)<rest_acc;
  for(int i=0;i<3;++i){
    // Input already has independent startup compensation subtracted. Do not
    // let the estimate being corrected veto its own recovery from stillness,
    // or cancel genuine rotation against a corrupted learned bias.
    if(std::fabs(acc[i]-acc_lpf[i])>rest_acc || std::fabs(last_gyr[i])>rest_gyr*RAD)stationary=false;
    acc_lpf[i]+=dt_a/(.005f+dt_a)*(acc[i]-acc_lpf[i]);
  }
  if(stationary) {
    rest_elapsed+=dt_a;
    // Grow a mean from this continuous quiet interval, then retain the
    // configured smoothing time constant. alpha=1 on its first sample
    // discards all observations from motion or a previous rest interval.
    const float alpha=dt_a/std::min(rest_elapsed,std::max(dt_a,APP_VQF_REST_FILTER_TAU_S));
    for(int i=0;i<3;++i)rest_gyr_mean[i]+=alpha*(last_gyr[i]-rest_gyr_mean[i]);
  } else rest_elapsed=0;
  if(!obs_due[0]&&!obs_due[1])return;
  const float n=norm(acc_lpf),dev=std::fabs(n-G);
  const bool gravity_valid=tilt_ready&&n>1e-6f&&dev<G*.15f&&std::fabs(norm(acc)-G)<G*.15f;
  float measured[3];for(int i=0;i<3;++i)measured[i]=n>1e-6f?acc_lpf[i]/n:0;
  const float reference[3]={0,0,1};
  const float gravity_noise=rest_bias_enabled?.025f:APP_EKF_GRAVITY_NOISE;
  const float variance=gravity_noise*gravity_noise*(tau_acc/2.5f);
  // Near-1g alone does not rule out transient lateral acceleration. Use
  // direction innovation plus magnitude and fast-sample mismatch to reduce
  // its influence. A soft weight still allows tilt recovery after a real
  // initial error; don't permanently reject solely on direction disagreement.
  const float magnitude_error=std::max(dev,std::fabs(norm(acc)-G));
  float transient2=0;for(int i=0;i<3;++i)transient2+=(acc[i]-acc_lpf[i])*(acc[i]-acc_lpf[i]);
  const float dynamic_scale=1+magnitude_error*magnitude_error/(GRAVITY_NORM_SCALE*GRAVITY_NORM_SCALE)
                             +transient2/(GRAVITY_TRANSIENT_SCALE*GRAVITY_TRANSIENT_SCALE);
  const bool learn=APP_VQF_MOTION_BIAS_ENABLE || (rest_bias_enabled&&rest());
  const bool bias_acc_valid=rest_bias_enabled ||
    (dev<=APP_EKF_BIAS_MAX_ACC_NORM_ERROR_MS2 &&
     transient2<=APP_EKF_BIAS_MAX_ACC_TRANSIENT_MS2*APP_EKF_BIAS_MAX_ACC_TRANSIENT_MS2);
  for(unsigned index=0;index<2;++index) {
    if(!obs_due[index])continue;
    obs_due[index]=false;
    State& s=index==0?s6:s9;
    health_elapsed[index]+=OBS_DT;
    if(health_elapsed[index]>=.1f){
      health_elapsed[index]=0;
      if(!positive_covariance(s))recover(s,false);
    }
    trace_increment(index,0);
    float predicted[3];body(s.q,reference,predicted);
    float innovation2=0;for(int i=0;i<3;++i)innovation2+=(measured[i]-predicted[i])*(measured[i]-predicted[i]);
    const float noise_scale=dynamic_scale+innovation2/(GRAVITY_DIR_SCALE*GRAVITY_DIR_SCALE);
    const float weight=gravity_valid?1/noise_scale:0;
    const bool bias_quality=bias_acc_valid && weight>=(rest_bias_enabled?.2f:MIN_GRAVITY_BIAS_WEIGHT);
    if(!rest_bias_enabled){
      // This is acceleration quality confirmation, independent of the rest
      // detector/gyro magnitude: qualified continuous rotation is allowed.
      // A shock must not immediately leak its tilt recovery into learned bias.
      if(bias_quality)bias_acc_elapsed[index]=std::min(APP_EKF_BIAS_ACC_QUALIFY_S,bias_acc_elapsed[index]+OBS_DT);
      else bias_acc_elapsed[index]=0;
    }
    if(trace[index].lease_s>0){
      float dot=0;for(int i=0;i<3;++i)dot+=predicted[i]*measured[i];
      trace[index].value.acc_innovation_deg=std::acos(std::max(-1.0f,std::min(1.0f,dot)))*DEG;
      trace[index].value.acc_norm_error_ms2=norm(acc)-G;
      trace[index].value.acc_weight=weight;
    }
    if(gravity_valid){
      // Do not learn gyro bias from strongly inconsistent gravity samples.
      // Qualified zero-rate observations below remain independent of this.
      direction(s,measured,reference,variance*noise_scale,
                learn&&bias_quality&&(rest_bias_enabled||bias_acc_elapsed[index]>=APP_EKF_BIAS_ACC_QUALIFY_S),true);
      if(trace[index].lease_s>0)++trace[index].value.updates[1];
      trace_increment(index,1);
    } else if(trace[index].lease_s>0)++trace[index].value.gravity_skipped;
    if(rest_bias_enabled&&rest()){
      const bool accepted=zero_rate(s);
      if(accepted&&trace[index].lease_s>0)++trace[index].value.updates[2];
      trace_increment(index,2);
    }
  }
}
int mag(const float field[3]) {
  if(!valid(field)||!tilt_ready)return -1;
  const float n=norm(field);if(n<1e-6f||n>2000)return -1;
  float w[3];world(s6.q,field,w);
  const float dip=-std::asin(std::max(-1.0f,std::min(1.0f,w[2]/n)));
  // Called at the same 10 Hz cadence as VQF. Initial reference needs 5 s;
  // a stable replacement after a disturbance needs 20 s.
  const bool candidate_ok=candidate_time>0&&std::fabs(n-candidate_norm)<.10f*candidate_norm&&std::fabs(dip-candidate_dip)<10*RAD;
  if(candidate_ok){candidate_time+=.1f;candidate_norm+=.1f*(n-candidate_norm);candidate_dip+=.1f*(dip-candidate_dip);}
  else {candidate_time=.1f;candidate_norm=n;candidate_dip=dip;}
  bool good=ref_valid&&std::fabs(n-mag_norm)<.10f*mag_norm&&std::fabs(dip-mag_dip)<10*RAD;
  if(!rejecting || (!ref_valid&&candidate_time>=5.0f) || (!good&&candidate_time>=20.0f)) {
    if(!ref_valid||!good){mag_norm=candidate_norm;mag_dip=candidate_dip;ref_valid=true;}
    good=true;
  }
  disturbed=rejecting&&!good;
  if(disturbed){reject_time+=.1f;last_mag_rate=0;return 0;}
  reject_time=0;
  if(!ref_valid){mag_norm=n;mag_dip=dip;ref_valid=true;}
  if(!nine_ready) {
    s9=s6;const float a=std::atan2(w[0],w[1]);const float dq[4]={std::cos(a*.5f),0,0,std::sin(a*.5f)};
    multiply(dq,s9.q,s9.q);normalize(s9.q);nine_ready=true;
    if(trace[1].lease_s>0)trace_begin(1);
    // Give 9D its own elapsed-time/gyro integral and offset its 200 Hz work
    // by half a 6D period, instead of running both covariance/observations
    // in the same 500 us sensor slot. Only the first interval is shorter.
    cov_dt[1]=0;std::memset(gyro_sum[1],0,sizeof(gyro_sum[1]));obs_due[1]=false;
    first_cov9=true;first_cov9_dt=OBS_DT*.5f-cov_dt[0];
    if(first_cov9_dt<dt_g*.5f)first_cov9_dt+=OBS_DT;
  }
  if(rejecting){mag_norm+=.005f*(n-mag_norm);mag_dip+=.005f*(dip-mag_dip);}
  // North points along world +Y, matching the installed VQF convention.
  const float reference[3]={0,std::cos(mag_dip),-std::sin(mag_dip)};
  float measured[3];for(int i=0;i<3;++i)measured[i]=field[i]/n;
  float before=yaw(s9.q),pw[3];world(s9.q,field,pw);last_mag_error=std::atan2(pw[0],pw[1]);
  trace_increment(1,0);
  direction(s9,measured,reference,(2*RAD)*(2*RAD)*(tau_mag/4),APP_VQF_MOTION_BIAS_ENABLE||(rest_bias_enabled&&rest()));
  if(trace[1].lease_s>0)++trace[1].value.updates[3];
  trace_increment(1,3);
  last_mag_rate=wrap(yaw(s9.q)-before)/.1f;
  return 0;
}
void set_mag_rejection(bool enabled){rejecting=enabled;candidate_time=0;disturbed=false;}
void reset_mag_reference(){ref_valid=false;candidate_time=0;disturbed=rejecting;}
void quat6(float q[4]){std::memcpy(q,s6.q,sizeof(s6.q));}
void quat9(float q[4]){std::memcpy(q,nine_ready?s9.q:s6.q,sizeof(s6.q));}
void bias(float out[3]){std::memcpy(out,s6.bias,sizeof(s6.bias));}
void bias9(float out[3]){std::memcpy(out,nine_ready?s9.bias:s6.bias,sizeof(s6.bias));}
float bias_sigma_dps(){
  const State& s=nine_ready?s9:s6;float largest=0;
  for(int i=3;i<6;++i){float v=0;for(int j=0;j<=i;++j)v+=s.root[i][j]*s.root[i][j];largest=std::max(largest,v);}
  return std::sqrt(largest)*DEG;
}
void bias_sigmas(float& init_dps,float& rest_dps){init_dps=sigma_init;rest_dps=sigma_rest;}
bool rest(){return rest_elapsed>=APP_VQF_REST_MIN_SECONDS;}
float rest_time(){return rest()?rest_elapsed-APP_VQF_REST_MIN_SECONDS:0;}
bool mag_ready(){return nine_ready;}
bool mag_disturbed(){return disturbed;}
float mag_delta_deg(){return nine_ready?wrap(yaw(s9.q)-yaw(s6.q))*DEG:0;}
void diagnostic(float out[8]) {
  out[0]=yaw(s6.q)*DEG;out[1]=mag_norm;out[2]=mag_dip*DEG;out[3]=reject_time;
  out[4]=candidate_time;out[5]=last_mag_rate*DEG;out[6]=last_mag_error*DEG;out[7]=bias_sigma_dps();
}
const State& state6(){covariance_snapshot(s6);return s6;}
bool query_update_diagnostic(unsigned index,fusion_update_diagnostic_t& out) {
  if(index>1)return false;
  if(trace[index].lease_s<=0)trace_begin(index);
  else trace_increment(index,0);
  trace[index].lease_s=2;
  out=trace[index].value;
  return true;
}
const State& state9(){covariance_snapshot(s9);return s9;}
unsigned health_flags(unsigned index){
  if(index>1)return 0;
  const State& s=index?s9:s6;
  return (s.covariance_recoveries?4U:0U)|(s.bias_recoveries?8U:0U);
}
}
