#ifndef VQF_STATIC_CAL_H
#define VQF_STATIC_CAL_H

#include <stdint.h>

/* Wire values for message 0x0D. Do not renumber them. */
#define VQF_STATIC_CAL_IDLE         0U
#define VQF_STATIC_CAL_PRECHECK     1U
#define VQF_STATIC_CAL_PRE_STABLE   2U
#define VQF_STATIC_CAL_COLLECTING   3U
#define VQF_STATIC_CAL_VALIDATING   4U
#define VQF_STATIC_CAL_DONE         5U
#define VQF_STATIC_CAL_FAILED       6U

#define VQF_STATIC_CAL_OK                 0U
#define VQF_STATIC_CAL_ERR_MOVED          1U
#define VQF_STATIC_CAL_ERR_GYRO_NOISE     2U
#define VQF_STATIC_CAL_ERR_ACC_NOISE      3U
#define VQF_STATIC_CAL_ERR_BIAS_RANGE     4U
#define VQF_STATIC_CAL_ERR_TEMP_DRIFT     5U
#define VQF_STATIC_CAL_ERR_BIAS_DRIFT     6U
#define VQF_STATIC_CAL_ERR_SAMPLE_COUNT   7U
#define VQF_STATIC_CAL_ERR_INVALID_NUMBER 8U
#define VQF_STATIC_CAL_ERR_FLASH_WRITE    9U

#define VQF_STATIC_CAL_SOURCE_DEFAULT 0U
#define VQF_STATIC_CAL_SOURCE_CAL     1U

#define VQF_STATIC_CAL_MAGIC   0x46535156U /* 'VQSF' little-endian */
#define VQF_STATIC_CAL_VERSION 1U

typedef struct {
  float gyro_bias_dps[3];
  float acc_mean_ms2[3];
  float bias_sigma_init_dps;
  float bias_sigma_rest_dps;
  float rest_th_gyr_dps;
  float rest_th_acc_ms2;
  float calibration_temp_c;
} vqf_static_params_t;

typedef struct {
  uint8_t state;
  uint8_t error;
  uint8_t source;
  uint32_t elapsed_ms;
  uint32_t remaining_ms;
  uint32_t sample_count;
  float gyro_rate_dps;
  float acc_deviation_ms2;
  float temperature_c;
} vqf_static_cal_status_t;

/* Separate from device settings. 124 bytes, no padding, one sector A/B pair. */
typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t sequence;
  uint32_t valid;
  float gyro_bias_dps[3];
  float acc_mean_ms2[3];
  float bias_sigma_init_dps;
  float bias_sigma_rest_dps;
  float rest_th_gyr_dps;
  float rest_th_acc_ms2;
  float calibration_temp_c;
  float gyro_std_dps[3];
  float acc_std_ms2[3];
  float gyro_dev_p95_dps;
  float gyro_dev_p99_dps;
  float acc_dev_p95_ms2;
  float acc_dev_p99_ms2;
  float temp_start_c;
  float temp_end_c;
  float temp_mean_c;
  uint32_t sample_count;
  uint32_t flags;
  uint32_t crc32;
} vqf_static_record_t;

void vqf_static_cal_defaults(vqf_static_params_t *out);
/* 0 when a valid static-calibration record was loaded. -1 fills compiled defaults. */
int vqf_static_cal_load(vqf_static_params_t *out);
/* 0 started, -1 already active. Does not change the running parameters. */
int vqf_static_cal_start(uint32_t now_ms);
void vqf_static_cal_cancel(void);
/* 0 invalidated, -1 active, -2 flash failed and the live source is unchanged. */
int vqf_static_cal_restore_defaults(void);
int vqf_static_cal_active(void);
uint8_t vqf_static_cal_source(void);
void vqf_static_cal_get_applied(vqf_static_params_t *out);
void vqf_static_cal_feed(uint32_t now_ms, float dt_s,
                         const float gyr_dps[3], const float acc_ms2[3], float temp_c);
void vqf_static_cal_tick(uint32_t now_ms);
void vqf_static_cal_get_status(vqf_static_cal_status_t *out);
/* 1 once, when a run reaches done or failed. Flash is written here, not in feed. */
int vqf_static_cal_poll(vqf_static_cal_status_t *status, vqf_static_params_t *params);

#endif
