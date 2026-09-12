#ifndef VQF_LIVE_H
#define VQF_LIVE_H

#include <stdint.h>

#define VQF_LIVE_MAGIC 0x56465131u /* "VQF1" */

typedef struct
{
  uint32_t magic;
  uint32_t seq;
  int32_t  init_err;
  uint32_t whoami;
  uint32_t clk_hz;
  uint32_t millis;
  uint32_t fusion_hz;
  uint32_t out_hz;
  uint32_t fusion_n;
  uint32_t skip_n;
  uint32_t vqf_us;
  float roll;
  float pitch;
  float yaw;
  float qw;
  float qx;
  float qy;
  float qz;
  float gx;
  float gy;
  float gz;
  float ax;
  float ay;
  float az;
} vqf_live_t;

extern volatile vqf_live_t vqf_live;

#endif
