#include "gyro_startup_calibration.h"
#include "app_config.h"
#include <math.h>
#include <string.h>

void gyro_startup_calibration_init(gyro_startup_calibration_t *s, uint16_t duration_ms)
{
  memset(s, 0, sizeof(*s));
  s->duration_ms = duration_ms;
}

static void restart_window(gyro_startup_calibration_t *s)
{
  s->samples = 0;
  memset(s->gyro_sum, 0, sizeof(s->gyro_sum));
  memset(s->acc_sum, 0, sizeof(s->acc_sum));
  s->block_samples = 0;
  s->gravity_reference_valid = 0;
  memset(s->gyro_mean, 0, sizeof(s->gyro_mean));
  memset(s->gyro_m2, 0, sizeof(s->gyro_m2));
  memset(s->acc_mean, 0, sizeof(s->acc_mean));
  memset(s->acc_m2, 0, sizeof(s->acc_m2));
}

int gyro_startup_calibration_push(gyro_startup_calibration_t *s, uint32_t now,
                                  const float gyro[3], const float acc[3])
{
  float gyro_norm2 = 0, acc_norm2 = 0;
  if(s->complete) return 1;
  for(unsigned i = 0; i < 3; ++i) {
    if(!isfinite(gyro[i]) || !isfinite(acc[i])) { restart_window(s); s->rejection_reason=1; ++s->rejected_windows; return 0; }
    gyro_norm2 += gyro[i] * gyro[i];
    acc_norm2 += acc[i] * acc[i];
  }
  const float gyro_limit = APP_CAL_GYR_REST_DPS * 0.017453292519943295f;
  if(gyro_norm2 > gyro_limit * gyro_limit ||
     fabsf(sqrtf(acc_norm2) - 9.80665f) > APP_CAL_ACC_REST_MS2) {
    restart_window(s); s->rejection_reason=2; ++s->rejected_windows; return 0;
  }
  if(s->samples && (uint32_t)(now - s->last_ms) > 5U) { restart_window(s); s->rejection_reason=3; ++s->rejected_windows; }
  if(!s->samples) s->start_ms = now;
  s->last_ms = now;
  if(!s->block_samples) s->block_start_ms=now;
  ++s->block_samples;
  for(unsigned i = 0; i < 3; ++i) {
    /* Whole-window online means avoid long float sums at 2 kHz. */
    s->gyro_sum[i] += (gyro[i]-s->gyro_sum[i])/(float)(s->samples+1U);
    s->acc_sum[i] += (acc[i]-s->acc_sum[i])/(float)(s->samples+1U);
    float dg=gyro[i]-s->gyro_mean[i], da=acc[i]-s->acc_mean[i];
    s->gyro_mean[i] += dg/(float)s->block_samples;
    s->acc_mean[i] += da/(float)s->block_samples;
    s->gyro_m2[i] += dg*(gyro[i]-s->gyro_mean[i]);
    s->acc_m2[i] += da*(acc[i]-s->acc_mean[i]);
  }
  ++s->samples;
  if((uint32_t)(now-s->block_start_ms)>=100U && s->block_samples>=50U) {
    const float gyro_std_limit=.15f*0.017453292519943295f;
    uint8_t bad=0;
    for(unsigned i=0;i<3;i++) {
      if(s->gyro_m2[i]/(float)(s->block_samples-1U)>gyro_std_limit*gyro_std_limit ||
         s->acc_m2[i]/(float)(s->block_samples-1U)>.15f*.15f) bad=4;
    }
    if(s->gravity_reference_valid) {
      float dot=0, a2=0, b2=0;
      for(unsigned i=0;i<3;i++) {
        dot+=s->gravity_reference[i]*s->acc_mean[i];
        a2+=s->gravity_reference[i]*s->gravity_reference[i];
        b2+=s->acc_mean[i]*s->acc_mean[i];
      }
      if(dot<.999847695f*sqrtf(a2*b2)) bad=5; /* gravity changed by >1 degree */
    } else { memcpy(s->gravity_reference,s->acc_mean,sizeof(s->acc_mean)); s->gravity_reference_valid=1; }
    if(bad) { restart_window(s); s->rejection_reason=bad; ++s->rejected_windows; return 0; }
    s->block_samples=0;
    memset(s->gyro_mean,0,sizeof(s->gyro_mean)); memset(s->gyro_m2,0,sizeof(s->gyro_m2));
    memset(s->acc_mean,0,sizeof(s->acc_mean)); memset(s->acc_m2,0,sizeof(s->acc_m2));
  }
  /* At least 1 kHz of valid samples as well as the full stationary duration. */
  s->complete = (uint32_t)(now - s->start_ms) >= s->duration_ms && s->samples >= s->duration_ms;
  return s->complete;
}

int gyro_startup_calibration_result(const gyro_startup_calibration_t *s,
                                    float bias[3], float gravity[3])
{
  if(!s->complete || !s->samples) return 0;
  for(unsigned i = 0; i < 3; ++i) { bias[i] = s->gyro_sum[i]; gravity[i] = s->acc_sum[i]; }
  return 1;
}
