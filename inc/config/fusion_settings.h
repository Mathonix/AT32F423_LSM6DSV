#ifndef FUSION_SETTINGS_H
#define FUSION_SETTINGS_H
#include <stdint.h>
#include "protocol.h"
#include "can_protocol.h"
#include "zaru_heading_hold.h"
typedef enum {
  FUSION_MODE_6AXIS = 0,
  FUSION_MODE_9AXIS = 1,
  FUSION_MODE_9AXIS_RELATIVE = 2
} fusion_mode_t;

typedef struct {
  fusion_mode_t mode;
  uint16_t can_node_id;
  uint8_t fast_start;
  uint8_t reserved;
  output_config_t outputs[2];
  can_config_t can;
  uint16_t gyro_init_ms;
  uint16_t gyro_range_dps;
  uint16_t output_hz;
  uint8_t filter_profile;
  zaru_limits_t zaru;
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
