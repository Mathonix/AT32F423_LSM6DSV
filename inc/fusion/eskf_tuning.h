#ifndef ESKF_TUNING_H
#define ESKF_TUNING_H
#include <stdint.h>
#include <math.h>
#include <string.h>
/* Version 1 page is 60 bytes: version, page, flags, reserved, then this body.
 * custom=0 keeps the compiled literals, including the 0.1091 dps rest floor. */
typedef struct {
  float rest_tau_s;
  float rest_fast_tau_s;
  float rest_boot_s;
  float rest_floor_dps;
  float mag_lock_s;
  float mag_step_rad;
  float mag_drop_rad;
  float mag_soft;
  float mag_hard;
  float mag_soft_dip_rad;
  float mag_hard_dip_rad;
  float tilt_rad;
  uint16_t rest_leave;
  uint16_t rest_return;
  uint16_t mag_reject_limit;
  uint16_t accel_age;
} eskf_tuning_t;
#ifdef __cplusplus
static_assert(sizeof(eskf_tuning_t) == 56U, "ESKF tuning body");
#else
_Static_assert(sizeof(eskf_tuning_t) == 56U, "ESKF tuning body");
#endif

static inline void eskf_tuning_defaults(eskf_tuning_t *p) {
  if(!p) return;
  /* The resting heading drift is tau times the gyro bias creep. Measured on
   * this board over 5 min: 12 s -> 1.87 deg/h, 6 s -> 0.36 deg/h, 3 s ->
   * -0.006 deg/5min (at the noise floor, 0.0020 deg detrended). The shipped
   * unit stores 3 s through the runtime tuning record; 6 s stays the compiled
   * fallback because it is the conservative choice if a future unit turns out
   * to creep faster. See tools/serial/zerodrift_*.csv. */
  p->rest_tau_s = 6.f;
  p->rest_fast_tau_s = 1.f;
  p->rest_boot_s = 3.f;
  p->rest_floor_dps = 0.1091f;
  p->mag_lock_s = 1.0f;
  p->mag_step_rad = 0.4f;
  p->mag_drop_rad = 1.f;
  p->mag_soft = 0.10f;
  p->mag_hard = 0.20f;
  p->mag_soft_dip_rad = 0.087f;
  p->mag_hard_dip_rad = 0.175f;
  p->tilt_rad = 0.05f;
  p->rest_leave = 8U;
  p->rest_return = 800U;
  p->mag_reject_limit = 500U;
  p->accel_age = 240U;
}
static inline int eskf_tuning_valid(const eskf_tuning_t *p) {
  if(!p) return 0;
  if(!isfinite(p->rest_tau_s) || p->rest_tau_s < 0.5f || p->rest_tau_s > 120.f) return 0;
  if(!isfinite(p->rest_fast_tau_s) || p->rest_fast_tau_s < 0.05f || p->rest_fast_tau_s > 30.f) return 0;
  if(p->rest_fast_tau_s > p->rest_tau_s) return 0;
  if(!isfinite(p->rest_boot_s) || p->rest_boot_s < 0.f || p->rest_boot_s > 60.f) return 0;
  if(!isfinite(p->rest_floor_dps) || p->rest_floor_dps < 0.02f || p->rest_floor_dps > 2.f) return 0;
  if(!isfinite(p->mag_lock_s) || p->mag_lock_s < 0.f || p->mag_lock_s > 10.f) return 0;
  if(!isfinite(p->mag_step_rad) || p->mag_step_rad < 0.05f || p->mag_step_rad > 1.5f) return 0;
  if(!isfinite(p->mag_drop_rad) || p->mag_drop_rad < p->mag_step_rad || p->mag_drop_rad > 3.14f) return 0;
  if(!isfinite(p->mag_soft) || p->mag_soft < 0.01f || p->mag_soft > 0.5f) return 0;
  if(!isfinite(p->mag_hard) || p->mag_hard < p->mag_soft || p->mag_hard > 1.f) return 0;
  if(!isfinite(p->mag_soft_dip_rad) || p->mag_soft_dip_rad < 0.01f || p->mag_soft_dip_rad > 0.5f) return 0;
  if(!isfinite(p->mag_hard_dip_rad) || p->mag_hard_dip_rad < p->mag_soft_dip_rad || p->mag_hard_dip_rad > 1.5f) return 0;
  if(!isfinite(p->tilt_rad) || p->tilt_rad < 0.005f || p->tilt_rad > 0.5f) return 0;
  if(p->rest_leave < 1U || p->rest_leave > 200U) return 0;
  if(p->rest_return < 1U || p->rest_return > 8000U) return 0;
  if(p->mag_reject_limit < 1U || p->mag_reject_limit > 5000U) return 0;
  if(p->accel_age < 1U || p->accel_age > 8000U) return 0;
  return 1;
}
static inline int eskf_tuning_is_default(const eskf_tuning_t *p) {
  eskf_tuning_t d;
  eskf_tuning_defaults(&d);
  return p && memcmp(p, &d, sizeof(d)) == 0;
}
#ifdef __cplusplus
extern "C" {
#endif
void eskf_apply_tuning(const eskf_tuning_t *tuning, int custom);
void eskf_read_tuning(eskf_tuning_t *tuning, int *custom);
#ifdef __cplusplus
}
#endif
#endif
