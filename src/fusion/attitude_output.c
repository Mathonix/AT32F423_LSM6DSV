#include "attitude_output.h"
#include <math.h>
#include <string.h>
#define RAD 0.017453292519943295f
static int normalize(float q[4])
{
  float n = 0;
  for(unsigned i=0;i<4;i++) { if(!isfinite(q[i])) return 0; n += q[i]*q[i]; }
  if(!(n > 1e-12f) || !isfinite(n)) return 0;
  n = 1.0f/sqrtf(n); for(unsigned i=0;i<4;i++) q[i] *= n;
  return 1;
}
static void multiply(const float a[4], const float b[4], float out[4])
{
  float r[4] = {a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],
    a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],
    a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],
    a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]};
  memcpy(out,r,sizeof(r));
}
void attitude_from_euler(float roll, float pitch, float yaw, float q[4])
{
  float cr=cosf(roll*RAD*.5f), sr=sinf(roll*RAD*.5f);
  float cp=cosf(pitch*RAD*.5f), sp=sinf(pitch*RAD*.5f);
  float cy=cosf(yaw*RAD*.5f), sy=sinf(yaw*RAD*.5f);
  q[0]=cr*cp*cy+sr*sp*sy; q[1]=sr*cp*cy-cr*sp*sy;
  q[2]=cr*sp*cy+sr*cp*sy; q[3]=cr*cp*sy-sr*sp*cy;
}
void attitude_to_euler(const float q[4], float *roll, float *pitch, float *yaw)
{
  float sp=2*(q[0]*q[2]-q[3]*q[1]);
  sp=fmaxf(-1,fminf(1,sp));
  *roll=atan2f(2*(q[0]*q[1]+q[2]*q[3]),1-2*(q[1]*q[1]+q[2]*q[2]))/RAD;
  *pitch=asinf(sp)/RAD;
  *yaw=atan2f(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]*q[2]+q[3]*q[3]))/RAD;
}
int attitude_output_update(attitude_output_t *s, const float q6[4],
                           const float q_target[4], uint8_t rest,
                           float rate, uint8_t mode, float dt)
{
  float six[4], target[4];
  if(!s || mode>=FUSION_PROFILE_COUNT || !isfinite(dt) || dt<=0 || dt>.1f ||
     !isfinite(rate) || rate<0) return 0;
  memcpy(six,q6,sizeof(six)); memcpy(target,q_target,sizeof(target));
  if(!normalize(six) || !normalize(target)) return 0;
  if(!s->initialized) {
    memcpy(s->q,target,sizeof(target)); memcpy(s->last_q6,six,sizeof(six));
    s->initialized=1; return 1;
  }
  /* VQF rest detection can accept uniform slow rotation. The independent
   * residual-rate guard keeps detectable slow movement responsive. */
  uint8_t moving = !rest || rate > .15f;
  if(moving) {
    float inverse[4]={s->last_q6[0],-s->last_q6[1],-s->last_q6[2],-s->last_q6[3]}, delta[4];
    multiply(inverse,six,delta); multiply(s->q,delta,s->q);
    normalize(s->q);
  }
  fusion_profile_t p=fusion_profile_get(mode);
  float alpha=1-expf(-dt/(moving?p.motion_tau_s:p.rest_tau_s)), dot=0;
  for(unsigned i=0;i<4;i++) dot += s->q[i]*target[i];
  /* Shortest-arc normalized interpolation avoids Euler wrap/singularity
   * and quaternion-sign discontinuities. No output freeze at rest. */
  for(unsigned i=0;i<4;i++) s->q[i] += alpha*((dot<0?-target[i]:target[i])-s->q[i]);
  normalize(s->q); memcpy(s->last_q6,six,sizeof(six));
  return 1;
}
