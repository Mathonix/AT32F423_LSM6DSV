#include "zaru_heading_hold.h"
#include "app_config.h"
#include <math.h>
#include <string.h>

void zaru_limits_default(zaru_limits_t *limits)
{
  if(!limits) return;
  memset(limits, 0, sizeof(*limits));
  limits->enter_dps = APP_ZARU_ENTER_DPS;
  limits->exit_dps = APP_ZARU_EXIT_DPS;
  limits->acc_dev_ms2 = APP_ZARU_ACC_DEV_MS2;
  limits->enter_filter_ms = (uint16_t)APP_ZARU_ENTER_FILTER_MS;
  limits->enter_confirm_ms = (uint16_t)APP_ZARU_ENTER_CONFIRM_MS;
  limits->exit_confirm_ms = (uint16_t)APP_ZARU_EXIT_CONFIRM_MS;
}

int zaru_limits_valid(const zaru_limits_t *limits)
{
  if(!limits || limits->reserved != 0U) return 0;
  if(!isfinite(limits->enter_dps) || !isfinite(limits->exit_dps) ||
     !isfinite(limits->acc_dev_ms2)) return 0;
  if(limits->enter_dps < 0.05f || limits->enter_dps > 2.0f) return 0;
  if(limits->exit_dps <= limits->enter_dps || limits->exit_dps > 5.0f) return 0;
  if(limits->acc_dev_ms2 < 0.02f || limits->acc_dev_ms2 > 2.0f) return 0;
  if(limits->enter_filter_ms > 200U || limits->enter_confirm_ms > 2000U ||
     limits->exit_confirm_ms > 500U) return 0;
  return 1;
}

static const zaru_limits_t *active_limits(const zaru_limits_t *limits, zaru_limits_t *fallback)
{
  if(limits && zaru_limits_valid(limits)) return limits;
  zaru_limits_default(fallback);
  return fallback;
}

static float wrap_deg(float angle)
{
  if(!isfinite(angle)) return 0.0f;
  angle = fmodf(angle + 180.0f, 360.0f);
  if(angle < 0.0f) angle += 360.0f;
  return angle - 180.0f;
}

/* A zero offset must pass the attitude-filter yaw through unchanged, including
 * the exact ±180 representation. Non-zero corrections always wrap. */
static float apply_offset(const zaru_heading_hold_t *s, float raw_yaw_deg)
{
  if(s->yaw_offset_deg == 0.0f) return raw_yaw_deg;
  return wrap_deg(raw_yaw_deg + s->yaw_offset_deg);
}

static void release_hold(zaru_heading_hold_t *s)
{
  s->hold_active = 0U;
  s->exit_ms = 0.0f;
  s->enter_ms = 0.0f;
  s->exit_count++;
}

void zaru_heading_hold_reset(zaru_heading_hold_t *s)
{
  if(s) memset(s, 0, sizeof(*s));
}

float zaru_heading_hold_update(zaru_heading_hold_t *s, float raw_yaw_deg,
    float gyro_rate_fast_dps, float acc_deviation_ms2, float dt, uint8_t enabled,
    const zaru_limits_t *limits)
{
  zaru_limits_t fallback;
  const zaru_limits_t *lim;
  float tau, alpha, fast, rms;
  uint8_t rate_ok, acc_ok;
  lim = active_limits(limits, &fallback);
  if(!s) return isfinite(raw_yaw_deg) ? raw_yaw_deg : 0.0f;
  if(!isfinite(raw_yaw_deg))
    return (s->hold_active && isfinite(s->hold_yaw_deg)) ? s->hold_yaw_deg : 0.0f;
  if(!isfinite(dt) || dt <= 0.0f || dt > 0.1f) {
    if(s->hold_active && isfinite(s->hold_yaw_deg)) return s->hold_yaw_deg;
    return apply_offset(s, raw_yaw_deg);
  }
  s->initialized = 1U;
  rate_ok = (uint8_t)(isfinite(gyro_rate_fast_dps) && gyro_rate_fast_dps >= 0.0f);
  fast = rate_ok ? gyro_rate_fast_dps : 0.0f;
  acc_ok = (uint8_t)(isfinite(acc_deviation_ms2) && acc_deviation_ms2 >= 0.0f &&
      acc_deviation_ms2 < lim->acc_dev_ms2);
  s->acc_deviation_ms2 = isfinite(acc_deviation_ms2) ? acc_deviation_ms2 : 0.0f;
  if(rate_ok) {
    tau = (float)lim->enter_filter_ms * 0.001f;
    alpha = dt / (tau + dt);
    s->rate2_lpf += alpha * (fast * fast - s->rate2_lpf);
    if(s->rate2_lpf < 0.0f) s->rate2_lpf = 0.0f;
  }
  s->gyro_rate_fast_dps = fast;
  rms = sqrtf(s->rate2_lpf);
  s->gyro_rate_rms_dps = rms;
  if(!enabled) {
    if(s->hold_active) release_hold(s);
    return apply_offset(s, raw_yaw_deg);
  }
  if(s->hold_active) {
    if(rate_ok && fast > lim->exit_dps) {
      s->exit_ms += dt * 1000.0f;
      if(s->exit_ms >= (float)lim->exit_confirm_ms) {
        /* Keep the offset from the last held sample. Do not enter again on
         * this sample: the RMS filter is still slow and would re-lock. */
        release_hold(s);
        return apply_offset(s, raw_yaw_deg);
      }
    } else s->exit_ms = 0.0f;
    s->yaw_offset_deg = wrap_deg(s->hold_yaw_deg - raw_yaw_deg);
    return s->hold_yaw_deg;
  }
  /* A real turn is above the exit threshold even while the 10 ms RMS lags.
   * A single spike inside the hysteresis band must not clear the enter timer. */
  if(!rate_ok || fast > lim->exit_dps || rms >= lim->enter_dps || !acc_ok) {
    s->enter_ms = 0.0f;
    return apply_offset(s, raw_yaw_deg);
  }
  s->enter_ms += dt * 1000.0f;
  if(s->enter_ms >= (float)lim->enter_confirm_ms) {
    float held = apply_offset(s, raw_yaw_deg);
    s->hold_yaw_deg = held;
    s->yaw_offset_deg = wrap_deg(held - raw_yaw_deg);
    s->hold_active = 1U;
    s->exit_ms = 0.0f;
    s->enter_ms = 0.0f;
    s->enter_count++;
    return s->hold_yaw_deg;
  }
  return apply_offset(s, raw_yaw_deg);
}
