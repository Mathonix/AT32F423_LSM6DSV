#ifndef FUSION_SETTINGS_H
#define FUSION_SETTINGS_H
#include <stdint.h>
typedef enum {
  FUSION_MODE_6AXIS = 0,
  FUSION_MODE_9AXIS = 1,
  FUSION_MODE_9AXIS_RELATIVE = 2
} fusion_mode_t;

/* Settings are stored as one CRC-protected record so mode and CAN ID cannot
 * get out of sync after a power loss. */
int fusion_settings_load_ex(fusion_mode_t *mode, uint16_t *can_node_id);
int fusion_settings_save_ex(fusion_mode_t mode, uint16_t can_node_id);
int fusion_settings_load(fusion_mode_t *mode);
int fusion_settings_save(fusion_mode_t mode);
#endif
