#include "gyro_prefilter.h"
#include <math.h>

/* Trapezoidal SVF. A direct-form biquad in float32 at 2 Hz / 8 kHz
 * quantizes to about 0.976 DC gain and would scale every turn. */
static struct {
  float a1, a2, a3;
  float ic1[3], ic2[3];
  float cutoff_hz, q, dt;
  uint8_t enabled, primed;
} g = {0, 0, 0, {0, 0, 0}, {0, 0, 0},
      GYRO_PREFILTER_DEFAULT_HZ, GYRO_PREFILTER_DEFAULT_Q, 0, 0, 0};

static int design(float cutoff_hz, float q, float dt)
{
  const double angle = 3.141592653589793 * (double)cutoff_hz * (double)dt;
  const double gtan = tan(angle);
  const double k = 1.0 / (double)q;
  const double denom = 1.0 + gtan * (gtan + k);
  const double a1 = 1.0 / denom;
  if(!(dt > 0.0f && dt <= 0.02f) || !(angle > 0.0 && angle < 1.4) || !(denom > 0.0)) return 0;
  g.a1 = (float)a1;
  g.a2 = (float)(gtan * a1);
  g.a3 = (float)(gtan * (double)g.a2);
  g.dt = dt;
  return 1;
}

void gyro_prefilter_configure(int enabled, float cutoff_hz, float q)
{
  if(!gyro_prefilter_params_ok(enabled, cutoff_hz, q)) return;
  if(g.enabled == (uint8_t)enabled && g.cutoff_hz == cutoff_hz && g.q == q) return;
  g.enabled = (uint8_t)enabled;
  g.cutoff_hz = cutoff_hz;
  g.q = q;
  g.primed = 0;
}

void gyro_prefilter_get(int *enabled, float *cutoff_hz, float *q)
{
  if(enabled) *enabled = g.enabled;
  if(cutoff_hz) *cutoff_hz = g.cutoff_hz;
  if(q) *q = g.q;
}

void gyro_prefilter_apply(float gyro[3], float dt)
{
  int axis;
  if(!g.enabled || !gyro) return;
  if(!g.primed || g.dt <= 0.0f || dt < g.dt * 0.99f || dt > g.dt * 1.01f) {
    if(!design(g.cutoff_hz, g.q, dt)) return;
    for(axis = 0; axis < 3; ++axis) {
      g.ic1[axis] = 0.0f;
      g.ic2[axis] = gyro[axis];
    }
    g.primed = 1;
    return;
  }
  for(axis = 0; axis < 3; ++axis) {
    const float x = gyro[axis];
    const float v3 = x - g.ic2[axis];
    const float v1 = g.a1 * g.ic1[axis] + g.a2 * v3;
    const float v2 = g.ic2[axis] + g.a2 * g.ic1[axis] + g.a3 * v3;
    g.ic1[axis] = 2.0f * v1 - g.ic1[axis];
    g.ic2[axis] = 2.0f * v2 - g.ic2[axis];
    gyro[axis] = v2;
  }
}
