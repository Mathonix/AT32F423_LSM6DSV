#include "gyro_startup_calibration.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
  gyro_startup_calibration_t s;
  const float gyro[3] = {0.001f, -0.002f, 0.0005f};
  const float gravity[3] = {0, 0, 9.80665f};
  const float motion[3] = {0.1f, 0, 0};
  float bias[3], acc[3];
  gyro_startup_calibration_init(&s, 2000);
  for(unsigned i = 0; i < 4000; ++i) assert(!gyro_startup_calibration_push(&s, 500 + i / 2, gyro, gravity));
  assert(gyro_startup_calibration_push(&s, 2500, gyro, gravity));
  assert(gyro_startup_calibration_result(&s, bias, acc));
  for(unsigned i = 0; i < 3; ++i) assert(fabsf(bias[i] - gyro[i]) < 0.000001f);
  assert(fabsf(acc[2] - gravity[2]) < 0.001f);

  gyro_startup_calibration_init(&s, 1000);
  for(unsigned i = 0; i < 1000; ++i) gyro_startup_calibration_push(&s, i / 2, gyro, gravity);
  assert(!gyro_startup_calibration_push(&s, 500, motion, gravity) && s.samples == 0);
  for(unsigned i = 0; i < 2000; ++i) assert(!gyro_startup_calibration_push(&s, 501 + i / 2, gyro, gravity));
  assert(gyro_startup_calibration_push(&s, 1501, gyro, gravity));

  gyro_startup_calibration_init(&s, 100);
  gyro_startup_calibration_push(&s, 0, gyro, gravity);
  gyro_startup_calibration_push(&s, 10, gyro, gravity);
  assert(s.start_ms == 10 && s.samples == 1);
  const float invalid[3] = {NAN, 0, 0};
  assert(!gyro_startup_calibration_push(&s, 11, invalid, gravity) && !s.samples);
  assert(!gyro_startup_calibration_result(&s, bias, acc));
  const float invalid_acc[3] = {0, 0, 0};
  assert(!gyro_startup_calibration_push(&s, 12, gyro, invalid_acc) && !s.samples);
  for(unsigned i = 0; i <= 200; ++i) gyro_startup_calibration_push(&s, 0xFFFFFFF0U + i / 2, gyro, gravity);
  assert(s.complete && gyro_startup_calibration_result(&s, bias, acc));
  /* Vibration below the old absolute 1 dps gate must reject the window. */
  gyro_startup_calibration_init(&s,1000);
  for(unsigned i=0;i<1000;i++) {
    float vibrating[3]={i%2 ? .008f : -.008f,0,0};
    gyro_startup_calibration_push(&s,i/2,vibrating,gravity);
  }
  assert(!s.complete && s.rejected_windows>0 && s.rejection_reason==4);
  /* A gentle gravity-direction change below the absolute acceleration
   * tolerance must not be accepted as a stationary calibration. */
  gyro_startup_calibration_init(&s,1000);
  for(unsigned i=0;i<1200;i++) {
    float angle=(float)i*.00004f;
    float tilted[3]={9.80665f*sinf(angle),0,9.80665f*cosf(angle)};
    gyro_startup_calibration_push(&s,i/2,gyro,tilted);
  }
  assert(!s.complete && s.rejected_windows>0);
  puts("startup zero bias: duration, mean, motion/gap rejection, invalid samples, timer wrap: OK");
  return 0;
}
