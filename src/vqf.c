/**
 * Compact C port of BasicVQF (6D) plus rest-state gyro bias estimation.
 */

#include "vqf.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VQF_EPS 1.0e-8f
#define G_TO_MS2 9.80665f
#define DEG2RAD  0.017453292519943295f

/*
 * Gimbal-oriented tuning. Rest detection is conservative, while the
 * stationary bias estimate converges faster after a confirmed rest state. Units are rad/s, m/s^2,
 * and seconds.
 */
#ifndef VQF_REST_TH_GYR_BIAS_DPS
#define VQF_REST_TH_GYR_BIAS_DPS 0.20f
#endif
#ifndef VQF_REST_TH_GYR_FAST_DPS
#define VQF_REST_TH_GYR_FAST_DPS 0.60f
#endif
#ifndef VQF_REST_TH_ACC_MAG_MS2
#define VQF_REST_TH_ACC_MAG_MS2  0.25f
#endif
#ifndef VQF_REST_TH_ACC_FAST_MS2
#define VQF_REST_TH_ACC_FAST_MS2 0.50f
#endif
#ifndef VQF_REST_MIN_T
#define VQF_REST_MIN_T           1.5f
#endif
#ifndef VQF_REST_LP_TAU
#define VQF_REST_LP_TAU          0.5f
#endif
#ifndef VQF_BIAS_TAU
#define VQF_BIAS_TAU             2.0f
#endif
#ifndef VQF_BIAS_CLIP_DPS
#define VQF_BIAS_CLIP_DPS        2.0f
#endif
#ifndef VQF_TAU_ACC_DEFAULT
#define VQF_TAU_ACC_DEFAULT      2.0f
#endif

#define REST_TH_GYR_BIAS (VQF_REST_TH_GYR_BIAS_DPS * DEG2RAD)
#define REST_TH_GYR_FAST (VQF_REST_TH_GYR_FAST_DPS * DEG2RAD)
#define REST_TH_ACC_MAG  VQF_REST_TH_ACC_MAG_MS2
#define REST_TH_ACC_FAST VQF_REST_TH_ACC_FAST_MS2
#define REST_MIN_T       VQF_REST_MIN_T
#define REST_LP_TAU      VQF_REST_LP_TAU
#define BIAS_TAU         VQF_BIAS_TAU
#define BIAS_CLIP        (VQF_BIAS_CLIP_DPS * DEG2RAD)
#define TAU_ACC_DEFAULT  VQF_TAU_ACC_DEFAULT
#ifndef VQF_TAU_MAG_DEFAULT
#define VQF_TAU_MAG_DEFAULT 3.0f
#endif
#define TAU_MAG_DEFAULT VQF_TAU_MAG_DEFAULT

static float gyr_dt;
static float acc_dt;
static float tau_acc;
static float acc_lp_k;
static float tau_mag;
static float mag_lp_k;
static float mag_heading;
static uint8_t mag_ready;

static float gyr_quat[4];
static float acc_quat[4];
static float last_acc_lp[3];
static uint8_t acc_lp_inited;

static float gyr_lp[3];
static float rest_acc_lp[3];
static float bias[3];
static float rest_t;
static uint8_t rest_lp_inited;

static void vqf_set_acc_quat(const float acc_earth[3]);

static float vqf_norm3(const float v[3])
{
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void vqf_normalize4(float q[4])
{
  float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if(n < VQF_EPS)
  {
    q[0] = 1.0f;
    q[1] = q[2] = q[3] = 0.0f;
    return;
  }
  n = 1.0f / n;
  q[0] *= n;
  q[1] *= n;
  q[2] *= n;
  q[3] *= n;
}

static void vqf_quat_multiply(const float q1[4], const float q2[4], float out[4])
{
  float w = q1[0] * q2[0] - q1[1] * q2[1] - q1[2] * q2[2] - q1[3] * q2[3];
  float x = q1[0] * q2[1] + q1[1] * q2[0] + q1[2] * q2[3] - q1[3] * q2[2];
  float y = q1[0] * q2[2] - q1[1] * q2[3] + q1[2] * q2[0] + q1[3] * q2[1];
  float z = q1[0] * q2[3] + q1[1] * q2[2] - q1[2] * q2[1] + q1[3] * q2[0];
  out[0] = w;
  out[1] = x;
  out[2] = y;
  out[3] = z;
}

static void vqf_quat_rotate(const float q[4], const float v[3], float out[3])
{
  float tx = 2.0f * (q[2] * v[2] - q[3] * v[1]);
  float ty = 2.0f * (q[3] * v[0] - q[1] * v[2]);
  float tz = 2.0f * (q[1] * v[1] - q[2] * v[0]);
  out[0] = v[0] + q[0] * tx + (q[2] * tz - q[3] * ty);
  out[1] = v[1] + q[0] * ty + (q[3] * tx - q[1] * tz);
  out[2] = v[2] + q[0] * tz + (q[1] * ty - q[2] * tx);
}

void vqf_init(float gyr_dt_, float acc_dt_)
{
  gyr_dt = gyr_dt_;
  acc_dt = acc_dt_;
  tau_acc = TAU_ACC_DEFAULT;
  tau_mag = TAU_MAG_DEFAULT;
  mag_lp_k = 1.0f - expf(-0.02f / tau_mag);
  mag_heading = 0.0f;
  mag_ready = 0U;
  acc_lp_k = 1.0f - expf(-acc_dt / tau_acc);

  gyr_quat[0] = 1.0f;
  gyr_quat[1] = gyr_quat[2] = gyr_quat[3] = 0.0f;
  acc_quat[0] = 1.0f;
  acc_quat[1] = acc_quat[2] = acc_quat[3] = 0.0f;
  memset(last_acc_lp, 0, sizeof(last_acc_lp));
  acc_lp_inited = 0;
  memset(gyr_lp, 0, sizeof(gyr_lp));
  memset(rest_acc_lp, 0, sizeof(rest_acc_lp));
  memset(bias, 0, sizeof(bias));
  rest_t = 0.0f;
  rest_lp_inited = 0;
}

void vqf_set_tau_mag(float tau)
{
  if(tau < 0.2f) tau = 0.2f;
  tau_mag = tau;
  mag_lp_k = 1.0f - expf(-0.02f / tau_mag);
}

float vqf_get_tau_mag(void) { return tau_mag; }
int vqf_get_mag_ready(void) { return mag_ready ? 1 : 0; }

void vqf_set_gyr_bias(const float gyr_bias[3])
{
  int i;

  for(i = 0; i < 3; i++)
  {
    bias[i] = gyr_bias[i];
    if(bias[i] > BIAS_CLIP)
    {
      bias[i] = BIAS_CLIP;
    }
    else if(bias[i] < -BIAS_CLIP)
    {
      bias[i] = -BIAS_CLIP;
    }
    gyr_lp[i] = bias[i];
  }
  rest_t = REST_MIN_T;
  rest_lp_inited = 1;
}

static void vqf_set_acc_quat(const float acc_earth[3])
{
  float n = vqf_norm3(acc_earth);
  float inv_n;
  float a[3];
  float qw;

  if(n < 1.0e-6f)
  {
    return;
  }
  inv_n = 1.0f / n;
  a[0] = acc_earth[0] * inv_n;
  a[1] = acc_earth[1] * inv_n;
  a[2] = acc_earth[2] * inv_n;
  qw = sqrtf((a[2] + 1.0f) * 0.5f);
  if(qw > 1.0e-6f)
  {
    acc_quat[0] = qw;
    acc_quat[1] = a[1] / (2.0f * qw);
    acc_quat[2] = -a[0] / (2.0f * qw);
    acc_quat[3] = 0.0f;
  }
  else
  {
    acc_quat[0] = 0.0f;
    acc_quat[1] = 1.0f;
    acc_quat[2] = 0.0f;
    acc_quat[3] = 0.0f;
  }
}

void vqf_prime_rest(const float acc_ms2[3], const float gyr_bias[3])
{
  int i;

  vqf_set_gyr_bias(gyr_bias);
  gyr_quat[0] = 1.0f;
  gyr_quat[1] = gyr_quat[2] = gyr_quat[3] = 0.0f;
  for(i = 0; i < 3; i++)
  {
    last_acc_lp[i] = acc_ms2[i];
    rest_acc_lp[i] = acc_ms2[i];
  }
  acc_lp_inited = 1;
  vqf_set_acc_quat(acc_ms2);
}

void vqf_set_tau_acc(float tau)
{
  if(tau < 0.05f)
  {
    tau = 0.05f;
  }
  tau_acc = tau;
  acc_lp_k = 1.0f - expf(-acc_dt / tau_acc);
  acc_lp_inited = 0;
}

void vqf_update_gyr(const float gyr[3])
{
  float n = vqf_norm3(gyr);
  float angle;
  float c;
  float s;
  float step[4];
  float q[4];

  if(n < VQF_EPS)
  {
    return;
  }

  angle = n * gyr_dt;
  c = cosf(0.5f * angle);
  s = sinf(0.5f * angle) / n;
  step[0] = c;
  step[1] = s * gyr[0];
  step[2] = s * gyr[1];
  step[3] = s * gyr[2];
  vqf_quat_multiply(gyr_quat, step, q);
  gyr_quat[0] = q[0];
  gyr_quat[1] = q[1];
  gyr_quat[2] = q[2];
  gyr_quat[3] = q[3];
  vqf_normalize4(gyr_quat);
}

void vqf_update_acc(const float acc[3])
{
  float acc_earth[3];
  float n;
  int i;

  if((acc[0] == 0.0f) && (acc[1] == 0.0f) && (acc[2] == 0.0f))
  {
    return;
  }

  vqf_quat_rotate(gyr_quat, acc, acc_earth);

  if(!acc_lp_inited)
  {
    for(i = 0; i < 3; i++)
    {
      last_acc_lp[i] = acc_earth[i];
    }
    acc_lp_inited = 1;
  }
  else
  {
    for(i = 0; i < 3; i++)
    {
      last_acc_lp[i] += acc_lp_k * (acc_earth[i] - last_acc_lp[i]);
    }
  }

  n = vqf_norm3(last_acc_lp);
  if(n < 1.0e-6f)
  {
    return;
  }

  vqf_set_acc_quat(last_acc_lp);
}

static void vqf_rest_bias(const float gyr[3], const float acc[3], float gyr_corr[3])
{
  float k_lp = gyr_dt / (REST_LP_TAU + gyr_dt);
  float k_b = gyr_dt / (BIAS_TAU + gyr_dt);
  float gyr_bias_err[3];
  float gyr_fast_err[3];
  float acc_fast_err[3];
  float gyr_bias_n;
  float gyr_fast_n;
  float acc_fast_n;
  float acc_n;
  int i;

  if(!rest_lp_inited)
  {
    for(i = 0; i < 3; i++)
    {
      gyr_lp[i] = gyr[i];
      rest_acc_lp[i] = acc[i];
    }
    rest_lp_inited = 1;
  }
  else
  {
    for(i = 0; i < 3; i++)
    {
      gyr_lp[i] += k_lp * (gyr[i] - gyr_lp[i]);
      rest_acc_lp[i] += k_lp * (acc[i] - rest_acc_lp[i]);
    }
  }

  for(i = 0; i < 3; i++)
  {
    /* Compare against the known bias, not against absolute angular rate. */
    gyr_bias_err[i] = gyr_lp[i] - bias[i];
    gyr_fast_err[i] = gyr[i] - gyr_lp[i];
    acc_fast_err[i] = acc[i] - rest_acc_lp[i];
  }
  gyr_bias_n = vqf_norm3(gyr_bias_err);
  gyr_fast_n = vqf_norm3(gyr_fast_err);
  acc_fast_n = vqf_norm3(acc_fast_err);
  acc_n = vqf_norm3(rest_acc_lp);

  if((gyr_bias_n < REST_TH_GYR_BIAS) &&
     (gyr_fast_n < REST_TH_GYR_FAST) &&
     (acc_fast_n < REST_TH_ACC_FAST) &&
     (fabsf(acc_n - G_TO_MS2) < REST_TH_ACC_MAG))
  {
    rest_t += gyr_dt;
  }
  else
  {
    rest_t = 0.0f;
  }

  if(rest_t >= REST_MIN_T)
  {
    for(i = 0; i < 3; i++)
    {
      bias[i] += k_b * (gyr_lp[i] - bias[i]);
      if(bias[i] > BIAS_CLIP)
      {
        bias[i] = BIAS_CLIP;
      }
      else if(bias[i] < -BIAS_CLIP)
      {
        bias[i] = -BIAS_CLIP;
      }
    }
  }

  gyr_corr[0] = gyr[0] - bias[0];
  gyr_corr[1] = gyr[1] - bias[1];
  gyr_corr[2] = gyr[2] - bias[2];
}

void vqf_update(const float gyr[3], const float acc[3])
{
  float gyr_corr[3];
  vqf_rest_bias(gyr, acc, gyr_corr);
  vqf_update_gyr(gyr_corr);
  vqf_update_acc(acc);
}

int vqf_update_mag(const float mag[3])
{
  float m_earth[3];
  float q6[4];
  float n = vqf_norm3(mag);
  float h;
  if(n < 1.0e-6f || n > 2000.0f) return -1;
  vqf_quat_multiply(acc_quat, gyr_quat, q6);
  vqf_quat_rotate(q6, mag, m_earth);
  h = atan2f(-m_earth[1], m_earth[0]);
  if(!mag_ready) { mag_heading = h; mag_ready = 1U; }
  else {
    float e = h - mag_heading;
    while(e > (float)M_PI) e -= 2.0f*(float)M_PI;
    while(e < -(float)M_PI) e += 2.0f*(float)M_PI;
    mag_heading += mag_lp_k * e;
  }
  return 0;
}

void vqf_get_quat6d(float q[4])
{
  vqf_quat_multiply(acc_quat, gyr_quat, q);
}

void vqf_get_quat9d(float q[4])
{
  float q6[4], qc[4], z[4];
  vqf_get_quat6d(q6);
  /* mag_heading is the VQF heading-difference delta, not an absolute yaw.
   * Applying delta to q6 makes gyro yaw changes cancel in the earth-frame
   * magnetic heading during a stationary interval. */
  z[0] = cosf(0.5f * mag_heading);
  z[1] = 0.0f; z[2] = 0.0f; z[3] = sinf(0.5f * mag_heading);
  vqf_quat_multiply(z, q6, qc);
  vqf_normalize4(qc);
  if(mag_ready) { q[0]=qc[0]; q[1]=qc[1]; q[2]=qc[2]; q[3]=qc[3]; }
  else { q[0]=q6[0]; q[1]=q6[1]; q[2]=q6[2]; q[3]=q6[3]; }
}

void vqf_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg)
{
  float q[4];
  float w;
  float x;
  float y;
  float z;
  float sinp;
  float rad2deg = 180.0f / (float)M_PI;

  vqf_get_quat9d(q);
  w = q[0];
  x = q[1];
  y = q[2];
  z = q[3];

  sinp = 2.0f * (w * y - z * x);
  if(sinp > 1.0f)
  {
    sinp = 1.0f;
  }
  else if(sinp < -1.0f)
  {
    sinp = -1.0f;
  }

  *roll_deg  = atan2f(2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y)) * rad2deg;
  *pitch_deg = asinf(sinp) * rad2deg;
  *yaw_deg   = atan2f(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z)) * rad2deg;
}

void vqf_get_gyr_bias(float gyr_bias[3])
{
  if(gyr_bias == 0)
  {
    return;
  }
  gyr_bias[0] = bias[0];
  gyr_bias[1] = bias[1];
  gyr_bias[2] = bias[2];
}

float vqf_get_rest_time(void)
{
  return rest_t;
}

int vqf_get_rest_detected(void)
{
  return (rest_t >= REST_MIN_T) ? 1 : 0;
}

float vqf_get_tau_acc(void)
{
  return tau_acc;
}
