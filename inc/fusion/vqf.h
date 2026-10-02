#ifndef VQF_H
#define VQF_H
#include <stdint.h>
/* C interface implemented by vqf_wrapper.cpp over the official Full VQF. */
#ifdef __cplusplus
extern "C" {
#endif
void vqf_init(float gyr_dt, float acc_dt);
void vqf_apply_profile(unsigned profile);
void vqf_set_tau_acc(float tau_acc);
void vqf_set_tau_mag(float tau_mag);
void vqf_set_rest_thresholds(float th_gyr_dps, float th_acc_ms2);
void vqf_set_bias_sigmas(float sigma_init_dps, float sigma_rest_dps);
void vqf_set_gyr_bias(const float gyr_bias[3]);
void vqf_seed_gyr_bias(const float gyr_bias[3], float sigma_dps);
float vqf_get_bias_sigma_dps(void);
void vqf_prime_rest(const float acc_ms2[3], const float gyr_bias[3]);
void vqf_update(const float gyr[3], const float acc[3]);
void vqf_update_gyr(const float gyr[3]);
void vqf_update_acc(const float acc[3]);
int vqf_update_mag(const float mag[3]);
void vqf_get_quat6d(float q[4]);
void vqf_get_quat9d(float q[4]);
void vqf_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg);
void vqf_get_gyr_bias(float gyr_bias[3]);
typedef struct {
  uint8_t motion_bias_enabled;
  uint8_t rest_bias_enabled;
  float bias_sigma_motion_dps;
  float bias_vertical_forgetting;
  float bias_forgetting_time_s;
  float bias_clip_dps;
  float bias_sigma_rest_dps;
  float bias_sigma_init_dps;
  float tau_acc_s;
} vqf_bias_estimator_config_t;
/* Live constructor parameters. All zeros when the filter is not initialized. */
void vqf_get_bias_estimator_config(vqf_bias_estimator_config_t *out);
void vqf_get_nine_diagnostic(float out[8]);
float vqf_get_rest_time(void);
int vqf_get_rest_detected(void);
float vqf_get_tau_acc(void);
float vqf_get_tau_mag(void);
int vqf_get_mag_ready(void);
float vqf_get_mag_delta_deg(void);
int vqf_get_mag_dist_detected(void);
#ifdef __cplusplus
}
#endif
#endif
