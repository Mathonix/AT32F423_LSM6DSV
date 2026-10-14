#ifndef AHRS_EKF_HPP
#define AHRS_EKF_HPP
#include <stdint.h>
#include "vqf.h"

// Body-to-world Hamilton quaternion, right multiplicative attitude error.
// State: quaternion and residual gyro bias; covariance: [dtheta, dbias].
// Separate 6D and 9D estimators keep the magnetometer out of the 6D output.
namespace ahrs_ekf {
// P=root*root', lower triangular. Square-root Joseph/prediction use the
// single-precision FPU without subtracting almost equal covariance entries.
// p is a double read-only snapshot for native regression getters, not state
// used by the runtime estimator (no double arithmetic in the sensor loop).
struct State {
  float q[4], bias[3];
  float root[6][6];
  double p[6][6];
  uint32_t covariance_recoveries, bias_recoveries;
};
void init(float gyr_dt, float acc_dt);
void set_profile(float tau_acc, float tau_mag, float rest_gyr_dps, float rest_acc);
void set_bias_sigmas(float init_dps, float rest_dps);
void set_rest_bias_enabled(bool enabled);
void seed_bias(float sigma_dps);
void prime(const float acc[3]);
void gyro(const float gyr[3]);
void accel(const float acc[3]);
int mag(const float field[3]);
void set_mag_rejection(bool enabled);
void reset_mag_reference();
void quat6(float q[4]);
void quat9(float q[4]);
void bias(float out[3]);
void bias9(float out[3]);
float bias_sigma_dps();
void bias_sigmas(float& init_dps,float& rest_dps);
bool rest();
float rest_time();
bool mag_ready();
bool mag_disturbed();
float mag_delta_deg();
void diagnostic(float out[8]);
bool query_update_diagnostic(unsigned filter_index, fusion_update_diagnostic_t& out);
// Latched until init: wire flags bit2 covariance recovery, bit3 bias recovery.
unsigned health_flags(unsigned filter_index);
// Read-only state for deterministic numerical regressions.
const State& state6();
const State& state9();
}
#endif
