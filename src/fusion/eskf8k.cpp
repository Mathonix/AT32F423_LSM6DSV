// Standalone 8 kHz, 3-state attitude error-state KF (not a copy of CH32 code).
// Quaternion nominal state + 3x3 attitude covariance, residual stationary bias.
#include "eskf8k.hpp"
#include <cmath>
#include <cstring>
#include "static6_diag.h"
extern "C" { volatile unsigned eskf8k_counts[8]; }

namespace ahrs_eskf8k {
namespace {
constexpr float PI=3.14159265358979323846f, RAD=PI/180.f, G=9.80665f;
constexpr float RATE=8000.f, ACC_RATE=2000.f, ACC_OBS_DIV=10.f;
constexpr float GYRO_NOISE=0.015f*RAD; // rad/sqrt(s), initial tuning value
constexpr float ACC_SIG=0.035f;        // unit-vector measurement noise
constexpr float MAG_SIG=0.06f;         // magnetometer observation noise
// The triangular factor is authoritative. Repeated float F*P*F' at 8 kHz
// loses the small observable eigenvalues when yaw variance is much larger.
// QR propagation computes the same covariance, without subtractive loss of
// positive definiteness. p is the symmetric diagnostic/gain projection L*L'.
struct State { float q[4]; float p[3][3]; float l[3][3];
#if APP_ESKF_Q_COMP
  float qlow[4]; /* retained rounding residue, not a new estimator state */
#endif
};
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
  for(int i=0;i<3;++i){s.p[i][i]=0.12f*0.12f;s.l[i][i]=.12f;}
}
void factor_rows(State& s,float rows[][3],int count) {
  // QR of [F*L, sqrt(Q)]' or [(I-KH)*L, sqrt(R)*K]'.
  for(int j=0;j<3;++j)for(int i=count-1;i>j;--i){
    const float b=rows[i][j];if(b==0.f)continue;
    const float a=rows[j][j],r=std::sqrt(a*a+b*b);
    const float c=a/r,d=b/r;
    rows[j][j]=r;rows[i][j]=0;
    for(int k=j+1;k<3;++k){
      const float u=rows[j][k],v=rows[i][k];
      rows[j][k]=c*u+d*v;rows[i][k]=c*v-d*u;
    }
  }
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)s.l[i][j]=j<=i?rows[j][i]:0.f;
  for(int i=0;i<3;++i)for(int j=0;j<=i;++j){
    float p=0;for(int k=0;k<=j;++k)p+=s.l[i][k]*s.l[j][k];
    s.p[i][j]=s.p[j][i]=p;
  }
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
void quat_mul(State& s,const float d[4]) {
#if APP_ESKF_Q_COMP
  /* Same right quaternion product and normalization as above. Compute the
   * increment separately so <1ULP rotations are not rounded away at8kHz.
   * TwoSum retains addition error; fused residual retains division error.
   * Both gyro and measurement injection rotate the retained low part. */
  const float a=s.q[0],b=s.q[1],c=s.q[2],e=s.q[3],dd=d[0]-1.f;
  const float la=s.qlow[0],lb=s.qlow[1],lc=s.qlow[2],le=s.qlow[3];
#if APP_ESKF_Q_COMP_FAST==2
  if(d[0]==1.f) {
    // Produce/commit one component at a time. Copies above preserve the old
    // high/low quaternion until all four right-product components are done.
    auto add=[&s](int i,float old,float u) {
      const float t=old+u,v=t-old;
      s.qlow[i]=(old-(t-v))+(u-v);s.q[i]=t;
    };
    add(0,a,(-b*d[1]-c*d[2]-e*d[3])+(la-lb*d[1]-lc*d[2]-le*d[3]));
    add(1,b,(a*d[1]+c*d[3]-e*d[2])+(la*d[1]+lb+lc*d[3]-le*d[2]));
    add(2,c,(a*d[2]-b*d[3]+e*d[1])+(la*d[2]-lb*d[3]+lc+le*d[1]));
    add(3,e,(a*d[3]+b*d[2]-c*d[1])+(la*d[3]+lb*d[2]-lc*d[1]+le));
  } else {
#endif
  float inc[4]={a*dd-b*d[1]-c*d[2]-e*d[3],
    a*d[1]+b*dd+c*d[3]-e*d[2],a*d[2]-b*d[3]+c*dd+e*d[1],
    a*d[3]+b*d[2]-c*d[1]+e*dd};
  float low[4]={la*d[0]-lb*d[1]-lc*d[2]-le*d[3],
    la*d[1]+lb*d[0]+lc*d[3]-le*d[2],la*d[2]-lb*d[3]+lc*d[0]+le*d[1],
    la*d[3]+lb*d[2]-lc*d[1]+le*d[0]};
#if APP_ESKF_Q_COMP_FAST==1
  // All production gyro and correction increments have d[0]==1. Retain
  // the general path for other callers; omit only exact identity terms.
  if(d[0]==1.f) {
    inc[0]=-b*d[1]-c*d[2]-e*d[3];
    inc[1]=a*d[1]+c*d[3]-e*d[2];
    inc[2]=a*d[2]-b*d[3]+e*d[1];
    inc[3]=a*d[3]+b*d[2]-c*d[1];
    low[0]=la-lb*d[1]-lc*d[2]-le*d[3];
    low[1]=la*d[1]+lb+lc*d[3]-le*d[2];
    low[2]=la*d[2]-lb*d[3]+lc+le*d[1];
    low[3]=la*d[3]+lb*d[2]-lc*d[1]+le;
  }
#endif
  for(int i=0;i<4;i++) {
    const float old=s.q[i],u=inc[i]+low[i],t=old+u,v=t-old;
#if APP_ESKF_Q_COMP_FAST==1
    // FastTwoSum is error free only under its magnitude precondition.
    // Near zero/cancellation retain the full TwoSum expression.
    s.qlow[i]=std::fabs(old)>=std::fabs(u)?(old-t)+u:(old-(t-v))+(u-v);
#else
    s.qlow[i]=(old-(t-v))+(u-v);s.q[i]=t;
#endif
    s.q[i]=t;
  }
#if APP_ESKF_Q_COMP_FAST==2
  }
#endif
  const float n=std::sqrt(s.q[0]*s.q[0]+s.q[1]*s.q[1]+s.q[2]*s.q[2]+s.q[3]*s.q[3]);
  /* Division by exactly1 and its exactly-zero fused residual are identities.
   * The norm is still computed every call; all other norms normalize fully. */
  if(n==1.f)return;
#if APP_ESKF_Q_COMP_FAST==2
  if(n>0.5f&&std::isfinite(n)) {
    // One shared reciprocal; recover the error against the ORIGINAL divisor,
    // including reciprocal rounding (product-only residual is insufficient).
    // The final low product error is second order in float rounding.
    const float inv=1.f/n;
    for(int i=0;i<4;i++) {
      const float old=s.q[i],t=old*inv;
      s.qlow[i]=(__builtin_fmaf(-t,n,old)+s.qlow[i])*inv;s.q[i]=t;
    }
  }
#else
  if(n>0.5f&&std::isfinite(n))for(int i=0;i<4;i++) {
    const float old=s.q[i],t=old/n;
    s.qlow[i]=(__builtin_fmaf(-t,n,old)+s.qlow[i])/n;s.q[i]=t;
  }
#endif
#else
  quat_mul(s.q,d);
#endif
}
void integrate(State& s,const float rate[3],float dt) {
  // At <= 4000 dps and 125 us: |delta| <= 0.00873rad; first-order
  // quaternion incremental rotation has O(delta^3) local error.
  const float d[4]={1,rate[0]*dt*.5f,rate[1]*dt*.5f,rate[2]*dt*.5f};
  quat_mul(s,d);
  float f[3][3]={
    {1,rate[2]*dt,-rate[1]*dt},
    {-rate[2]*dt,1,rate[0]*dt},
    {rate[1]*dt,-rate[0]*dt,1}
  };
  float rows[6][3]={};
  for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
    for(int k=0;k<3;++k)rows[j][i]+=f[i][k]*s.l[k][j];
  }
  const float noise=GYRO_NOISE*std::sqrt(dt);
  for(int i=0;i<3;++i)rows[i+3][i]=noise;
  factor_rows(s,rows,6);
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
bool update(State& s,const float reference[3],const float measured[3],float variance,bool gravity) {
  float pred[3],dx[3]={};
  // Commit the attitude and covariance together only after all scalar rows
  // and the correction gate succeed.
  State candidate=s;
  body(s.q,reference,pred);
#if APP_STATIC6_DIAG
  if(static6_enabled && &s==&six && gravity)
    for(int i=0;i<3;i++)static6_work.innovation[i]=measured[i]-pred[i];
#endif
  const float h[3][3]={
    {0,-pred[2],pred[1]},
    {pred[2],0,-pred[0]},
    {-pred[1],pred[0],0}
  };
  for(int row=0;row<3;++row){
    float ph[3]={},hp=variance;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)ph[i]+=candidate.p[i][j]*h[row][j];
    for(int j=0;j<3;++j)hp+=h[row][j]*ph[j];
    if(!(hp>1.e-12f)||!std::isfinite(hp)) {
#if APP_STATIC6_DIAG
      if(static6_enabled && &s==&six && gravity)static6_work.counts[S6_NUMERIC_REJECT6]++;
#endif
      return false;
    }
    float k[3];
    for(int i=0;i<3;++i)k[i]=ph[i]/hp;
    if(gravity){
      // Gravity has no heading information: project applied gain into
      // the plane orthogonal to the current body-frame vertical.
      const float leak=dot(k,pred);
      for(int i=0;i<3;++i)k[i]-=leak*pred[i];
    }
    float a[3][3],rows[4][3]={};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[i][j]=(i==j?1.f:0.f)-k[i]*h[row][j];
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){
      for(int m=0;m<3;++m)rows[j][i]+=a[i][m]*candidate.l[m][j];
    }
    const float sigma=std::sqrt(variance);
    for(int i=0;i<3;++i)rows[3][i]=sigma*k[i];
    factor_rows(candidate,rows,4);
    const float innovation=measured[row]-pred[row]-dot(h[row],dx);
    for(int i=0;i<3;++i)dx[i]+=k[i]*innovation;
  }
  const float angle=norm(dx);
  if(!std::isfinite(angle)||angle>.4f) {
#if APP_STATIC6_DIAG
    if(static6_enabled && &s==&six && gravity)static6_work.counts[S6_ANGLE_REJECT6]++;
#endif
    return false;
  }
  const float dq[4]={1,.5f*dx[0],.5f*dx[1],.5f*dx[2]};
  quat_mul(candidate,dq);
  // Exact derivative of the right-error reset in the Gibbs coordinates
  // used by dq = normalize([1, dx/2]). No covariance row is skipped.
  const float scale=1.f/(1.f+.25f*dot(dx,dx));
  const float reset_j[3][3]={
    {scale,scale*.5f*dx[2],-scale*.5f*dx[1]},
    {-scale*.5f*dx[2],scale,scale*.5f*dx[0]},
    {scale*.5f*dx[1],-scale*.5f*dx[0],scale}
  };
  float rows[3][3]={};
  for(int i=0;i<3;++i)for(int j=0;j<3;++j){
    for(int k=0;k<3;++k)rows[j][i]+=reset_j[i][k]*candidate.l[k][j];
  }
  factor_rows(candidate,rows,3);
  s=candidate;
  return true;
}
float yaw(const float q[4]) {
  return std::atan2(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]*q[2]+q[3]*q[3]));
}
float wrap(float a) {while(a>PI)a-=2*PI;while(a<-PI)a+=2*PI;return a;}
}
void init() {
  for(auto& count:eskf8k_counts)count=0;
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
  }else{
    // The antipodal case has no unique yaw: choose 180 degrees about X.
    // Leaving identity here cannot recover from exactly inverted gravity.
    six.q[0]=0;six.q[1]=1;six.q[2]=six.q[3]=0;
  }
  std::memcpy(nine.q,six.q,sizeof(six.q));
#if APP_ESKF_Q_COMP
  std::memset(six.qlow,0,sizeof(six.qlow));
  std::memset(nine.qlow,0,sizeof(nine.qlow));
#endif
}
void gyro(const float gyr[3],float dt){
  if(!(dt>=.00005f&&dt<=.005f)||!std::isfinite(dt)){eskf8k_counts[1]++;return;}
  for(int i=0;i<3;++i) if(!std::isfinite(gyr[i])||std::fabs(gyr[i])>100.f){eskf8k_counts[2]++;return;}
  eskf8k_counts[0]++;
  if(dt>max_dt_seen)max_dt_seen=dt;
  float corrected[3];for(int i=0;i<3;++i){last_gyro[i]=gyr[i];corrected[i]=gyr[i]-residual_bias[i];}
#if APP_STATIC6_DIAG
  if(static6_enabled) {
    static6_rate(corrected);static6_work.counts[S6_PRED6]++;
    if(mag_valid)static6_work.counts[S6_PRED9]++;
  }
#endif
  integrate(six,corrected,dt);
  if(mag_valid)integrate(nine,corrected,dt);
}
void accel(const float acc[3]){
#if APP_STATIC6_DIAG
  if(static6_enabled)static6_work.counts[S6_ACC_CALL]++;
#endif
  float a[3];const float length=norm(acc);
  if(!unit(acc,a)||!(length>G*.75f&&length<G*1.25f)) {
#if APP_STATIC6_DIAG
    if(static6_enabled)static6_work.counts[S6_BROAD_REJECT]++;
#endif
    return;
  }
  is_rest=(std::fabs(length-G)<APP_ESKF_REST_ACC_MS2&&
           norm(last_gyro)<APP_ESKF_REST_GYR_DPS*RAD);
#if APP_STATIC6_DIAG
  if(static6_enabled) {
    static6_work.acc_length=length;static6_work.gyro_norm=norm(last_gyro);
    if(is_rest)static6_work.counts[S6_REST]++;
  }
#endif
  rest_seconds=is_rest?rest_seconds+1.f/ACC_RATE:0;
  if(is_rest&&rest_bias_on){
    const float alpha=1.f/(ACC_RATE*12.f);
    for(int i=0;i<3;++i) {
      residual_bias[i]+=(last_gyro[i]-residual_bias[i])*alpha;
      if(std::fabs(residual_bias[i])>2.f*RAD)residual_bias[i]=0;
    }
  }
  if(++acc_decim<10) {
#if APP_STATIC6_DIAG
    if(static6_enabled)static6_work.counts[S6_DECIM_WAIT]++;
#endif
    return;
  }
  acc_decim=0;
#if APP_STATIC6_DIAG
  if(static6_enabled)static6_work.counts[S6_PLAN]++;
#endif
  if(std::fabs(length-G)>.25f) {
#if APP_STATIC6_DIAG
    if(static6_enabled)static6_work.counts[S6_NORM_REJECT]++;
#endif
    return;
  }
  const float variance=ACC_SIG*ACC_SIG*(1.f+std::fabs(length-G)*10.f);
  eskf8k_counts[3]++;
#if APP_STATIC6_DIAG
  if(static6_enabled)static6_work.counts[S6_ATTEMPT6]++;
#endif
  if(!update(six,acc_ref,a,variance,true))eskf8k_counts[4]++;
#if APP_STATIC6_DIAG
  else if(static6_enabled)static6_work.counts[S6_OK6]++;
#endif
  if(mag_valid&&!update(nine,acc_ref,a,variance,true)) {
    eskf8k_counts[4]++;
#if APP_STATIC6_DIAG
    if(static6_enabled)static6_work.counts[S6_REJECT9]++;
#endif
  }
#if APP_STATIC6_DIAG
  else if(static6_enabled&&mag_valid)static6_work.counts[S6_OK9]++;
  if(static6_enabled) {
    for(int i=0;i<3;i++)static6_work.bias[i]=residual_bias[i];
  }
#endif
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
    std::memcpy(nine.l,six.l,sizeof(six.l));
#if APP_ESKF_Q_COMP
    std::memcpy(nine.qlow,six.qlow,sizeof(six.qlow));
#endif
    world(nine.q,measured,mag_ref);
    mag_valid=true;mag_bad=false;
    return 0;
  }
  const bool disturbed=std::fabs(n-mag_norm)>mag_norm*.2f;
  mag_bad=mag_reject_on&&disturbed;
  if(mag_bad){eskf8k_counts[6]++;return 0;}
  mag_norm+=.01f*(n-mag_norm);
  float predicted[3];body(nine.q,mag_ref,predicted);
  const float err=std::sqrt(fmaxf(0.f,2.f-2.f*dot(predicted,measured)));
  mag_err=err/RAD;
  eskf8k_counts[5]++;
  if(!update(nine,mag_ref,measured,MAG_SIG*MAG_SIG,false))eskf8k_counts[7]++;
  return 0;
}
void quat6(float q[4]){std::memcpy(q,six.q,sizeof(six.q));}
void quat9(float q[4]){std::memcpy(q,mag_valid?nine.q:six.q,sizeof(six.q));}
void bias(float b[3]){std::memcpy(b,residual_bias,sizeof(residual_bias));}
float bias_sigma_dps(){return .035f;}
int rest(){return rest_seconds>=1.f;}
#if APP_ESKF_REST_RELAX
void rest_thresholds(float out[2]) {
  out[0]=APP_ESKF_REST_GYR_DPS;out[1]=APP_ESKF_REST_ACC_MS2;
}
#endif
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
unsigned health(unsigned index){
  if(index>1)return 0;
  const State& s=index?nine:six;
  float q2=0;unsigned flags=0;
  for(int i=0;i<4;++i)q2+=s.q[i]*s.q[i];
#if APP_ESKF_Q_COMP
  for(int i=0;i<4;++i)if(!std::isfinite(s.qlow[i]))flags|=1U;
#endif
  if(!std::isfinite(q2)||std::fabs(q2-1.f)>1.e-4f)flags|=1U;
  for(int i=0;i<3;++i){
    if(!(s.l[i][i]>0.f))flags|=2U;
    for(int j=0;j<3;++j)if(!std::isfinite(s.l[i][j])||!std::isfinite(s.p[i][j]))flags|=2U;
  }
  return flags;
}
void state_snapshot(float out[26]){
  std::memcpy(out,six.q,sizeof(six.q));std::memcpy(out+4,six.p,sizeof(six.p));
  std::memcpy(out+13,nine.q,sizeof(nine.q));std::memcpy(out+17,nine.p,sizeof(nine.p));
}
}
