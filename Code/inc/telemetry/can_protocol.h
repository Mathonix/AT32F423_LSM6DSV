#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H
#include <stdint.h>

/* This project's configurable Damiao transport. IDs are 11-bit standard IDs.
 * period_ms is the period PER selected group, not an aggregate frame period. */
typedef struct {
  uint16_t node_id, master_id, period_ms;
  uint8_t baud_index, active, output_mask, reserved;
} can_config_t;
#define CAN_OUTPUT_ACCEL 1U
#define CAN_OUTPUT_GYRO 2U
#define CAN_OUTPUT_EULER 4U
#define CAN_OUTPUT_QUAT 8U
typedef struct {
  float roll, pitch, yaw, gx_rad, gy_rad, gz_rad;
  float ax_ms2, ay_ms2, az_ms2, qw, qx, qy, qz, temperature;
} can_sample_t;
void can_config_defaults(can_config_t *config);
int can_config_valid(const can_config_t *config);
unsigned can_output_count(uint8_t mask);
uint32_t can_baudrate(unsigned index);
uint16_t can_baud_divisor(unsigned index);
/* Sensor reply and active transmission have identical eight-byte payloads. */
int can_damiao_pack(uint8_t type, const can_sample_t *sample, uint8_t data[8]);
#define CAN_ACTION_CONFIG 1U
#define CAN_ACTION_SAVE 2U
#define CAN_ACTION_REBOOT 4U
#define CAN_ACTION_ZERO 8U
/* Unspecified register widths/units are project conventions: uint32 LE,
 * interval in ms, active 0/1, mask bits accel/gyro/euler/quat. Not a claim
 * that the original Damiao firmware supports these extensions. */
int can_damiao_request(uint16_t id, uint8_t dlc, const uint8_t data[8],
                       const can_config_t *config, const can_sample_t *sample,
                       uint16_t *reply_id, uint8_t reply[8],
                       can_config_t *next, uint8_t *actions);
#endif
