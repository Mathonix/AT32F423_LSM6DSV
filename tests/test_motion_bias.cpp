#include <cassert>
#include <cmath>
#include <cstdio>
#include "app_config.h"
#include "fusion_profile.h"
#include "vqf.h"
#include "vqf_full.hpp"
#include "zaru_heading_hold.h"

static const float kPi = 3.14159265358979323846f;
static const float kDt = 0.005f;
static const float kG = 9.80665f;

struct Quat { float w, x, y, z; };

static int finite3(const float v[3])
{
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

static void normalize_q(Quat *q)
{
  float n = std::sqrt(q->w * q->w + q->x * q->x + q->y * q->y + q->z * q->z);
  q->w /= n; q->x /= n; q->y /= n; q->z /= n;
}

static void integrate_body(Quat *q, float wx, float wy, float wz, float dt)
{
  float n = std::sqrt(wx * wx + wy * wy + wz * wz);
  float half = 0.5f * n * dt;
  float s = (n > 1.0e-8f) ? std::sin(half) / n : 0.5f * dt;
  float c = std::cos(half);
  float nw = q->w * c - q->x * s * wx - q->y * s * wy - q->z * s * wz;
  float nx = q->w * s * wx + q->x * c + q->y * s * wz - q->z * s * wy;
  float ny = q->w * s * wy - q->x * s * wz + q->y * c + q->z * s * wx;
  float nz = q->w * s * wz + q->x * s * wy - q->y * s * wx + q->z * c;
  q->w = nw; q->x = nx; q->y = ny; q->z = nz;
  normalize_q(q);
}

static void gravity_body(const Quat *q, float acc[3])
{
  float cw = q->w, cx = -q->x, cy = -q->y, cz = -q->z;
  float tw = -cx * 0.0f - cy * 0.0f - cz * kG;
  float tx = cw * 0.0f + cy * kG - cz * 0.0f;
  float ty = cw * 0.0f + cz * 0.0f - cx * kG;
  float tz = cw * kG + cx * 0.0f - cy * 0.0f;
  acc[0] = tw * q->x + tx * q->w + ty * q->z - tz * q->y;
  acc[1] = tw * q->y - tx * q->z + ty * q->w + tz * q->x;
  acc[2] = tw * q->z + tx * q->y - ty * q->x + tz * q->w;
}

static VQFParams firmware_params(bool motion)
{
  VQFParams params;
  fusion_profile_t profile = fusion_profile_get(FUSION_PROFILE_BALANCED);
  params.tauAcc = profile.tau_acc_s;
  params.tauMag = profile.tau_mag_s;
  params.motionBiasEstEnabled = motion;
  params.restBiasEstEnabled = true;
  params.magDistRejectionEnabled = true;
  params.biasSigmaRest = APP_VQF_BIAS_SIGMA_REST_DPS;
  params.biasSigmaMotion = APP_VQF_BIAS_SIGMA_MOTION_DPS;
  params.biasClip = APP_VQF_BIAS_CLIP_DPS;
  params.biasForgettingTime = APP_VQF_BIAS_FORGETTING_TIME_S;
  params.biasVerticalForgettingFactor = 0.0001f;
  params.restFilterTau = APP_VQF_REST_FILTER_TAU_S;
  params.magNewMinGyr = 0.0f;
  params.restThGyr = profile.rest_th_gyr_dps;
  params.restThAcc = profile.rest_th_acc_ms2;
  params.restMinT = APP_VQF_REST_MIN_SECONDS;
  return params;
}

struct RunStats {
  float bias_motion_end[3];
  float bias_rest_final[3];
  float max_step_dps;
  float max_abs_dps;
  unsigned motion_samples;
  unsigned motion_rest;
  unsigned clip_hits;
};

static void read_bias_dps(const VQF& vqf, float out[3])
{
  vqf_real_t raw[3];
  vqf.getBiasEstimate(raw);
  for(unsigned i = 0; i < 3; ++i) out[i] = static_cast<float>(raw[i]) * (180.0f / kPi);
}

static RunStats run_case(bool motion, const float true_bias_dps[3])
{
  VQF vqf(firmware_params(motion), kDt, kDt, 0.1f);
  Quat q = {1.0f, 0.0f, 0.0f, 0.0f};
  float true_bias[3];
  float previous[3] = {0.0f, 0.0f, 0.0f};
  RunStats stats = {};
  unsigned phase_samples[2] = {8000U, 8000U}; /* 40 s motion, 40 s rest */
  for(unsigned i = 0; i < 3; ++i) true_bias[i] = true_bias_dps[i] * (kPi / 180.0f);
  for(unsigned phase = 0; phase < 2; ++phase) {
    for(unsigned n = 0; n < phase_samples[phase]; ++n) {
      float omega[3] = {0.0f, 0.0f, 0.0f};
      float gyro[3], acc[3], bias[3];
      if(phase == 0) {
        float t = (n * kDt);
        float segment = std::fmod(t, 8.0f);
        if(segment < 4.0f) omega[0] = 0.90f;
        else omega[1] = 0.75f;
      }
      integrate_body(&q, omega[0], omega[1], omega[2], kDt);
      gravity_body(&q, acc);
      for(unsigned i = 0; i < 3; ++i) gyro[i] = omega[i] + true_bias[i];
      vqf.update(gyro, acc);
      read_bias_dps(vqf, bias);
      assert(finite3(bias));
      assert(std::isfinite(acc[0]) && std::isfinite(acc[1]) && std::isfinite(acc[2]));
      for(unsigned i = 0; i < 3; ++i) {
        float step = std::fabs(bias[i] - previous[i]);
        if(n || phase) stats.max_step_dps = std::fmax(stats.max_step_dps, step);
        stats.max_abs_dps = std::fmax(stats.max_abs_dps, std::fabs(bias[i]));
        if(std::fabs(bias[i]) > 1.9f) stats.clip_hits++;
        previous[i] = bias[i];
      }
      if(phase == 0) {
        stats.motion_samples++;
        if(vqf.getRestDetected()) stats.motion_rest++;
      }
    }
    if(phase == 0) read_bias_dps(vqf, stats.bias_motion_end);
    else read_bias_dps(vqf, stats.bias_rest_final);
  }
  return stats;
}

static float norm3(const float a[3], const float b[3])
{
  float s = 0.0f;
  for(unsigned i = 0; i < 3; ++i) {
    float d = a[i] - b[i];
    s += d * d;
  }
  return std::sqrt(s);
}

static void switches_stay_finite(void)
{
  VQF vqf(firmware_params(true), kDt, kDt, 0.1f);
  Quat q = {1.0f, 0.0f, 0.0f, 0.0f};
  const float bias = 0.4f * (kPi / 180.0f);
  float previous[3] = {0.0f, 0.0f, 0.0f};
  for(unsigned cycle = 0; cycle < 4; ++cycle) {
    for(unsigned n = 0; n < 1000; ++n) {
      float moving = (cycle % 2) == 0;
      float omega = moving ? 0.8f : 0.0f;
      float gyro[3] = {omega + bias, bias * 0.5f, -bias};
      float acc[3], estimate[3];
      integrate_body(&q, omega, 0.0f, 0.0f, kDt);
      gravity_body(&q, acc);
      vqf.update(gyro, acc);
      read_bias_dps(vqf, estimate);
      assert(finite3(estimate));
      for(unsigned i = 0; i < 3; ++i) {
        if(cycle || n) assert(std::fabs(estimate[i] - previous[i]) < 0.25f);
        assert(std::fabs(estimate[i]) < 1.9f);
        previous[i] = estimate[i];
      }
    }
  }
}

static void zaru_follows_residual(void)
{
  zaru_heading_hold_t hold;
  zaru_heading_hold_reset(&hold);
  for(unsigned i = 0; i < 200; ++i) {
    float yaw = zaru_heading_hold_update(&hold, 0.0f, 20.0f, 0.0f, 0.001f, 1, NULL);
    assert(std::isfinite(yaw));
    assert(!hold.hold_active);
  }
  zaru_heading_hold_reset(&hold);
  for(unsigned i = 0; i < 80; ++i)
    zaru_heading_hold_update(&hold, 0.0f, 0.05f, 0.0f, 0.001f, 1, NULL);
  assert(hold.hold_active);
  for(unsigned i = 0; i < 10; ++i)
    zaru_heading_hold_update(&hold, 1.0f, 1.2f, 0.0f, 0.001f, 1, NULL);
  assert(!hold.hold_active);
}

static void live_config_is_enabled(void)
{
  vqf_bias_estimator_config_t config;
  vqf_apply_profile(FUSION_PROFILE_BALANCED);
  vqf_init(0.0005f, 0.0005f);
  vqf_get_bias_estimator_config(&config);
  assert(APP_VQF_MOTION_BIAS_ENABLE == 1U);
  assert(config.motion_bias_enabled == 1U);
  assert(config.rest_bias_enabled == 1U);
  assert(std::fabs(config.bias_sigma_motion_dps - 0.1f) < 1.0e-6f);
  assert(std::fabs(config.bias_vertical_forgetting - 0.0001f) < 1.0e-8f);
  assert(std::fabs(config.bias_forgetting_time_s - 100.0f) < 1.0e-4f);
  assert(std::fabs(config.bias_clip_dps - 2.0f) < 1.0e-6f);
  assert(std::fabs(config.bias_sigma_rest_dps - 0.035f) < 1.0e-6f);
  assert(std::fabs(config.bias_sigma_init_dps - 0.5f) < 1.0e-6f);
  assert(std::fabs(config.tau_acc_s - 2.5f) < 1.0e-4f);
  {
    float q_before[4], q_after[4], bias_before[3], bias_after[3];
    float seed[3] = {0.012f, -0.021f, 0.008f};
    const float still_acc[3] = {0.0f, 0.0f, 9.80665f};
    unsigned k;
    vqf_seed_gyr_bias(seed, 0.2f);
    for(k = 0; k < 20; ++k) vqf_update(seed, still_acc);
    vqf_get_quat6d(q_before);
    vqf_get_gyr_bias(bias_before);
    vqf_set_bias_sigmas(0.25f, 0.04f);
    vqf_get_bias_estimator_config(&config);
    vqf_get_quat6d(q_after);
    vqf_get_gyr_bias(bias_after);
    assert(std::fabs(config.bias_sigma_init_dps - 0.25f) < 1.0e-6f);
    assert(std::fabs(config.bias_sigma_rest_dps - 0.04f) < 1.0e-6f);
    assert(std::fabs(config.bias_sigma_motion_dps - 0.1f) < 1.0e-6f);
    assert(std::fabs(config.tau_acc_s - 2.5f) < 1.0e-4f);
    for(k = 0; k < 4; ++k) assert(q_before[k] == q_after[k]);
    for(k = 0; k < 3; ++k) assert(bias_before[k] == bias_after[k]);
    vqf_set_bias_sigmas(0.0f, 0.04f);
    vqf_get_bias_estimator_config(&config);
    assert(std::fabs(config.bias_sigma_init_dps - 0.25f) < 1.0e-6f);
  }
}

int main()
{
  const float true_bias[3] = {0.80f, -0.50f, 0.40f};
  RunStats off = run_case(false, true_bias);
  RunStats on = run_case(true, true_bias);
  float e_off[3], e_on[3];
  for(unsigned i = 0; i < 3; ++i) {
    e_off[i] = std::fabs(off.bias_motion_end[i] - off.bias_rest_final[i]);
    e_on[i] = std::fabs(on.bias_motion_end[i] - on.bias_rest_final[i]);
    assert(std::fabs(off.bias_rest_final[i] - true_bias[i]) < 0.15f);
    assert(std::fabs(on.bias_rest_final[i] - true_bias[i]) < 0.15f);
  }
  float e_off_n = norm3(off.bias_motion_end, off.bias_rest_final);
  float e_on_n = norm3(on.bias_motion_end, on.bias_rest_final);
  std::printf("E_motion off %.4f (%.4f %.4f %.4f) on %.4f (%.4f %.4f %.4f)\n",
              e_off_n, e_off[0], e_off[1], e_off[2], e_on_n, e_on[0], e_on[1], e_on[2]);
  std::printf("motion-end off %.4f %.4f %.4f on %.4f %.4f %.4f\n",
              off.bias_motion_end[0], off.bias_motion_end[1], off.bias_motion_end[2],
              on.bias_motion_end[0], on.bias_motion_end[1], on.bias_motion_end[2]);
  std::printf("rest off %.4f %.4f %.4f on %.4f %.4f %.4f clip %u/%u step %.4f/%.4f rest-during-motion %u/%u\n",
              off.bias_rest_final[0], off.bias_rest_final[1], off.bias_rest_final[2],
              on.bias_rest_final[0], on.bias_rest_final[1], on.bias_rest_final[2],
              off.clip_hits, on.clip_hits, off.max_step_dps, on.max_step_dps,
              on.motion_rest, on.motion_samples);
  assert(off.clip_hits == 0U && on.clip_hits == 0U);
  assert(off.max_abs_dps < 1.9f && on.max_abs_dps < 1.9f);
  assert(off.max_step_dps < 0.25f && on.max_step_dps < 0.25f);
  assert(on.motion_rest * 20U < on.motion_samples);
  assert(e_on_n < e_off_n);
  assert(e_on[0] < e_off[0] && e_on[1] < e_off[1]);
  switches_stay_finite();
  zaru_follows_residual();
  live_config_is_enabled();
  std::puts("motion bias: enable, rest kept, synthetic A/B, ZARU residual: OK");
  return 0;
}
