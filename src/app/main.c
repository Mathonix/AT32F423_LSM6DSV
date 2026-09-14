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
#include "can_test.h"
#include "usb_cdc.h"

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

#define VOFA_N_CH   3U
#define VOFA_BYTES  ((VOFA_N_CH * 4U) + 4U)

static uint8_t vofa_dma[2][VOFA_BYTES];
static uint8_t vofa_sel;
static uint32_t vofa_late;

static float wrap_deg(float angle)
{
  while(angle > 180.0f) angle -= 360.0f;
  while(angle < -180.0f) angle += 360.0f;
  return angle;
}

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
  static const uint8_t tail[4] = {0x00U, 0x00U, 0x80U, 0x7FU};
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
  const float nominal_dt = ((float)OUTPUT_DIV / FUSION_HZ);
  float dt = nominal_dt;
  float ch[VOFA_N_CH];
  float output_yaw;
  float output_pitch;
  float output_roll;
  uint8_t *pkt = vofa_dma[vofa_sel];

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

  /* Send only yaw, pitch and roll as a continuous 3-channel JustFloat frame. */
  ch[0]  = output_yaw;
  ch[1]  = output_pitch;
  ch[2]  = output_roll;
  memcpy(pkt, ch, VOFA_N_CH * 4U);
  memcpy(pkt + (VOFA_N_CH * 4U), tail, 4U);
  (void)usb_cdc_write(pkt, (uint16_t)VOFA_BYTES);
  if(uart_dma_send(pkt, (uint16_t)VOFA_BYTES) == 0)
  {
    vofa_sel ^= 1U;
  }
  else
  {
    vofa_late++;
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

static void fail_loop(int err)
{
  vqf_live.init_err = err;
  ws2812_show_error(1U);
  while(1)
  {
    live_whoami();
    vqf_live.millis = millis();
    vqf_live.seq++;
    led_toggle();
    delay_ms(200);
  }
}

int main(void)
{
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
  float temp_c;
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
#if APP_CAN_ENABLE
  can_test_init();
#endif
  usb_cdc_init();
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
    fail_loop(err);
  }

  mag_err = ist8310_init();
  mag_ok = (mag_err == 0) ? 1U : 0U;
  mag_pending = 0U;
  mag_start_n = 0U;
  mag_updates = 0U;
  mag_read_n = 0U;
  vqf_live.mag_err = mag_err;
  vqf_live.mag_addr = ist8310_get_addr();

  vqf_init(1.0f / FUSION_HZ, 1.0f / FUSION_HZ);
  {
    float block_gyr_sum[3] = {0.0f, 0.0f, 0.0f};
    float block_acc_sum[3] = {0.0f, 0.0f, 0.0f};
    float gyr_bias[3];
    float acc_avg[3];
    uint32_t block_samples = 0U;
    uint32_t block_count = 0U;
    /* Discard samples for the configured settling time.  This used to be
     * hard-coded to FUSION_HZ/5 (200 ms), so APP_CAL_DROP_MS had no effect. */
    uint32_t drop_n = ((FUSION_RATE_HZ * (uint32_t)CAL_DROP_MS) + 999U) /
                      1000U;
    uint32_t cal_n = (uint32_t)((float)FUSION_RATE_HZ * CAL_REST_SECONDS + 0.5f);
    uint32_t target_blocks;
    uint32_t target_samples;
    uint32_t tries;
    uint32_t got = 0U;
    float acc_n3;
    float gyr_n;
    float temp_c;

    if(CAL_BLOCK_SAMPLES == 0U || CAL_BLOCK_MAX == 0U)
    {
      fail_loop(-21);
    }
    target_blocks = (cal_n + CAL_BLOCK_SAMPLES - 1U) / CAL_BLOCK_SAMPLES;
    if(target_blocks == 0U) target_blocks = 1U;
    if(target_blocks > CAL_BLOCK_MAX) target_blocks = CAL_BLOCK_MAX;
    target_samples = target_blocks * CAL_BLOCK_SAMPLES;

    tries = 0U;
    while((got < drop_n) && (tries < (drop_n * 4U)))
    {
      tries++;
      if(lsm6dsv_wait_sample(2000U) != 0)
      {
        continue;
      }
      if(lsm6dsv_read_raw(&raw) != 0)
      {
        continue;
      }
      got++;
      (void)update_temperature(raw.temp_raw);
    }

    tries = 0U;
    while((block_count < target_blocks) &&
          (tries < (target_samples * 4U)))
    {
      tries++;
      if(lsm6dsv_wait_sample(2000U) != 0)
      {
        continue;
      }
      if(lsm6dsv_read_raw(&raw) != 0)
      {
        continue;
      }
      temp_c = update_temperature(raw.temp_raw);
      for(i = 0; i < 3U; i++)
      {
        gyr[i] = gyro_dps_from_raw(i, raw.gyr[i], temp_c) * DEG2RAD;
        acc[i] = acc_calibrate_g(i, (float)raw.acc[i] * ACC_G_PER_LSB) * G_TO_MS2;
      }
      gyr_n = sqrtf(gyr[0] * gyr[0] + gyr[1] * gyr[1] + gyr[2] * gyr[2]);
      acc_n3 = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
      if((gyr_n > (CAL_GYR_REST_DPS * DEG2RAD)) ||
         (fabsf(acc_n3 - G_TO_MS2) > CAL_ACC_REST_MS2))
      {
        continue;
      }
      for(i = 0U; i < 3U; ++i)
      {
        block_gyr_sum[i] += gyr[i];
        block_acc_sum[i] += acc[i];
      }
      block_samples++;
      if(block_samples >= CAL_BLOCK_SAMPLES)
      {
        for(i = 0U; i < 3U; ++i)
        {
          cal_gyr_blocks[i][block_count] =
              block_gyr_sum[i] / (float)CAL_BLOCK_SAMPLES;
          cal_acc_blocks[i][block_count] =
              block_acc_sum[i] / (float)CAL_BLOCK_SAMPLES;
          block_gyr_sum[i] = 0.0f;
          block_acc_sum[i] = 0.0f;
        }
        block_samples = 0U;
        block_count++;
      }
    }
    if(block_count < target_blocks)
    {
      fail_loop(-20);
    }
    /* Trim 10% of the block means at both ends for each axis.  This is more
     * robust than trimming individual samples: one knock cannot dominate a
     * 32-sample block, while a short motion interval is discarded entirely
     * when it lands in the tails. */
    for(i = 0U; i < 3U; ++i)
    {
      gyr_bias[i] = trimmed_mean(cal_gyr_blocks[i], block_count);
      acc_avg[i] = trimmed_mean(cal_acc_blocks[i], block_count);
    }
#if APP_SFLP_BIAS_ENABLE
    if((sflp_bias_ok != 0U) &&
       (fabsf(sflp_bias_dps[0]) <= APP_SFLP_BIAS_MAX_DPS) &&
       (fabsf(sflp_bias_dps[1]) <= APP_SFLP_BIAS_MAX_DPS) &&
       (fabsf(sflp_bias_dps[2]) <= APP_SFLP_BIAS_MAX_DPS))
    {
      gyr_bias[0] = sflp_bias_dps[0] * DEG2RAD;
      gyr_bias[1] = sflp_bias_dps[1] * DEG2RAD;
      gyr_bias[2] = sflp_bias_dps[2] * DEG2RAD;
    }
#endif
    vqf_prime_rest(acc_avg, gyr_bias);
  }
  vqf_live.seq = 2;
  last_ms = millis();
  if(!mag_ok) ws2812_show_error(2U);

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
      acc[i] = acc_calibrate_g(i, (float)raw.acc[i] * ACC_G_PER_LSB) * G_TO_MS2;
    }

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

    if((fusion_n % OUTPUT_DIV) == 0U)
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
    if(mag_ok) ws2812_normal_task(millis());
#if APP_CAN_ENABLE
    can_test_task(millis());
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













