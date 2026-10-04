#ifndef MAG_CALIBRATION_H
#define MAG_CALIBRATION_H
/* PROVISIONAL 2026-09-12: fit to mag_raw_20260912_220726.csv (5830 samples).
 * Earlier batch is inconsistent: do not treat this as heading validation.
 * Input: uncalibrated uT column vector. Output = M * (input - offset).
 * Matrix scale preserves ellipsoid geometric-mean radius, not known Earth field.
 * Matrix below does NOT include sensor alignment; call mag_map_to_imu AFTER it.
 * Magnetic fusion selection is in app_config.h; coefficients are used for the
 * current 9D trial.
 */
static const float mag_cal_offset[3] = {0.561956977f, -3.455820552f, 4.206353964f};
static const float mag_cal_matrix[3][3] = {
  {0.978002793f, -0.009591232f, 0.015064551f},
  {-0.009591232f, 1.067653364f, 0.005627912f},
  {0.015064551f, 0.005627912f, 0.958047992f}
};
/* Empirical mounting map, 2026-09-12:
 * mag_validate_20260912_221735.csv, all three temporal subsets agree.
 * IMU = R * calibrated_IST, R = {{0,1,0},{1,0,0},{0,0,-1}}.
 * R is orthonormal, det(R)=+1; preserves magnitude and handedness.
 * Board markings/known heading must still confirm absolute magnetic polarity.
 * Keep offsets and the soft-iron matrix in the ORIGINAL IST8310 frame.
 * Input and output may alias; temporaries prevent accidental double swapping.
 */
static inline void mag_map_to_imu(const float sensor[3], float imu[3])
{
  const float x = sensor[0];
  const float y = sensor[1];
  const float z = sensor[2];
  imu[0] = y;
  imu[1] = x;
  imu[2] = -z;
}
#endif
