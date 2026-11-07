#ifndef ACC_RANGE_H
#define ACC_RANGE_H
#include <stdint.h>

/* LSM6DSV CTRL8 FS_XL and nominal sensitivity, ST lsm6dsv_reg driver. */
#define ACC_RANGE_DEFAULT_G 4U
static inline int acc_range_valid(uint8_t g)
{
  return g == 2U || g == 4U || g == 8U || g == 16U;
}
static inline uint8_t acc_range_register(uint8_t g)
{
  switch(g) {
    case 2U: return 0x00U; case 4U: return 0x01U;
    case 8U: return 0x02U; case 16U: return 0x03U;
    default: return 0xFFU;
  }
}
static inline float acc_range_g_per_lsb(uint8_t g)
{
  return acc_range_valid(g) ? (float)g * 0.0000305f : 0.0f;
}
#endif
