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
int mag(const float mag_ut[3]);    /* new observations only; profile selects rate */
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
#if APP_ESKF_REST_CONTROL || defined(APP_REST_BIAS_TEST_ACCESS)
bool rest_bias_enabled(); /* actual EMA consumer, independent of request cache */
unsigned rest_bias_gate_flags(); /* bit0 ready(1s), bit1 last gate, bit2 EMA eligible */
#endif
void set_mag_rejection(bool enabled);
#if APP_ESKF_REFERENCE
void set_magnetic_fusion(bool enabled); // Single state; no reset and no Flash.
bool magnetic_fusion_enabled();
#endif
void diagnostic(float out[8]);
unsigned health(unsigned filter_index);
void state_snapshot(float out[26]); /* q4 + P9 for each independent state */
}
extern "C" {
extern volatile unsigned eskf8k_counts[8];
}
#endif
