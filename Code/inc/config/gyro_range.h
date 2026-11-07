#ifndef GYRO_RANGE_H
#define GYRO_RANGE_H
#include <stdint.h>

/* LSM6DSV UI full scales and sensitivities from ST's lsm6dsv_reg driver:
 * https://github.com/STMicroelectronics/lsm6dsv-pid */
#define GYRO_RANGE_DEFAULT_DPS 1000U
static inline int gyro_range_valid(uint16_t dps)
{
  return dps == 125U || dps == 250U || dps == 500U || dps == 1000U ||
         dps == 2000U || dps == 4000U;
}
static inline uint8_t gyro_range_register(uint16_t dps)
{
  switch(dps) {
    case 125U: return 0x00U; case 250U: return 0x01U;
    case 500U: return 0x02U; case 1000U: return 0x03U;
    case 2000U: return 0x04U; case 4000U: return 0x0CU;
    default: return 0xFFU;
  }
}
static inline float gyro_range_dps_per_lsb(uint16_t dps)
{
  return gyro_range_valid(dps) ? (float)dps * 0.000035f : 0.0f;
}
#endif
