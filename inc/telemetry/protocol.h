#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "can_protocol.h"

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
#define AHRS_MSG_SELECTED_DATA     0x06U /* mask:u16, timestamp:u16, selected floats */
#define AHRS_MSG_DEVICE_CONFIG     0x07U /* Versioned startup and per-port config */
#define AHRS_MSG_CAN_CONFIG        0x08U /* version/ready, active+saved config, bus_off/reserved */
#define AHRS_MSG_ACC_CAL_STATUS    0x0AU /* versioned six-face progress and persisted parameters */
#define AHRS_MSG_FILTER_CONFIG     0x0BU /* active/saved profile and effective filter constants */
#define AHRS_MSG_FUSION_DIAGNOSTIC 0x0CU /* timestamp, raw gyro, bias, residual, raw pose, sigma */
#define AHRS_MSG_VQF_CAL_STATUS    0x0DU /* static VQF initialization progress */
#define AHRS_MSG_VQF_SETTINGS      0x0EU /* applied VQF static parameters and compiled defaults */
#define AHRS_MSG_MOTION_BIAS       0x09U /* live motion-bias enable and estimator numbers */
#define AHRS_MSG_ZARU_CONFIG       0x0FU /* active and saved zero-rate-hold limits */
#define AHRS_MSG_FIRMWARE_INFO     0x32U /* compiled application version text */
#define AHRS_MSG_BIAS_HISTORY      0x34U /* one page of startup gyro-bias history */
#define AHRS_MSG_DEVICE_MODEL      0x36U /* ASCII AT32, exactly 4 bytes, no NUL */
#define AHRS_MSG_STARTUP_BIAS      0x38U /* actual boot selection and current total bias */
#define AHRS_MSG_ACK               0x90U /* Command Acknowledge */

/* Uplink Command IDs (Host -> MCU) */
#define AHRS_CMD_PING              0x10U /* Ping -> replies with ACK */
#define AHRS_CMD_ZERO_YAW          0x11U /* Zero Yaw origin or reset offset */
#define AHRS_CMD_RECALIBRATE_GYRO  0x12U /* Trigger stationary recalibration */
#define AHRS_CMD_SET_STREAM_MODE   0x13U /* Switch active stream mode */
#define AHRS_CMD_QUERY_STATUS      0x14U /* Request System Info frame */
#define AHRS_CMD_SYSTEM_RESET      0x15U /* Request system reboot */
#define AHRS_CMD_ENTER_BOOTLOADER  0x16U /* Reboot into user bootloader */
#define AHRS_CMD_ENTER_SETTINGS    0x17U /* Enter host settings mode */
#define AHRS_CMD_EXIT_SETTINGS     0x18U /* Leave host settings mode */
#define AHRS_CMD_SET_FUSION_MODE   0x19U /* payload: mode, apply_now */
#define AHRS_CMD_SET_CAN_NODE_ID   0x1AU /* payload: uint16 LE, settings mode */
#define AHRS_CMD_START_GYRO_CAL_60S 0x1BU /* start runtime gyro calibration */
#define AHRS_CMD_START_ACC_6FACE_CAL 0x1CU /* start six-face calibration */
#define AHRS_CMD_SET_OUTPUT_HZ      0x1DU /* payload: uint16 LE Hz; apply and persist */
#define AHRS_CMD_SET_STARTUP_CONFIG 0x1EU /* mode, fast_start, apply_now, optional init_ms:u16, range_dps:u16 */
#define AHRS_CMD_QUERY_CONFIG       0x1FU /* empty; reply DEVICE_CONFIG */
#define AHRS_CMD_SET_OUTPUT_CONFIG  0x20U /* port, format, mask:u16 LE, persist */
#define AHRS_CMD_QUERY_CAN_CONFIG   0x21U /* empty; reply CAN_CONFIG */
#define AHRS_CMD_SET_CAN_CONFIG     0x22U /* can_config_t (10 bytes), persist; settings mode */
#define AHRS_CMD_QUERY_FIRMWARE_INFO 0x23U /* empty; reply FIRMWARE_INFO, no ACK */
#define AHRS_CMD_QUERY_ACC_CAL      0x24U /* empty; reply ACC_CAL_STATUS */
#define AHRS_CMD_CANCEL_ACC_CAL     0x25U /* empty; cancel pending six-face observations */
#define AHRS_CMD_QUERY_FILTER       0x26U /* empty; reply FILTER_CONFIG; capability discovery */
#define AHRS_CMD_SET_FILTER         0x27U /* profile:u8, persist:u8; settings mode, apply live */
#define AHRS_CMD_QUERY_FUSION_DIAGNOSTIC 0x28U /* empty; one diagnostic snapshot */
#define AHRS_CMD_QUERY_VQF_CAL     0x29U /* empty; reply VQF_CAL_STATUS, no ACK */
#define AHRS_CMD_START_VQF_CAL     0x2AU /* empty; settings mode; ACK then VQF_CAL_STATUS */
#define AHRS_CMD_CANCEL_VQF_CAL    0x2BU /* empty; ACK then VQF_CAL_STATUS; no flash write */
#define AHRS_CMD_QUERY_VQF_SETTINGS 0x2CU /* empty; reply VQF_SETTINGS, no ACK */
#define AHRS_CMD_RESTORE_VQF_DEFAULTS 0x2DU /* empty; settings mode; invalidate static cal */
#define AHRS_CMD_QUERY_ZARU        0x2EU /* empty; reply ZARU_CONFIG */
#define AHRS_CMD_QUERY_MOTION_BIAS 0x30U /* empty; reply MOTION_BIAS */
#define AHRS_CMD_SET_ZARU          0x2FU /* limits + persist; settings mode, apply live */
#define AHRS_CMD_RESTORE_ZARU      0x31U /* persist + reserved; settings mode, compiled defaults */
#define AHRS_CMD_QUERY_BIAS_HISTORY 0x33U /* empty or offset:u16; reply BIAS_HISTORY, no ACK */
#define AHRS_CMD_QUERY_DEVICE_MODEL 0x35U /* empty; reply DEVICE_MODEL, no ACK */
#define AHRS_CMD_QUERY_STARTUP_BIAS 0x37U /* empty; reply STARTUP_BIAS, no ACK */

#define AHRS_CONFIG_VERSION        4U /* v3 layout; shared BL/init time 0..60000 ms */
#define AHRS_FIELD_COUNT           9U
#define AHRS_FIELDS_ALL            0x01FFU
#define AHRS_FIELDS_ATTITUDE       0x0007U
/* Bit/float order: yaw, pitch, roll, ax, ay, az, gx, gy, gz.
 * Units: deg, m/s^2, deg/s. mask=0 disables telemetry, not command replies. */
#define OUTPUT_FORMAT_JUSTFLOAT    0U
#define OUTPUT_FORMAT_CUSTOM       1U
#define OUTPUT_FORMAT_LEGACY       2U
typedef struct {
  uint8_t format;
  uint8_t legacy_mode;
  uint16_t field_mask;
} output_config_t;

int protocol_output_config_valid(const output_config_t *config);
uint16_t protocol_pack_output(uint8_t *buf, uint16_t capacity, uint8_t seq,
                              const output_config_t *config, const float fields[AHRS_FIELD_COUNT],
                              float temperature, uint8_t flags, uint16_t timestamp_ms);

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
#define AHRS_FLAG_VALUE_CLIPPED    (1U << 5) /* compact fixed-point saturated */

/* ACK Status Codes */
#define AHRS_ACK_SUCCESS           0x00U
#define AHRS_ACK_UNKNOWN_CMD       0x01U
#define AHRS_ACK_INVALID_PARAM     0x02U
#define AHRS_ACK_EXEC_FAILED       0x03U
/* EXEC_FAILED detail while a static VQF initialization is in progress. */
#define AHRS_VQF_CAL_BUSY          0x0701U
#define AHRS_VQF_CAL_NOT_READY     0x0702U
#define AHRS_VQF_CAL_ACTIVE        0x0703U

#pragma pack(push, 1)

typedef struct {
  uint8_t version, active_profile, saved_profile, capabilities;
  uint16_t estimator_hz, reserved;
  float tau_mag_s, rest_tau_s;
} ahrs_payload_filter_config_t;
typedef struct {
  uint8_t version, supported;
  uint16_t reserved;
  float active_enter_dps, active_exit_dps, active_acc_dev_ms2;
  uint16_t active_enter_filter_ms, active_enter_confirm_ms, active_exit_confirm_ms;
  float saved_enter_dps, saved_exit_dps, saved_acc_dev_ms2;
  uint16_t saved_enter_filter_ms, saved_enter_confirm_ms, saved_exit_confirm_ms;
} ahrs_payload_zaru_config_t;
typedef struct {
  float enter_dps, exit_dps, acc_dev_ms2;
  uint16_t enter_filter_ms, enter_confirm_ms, exit_confirm_ms;
  uint8_t persist, reserved;
} ahrs_zaru_set_t;
typedef struct {
  uint8_t persist, reserved;
} ahrs_zaru_restore_t;
typedef struct {
  uint8_t version; /* 1 */
  uint8_t state;
  uint8_t error;
  uint8_t source; /* 0 compiled default, 1 static calibration */
  uint32_t elapsed_ms, remaining_ms, sample_count;
  float gyro_rate_dps, acc_deviation_ms2, temperature_c;
} ahrs_payload_vqf_cal_status_t; /* 28 bytes */
typedef struct {
  uint8_t version; /* 1 */
  uint8_t source; /* 0 compiled default, 1 static calibration */
  uint8_t cal_valid;
  uint8_t reserved;
  float bias_dps[3];
  float bias_sigma_init_dps, bias_sigma_rest_dps, rest_th_gyr_dps, rest_th_acc_ms2;
  float default_sigma_init_dps, default_sigma_rest_dps, default_rest_gyr_dps, default_rest_acc_ms2;
  float calibration_temp_c;
} ahrs_payload_vqf_settings_t; /* 52 bytes */
typedef struct {
  uint8_t format; /* 1 */
  uint8_t text_len;
  char text[14]; /* NUL-terminated ASCII, for example 20261002a */
} ahrs_payload_firmware_info_t; /* 16 bytes */
typedef struct {
  float bias_dps[3];
  float temperature_c;
} ahrs_bias_history_entry_t; /* 16 bytes */
#define AHRS_BIAS_HISTORY_PAGE 3U
typedef struct {
  uint8_t version; /* 1 */
  /* History wire version 2 reports deg/s; legacy version 1 sent rad/s. */
  uint8_t record_version; /* 0 none, 2 legacy 15-entry slot, 3 current slot */
  uint8_t corrupt; /* 1 when a slot was present but unreadable */
  uint8_t count; /* valid samples, 0..50. Index 0 is the oldest */
  uint16_t offset;
  uint8_t entry_count; /* 0..3 samples in entry[] */
  uint8_t reserved; /* 0 */
  uint32_t sequence;
  ahrs_bias_history_entry_t entry[AHRS_BIAS_HISTORY_PAGE];
} ahrs_payload_bias_history_t; /* 60 bytes */
typedef struct {
  uint8_t version, profile, rest, mag_flags;
  uint32_t timestamp_ms;
  float raw_gyro_dps[3], bias_dps[3], residual_dps[3], raw_euler_deg[3];
  float bias_sigma_dps;
} ahrs_payload_fusion_diagnostic_t;
typedef struct {
  uint8_t version, source, reserved[2]; /* source: 1 fresh, 2 history, 3 default */
  uint32_t duration_ms, elapsed_ms, samples, rejected_windows, rejection_reason;
  uint16_t range_dps, reserved2;
  float boot_temperature_c, selected_temperature_c;
  float initial_bias_dps[3], current_bias_dps[3];
} ahrs_payload_startup_bias_t; /* 60 bytes; initial values are immutable per boot */
typedef struct {
  uint8_t version, motion_bias_enabled, rest_bias_enabled, rest_detected;
  float bias_sigma_motion_dps, bias_vertical_forgetting, bias_forgetting_time_s;
  float bias_clip_dps, bias_sigma_rest_dps, tau_acc_s, bias_dps[3], residual_norm_dps;
  uint8_t zaru_hold, zaru_enabled;
  uint16_t reserved;
} ahrs_payload_motion_bias_t;

typedef struct {
  uint8_t version, source, active_mode, saved_mode;
  uint8_t active_fast_start, saved_fast_start, capabilities, reserved;
  uint16_t output_hz;
  output_config_t outputs[2]; /* Active UART then USB configuration */
  uint16_t active_gyro_init_ms, saved_gyro_init_ms;
  uint16_t active_gyro_range_dps, saved_gyro_range_dps, saved_output_hz;
} ahrs_payload_device_config_t;
typedef struct {
  uint8_t version, ready;
  can_config_t active, saved;
  uint8_t bus_off, reserved;
} ahrs_payload_can_config_t;

typedef struct {
  uint8_t version, status, phase, detected_face, face_mask, enabled, valid, reserved;
  uint16_t progress_permille, error;
  uint32_t samples, elapsed_ms, remaining_ms;
  float bias_g[3], scale[3], raw_g[3];
} ahrs_payload_acc_cal_t; /* 60 bytes; face 0=none, 1..6=-X,+X,-Y,+Y,-Z,+Z */

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
  int16_t gz_x10;        /* 0.1 dps; saturates, flags bit5 marks clipping */
  uint8_t flags;
  uint8_t reserved;
  uint16_t timestamp_ms;
} ahrs_payload_compact_t;

typedef struct
{
  uint32_t fusion_hz;    /* configured fusion target, not measured throughput */
  uint32_t out_hz;       /* configured stream target */
  uint16_t skip_n;       /* actual cumulative skips, saturates at 65535 */
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
typedef void (*protocol_vofa_cb_t)(void *user_data);

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
  uint8_t vofa_match;
  protocol_vofa_cb_t vofa_cb;
};

/* API */
uint16_t protocol_crc16(const uint8_t *data, uint16_t len);
void protocol_parser_init(protocol_parser_t *parser, protocol_frame_cb_t cb, void *user_data);
/* Optional ASCII "vofa" command (case-insensitive, no newline required).
 * Recognized only outside AA55 frames; state is independent for each port. */
void protocol_parser_set_vofa_callback(protocol_parser_t *parser, protocol_vofa_cb_t cb);
void protocol_parser_feed_byte(protocol_parser_t *parser, uint8_t byte);

uint16_t protocol_pack_frame(uint8_t *buf, uint16_t capacity, uint8_t msg_id, uint8_t seq, const void *payload, uint8_t len);
uint16_t protocol_pack_device_model(uint8_t *buf, uint16_t capacity, uint8_t seq);
uint16_t protocol_pack_attitude(uint8_t *buf, uint16_t capacity, uint8_t seq, float roll, float pitch, float yaw, uint8_t flags, uint16_t timestamp_ms);
uint16_t protocol_pack_quaternion(uint8_t *buf, uint16_t capacity, uint8_t seq, float qw, float qx, float qy, float qz, uint16_t timestamp_ms);
uint16_t protocol_pack_compact(uint8_t *buf, uint16_t capacity, uint8_t seq, float roll, float pitch, float yaw, float gz, uint8_t flags, uint16_t timestamp_ms);
uint16_t protocol_pack_imu(uint8_t *buf, uint16_t capacity, uint8_t seq, float gx, float gy, float gz, float ax, float ay, float az, float temp_c, uint16_t timestamp_ms);
uint16_t protocol_pack_system_info(uint8_t *buf, uint16_t capacity, uint8_t seq, uint32_t fusion_hz, uint32_t out_hz, uint32_t skip_n, float temp_c, uint8_t stream_mode, uint8_t can_ok);
uint16_t protocol_pack_ack(uint8_t *buf, uint16_t capacity, uint8_t seq, uint8_t cmd_id, uint8_t status, uint16_t detail);

#ifdef __cplusplus
}
#endif

#endif
