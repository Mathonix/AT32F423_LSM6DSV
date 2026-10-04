#ifndef ZARU_HEADING_HOLD_H
#define ZARU_HEADING_HOLD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Output-only stationary heading hold. VQF keeps integrating and estimating
 * bias. This state only cancels published yaw drift; it does not read VQF rest.
 * limits == NULL, or an invalid limits block, uses the compiled defaults. */
typedef struct {
  float enter_dps;
  float exit_dps;
  float acc_dev_ms2;
  uint16_t enter_filter_ms;
  uint16_t enter_confirm_ms;
  uint16_t exit_confirm_ms;
  uint16_t reserved;
} zaru_limits_t;
void zaru_limits_default(zaru_limits_t *limits);
int zaru_limits_valid(const zaru_limits_t *limits);
typedef struct {
  float yaw_offset_deg;
  float hold_yaw_deg;
  float gyro_rate_fast_dps;
  float gyro_rate_rms_dps;
  float rate2_lpf;
  float acc_deviation_ms2;
  float exit_ms;
  float enter_ms;
  uint8_t hold_active;
  uint8_t initialized;
  uint32_t enter_count;
  uint32_t exit_count;
} zaru_heading_hold_t;
void zaru_heading_hold_reset(zaru_heading_hold_t *s);
/* gyro_rate_fast_dps is the bias-corrected 3-axis norm in deg/s, before the
 * enter low-pass. acc_deviation_ms2 is | |a| - g |. enabled=0 keeps the yaw
 * offset applied so leaving the profile does not jump. A non-finite raw yaw
 * returns the held heading, or 0 before the first sample. */
float zaru_heading_hold_update(zaru_heading_hold_t *s, float raw_yaw_deg,
    float gyro_rate_fast_dps, float acc_deviation_ms2, float dt, uint8_t enabled,
    const zaru_limits_t *limits);
#ifdef __cplusplus
}
#endif
#endif
