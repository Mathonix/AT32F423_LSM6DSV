#ifndef IMU8K_CAPTURE_H
#define IMU8K_CAPTURE_H
#include "lsm6dsv.h"
#include "lsm6dsv_8k.h"
typedef struct {
    uint32_t irq_n,valid_n,stale_n,spi_error_n,overrun_n,busy_n,max_depth;
    uint32_t gap_n,min_interval,max_interval,interval_n;
    uint64_t interval_sum;
    uint32_t spi_n,spi_min,spi_max;
    uint64_t spi_sum;
    uint32_t irq_body_n,irq_body_max,dma_body_n,dma_body_max;
    uint64_t irq_body_sum,dma_body_sum;
    uint32_t repeated_raw_n,consumed_n;
    uint32_t irq_hist[128],spi_hist[128],dma_hist[128];
} capture_stats_t;
extern volatile capture_stats_t capture_stats;
extern volatile uint32_t capture_registers[6];
/* TMR3_CH3 hardware capture on PB0: valid, missing flag, overcapture,
 * latency min/max, DWT/CNT bracket max, estimated missing periods, histogram. */
extern volatile uint32_t capture_edge[135],capture_timer_period_n;
extern volatile uint64_t capture_edge_latency_sum,capture_timer_period_sum;
int imu8k_begin(unsigned gyro_range,unsigned acc_range);
void imu8k_end(void);
int imu8k_pop(lsm6dsv_raw_t* raw,uint32_t* cycles);
int imu8k_pending(void);
#endif
