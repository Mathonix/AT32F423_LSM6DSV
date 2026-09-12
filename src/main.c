/**
 * AT32F423KCU7-4 + LSM6DSV
 *   IMU HA01 2000 Hz, BasicVQF 2 kHz, 1 kHz snapshot in vqf_live (DAP)
 */

#include "at32f423_clock.h"
#include "bsp.h"
#include "lsm6dsv.h"
#include "vqf.h"
#include "vqf_live.h"

#include <math.h>
#include <string.h>

#define FUSION_HZ            2000.0f
#define OUTPUT_DIV           2U

#define GYR_DPS_PER_LSB      0.070f
#define ACC_G_PER_LSB        0.000122f
#define DEG2RAD              0.017453292519943295f
#define G_TO_MS2             9.80665f
#define SAMPLE_DT_CYCLES     ((uint32_t)(system_core_clock / (uint32_t)FUSION_HZ))
#define DROP_DT_CYCLES       ((SAMPLE_DT_CYCLES * 3U) / 2U)
#define CAL_GYR_REST_DPS     2.0f
#define CAL_ACC_REST_MS2     1.5f
#define ERR_STREAK_RECOVER   20U

volatile vqf_live_t vqf_live;

#define VOFA_N_CH   16U
#define VOFA_BYTES  ((VOFA_N_CH * 4U) + 4U)

static uint8_t vofa_dma[2][VOFA_BYTES];
static uint8_t vofa_sel;
static uint32_t vofa_late;

static void vofa_send_justfloat(float late)
{
  static const uint8_t tail[4] = {0x00U, 0x00U, 0x80U, 0x7FU};
  float ch[VOFA_N_CH];
  uint8_t *pkt = vofa_dma[vofa_sel];

  ch[0]  = vqf_live.roll;
  ch[1]  = vqf_live.pitch;
  ch[2]  = vqf_live.yaw;
  ch[3]  = vqf_live.qw;
  ch[4]  = vqf_live.qx;
  ch[5]  = vqf_live.qy;
  ch[6]  = vqf_live.qz;
  ch[7]  = vqf_live.gx;
  ch[8]  = vqf_live.gy;
  ch[9]  = vqf_live.gz;
  ch[10] = vqf_live.ax;
  ch[11] = vqf_live.ay;
  ch[12] = vqf_live.az;
  ch[13] = (float)vqf_live.vqf_us;
  ch[14] = (float)vqf_live.fusion_hz;
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
  uint32_t t0;
  uint32_t vqf_us;
  uint32_t i;

  system_clock_config();
  bsp_init();
  live_init();
  lsm6dsv_spi_init();
  delay_ms(20);

  live_whoami();
  err = lsm6dsv_init_2khz();
  live_whoami();
  if(err != 0)
  {
    fail_loop(err);
  }

  vqf_init(1.0f / FUSION_HZ, 1.0f / FUSION_HZ);
  {
    float gyr_sum[3] = {0.0f, 0.0f, 0.0f};
    float acc_sum[3] = {0.0f, 0.0f, 0.0f};
    float gyr_bias[3];
    float acc_avg[3];
    uint32_t drop_n = (uint32_t)FUSION_HZ / 5U;
    uint32_t cal_n = (uint32_t)FUSION_HZ;
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
        acc[i] = (float)raw.acc[i] * ACC_G_PER_LSB * G_TO_MS2;
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
    acc_avg[0] = acc_sum[0] / (float)rest_n;
    acc_avg[1] = acc_sum[1] / (float)rest_n;
    acc_avg[2] = acc_sum[2] / (float)rest_n;
    vqf_prime_rest(acc_avg, gyr_bias);
  }
  vqf_live.seq = 1;
  last_ms = millis();

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
      acc[i] = (float)raw.acc[i] * ACC_G_PER_LSB * G_TO_MS2;
    }

    t0 = dwt_cycles();
    vqf_update(gyr, acc);
    vqf_us = (dwt_cycles() - t0) / (system_core_clock / 1000000U);
    fusion_n++;

    if((fusion_n % OUTPUT_DIV) == 0U)
    {
      vqf_get_quat6d(q);
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
      vqf_live.vqf_us = vqf_us;
      vqf_live.fusion_n = fusion_n;
      vqf_live.skip_n = skip_n;
      vqf_live.millis = millis();
      vqf_live.seq++;
      out_n++;
      vofa_send_justfloat((float)vofa_late);
    }

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
