#ifndef GYRO_STARTUP_CALIBRATION_H
#define GYRO_STARTUP_CALIBRATION_H
#include <stdint.h>

typedef struct {
  uint32_t start_ms, last_ms, samples;
  uint16_t duration_ms;
  uint8_t complete;
  float gyro_sum[3], acc_sum[3];
  uint32_t block_start_ms, block_samples, rejected_windows;
  float gyro_mean[3], gyro_m2[3], acc_mean[3], acc_m2[3], gravity_reference[3];
  uint8_t gravity_reference_valid, rejection_reason;
} gyro_startup_calibration_t;

void gyro_startup_calibration_init(gyro_startup_calibration_t *state, uint16_t duration_ms);
/* rad/s and m/s^2. Rest uses block mean/variance and gravity direction;
 * gross motion, invalid samples and acquisition gaps restart the window. */
int gyro_startup_calibration_push(gyro_startup_calibration_t *state, uint32_t now_ms,
                                  const float gyro[3], const float acc[3]);
int gyro_startup_calibration_result(const gyro_startup_calibration_t *state,
                                    float bias[3], float gravity[3]);
/* Fixed BL deadline: reject movement/gaps instead of extending the window. */
int gyro_startup_calibration_finish_window(const gyro_startup_calibration_t *state,
    uint32_t now_ms, uint32_t window_start_ms, float bias[3], float gravity[3]);
/* Same fixed-deadline validation, with a reason for history fallback. */
uint8_t gyro_startup_calibration_window_reason(const gyro_startup_calibration_t *state,
    uint32_t now_ms, uint32_t window_start_ms);
#endif
