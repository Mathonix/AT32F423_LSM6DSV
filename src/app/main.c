/**
 * AT32F423KCU7-4 + LSM6DSV
 *   IMU HA01 2000 Hz + IST8310 50 Hz, 9D VQF, DAP live telemetry
 */

#include "at32f423_clock.h"
#include "bsp.h"
#include "lsm6dsv.h"
#include "ist8310.h"
#include "ws2812.h"
#include "vqf.h"
#include "attitude_output.h"
#include "zaru_heading_hold.h"
#include "yaw_reference.h"
#include "app_config.h"
#include "vqf_live.h"
#include "mag_calibration.h"
#include "acc_calibration.h"
#include "acc_six_face.h"
#include "gyro_bias_history.h"
#include "boot_startup.h"
#include "gyro_startup_calibration.h"
#include "vqf_static_cal.h"
#include "can_test.h"
#include "usb_cdc.h"
#include "protocol.h"
#include "fusion_settings.h"
#include "gyro_range.h"
#include "boot_request.h"
#include "user_bl_update.h"
#ifdef APP_USER_BL_UPDATE
static uint8_t user_bl_pending;
static uint32_t user_bl_requested_ms;
#endif

#include <math.h>
#include <string.h>

#define FUSION_RATE_HZ       APP_FUSION_HZ
#define FUSION_HZ            ((float)FUSION_RATE_HZ)
#if (APP_VOFA_OUTPUT_HZ == 0U)
#error "APP_VOFA_OUTPUT_HZ must be non-zero"
#elif (APP_VOFA_OUTPUT_HZ > APP_FUSION_HZ)
#error "APP_VOFA_OUTPUT_HZ cannot exceed APP_FUSION_HZ"
#elif ((APP_FUSION_HZ % APP_VOFA_OUTPUT_HZ) != 0U)
#error "APP_VOFA_OUTPUT_HZ must divide APP_FUSION_HZ exactly"
#endif
#define OUTPUT_DIV           (FUSION_RATE_HZ / APP_VOFA_OUTPUT_HZ)
#define DAP_OUTPUT_HZ        ((APP_VOFA_OUTPUT_HZ < 20U) ? APP_VOFA_OUTPUT_HZ : 20U)
#if ((APP_FUSION_HZ % DAP_OUTPUT_HZ) != 0U)
#error "DAP_OUTPUT_HZ must divide APP_FUSION_HZ exactly"
#endif
#define DAP_OUTPUT_DIV       ((FUSION_RATE_HZ >= DAP_OUTPUT_HZ) ? (FUSION_RATE_HZ / DAP_OUTPUT_HZ) : 1U)
#define YAW_KF_SYNC_DIV      1U   /* VOFA output is now the synchronized 200 Hz stream */

#define GYR_DPS_PER_LSB      gyro_range_dps_per_lsb(app_gyro_range_dps)
#define ACC_G_PER_LSB        0.000122f
#define DEG2RAD              0.017453292519943295f
#define G_TO_MS2             9.80665f
#define SAMPLE_DT_CYCLES     ((uint32_t)(system_core_clock / (uint32_t)FUSION_HZ))
#define DROP_DT_CYCLES       ((SAMPLE_DT_CYCLES * 3U) / 2U)
#define CAL_GYR_REST_DPS APP_CAL_GYR_REST_DPS
#define CAL_ACC_REST_MS2 APP_CAL_ACC_REST_MS2
#define CAL_BIAS_TEMP_WINDOW_C APP_GYR_BIAS_TEMP_WINDOW_C
#define GYR_LPF_CUTOFF_HZ APP_GYR_LPF_CUTOFF_HZ /* reduce gyro noise before integration */
#define ERR_STREAK_RECOVER   20U
#define MAG_PERIOD_N         40U /* read IST8310 at 50 Hz */
#define MAG_VQF_DIV          APP_MAG_VQF_UPDATE_DIV /* Full VQF update at 10 Hz */
#define MAG_FUSION_ENABLE    APP_MAG_FUSION_ENABLE
#define MAG_TIMEOUT_N        30U
#define MAG_UT_PER_LSB       0.3f

/* Separate startup diagnostic ABI; existing live telemetry stays unchanged. */
volatile struct {
  uint32_t magic, status, duration_ms, elapsed_ms, samples;
  float bias_dps[3];
} gyro_startup_live = {0x47535443U, 0, APP_GYR_INIT_DEFAULT_MS, 0, 0, {0}};
static ahrs_payload_startup_bias_t app_startup_bias;
/* Separate raw capture ABI; leaves existing attitude telemetry unchanged. */
volatile struct {
  uint32_t magic, seq, millis, sample_n, fusion_enabled;
  int32_t x, y, z;
} mag_raw_live = {0x4D524157U, 0U, 0U, 0U, MAG_FUSION_ENABLE, 0, 0, 0};

/* Separate 36-byte coherent ABI; legacy ax/ay/az remain nominal raw g. */
volatile struct {
  uint32_t magic, seq, millis;
  float raw_g[3], corrected_g[3];
} acc_cal_live = {0x4143434CU, 0U, 0U, {0}, {0}};

volatile struct {
  uint32_t magic, seq, millis;
  int16_t raw_temp;
  int16_t reserved;
  float temperature_c;
} imu_temp_live = {0x54454D50U, 0U, 0U, 0, 0, 25.0f};

/* Output VOFA/DAPLink pose ABI. Quaternion-filtered pose, fixed estimator cadence. */
volatile struct {
  uint32_t magic, seq, millis;
  float yaw, pitch, roll, temperature_c;
} vofa_pose_live = {0x56504F53U, 0U, 0U, 0.0f, 0.0f, 0.0f, 25.0f};

volatile vqf_live_t vqf_live;
volatile vqf_tune_live_t vqf_tune_live;
volatile yaw_kf_sync_live_t yaw_kf_sync_live = {
  YAW_KF_SYNC_MAGIC, 0U, 0U, 0.0f, 0.0f, 0.0f, 0.0f, 0U, 0U,
  APP_GYR_TEMP_REF_C, 0.0f, 0.0f
};
/* Separate ABI: preserves all existing tune/VOFA layouts. */
volatile struct {
  uint32_t magic, seq;
  vqf_tune_live_t snapshot;
  float diagnostic[8]; /* yaw6, refNorm, refDipDeg, rejectT, candidateT,
                       * corrRateDegS, disagreementDeg, biasSigmaDegS */
} vqf_nine_live = {0x39565146U, 0U, {0}, {0}};

#define VOFA_MAX_BYTES AHRS_MAX_FRAME_LEN /* Also holds BIN_IMU (28 + 7 bytes). */
_Static_assert(VOFA_MAX_BYTES >= AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_imu_t),
               "IMU packet exceeds telemetry DMA buffer");

static uint8_t vofa_dma[2][VOFA_MAX_BYTES];
static uint8_t usb_telemetry[VOFA_MAX_BYTES];
static uint8_t vofa_sel;
static uint32_t vofa_late;
static uint32_t app_sample_skips;

static float wrap_deg(float angle)
{
  while(angle > 180.0f) angle -= 360.0f;
  while(angle < -180.0f) angle += 360.0f;
  return angle;
}

static float app_yaw_offset = 0.0f;
static zaru_heading_hold_t app_zaru;
static void app_zero_yaw(float current_yaw)
{
  app_yaw_offset = wrap_deg(app_yaw_offset + current_yaw);
}

/* UART and USB CDC accept the same framed host-command protocol. */
typedef enum
{
  PROTOCOL_SOURCE_UART = 0,
  PROTOCOL_SOURCE_USB = 1,
  /* WebUSB (USB interface 2) is a separate transport for replies but shares
   * the USB output configuration (port index 1). Never persisted. */
  PROTOCOL_SOURCE_WEBUSB = 2
} protocol_source_t;

/* Output-config / stream index (0 = UART, 1 = USB) for a transport. */
#define PROTOCOL_PORT(source) ((source) == PROTOCOL_SOURCE_UART ? 0U : 1U)

/* Non-blocking automatic six-face accelerometer calibration. */
#if APP_ACC_CAL_ENABLE
static acc_six_face_t acc_cal_rt;
static uint8_t acc_cal_source, acc_cal_command_seq, acc_cal_final_sent;
static uint32_t acc_cal_report_ms;
#endif
volatile struct {
  uint32_t magic, seq, millis;
  uint8_t status, face, detected_face, face_mask;
  uint16_t sample_count;
  uint16_t reserved;
  float bias_g[3], scale[3];
} acc_cal_status_live = {0x41434353U,0U,0U,0U,0U,0U,0U,0U,0U,{0},{1.0f,1.0f,1.0f}};

static protocol_parser_t uart_protocol_parser;
static protocol_parser_t usb_protocol_parser;
static protocol_parser_t webusb_protocol_parser;
static stream_mode_t app_stream_mode = STREAM_MODE_VOFA_3CH;
static fusion_mode_t app_fusion_mode = FUSION_MODE_9AXIS;
static device_settings_t app_saved_settings;
static output_config_t app_outputs[2];
static uint8_t app_fast_start;
static uint8_t app_filter_profile = FUSION_PROFILE_DEFAULT;
static attitude_output_t app_attitude;
static float app_output_q[4] = {1,0,0,0};
static float app_residual_dps[3];
static float app_zaru_rate_fast_dps;
static float app_zaru_acc_dev_ms2;
static zaru_limits_t app_zaru_limits;
static uint8_t app_fusion_ready;
static protocol_source_t app_vqf_cal_port;
static uint32_t app_vqf_cal_report_ms;
static float app_vqf_pre_cal_bias_rad[3];
static uint8_t app_vqf_pre_cal_bias_valid;
static uint16_t app_gyro_init_ms;
static uint16_t app_gyro_range_dps = GYRO_RANGE_DEFAULT_DPS;
static uint8_t app_relative_yaw_enabled;
/* Common UART/USB telemetry rate, applied live and restored from Flash. */
static uint16_t app_output_hz = APP_VOFA_OUTPUT_HZ;
static uint16_t app_output_div = OUTPUT_DIV;
static uint8_t app_stream_seq[2];
typedef enum { RESET_NONE, RESET_APPLICATION, RESET_BOOTLOADER } reset_request_t;
static volatile reset_request_t protocol_reset_pending;
static uint32_t reset_requested_ms;
static volatile uint8_t app_sample_rebase;
volatile uint32_t app_flash_pause_count, app_flash_pause_us;

static void app_request_reset(reset_request_t request)
{
  reset_requested_ms = millis();
  protocol_reset_pending = request;
}

/* Flash erase/program suspends the sampling contract; the next sample
 * establishes a new timing baseline instead of being integrated as a 500 us step. */
static void app_flash_pause_end(uint32_t start_cycles)
{
  app_flash_pause_us = (dwt_cycles() - start_cycles) /
                       (system_core_clock / 1000000U);
  app_flash_pause_count++;
  app_sample_rebase = 1U;
}

static volatile uint8_t app_settings_mode;
static volatile uint8_t app_settings_dirty;

static int app_save_device_settings(const device_settings_t *settings)
{
  uint32_t start = dwt_cycles();
  int result = device_settings_save(settings);
  app_flash_pause_end(start);
  if(result == 0) app_saved_settings = *settings;
  return result;
}

static int app_save_settings(fusion_mode_t mode, uint16_t node_id)
{
  device_settings_t settings = app_saved_settings;
  settings.mode = mode;
  settings.can_node_id = node_id;
  settings.can.node_id = node_id;
  return app_save_device_settings(&settings);
}

int can_test_save_config(const can_config_t *config)
{
  device_settings_t saved = app_saved_settings;
  saved.can = *config; saved.can_node_id = config->node_id;
  return app_save_device_settings(&saved);
}

#if APP_ACC_CAL_ENABLE
static int app_save_acc_calibration(const acc_calibration_t *cal)
{
  uint32_t start = dwt_cycles();
  int result = acc_calibration_save(cal);
  app_flash_pause_end(start);
  return result;
}
#endif

static int app_save_gyro_bias(const float bias[3], float temp_c)
{
#ifdef APP_USER_BL_UPDATE
  /* Temporary maintenance APP must not alter the persistent bias journal. */
  (void)bias; (void)temp_c; return 0;
#else
  uint32_t start = dwt_cycles();
  int result = gyro_bias_history_save_at_temp(bias, temp_c);
  app_flash_pause_end(start);
  return result;
#endif
}

/* Prefer the most recent capture within a narrow temperature window;
 * otherwise use the closest record. Never average unrelated boot captures. */
static int app_select_temp_matched_bias(float bias_rad[3], float *matched_temp_c,
                                        float temp_c)
{
  float latest[3];
  float average[3];
  float nearest[3];
  float nearest_temp_c = 0.0f;
  uint32_t count = 0U;
  uint8_t nearest_valid = 0U;
  uint8_t corrupt = 0U;
  unsigned axis;
  int valid = gyro_bias_history_load_recent_for_temp(
      latest, average, nearest, temp_c, CAL_BIAS_TEMP_WINDOW_C,
      &nearest_temp_c, &nearest_valid, &count, &corrupt);

  (void)latest;
  (void)average;
  (void)corrupt;
  if((valid != 0) && (nearest_valid != 0U))
  {
    for(axis = 0U; axis < 3U; ++axis) bias_rad[axis] = nearest[axis];
    if(matched_temp_c != NULL) *matched_temp_c = nearest_temp_c;
    return 1;
  }
  if(valid && count && gyro_bias_history_load_nearest(bias_rad,temp_c,matched_temp_c)) return 1;
  bias_rad[0] = APP_GYR_DEFAULT_BIAS_X_DPS * DEG2RAD;
  bias_rad[1] = APP_GYR_DEFAULT_BIAS_Y_DPS * DEG2RAD;
  bias_rad[2] = APP_GYR_DEFAULT_BIAS_Z_DPS * DEG2RAD;
  if(matched_temp_c != NULL) *matched_temp_c = temp_c;
  return 0;
}

volatile uint32_t protocol_reply_drops_uart, protocol_reply_drops_usb, protocol_reply_drops_webusb;

static int protocol_send_frame(protocol_source_t source, const uint8_t *frame, uint16_t len)
{
  int result;
  /* Replies go back only to the transport the command arrived on. */
  if(source == PROTOCOL_SOURCE_UART) result = uart_control_enqueue(frame, len);
  else if(source == PROTOCOL_SOURCE_WEBUSB) result = webusb_write(frame, len);
  else result = usb_cdc_write(frame, len);
  if(result != 0)
  {
    if(source == PROTOCOL_SOURCE_UART) protocol_reply_drops_uart++;
    else if(source == PROTOCOL_SOURCE_WEBUSB) protocol_reply_drops_webusb++;
    else protocol_reply_drops_usb++;
  }
  return result;
}

static int protocol_reply_ack(protocol_source_t source, uint8_t seq,
                               uint8_t cmd_id, uint8_t status, uint16_t detail)
{
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  uint16_t len = protocol_pack_ack(frame, sizeof(frame), seq, cmd_id, status, detail);
  return (len != 0U) ? protocol_send_frame(source, frame, len) : -1;
}

static void protocol_reply_firmware_info(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_firmware_info_t info;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  _Static_assert(sizeof(info) == 16U, "firmware info payload");
  _Static_assert(sizeof(APP_FIRMWARE_VERSION) == 10, "firmware version is YYYYMMDD and one letter");
  memset(&info, 0, sizeof(info));
  info.format = 1U;
  info.text_len = (uint8_t)(sizeof(APP_FIRMWARE_VERSION) - 1U);
  memcpy(info.text, APP_FIRMWARE_VERSION, sizeof(APP_FIRMWARE_VERSION));
  uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_FIRMWARE_INFO,
                                  seq, &info, sizeof(info));
  if(n) (void)protocol_send_frame(source, frame, n);
}

static void protocol_reply_bias_history(protocol_source_t source, uint8_t seq, uint16_t offset)
{
  ahrs_payload_bias_history_t page;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  float bias[AHRS_BIAS_HISTORY_PAGE][3];
  float temperature_c[AHRS_BIAS_HISTORY_PAGE];
  uint32_t copied = 0U, count = 0U, sequence = 0U;
  uint8_t record_version = 0U, corrupt = 0U;
  uint32_t i;
  _Static_assert(sizeof(page) == 60U, "bias history page");
  _Static_assert(AHRS_BIAS_HISTORY_PAGE == 3U, "bias history page length");
  _Static_assert(GYRO_BIAS_HISTORY_MAX <= 255U, "bias history count fits in one byte");
  memset(&page, 0, sizeof(page));
  (void)gyro_bias_history_read(offset, AHRS_BIAS_HISTORY_PAGE, bias, temperature_c,
                               &copied, &count, &sequence, &record_version, &corrupt);
  page.version = 2U; /* v1 accidentally transmitted rad/s; v2 is deg/s. */
  page.record_version = record_version;
  page.corrupt = corrupt;
  page.count = (uint8_t)count;
  page.offset = offset;
  page.entry_count = (uint8_t)copied;
  page.sequence = sequence;
  for(i = 0U; i < copied && i < AHRS_BIAS_HISTORY_PAGE; ++i)
  {
    for(unsigned axis = 0U; axis < 3U; ++axis)
      page.entry[i].bias_dps[axis] = bias[i][axis] / DEG2RAD;
    page.entry[i].temperature_c = temperature_c[i];
  }
  uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_BIAS_HISTORY,
                                  seq, &page, sizeof(page));
  if(n) (void)protocol_send_frame(source, frame, n);
}

static void protocol_reply_config(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_device_config_t config;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  memset(&config, 0, sizeof(config));
  config.version = AHRS_CONFIG_VERSION; config.source = (uint8_t)source;
  config.active_mode = (uint8_t)app_fusion_mode;
  config.saved_mode = (uint8_t)app_saved_settings.mode;
  config.active_fast_start = app_fast_start;
  config.saved_fast_start = app_saved_settings.fast_start;
  config.capabilities = 126U | (APP_MAG_FUSION_ENABLE ? 1U : 0U) | (APP_ACC_CAL_ENABLE ? 128U : 0U);
  config.output_hz = app_output_hz;
  memcpy(config.outputs, app_outputs, sizeof(app_outputs));
  config.active_gyro_init_ms = app_gyro_init_ms;
  config.saved_gyro_init_ms = app_saved_settings.gyro_init_ms;
  config.active_gyro_range_dps = app_gyro_range_dps;
  config.saved_gyro_range_dps = app_saved_settings.gyro_range_dps;
  config.saved_output_hz = app_saved_settings.output_hz;
  uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_DEVICE_CONFIG,
                                  seq, &config, sizeof(config));
  if(n) (void)protocol_send_frame(source, frame, n);
}

static void protocol_reply_can_config(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_can_config_t config;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  memset(&config, 0, sizeof(config));
  config.version = 1; config.ready = can_test_live.init_ok != 0;
  can_test_get_config(&config.active); config.saved = app_saved_settings.can;
  config.bus_off = can_test_live.bus_off != 0;
  uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_CAN_CONFIG, seq, &config, sizeof(config));
  if(n) (void)protocol_send_frame(source, frame, n);
}

static void protocol_reply_zaru(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_zaru_config_t c;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  _Static_assert(sizeof(c) == 40U, "zaru config payload");
  _Static_assert(sizeof(ahrs_zaru_set_t) == 20U, "zaru set payload");
  _Static_assert(sizeof(ahrs_zaru_restore_t) == 2U, "zaru restore payload");
  memset(&c, 0, sizeof(c));
  c.version = 1U;
  c.supported = APP_ZARU_ENABLE ? 1U : 0U;
  c.active_enter_dps = app_zaru_limits.enter_dps;
  c.active_exit_dps = app_zaru_limits.exit_dps;
  c.active_acc_dev_ms2 = app_zaru_limits.acc_dev_ms2;
  c.active_enter_filter_ms = app_zaru_limits.enter_filter_ms;
  c.active_enter_confirm_ms = app_zaru_limits.enter_confirm_ms;
  c.active_exit_confirm_ms = app_zaru_limits.exit_confirm_ms;
  c.saved_enter_dps = app_saved_settings.zaru.enter_dps;
  c.saved_exit_dps = app_saved_settings.zaru.exit_dps;
  c.saved_acc_dev_ms2 = app_saved_settings.zaru.acc_dev_ms2;
  c.saved_enter_filter_ms = app_saved_settings.zaru.enter_filter_ms;
  c.saved_enter_confirm_ms = app_saved_settings.zaru.enter_confirm_ms;
  c.saved_exit_confirm_ms = app_saved_settings.zaru.exit_confirm_ms;
  uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_ZARU_CONFIG, seq, &c, sizeof(c));
  if(n) (void)protocol_send_frame(source, frame, n);
}

static void protocol_reply_vqf_cal(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_vqf_cal_status_t status;
  vqf_static_cal_status_t live;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  _Static_assert(sizeof(status) == 28U, "vqf cal status payload");
  vqf_static_cal_get_status(&live);
  memset(&status, 0, sizeof(status));
  status.version = 1U;
  status.state = live.state;
  status.error = live.error;
  status.source = live.source;
  status.elapsed_ms = live.elapsed_ms;
  status.remaining_ms = live.remaining_ms;
  status.sample_count = live.sample_count;
  status.gyro_rate_dps = live.gyro_rate_dps;
  status.acc_deviation_ms2 = live.acc_deviation_ms2;
  status.temperature_c = live.temperature_c;
  {
    uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_VQF_CAL_STATUS,
                                    seq, &status, sizeof(status));
    if(n) (void)protocol_send_frame(source, frame, n);
  }
}

static void protocol_reply_vqf_settings(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_vqf_settings_t payload;
  vqf_static_params_t applied, defaults;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  _Static_assert(sizeof(payload) == 52U, "vqf settings payload");
  vqf_static_cal_get_applied(&applied);
  vqf_static_cal_defaults(&defaults);
  memset(&payload, 0, sizeof(payload));
  payload.version = 1U;
  payload.source = vqf_static_cal_source();
  payload.cal_valid = payload.source;
  if(payload.source == VQF_STATIC_CAL_SOURCE_CAL) {
    memcpy(payload.bias_dps, applied.gyro_bias_dps, sizeof(payload.bias_dps));
    payload.bias_sigma_init_dps = applied.bias_sigma_init_dps;
    payload.bias_sigma_rest_dps = applied.bias_sigma_rest_dps;
    payload.rest_th_gyr_dps = applied.rest_th_gyr_dps;
    payload.rest_th_acc_ms2 = applied.rest_th_acc_ms2;
    payload.calibration_temp_c = applied.calibration_temp_c;
  } else {
    float bias_rad[3];
    unsigned axis;
    vqf_get_gyr_bias(bias_rad);
    for(axis = 0U; axis < 3U; ++axis) payload.bias_dps[axis] = bias_rad[axis] / DEG2RAD;
    payload.bias_sigma_init_dps = defaults.bias_sigma_init_dps;
    payload.bias_sigma_rest_dps = defaults.bias_sigma_rest_dps;
    payload.rest_th_gyr_dps = defaults.rest_th_gyr_dps;
    payload.rest_th_acc_ms2 = defaults.rest_th_acc_ms2;
  }
  payload.default_sigma_init_dps = defaults.bias_sigma_init_dps;
  payload.default_sigma_rest_dps = defaults.bias_sigma_rest_dps;
  payload.default_rest_gyr_dps = defaults.rest_th_gyr_dps;
  payload.default_rest_acc_ms2 = defaults.rest_th_acc_ms2;
  {
    uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_VQF_SETTINGS,
                                    seq, &payload, sizeof(payload));
    if(n) (void)protocol_send_frame(source, frame, n);
  }
}

static void app_vqf_cal_service(uint32_t now_ms)
{
  vqf_static_cal_status_t st;
  vqf_static_params_t params;
  uint32_t start = dwt_cycles();
  int flashed = 0;
  vqf_static_cal_tick(now_ms);
  if(!vqf_static_cal_poll(&st, &params)) {
    if(vqf_static_cal_active() && (uint32_t)(now_ms - app_vqf_cal_report_ms) >= 200U) {
      protocol_reply_vqf_cal(app_vqf_cal_port, 0U);
      app_vqf_cal_report_ms = now_ms;
    }
    return;
  }
  if(st.state == VQF_STATIC_CAL_DONE && st.error == VQF_STATIC_CAL_OK) {
    float bias_rad[3];
    unsigned axis;
    vqf_set_rest_thresholds(params.rest_th_gyr_dps, params.rest_th_acc_ms2);
    vqf_set_bias_sigmas(params.bias_sigma_init_dps, params.bias_sigma_rest_dps);
    for(axis = 0U; axis < 3U; ++axis) bias_rad[axis] = params.gyro_bias_dps[axis] * DEG2RAD;
    vqf_seed_gyr_bias(bias_rad, params.bias_sigma_init_dps);
    flashed = 1;
    protocol_reply_vqf_cal(app_vqf_cal_port, 0U);
    protocol_reply_vqf_settings(app_vqf_cal_port, 0U);
  } else {
    if(st.error == VQF_STATIC_CAL_ERR_FLASH_WRITE) flashed = 1;
    protocol_reply_vqf_cal(app_vqf_cal_port, 0U);
  }
  if(flashed) app_flash_pause_end(start);
  app_vqf_cal_report_ms = now_ms;
}

static void protocol_reply_filter(protocol_source_t source, uint8_t seq)
{
  fusion_profile_t p=fusion_profile_get(app_filter_profile);
  ahrs_payload_filter_config_t c={1,app_filter_profile,app_saved_settings.filter_profile,
                                (uint8_t)FUSION_PROFILE_COUNT,
                                ATTITUDE_FILTER_HZ,0,p.tau_mag_s,p.rest_tau_s};
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  uint16_t n=protocol_pack_frame(frame,sizeof(frame),AHRS_MSG_FILTER_CONFIG,seq,&c,sizeof(c));
  if(n) protocol_send_frame(source,frame,n);
}

static void protocol_reply_motion_bias(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_motion_bias_t d;
  vqf_bias_estimator_config_t config;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  float norm = 0.0f;
  unsigned axis;
  _Static_assert(sizeof(d) == 48U, "motion bias payload");
  memset(&d, 0, sizeof(d));
  vqf_get_bias_estimator_config(&config);
  d.version = 1U;
  d.motion_bias_enabled = config.motion_bias_enabled;
  d.rest_bias_enabled = config.rest_bias_enabled;
  d.rest_detected = (uint8_t)vqf_get_rest_detected();
  d.bias_sigma_motion_dps = config.bias_sigma_motion_dps;
  d.bias_vertical_forgetting = config.bias_vertical_forgetting;
  d.bias_forgetting_time_s = config.bias_forgetting_time_s;
  d.bias_clip_dps = config.bias_clip_dps;
  d.bias_sigma_rest_dps = config.bias_sigma_rest_dps;
  d.tau_acc_s = config.tau_acc_s;
  d.bias_dps[0] = vqf_live.bias_x;
  d.bias_dps[1] = vqf_live.bias_y;
  d.bias_dps[2] = vqf_live.bias_z;
  for(axis = 0U; axis < 3U; ++axis) norm += app_residual_dps[axis] * app_residual_dps[axis];
  d.residual_norm_dps = sqrtf(norm);
  d.zaru_hold = app_zaru.hold_active;
  d.zaru_enabled = (uint8_t)(APP_ZARU_ENABLE &&
      app_filter_profile == FUSION_PROFILE_ZARU &&
      app_fusion_mode == FUSION_MODE_6AXIS);
  {
    uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_MOTION_BIAS, seq, &d, sizeof(d));
    if(n) (void)protocol_send_frame(source, frame, n);
  }
}

static void protocol_reply_startup_bias(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_startup_bias_t d = app_startup_bias;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  float bias_rad[3];
  _Static_assert(sizeof(d) == 60U, "startup bias payload");
  vqf_get_gyr_bias(bias_rad);
  for(unsigned axis = 0U; axis < 3U; ++axis)
    d.current_bias_dps[axis] = bias_rad[axis] / DEG2RAD;
  uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_STARTUP_BIAS, seq, &d, sizeof(d));
  if(n) (void)protocol_send_frame(source, frame, n);
}

static void protocol_reply_fusion_diagnostic(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_fusion_diagnostic_t d;
  _Static_assert(sizeof(d)==60U,"fusion diagnostic payload");
  memset(&d,0,sizeof(d)); d.version=1; d.profile=app_filter_profile;
  d.rest=(uint8_t)vqf_live.rest_detected;
  d.mag_flags=(uint8_t)((vqf_get_mag_ready()?1U:0U)|(vqf_get_mag_dist_detected()?2U:0U));
  d.timestamp_ms=vqf_live.millis;
  d.raw_gyro_dps[0]=vqf_live.gx; d.raw_gyro_dps[1]=vqf_live.gy; d.raw_gyro_dps[2]=vqf_live.gz;
  d.bias_dps[0]=vqf_live.bias_x; d.bias_dps[1]=vqf_live.bias_y; d.bias_dps[2]=vqf_live.bias_z;
  memcpy(d.residual_dps,app_residual_dps,sizeof(d.residual_dps));
  d.raw_euler_deg[0]=vqf_live.roll; d.raw_euler_deg[1]=vqf_live.pitch; d.raw_euler_deg[2]=vqf_live.yaw;
  d.bias_sigma_dps=vqf_get_bias_sigma_dps();
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  uint16_t n=protocol_pack_frame(frame,sizeof(frame),AHRS_MSG_FUSION_DIAGNOSTIC,seq,&d,sizeof(d));
  if(n) protocol_send_frame(source,frame,n);
}

static void protocol_reply_acc_cal(protocol_source_t source, uint8_t seq)
{
  ahrs_payload_acc_cal_t status;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  _Static_assert(sizeof(status) == 60U, "six-face status payload");
  memset(&status, 0, sizeof(status)); status.version = 1U;
  status.enabled = APP_ACC_CAL_ENABLE; status.valid = acc_calibration_active.valid;
  memcpy(status.bias_g, acc_calibration_active.bias_g, sizeof(status.bias_g));
  memcpy(status.scale, acc_calibration_active.scale, sizeof(status.scale));
#if APP_ACC_CAL_ENABLE
  status.status = acc_cal_rt.status; status.phase = acc_cal_rt.phase;
  status.detected_face = acc_cal_rt.candidate; status.face_mask = acc_cal_rt.face_mask;
  status.progress_permille = acc_cal_rt.progress; status.error = acc_cal_rt.error;
  status.samples = acc_cal_rt.samples;
  status.elapsed_ms = acc_cal_rt.status ?
    (acc_cal_rt.active ? millis() : acc_cal_rt.end_ms)-acc_cal_rt.start_ms : 0U;
  if(acc_cal_rt.active) {
    uint32_t elapsed = millis()-acc_cal_rt.face_start_ms;
    uint32_t limit = (uint32_t)(APP_ACC_CAL_FACE_TIMEOUT_S*1000.0f);
    status.remaining_ms = elapsed < limit ? limit-elapsed : 0U;
  }
  memcpy(status.raw_g, acc_cal_rt.raw_g, sizeof(status.raw_g));
#endif
  uint16_t n = protocol_pack_frame(frame, sizeof(frame), AHRS_MSG_ACC_CAL_STATUS, seq, &status, sizeof(status));
  if(n) (void)protocol_send_frame(source, frame, n);
}

/* Keep the original four-byte maintenance commands for compatibility with
 * older scripts: AA 0C 01 0D = zero yaw, AA 00 00 0D = reset. */
static void legacy_command_feed(uint8_t byte, uint8_t buffer[4], uint8_t *index)
{
  if(*index == 0U)
  {
    if(byte == 0xAAU) buffer[(*index)++] = byte;
    return;
  }
  buffer[(*index)++] = byte;
  if(*index == 4U)
  {
    if(buffer[3] == 0x0DU)
    {
      if((buffer[1] == 0x0CU) && (buffer[2] == 0x01U))
        app_zero_yaw(vofa_pose_live.yaw);
      else if((buffer[1] == 0x00U) && (buffer[2] == 0x00U))
        app_request_reset(RESET_APPLICATION);
    }
    *index = 0U;
  }
}

static void protocol_vofa_received(void *user_data)
{
  (void)user_data;
  /* A VOFA terminal on either input switches both live outputs. Preserve
   * selected fields and saved flash settings, and keep text out of telemetry. */
  for(unsigned port = 0; port < 2; ++port) {
    app_outputs[port].format = OUTPUT_FORMAT_JUSTFLOAT;
    app_outputs[port].legacy_mode = STREAM_MODE_VOFA_3CH;
  }
}

static void protocol_frame_received(uint8_t msg_id, uint8_t seq,
                                    const uint8_t *payload, uint8_t len,
                                    void *user_data)
{
  const protocol_source_t source = (protocol_source_t)(uintptr_t)user_data;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  uint16_t frame_len;
  uint8_t status = AHRS_ACK_SUCCESS;
#ifdef APP_USER_BL_UPDATE
  if(msg_id==0x39U) {
    user_bl_info_t info;
    if(len) protocol_reply_ack(source,seq,msg_id,AHRS_ACK_INVALID_PARAM,0);
    else { user_bl_get_info(&info); frame_len=protocol_pack_frame(frame,sizeof(frame),0x3AU,seq,&info,sizeof(info)); (void)protocol_send_frame(source,frame,frame_len); }
    return;
  }
  if(msg_id==0x3BU) {
    /* Read-only bounded backup: area 0 BL (32 KiB), area 1 reserved (16 KiB). */
    if(len!=4 || payload[0]>1 || !payload[3] || payload[3]>56) status=AHRS_ACK_INVALID_PARAM;
    else {
      uint32_t offset=(uint32_t)payload[1]|((uint32_t)payload[2]<<8), size=payload[0] ? 0x4000U : 0x8000U;
      if(offset>size || payload[3]>size-offset) status=AHRS_ACK_INVALID_PARAM;
      else { uint8_t data[60];memcpy(data,payload,4);memcpy(data+4,(const void *)(uintptr_t)((payload[0] ? 0x0803C000U : 0x08000000U)+offset),payload[3]);
        frame_len=protocol_pack_frame(frame,sizeof(frame),0x3CU,seq,data,4+payload[3]);(void)protocol_send_frame(source,frame,frame_len);return; }
    }
    protocol_reply_ack(source,seq,msg_id,status,0);return;
  }
  if(msg_id==0x3DU) {
    uint32_t request[3]={0};if(len==sizeof(request))memcpy(request,payload,sizeof(request));
    if(len!=sizeof(request) || !user_bl_request_valid(request[0],request[1],request[2])) status=AHRS_ACK_INVALID_PARAM;
    else if(source!=PROTOCOL_SOURCE_USB || !app_settings_mode || user_bl_pending || protocol_reset_pending!=RESET_NONE) status=AHRS_ACK_EXEC_FAILED;
    if(protocol_reply_ack(source,seq,msg_id,status,0)==0 && status==AHRS_ACK_SUCCESS) {user_bl_pending=1;user_bl_requested_ms=millis();}
    return;
  }
#endif

  if(vqf_static_cal_active() &&
     (msg_id == AHRS_CMD_SET_FUSION_MODE || msg_id == AHRS_CMD_SET_OUTPUT_HZ ||
      msg_id == AHRS_CMD_SET_STARTUP_CONFIG || msg_id == AHRS_CMD_SET_FILTER ||
      msg_id == AHRS_CMD_SET_ZARU || msg_id == AHRS_CMD_RESTORE_ZARU ||
      msg_id == AHRS_CMD_RESTORE_VQF_DEFAULTS || msg_id == AHRS_CMD_START_ACC_6FACE_CAL)) {
    protocol_reply_ack(source, seq, msg_id, AHRS_ACK_EXEC_FAILED, AHRS_VQF_CAL_ACTIVE);
    return;
  }

  switch(msg_id)
  {
    case AHRS_CMD_PING:
      protocol_reply_ack(source, seq, msg_id, AHRS_ACK_SUCCESS, 0U);
      break;
    case AHRS_CMD_ZERO_YAW:
      if(len != 0U) status = AHRS_ACK_INVALID_PARAM;
      else app_zero_yaw(vofa_pose_live.yaw);
      protocol_reply_ack(source, seq, msg_id, status, 0U);
      break;
    case AHRS_CMD_RECALIBRATE_GYRO:
      /* Runtime calibration is not allowed to block the 2 kHz sensor loop. */
      protocol_reply_ack(source, seq, msg_id, AHRS_ACK_EXEC_FAILED, 1U);
      break;
    case AHRS_CMD_SET_STREAM_MODE:
      if((len != 1U) || (payload == NULL) ||
         (payload[0] > STREAM_MODE_VOFA_6CH))
      {
        status = AHRS_ACK_INVALID_PARAM;
      }
      else
      {
        app_stream_mode = (stream_mode_t)payload[0];
        for(unsigned port = 0; port < 2; ++port) {
          app_outputs[port].format = OUTPUT_FORMAT_LEGACY;
          app_outputs[port].legacy_mode = payload[0];
        }
      }
      protocol_reply_ack(source, seq, msg_id, status, (uint16_t)app_stream_mode);
      break;
    case AHRS_CMD_SET_OUTPUT_HZ:
      /* Save before applying; failed Flash writes keep the previous rate. */
      if((payload == NULL) || (len != 2U))
      {
        status = AHRS_ACK_INVALID_PARAM;
      }
      else
      {
        uint16_t hz = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
        if((hz == 0U) || (hz > APP_FUSION_HZ) || ((APP_FUSION_HZ % hz) != 0U))
        {
          status = AHRS_ACK_INVALID_PARAM;
        }
        else
        {
          device_settings_t saved = app_saved_settings;
          saved.output_hz = hz;
          if(app_save_device_settings(&saved) != 0) status = AHRS_ACK_EXEC_FAILED;
          else { app_output_hz = hz; app_output_div = (uint16_t)(APP_FUSION_HZ / hz); }
        }
      }
      protocol_reply_ack(source, seq, msg_id, status, app_output_hz);
      if(status == AHRS_ACK_SUCCESS) protocol_reply_config(source, seq);
      break;
    case AHRS_CMD_QUERY_STATUS:
      if(len != 0U)
      {
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
        break;
      }
      frame_len = protocol_pack_system_info(frame, sizeof(frame), seq, APP_FUSION_HZ,
                                             app_output_hz, app_sample_skips,
                                             imu_temp_live.temperature_c,
                                             (uint8_t)(app_outputs[PROTOCOL_PORT(source)].format == OUTPUT_FORMAT_LEGACY ? app_outputs[PROTOCOL_PORT(source)].legacy_mode : 0xFFU),
                                             (uint8_t)((APP_CAN_ENABLE != 0U) && (can_test_live.init_ok != 0U)));
      if(frame_len != 0U) protocol_send_frame(source, frame, frame_len);
      break;
    case AHRS_CMD_ENTER_SETTINGS:
      if(len != 0U) status = AHRS_ACK_INVALID_PARAM;
      else app_settings_mode = 1U;
      protocol_reply_ack(source, seq, msg_id, status, 0U);
      break;
    case AHRS_CMD_EXIT_SETTINGS:
#if APP_ACC_CAL_ENABLE
      acc_six_face_cancel(&acc_cal_rt);
#endif
      if(len != 0U) status = AHRS_ACK_INVALID_PARAM;
      else app_settings_mode = 0U;
      protocol_reply_ack(source, seq, msg_id, status, app_settings_dirty);
      break;
    case AHRS_CMD_SET_FUSION_MODE:
      /* Settings are deliberately staged in flash. The running VQF mode is
       * never changed in-place; reboot applies the selected mode. */
      if((app_settings_mode == 0U) || (len < 1U) || (len > 2U) ||
         (payload == NULL) || (payload[0] > FUSION_MODE_9AXIS_RELATIVE) ||
         ((len == 2U) && (payload[1] > 1U)) ||
         (!APP_MAG_FUSION_ENABLE && payload[0] != FUSION_MODE_6AXIS))
      {
        status = (app_settings_mode == 0U) ? AHRS_ACK_EXEC_FAILED : AHRS_ACK_INVALID_PARAM;
      }
      else if(app_save_settings((fusion_mode_t)payload[0],
                                can_test_get_node_id()) != 0)
      {
        status = AHRS_ACK_EXEC_FAILED;
      }
      else
      {
        app_settings_dirty = 1U;
      }
      /* Do not reboot if the success reply could not enter the TX queue. */
      if(protocol_reply_ack(source, seq, msg_id, status,
                            (payload != NULL && len != 0U) ? payload[0] : 0U) == 0 &&
         status == AHRS_ACK_SUCCESS && len == 2U && payload[1] != 0U)
        app_request_reset(RESET_APPLICATION);
      break;
    case AHRS_CMD_SET_CAN_NODE_ID:
      if((app_settings_mode == 0U) || (payload == NULL) || (len != 2U))
      {
        status = (app_settings_mode == 0U) ? AHRS_ACK_EXEC_FAILED : AHRS_ACK_INVALID_PARAM;
      }
      else
      {
        uint16_t node_id = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
        if((node_id > 0x7FFU) ||
           (app_save_settings(app_saved_settings.mode, node_id) != 0) ||
           (can_test_set_node_id(node_id) != 0))
        {
          status = (node_id > 0x7FFU) ? AHRS_ACK_INVALID_PARAM : AHRS_ACK_EXEC_FAILED;
        }
        else
        {
          app_settings_dirty = 1U;
        }
      }
      protocol_reply_ack(source, seq, msg_id, status,
                         (uint16_t)can_test_get_node_id());
      break;
    case AHRS_CMD_QUERY_CONFIG:
      if(len != 0U) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
      else protocol_reply_config(source, seq);
      break;
    case AHRS_CMD_QUERY_FIRMWARE_INFO:
      if(len != 0U) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
      else protocol_reply_firmware_info(source, seq);
      break;
    case AHRS_CMD_QUERY_DEVICE_MODEL:
      if(len != 0U) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
      else {
        frame_len = protocol_pack_device_model(frame, sizeof(frame), seq);
        if(frame_len != 0U) (void)protocol_send_frame(source, frame, frame_len);
      }
      break;
    case AHRS_CMD_QUERY_STARTUP_BIAS:
      if(len != 0U) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
      else protocol_reply_startup_bias(source, seq);
      break;
    case AHRS_CMD_QUERY_BIAS_HISTORY: {
      uint16_t offset = 0U;
      if(len == 2U)
      {
        if(payload == NULL)
        {
          protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
          break;
        }
        offset = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
      }
      else if(len != 0U)
      {
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
        break;
      }
      if(offset > GYRO_BIAS_HISTORY_MAX)
      {
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
        break;
      }
      protocol_reply_bias_history(source, seq, offset);
      break;
    }
    case AHRS_CMD_QUERY_FILTER:
      if(len) protocol_reply_ack(source,seq,msg_id,AHRS_ACK_INVALID_PARAM,0);
      else protocol_reply_filter(source,seq);
      break;
    case AHRS_CMD_SET_FILTER:
      if(!app_settings_mode) status=AHRS_ACK_EXEC_FAILED;
      else if(!payload || len!=2U || payload[0]>=FUSION_PROFILE_COUNT || payload[1]>1U)
        status=AHRS_ACK_INVALID_PARAM;
      else {
        device_settings_t saved=app_saved_settings;
        saved.filter_profile=payload[0];
        if(payload[1] && app_save_device_settings(&saved)) status=AHRS_ACK_EXEC_FAILED;
        else {
          app_filter_profile=payload[0];
          vqf_apply_profile(app_filter_profile);
          if(vqf_static_cal_source() == VQF_STATIC_CAL_SOURCE_CAL) {
            vqf_static_params_t cal;
            vqf_static_cal_get_applied(&cal);
            vqf_set_rest_thresholds(cal.rest_th_gyr_dps, cal.rest_th_acc_ms2);
          }
        }
      }
      protocol_reply_ack(source,seq,msg_id,status,app_filter_profile);
      protocol_reply_filter(source,seq);
      break;
    case AHRS_CMD_QUERY_FUSION_DIAGNOSTIC:
      if(len) protocol_reply_ack(source,seq,msg_id,AHRS_ACK_INVALID_PARAM,0);
      else protocol_reply_fusion_diagnostic(source,seq);
      break;
    case AHRS_CMD_QUERY_MOTION_BIAS:
      if(len) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0);
      else protocol_reply_motion_bias(source, seq);
      break;
    case AHRS_CMD_QUERY_ZARU:
      if(len) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0);
      else protocol_reply_zaru(source, seq);
      break;
    case AHRS_CMD_SET_ZARU:
      if(!APP_ZARU_ENABLE) status = AHRS_ACK_EXEC_FAILED;
      else if(!app_settings_mode) status = AHRS_ACK_EXEC_FAILED;
      else if(!payload || len != sizeof(ahrs_zaru_set_t)) status = AHRS_ACK_INVALID_PARAM;
      else {
        ahrs_zaru_set_t req;
        zaru_limits_t next;
        memcpy(&req, payload, sizeof(req));
        next.enter_dps = req.enter_dps;
        next.exit_dps = req.exit_dps;
        next.acc_dev_ms2 = req.acc_dev_ms2;
        next.enter_filter_ms = req.enter_filter_ms;
        next.enter_confirm_ms = req.enter_confirm_ms;
        next.exit_confirm_ms = req.exit_confirm_ms;
        next.reserved = 0U;
        if(req.persist > 1U || req.reserved != 0U || !zaru_limits_valid(&next))
          status = AHRS_ACK_INVALID_PARAM;
        else if(req.persist) {
          device_settings_t saved = app_saved_settings;
          saved.zaru = next;
          if(app_save_device_settings(&saved) != 0) status = AHRS_ACK_EXEC_FAILED;
          else app_zaru_limits = next;
        } else app_zaru_limits = next;
      }
      protocol_reply_ack(source, seq, msg_id, status, 0);
      protocol_reply_zaru(source, seq);
      break;
    case AHRS_CMD_RESTORE_ZARU:
      if(!APP_ZARU_ENABLE) status = AHRS_ACK_EXEC_FAILED;
      else if(!app_settings_mode) status = AHRS_ACK_EXEC_FAILED;
      else if(!payload || len != sizeof(ahrs_zaru_restore_t)) status = AHRS_ACK_INVALID_PARAM;
      else if(payload[0] > 1U || payload[1] != 0U) status = AHRS_ACK_INVALID_PARAM;
      else {
        zaru_limits_t next;
        zaru_limits_default(&next);
        if(payload[0]) {
          device_settings_t saved = app_saved_settings;
          saved.zaru = next;
          if(app_save_device_settings(&saved) != 0) status = AHRS_ACK_EXEC_FAILED;
          else app_zaru_limits = next;
        } else app_zaru_limits = next;
      }
      protocol_reply_ack(source, seq, msg_id, status, 0);
      protocol_reply_zaru(source, seq);
      break;
    case AHRS_CMD_QUERY_CAN_CONFIG:
      if(len) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0);
      else protocol_reply_can_config(source, seq);
      break;
    case AHRS_CMD_SET_CAN_CONFIG:
      if(!app_settings_mode) status = AHRS_ACK_EXEC_FAILED;
      else if(!payload || len != sizeof(can_config_t) + 1U || payload[10] > 1U)
        status = AHRS_ACK_INVALID_PARAM;
      else {
        can_config_t config, previous;
        memcpy(&config, payload, sizeof(config));
        can_test_get_config(&previous);
        if(!can_config_valid(&config)) status = AHRS_ACK_INVALID_PARAM;
        else {
          uint32_t start = dwt_cycles();
          if(can_test_set_config(&config)) status = AHRS_ACK_EXEC_FAILED;
          else if(payload[10] && can_test_save_config(&config)) {
            (void)can_test_set_config(&previous);
            status = AHRS_ACK_EXEC_FAILED;
          }
          /* Freeze-mode transitions and flash saves must not distort fusion dt. */
          app_flash_pause_end(start);
        }
      }
      protocol_reply_ack(source, seq, msg_id, status, 0);
      protocol_reply_can_config(source, seq);
      break;
    case AHRS_CMD_SET_STARTUP_CONFIG:
      if(!app_settings_mode || (len != 3U && len != 5U && len != 7U) || payload == NULL)
        status = app_settings_mode ? AHRS_ACK_INVALID_PARAM : AHRS_ACK_EXEC_FAILED;
      else if(payload[0] > FUSION_MODE_9AXIS_RELATIVE || payload[1] > 1U || payload[2] > 1U ||
              (!APP_MAG_FUSION_ENABLE && payload[0] != FUSION_MODE_6AXIS))
        status = AHRS_ACK_INVALID_PARAM;
      else {
        device_settings_t saved = app_saved_settings;
        saved.mode = (fusion_mode_t)payload[0]; saved.fast_start = payload[1];
        if(len >= 5U) saved.gyro_init_ms = (uint16_t)payload[3] | ((uint16_t)payload[4] << 8);
        if(len == 7U) saved.gyro_range_dps = (uint16_t)payload[5] | ((uint16_t)payload[6] << 8);
        saved.fast_start = saved.gyro_init_ms == 0U;
        if(saved.gyro_init_ms > APP_GYR_INIT_MAX_MS || !gyro_range_valid(saved.gyro_range_dps))
          status = AHRS_ACK_INVALID_PARAM;
        else if(app_save_device_settings(&saved) != 0) status = AHRS_ACK_EXEC_FAILED;
        else app_settings_dirty = (saved.mode != app_fusion_mode || saved.fast_start != app_fast_start || saved.gyro_init_ms != app_gyro_init_ms || saved.gyro_range_dps != app_gyro_range_dps);
      }
      if(protocol_reply_ack(source, seq, msg_id, status, (uint16_t)app_saved_settings.mode) == 0 &&
         status == AHRS_ACK_SUCCESS && payload[2]) app_request_reset(RESET_APPLICATION);
      if(status == AHRS_ACK_SUCCESS && !payload[2]) protocol_reply_config(source, seq);
      break;
    case AHRS_CMD_SET_OUTPUT_CONFIG:
      if(len != 5U || payload == NULL || payload[0] > 1U || payload[1] > OUTPUT_FORMAT_CUSTOM || payload[4] > 1U)
        status = AHRS_ACK_INVALID_PARAM;
      else {
        output_config_t output = {payload[1], 0U, (uint16_t)(payload[2] | ((uint16_t)payload[3] << 8))};
        if(!protocol_output_config_valid(&output)) status = AHRS_ACK_INVALID_PARAM;
        else {
          device_settings_t saved = app_saved_settings;
          saved.outputs[payload[0]] = output;
          if(payload[4] && app_save_device_settings(&saved) != 0) status = AHRS_ACK_EXEC_FAILED;
          else app_outputs[payload[0]] = output;
        }
      }
      protocol_reply_ack(source, seq, msg_id, status, (len == 5U && payload) ? payload[0] : 0U);
      if(status == AHRS_ACK_SUCCESS) protocol_reply_config(source, seq);
      break;
    case AHRS_CMD_START_GYRO_CAL_60S:
      /* Runtime calibration is not yet a blocking operation. Reuse the
       * existing safe command response until its non-blocking state machine
       * is enabled, instead of silently pretending that it completed. */
      protocol_reply_ack(source, seq, msg_id, AHRS_ACK_EXEC_FAILED, 0x0601U);
      break;
    case AHRS_CMD_QUERY_ACC_CAL:
      if(len != 0U) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
      else protocol_reply_acc_cal(source, seq);
      break;
    case AHRS_CMD_CANCEL_ACC_CAL:
      if(len != 0U) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
      else {
#if APP_ACC_CAL_ENABLE
        acc_six_face_cancel(&acc_cal_rt);
#endif
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_SUCCESS, 0U);
        protocol_reply_acc_cal(source, seq);
      }
      break;
    case AHRS_CMD_START_ACC_6FACE_CAL:
#if !APP_ACC_CAL_ENABLE
      (void)payload;
      (void)len;
      protocol_reply_ack(source, seq, msg_id, AHRS_ACK_EXEC_FAILED, 0x0600U);
#else
      if((app_settings_mode == 0U) || (len != 0U))
      {
        status = (app_settings_mode == 0U) ? AHRS_ACK_EXEC_FAILED : AHRS_ACK_INVALID_PARAM;
        protocol_reply_ack(source, seq, msg_id, status, 0x0602U);
      }
      else if(acc_cal_rt.active != 0U)
      {
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_EXEC_FAILED, 0x0604U);
      }
      else
      {
        acc_six_face_start(&acc_cal_rt, millis());
        acc_cal_source = (uint8_t)source; acc_cal_command_seq = seq; acc_cal_final_sent = 0U;
        acc_cal_report_ms = millis()-200U;
        acc_cal_status_live.status = ACC_CAL_STATUS_RUNNING;
        acc_cal_status_live.face = 1U; acc_cal_status_live.face_mask = 0U;
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_SUCCESS, 0x0100U);
      }
#endif
      break;
    case AHRS_CMD_SYSTEM_RESET:
    case AHRS_CMD_ENTER_BOOTLOADER:
      if(len != 0U)
      {
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
      }
      else
      {
        if(protocol_reply_ack(source, seq, msg_id, AHRS_ACK_SUCCESS, 0U) == 0)
          app_request_reset((msg_id == AHRS_CMD_ENTER_BOOTLOADER) ?
                             RESET_BOOTLOADER : RESET_APPLICATION);
      }
      break;
    case AHRS_CMD_QUERY_VQF_CAL:
      if(len) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0);
      else protocol_reply_vqf_cal(source, seq);
      break;
    case AHRS_CMD_QUERY_VQF_SETTINGS:
      if(len) protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0);
      else protocol_reply_vqf_settings(source, seq);
      break;
    case AHRS_CMD_START_VQF_CAL: {
      uint16_t detail = 0U;
      int busy = vqf_static_cal_active();
#if APP_ACC_CAL_ENABLE
      busy = busy || acc_cal_rt.active;
#endif
      if(len != 0U) status = AHRS_ACK_INVALID_PARAM;
      else if(!app_settings_mode) status = AHRS_ACK_EXEC_FAILED;
      else if(!app_fusion_ready) {
        status = AHRS_ACK_EXEC_FAILED;
        detail = AHRS_VQF_CAL_NOT_READY;
      } else if(busy || vqf_static_cal_start(millis()) != 0) {
        status = AHRS_ACK_EXEC_FAILED;
        detail = AHRS_VQF_CAL_BUSY;
      } else {
        if(!app_vqf_pre_cal_bias_valid) {
          vqf_get_gyr_bias(app_vqf_pre_cal_bias_rad);
          app_vqf_pre_cal_bias_valid = 1U;
        }
        app_vqf_cal_port = source;
        app_vqf_cal_report_ms = millis();
      }
      protocol_reply_ack(source, seq, msg_id, status, detail);
      if(status == AHRS_ACK_SUCCESS) protocol_reply_vqf_cal(source, seq);
      break;
    }
    case AHRS_CMD_CANCEL_VQF_CAL:
      if(len) status = AHRS_ACK_INVALID_PARAM;
      else vqf_static_cal_cancel();
      protocol_reply_ack(source, seq, msg_id, status, 0);
      if(status == AHRS_ACK_SUCCESS) protocol_reply_vqf_cal(source, seq);
      break;
    case AHRS_CMD_RESTORE_VQF_DEFAULTS:
      if(len) status = AHRS_ACK_INVALID_PARAM;
      else if(!app_settings_mode) status = AHRS_ACK_EXEC_FAILED;
      else {
        uint32_t start = dwt_cycles();
        int restored = vqf_static_cal_restore_defaults();
        if(restored == 0 || restored == -2) app_flash_pause_end(start);
        if(restored != 0) status = AHRS_ACK_EXEC_FAILED;
        else {
          vqf_static_params_t defaults;
          vqf_static_cal_defaults(&defaults);
          vqf_set_rest_thresholds(defaults.rest_th_gyr_dps, defaults.rest_th_acc_ms2);
          vqf_set_bias_sigmas(defaults.bias_sigma_init_dps, defaults.bias_sigma_rest_dps);
          if(app_vqf_pre_cal_bias_valid)
            vqf_seed_gyr_bias(app_vqf_pre_cal_bias_rad, defaults.bias_sigma_init_dps);
        }
      }
      protocol_reply_ack(source, seq, msg_id, status, 0);
      protocol_reply_vqf_settings(source, seq);
      break;
    default:
      protocol_reply_ack(source, seq, msg_id, AHRS_ACK_UNKNOWN_CMD, 0U);
      break;
  }
}


#if APP_ACC_CAL_ENABLE
static void acc_calibration_sample_task(uint32_t now_ms, const float raw_g[3], const float gyro_dps[3])
{
  (void)acc_six_face_push(&acc_cal_rt, now_ms, raw_g, gyro_dps);
}

static void acc_calibration_service_task(uint32_t now_ms)
{
  unsigned i;
  acc_six_face_tick(&acc_cal_rt, now_ms);
  if(acc_cal_rt.active && acc_cal_rt.phase == ACC_CAL_PHASE_SAVING)
    acc_six_face_saved(&acc_cal_rt, app_save_acc_calibration(&acc_cal_rt.result));
  /* Keep the existing DAP status ABI, now with coherent odd/even sequence. */
  acc_cal_status_live.seq++; __DMB();
  acc_cal_status_live.millis = now_ms; acc_cal_status_live.status = acc_cal_rt.status;
  acc_cal_status_live.face = acc_cal_rt.face < 6U ? acc_cal_rt.face+1U : 6U;
  acc_cal_status_live.detected_face = acc_cal_rt.candidate;
  acc_cal_status_live.face_mask = acc_cal_rt.face_mask;
  acc_cal_status_live.sample_count = (uint16_t)(acc_cal_rt.samples > 65535U ? 65535U : acc_cal_rt.samples);
  acc_cal_status_live.reserved = acc_cal_rt.error;
  for(i=0; i<3; i++) {
    acc_cal_status_live.bias_g[i] = acc_calibration_active.bias_g[i];
    acc_cal_status_live.scale[i] = acc_calibration_active.scale[i];
  }
  __DMB(); acc_cal_status_live.seq++;
  if(acc_cal_rt.status && acc_cal_rt.status != ACC_CAL_STATUS_RUNNING && !acc_cal_final_sent) {
    acc_cal_final_sent = 1U;
    protocol_reply_ack((protocol_source_t)acc_cal_source, acc_cal_command_seq,
      AHRS_CMD_START_ACC_6FACE_CAL, acc_cal_rt.status == ACC_CAL_STATUS_DONE ? AHRS_ACK_SUCCESS : AHRS_ACK_EXEC_FAILED,
      acc_cal_rt.error);
    protocol_reply_acc_cal((protocol_source_t)acc_cal_source, acc_cal_command_seq);
  } else if(acc_cal_rt.active && (uint32_t)(now_ms-acc_cal_report_ms) >= 200U) {
    acc_cal_report_ms = now_ms;
    protocol_reply_acc_cal((protocol_source_t)acc_cal_source, acc_cal_command_seq);
  }
}
#endif /* APP_ACC_CAL_ENABLE */

static float temp_lpf_c;
static float temp_lpf_alpha;
static uint8_t temp_lpf_init;
static uint8_t temp_lpf_alpha_init;

/* LSM6DSV temperature is specified as 25 degC + raw/256.  The raw value is
 * kept in imu_temp_live; a slow LPF is used only for the optional gyro
 * temperature compensation so temperature ADC noise is not turned into gyro
 * noise. */
static float update_temperature(int16_t raw_temp)
{
  const float raw_c = APP_GYR_TEMP_REF_C + ((float)raw_temp / 256.0f);

  if(!temp_lpf_alpha_init)
  {
    if(APP_GYR_TEMP_LPF_HZ > 0.0f)
      temp_lpf_alpha = 1.0f - expf(-6.28318530718f *
                                    APP_GYR_TEMP_LPF_HZ / FUSION_HZ);
    else
      temp_lpf_alpha = 1.0f;
    if(temp_lpf_alpha < 0.0f) temp_lpf_alpha = 0.0f;
    if(temp_lpf_alpha > 1.0f) temp_lpf_alpha = 1.0f;
    temp_lpf_alpha_init = 1U;
  }

  if(!temp_lpf_init)
  {
    temp_lpf_c = raw_c;
    temp_lpf_init = 1U;
  }
  else
  {
    temp_lpf_c += temp_lpf_alpha * (raw_c - temp_lpf_c);
  }

  imu_temp_live.seq++;
  __DMB();
  imu_temp_live.raw_temp = raw_temp;
  imu_temp_live.temperature_c = raw_c;
  imu_temp_live.millis = millis();
  __DMB();
  imu_temp_live.seq++;
  return temp_lpf_c;
}

static float gyro_dps_from_raw(unsigned axis, int16_t raw, float temp_c)
{
  float dps = (float)raw * GYR_DPS_PER_LSB;

#if APP_GYR_TEMP_COMP_ENABLE
  float coeff = 0.0f;
  if(axis == 0U) coeff = APP_GYR_TEMP_COEFF_X_DPS_PER_C;
  else if(axis == 1U) coeff = APP_GYR_TEMP_COEFF_Y_DPS_PER_C;
  else if(axis == 2U) coeff = APP_GYR_TEMP_COEFF_Z_DPS_PER_C;
  dps -= coeff * (temp_c - APP_GYR_TEMP_REF_C);
#else
  (void)axis;
  (void)temp_c;
#endif
  return dps;
}

static void app_update_attitude(void)
{
  static yaw_reference_t relative_yaw_reference;
  static uint32_t last_cy;
  const float nominal_dt = 1.0f / ATTITUDE_FILTER_HZ;
  float dt = nominal_dt, q6[4], target[4], measured_yaw=vqf_live.yaw;
  uint32_t now=dwt_cycles();
  if(last_cy && system_core_clock) {
    float elapsed=(float)(now-last_cy)/(float)system_core_clock;
    if(elapsed>=nominal_dt*.5f && elapsed<=nominal_dt*3) dt=elapsed;
  }
  last_cy=now;
  if(app_relative_yaw_enabled)
    measured_yaw=yaw_reference_apply(&relative_yaw_reference,measured_yaw,
                                    vqf_get_mag_delta_deg(),(uint8_t)vqf_get_mag_ready());
  attitude_from_euler(vqf_live.roll,vqf_live.pitch,measured_yaw,target);
  vqf_get_quat6d(q6);
  float rate=sqrtf(app_residual_dps[0]*app_residual_dps[0]+
                   app_residual_dps[1]*app_residual_dps[1]+
                   app_residual_dps[2]*app_residual_dps[2]);
  attitude_output_update(&app_attitude,q6,target,(uint8_t)vqf_live.rest_detected,
                         rate,app_filter_profile,dt);
  float output_roll, output_pitch, output_yaw;
  attitude_to_euler(app_attitude.q,&output_roll,&output_pitch,&output_yaw);
  /* ZARU locks published yaw only. Roll, pitch, and vqf_live.yaw stay raw.
   * Manual zero is applied to the corrected heading so a zero during hold
   * remains zero until the device actually rotates. VQF rest is not a gate. */
  {
    float raw_output_yaw = output_yaw;
    uint8_t zaru_on = (uint8_t)(APP_ZARU_ENABLE &&
        app_filter_profile == FUSION_PROFILE_ZARU &&
        app_fusion_mode == FUSION_MODE_6AXIS);
    float corrected_yaw = zaru_heading_hold_update(&app_zaru, raw_output_yaw,
        app_zaru_rate_fast_dps, app_zaru_acc_dev_ms2, dt, zaru_on, &app_zaru_limits);
    output_yaw = wrap_deg(corrected_yaw - app_yaw_offset);
  }

  vofa_pose_live.seq++;
  __DMB();
  vofa_pose_live.yaw = output_yaw;
  vofa_pose_live.pitch = output_pitch;
  vofa_pose_live.roll = output_roll;
  vofa_pose_live.temperature_c = imu_temp_live.temperature_c;
  vofa_pose_live.millis = millis();
  __DMB();
  vofa_pose_live.seq++;

  attitude_from_euler(output_roll,output_pitch,output_yaw,app_output_q);
  /* Legacy diagnostic ABI remains readable; kf_yaw now names filtered yaw. */
  yaw_kf_sync_live.seq++; __DMB();
  yaw_kf_sync_live.millis=vofa_pose_live.millis;
  yaw_kf_sync_live.vqf_yaw=vqf_live.yaw;
  yaw_kf_sync_live.kf_yaw=output_yaw;
  yaw_kf_sync_live.gz=vqf_live.gz;
  yaw_kf_sync_live.bias_z=vqf_live.bias_z;
  yaw_kf_sync_live.rest_detected=vqf_live.rest_detected;
  yaw_kf_sync_live.mag_updates=vqf_live.mag_updates;
  yaw_kf_sync_live.temperature_c=imu_temp_live.temperature_c;
  yaw_kf_sync_live.gyr_lpf_z=vqf_live.gyr_lpf_z;
  yaw_kf_sync_live.corrected_z=vqf_live.corrected_z;
  __DMB(); yaw_kf_sync_live.seq++;
#if APP_CAN_ENABLE
  can_test_update_data(output_roll,output_pitch,output_yaw,
                       vqf_live.gx,vqf_live.gy,vqf_live.gz,
                       vqf_live.ax,vqf_live.ay,vqf_live.az,
                       app_output_q[0],app_output_q[1],app_output_q[2],app_output_q[3],
                       imu_temp_live.temperature_c,(uint8_t)vqf_live.rest_detected);
#endif
}

static void vofa_send_justfloat(void)
{
  float output_yaw=vofa_pose_live.yaw, output_pitch=vofa_pose_live.pitch, output_roll=vofa_pose_live.roll;
  /* Independent UART DMA storage and USB copied ring storage. Never reuse an
   * in-flight UART slot for USB packing or a failed UART DMA submission. */
  {
    const float fields[AHRS_FIELD_COUNT] = {
      output_yaw, output_pitch, output_roll,
      vqf_live.ax * G_TO_MS2, vqf_live.ay * G_TO_MS2, vqf_live.az * G_TO_MS2,
      vqf_live.gx, vqf_live.gy, vqf_live.gz
    };
    const uint8_t flags = vqf_live.rest_detected ? AHRS_FLAG_REST_DETECTED : 0U;
    for(unsigned source = 0U; source < 2U; ++source) {
      float legacy_fields[AHRS_FIELD_COUNT];
      const float *port_fields = fields;
      if(app_outputs[source].format == OUTPUT_FORMAT_LEGACY) {
        /* Preserve the historical presets' acceleration in g. The new
         * selected-channel protocols consistently use m/s^2. */
        memcpy(legacy_fields, fields, sizeof(fields));
        legacy_fields[3] = vqf_live.ax; legacy_fields[4] = vqf_live.ay; legacy_fields[5] = vqf_live.az;
        port_fields = legacy_fields;
      }
      uint8_t *tx = source == PROTOCOL_SOURCE_UART ? vofa_dma[vofa_sel] : usb_telemetry;
      uint16_t n = protocol_pack_output(tx, VOFA_MAX_BYTES, app_stream_seq[source]++,
                                       &app_outputs[source], port_fields,
                                       imu_temp_live.temperature_c, flags, (uint16_t)millis());
      if(n == 0U) continue;
      if(source == PROTOCOL_SOURCE_UART) {
        if(uart_dma_send(tx, n) == 0) vofa_sel ^= 1U;
        else vofa_late++;
      } else {
        (void)usb_cdc_write(tx, n);
        (void)webusb_write(tx, n);
      }
    }
  }


}

static void live_init(void)
{
  vqf_live.magic = VQF_LIVE_MAGIC;
  vqf_live.seq = 0;
  vqf_live.init_err = 0;
  vqf_live.whoami = 0;
  vqf_live.clk_hz = system_core_clock;
  vqf_live.millis = 0;
  vqf_live.fusion_hz = 0;
  vqf_live.out_hz = 0;
  vqf_live.fusion_n = 0;
  vqf_live.skip_n = 0;
  vqf_live.vqf_us = 0;
  vqf_live.bias_x = 0.0f;
  vqf_live.bias_y = 0.0f;
  vqf_live.bias_z = 0.0f;
  vqf_live.rest_time = 0.0f;
  vqf_live.tau_acc = 0.0f;
  vqf_live.rest_detected = 0U;
  vqf_live.mag_err = -1;
  vqf_live.mag_addr = 0U;
  vqf_live.mag_updates = 0U;
  vqf_live.mag_ready = 0U;
  vqf_live.temperature_c = APP_GYR_TEMP_REF_C;
  vqf_live.gyr_lpf_z = 0.0f;
  vqf_live.corrected_z = 0.0f;

  memset((void *)&vqf_tune_live, 0, sizeof(vqf_tune_live));
  vqf_tune_live.magic = VQF_TUNE_MAGIC;
}

static void live_whoami(void)
{
  uint8_t who = 0xFFU;
  (void)lsm6dsv_read_reg(LSM6DSV_WHO_AM_I_REG, &who);
  vqf_live.whoami = who;
}

/* Keep the application observable and recoverable when the IMU is absent or
 * SPI initialization fails. The old implementation stopped forever here,
 * leaving the last SRAM telemetry snapshot looking like a valid zero-drift
 * result and preventing a later sensor reconnection from recovering. */
static int sensor_init_retry_loop(int initial_err)
{
  uint32_t next_retry_ms = 0U;
  uint32_t last_led_ms = 0U;
  int err = initial_err;

  vqf_live.init_err = err;
  for(;;)
  {
    const uint32_t now = millis();
    live_whoami();
    vqf_live.init_err = err;
    vqf_live.millis = now;
    vqf_live.fusion_hz = 0U;
    vqf_live.out_hz = 0U;
    vqf_live.seq++;
    /* Publish a changing sequence in the exact live block used by DAPLink
     * tools, so stale SRAM is never mistaken for a valid zero-drift record. */
    vqf_tune_live.millis = now;
    vqf_tune_live.fusion_hz = 0U;
    vqf_tune_live.seq++;

    /* Keep the host link serviced so the failure remains diagnosable. */
    usb_cdc_task();
    if((uint32_t)(now - last_led_ms) >= 200U)
    {
      last_led_ms = now;
      ws2812_show_error(1U);
      led_toggle();
    }

    if((int32_t)(now - next_retry_ms) >= 0)
    {
      (void)lsm6dsv_spi_recover();
      err = lsm6dsv_init_2khz(app_gyro_range_dps);
      live_whoami();
      if(err == 0)
      {
        vqf_live.init_err = 0;
        return 0;
      }
      next_retry_ms = millis() + 500U;
    }
    delay_ms(10U);
  }
}
static void app_perform_reset(reset_request_t request)
{
  if(request == RESET_BOOTLOADER)
  {
    *((volatile uint32_t *)APP_BOOT_REQUEST_ADDR) = APP_BOOT_REQUEST_MAGIC;
    __DMB();
  }
  nvic_system_reset();
}

static void app_service_commands(void)
{
  uint8_t ch;
  uint16_t budget = 128U;
  static uint8_t uart_legacy_buf[4], uart_legacy_idx;
  static uint8_t usb_legacy_buf[4], usb_legacy_idx;
  static uint8_t webusb_legacy_buf[4], webusb_legacy_idx;
  usb_cdc_task();
  while(budget-- && uart_read_byte(&ch)) {
    protocol_parser_feed_byte(&uart_protocol_parser, ch);
    legacy_command_feed(ch, uart_legacy_buf, &uart_legacy_idx);
  }
  budget = 64U;
  while(budget-- && usb_cdc_read_byte(&ch)) {
    protocol_parser_feed_byte(&usb_protocol_parser, ch);
    legacy_command_feed(ch, usb_legacy_buf, &usb_legacy_idx);
  }
  budget = 64U;
  while(budget-- && webusb_read_byte(&ch)) {
    protocol_parser_feed_byte(&webusb_protocol_parser, ch);
    legacy_command_feed(ch, webusb_legacy_buf, &webusb_legacy_idx);
  }
  uart_tx_task();
#ifdef APP_USER_BL_UPDATE
  if(user_bl_pending && ((uart_tx_idle() && usb_cdc_tx_idle() && webusb_tx_idle()) ||
     (uint32_t)(millis()-user_bl_requested_ms)>=100U)) {
    user_bl_pending=0;user_bl_apply();
  }
#endif
  if(protocol_reset_pending != RESET_NONE &&
     ((uart_tx_idle() && usb_cdc_tx_idle() && webusb_tx_idle()) || (uint32_t)(millis() - reset_requested_ms) >= 100U))
    app_perform_reset(protocol_reset_pending);
}

int main(void)
{
  boot_startup_handoff_t startup_handoff;
  int startup_handoff_present=boot_startup_take(
      (volatile boot_startup_handoff_t *)BOOT_STARTUP_ADDR,&startup_handoff);
  SCB->VTOR = 0x08008000U;
  lsm6dsv_raw_t raw;
  float gyr[3];
  float acc[3];
  float mag[3] = {0.0f, 0.0f, 0.0f};
  float q[4];
  float roll;
  float pitch;
  float yaw;
  uint32_t fusion_n = 0; /* monotonic sample sequence; never reset each second */
  uint32_t window_samples = 0;
  uint32_t out_n = 0;
  uint32_t last_ms;
  uint32_t fusion_hz = 0;
  int err;
  int mag_err;
  uint8_t mag_ok;
  uint8_t mag_pending;
  uint32_t mag_start_n;
  uint32_t mag_updates;
  uint32_t mag_read_n;
  uint32_t t0;
  uint32_t vqf_us;
  uint32_t i;
  float temp_c = APP_GYR_TEMP_REF_C;
  uint8_t bias_fallback = 0U;
  uint8_t bias_no_history = 0U;
  uint8_t bias_history_write_error = 0U;
  uint8_t quick_bias_active = 0U;
  uint32_t quick_rest_ms = 0U;
  uint32_t quick_save_elapsed_ms = 0U;
  float quick_bias[3] = {0.0f, 0.0f, 0.0f};
  float quick_bias_initial[3] = {0.0f, 0.0f, 0.0f};
  float quick_bias_temp_c = APP_GYR_TEMP_REF_C;
#if APP_SFLP_BIAS_ENABLE
  float sflp_bias_dps[3] = {0.0f, 0.0f, 0.0f};
  uint8_t sflp_bias_ok = 0U;
#endif
  float gyr_lpf[3] = {0.0f, 0.0f, 0.0f};
  uint8_t gyr_lpf_init = 0U;
  const float gyr_lpf_alpha = 1.0f - expf(-6.28318530718f * GYR_LPF_CUTOFF_HZ / FUSION_HZ);

  system_clock_config();
  bsp_init();
  ws2812_init();
  live_init();
  lsm6dsv_spi_init();
#if APP_ACC_CAL_ENABLE
  acc_calibration_load();
#endif
#if APP_CAN_ENABLE
  can_test_init();
#endif
  usb_cdc_init();
  protocol_parser_init(&uart_protocol_parser, protocol_frame_received,
                       (void *)(uintptr_t)PROTOCOL_SOURCE_UART);
  protocol_parser_init(&usb_protocol_parser, protocol_frame_received,
                       (void *)(uintptr_t)PROTOCOL_SOURCE_USB);
  protocol_parser_set_vofa_callback(&uart_protocol_parser, protocol_vofa_received);
  protocol_parser_set_vofa_callback(&usb_protocol_parser, protocol_vofa_received);
  protocol_parser_init(&webusb_protocol_parser, protocol_frame_received,
                       (void *)(uintptr_t)PROTOCOL_SOURCE_WEBUSB);
  protocol_parser_set_vofa_callback(&webusb_protocol_parser, protocol_vofa_received);
  {
    (void)device_settings_load(&app_saved_settings);
    app_fusion_mode = app_saved_settings.mode;
    /* Version 4 uses duration as the single startup control. */
    app_saved_settings.fast_start = app_saved_settings.gyro_init_ms == 0U;
    app_fast_start = app_saved_settings.fast_start;
    app_filter_profile = app_saved_settings.filter_profile;
    app_zaru_limits = app_saved_settings.zaru;
    app_gyro_init_ms = app_saved_settings.gyro_init_ms;
    app_gyro_range_dps = app_saved_settings.gyro_range_dps;
    app_output_hz = app_saved_settings.output_hz;
    app_output_div = (uint16_t)(APP_FUSION_HZ / app_output_hz);
    gyro_startup_live.duration_ms = app_gyro_init_ms;
    memcpy(app_outputs, app_saved_settings.outputs, sizeof(app_outputs));
#if !APP_MAG_FUSION_ENABLE
    /* A six-axis image must not inherit a previously stored nine-axis mode. */
    app_fusion_mode = FUSION_MODE_6AXIS;
#endif
    (void)can_test_set_config(&app_saved_settings.can);
  }
  app_relative_yaw_enabled = (app_fusion_mode == FUSION_MODE_9AXIS_RELATIVE) ? 1U : 0U;
  delay_ms(20);

#if APP_SFLP_BIAS_ENABLE
  if(lsm6dsv_read_sflp_gbias(sflp_bias_dps, APP_SFLP_BIAS_SETTLE_MS) == 0)
  {
    sflp_bias_ok = 1U;
  }
#endif
  live_whoami();
  err = lsm6dsv_init_2khz(app_gyro_range_dps);
  live_whoami();
  if(err != 0)
  {
    (void)sensor_init_retry_loop(err);
  }

  mag_err = ist8310_init();
  mag_ok = ((mag_err == 0) && (app_fusion_mode != FUSION_MODE_6AXIS)) ? 1U : 0U;
  mag_pending = 0U;
  mag_start_n = 0U;
  mag_updates = 0U;
  mag_read_n = 0U;
  vqf_live.mag_err = mag_err;
  vqf_live.mag_addr = ist8310_get_addr();

  vqf_apply_profile(app_filter_profile);
  vqf_init(1.0f / FUSION_HZ, 1.0f / FUSION_HZ);
  zaru_heading_hold_reset(&app_zaru);
  {
    float startup_acc[3] = {0.0f, 0.0f, G_TO_MS2};
    float gyr_bias[3] = {0.0f, 0.0f, 0.0f};
    float acc_avg[3] = {0.0f, 0.0f, G_TO_MS2};
    lsm6dsv_raw_t startup_raw;
    uint8_t seed_history_sigma = 0U;
    vqf_static_params_t boot_cal;

    if(vqf_static_cal_load(&boot_cal) == 0) {
      vqf_set_rest_thresholds(boot_cal.rest_th_gyr_dps, boot_cal.rest_th_acc_ms2);
      vqf_set_bias_sigmas(boot_cal.bias_sigma_init_dps, boot_cal.bias_sigma_rest_dps);
    }

    /* Gravity and temperature for fallback; no second startup window. */
    (void)lsm6dsv_wait_sample(2000U);
    if(lsm6dsv_read_raw(&startup_raw) == 0)
    {
      temp_c = update_temperature(startup_raw.temp_raw);
      for(i = 0U; i < 3U; ++i)
      {
#if APP_ACC_CAL_ENABLE
        startup_acc[i] = acc_calibrate_g(i, (float)startup_raw.acc[i] * ACC_G_PER_LSB) * G_TO_MS2;
#else
        startup_acc[i] = (float)startup_raw.acc[i] * ACC_G_PER_LSB * G_TO_MS2;
#endif
      }
    }
    int valid_handoff = startup_handoff_present &&
        boot_startup_matches(&startup_handoff, app_gyro_init_ms, app_gyro_range_dps);
    app_startup_bias.version = 1U;
    app_startup_bias.duration_ms = app_gyro_init_ms;
    app_startup_bias.range_dps = app_gyro_range_dps;
    app_startup_bias.boot_temperature_c = temp_c;
    app_startup_bias.elapsed_ms = valid_handoff ? startup_handoff.elapsed_ms : 0U;
    app_startup_bias.samples = valid_handoff ? startup_handoff.samples : 0U;
    app_startup_bias.rejected_windows = valid_handoff ? startup_handoff.rejected_windows : 0U;
    app_startup_bias.rejection_reason = valid_handoff ? startup_handoff.rejection_reason : 6U;
    gyro_startup_live.elapsed_ms = valid_handoff ? startup_handoff.elapsed_ms : 0U;
    gyro_startup_live.samples = valid_handoff ? startup_handoff.samples : 0U;
    if(valid_handoff && startup_handoff.status == BOOT_STARTUP_FRESH) {
      temp_c = startup_handoff.temperature_c;
      for(i = 0U; i < 3U; ++i) {
        gyr_bias[i] = startup_handoff.bias_rad_s[i];
        acc_avg[i] = startup_handoff.gravity_ms2[i];
        gyro_startup_live.bias_dps[i] = gyr_bias[i] / DEG2RAD;
      }
      gyro_startup_live.status = 2U;
      app_startup_bias.source = 1U;
      app_startup_bias.selected_temperature_c = temp_c;
      if(app_save_gyro_bias(gyr_bias, temp_c) != 0) bias_history_write_error = 1U;
    } else {
      /* T=0, motion, missing/corrupt mailbox, or incomplete sampling:
       * immediately select history; only fresh results are saved above. */
      if(app_select_temp_matched_bias(quick_bias, &quick_bias_temp_c, temp_c) == 0) {
        bias_no_history = 1U;
        bias_fallback = 1U;
      }
      for(i = 0U; i < 3U; ++i) {
        quick_bias_initial[i] = quick_bias[i];
        gyr_bias[i] = quick_bias[i];
        acc_avg[i] = startup_acc[i];
        gyro_startup_live.bias_dps[i] = gyr_bias[i] / DEG2RAD;
      }
      quick_bias_active = 1U;
      seed_history_sigma = 1U;
      gyro_startup_live.status = 3U;
      app_startup_bias.source = bias_no_history ? 3U : 2U;
      app_startup_bias.selected_temperature_c = quick_bias_temp_c;
    }
    memcpy(app_startup_bias.initial_bias_dps, gyro_startup_live.bias_dps,
           sizeof(app_startup_bias.initial_bias_dps));
    vqf_prime_rest(acc_avg, gyr_bias);
    if(seed_history_sigma) vqf_seed_gyr_bias(gyr_bias, bias_fallback ? .50f : .25f);
  }
  vqf_live.seq = 2;
  last_ms = millis();
  /* In 6-axis mode a missing/disabled magnetometer is expected; do not
   * replace the normal status LED with a permanent amber error. */
  if((!mag_ok) && (app_fusion_mode != FUSION_MODE_6AXIS))
    ws2812_show_error(2U);

  {
    uint32_t last_sample_cy = dwt_cycles();
    uint32_t err_streak = 0U;

    app_fusion_ready = 1U;
  while(1)
  {
    uint32_t now_cy;
    uint32_t dt_cy;

    if(lsm6dsv_wait_sample(2000U) != 0)
    {
      app_sample_skips++;
      err_streak++;
      goto recover_or_continue;
    }
    if(lsm6dsv_read_raw(&raw) != 0)
    {
      app_sample_skips++;
      err_streak++;
      goto recover_or_continue;
    }

    /* Temperature is part of the same 14-byte SPI burst as gyro/accel.
     * temp_c is the filtered value used by the optional gyro compensation;
     * imu_temp_live.temperature_c remains the raw converted telemetry value. */
    temp_c = update_temperature(raw.temp_raw);

    now_cy = dwt_cycles();
    dt_cy = now_cy - last_sample_cy;
    last_sample_cy = now_cy;
    if((fusion_n > 0U) && (dt_cy > DROP_DT_CYCLES))
    {
      app_sample_skips++;
      err_streak = 0U;
      goto service_tasks;
    }
    err_streak = 0U;

    for(i = 0; i < 3U; i++)
    {
      gyr[i] = gyro_dps_from_raw(i, raw.gyr[i], temp_c) * DEG2RAD;
#if APP_ACC_CAL_ENABLE
      acc[i] = acc_calibrate_g(i, (float)raw.acc[i] * ACC_G_PER_LSB) * G_TO_MS2;
#else
      acc[i] = (float)raw.acc[i] * ACC_G_PER_LSB * G_TO_MS2;
#endif
    }

#if APP_ACC_CAL_ENABLE
    { float raw_acc_g[3] = { (float)raw.acc[0] * ACC_G_PER_LSB, (float)raw.acc[1] * ACC_G_PER_LSB, (float)raw.acc[2] * ACC_G_PER_LSB };
      float gyr_dps_now[3] = { gyr[0] / DEG2RAD, gyr[1] / DEG2RAD, gyr[2] / DEG2RAD };
      acc_calibration_sample_task(millis(), raw_acc_g, gyr_dps_now); }
#endif

    /* Configurable gyro low-pass reduces white noise without changing the
     * 2 kHz fusion cadence. The first sample seeds the filter. */
    if(!gyr_lpf_init)
    {
      for(i = 0; i < 3U; i++) gyr_lpf[i] = gyr[i];
      gyr_lpf_init = 1U;
    }
    else
    {
      for(i = 0; i < 3U; i++)
      {
        gyr_lpf[i] += gyr_lpf_alpha * (gyr[i] - gyr_lpf[i]);
      }
    }

    {
      float gyr_dps_lpf[3];
      for(i = 0U; i < 3U; ++i) gyr_dps_lpf[i] = gyr_lpf[i] / DEG2RAD;
      vqf_static_cal_feed(millis(), 1.0f / FUSION_HZ, gyr_dps_lpf, acc, temp_c);
    }
    t0 = dwt_cycles();
    vqf_update(gyr_lpf, acc);

    /* VQF is the sole runtime bias owner. A fast-start history estimate is
     * seeded once; do not override its mean or covariance on each sample. */
    if(quick_bias_active && APP_VQF_REST_BIAS_ENABLE) {
      uint32_t now_ms=millis();
      uint32_t elapsed=quick_save_elapsed_ms ? (uint32_t)(now_ms-quick_save_elapsed_ms) : 0U;
      float b[3]; vqf_get_gyr_bias(b);
      float r2=0;
      for(i=0;i<3;i++) { float r=gyr[i]-b[i]; r2+=r*r; }
      if(vqf_get_rest_detected() && r2 < (.15f*DEG2RAD)*(.15f*DEG2RAD) && elapsed<=5U)
        quick_rest_ms+=elapsed;
      else quick_rest_ms=0;
      if(quick_rest_ms>=APP_GYR_FAST_START_SAVE_MS && vqf_get_bias_sigma_dps()<=.10f) {
        float delta=0;
        for(i=0;i<3;i++) delta+=fabsf(b[i]-quick_bias_initial[i]);
        if(delta>APP_GYR_FAST_START_SAVE_DELTA_DPS*DEG2RAD ||
           fabsf(temp_c-quick_bias_temp_c)>APP_GYR_FAST_START_SAVE_TEMP_C) {
          if(app_save_gyro_bias(b,temp_c)) bias_history_write_error=1U;
        }
        quick_bias_active=0; /* one attempt per boot; VQF keeps adapting */
      }
      quick_save_elapsed_ms=now_ms;
    }
    /* Non-blocking 50 Hz magnetometer scheduler: never wait 5 ms inside
     * the 2 kHz IMU/VQF path. Only the short I2C trigger/read transactions
     * occupy the loop. */
    if(mag_ok)
    {
      if(!mag_pending && ((fusion_n % MAG_PERIOD_N) == 0U))
      {
        mag_err = ist8310_start_measurement();
        if(mag_err == 0)
        {
          mag_pending = 1U;
          mag_start_n = fusion_n;
        }
      }
      else if(mag_pending && ist8310_data_ready())
      {
        int16_t mag_raw[3];
        mag_err = ist8310_read_ready(mag_raw);
        mag_pending = 0U;
        if(mag_err == 0)
        {
          mag_raw_live.seq++;
          __DMB();
          mag_raw_live.millis = millis();
          mag_raw_live.x = mag_raw[0];
          mag_raw_live.y = mag_raw[1];
          mag_raw_live.z = mag_raw[2];
          mag_raw_live.sample_n++;
          __DMB();
          mag_raw_live.seq++;
          /* Calibrate in IST8310 axes first, then map once into LSM6DSV axes.
           * Raw telemetry above stays in original IST8310 register axes. */
          {
            float uncal_centered[3];
            float calibrated_mag[3];
            unsigned row, col;
            for(row = 0; row < 3U; ++row)
              uncal_centered[row] = (float)mag_raw[row] * MAG_UT_PER_LSB - mag_cal_offset[row];
            for(row = 0; row < 3U; ++row)
            {
              calibrated_mag[row] = 0.0f;
              for(col = 0; col < 3U; ++col)
                calibrated_mag[row] += mag_cal_matrix[row][col] * uncal_centered[col];
            }
            mag_map_to_imu(calibrated_mag, mag);
          }
          mag_read_n++;
          /* Official Full VQF's disturbance rejection uses atan2/asin and a
           * second-order filter. Run that at 10 Hz so it cannot steal a 2 kHz
           * gyro sample; the latest 50 Hz field vector remains in telemetry. */
#if MAG_FUSION_ENABLE
          if(((mag_read_n - 1U) % MAG_VQF_DIV) == 0U)
          {
            if(vqf_update_mag(mag) == 0) mag_updates++;
          }
#endif
        }
      }
      else if(mag_pending && ((fusion_n - mag_start_n) > MAG_TIMEOUT_N))
      {
        mag_err = -4;
        mag_pending = 0U;
      }
    }
    vqf_us = (dwt_cycles() - t0) / (system_core_clock / 1000000U);
    fusion_n++;
    window_samples++;

    if((fusion_n % (APP_FUSION_HZ / ATTITUDE_FILTER_HZ)) == 0U || (fusion_n % app_output_div) == 0U)
    {
      /* Odd/even sequence lock lets DAPLink read a coherent live snapshot
       * without halting the 2 kHz fusion loop. */
      vqf_live.seq++;
      __DMB();
      vqf_get_quat9d(q);
      vqf_get_euler_deg(&roll, &pitch, &yaw);

      vqf_live.roll = roll;
      vqf_live.pitch = pitch;
      vqf_live.yaw = yaw;
      vqf_live.qw = q[0];
      vqf_live.qx = q[1];
      vqf_live.qy = q[2];
      vqf_live.qz = q[3];
      vqf_live.gx = (float)raw.gyr[0] * GYR_DPS_PER_LSB;
      vqf_live.gy = (float)raw.gyr[1] * GYR_DPS_PER_LSB;
      vqf_live.gz = (float)raw.gyr[2] * GYR_DPS_PER_LSB;
      vqf_live.ax = (float)raw.acc[0] * ACC_G_PER_LSB;
      vqf_live.ay = (float)raw.acc[1] * ACC_G_PER_LSB;
      vqf_live.az = (float)raw.acc[2] * ACC_G_PER_LSB;
      {
        float live_bias[3];
        vqf_get_gyr_bias(live_bias);
        vqf_live.bias_x = live_bias[0] / DEG2RAD;
        vqf_live.bias_y = live_bias[1] / DEG2RAD;
        vqf_live.bias_z = live_bias[2] / DEG2RAD;
        for(i=0;i<3;i++) app_residual_dps[i]=(gyr_lpf[i]-live_bias[i])/DEG2RAD;
        /* ZARU exit uses this sample's compensated gyro minus the VQF bias.
         * Both are rad/s; the norm below is deg/s. The 30 Hz gyro LPF is not
         * used here, so a real turn is not delayed by that filter. */
        {
          float fast2 = 0.0f;
          float acc2 = acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2];
          for(i = 0; i < 3U; i++) {
            float dps = (gyr[i] - live_bias[i]) / DEG2RAD;
            fast2 += dps * dps;
          }
          app_zaru_rate_fast_dps = sqrtf(fast2);
          app_zaru_acc_dev_ms2 = fabsf(sqrtf(acc2) - G_TO_MS2);
        }
      }
      /* gyr_lpf_z is the (temperature-compensated) signal actually supplied
       * to VQF. corrected_z is its residual after VQF's current bias estimate;
       * both fields are exposed in dps for direct drift diagnosis. */
      vqf_live.temperature_c = imu_temp_live.temperature_c;
      vqf_live.gyr_lpf_z = gyr_lpf[2] / DEG2RAD;
      vqf_live.corrected_z = vqf_live.gyr_lpf_z - vqf_live.bias_z;
      vqf_live.rest_time = vqf_get_rest_time();
      vqf_live.tau_acc = vqf_get_tau_acc();
      vqf_live.rest_detected = (uint32_t)vqf_get_rest_detected();
      vqf_live.mx = mag[0];
      vqf_live.my = mag[1];
      vqf_live.mz = mag[2];
      vqf_live.mag_norm = sqrtf(mag[0]*mag[0] + mag[1]*mag[1] + mag[2]*mag[2]);
      vqf_live.tau_mag = vqf_get_tau_mag();
      vqf_live.mag_err = mag_err;
      vqf_live.mag_updates = mag_updates;
      vqf_live.mag_ready = (uint32_t)vqf_get_mag_ready();
      vqf_live.mag_disturbed = (uint32_t)vqf_get_mag_dist_detected();
      vqf_live.vqf_us = vqf_us;
      vqf_live.fusion_n = fusion_n;
      vqf_live.skip_n = app_sample_skips;
      vqf_live.millis = millis();
      __DMB();
      vqf_live.seq++;
      if((fusion_n % app_output_div)==0U) out_n++;

      if((fusion_n % DAP_OUTPUT_DIV) == 0U)
      {
        acc_cal_live.seq++;
        __DMB();
        acc_cal_live.millis = vqf_live.millis;
        for(i = 0U; i < 3U; ++i)
        {
          acc_cal_live.raw_g[i] = (float)raw.acc[i] * ACC_G_PER_LSB;
          acc_cal_live.corrected_g[i] = acc[i] / G_TO_MS2;
        }
        __DMB();
        acc_cal_live.seq++;
        vqf_tune_live.seq++;
        __DMB();
        vqf_tune_live.millis = vqf_live.millis;
        vqf_tune_live.fusion_hz = vqf_live.fusion_hz;
        vqf_tune_live.skip_n = vqf_live.skip_n;
        vqf_tune_live.rest_detected = vqf_live.rest_detected;
        vqf_tune_live.roll = vqf_live.roll;
        vqf_tune_live.pitch = vqf_live.pitch;
        vqf_tune_live.yaw = vqf_live.yaw;
        vqf_tune_live.gx = vqf_live.gx;
        vqf_tune_live.gy = vqf_live.gy;
        vqf_tune_live.gz = vqf_live.gz;
        vqf_tune_live.ax = vqf_live.ax;
        vqf_tune_live.ay = vqf_live.ay;
        vqf_tune_live.az = vqf_live.az;
        vqf_tune_live.bias_x = vqf_live.bias_x;
        vqf_tune_live.bias_y = vqf_live.bias_y;
        vqf_tune_live.bias_z = vqf_live.bias_z;
        vqf_tune_live.rest_time = vqf_live.rest_time;
        vqf_tune_live.tau_acc = vqf_live.tau_acc;
        vqf_tune_live.temperature_c = vqf_live.temperature_c;
        vqf_tune_live.gyr_lpf_z = vqf_live.gyr_lpf_z;
        vqf_tune_live.corrected_z = vqf_live.corrected_z;
        vqf_tune_live.mx = vqf_live.mx;
        vqf_tune_live.my = vqf_live.my;
        vqf_tune_live.mz = vqf_live.mz;
        vqf_tune_live.mag_norm = vqf_live.mag_norm;
        vqf_tune_live.tau_mag = vqf_live.tau_mag;
        vqf_tune_live.mag_err = vqf_live.mag_err;
        vqf_tune_live.mag_addr = vqf_live.mag_addr;
        vqf_tune_live.mag_updates = vqf_live.mag_updates;
        vqf_tune_live.mag_ready = vqf_live.mag_ready;
        vqf_tune_live.mag_disturbed = vqf_live.mag_disturbed;
        __DMB();
        vqf_tune_live.seq++;
        vqf_nine_live.seq++;
        __DMB();
        vqf_nine_live.snapshot = vqf_tune_live;
        {
          float diagnostic[8];
          unsigned j;
          vqf_get_nine_diagnostic(diagnostic);
          for(j = 0U; j < 8U; ++j) vqf_nine_live.diagnostic[j] = diagnostic[j];
        }
        __DMB();
        vqf_nine_live.seq++;
      }
      if((fusion_n % (APP_FUSION_HZ / ATTITUDE_FILTER_HZ))==0U) app_update_attitude();
      if(protocol_reset_pending == RESET_NONE && (fusion_n % app_output_div)==0U)
        vofa_send_justfloat();
    }

service_tasks:
    app_service_commands();
    app_vqf_cal_service(millis());
#if APP_ACC_CAL_ENABLE
    acc_calibration_service_task(millis());
#endif
    if(app_sample_rebase != 0U)
    {
      app_sample_rebase = 0U;
      last_sample_cy = dwt_cycles();
      app_sample_skips += (app_flash_pause_us * APP_FUSION_HZ) / 1000000U;
    }

    {
      ws2812_mode_t led_mode = WS2812_MODE_6AXIS;
      /* A magnetometer sample must have reached VQF before advertising 9D.
       * Relative yaw is only an output reference and does not change this
       * nine-axis status indication. */
      if((mag_ok != 0U) && (vqf_get_mag_ready() != 0))
      {
        led_mode = WS2812_MODE_9AXIS;
      }
#if APP_ACC_CAL_ENABLE
      if(acc_cal_rt.active != 0U || acc_cal_status_live.status == ACC_CAL_STATUS_FAILED)
      {
        ws2812_acc_calibration_task(millis(), acc_cal_rt.face + 1U, acc_cal_rt.active, acc_cal_status_live.status == ACC_CAL_STATUS_FAILED);
      }
      else
#endif
      if(vqf_static_cal_active())
      {
        /* Preparation, collection and validation override settings colour;
         * completion or cancellation naturally returns to the prior mode. */
        ws2812_calibration_task(millis());
      }
      else if(app_settings_mode != 0U)
      {
        /* Settings mode has priority over status warnings and freezes
         * the LED brightness (no breathing). */
        ws2812_settings_task(millis(), led_mode);
      }
      else if((app_fusion_mode == FUSION_MODE_6AXIS) ||
              (mag_ok != 0U) || (APP_MAG_FUSION_ENABLE == 0U) ||
              (bias_fallback != 0U) || (bias_history_write_error != 0U) ||
              (app_settings_dirty != 0U))
      {
        ws2812_normal_task(millis(), led_mode,
                           bias_no_history,
                           (uint8_t)vqf_get_mag_dist_detected(),
                           app_settings_dirty,
                           bias_history_write_error);
      }
    }
#if APP_CAN_ENABLE
    /* CAN needs microsecond timing: the millisecond tick cannot schedule 1 kHz. */
    can_test_task(micros());
    {
      uint8_t cmd = can_test_get_cmd_flag();
      if(cmd & CAN_CMD_FLAG_REBOOT)
      {
        can_test_clear_cmd_flag(CAN_CMD_FLAG_REBOOT);
        app_request_reset(RESET_APPLICATION);
      }
      if(cmd & CAN_CMD_FLAG_ZERO_YAW)
      {
        can_test_clear_cmd_flag(CAN_CMD_FLAG_ZERO_YAW);
        app_zero_yaw(vofa_pose_live.yaw);
      }
      if(cmd & CAN_CMD_FLAG_RECAL)
      {
        can_test_clear_cmd_flag(CAN_CMD_FLAG_RECAL);
      }
    }
#endif

    if((millis() - last_ms) >= 1000U)
    {
      fusion_hz = window_samples;
      vqf_live.fusion_hz = fusion_hz;
      vqf_live.out_hz = out_n;
      vqf_live.clk_hz = system_core_clock;
      window_samples = 0;
      last_ms += 1000U;
      if(out_n >= 500U)
      {
        led_toggle();
      }
      out_n = 0;
    }
    continue;
recover_or_continue:
    if(err_streak >= ERR_STREAK_RECOVER)
    {
      (void)lsm6dsv_spi_recover();
      (void)lsm6dsv_init_2khz(app_gyro_range_dps);
      live_whoami();
      err_streak = 0U;
    }
    goto service_tasks;
  }
  }
}
