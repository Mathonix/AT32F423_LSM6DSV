#ifndef MAG_CALIBRATION_H
#define MAG_CALIBRATION_H
#include <stdint.h>
#include <string.h>
#include <math.h>
/* In-situ fit, 2026-10-22: sphere fit over a 6293-sample full tumble.
 *
 * This unit accumulates a large hard-iron offset when a magnet is brought
 * close (a ~69 uT vector in the sensor frame after one such test), so the
 * compiled fallback has to carry it: with the older 18 uT coefficients the
 * corrected field sat at 88 uT and the plausibility window refused to lock.
 *
 * A four-parameter sphere fit and a nine-parameter ellipsoid fit gave the same
 * residual (6.4% vs 6.5%), so the distortion is essentially pure hard iron and
 * the matrix is kept at identity: fitting unconstrained shape terms to that
 * data only injected noise into the result.
 *
 * Matrix scale preserves the fitted field radius (41.5 uT), not a known Earth
 * value. Measured residual after correction: |B| 41.5 +- 2.4 uT, and the angle
 * to the accelerometer is a constant 47.3 deg, which is the invariant a correct
 * hard-iron removal must produce.
 *
 * These are PER UNIT. Every board needs its own tumble capture; see
 * tools/serial/mag_cal_fit.py (it prints the C array and the 0x4B payload).
 * Regenerate after any close exposure to a strong magnet.
 *
 * Input: uncalibrated uT column vector. Output = M * (input - offset).
 * Matrix below does NOT include sensor alignment; call mag_map_to_imu AFTER it.
 */
/* Field magnitude the fit above preserves; used as the calibration-health
 * fiducial when the compiled defaults are restored. */
static const float mag_cal_default_ut = 41.5f;
static const float mag_cal_offset[3] = {-55.263f, -21.555f, -34.375f};
static const float mag_cal_matrix[3][3] = {
  {1.0f, 0.0f, 0.0f},
  {0.0f, 1.0f, 0.0f},
  {0.0f, 0.0f, 1.0f}
};
typedef struct { uint32_t valid; float offset[3], matrix[3][3]; } mag_cal_parameters_t;
static inline void mag_cal_defaults(mag_cal_parameters_t *p)
{
  memset(p,0,sizeof(*p));
  memcpy(p->offset,mag_cal_offset,sizeof(p->offset));
  memcpy(p->matrix,mag_cal_matrix,sizeof(p->matrix));
}
/* Ellipsoid correction is symmetric positive definite. Alignment stays separate. */
static inline int mag_cal_valid(const mag_cal_parameters_t *p)
{
  if(!p || p->valid>1U) return 0;
  for(unsigned i=0;i<3;i++) {
    if(!isfinite(p->offset[i]) || fabsf(p->offset[i])>500.0f) return 0;
    for(unsigned j=0;j<3;j++) if(!isfinite(p->matrix[i][j]) || fabsf(p->matrix[i][j])>5.0f ||
       fabsf(p->matrix[i][j]-p->matrix[j][i])>0.0001f) return 0;
    if(p->matrix[i][i]<0.2f || p->matrix[i][i]>5.0f) return 0;
  }
  const float (*m)[3]=p->matrix;
  float minor=m[0][0]*m[1][1]-m[0][1]*m[1][0];
  float det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-
    m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
  return minor>0.01f && det>0.01f;
}
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
