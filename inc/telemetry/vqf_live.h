#ifndef VQF_LIVE_H
#define VQF_LIVE_H

#include <stdint.h>

#define VQF_LIVE_MAGIC 0x56465131u /* "VQF1" */
#define VQF_TUNE_MAGIC 0x56514654u /* "VQFT" */
#define YAW_KF_SYNC_MAGIC 0x594B4631u /* "YKF1" */

typedef struct
{
  uint32_t magic;
  uint32_t seq;
  int32_t  init_err;
  uint32_t whoami;
  uint32_t clk_hz;
  uint32_t millis;
  uint32_t fusion_hz;
  uint32_t out_hz;
  uint32_t fusion_n;
  uint32_t skip_n;
  uint32_t vqf_us;
  float roll;
  float pitch;
  float yaw;
  float qw;
  float qx;
  float qy;
  float qz;
  float gx;
  float gy;
  float gz;
  float ax;
  float ay;
  float az;
  float bias_x;
  float bias_y;
  float bias_z;
  float rest_time;
  float tau_acc;
  uint32_t rest_detected;
  float mx;
  float my;
  float mz;
  float mag_norm;
  float tau_mag;
  int32_t mag_err;
  uint32_t mag_addr;
  uint32_t mag_updates;
  uint32_t mag_ready;
  uint32_t mag_disturbed;
  /* Appended fields: preserve the address of every legacy field above. */
  float temperature_c;
  float gyr_lpf_z;
  float corrected_z;
} vqf_live_t;

typedef struct
{
  uint32_t magic;
  uint32_t seq;
  uint32_t millis;
  uint32_t fusion_hz;
  uint32_t skip_n;
  uint32_t rest_detected;
  float roll;
  float pitch;
  float yaw;
  float gx;
  float gy;
  float gz;
  float ax;
  float ay;
  float az;
  float bias_x;
  float bias_y;
  float bias_z;
  float rest_time;
  float tau_acc;
  float mx;
  float my;
  float mz;
  float mag_norm;
  float tau_mag;
  int32_t mag_err;
  uint32_t mag_addr;
  uint32_t mag_updates;
  uint32_t mag_ready;
  uint32_t mag_disturbed;
  /* Appended fields: preserve the address of every legacy field above. */
  float temperature_c;
  float gyr_lpf_z;
  float corrected_z;
} vqf_tune_live_t;

/* Compact synchronized output snapshot for high-rate DAPLink logging. */
typedef struct
{
  uint32_t magic;
  uint32_t seq;
  uint32_t millis;
  float vqf_yaw;
  float kf_yaw;
  float gz;
  float bias_z;
  uint32_t rest_detected;
  uint32_t mag_updates;
  float temperature_c;
  float gyr_lpf_z;
  float corrected_z;
} yaw_kf_sync_live_t;

extern volatile vqf_live_t vqf_live;
extern volatile vqf_tune_live_t vqf_tune_live;
extern volatile yaw_kf_sync_live_t yaw_kf_sync_live;

#endif
