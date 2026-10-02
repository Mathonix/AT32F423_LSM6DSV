#include "attitude_output.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static float angular_error(const float a[4], const float b[4])
{
  float d=0;for(unsigned i=0;i<4;i++) d+=a[i]*b[i];
  return 2*acosf(fminf(1,fabsf(d)))*57.2957795f;
}
int main(void)
{
  /* Earth yaw with a 60-degree roll: using body gz would predict half
   * the turn. Quaternion body increments must track the complete turn. */
  for(unsigned mode=0;mode<3;mode++) {
    attitude_output_t s={0};float q[4];
    for(unsigned i=0;i<=5000;i++) {
      attitude_from_euler(60,30,(float)i*.018f,q);
      assert(attitude_output_update(&s,q,q,0,18,mode,.001f));
      assert(angular_error(s.q,q)<.1f);
    }
    float r,p,y;attitude_to_euler(s.q,&r,&p,&y);
    assert(fabsf(r-60)<.01f && fabsf(p-30)<.01f && fabsf(y-90)<.02f);
    float before[4];memcpy(before,s.q,sizeof(before));
    assert(!attitude_output_update(&s,q,q,0,NAN,mode,.001f));
    assert(!memcmp(before,s.q,sizeof(before)));
  }
  /* At rest each preset reduces white-like angle jitter; stronger mode
   * attenuates more. The mean is still corrected, never frozen. */
  double variance[3]={0};
  for(unsigned mode=0;mode<3;mode++) {
    attitude_output_t s={0};float six[4]={1,0,0,0},target[4];
    unsigned seed=17;
    for(unsigned i=0;i<20000;i++) {
      seed=seed*1664525U+1013904223U;
      float jitter=((float)(seed>>8)/16777216.0f-.5f)*.4f;
      attitude_from_euler(jitter,-jitter,jitter,target);
      assert(attitude_output_update(&s,six,target,1,.01f,mode,.001f));
      if(i>10000) {float r,p,y;attitude_to_euler(s.q,&r,&p,&y);variance[mode]+=y*y;}
    }
    attitude_from_euler(0,0,2,target);
    for(unsigned i=0;i<10000;i++) attitude_output_update(&s,six,target,1,.01f,mode,.001f);
    float r,p,y;attitude_to_euler(s.q,&r,&p,&y);assert(fabsf(y-2)<.02f);
  }
  assert(variance[0]>variance[1] && variance[1]>variance[2]);
  /* Angle wrap and q/-q represent the same continuous orientation. */
  attitude_output_t s={0};float q[4];
  attitude_from_euler(0,89.9f,179.9f,q);
  attitude_output_update(&s,q,q,0,1,1,.001f);
  for(unsigned i=0;i<4;i++) q[i]=-q[i];
  attitude_output_update(&s,q,q,0,1,2,.001f);assert(angular_error(s.q,q)<.1f);
  float invalid[4]={0};float before[4];memcpy(before,s.q,sizeof(before));
  assert(!attitude_output_update(&s,invalid,q,0,1,1,.001f));assert(!memcmp(before,s.q,sizeof(before)));
  /* Mode change preserves state. Rest blending waits out that gear's hold,
   * so one sample cannot jump to a new heading. Very slow detectable motion
   * still propagates. */
  memset(&s,0,sizeof(s));attitude_from_euler(0,0,0,q);
  attitude_output_update(&s,q,q,1,0,1,.001f);
  float target[4];attitude_from_euler(0,0,90,target);
  attitude_output_update(&s,q,target,1,0,2,.001f);
  float r,p,y;attitude_to_euler(s.q,&r,&p,&y);assert(fabsf(y)<.01f);
  for(unsigned i=0;i<500;i++) attitude_output_update(&s,q,target,1,0,2,.001f);
  attitude_to_euler(s.q,&r,&p,&y);assert(y>0 && y<10);
  /* Response keeps the pre-stop output for 1.5 s; stable releases at 0.4 s. */
  memset(&s,0,sizeof(s));attitude_from_euler(0,0,0,q);
  attitude_output_update(&s,q,q,1,0,0,.001f);
  attitude_from_euler(0,0,20,target);
  for(unsigned i=0;i<1400;i++) attitude_output_update(&s,q,target,1,0,0,.001f);
  attitude_to_euler(s.q,&r,&p,&y);assert(fabsf(y)<.05f);
  memset(&s,0,sizeof(s));attitude_output_update(&s,q,q,1,0,2,.001f);
  for(unsigned i=0;i<500;i++) attitude_output_update(&s,q,target,1,0,2,.001f);
  attitude_to_euler(s.q,&r,&p,&y);assert(y>1.f);
  memset(&s,0,sizeof(s));
  for(unsigned i=0;i<=10000;i++) {
    attitude_from_euler(0,0,i*.0002f,q);
    attitude_output_update(&s,q,q,1,.2f,2,.001f);
  }
  attitude_to_euler(s.q,&r,&p,&y);assert(fabsf(y-2)<.01f);
  printf("attitude output: tilted rotation, static attenuation (%.6f/%.6f/%.6f), no freeze, wrap, mode continuity, invalid samples: OK\n",
         variance[0]/9999,variance[1]/9999,variance[2]/9999);
}
