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

#include <math.h>
#include <string.h>

#define FUSION_HZ            2000.0f
#define OUTPUT_DIV           2U
#define DAP_OUTPUT_DIV       100U /* coherent 20 Hz snapshot for DAPLink */

#define GYR_DPS_PER_LSB      0.070f
#define ACC_G_PER_LSB        0.000122f
#define DEG2RAD              0.017453292519943295f
#define G_TO_MS2             9.80665f
#define SAMPLE_DT_CYCLES     ((uint32_t)(system_core_clock / (uint32_t)FUSION_HZ))
#define DROP_DT_CYCLES       ((SAMPLE_DT_CYCLES * 3U) / 2U)
#define CAL_GYR_REST_DPS APP_CAL_GYR_REST_DPS
#define CAL_ACC_REST_MS2 APP_CAL_ACC_REST_MS2
#define CAL_REST_SECONDS APP_CAL_REST_SECONDS   /* startup stationary bias calibration */
#define GYR_LPF_CUTOFF_HZ APP_GYR_LPF_CUTOFF_HZ /* reduce gyro noise before integration */
#define ERR_STREAK_RECOVER   20U
#define MAG_PERIOD_N         40U /* read IST8310 at 50 Hz */
#define MAG_VQF_DIV          5U  /* Full VQF magnetic update at 10 Hz */
#define MAG_FUSION_ENABLE   0U  /* 6D mode; retain calibrated/aligned magnetometer telemetry only */
#define MAG_TIMEOUT_N        30U
#define MAG_UT_PER_LSB       0.3f
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
/* Separate ABI: preserves all existing tune/VOFA layouts. */
volatile struct {
  uint32_t magic, seq;
  vqf_tune_live_t snapshot;
  float diagnostic[8]; /* yaw6, refNorm, refDipDeg, rejectT, candidateT,
                       * corrRateDegS, disagreementDeg, biasSigmaDegS */
} vqf_nine_live = {0x39565146U, 0U, {0}, {0}};

#define VOFA_N_CH   16U
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

static void vofa_send_justfloat(float late)
{
  static const uint8_t tail[4] = {0x00U, 0x00U, 0x80U, 0x7FU};
  static uint8_t filter_init;
  static float yaw_kf;
  static float yaw_kf_var;
  const float dt = ((float)OUTPUT_DIV / FUSION_HZ);
  float ch[VOFA_N_CH];
  uint8_t *pkt = vofa_dma[vofa_sel];

  /* Output-only adaptive 1D angle Kalman filter. VQF continues to calculate
   * its normal yaw; this filter only affects the VOFA/DAP output pose ABI.
   * At rest, use a small Q / larger R to suppress jitter. During motion,
   * increase Q and trust the fresh VQF measurement so the output responds
   * without a noticeable lag. */
  {
    float yaw_rate = vqf_live.gz - vqf_live.bias_z;
    float gx = vqf_live.gx - vqf_live.bias_x;
    float gy = vqf_live.gy - vqf_live.bias_y;
    float gz = yaw_rate;
    float rate_norm = sqrtf(gx * gx + gy * gy + gz * gz);
    float motion_den = APP_VOFA_YAW_KF_MOTION_FULL_DPS -
                       APP_VOFA_YAW_KF_MOTION_START_DPS;
    float motion = (rate_norm - APP_VOFA_YAW_KF_MOTION_START_DPS) / motion_den;
    float yaw_q;
    float yaw_r;

    if(motion < 0.0f) motion = 0.0f;
    if(motion > 1.0f) motion = 1.0f;
    /* Smooth the transition so Q/R do not jump at the threshold. */
    motion = motion * motion * (3.0f - 2.0f * motion);
    if(vqf_live.rest_detected != 0U && rate_norm < APP_VOFA_YAW_KF_MOTION_START_DPS)
      motion = 0.0f;

    yaw_q = APP_VOFA_YAW_KF_Q_REST_DEG2_PER_S +
            (APP_VOFA_YAW_KF_Q_MOVE_DEG2_PER_S -
             APP_VOFA_YAW_KF_Q_REST_DEG2_PER_S) * motion;
    yaw_r = APP_VOFA_YAW_KF_R_REST_DEG2 +
            (APP_VOFA_YAW_KF_R_MOVE_DEG2 -
             APP_VOFA_YAW_KF_R_REST_DEG2) * motion;

    if(!filter_init)
    {
      yaw_kf = vqf_live.yaw;
      yaw_kf_var = yaw_r;
      filter_init = 1U;
    }
    else
    {
      /* Do not integrate residual gyro noise while stationary. At rest the
       * state is held and only slowly corrected by the yaw measurement. Once
       * the measured angular rate crosses the motion threshold, use gyro
       * propagation for prompt response. */
      float yaw_pred = yaw_kf;
      if(rate_norm >= APP_VOFA_YAW_KF_MOTION_START_DPS)
        yaw_pred = wrap_deg(yaw_kf + yaw_rate * dt);
      float p_pred = yaw_kf_var + yaw_q * dt;
      float innovation = wrap_deg(vqf_live.yaw - yaw_pred);
      float k = p_pred / (p_pred + yaw_r);
      yaw_kf = wrap_deg(yaw_pred + k * innovation);
      yaw_kf_var = (1.0f - k) * p_pred;
    }
  }

  vofa_pose_live.seq++;
  __DMB();
  vofa_pose_live.yaw = yaw_kf;
  vofa_pose_live.pitch = vqf_live.pitch;
  vofa_pose_live.roll = vqf_live.roll;
  vofa_pose_live.temperature_c = imu_temp_live.temperature_c;
  vofa_pose_live.millis = millis();
  __DMB();
  vofa_pose_live.seq++;

  /* VOFA channels 0..3: yaw(KF), pitch(raw VQF), roll(raw VQF), temperature(raw). */
  ch[0]  = yaw_kf;
  ch[1]  = vqf_live.pitch;
  ch[2]  = vqf_live.roll;
  ch[3]  = imu_temp_live.temperature_c;
  ch[4]  = vqf_live.qw;
  ch[5]  = vqf_live.qx;
  ch[6]  = vqf_live.qy;
  ch[7]  = vqf_live.qz;
  ch[8]  = vqf_live.gx;
  ch[9]  = vqf_live.gy;
  ch[10] = vqf_live.gz;
  ch[11] = vqf_live.ax;
  ch[12] = vqf_live.ay;
  ch[13] = vqf_live.az;
  ch[14] = (float)vqf_live.vqf_us;
  ch[15] = late;
  memcpy(pkt, ch, VOFA_N_CH * 4U);
  memcpy(pkt + (VOFA_N_CH * 4U), tail, 4U);
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
    float gyr_sum[3] = {0.0f, 0.0f, 0.0f};
    float acc_sum[3] = {0.0f, 0.0f, 0.0f};
    float gyr_bias[3];
    float acc_avg[3];
    uint32_t drop_n = (uint32_t)FUSION_HZ / 5U;
    uint32_t cal_n = (uint32_t)FUSION_HZ * CAL_REST_SECONDS;
    uint32_t tries;
    uint32_t got = 0U;
    uint32_t rest_n = 0U;
    float acc_n3;
    float gyr_n;

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
    }

    tries = 0U;
    got = 0U;
    while((rest_n < cal_n) && (tries < (cal_n * 4U)))
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
      for(i = 0; i < 3U; i++)
      {
        gyr[i] = (float)raw.gyr[i] * GYR_DPS_PER_LSB * DEG2RAD;
        acc[i] = acc_calibrate_g(i, (float)raw.acc[i] * ACC_G_PER_LSB) * G_TO_MS2;
      }
      gyr_n = sqrtf(gyr[0] * gyr[0] + gyr[1] * gyr[1] + gyr[2] * gyr[2]);
      acc_n3 = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
      if((gyr_n > (CAL_GYR_REST_DPS * DEG2RAD)) ||
         (fabsf(acc_n3 - G_TO_MS2) > CAL_ACC_REST_MS2))
      {
        continue;
      }
      gyr_sum[0] += gyr[0];
      gyr_sum[1] += gyr[1];
      gyr_sum[2] += gyr[2];
      acc_sum[0] += acc[0];
      acc_sum[1] += acc[1];
      acc_sum[2] += acc[2];
      rest_n++;
    }
    if(rest_n < (cal_n / 4U))
    {
      fail_loop(-20);
    }
    gyr_bias[0] = gyr_sum[0] / (float)rest_n;
    gyr_bias[1] = gyr_sum[1] / (float)rest_n;
    gyr_bias[2] = gyr_sum[2] / (float)rest_n;
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
    acc_avg[0] = acc_sum[0] / (float)rest_n;
    acc_avg[1] = acc_sum[1] / (float)rest_n;
    acc_avg[2] = acc_sum[2] / (float)rest_n;
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

    /* Temperature is part of the same 14-byte SPI burst as gyro/accel. */
    imu_temp_live.seq++;
    __DMB();
    imu_temp_live.raw_temp = raw.temp_raw;
    imu_temp_live.temperature_c = 25.0f + ((float)raw.temp_raw / 256.0f);
    imu_temp_live.millis = millis();
    __DMB();
    imu_temp_live.seq++;

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
      gyr[i] = (float)raw.gyr[i] * GYR_DPS_PER_LSB * DEG2RAD;
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

    if(mag_ok) ws2812_normal_task(millis());

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









