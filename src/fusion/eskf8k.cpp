// Standalone 8 kHz, 3-state attitude error-state KF (not a copy of CH32 code).
// Quaternion nominal state + 3x3 attitude covariance, residual stationary bias.
#include "eskf8k.hpp"
#include <cmath>
#include <cstring>

namespace ahrs_eskf8k {
namespace {
constexpr float PI=3.14159265358979323846f, RAD=PI/180.f, G=9.80665f;
constexpr float RATE=8000.f, ACC_RATE=2000.f, ACC_OBS_DIV=10.f;
constexpr float GYRO_NOISE=0.015f*RAD; // rad/sqrt(s), initial tuning value
constexpr float ACC_SIG=0.035f;        // unit-vector measurement noise
constexpr float MAG_SIG=0.06f;         // magnetometer observation noise
struct State { float q[4]; float p[3][3]; };
State six, nine;
float residual_bias[3],last_gyro[3],acc_ref[3]={0,0,1},mag_ref[3];
float mag_norm=0,rest_seconds=0,mag_warmup=0;
unsigned acc_decim=0,mag_count=0;
bool is_rest=false,mag_valid=false,mag_bad=false,rest_bias_on=true,mag_reject_on=true;
float max_dt_seen=0;
float mag_err=0;
inline float dot(const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline float norm(const float a[3]) { return std::sqrt(dot(a,a)); }
bool unit(const float in[3],float out[3]) {
  const float n=norm(in);
  if(!(n>1.e-6f)||!std::isfinite(n)) return false;
  for(int i=0;i<3;++i)out[i]=in[i]/n;
  return true;
}
void reset(State& s) {
  std::memset(&s,0,sizeof(s));s.q[0]=1;
  for(int i=0;i<3;++i)s.p[i][i]=0.12f*0.12f;
}
void quat_mul(float q[4],const float d[4]) {
  const float a=q[0],b=q[1],c=q[2],e=q[3];
  q[0]=a*d[0]-b*d[1]-c*d[2]-e*d[3];
  q[1]=a*d[1]+b*d[0]+c*d[3]-e*d[2];
  q[2]=a*d[2]-b*d[3]+c*d[0]+e*d[1];
  q[3]=a*d[3]+b*d[2]-c*d[1]+e*d[0];
  const float n=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
  if(n>0.5f&&std::isfinite(n))for(int i=0;i<4;++i)q[i]/=n;
}
void integrate(State& s,const float rate[3],float dt) {
  // At <= 4000 dps and 125 us: |delta| <= 0.00873rad; first-order
  // quaternion incremental rotation has O(delta^3) local error.
  const float d[4]={1,rate[0]*dt*.5f,rate[1]*dt*.5f,rate[2]*dt*.5f};
  quat_mul(s.q,d);
  float f[3][3]={
    {1,rate[2]*dt,-rate[1]*dt},
    {-rate[2]*dt,1,rate[0]*dt},
    {rate[1]*dt,-rate[0]*dt,1}
  };
  float fp[3][3],out[3][3];
  for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
    float x=0;for(int k=0;k<3;++k)x+=f[i][k]*s.p[k][j];
    fp[i][j]=x;
  }
  for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
    float x=0;for(int k=0;k<3;++k)x+=fp[i][k]*f[j][k];
    out[i][j]=x+(i==j?GYRO_NOISE*GYRO_NOISE*dt:0);
  }
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)s.p[i][j]=.5f*(out[i][j]+out[j][i]);
}
void world(const float q[4],const float v[3],float out[3]){
  float t[3]={2*(q[2]*v[2]-q[3]*v[1]),2*(q[3]*v[0]-q[1]*v[2]),2*(q[1]*v[1]-q[2]*v[0])};
  for(int i=0;i<3;++i)out[i]=v[i]+q[0]*t[i];
  out[0]+=q[2]*t[2]-q[3]*t[1];
  out[1]+=q[3]*t[0]-q[1]*t[2];
  out[2]+=q[1]*t[1]-q[2]*t[0];
}
void body(const float q[4],const float v[3],float out[3]){
  float qc[4]={q[0],-q[1],-q[2],-q[3]};
  world(qc,v,out);
}
void update(State& s,const float reference[3],const float measured[3],float variance,bool gravity) {
  float pred[3],dx[3]={};
  body(s.q,reference,pred);
  const float h[3][3]={
    {0,-pred[2],pred[1]},
    {pred[2],0,-pred[0]},
    {-pred[1],pred[0],0}
  };
  for(int row=0;row<3;++row){
    float ph[3]={},hp=variance;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)ph[i]+=s.p[i][j]*h[row][j];
    for(int j=0;j<3;++j)hp+=h[row][j]*ph[j];
    if(!(hp>1.e-12f)||!std::isfinite(hp))return;
    float k[3];
    for(int i=0;i<3;++i)k[i]=ph[i]/hp;
    if(gravity){
      // Gravity has no heading information: project applied gain into
      // the plane orthogonal to the current body-frame vertical.
      const float leak=dot(k,pred);
      for(int i=0;i<3;++i)k[i]-=leak*pred[i];
    }
    float a[3][3],next[3][3],ap[3][3];
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[i][j]=(i==j?1.f:0.f)-k[i]*h[row][j];
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){
      float v=0;for(int m=0;m<3;++m)v+=a[i][m]*s.p[m][j];
      ap[i][j]=v;
    }
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){
      float v=0;for(int m=0;m<3;++m)v+=ap[i][m]*a[j][m];
      next[i][j]=v+variance*k[i]*k[j];
    }
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)s.p[i][j]=.5f*(next[i][j]+next[j][i]);
    const float innovation=measured[row]-pred[row];
    for(int i=0;i<3;++i)dx[i]+=k[i]*innovation;
  }
  const float angle=norm(dx);
  if(!std::isfinite(angle)||angle>.4f) return;
  const float dq[4]={1,.5f*dx[0],.5f*dx[1],.5f*dx[2]};
  quat_mul(s.q,dq);
}
float yaw(const float q[4]) {
  return std::atan2(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]*q[2]+q[3]*q[3]));
}
float wrap(float a) {while(a>PI)a-=2*PI;while(a<-PI)a+=2*PI;return a;}
}
void init() {
  reset(six);reset(nine);
  std::memset(residual_bias,0,sizeof(residual_bias));
  std::memset(last_gyro,0,sizeof(last_gyro));
  mag_ref[0]=0;mag_ref[1]=1;mag_ref[2]=0;
  mag_norm=rest_seconds=mag_warmup=max_dt_seen=mag_err=0;
  acc_decim=mag_count=0;is_rest=mag_valid=mag_bad=false;
}
void prime(const float acc_ms2[3]) {
  float a[3];if(!unit(acc_ms2,a))return;
  // Align body gravity to world +Z; preserve yaw=0.
  const float qw=std::sqrt(fmaxf(0.f,(1.f+a[2])*.5f));
  if(qw>1.e-4f){
    six.q[0]=qw;six.q[1]=a[1]/(2*qw);six.q[2]=-a[0]/(2*qw);six.q[3]=0;
    std::memcpy(nine.q,six.q,sizeof(six.q));
  }
}
void gyro(const float gyr[3],float dt){
  if(!(dt>=.00005f&&dt<=.005f)||!std::isfinite(dt))return;
  for(int i=0;i<3;++i) if(!std::isfinite(gyr[i])||std::fabs(gyr[i])>100.f)return;
  if(dt>max_dt_seen)max_dt_seen=dt;
  float corrected[3];for(int i=0;i<3;++i){last_gyro[i]=gyr[i];corrected[i]=gyr[i]-residual_bias[i];}
  integrate(six,corrected,dt);
  if(mag_valid)integrate(nine,corrected,dt);
}
void accel(const float acc[3]){
  float a[3];const float length=norm(acc);
  if(!unit(acc,a)||!(length>G*.75f&&length<G*1.25f))return;
  is_rest=(std::fabs(length-G)<.15f&&norm(last_gyro)<.6f*RAD);
  rest_seconds=is_rest?rest_seconds+1.f/ACC_RATE:0;
  if(is_rest&&rest_bias_on){
    const float alpha=1.f/(ACC_RATE*12.f);
    for(int i=0;i<3;++i) {
      residual_bias[i]+=(last_gyro[i]-residual_bias[i])*alpha;
      if(std::fabs(residual_bias[i])>2.f*RAD)residual_bias[i]=0;
    }
  }
  if(++acc_decim<10)return;
  acc_decim=0;
  if(std::fabs(length-G)>.25f)return;
  const float variance=ACC_SIG*ACC_SIG*(1.f+std::fabs(length-G)*10.f);
  update(six,acc_ref,a,variance,true);
  if(mag_valid)update(nine,acc_ref,a,variance,true);
}
int mag(const float mag_ut[3]){
  float measured[3];if(!unit(mag_ut,measured))return -1;
  const float n=norm(mag_ut);
  if(!mag_valid) {
    if(mag_count==0){mag_norm=n;mag_warmup=0;}
    if(std::fabs(n-mag_norm)>mag_norm*.12f){mag_count=0;mag_norm=n;return 0;}
    mag_norm+=.05f*(n-mag_norm);mag_count++;
    if(mag_count<40)return 0; // Stable candidate for about 4 s at 10 Hz.
    std::memcpy(nine.q,six.q,sizeof(six.q));
    std::memcpy(nine.p,six.p,sizeof(six.p));
    world(nine.q,measured,mag_ref);
    mag_valid=true;mag_bad=false;
    return 0;
  }
  const bool disturbed=std::fabs(n-mag_norm)>mag_norm*.2f;
  mag_bad=mag_reject_on&&disturbed;
  if(mag_bad)return 0;
  mag_norm+=.01f*(n-mag_norm);
  float predicted[3];body(nine.q,mag_ref,predicted);
  const float err=std::sqrt(fmaxf(0.f,2.f-2.f*dot(predicted,measured)));
  mag_err=err/RAD;
  update(nine,mag_ref,measured,MAG_SIG*MAG_SIG,false);
  return 0;
}
void quat6(float q[4]){std::memcpy(q,six.q,sizeof(six.q));}
void quat9(float q[4]){std::memcpy(q,mag_valid?nine.q:six.q,sizeof(six.q));}
void bias(float b[3]){std::memcpy(b,residual_bias,sizeof(residual_bias));}
float bias_sigma_dps(){return .035f;}
int rest(){return rest_seconds>=1.f;}
float rest_time(){return rest()?rest_seconds-1.f:0.f;}
int mag_ready(){return mag_valid;}
int mag_disturbed(){return mag_bad;}
float mag_delta_deg(){return mag_valid?wrap(yaw(nine.q)-yaw(six.q))/RAD:0;}
void reset_mag_reference(){mag_valid=false;mag_count=0;mag_bad=false;}
void set_rest_bias_enabled(bool enabled){rest_bias_on=enabled;}
void set_mag_rejection(bool enabled){mag_reject_on=enabled;}
void diagnostic(float out[8]){
  out[0]=yaw(six.q)/RAD;out[1]=mag_norm;out[2]=0;out[3]=mag_warmup;
  out[4]=(float)mag_count*.1f;out[5]=0;out[6]=mag_err;out[7]=max_dt_seen*1.e6f;
}
}
