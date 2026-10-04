#include "gyro_startup_calibration.h"
#include "app_config.h"
#include <math.h>
#include <string.h>

void gyro_startup_calibration_init(gyro_startup_calibration_t *s, uint16_t duration_ms)
{
  memset(s, 0, sizeof(*s));
  s->duration_ms = duration_ms;
}

static void clear_block(gyro_startup_calibration_t *s)
{
  s->block_samples=0; s->block_bad=0;
  memset(s->gyro_mean,0,sizeof(s->gyro_mean));
  memset(s->gyro_m2,0,sizeof(s->gyro_m2));
  memset(s->acc_mean,0,sizeof(s->acc_mean));
  memset(s->acc_m2,0,sizeof(s->acc_m2));
}

static uint8_t block_rejection(const gyro_startup_calibration_t *s)
{
  float g2=0, a2=0;
  if(s->block_samples<2U) return 0;
  for(unsigned i=0;i<3;++i) {
    g2+=s->gyro_mean[i]*s->gyro_mean[i];
    a2+=s->acc_mean[i]*s->acc_mean[i];
  }
  if(s->gravity_reference_valid) {
    float dot=0,b2=0;
    for(unsigned i=0;i<3;++i) {
      dot+=s->gravity_reference[i]*s->acc_mean[i];
      b2+=s->gravity_reference[i]*s->gravity_reference[i];
    }
    if(dot<.999847695f*sqrtf(a2*b2)) return 5;
  }
  const float mean_limit=APP_CAL_GYR_REST_DPS*0.017453292519943295f;
  if(g2>mean_limit*mean_limit) return 7; /* mean angular rate */
  if(fabsf(sqrtf(a2)-9.80665f)>APP_CAL_ACC_REST_MS2) return 8;
  const float std_limit=APP_STARTUP_GYR_STD_DPS*0.017453292519943295f;
  for(unsigned i=0;i<3;++i) {
    if(s->gyro_m2[i]/(float)(s->block_samples-1U)>std_limit*std_limit) return 4;
    if(s->acc_m2[i]/(float)(s->block_samples-1U)>
       APP_STARTUP_ACC_STD_MS2*APP_STARTUP_ACC_STD_MS2) return 9;
  }
  return 0;
}

/* Commit only a qualified block. Rejected blocks never enter either mean. */
void gyro_startup_calibration_finalize(gyro_startup_calibration_t *s)
{
  if(!s->block_active) return;
  uint32_t span=(uint32_t)(s->block_last_ms-s->block_start_ms)+1U;
  uint8_t checked=block_rejection(s);
  uint8_t bad=checked==5U ? checked : s->block_bad ? s->block_bad : checked;
  if(!bad && (s->block_samples<2U || s->block_samples<span)) bad=10;
  s->last_block_reason=bad;
  if(bad) { ++s->rejected_windows; s->rejection_reason=bad; }
  else {
    float weight=(float)s->block_samples/(float)(s->samples+s->block_samples);
    for(unsigned i=0;i<3;++i) {
      s->gyro_sum[i]+=(s->gyro_mean[i]-s->gyro_sum[i])*weight;
      s->acc_sum[i]+=(s->acc_mean[i]-s->acc_sum[i])*weight;
    }
    s->samples+=s->block_samples; s->accepted_ms+=span;
    if(!s->gravity_reference_valid) {
      memcpy(s->gravity_reference,s->acc_mean,sizeof(s->acc_mean));
      s->gravity_reference_valid=1;
    }
  }
  s->block_active=0; clear_block(s);
}

int gyro_startup_calibration_push(gyro_startup_calibration_t *s, uint32_t now,
                                  const float gyro[3], const float acc[3])
{
  if(s->complete) return 1;
  if(!s->seen_samples) s->start_ms=now;
  if(s->block_active && (uint32_t)(now-s->last_ms)>5U) {
    s->block_bad=3; gyro_startup_calibration_finalize(s);
  }
  if(s->block_active && (uint32_t)(now-s->block_start_ms)>=APP_STARTUP_BLOCK_MS)
    gyro_startup_calibration_finalize(s);
  if(!s->block_active) { s->block_active=1; s->block_start_ms=now; }
  s->block_last_ms=now; s->last_ms=now; ++s->seen_samples;
  float g2=0,a2=0;
  for(unsigned i=0;i<3;++i) {
    if(!isfinite(gyro[i]) || !isfinite(acc[i])) { s->block_bad=1; return 0; }
    g2+=gyro[i]*gyro[i]; a2+=acc[i]*acc[i];
  }
  const float gross=APP_STARTUP_GYR_GROSS_DPS*.017453292519943295f;
  if(g2>gross*gross) { s->block_bad=7; return 0; }
  if(fabsf(sqrtf(a2)-9.80665f)>APP_STARTUP_ACC_GROSS_MS2) { s->block_bad=8; return 0; }
  ++s->block_samples;
  for(unsigned i=0;i<3;++i) {
    float dg=gyro[i]-s->gyro_mean[i], da=acc[i]-s->acc_mean[i];
    s->gyro_mean[i]+=dg/(float)s->block_samples;
    s->acc_mean[i]+=da/(float)s->block_samples;
    s->gyro_m2[i]+=dg*(gyro[i]-s->gyro_mean[i]);
    s->acc_m2[i]+=da*(acc[i]-s->acc_mean[i]);
  }
  if(s->duration_ms && (uint32_t)(now-s->start_ms)>=s->duration_ms) {
    gyro_startup_calibration_finalize(s);
    s->complete=!gyro_startup_calibration_window_reason(s,now,s->start_ms);
  }
  return s->complete;
}

int gyro_startup_calibration_result(const gyro_startup_calibration_t *s,
                                    float bias[3], float gravity[3])
{
  if(!s->complete || !s->samples) return 0;
  for(unsigned i=0;i<3;++i) { bias[i]=s->gyro_sum[i]; gravity[i]=s->acc_sum[i]; }
  return 1;
}

uint8_t gyro_startup_calibration_window_reason(const gyro_startup_calibration_t *s,
    uint32_t now, uint32_t start)
{
  if(!s->duration_ms) return 0;
  gyro_startup_calibration_t qualified=*s;
  gyro_startup_calibration_finalize(&qualified);
  if((uint32_t)(now-start)<s->duration_ms) return 13;
  if(!s->seen_samples || (uint32_t)(now-s->last_ms)>5U) return 12;
  if(qualified.samples<s->duration_ms || qualified.samples<2U ||
     qualified.accepted_ms<((uint32_t)s->duration_ms+1U)/2U)
    return qualified.rejected_windows ? qualified.rejection_reason : 10;
  /* A changed final orientation cannot use gravity from an earlier pose. */
  if(qualified.last_block_reason==5U) return 5;
  return 0;
}

int gyro_startup_calibration_finish_window(const gyro_startup_calibration_t *s,
    uint32_t now, uint32_t start, float bias[3], float gravity[3])
{
  if(!s->duration_ms || gyro_startup_calibration_window_reason(s,now,start)) return 0;
  gyro_startup_calibration_t qualified=*s;
  gyro_startup_calibration_finalize(&qualified);
  for(unsigned i=0;i<3;++i) { bias[i]=qualified.gyro_sum[i]; gravity[i]=qualified.acc_sum[i]; }
  return 1;
}
