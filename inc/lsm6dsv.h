#ifndef __LSM6DSV_H
#define __LSM6DSV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f423.h"

#define LSM6DSV_WHO_AM_I_REG  0x0FU
#define LSM6DSV_WHO_AM_I_VAL  0x70U

#define LSM6DSV_ODR_HZ        2000U

typedef struct
{
  int16_t gyr[3];
  int16_t acc[3];
} lsm6dsv_raw_t;

void lsm6dsv_spi_init(void);
int lsm6dsv_spi_recover(void);
int lsm6dsv_probe_whoami(uint8_t who_mode[4]);
int lsm6dsv_read_reg(uint8_t reg, uint8_t *value);
int lsm6dsv_write_reg(uint8_t reg, uint8_t value);
int lsm6dsv_init_2khz(void);
int lsm6dsv_data_ready(void);
int lsm6dsv_wait_sample(uint32_t timeout_us);
int lsm6dsv_read_raw(lsm6dsv_raw_t *raw);

#ifdef __cplusplus
}
#endif

#endif
