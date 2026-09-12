#ifndef VQF_H
#define VQF_H
#ifdef __cplusplus
extern "C" {
#endif
void vqf_init(float gyr_dt, float acc_dt);
void vqf_set_tau_acc(float tau_acc);
void vqf_set_tau_mag(float tau_mag);
void vqf_set_gyr_bias(const float gyr_bias[3]);
void vqf_prime_rest(const float acc_ms2[3], const float gyr_bias[3]);
void vqf_update(const float gyr[3], const float acc[3]);
void vqf_update_gyr(const float gyr[3]);
void vqf_update_acc(const float acc[3]);
int vqf_update_mag(const float mag[3]);
void vqf_get_quat6d(float q[4]);
void vqf_get_quat9d(float q[4]);
void vqf_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg);
void vqf_get_gyr_bias(float gyr_bias[3]);
float vqf_get_rest_time(void);
int vqf_get_rest_detected(void);
float vqf_get_tau_acc(void);
float vqf_get_tau_mag(void);
int vqf_get_mag_ready(void);
int vqf_get_mag_dist_detected(void);
#ifdef __cplusplus
}
#endif
#endif
