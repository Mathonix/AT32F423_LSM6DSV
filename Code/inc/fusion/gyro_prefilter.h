#ifndef GYRO_PREFILTER_H
#define GYRO_PREFILTER_H
/* One second-order low-pass in front of VQF, EKF and ESKF.
 * Disabled passes every sample through unchanged. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GYRO_PREFILTER_DEFAULT_HZ 2.0f
#define GYRO_PREFILTER_DEFAULT_Q  0.70710677f
static inline int gyro_prefilter_params_ok(int enabled, float cutoff_hz, float q)
{
  return (enabled == 0 || enabled == 1) && cutoff_hz == cutoff_hz && q == q &&
         cutoff_hz >= 0.2f && cutoff_hz <= 200.0f && q >= 0.2f && q <= 5.0f;
}
void gyro_prefilter_configure(int enabled, float cutoff_hz, float q);
void gyro_prefilter_get(int *enabled, float *cutoff_hz, float *q);
void gyro_prefilter_apply(float gyro[3], float dt);
#ifdef __cplusplus
}
#endif
#endif
