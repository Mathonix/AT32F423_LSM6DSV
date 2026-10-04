#ifndef ATTITUDE_OUTPUT_H
#define ATTITUDE_OUTPUT_H
#include <stdint.h>
#include "fusion_profile.h"
typedef struct {
  float q[4], last_q6[4];
  float rest_hold_s;
  uint8_t initialized;
} attitude_output_t;
void attitude_from_euler(float roll, float pitch, float yaw, float q[4]);
void attitude_to_euler(const float q[4], float *roll, float *pitch, float *yaw);
/* Fixed estimator cadence; transmission never advances this state.
 * q6 provides body-frame rotation increments (no magnetometer correction).
 * q_target includes the desired absolute/relative heading reference. */
int attitude_output_update(attitude_output_t *s, const float q6[4],
                           const float q_target[4], uint8_t rest,
                           float residual_rate_dps, uint8_t profile, float dt);
#endif
