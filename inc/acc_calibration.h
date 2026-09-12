/* Provisional six-face diagonal calibration, 2026-09-12.
 * Source: acc_six_faces_20260912_231659_recheck_z_234958_fit.json
 * Standard 1g assumed. Not an installation/nonorthogonality calibration.
 * Apply once to nominal g, before conversion to m/s^2. */
#ifndef ACC_CALIBRATION_H
#define ACC_CALIBRATION_H
static const float acc_cal_bias_g[3] = {-0.001931983667f, 0.000993790571f, -0.001302519166f};
static const float acc_cal_gain[3] = {1.004898171941f, 1.002332655185f, 1.002755530949f};
static inline float acc_calibrate_g(unsigned axis, float nominal_g)
{
  return (nominal_g - acc_cal_bias_g[axis]) * acc_cal_gain[axis];
}
#endif
