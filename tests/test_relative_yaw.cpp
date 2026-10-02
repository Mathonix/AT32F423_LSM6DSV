#include <cassert>
#include <cmath>
#include <cstdio>
#include "vqf.h"
#include "yaw_reference.h"

static void near(float got, float expected, float tolerance = 0.002f)
{
  assert(std::fabs(yaw_reference_wrap(got - expected)) < tolerance);
}

static void frame_changes()
{
  yaw_reference_t reference = {};
  near(yaw_reference_apply(&reference, 12, 0, 0), 0);
  near(yaw_reference_apply(&reference, 37, 0, 0), 25);
  near(yaw_reference_apply(&reference, 137, 100, 1), 25);
  near(yaw_reference_apply(&reference, 143, 101, 1), 31); // later magnetic correction is retained

  yaw_reference_t crossing = {};
  near(yaw_reference_apply(&crossing, 175, 0, 0), 0);
  near(yaw_reference_apply(&crossing, -175, 0, 0), 10);
  near(yaw_reference_apply(&crossing, -85, 90, 1), 10);

  yaw_reference_t already_magnetic = {};
  near(yaw_reference_apply(&already_magnetic, -120, -90, 1), 0);
  near(yaw_reference_apply(&already_magnetic, -100, -90, 1), 20);
}

static void stationary_vqf(bool magnetic_before_output, const float acc[3])
{
  vqf_init(0.0005f, 0.0005f);
  const float gyr[3] = {0, 0, 0};
  const float mag[3] = {30, 0, 15};
  vqf_prime_rest(acc, gyr);
  yaw_reference_t reference = {};
  float old_offset = 0, maximum_relative = 0, old_final = 0;
  bool first = true;
  for(unsigned i = 0; i < 16000; ++i) {
    vqf_update(gyr, acc);
    if((i % 200) == 0 && (magnetic_before_output || i >= 200)) assert(vqf_update_mag(mag) == 0);
    float roll, pitch, yaw;
    vqf_get_euler_deg(&roll, &pitch, &yaw);
    if(first) { old_offset = yaw; first = false; }
    const float relative = yaw_reference_apply(&reference, yaw, vqf_get_mag_delta_deg(), vqf_get_mag_ready());
    maximum_relative = std::fmax(maximum_relative, std::fabs(relative));
    old_final = yaw_reference_wrap(yaw - old_offset);
  }
  assert(vqf_get_mag_ready());
  assert(maximum_relative < 0.02f);
  if(!magnetic_before_output) assert(std::fabs(old_final) > 20); // reproduces the old startup jump
  float roll, pitch, yaw;
  vqf_get_euler_deg(&roll, &pitch, &yaw);
  if(acc[0] != 0) assert(std::fabs(pitch) > 10); // gravity inclination is not zeroed
}

int main()
{
  frame_changes();
  vqf_init(.0005f,.0005f);
  const float bias[3]={.001f,-.002f,.003f};
  vqf_seed_gyr_bias(bias,.05f);
  assert(std::fabs(vqf_get_bias_sigma_dps()-.05f)<.00001f);
  float got[3];vqf_get_gyr_bias(got);
  for(unsigned i=0;i<3;i++) assert(std::fabs(got[i]-bias[i])<1e-7f);
  const float level[3] = {0, 0, 9.80665f};
  const float tilted[3] = {4.903325f, 0, 8.492808f};
  stationary_vqf(false, level);
  stationary_vqf(true, level);
  stationary_vqf(false, tilted);
  std::puts("PASS relative yaw: startup magnetic frame, delayed magnetometer, rotation, angle wrap, gravity inclination");
}
