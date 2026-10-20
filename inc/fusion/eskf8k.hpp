#ifndef AHRS_ESKF8K_HPP
#define AHRS_ESKF8K_HPP

namespace ahrs_eskf8k {
/* Independent three-state right-error quaternion ESKF.
 * Startup bias is owned by vqf_wrapper; this filter estimates only residual
 * stationary gyro bias. All angular arguments and returned bias are rad/s. */
void init();
void prime(const float acc_ms2[3]);
void gyro(const float corrected_gyro_rad_s[3], float dt_s);
void accel(const float acc_ms2[3]); /* called at 2 kHz, observes at 200 Hz */
int mag(const float mag_ut[3]);    /* new 10 Hz observations only */
void quat6(float q[4]);
void quat9(float q[4]);
void bias(float b[3]);
float bias_sigma_dps();
int rest();
#if APP_ESKF_REST_RELAX
void rest_thresholds(float out[2]); /* effective gyro dps / acceleration m/s2 */
#endif
float rest_time();
int mag_ready();
int mag_disturbed();
float mag_delta_deg();
void reset_mag_reference();
void set_rest_bias_enabled(bool enabled);
void set_mag_rejection(bool enabled);
void diagnostic(float out[8]);
unsigned health(unsigned filter_index);
void state_snapshot(float out[26]); /* q4 + P9 for each independent state */
}
extern "C" {
extern volatile unsigned eskf8k_counts[8];
}
#endif
