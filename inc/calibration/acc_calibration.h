#ifndef ACC_CALIBRATION_H
#define ACC_CALIBRATION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ACC_CAL_FACE_COUNT 6U
#define ACC_CAL_STATUS_IDLE 0U
#define ACC_CAL_STATUS_RUNNING 1U
#define ACC_CAL_STATUS_DONE 2U
#define ACC_CAL_STATUS_FAILED 3U

typedef struct
{
  float bias_g[3];
  float scale[3];
  uint8_t valid;
} acc_calibration_t;

extern acc_calibration_t acc_calibration_active;
void acc_calibration_load(void);
int acc_calibration_save(const acc_calibration_t *cal);
static inline float acc_calibrate_g(unsigned axis, float nominal_g)
{
  if(axis >= 3U || acc_calibration_active.valid == 0U) return nominal_g;
  return (nominal_g - acc_calibration_active.bias_g[axis]) * acc_calibration_active.scale[axis];
}

#ifdef __cplusplus
}
#endif
#endif
