#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Framing delimiters */
#define AHRS_SYNC1                 0xAAU
#define AHRS_SYNC2                 0x55U
#define AHRS_MAX_PAYLOAD_LEN       64U
#define AHRS_HEADER_LEN            5U  /* SYNC1(1) + SYNC2(1) + MSG_ID(1) + LEN(1) + SEQ(1) */
#define AHRS_CHECKSUM_LEN          2U  /* CRC16_L(1) + CRC16_H(1) */
#define AHRS_FRAME_OVERHEAD        (AHRS_HEADER_LEN + AHRS_CHECKSUM_LEN)
#define AHRS_MAX_FRAME_LEN         (AHRS_FRAME_OVERHEAD + AHRS_MAX_PAYLOAD_LEN)

/* Downlink Message IDs (MCU -> Host) */
#define AHRS_MSG_ATTITUDE_EULER    0x01U /* Roll, Pitch, Yaw, Status, Timestamp */
#define AHRS_MSG_QUATERNION        0x02U /* Qw, Qx, Qy, Qz, Timestamp */
#define AHRS_MSG_IMU_RAW           0x03U /* GyrXYZ, AccXYZ, Temp, Timestamp */
#define AHRS_MSG_COMPACT           0x04U /* Scaled int16 attitude, gz, status */
#define AHRS_MSG_SYSTEM_INFO       0x05U /* Rate, skip_n, temp, mode, can_ok */
#define AHRS_MSG_ACK               0x90U /* Command Acknowledge */

/* Uplink Command IDs (Host -> MCU) */
#define AHRS_CMD_PING              0x10U /* Ping -> replies with ACK */
#define AHRS_CMD_ZERO_YAW          0x11U /* Zero Yaw origin or reset offset */
#define AHRS_CMD_RECALIBRATE_GYRO  0x12U /* Trigger stationary recalibration */
#define AHRS_CMD_SET_STREAM_MODE   0x13U /* Switch active stream mode */
#define AHRS_CMD_QUERY_STATUS      0x14U /* Request System Info frame */
#define AHRS_CMD_SYSTEM_RESET      0x15U /* Request system reboot */
#define AHRS_CMD_ENTER_SETTINGS    0x17U /* Enter host settings mode */
#define AHRS_CMD_EXIT_SETTINGS     0x18U /* Leave host settings mode */
#define AHRS_CMD_SET_FUSION_MODE   0x19U /* payload: mode, apply_now */
#define AHRS_CMD_SET_CAN_NODE_ID   0x1AU /* payload: uint16 LE, settings mode */
#define AHRS_CMD_START_GYRO_CAL_60S 0x1BU /* start runtime gyro calibration */
#define AHRS_CMD_START_ACC_6FACE_CAL 0x1CU /* start six-face calibration */

/* Stream Modes */
typedef enum
{
  STREAM_MODE_VOFA_3CH    = 0U, /* VOFA+ JustFloat 3 channels (Yaw, Pitch, Roll) */
  STREAM_MODE_BIN_ATT     = 1U, /* Binary AHRS Euler attitude frame */
  STREAM_MODE_BIN_COMPACT = 2U, /* Binary compact attitude frame */
  STREAM_MODE_BIN_IMU     = 3U, /* Binary 9-axis raw/filtered IMU frame */
  STREAM_MODE_VOFA_6CH    = 4U  /* VOFA+ JustFloat 6 channels (Yaw, Pitch, Roll, Gz, Az, Temp) */
} stream_mode_t;

/* Status Flags Bitmask */
#define AHRS_FLAG_REST_DETECTED    (1U << 0)
#define AHRS_FLAG_MAG_VALID        (1U << 1)
#define AHRS_FLAG_MAG_DISTURBED    (1U << 2)
#define AHRS_FLAG_CALIB_DONE       (1U << 3)
#define AHRS_FLAG_SENSOR_ERROR     (1U << 4)

/* ACK Status Codes */
#define AHRS_ACK_SUCCESS           0x00U
#define AHRS_ACK_UNKNOWN_CMD       0x01U
#define AHRS_ACK_INVALID_PARAM     0x02U
#define AHRS_ACK_EXEC_FAILED       0x03U

#pragma pack(push, 1)

typedef struct
{
  float roll;            /* deg, -180.0 ~ +180.0 */
  float pitch;           /* deg, -90.0 ~ +90.0 */
  float yaw;             /* deg, -180.0 ~ +180.0 */
  uint8_t flags;         /* status flags */
  uint8_t reserved;      /* 0 */
  uint16_t timestamp_ms; /* millis() low 16 bits */
} ahrs_payload_attitude_t;

typedef struct
{
  float qw;
  float qx;
  float qy;
  float qz;
  uint16_t timestamp_ms;
} ahrs_payload_quaternion_t;

typedef struct
{
  float gx;              /* dps */
  float gy;              /* dps */
  float gz;              /* dps */
  float ax;              /* m/s^2 */
  float ay;              /* m/s^2 */
  float az;              /* m/s^2 */
  int16_t temp_c_x100;   /* temp_c * 100 */
  uint16_t timestamp_ms;
} ahrs_payload_imu_t;

typedef struct
{
  int16_t roll_x100;     /* roll * 100 (0.01 deg) */
  int16_t pitch_x100;    /* pitch * 100 (0.01 deg) */
  int16_t yaw_x100;      /* yaw * 100 (0.01 deg) */
  int16_t gz_x10;        /* gz * 10 (0.1 dps) */
  uint8_t flags;
  uint8_t reserved;
  uint16_t timestamp_ms;
} ahrs_payload_compact_t;

typedef struct
{
  uint32_t fusion_hz;
  uint32_t out_hz;
  uint16_t skip_n;
  int16_t temp_c_x100;
  uint8_t stream_mode;
  uint8_t can_ok;
  uint16_t reserved;
} ahrs_payload_system_info_t;

typedef struct
{
  uint8_t cmd_id;
  uint8_t status;
  uint16_t detail;
} ahrs_payload_ack_t;

#pragma pack(pop)

/* Parser state machine */
typedef enum
{
  PARSE_STATE_SYNC1 = 0,
  PARSE_STATE_SYNC2,
  PARSE_STATE_MSG_ID,
  PARSE_STATE_LEN,
  PARSE_STATE_SEQ,
  PARSE_STATE_PAYLOAD,
  PARSE_STATE_CRC_L,
  PARSE_STATE_CRC_H
} parse_state_t;

typedef struct protocol_parser_s protocol_parser_t;

typedef void (*protocol_frame_cb_t)(uint8_t msg_id, uint8_t seq, const uint8_t *payload, uint8_t len, void *user_data);

struct protocol_parser_s
{
  parse_state_t state;
  uint8_t msg_id;
  uint8_t payload_len;
  uint8_t seq;
  uint8_t payload_idx;
  uint16_t rx_crc;
  uint8_t payload[AHRS_MAX_PAYLOAD_LEN];
  protocol_frame_cb_t frame_cb;
  void *user_data;
  uint32_t parsed_frames;
  uint32_t crc_errors;
};

/* API */
uint16_t protocol_crc16(const uint8_t *data, uint16_t len);
void protocol_parser_init(protocol_parser_t *parser, protocol_frame_cb_t cb, void *user_data);
void protocol_parser_feed_byte(protocol_parser_t *parser, uint8_t byte);

uint16_t protocol_pack_frame(uint8_t *buf, uint8_t msg_id, uint8_t seq, const void *payload, uint8_t len);
uint16_t protocol_pack_attitude(uint8_t *buf, uint8_t seq, float roll, float pitch, float yaw, uint8_t flags, uint16_t timestamp_ms);
uint16_t protocol_pack_quaternion(uint8_t *buf, uint8_t seq, float qw, float qx, float qy, float qz, uint16_t timestamp_ms);
uint16_t protocol_pack_compact(uint8_t *buf, uint8_t seq, float roll, float pitch, float yaw, float gz, uint8_t flags, uint16_t timestamp_ms);
uint16_t protocol_pack_imu(uint8_t *buf, uint8_t seq, float gx, float gy, float gz, float ax, float ay, float az, float temp_c, uint16_t timestamp_ms);
uint16_t protocol_pack_system_info(uint8_t *buf, uint8_t seq, uint32_t fusion_hz, uint32_t out_hz, uint16_t skip_n, float temp_c, uint8_t stream_mode, uint8_t can_ok);
uint16_t protocol_pack_ack(uint8_t *buf, uint8_t seq, uint8_t cmd_id, uint8_t status, uint16_t detail);

#ifdef __cplusplus
}
#endif

#endif
