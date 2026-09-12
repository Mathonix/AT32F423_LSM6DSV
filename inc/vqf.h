/**
 * BasicVQF 6-axis (gyro + accel) orientation filter.
 * Laidig & Seel, Information Fusion 2023. https://github.com/dlaidig/vqf
 *
 * Quaternion is Hamilton [w, x, y, z]. Gravity is +Z in the earth frame.
 */

#ifndef VQF_H
#define VQF_H

#ifdef __cplusplus
extern "C" {
#endif

void vqf_init(float gyr_dt, float acc_dt);
void vqf_set_tau_acc(float tau_acc);
void vqf_set_gyr_bias(const float gyr_bias[3]);
void vqf_prime_rest(const float acc_ms2[3], const float gyr_bias[3]);
void vqf_update(const float gyr[3], const float acc[3]);
void vqf_update_gyr(const float gyr[3]);
void vqf_update_acc(const float acc[3]);
void vqf_get_quat6d(float q[4]);
void vqf_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg);

#ifdef __cplusplus
}
#endif

#endif
