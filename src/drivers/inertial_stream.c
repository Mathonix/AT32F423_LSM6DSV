#include "inertial_stream.h"
#include <string.h>
uint8_t inertial_stream_configure(inertial_stream_t *s, unsigned port, const uint8_t *p,
                                 unsigned len, uint32_t now_ms, uint16_t fusion_hz)
{
  uint16_t hz; uint32_t token;
  if(!s || port>=3 || !p || len!=7 || p[0]>1) return AHRS_ACK_INVALID_PARAM;
  hz=(uint16_t)p[1]|((uint16_t)p[2]<<8); memcpy(&token,p+3,4);
  if(!token || (p[0] && (hz<50 || hz>500 || fusion_hz<hz || fusion_hz%hz))) return AHRS_ACK_INVALID_PARAM;
  if(!p[0]) { if(s->ports[port].token==token) memset(&s->ports[port],0,sizeof(s->ports[port])); return AHRS_ACK_SUCCESS; }
  if(s->ports[port].token!=token) {
    memset(&s->ports[port],0,sizeof(s->ports[port])); s->ports[port].token=token;
  } else if(s->ports[port].divider && s->ports[port].divider!=fusion_hz/hz) return AHRS_ACK_INVALID_PARAM;
  s->ports[port].divider=(uint16_t)(fusion_hz/hz);
  s->ports[port].deadline=now_ms+10000U;
  return AHRS_ACK_SUCCESS;
}
uint32_t inertial_stream_clock(inertial_stream_t *s, uint32_t cycles, uint32_t clock_hz)
{
  if(!s->clock_ready) { s->clock_ready=1; s->cycles=cycles; return s->us; }
  if(clock_hz) {
    uint64_t scaled=(uint64_t)(uint32_t)(cycles-s->cycles)*1000000U+s->remainder;
    s->us+=(uint32_t)(scaled/clock_hz); s->remainder=(uint32_t)(scaled%clock_hz);
  }
  s->cycles=cycles; return s->us;
}
void inertial_stream_stop(inertial_stream_t *s) { memset(s->ports,0,sizeof(s->ports)); }
int inertial_stream_active(const inertial_stream_t *s)
{ return s->ports[0].token || s->ports[1].token || s->ports[2].token; }
static void rotation(const float q[4], float r[3][3])
{
  float w=q[0],x=q[1],y=q[2],z=q[3];
  r[0][0]=1-2*(y*y+z*z);r[0][1]=2*(x*y-w*z);r[0][2]=2*(x*z+w*y);
  r[1][0]=2*(x*y+w*z);r[1][1]=1-2*(x*x+z*z);r[1][2]=2*(y*z-w*x);
  r[2][0]=2*(x*z-w*y);r[2][1]=2*(y*z+w*x);r[2][2]=1-2*(x*x+y*y);
}
void inertial_stream_accumulate(inertial_stream_t *s, const float q[4], const float acc[3], const float gyro[3])
{
  float r[3][3],world[3]={0};rotation(q,r);
  for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)world[i]+=r[i][j]*acc[j];
  for(unsigned port=0;port<3;port++) if(s->ports[port].token) {
    ++s->ports[port].count;
    for(unsigned i=0;i<3;i++) {s->ports[port].world_sum[i]+=world[i];s->ports[port].gyro_sum[i]+=gyro[i];}
  }
}
int inertial_stream_due(inertial_stream_t *s, unsigned port, uint32_t now_ms)
{
  if(port>=3 || !s->ports[port].token) return 0;
  if((int32_t)(now_ms-s->ports[port].deadline)>=0) { memset(&s->ports[port],0,sizeof(s->ports[port])); return 0; }
  return s->ports[port].count>=s->ports[port].divider;
}
uint16_t inertial_stream_pack(inertial_stream_t *s, unsigned port, uint8_t *frame,
                             uint16_t capacity, const float q[4], uint8_t flags, uint16_t fusion_hz)
{
  ahrs_payload_inertial_t p={0};
  if(port>=3 || !s->ports[port].token || !fusion_hz || !s->ports[port].count) return 0;
  p.version=1; p.flags=flags; p.token=s->ports[port].token;
  p.sample_seq=s->ports[port].seq++; p.sample_us=s->us;
  p.period_us=1000000U*s->ports[port].divider/fusion_hz;
  /* Equivalent acceleration in the end-sample body axes. Host R(q)*acc
   * recovers the average of same-sample R(q_i)*acc_i, without gravity lag.
   * A complete divider-point boxcar has zeros at decimation aliases. */
  float r[3][3];rotation(q,r);memcpy(p.quaternion,q,sizeof(p.quaternion));
  for(unsigned i=0;i<3;i++) {
    for(unsigned j=0;j<3;j++)p.acc_ms2[i]+=r[j][i]*s->ports[port].world_sum[j]/s->ports[port].count;
    p.gyro_dps[i]=s->ports[port].gyro_sum[i]/s->ports[port].count;
  }
  s->ports[port].count=0;
  memset(s->ports[port].world_sum,0,sizeof(s->ports[port].world_sum));
  memset(s->ports[port].gyro_sum,0,sizeof(s->ports[port].gyro_sum));
  return protocol_pack_frame(frame,capacity,AHRS_MSG_INERTIAL,(uint8_t)p.sample_seq,&p,sizeof(p));
}
