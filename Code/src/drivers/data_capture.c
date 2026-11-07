#include "data_capture.h"
#include <string.h>
int data_capture_rate_valid(uint16_t hz)
{return hz==1 || hz==5 || hz==10 || hz==20 || hz==50 || hz==100 || hz==200 || hz==500;}
uint8_t data_capture_configure(data_capture_t *s,unsigned port,const uint8_t *p,unsigned len,uint32_t now,uint16_t fusion_hz)
{
  uint16_t hz;uint32_t token;
  if(!s || port>=3 || !p || len!=7 || p[0]>1)return AHRS_ACK_INVALID_PARAM;
  memcpy(&hz,p+1,2);memcpy(&token,p+3,4);
  if(!token || (p[0] && (!data_capture_rate_valid(hz) || fusion_hz<hz || fusion_hz%hz)))return AHRS_ACK_INVALID_PARAM;
  if(s->ports[port].token && (int32_t)(now-s->ports[port].deadline)>=0)memset(&s->ports[port],0,sizeof(s->ports[port]));
  if(!p[0]) {if(s->ports[port].token==token)memset(&s->ports[port],0,sizeof(s->ports[port]));return AHRS_ACK_SUCCESS;}
  if(s->ports[port].token && (s->ports[port].token!=token || s->ports[port].hz!=hz))return AHRS_ACK_EXEC_FAILED;
  s->ports[port].token=token;s->ports[port].hz=hz;s->ports[port].divider=fusion_hz/hz;s->ports[port].deadline=now+10000U;
  return AHRS_ACK_SUCCESS;
}
void data_capture_stop(data_capture_t *s){memset(s,0,sizeof(*s));}
int data_capture_active(const data_capture_t *s){return s->ports[0].token || s->ports[1].token || s->ports[2].token;}
int data_capture_due(data_capture_t *s,unsigned port,uint32_t now,uint32_t fusion_n)
{
  if(port>=3 || !s->ports[port].token)return 0;
  if((int32_t)(now-s->ports[port].deadline)>=0){memset(&s->ports[port],0,sizeof(s->ports[port]));return 0;}
  /* Align to the fusion counter, so every record has freshly updated Euler output. */
  return fusion_n%s->ports[port].divider==0;
}
int data_capture_telemetry_due(const data_capture_t *s,unsigned port,uint32_t fusion_n,uint16_t fusion_hz)
{
  if(!s || port>=3)return 0;
  if(!s->ports[port].token)return 1;
  const uint32_t divider=fusion_hz>100U?fusion_hz/100U:1U;
  return fusion_n%divider==0;
}
uint16_t data_capture_pack(data_capture_t *s,unsigned port,uint8_t *frame,uint16_t capacity,uint32_t sample_us,uint8_t flags,const float values[9])
{
  if(port>=3 || !s->ports[port].token)return 0;
  ahrs_payload_data_capture_t p={0};p.version=1;p.flags=flags;
  p.token=s->ports[port].token;p.sample_seq=s->ports[port].seq++;p.sample_us=sample_us;p.period_us=1000000U/s->ports[port].hz;
  memcpy(p.values,values,sizeof(p.values));
  return protocol_pack_frame(frame,capacity,AHRS_MSG_DATA_CAPTURE,(uint8_t)p.sample_seq,&p,sizeof(p));
}
int data_capture_command_mutates(uint8_t cmd)
{
  if((cmd>=0x11 && cmd<=0x13) || (cmd>=0x15 && cmd<=0x1e))return 1;
  switch(cmd) {
    case 0x20:case 0x22:case 0x25:case 0x27:case 0x2a:case 0x2b:
    case 0x2d:case 0x2f:case 0x31:case 0x3d:case 0x45:case 0x4b:
    case 0x4c:case 0x4e:case 0x51:case 0x56:case 0x59:case 0x5a:case 0x5d:case 0x5e:case 0x65:
    case 0x68:case 0x69:return 1;
    default:return 0;
  }
}
