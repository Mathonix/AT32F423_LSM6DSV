#ifndef FUSION_PROFILE_H
#define FUSION_PROFILE_H
#include <stdint.h>
enum { FUSION_PROFILE_RESPONSE = 0, FUSION_PROFILE_BALANCED = 1,
       FUSION_PROFILE_STABLE = 2, FUSION_PROFILE_COUNT = 3 };
#define FUSION_PROFILE_DEFAULT FUSION_PROFILE_BALANCED
#define ATTITUDE_FILTER_HZ 1000U
typedef struct { float tau_mag_s, rest_tau_s, motion_tau_s; } fusion_profile_t;
/* Initial engineering presets; improvement must be measured on the board.
 * Gyro bandwidth and bias-estimator tuning deliberately stay common. */
static inline fusion_profile_t fusion_profile_get(unsigned mode)
{
  const fusion_profile_t profiles[3] = {{2.0f, 0.15f, 0.04f},
                                       {4.0f, 0.50f, 0.10f},
                                       {6.0f, 1.50f, 0.20f}};
  return profiles[mode < FUSION_PROFILE_COUNT ? mode : FUSION_PROFILE_DEFAULT];
}
#endif
