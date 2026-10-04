#ifndef FUSION_PROFILE_H
#define FUSION_PROFILE_H
#include <stdint.h>
enum { FUSION_PROFILE_RESPONSE = 0, FUSION_PROFILE_BALANCED = 1,
       FUSION_PROFILE_STABLE = 2, FUSION_PROFILE_ZARU = 3,
       FUSION_PROFILE_COUNT = 4 };
#define FUSION_PROFILE_DEFAULT FUSION_PROFILE_BALANCED
#define ATTITUDE_FILTER_HZ 1000U
typedef struct {
  float tau_mag_s, rest_tau_s, motion_tau_s;
  float tau_acc_s, rest_th_gyr_dps, rest_th_acc_ms2, hold_s;
} fusion_profile_t;
/* Output smoothing, tauAcc, and the outer hold stay per gear. Rest gates
 * are the shared trial on every row: 0.6 °/s and 0.15 m/s². restMinT and
 * biasSigmaRest are the macros in app_config.h. Stable hold is 0.4 s. */
static inline fusion_profile_t fusion_profile_get(unsigned mode)
{
  /* ZARU copies the balanced gear. Heading hold is applied after this filter. */
  const fusion_profile_t profiles[FUSION_PROFILE_COUNT] = {
    {2.0f, 0.15f, 0.04f, 1.0f, 0.6f, 0.15f, 1.5f},
    {4.0f, 0.50f, 0.10f, 2.5f, 0.6f, 0.15f, 0.8f},
    {6.0f, 1.50f, 0.20f, 4.0f, 0.6f, 0.15f, 0.4f},
    {4.0f, 0.50f, 0.10f, 2.5f, 0.6f, 0.15f, 0.8f}};
  unsigned index = mode < (unsigned)FUSION_PROFILE_COUNT ? mode : (unsigned)FUSION_PROFILE_DEFAULT;
  return profiles[index];
}
#endif
