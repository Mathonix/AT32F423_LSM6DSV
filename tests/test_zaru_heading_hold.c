#include "zaru_heading_hold.h"
#include "attitude_output.h"
#include "app_config.h"
#include "fusion_profile.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(float a, float b)
{
  return fabsf(a - b) < 1e-3f;
}

static float wrap_deg(float angle)
{
  angle = fmodf(angle + 180.0f, 360.0f);
  if(angle < 0.0f) angle += 360.0f;
  return angle - 180.0f;
}

static float step(zaru_heading_hold_t *s, float yaw, float rate, float acc, uint8_t enabled)
{
  return zaru_heading_hold_update(s, yaw, rate, acc, 0.001f, enabled, NULL);
}

static void settle(zaru_heading_hold_t *s, float yaw)
{
  unsigned i;
  for(i = 0; i < APP_ZARU_ENTER_CONFIRM_MS; i++) step(s, yaw, 0.12f, 0.02f, 1);
}

static void test_still_enters_after_confirm(void)
{
  zaru_heading_hold_t s;
  unsigned i;
  zaru_heading_hold_reset(&s);
  for(i = 0; i + 1U < APP_ZARU_ENTER_CONFIRM_MS; i++) {
    step(&s, 10.0f, 0.20f, 0.05f, 1);
    assert(!s.hold_active);
  }
  assert(near(step(&s, 10.0f, 0.20f, 0.05f, 1), 10.0f));
  assert(s.hold_active);
  assert(s.gyro_rate_rms_dps < APP_ZARU_ENTER_DPS);
}

static void test_spike_does_not_reset_enter(void)
{
  zaru_heading_hold_t s;
  unsigned i;
  float entered;
  zaru_heading_hold_reset(&s);
  for(i = 0; i < 30; i++) step(&s, 4.0f, 0.20f, 0.02f, 1);
  entered = s.enter_ms;
  assert(entered > 20.0f && !s.hold_active);
  step(&s, 4.0f, 0.45f, 0.02f, 1);
  assert(!s.hold_active);
  assert(s.enter_ms > entered);
  assert(s.gyro_rate_rms_dps < APP_ZARU_ENTER_DPS);
  for(; i < APP_ZARU_ENTER_CONFIRM_MS; i++) step(&s, 4.0f, 0.20f, 0.02f, 1);
  assert(s.hold_active);
}

static void test_motion_does_not_lock(void)
{
  zaru_heading_hold_t s;
  unsigned i;
  zaru_heading_hold_reset(&s);
  for(i = 0; i < 200; i++) {
    float out = step(&s, (float)i * 0.01f, 1.0f, 0.0f, 1);
    assert(!s.hold_active);
    assert(near(out, (float)i * 0.01f));
  }
}

static void test_fast_exit(void)
{
  zaru_heading_hold_t s;
  unsigned i;
  zaru_heading_hold_reset(&s);
  settle(&s, 10.0f);
  assert(s.hold_active);
  for(i = 0; i + 1U < APP_ZARU_EXIT_CONFIRM_MS; i++) {
    assert(s.hold_active);
    assert(near(step(&s, 10.0f, APP_ZARU_EXIT_DPS + 0.05f, 0.0f, 1), 10.0f));
  }
  step(&s, 10.2f, APP_ZARU_EXIT_DPS + 0.05f, 0.0f, 1);
  assert(!s.hold_active);
  step(&s, 11.2f, APP_ZARU_EXIT_DPS + 0.05f, 0.0f, 1);
  assert(!s.hold_active);
}

static void test_hysteresis_band(void)
{
  zaru_heading_hold_t s;
  unsigned i;
  zaru_heading_hold_reset(&s);
  for(i = 0; i < 200; i++) step(&s, 1.0f, 0.45f, 0.0f, 1);
  assert(!s.hold_active);
  zaru_heading_hold_reset(&s);
  settle(&s, 8.0f);
  assert(s.hold_active);
  for(i = 0; i < 200; i++) assert(near(step(&s, 8.01f, 0.45f, 0.0f, 1), 8.0f));
  assert(s.hold_active);
}

static void test_hold_drift_and_continuous_exit(void)
{
  zaru_heading_hold_t s;
  float raws[] = {30.0f, 30.010f, 30.020f, 30.050f, 30.100f};
  unsigned i;
  zaru_heading_hold_reset(&s);
  settle(&s, 30.0f);
  assert(s.hold_active && near(s.hold_yaw_deg, 30.0f));
  for(i = 1; i < 5; i++) assert(near(step(&s, raws[i], 0.10f, 0.0f, 1), 30.0f));
  assert(near(s.yaw_offset_deg, -0.100f));
  for(i = 0; i + 1U < APP_ZARU_EXIT_CONFIRM_MS; i++)
    assert(near(step(&s, 30.5f, 2.0f, 0.0f, 1), 30.0f));
  assert(near(step(&s, 31.0f, 2.0f, 0.0f, 1), 30.5f));
  assert(!s.hold_active && near(s.yaw_offset_deg, -0.5f));
  assert(near(step(&s, 32.0f, 2.0f, 0.0f, 1), 31.5f));
}

static void test_wrap(void)
{
  zaru_heading_hold_t s;
  unsigned i;
  float out;
  zaru_heading_hold_reset(&s);
  settle(&s, 179.9f);
  assert(s.hold_active);
  out = step(&s, -179.9f, 0.10f, 0.0f, 1);
  assert(s.hold_active && near(out, 179.9f));
  assert(fabsf(s.yaw_offset_deg) < 2.0f);
  out = step(&s, -179.5f, 0.10f, 0.0f, 1);
  assert(near(out, 179.9f) && fabsf(s.yaw_offset_deg) < 2.0f);
  for(i = 0; i < APP_ZARU_EXIT_CONFIRM_MS; i++) out = step(&s, -179.5f, 1.0f, 0.0f, 1);
  assert(!s.hold_active);
  assert(fabsf(s.yaw_offset_deg) < 2.0f);
  assert(fabsf(out) < 190.0f);
}

static void test_low_rate_stays_held_without_vqf_rest(void)
{
  zaru_heading_hold_t s;
  unsigned i;
  zaru_heading_hold_reset(&s);
  settle(&s, 5.0f);
  for(i = 0; i < 20; i++) assert(near(step(&s, 5.02f, 0.05f, 0.0f, 1), 5.0f));
  assert(s.hold_active);
}

static void test_acc_gate_blocks_enter(void)
{
  zaru_heading_hold_t s;
  unsigned i;
  zaru_heading_hold_reset(&s);
  for(i = 0; i < 100; i++) step(&s, 1.0f, 0.10f, 0.50f, 1);
  assert(!s.hold_active);
  for(i = 0; i < APP_ZARU_ENTER_CONFIRM_MS; i++) step(&s, 1.0f, 0.10f, 0.05f, 1);
  assert(s.hold_active);
}

static void test_manual_zero_while_held(void)
{
  zaru_heading_hold_t s;
  float manual = 0.0f, published, corrected;
  zaru_heading_hold_reset(&s);
  settle(&s, 35.0f);
  corrected = s.hold_yaw_deg;
  published = wrap_deg(corrected - manual);
  assert(s.hold_active && near(published, 35.0f));
  manual = wrap_deg(manual + published);
  corrected = step(&s, 35.02f, 0.10f, 0.0f, 1);
  published = wrap_deg(corrected - manual);
  assert(s.hold_active && near(published, 0.0f));
  corrected = step(&s, 35.08f, 0.10f, 0.0f, 1);
  published = wrap_deg(corrected - manual);
  assert(near(published, 0.0f));
  step(&s, 35.10f, 2.0f, 0.0f, 1);
  step(&s, 35.12f, 2.0f, 0.0f, 1);
  corrected = step(&s, 45.12f, 2.0f, 0.0f, 1);
  published = wrap_deg(corrected - manual);
  assert(!s.hold_active && near(published, 10.0f));
}

static void test_roll_pitch_independent(void)
{
  zaru_heading_hold_t s;
  float q[4], roll, pitch, yaw, held;
  zaru_heading_hold_reset(&s);
  settle(&s, 30.0f);
  held = s.hold_yaw_deg;
  attitude_from_euler(10.0f, -4.0f, held, q);
  attitude_to_euler(q, &roll, &pitch, &yaw);
  assert(near(roll, 10.0f) && near(pitch, -4.0f) && near(yaw, 30.0f));
  held = step(&s, 30.04f, 0.10f, 0.0f, 1);
  attitude_from_euler(16.0f, 7.0f, held, q);
  attitude_to_euler(q, &roll, &pitch, &yaw);
  assert(s.hold_active && near(yaw, 30.0f));
  assert(near(roll, 16.0f) && near(pitch, 7.0f));
}

static void test_profile_switch_is_continuous(void)
{
  zaru_heading_hold_t s;
  float out;
  fusion_profile_t balanced = fusion_profile_get(FUSION_PROFILE_BALANCED);
  fusion_profile_t zaru = fusion_profile_get(FUSION_PROFILE_ZARU);
  zaru_heading_hold_reset(&s);
  assert(FUSION_PROFILE_RESPONSE == 0 && FUSION_PROFILE_BALANCED == 1 &&
         FUSION_PROFILE_STABLE == 2 && FUSION_PROFILE_ZARU == 3);
  assert(!memcmp(&balanced, &zaru, sizeof(balanced)));
  out = step(&s, 12.0f, 1.0f, 0.0f, 0);
  assert(near(out, 12.0f) && !s.hold_active);
  settle(&s, 12.0f);
  assert(s.hold_active && near(s.hold_yaw_deg, 12.0f));
  out = step(&s, 12.2f, 0.10f, 0.0f, 1);
  assert(near(out, 12.0f));
  out = step(&s, 12.2f, 0.10f, 0.0f, 0);
  assert(!s.hold_active && near(out, 12.0f));
  out = step(&s, 13.2f, 1.0f, 0.0f, 0);
  assert(near(out, 13.0f));
}

static float step_lim(zaru_heading_hold_t *s, float yaw, float rate, float acc,
                      const zaru_limits_t *limits)
{
  return zaru_heading_hold_update(s, yaw, rate, acc, 0.001f, 1, limits);
}

static void test_custom_limits(void)
{
  zaru_heading_hold_t s;
  zaru_limits_t lim, bad;
  unsigned i;
  zaru_limits_default(&lim);
  assert(zaru_limits_valid(&lim));
  assert(lim.enter_dps == 0.30f && lim.exit_dps == 0.70f && lim.acc_dev_ms2 == 0.15f);
  bad = lim;
  bad.exit_dps = bad.enter_dps;
  assert(!zaru_limits_valid(&bad));
  bad = lim;
  bad.enter_dps = 0.01f;
  assert(!zaru_limits_valid(&bad));
  bad = lim;
  bad.reserved = 1U;
  assert(!zaru_limits_valid(&bad));
  assert(!zaru_limits_valid(NULL));
  lim.enter_dps = 0.10f;
  lim.exit_dps = 0.40f;
  lim.acc_dev_ms2 = 0.08f;
  lim.enter_filter_ms = 10U;
  lim.enter_confirm_ms = 20U;
  lim.exit_confirm_ms = 4U;
  assert(zaru_limits_valid(&lim));
  zaru_heading_hold_reset(&s);
  for(i = 0; i < 40; i++) step_lim(&s, 3.0f, 0.20f, 0.0f, &lim);
  assert(!s.hold_active);
  zaru_heading_hold_reset(&s);
  for(i = 0; i < 19; i++) step_lim(&s, 3.0f, 0.05f, 0.02f, &lim);
  assert(!s.hold_active);
  assert(near(step_lim(&s, 3.0f, 0.05f, 0.02f, &lim), 3.0f));
  assert(s.hold_active);
  for(i = 0; i < 3; i++) assert(near(step_lim(&s, 3.0f, 0.50f, 0.0f, &lim), 3.0f));
  assert(s.hold_active);
  step_lim(&s, 3.2f, 0.50f, 0.0f, &lim);
  assert(!s.hold_active);
  zaru_heading_hold_reset(&s);
  for(i = 0; i < 30; i++) step_lim(&s, 1.0f, 0.05f, 0.10f, &lim);
  assert(!s.hold_active);
}

static void test_nan_keeps_hold(void)
{
  zaru_heading_hold_t s;
  zaru_heading_hold_reset(&s);
  settle(&s, 8.0f);
  assert(near(step(&s, NAN, 0.10f, 0.0f, 1), 8.0f));
  assert(s.hold_active);
}

int main(void)
{
  assert(APP_ZARU_ENABLE == 1U);
  assert(APP_ZARU_ENTER_DPS == 0.30f && APP_ZARU_EXIT_DPS == 0.70f);
  assert(APP_ZARU_ENTER_FILTER_MS == 10U);
  assert(APP_ZARU_ENTER_CONFIRM_MS == 50U && APP_ZARU_EXIT_CONFIRM_MS == 3U);
  assert(APP_ZARU_ACC_DEV_MS2 == 0.15f);
  assert(APP_ZARU_EXIT_DPS > APP_ZARU_ENTER_DPS);
  test_still_enters_after_confirm();
  test_spike_does_not_reset_enter();
  test_motion_does_not_lock();
  test_fast_exit();
  test_hysteresis_band();
  test_hold_drift_and_continuous_exit();
  test_wrap();
  test_low_rate_stays_held_without_vqf_rest();
  test_acc_gate_blocks_enter();
  test_manual_zero_while_held();
  test_roll_pitch_independent();
  test_profile_switch_is_continuous();
  test_custom_limits();
  test_nan_keeps_hold();
  puts("zaru heading hold: rms enter, hysteresis, fast exit, offset, wrap: OK");
  return 0;
}
