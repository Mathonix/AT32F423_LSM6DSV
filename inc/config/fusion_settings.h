#ifndef FUSION_SETTINGS_H
#define FUSION_SETTINGS_H
#include <stdint.h>
#include "protocol.h"
#include "can_protocol.h"
#include "zaru_heading_hold.h"
#include "fusion_algorithm.h"
#include "vqf_tuning.h"
#include "../calibration/gyro_scale.h"
#include "../fusion/eskf_tuning.h"
#include "../calibration/mag_calibration.h"
typedef enum {
  FUSION_MODE_6AXIS = 0,
  FUSION_MODE_9AXIS = 1,
  FUSION_MODE_9AXIS_RELATIVE = 2
} fusion_mode_t;

typedef struct {
  fusion_mode_t mode;
  uint16_t can_node_id;
  uint8_t fast_start;
  uint8_t ws2812_enabled;
  uint8_t mag_rejection_enabled;
  uint8_t fusion_algorithm;
  uint8_t rest_bias_enabled;
  output_config_t outputs[2];
  can_config_t can;
  uint16_t gyro_init_ms;
  uint16_t gyro_range_dps;
  uint16_t output_hz;
  uint8_t filter_profile;
  zaru_limits_t zaru;
  mag_cal_parameters_t mag_cal;
  uint8_t vqf_tuning_enabled;
  vqf_tuning_t vqf_tuning;
  gyro_scale_t gyro_scale;
  uint8_t acc_range_g;
  uint8_t gyro_prefilter_enabled;
  float gyro_prefilter_hz;
  float gyro_prefilter_q;
  uint8_t eskf_tuning_custom;
  eskf_tuning_t eskf_tuning;
  uint8_t rest_indicator_enabled;
  uint8_t mag_vqf_gate_enabled;
  /* Field magnitude the current calibration was fitted to, uT. Zero means
   * unknown (older calibration or explicitly cleared): the health check is
   * then inactive rather than guessing a value. */
  float mag_expected_ut;
} device_settings_t;
void device_settings_defaults(device_settings_t *settings);
int device_settings_load(device_settings_t *settings);
int device_settings_save(const device_settings_t *settings);

/* Settings are stored as one CRC-protected record so mode and CAN ID cannot
 * get out of sync after a power loss. */
int fusion_settings_load_ex(fusion_mode_t *mode, uint16_t *can_node_id);
int fusion_settings_save_ex(fusion_mode_t mode, uint16_t can_node_id);
int fusion_settings_load(fusion_mode_t *mode);
int fusion_settings_save(fusion_mode_t mode);
#endif
