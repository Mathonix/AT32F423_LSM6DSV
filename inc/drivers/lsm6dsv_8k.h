#ifndef LSM6DSV_8K_H
#define LSM6DSV_8K_H
#include "lsm6dsv.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental high-priority EXINT0 capture into a bounded SPSC ring.
 * Must call after normal 2 kHz startup bias calibration has finished.
 * IRQ performs blocking 14-byte SPI read; future DMA migration possible.
 * Never call any other lsm6dsv SPI read/write while the capture is enabled.
 */
int lsm6dsv_8k_set_odr_8k(void);
void lsm6dsv_8k_capture_start(void);
void lsm6dsv_8k_capture_stop(void);
int lsm6dsv_8k_pop(lsm6dsv_raw_t *raw,uint32_t *drdy_cycles);
uint32_t lsm6dsv_8k_overruns(void);
uint32_t lsm6dsv_8k_irq_count(void);
uint32_t lsm6dsv_8k_spi_errors(void);
#ifdef __cplusplus
}
#endif
#endif
