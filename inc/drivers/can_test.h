#ifndef CAN_TEST_H
#define CAN_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_TEST_LIVE_MAGIC 0x43414E31u /* "CAN1" */

/* Damiao DM-IMU-L1 Protocol Constants */
#define DAMIAO_CAN_TYPE_ACCEL       0x01U
#define DAMIAO_CAN_TYPE_GYRO        0x02U
#define DAMIAO_CAN_TYPE_EULER       0x03U
#define DAMIAO_CAN_TYPE_QUAT        0x04U

/* Damiao Request / Response Delimiters */
#define DAMIAO_REQ_HEADER           0xCCU
#define DAMIAO_REQ_TAIL             0xDDU

/* Damiao Register IDs */
#define DAMIAO_REG_REBOOT           0x00U
#define DAMIAO_REG_ACCEL            0x01U
#define DAMIAO_REG_GYRO             0x02U
#define DAMIAO_REG_EULER            0x03U
#define DAMIAO_REG_QUAT             0x04U
#define DAMIAO_REG_ZERO_YAW         0x05U
#define DAMIAO_REG_CALIB_ACC        0x06U
#define DAMIAO_REG_CALIB_GYRO       0x07U
#define DAMIAO_REG_CALIB_MAG        0x08U
#define DAMIAO_REG_COMM_TYPE        0x09U
#define DAMIAO_REG_SEND_INTERVAL    0x0AU
#define DAMIAO_REG_ACTIVE_MODE      0x0BU
#define DAMIAO_REG_BAUDRATE         0x0CU
#define DAMIAO_REG_CAN_ID           0x0DU
#define DAMIAO_REG_MST_ID           0x0EU
#define DAMIAO_REG_OUTPUT_SEL       0x0FU
#define DAMIAO_REG_SAVE_PARAM       0xFEU
#define DAMIAO_REG_RESTORE_FACTORY  0xFFU

/* Damiao ACK Codes */
#define DAMIAO_ACK_SUCCESS          0x00U
#define DAMIAO_ACK_NO_REG           0x01U
#define DAMIAO_ACK_INVALID_DATA     0x02U
#define DAMIAO_ACK_FAIL             0x03U

/* Damiao Scaling Limits */
#define DAMIAO_ACCEL_MIN            (-235.2f)
#define DAMIAO_ACCEL_MAX            (235.2f)
#define DAMIAO_GYRO_MIN             (-34.88f)
#define DAMIAO_GYRO_MAX             (34.88f)
#define DAMIAO_PITCH_MIN            (-90.0f)
#define DAMIAO_PITCH_MAX            (90.0f)
#define DAMIAO_ROLL_MIN             (-180.0f)
#define DAMIAO_ROLL_MAX             (180.0f)
#define DAMIAO_YAW_MIN              (-180.0f)
#define DAMIAO_YAW_MAX              (180.0f)
#define DAMIAO_QUAT_MIN             (-1.0f)
#define DAMIAO_QUAT_MAX             (1.0f)

/* Internal Command Flags */
#define CAN_CMD_FLAG_ZERO_YAW       (1U << 0)
#define CAN_CMD_FLAG_RECAL          (1U << 1)
#define CAN_CMD_FLAG_REBOOT         (1U << 2)

typedef struct
{
  uint32_t magic;
  uint32_t seq;
  uint32_t millis;
  uint32_t init_ok;
  uint32_t tx_count;
  uint32_t tx_success_count;
  uint32_t tx_failed_count;
  uint32_t tx_no_mailbox_count;
  uint32_t tx_pending_count;
  uint32_t tx_error_counter;
  uint32_t rx_error_counter;
  uint32_t error_record;
  uint32_t bus_off;
  uint32_t error_passive;
  uint32_t last_mailbox;
  uint32_t last_status;
  uint32_t last_data_counter;
  uint32_t rx_count;
  uint32_t rx_standard_count;
  uint32_t rx_extended_count;
  uint32_t rx_overrun_count;
  uint32_t rx_pending;
  uint32_t rx_last_id;
  uint32_t rx_last_dlc;
  uint32_t rx_last_frame_type;
  uint32_t rx_last_millis;
  uint32_t rx_last_data[8];
} can_test_live_t;

extern volatile can_test_live_t can_test_live;

void can_test_init(void);
/* Run from the main loop with a monotonic microsecond timestamp. */
void can_test_task(uint32_t now_us);
void can_test_update_data(float roll, float pitch, float yaw,
                          float gx_dps, float gy_dps, float gz_dps,
                          float ax_g, float ay_g, float az_g,
                          float qw, float qx, float qy, float qz,
                          float temp_c, uint8_t flags);
uint8_t can_test_get_cmd_flag(void);
void can_test_clear_cmd_flag(uint8_t flag);

/* Backward compatibility wrapper */
static inline void can_test_update_attitude(float roll, float pitch, float yaw, float gz, float az, float temp_c, uint8_t flags)
{
  can_test_update_data(roll, pitch, yaw, 0.0f, 0.0f, gz, 0.0f, 0.0f, az, 1.0f, 0.0f, 0.0f, 0.0f, temp_c, flags);
}

#ifdef __cplusplus
}
#endif

#endif
