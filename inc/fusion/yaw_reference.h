#ifndef YAW_REFERENCE_H
#define YAW_REFERENCE_H
#include <stdint.h>

typedef struct {
  float boot_offset_deg;
  uint8_t initialized, magnetic_initialized;
} yaw_reference_t;

static inline float yaw_reference_wrap(float angle)
{
  while(angle > 180.0f) angle -= 360.0f;
  while(angle < -180.0f) angle += 360.0f;
  return angle;
}

/* VQF changes from its 6D startup frame to the magnetic frame on the first
 * updateMag. Remove that one frame change without removing actual rotation
 * before the magnetometer arrives. Later magnetic corrections remain active. */
static inline float yaw_reference_apply(yaw_reference_t *reference,
                                        float yaw_deg, float magnetic_delta_deg,
                                        uint8_t magnetic_ready)
{
  if(magnetic_ready && !reference->magnetic_initialized) {
    if(reference->initialized)
      reference->boot_offset_deg = yaw_reference_wrap(reference->boot_offset_deg + magnetic_delta_deg);
    reference->magnetic_initialized = 1U;
  }
  if(!reference->initialized) {
    reference->boot_offset_deg = yaw_deg;
    reference->initialized = 1U;
  }
  return yaw_reference_wrap(yaw_deg - reference->boot_offset_deg);
}
#endif
