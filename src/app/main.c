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
#include "app_config.h"
#include "vqf_live.h"
#include "mag_calibration.h"
#include "acc_calibration.h"
#include "gyro_bias_history.h"
#include "can_test.h"
#include "usb_cdc.h"
#include "protocol.h"
#include "fusion_settings.h"

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

#define GYR_DPS_PER_LSB      0.035f /* LSM6DSV CTRL6 FS_G=0011, +/-1000 dps */
#define ACC_G_PER_LSB        0.000122f
#define DEG2RAD              0.017453292519943295f
#define G_TO_MS2             9.80665f
#define SAMPLE_DT_CYCLES     ((uint32_t)(system_core_clock / (uint32_t)FUSION_HZ))
#define DROP_DT_CYCLES       ((SAMPLE_DT_CYCLES * 3U) / 2U)
#define CAL_GYR_REST_DPS APP_CAL_GYR_REST_DPS
#define CAL_ACC_REST_MS2 APP_CAL_ACC_REST_MS2
#define CAL_REST_SECONDS APP_CAL_REST_SECONDS   /* startup stationary bias calibration */
#define CAL_DROP_MS      APP_CAL_DROP_MS        /* startup settling/discard time */
#define CAL_BIAS_TEMP_WINDOW_C APP_GYR_BIAS_TEMP_WINDOW_C
#define GYR_LPF_CUTOFF_HZ APP_GYR_LPF_CUTOFF_HZ /* reduce gyro noise before integration */
#define ERR_STREAK_RECOVER   20U
#define MAG_PERIOD_N         40U /* read IST8310 at 50 Hz */
#define MAG_VQF_DIV          APP_MAG_VQF_UPDATE_DIV /* Full VQF update at 10 Hz */
#define MAG_FUSION_ENABLE    APP_MAG_FUSION_ENABLE
#define MAG_TIMEOUT_N        30U
#define MAG_UT_PER_LSB       0.3f

/* Startup calibration is performed on equal-duration block means.  Sorting
 * and trimming the outer blocks rejects knocks and short motion bursts while
 * retaining the configured multi-second calibration duration. */
#define CAL_BLOCK_SAMPLES     APP_CAL_BLOCK_SAMPLES
#define CAL_BLOCK_MAX         APP_CAL_BLOCK_MAX
#define CAL_TRIM_PERCENT      APP_CAL_TRIM_PERCENT

static float cal_gyr_blocks[3][CAL_BLOCK_MAX];
static float cal_acc_blocks[3][CAL_BLOCK_MAX];
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

/* Output VOFA/DAPLink pose ABI. Only yaw is KF-filtered; other fields are raw. */
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

#define VOFA_MAX_CH   6U
#define VOFA_MAX_BYTES ((VOFA_MAX_CH * 4U) + 4U)

static uint8_t vofa_dma[2][VOFA_MAX_BYTES];
static uint8_t vofa_sel;
static uint32_t vofa_late;

static float wrap_deg(float angle)
{
  while(angle > 180.0f) angle -= 360.0f;
  while(angle < -180.0f) angle += 360.0f;
  return angle;
}

static float app_yaw_offset = 0.0f;
static void app_zero_yaw(float current_yaw)
{
  app_yaw_offset = wrap_deg(app_yaw_offset + current_yaw);
}

/* UART and USB CDC accept the same framed host-command protocol. */
typedef enum
{
  PROTOCOL_SOURCE_UART = 0,
  PROTOCOL_SOURCE_USB = 1
} protocol_source_t;

/* Non-blocking automatic six-face accelerometer calibration. */
#if APP_ACC_CAL_ENABLE
typedef struct {
  uint8_t active, face, face_mask, candidate;
  uint8_t source, seq;
  uint32_t start_ms, face_start_ms, stable_ms, collect_ms;
  uint32_t samples;
  float sum[3];
  float face_mean[6][3];
} acc_cal_runtime_t;
static acc_cal_runtime_t acc_cal_rt;
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
static stream_mode_t app_stream_mode = STREAM_MODE_VOFA_3CH;
static fusion_mode_t app_fusion_mode = FUSION_MODE_9AXIS;
static uint8_t app_relative_yaw_enabled;
/* Runtime stream rate. It is intentionally volatile only in RAM for now;
 * persistent output-rate settings need a versioned flash-record extension. */
static uint16_t app_output_hz = APP_VOFA_OUTPUT_HZ;
static uint16_t app_output_div = OUTPUT_DIV;
static uint8_t app_stream_seq;
static volatile uint8_t protocol_reset_pending;
static volatile uint8_t app_settings_mode;
static volatile uint8_t app_settings_dirty;

static void protocol_send_frame(protocol_source_t source, const uint8_t *frame, uint16_t len)
{
  if(source == PROTOCOL_SOURCE_UART)
    (void)uart_dma_send(frame, len);
  else
    (void)usb_cdc_write(frame, len);
}

static void protocol_reply_ack(protocol_source_t source, uint8_t seq,
                               uint8_t cmd_id, uint8_t status, uint16_t detail)
{
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  uint16_t len = protocol_pack_ack(frame, seq, cmd_id, status, detail);
  if(len != 0U) protocol_send_frame(source, frame, len);
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
        protocol_reset_pending = 1U;
    }
    *index = 0U;
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
      }
      protocol_reply_ack(source, seq, msg_id, status, (uint16_t)app_stream_mode);
      break;
    case AHRS_CMD_SET_OUTPUT_HZ:
      /* 2 kHz fusion permits only exact integer divisors. This command is
       * runtime-only; flash persistence is deliberately not implied. */
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
          app_output_hz = hz;
          app_output_div = (uint16_t)(APP_FUSION_HZ / hz);
        }
      }
      protocol_reply_ack(source, seq, msg_id, status, app_output_hz);
      break;
    case AHRS_CMD_QUERY_STATUS:
      if(len != 0U)
      {
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_INVALID_PARAM, 0U);
        break;
      }
      frame_len = protocol_pack_system_info(frame, seq, APP_FUSION_HZ,
                                             app_output_hz, app_output_div,
                                             imu_temp_live.temperature_c,
                                             (uint8_t)app_stream_mode,
                                             (uint8_t)((APP_CAN_ENABLE != 0U) && (can_test_live.init_ok != 0U)));
      if(frame_len != 0U) protocol_send_frame(source, frame, frame_len);
      break;
    case AHRS_CMD_ENTER_SETTINGS:
      if(len != 0U) status = AHRS_ACK_INVALID_PARAM;
      else app_settings_mode = 1U;
      protocol_reply_ack(source, seq, msg_id, status, 0U);
      break;
    case AHRS_CMD_EXIT_SETTINGS:
      if(len != 0U) status = AHRS_ACK_INVALID_PARAM;
      else app_settings_mode = 0U;
      protocol_reply_ack(source, seq, msg_id, status, app_settings_dirty);
      break;
    case AHRS_CMD_SET_FUSION_MODE:
      /* Settings are deliberately staged in flash. The running VQF mode is
       * never changed in-place; reboot applies the selected mode. */
      if((app_settings_mode == 0U) || (len < 1U) || (len > 2U) ||
         (payload == NULL) || (payload[0] > FUSION_MODE_9AXIS_RELATIVE) ||
         ((len == 2U) && (payload[1] > 1U)))
      {
        status = (app_settings_mode == 0U) ? AHRS_ACK_EXEC_FAILED : AHRS_ACK_INVALID_PARAM;
      }
      else if(fusion_settings_save_ex((fusion_mode_t)payload[0],
                                       can_test_get_node_id()) != 0)
      {
        status = AHRS_ACK_EXEC_FAILED;
      }
      else
      {
        app_settings_dirty = 1U;
        if((len == 2U) && (payload[1] != 0U))
          protocol_reset_pending = 1U;
      }
      protocol_reply_ack(source, seq, msg_id, status, (uint16_t)payload[0]);
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
           (can_test_set_node_id(node_id) != 0) ||
           (fusion_settings_save_ex(app_fusion_mode, node_id) != 0))
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
    case AHRS_CMD_START_GYRO_CAL_60S:
      /* Runtime calibration is not yet a blocking operation. Reuse the
       * existing safe command response until its non-blocking state machine
       * is enabled, instead of silently pretending that it completed. */
      protocol_reply_ack(source, seq, msg_id, AHRS_ACK_EXEC_FAILED, 0x0601U);
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
        memset(&acc_cal_rt, 0, sizeof(acc_cal_rt));
        acc_cal_rt.active = 1U; acc_cal_rt.face = 0U;
        acc_cal_rt.source = (uint8_t)source; acc_cal_rt.seq = seq;
        acc_cal_rt.start_ms = millis(); acc_cal_rt.face_start_ms = acc_cal_rt.start_ms;
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
        protocol_reply_ack(source, seq, msg_id, AHRS_ACK_SUCCESS, 0U);
        protocol_reset_pending = 1U;
      }
      break;
    default:
      protocol_reply_ack(source, seq, msg_id, AHRS_ACK_UNKNOWN_CMD, 0U);
      break;
  }
}


#if APP_ACC_CAL_ENABLE
static int acc_cal_detect_face(const float a[3], const float gyr_dps[3])
{
  float n = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
  float ag[3] = {fabsf(a[0]),fabsf(a[1]),fabsf(a[2])};
  uint32_t k; int axis=0; float best=ag[0];
  if(n < (1.0f-APP_ACC_CAL_NORM_TOL_G) || n > (1.0f+APP_ACC_CAL_NORM_TOL_G)) return -1;
  if(fabsf(gyr_dps[0]) > APP_ACC_CAL_GYR_REST_DPS || fabsf(gyr_dps[1]) > APP_ACC_CAL_GYR_REST_DPS || fabsf(gyr_dps[2]) > APP_ACC_CAL_GYR_REST_DPS) return -1;
  for(k=1U;k<3U;k++) if(ag[k]>best){best=ag[k];axis=(int)k;}
  if(best < APP_ACC_CAL_DOMINANT_MIN_G) return -1;
  for(k=0U;k<3U;k++) if((int)k!=axis && ag[k]>APP_ACC_CAL_OTHER_MAX_G) return -1;
  return axis*2 + ((a[axis] >= 0.0f) ? 1 : 0);
}

static void acc_cal_finish(uint8_t ok, uint16_t detail)
{
  uint8_t source=acc_cal_rt.source; uint8_t seq=acc_cal_rt.seq;
  acc_cal_rt.active=0U;
  acc_cal_status_live.status = ok ? ACC_CAL_STATUS_DONE : ACC_CAL_STATUS_FAILED;
  acc_cal_status_live.face = ok ? 6U : acc_cal_rt.face+1U;
  if(ok) { acc_cal_status_live.face_mask=0x3FU; acc_cal_status_live.bias_g[0]=acc_calibration_active.bias_g[0]; acc_cal_status_live.bias_g[1]=acc_calibration_active.bias_g[1]; acc_cal_status_live.bias_g[2]=acc_calibration_active.bias_g[2]; }
  protocol_reply_ack((protocol_source_t)source, seq, AHRS_CMD_START_ACC_6FACE_CAL, ok ? AHRS_ACK_SUCCESS : AHRS_ACK_EXEC_FAILED, detail);
}

static void acc_calibration_sample_task(uint32_t now_ms, const float raw_g[3], const float gyr_dps[3])
{
  int dir; uint8_t axis, sign; uint32_t i;
  if(acc_cal_rt.active==0U) return;
  acc_cal_status_live.millis=now_ms; acc_cal_status_live.seq++; acc_cal_status_live.face=acc_cal_rt.face+1U;
  acc_cal_status_live.detected_face=0U; acc_cal_status_live.sample_count=(uint16_t)(acc_cal_rt.samples>65535U?65535U:acc_cal_rt.samples);
  if((uint32_t)(now_ms - ((acc_cal_rt.face_start_ms != 0U) ? acc_cal_rt.face_start_ms : acc_cal_rt.start_ms)) >
     (uint32_t)(APP_ACC_CAL_FACE_TIMEOUT_S * 1000.0f)) { acc_cal_finish(0U,0x0603U); return; }
  dir=acc_cal_detect_face(raw_g,gyr_dps);
  if(dir<0 || (acc_cal_rt.face_mask & (uint8_t)(1U<<dir))!=0U) { acc_cal_rt.stable_ms=0U; acc_cal_rt.collect_ms=0U; acc_cal_rt.samples=0U; return; }
  acc_cal_status_live.detected_face=(uint8_t)(dir+1);
  if(acc_cal_rt.candidate != (uint8_t)(dir+1)) { acc_cal_rt.candidate=(uint8_t)(dir+1); acc_cal_rt.stable_ms=now_ms; acc_cal_rt.collect_ms=0U; acc_cal_rt.samples=0U; for(i=0;i<3;i++) acc_cal_rt.sum[i]=0.0f; return; }
  if((uint32_t)(now_ms-acc_cal_rt.stable_ms) < APP_ACC_CAL_STABLE_MS) return;
  if(acc_cal_rt.collect_ms==0U) acc_cal_rt.collect_ms=now_ms;
  for(i=0;i<3;i++)
    acc_cal_rt.sum[i]+=raw_g[i];
  acc_cal_rt.samples++;
  if((uint32_t)(now_ms-acc_cal_rt.collect_ms) < (uint32_t)(APP_ACC_CAL_FACE_SECONDS * 1000.0f)) return;
  if(acc_cal_rt.samples < 50U) { acc_cal_finish(0U,0x0605U); return; }
  axis=(uint8_t)(dir/2); sign=(uint8_t)(dir&1U);
  for(i=0;i<3;i++) acc_cal_rt.face_mean[dir][i]=acc_cal_rt.sum[i]/(float)acc_cal_rt.samples;
  acc_cal_rt.face_mask |= (uint8_t)(1U<<dir); acc_cal_rt.face++; acc_cal_rt.face_start_ms=now_ms; acc_cal_rt.candidate=0U; acc_cal_rt.stable_ms=0U; acc_cal_rt.collect_ms=0U; acc_cal_rt.samples=0U;
  (void)axis; (void)sign;
  if(acc_cal_rt.face>=6U)
  {
    acc_calibration_t cal; float pos,neg;
    for(axis=0U;axis<3U;axis++) { pos=acc_cal_rt.face_mean[axis*2+1][axis]; neg=acc_cal_rt.face_mean[axis*2][axis]; cal.bias_g[axis]=(pos+neg)*0.5f; cal.scale[axis]=2.0f/(pos-neg); if(cal.scale[axis]<0.5f||cal.scale[axis]>1.5f) { acc_cal_finish(0U,0x0606U); return; } }
    cal.valid=1U; if(acc_calibration_save(&cal)!=0) { acc_cal_finish(0U,0x0607U); return; }
    acc_cal_status_live.bias_g[0]=cal.bias_g[0]; acc_cal_status_live.bias_g[1]=cal.bias_g[1]; acc_cal_status_live.bias_g[2]=cal.bias_g[2];
    acc_cal_status_live.scale[0]=cal.scale[0]; acc_cal_status_live.scale[1]=cal.scale[1]; acc_cal_status_live.scale[2]=cal.scale[2]; acc_cal_finish(1U,0U);
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

static void sort_float(float *values, uint32_t n)
{
  uint32_t i;

  for(i = 1U; i < n; ++i)
  {
    const float value = values[i];
    uint32_t j = i;
    while((j > 0U) && (values[j - 1U] > value))
    {
      values[j] = values[j - 1U];
      --j;
    }
    values[j] = value;
  }
}

static float trimmed_mean(float *values, uint32_t n)
{
  uint32_t trim;
  uint32_t first;
  uint32_t last;
  uint32_t i;
  float sum = 0.0f;

  if(n == 0U) return 0.0f;
  sort_float(values, n);
  trim = (n * CAL_TRIM_PERCENT) / 100U;
  if((trim * 2U) >= n) trim = 0U;
  first = trim;
  last = n - trim;
  for(i = first; i < last; ++i) sum += values[i];
  return sum / (float)(last - first);
}

static void vofa_send_justfloat(float late)
{
  static uint8_t filter_init;
#if APP_VOFA_REST_HOLD_ENABLE
  static uint8_t rest_hold;
  static float rest_yaw;
  static float rest_pitch;
  static float rest_roll;
#endif
  static float yaw_kf;
  static float yaw_kf_var;
  static float rate_norm_lpf;
  static float innovation_var;
  static float yaw_kf_r_scale = 1.0f;
  static float motion_on_time;
  static float motion_off_time;
  static uint32_t yaw_sync_div;
  static uint32_t kf_last_cy;
  static uint8_t kf_clock_init;
  static uint8_t rate_lpf_init;
  static uint8_t motion_active;
  const float nominal_dt = ((float)app_output_div / FUSION_HZ);
  float dt = nominal_dt;
  float output_yaw;
  float output_pitch;
  float output_roll;
    /* Use the real output interval when it is sane. This prevents a delayed
     * UART/I2C iteration from making the output filter use an incorrect gyro
     * integration interval. */
    {
      uint32_t now_cy = dwt_cycles();
      if((kf_clock_init != 0U) && (system_core_clock != 0U))
      {
        float measured_dt = (float)(now_cy - kf_last_cy) /
                            (float)system_core_clock;
        if((measured_dt >= 0.00025f) && (measured_dt <= 0.005f))
          dt = measured_dt;
      }
      kf_last_cy = now_cy;
      kf_clock_init = 1U;
    }

  /* Output-only adaptive 1D angle Kalman filter. VQF continues to calculate
    * its normal yaw; this filter only affects the VOFA/DAP output pose ABI.
    * The rate hysteresis, innovation gate and adaptive R below prevent a
    * single noisy sample or a threshold crossing from moving the output. */
  {
    /* Use the temperature-compensated, low-pass filtered gyro residual for
     * the output-only yaw KF. VQF itself continues to run independently. */
    float yaw_rate = vqf_live.corrected_z;
    float gx = vqf_live.gx - vqf_live.bias_x;
    float gy = vqf_live.gy - vqf_live.bias_y;
    float gz = yaw_rate;
    float rate_norm = sqrtf(gx * gx + gy * gy + gz * gz);
    float rate_alpha;
    float motion_den = APP_VOFA_YAW_KF_MOTION_FULL_DPS -
                       APP_VOFA_YAW_KF_MOTION_START_DPS;
    float motion;
    float yaw_q;
    float yaw_r_base;
    float yaw_r;
    float acc_norm;
    float acc_disturbance;
    float p_pred;
    float yaw_pred;
    float innovation;
    float p_floor = 1.0e-9f;

    /* A bad sample must not poison the state or covariance. */
    if(!(rate_norm >= 0.0f)) rate_norm = 0.0f;

    /* Filter the rate used for motion classification. The gyro itself is
     * still used for prediction once motion has been confirmed. */
    if(APP_VOFA_YAW_KF_RATE_LPF_HZ > 0.0f)
      rate_alpha = 1.0f - expf(-6.28318530718f *
                               APP_VOFA_YAW_KF_RATE_LPF_HZ * dt);
    else
      rate_alpha = 1.0f;
    if(rate_alpha < 0.0f) rate_alpha = 0.0f;
    if(rate_alpha > 1.0f) rate_alpha = 1.0f;
    if(!rate_lpf_init)
    {
      rate_norm_lpf = rate_norm;
      rate_lpf_init = 1U;
    }
    else
    {
      rate_norm_lpf += rate_alpha * (rate_norm - rate_norm_lpf);
    }

    /* Motion hysteresis and time confirmation keep Q/R and prediction mode
     * from chattering around the original 0.5 dps threshold. */
    {
      const float confirm_s = (float)APP_VOFA_YAW_KF_MOTION_CONFIRM_MS * 0.001f;
      if(!motion_active)
      {
        motion_off_time = 0.0f;
        if(rate_norm_lpf >= APP_VOFA_YAW_KF_MOTION_START_DPS)
        {
          motion_on_time += dt;
          if(motion_on_time >= confirm_s)
          {
            motion_active = 1U;
            motion_on_time = 0.0f;
          }
        }
        else
        {
          motion_on_time = 0.0f;
        }
      }
      else
      {
        motion_on_time = 0.0f;
        if(rate_norm_lpf <= APP_VOFA_YAW_KF_MOTION_STOP_DPS)
        {
          motion_off_time += dt;
          if(motion_off_time >= confirm_s)
          {
            motion_active = 0U;
            motion_off_time = 0.0f;
          }
        }
        else
        {
          motion_off_time = 0.0f;
        }
      }
    }

    if(motion_den <= 0.0f)
      motion = motion_active ? 1.0f : 0.0f;
    else
      motion = (rate_norm_lpf - APP_VOFA_YAW_KF_MOTION_START_DPS) / motion_den;
    if(motion < 0.0f) motion = 0.0f;
    if(motion > 1.0f) motion = 1.0f;
    /* Smooth the transition so Q/R do not jump at the threshold. */
    motion = motion * motion * (3.0f - 2.0f * motion);
    if(vqf_live.rest_detected != 0U && !motion_active)
      motion = 0.0f;

    yaw_q = APP_VOFA_YAW_KF_Q_REST_DEG2_PER_S +
            (APP_VOFA_YAW_KF_Q_MOVE_DEG2_PER_S -
             APP_VOFA_YAW_KF_Q_REST_DEG2_PER_S) * motion;
    yaw_r_base = APP_VOFA_YAW_KF_R_REST_DEG2 +
                 (APP_VOFA_YAW_KF_R_MOVE_DEG2 -
                  APP_VOFA_YAW_KF_R_REST_DEG2) * motion;

    /* Increase measurement uncertainty when the acceleration magnitude is
     * inconsistent with 1 g. This keeps dynamic acceleration from being
     * mistaken for a reliable VQF yaw correction. */
    acc_norm = sqrtf(vqf_live.ax * vqf_live.ax +
                     vqf_live.ay * vqf_live.ay +
                     vqf_live.az * vqf_live.az);
    acc_disturbance = 0.0f;
    if(APP_VOFA_YAW_KF_ACC_NORM_TOL_G > 0.0f)
    {
      acc_disturbance = (fabsf(acc_norm - 1.0f) -
                         APP_VOFA_YAW_KF_ACC_NORM_TOL_G) /
                        APP_VOFA_YAW_KF_ACC_NORM_TOL_G;
      if(acc_disturbance < 0.0f) acc_disturbance = 0.0f;
      if(acc_disturbance > 1.0f) acc_disturbance = 1.0f;
    }
    if(APP_VOFA_YAW_KF_R_ACCEL_DEG2 > yaw_r_base)
      yaw_r_base += (APP_VOFA_YAW_KF_R_ACCEL_DEG2 - yaw_r_base) *
                    acc_disturbance;
    if(yaw_r_base < p_floor) yaw_r_base = p_floor;
    if(yaw_q < 0.0f) yaw_q = 0.0f;

    if(!filter_init)
    {
      yaw_kf = vqf_live.yaw;
      yaw_kf_var = yaw_r_base;
      innovation_var = yaw_kf_var + yaw_r_base;
      yaw_kf_r_scale = 1.0f;
      filter_init = 1U;
    }
#if APP_VOFA_REST_HOLD_ENABLE
    /* Lock the complete attitude output after VQF confirms a rest state.
     * VQF and its telemetry continue running underneath this output hold. */
    if(vqf_live.rest_detected != 0U)
    {
      if(!rest_hold)
      {
        rest_yaw = yaw_kf;
        rest_pitch = vqf_live.pitch;
        rest_roll = vqf_live.roll;
        rest_hold = 1U;
      }
    }
    else
    {
      rest_hold = 0U;
    }
#endif
#if APP_VOFA_REST_HOLD_ENABLE
    if(!rest_hold)
#endif
    {
      /* Do not integrate residual gyro noise while stationary. At rest the
       * state is held and only slowly corrected by the yaw measurement. Once
       * confirmed motion is detected, use gyro propagation for prompt
       * response. */
      yaw_pred = yaw_kf;
      if(motion_active)
        yaw_pred = wrap_deg(yaw_kf + yaw_rate * dt);
      p_pred = yaw_kf_var + yaw_q * dt;
      if(!(p_pred >= p_floor)) p_pred = p_floor;
      innovation = wrap_deg(vqf_live.yaw - yaw_pred);

      /* Adapt R only during confirmed rest. It is increased when the
       * innovation variance is persistently larger than expected and then
       * slowly returns to the configured baseline. */
      {
        float r_alpha = 1.0f;
        float target_scale = 1.0f;
        float estimated_r;
        if(APP_VOFA_YAW_KF_R_ADAPT_TAU_S > 0.0f)
        {
          r_alpha = 1.0f - expf(-dt / APP_VOFA_YAW_KF_R_ADAPT_TAU_S);
          if(r_alpha < 0.0f) r_alpha = 0.0f;
          if(r_alpha > 1.0f) r_alpha = 1.0f;
        }
        if((!motion_active) && (vqf_live.rest_detected != 0U))
        {
          innovation_var += r_alpha * (innovation * innovation - innovation_var);
          estimated_r = innovation_var - p_pred;
          if(estimated_r > yaw_r_base)
            target_scale = estimated_r / yaw_r_base;
        }
        else
        {
          innovation_var += r_alpha *
                            ((p_pred + yaw_r_base) - innovation_var);
        }
        if(target_scale < 1.0f) target_scale = 1.0f;
        if(target_scale > APP_VOFA_YAW_KF_R_ADAPT_MAX_SCALE)
          target_scale = APP_VOFA_YAW_KF_R_ADAPT_MAX_SCALE;
        yaw_kf_r_scale += r_alpha * (target_scale - yaw_kf_r_scale);
        if(yaw_kf_r_scale < 1.0f) yaw_kf_r_scale = 1.0f;
        if(yaw_kf_r_scale > APP_VOFA_YAW_KF_R_ADAPT_MAX_SCALE)
          yaw_kf_r_scale = APP_VOFA_YAW_KF_R_ADAPT_MAX_SCALE;
      }

      yaw_r = yaw_r_base * yaw_kf_r_scale;

      /* Soft innovation gate: inflate R rather than dropping the sample,
       * so the filter can recover from a real turn without a hard step. */
      {
        float innovation_gate = APP_VOFA_YAW_KF_INNOVATION_GATE_SIGMA *
                                 sqrtf(p_pred + yaw_r);
        float motion_allowance = 2.0f * rate_norm_lpf * dt +
                                 APP_VOFA_YAW_KF_INNOVATION_GATE_MIN_DEG;
        float gate_factor;
        float max_gate_factor = sqrtf(APP_VOFA_YAW_KF_R_ADAPT_MAX_SCALE);
        if(innovation_gate < APP_VOFA_YAW_KF_INNOVATION_GATE_MIN_DEG)
          innovation_gate = APP_VOFA_YAW_KF_INNOVATION_GATE_MIN_DEG;
        if(motion_active && innovation_gate < motion_allowance)
          innovation_gate = motion_allowance;
        if(fabsf(innovation) > innovation_gate)
        {
          gate_factor = fabsf(innovation) / innovation_gate;
          if(gate_factor > max_gate_factor) gate_factor = max_gate_factor;
          yaw_r *= gate_factor * gate_factor;
        }
      }

      {
        float k = p_pred / (p_pred + yaw_r);
        if(k < 0.0f) k = 0.0f;
        if(k > 1.0f) k = 1.0f;
        yaw_kf = wrap_deg(yaw_pred + k * innovation);
        yaw_kf_var = (1.0f - k) * p_pred;
        if(yaw_kf_var < p_floor) yaw_kf_var = p_floor;
      }
    }
  }

#if APP_VOFA_REST_HOLD_ENABLE
  if(rest_hold)
  {
    output_yaw = rest_yaw;
    output_pitch = rest_pitch;
    output_roll = rest_roll;
  }
  else
#endif
  {
    output_yaw = yaw_kf;
    output_pitch = vqf_live.pitch;
    output_roll = vqf_live.roll;
  }

  /* Relative yaw is an output-reference option only. The VQF/KF state and
   * magnetometer fusion remain unchanged; latch the first published yaw as
   * the boot reference. */
  if(app_relative_yaw_enabled != 0U)
  {
    static uint8_t relative_yaw_initialized;
    if(relative_yaw_initialized == 0U)
    {
      app_yaw_offset = wrap_deg(output_yaw);
      relative_yaw_initialized = 1U;
    }
  }
  output_yaw = wrap_deg(output_yaw - app_yaw_offset);

  vofa_pose_live.seq++;
  __DMB();
  vofa_pose_live.yaw = output_yaw;
  vofa_pose_live.pitch = output_pitch;
  vofa_pose_live.roll = output_roll;
  vofa_pose_live.temperature_c = imu_temp_live.temperature_c;
  vofa_pose_live.millis = millis();
  __DMB();
  vofa_pose_live.seq++;

  /* Keep a compact, sequence-locked copy for high-rate DAPLink capture. */
  yaw_sync_div++;
  if(yaw_sync_div >= YAW_KF_SYNC_DIV)
  {
    yaw_sync_div = 0U;
    yaw_kf_sync_live.seq++;
    __DMB();
    yaw_kf_sync_live.millis = vofa_pose_live.millis;
    yaw_kf_sync_live.vqf_yaw = vqf_live.yaw;
    yaw_kf_sync_live.kf_yaw = output_yaw;
    yaw_kf_sync_live.gz = vqf_live.gz;
    yaw_kf_sync_live.bias_z = vqf_live.bias_z;
    yaw_kf_sync_live.rest_detected = vqf_live.rest_detected;
    yaw_kf_sync_live.mag_updates = vqf_live.mag_updates;
    yaw_kf_sync_live.temperature_c = vqf_live.temperature_c;
    yaw_kf_sync_live.gyr_lpf_z = vqf_live.gyr_lpf_z;
    yaw_kf_sync_live.corrected_z = vqf_live.corrected_z;
    __DMB();
    yaw_kf_sync_live.seq++;
  }

  /* Stream mode is now applied to the actual output path. The default is
   * unchanged: JustFloat yaw/pitch/roll on both USB CDC and USART4. */
  {
    uint16_t tx_len = 0U;
    uint8_t *tx = vofa_dma[vofa_sel];
    static const uint8_t tail[4] = {0x00U, 0x00U, 0x80U, 0x7FU};
    float ch[VOFA_MAX_CH];

    if(app_stream_mode == STREAM_MODE_VOFA_3CH ||
       app_stream_mode == STREAM_MODE_VOFA_6CH)
    {
      uint8_t n = (app_stream_mode == STREAM_MODE_VOFA_6CH) ? 6U : 3U;
      ch[0] = output_yaw;
      ch[1] = output_pitch;
      ch[2] = output_roll;
      if(n == 6U)
      {
        ch[3] = vqf_live.gz;
        ch[4] = vqf_live.az;
        ch[5] = imu_temp_live.temperature_c;
      }
      memcpy(tx, ch, (uint16_t)n * 4U);
      memcpy(tx + (uint16_t)n * 4U, tail, 4U);
      tx_len = (uint16_t)n * 4U + 4U;
    }
    else if(app_stream_mode == STREAM_MODE_BIN_ATT)
    {
      uint8_t flags = (uint8_t)(vqf_live.rest_detected ? AHRS_FLAG_REST_DETECTED : 0U);
      tx_len = protocol_pack_attitude(tx, app_stream_seq++, output_roll,
                                      output_pitch, output_yaw, flags,
                                      (uint16_t)millis());
    }
    else if(app_stream_mode == STREAM_MODE_BIN_COMPACT)
    {
      uint8_t flags = (uint8_t)(vqf_live.rest_detected ? AHRS_FLAG_REST_DETECTED : 0U);
      tx_len = protocol_pack_compact(tx, app_stream_seq++, output_roll,
                                     output_pitch, output_yaw, vqf_live.gz,
                                     flags, (uint16_t)millis());
    }
    else if(app_stream_mode == STREAM_MODE_BIN_IMU)
    {
      tx_len = protocol_pack_imu(tx, app_stream_seq++, vqf_live.gx,
                                 vqf_live.gy, vqf_live.gz, vqf_live.ax,
                                 vqf_live.ay, vqf_live.az,
                                 imu_temp_live.temperature_c,
                                 (uint16_t)millis());
    }

    if(tx_len != 0U)
    {
      (void)usb_cdc_write(tx, tx_len);
      if(uart_dma_send(tx, tx_len) == 0)
        vofa_sel ^= 1U;
      else
        vofa_late++;
    }
  }

#if APP_CAN_ENABLE
  can_test_update_data(output_roll, output_pitch, output_yaw,
                       vqf_live.gx, vqf_live.gy, vqf_live.gz,
                       vqf_live.ax, vqf_live.ay, vqf_live.az,
                       vqf_live.qw, vqf_live.qx, vqf_live.qy, vqf_live.qz,
                       imu_temp_live.temperature_c,
                       (uint8_t)vqf_live.rest_detected);
#endif
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
      err = lsm6dsv_init_2khz();
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
#define APP_BOOT_REQUEST_ADDR 0x2000BFF0U
#define APP_BOOT_REQUEST_MAGIC 0x424F4F54U
static void app_request_bootloader(void){ *((volatile uint32_t *)APP_BOOT_REQUEST_ADDR) = APP_BOOT_REQUEST_MAGIC; __DMB(); nvic_system_reset(); }

int main(void)
{
  SCB->VTOR = 0x08008000U;
  lsm6dsv_raw_t raw;
  float gyr[3];
  float acc[3];
  float mag[3] = {0.0f, 0.0f, 0.0f};
  float q[4];
  float roll;
  float pitch;
  float yaw;
  uint32_t fusion_n = 0;
  uint32_t out_n = 0;
  uint32_t skip_n = 0;
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
  uint8_t quick_bias_saved = 0U;
  uint32_t quick_rest_ms = 0U;
  uint32_t quick_save_elapsed_ms = 0U;
  uint32_t quick_samples = 0U;
  float quick_bias[3] = {0.0f, 0.0f, 0.0f};
  float quick_bias_initial[3] = {0.0f, 0.0f, 0.0f};
  float quick_bias_sum[3] = {0.0f, 0.0f, 0.0f};
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
  {
    uint16_t stored_can_id = APP_CAN_DEFAULT_CAN_ID;
    (void)fusion_settings_load_ex(&app_fusion_mode, &stored_can_id);
#if !APP_MAG_FUSION_ENABLE
    /* A six-axis image must not inherit a previously stored nine-axis mode. */
    app_fusion_mode = FUSION_MODE_6AXIS;
#endif
    (void)can_test_set_node_id(stored_can_id);
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
  err = lsm6dsv_init_2khz();
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

  vqf_init(1.0f / FUSION_HZ, 1.0f / FUSION_HZ);
  {
    float startup_acc[3] = {0.0f, 0.0f, G_TO_MS2};
    float history_latest[3] = {0.0f, 0.0f, 0.0f};
    float history_average[3] = {0.0f, 0.0f, 0.0f};
    float history_nearest[3] = {0.0f, 0.0f, 0.0f};
    float nearest_temp_c = 0.0f;
    uint32_t history_count = 0U;
    uint8_t nearest_valid = 0U;
    uint8_t history_corrupt = 0U;
    float gyr_bias[3] = {0.0f, 0.0f, 0.0f};
    float acc_avg[3] = {0.0f, 0.0f, G_TO_MS2};
    lsm6dsv_raw_t startup_raw;
    int history_valid;

    /* Read one sample only to obtain an immediate gravity vector and current
     * temperature. There is deliberately no startup calibration loop here. */
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
#if APP_GYR_FAST_START_ENABLE
    history_valid = gyro_bias_history_load_for_temp(
        history_latest, history_average, history_nearest, temp_c,
        CAL_BIAS_TEMP_WINDOW_C, &nearest_temp_c, &nearest_valid,
        &history_count, &history_corrupt);

    if((history_valid != 0) && (nearest_valid != 0U))
    {
      for(i = 0U; i < 3U; ++i) quick_bias[i] = history_nearest[i];
      quick_bias_temp_c = nearest_temp_c;
    }
    else
    {
      /* No temperature-matched history: use the configured fixed defaults.
       * This is also the explicit missing-history indication for the LED. */
      quick_bias[0] = APP_GYR_DEFAULT_BIAS_X_DPS * DEG2RAD;
      quick_bias[1] = APP_GYR_DEFAULT_BIAS_Y_DPS * DEG2RAD;
      quick_bias[2] = APP_GYR_DEFAULT_BIAS_Z_DPS * DEG2RAD;
      quick_bias_temp_c = temp_c;
      bias_no_history = 1U;
      bias_fallback = 1U;
    }
    for(i = 0U; i < 3U; ++i)
    {
      quick_bias_initial[i] = quick_bias[i];
      gyr_bias[i] = quick_bias[i];
      acc_avg[i] = startup_acc[i];
    }
    quick_bias_active = 1U;
    (void)history_average;
    (void)history_count;
    (void)history_corrupt;
#else
    /* Fast startup is deliberately disabled in normal builds. Keep the
     * immediate sensor seed deterministic; a debug build can enable the
     * history/background correction path with FAST_START=1. */
    quick_bias[0] = APP_GYR_DEFAULT_BIAS_X_DPS * DEG2RAD;
    quick_bias[1] = APP_GYR_DEFAULT_BIAS_Y_DPS * DEG2RAD;
    quick_bias[2] = APP_GYR_DEFAULT_BIAS_Z_DPS * DEG2RAD;
    for(i = 0U; i < 3U; ++i)
    {
      gyr_bias[i] = quick_bias[i];
      acc_avg[i] = startup_acc[i];
    }
    quick_bias_active = 0U;
    (void)history_valid;
    (void)history_average;
    (void)history_count;
    (void)history_corrupt;
#endif
    vqf_prime_rest(acc_avg, gyr_bias);
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

  while(1)
  {
    uint32_t now_cy;
    uint32_t dt_cy;

    if(lsm6dsv_wait_sample(2000U) != 0)
    {
      skip_n++;
      err_streak++;
      goto recover_or_continue;
    }
    if(lsm6dsv_read_raw(&raw) != 0)
    {
      skip_n++;
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
      skip_n++;
      err_streak = 0U;
      continue;
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

    t0 = dwt_cycles();
    vqf_update(gyr_lpf, acc);

    /* Fast-start background bias refinement. VQF is already running using the
     * persisted bias; while the unit remains still, average the live residual
     * and move the estimate gradually instead of causing an attitude step. */
    if(quick_bias_active != 0U)
    {
      const float residual[3] = {
        gyr[0] - quick_bias[0], gyr[1] - quick_bias[1], gyr[2] - quick_bias[2]
      };
      const float residual_norm = sqrtf(residual[0] * residual[0] +
                                        residual[1] * residual[1] +
                                        residual[2] * residual[2]);
      const float acc_norm = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
      const uint32_t now_ms = millis();
      const uint32_t elapsed_ms = (quick_save_elapsed_ms == 0U) ? 0U :
                                   (uint32_t)(now_ms - quick_save_elapsed_ms);
      if((residual_norm <= (CAL_GYR_REST_DPS * DEG2RAD)) &&
         (fabsf(acc_norm - G_TO_MS2) <= CAL_ACC_REST_MS2))
      {
        quick_rest_ms += elapsed_ms;
        quick_bias_sum[0] += gyr[0];
        quick_bias_sum[1] += gyr[1];
        quick_bias_sum[2] += gyr[2];
        quick_samples++;
        if(quick_rest_ms >= APP_GYR_FAST_START_REST_MS && quick_samples != 0U)
        {
          const float measured[3] = {
            quick_bias_sum[0] / (float)quick_samples,
            quick_bias_sum[1] / (float)quick_samples,
            quick_bias_sum[2] / (float)quick_samples
          };
          const float alpha = APP_GYR_FAST_START_BLEND;
          quick_bias[0] += alpha * (measured[0] - quick_bias[0]);
          quick_bias[1] += alpha * (measured[1] - quick_bias[1]);
          quick_bias[2] += alpha * (measured[2] - quick_bias[2]);
          vqf_set_gyr_bias(quick_bias);
          if((quick_rest_ms >= APP_GYR_FAST_START_SAVE_MS) && (quick_bias_saved == 0U))
          {
            const float delta = fabsf(quick_bias[0] - quick_bias_initial[0]) +
                                fabsf(quick_bias[1] - quick_bias_initial[1]) +
                                fabsf(quick_bias[2] - quick_bias_initial[2]);
            if((delta > (APP_GYR_FAST_START_SAVE_DELTA_DPS * DEG2RAD)) ||
               (fabsf(temp_c - quick_bias_temp_c) > APP_GYR_FAST_START_SAVE_TEMP_C))
            {
              if(gyro_bias_history_save_at_temp(quick_bias, temp_c) != 0)
                bias_history_write_error = 1U;
              else
                quick_bias_saved = 1U;
            }
            else
            {
              quick_bias_saved = 1U;
            }
          }
        }
      }
      else
      {
        quick_rest_ms = 0U;
        quick_samples = 0U;
        quick_bias_sum[0] = quick_bias_sum[1] = quick_bias_sum[2] = 0.0f;
      }
      quick_save_elapsed_ms = now_ms;
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

    if((fusion_n % app_output_div) == 0U)
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
      vqf_live.skip_n = skip_n;
      vqf_live.millis = millis();
      __DMB();
      vqf_live.seq++;
      out_n++;

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
      vofa_send_justfloat((float)vofa_late);
    }

    usb_cdc_task();
    {
      uint8_t ch;
      static uint8_t uart_legacy_buf[4];
      static uint8_t uart_legacy_idx;
      static uint8_t usb_legacy_buf[4];
      static uint8_t usb_legacy_idx;
      while(uart_read_byte(&ch))
      {
        protocol_parser_feed_byte(&uart_protocol_parser, ch);
        legacy_command_feed(ch, uart_legacy_buf, &uart_legacy_idx);
      }
      while(usb_cdc_read_byte(&ch))
      {
        protocol_parser_feed_byte(&usb_protocol_parser, ch);
        legacy_command_feed(ch, usb_legacy_buf, &usb_legacy_idx);
      }
    }
    if(protocol_reset_pending != 0U)
    {
      protocol_reset_pending = 0U;
      app_request_bootloader();
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
      if(app_settings_mode != 0U)
      {
        /* Settings mode has priority over all status warnings and freezes
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
    can_test_task(dwt_cycles() / (system_core_clock / 1000000U));
    {
      uint8_t cmd = can_test_get_cmd_flag();
      if(cmd & CAN_CMD_FLAG_REBOOT)
      {
        nvic_system_reset();
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
      fusion_hz = fusion_n;
      vqf_live.fusion_hz = fusion_hz;
      vqf_live.out_hz = out_n;
      vqf_live.clk_hz = system_core_clock;
      fusion_n = 0;
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
      (void)lsm6dsv_init_2khz();
      live_whoami();
      err_streak = 0U;
    }
  }
  }
}
