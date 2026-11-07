#ifndef PERF_METER_H
#define PERF_METER_H
#include <stdint.h>
enum { PERF_MAIN, PERF_GYRO, PERF_ACCEL, PERF_POSE, PERF_IMU_IRQ,
       PERF_USB_IRQ, PERF_UART_IRQ, PERF_SYSTICK_IRQ, PERF_ACCEL_KF, PERF_IDLE,
       PERF_PREWORK, PERF_AUXILIARY, PERF_SNAPSHOT, PERF_SERVICE, PERF_EMIT,
       PERF_IMU_DMA, PERF_BUCKETS };
typedef struct {uint32_t cycle,irq;} perf_mark_t;
void perf_begin(void);
perf_mark_t perf_mark(void);
uint32_t perf_record(unsigned bucket,perf_mark_t start);
void perf_record_cycles(unsigned bucket,uint32_t cycles);
void perf_loop_begin(void);
void perf_loop_end(void);
uint32_t perf_irq_enter(void);
void perf_irq_exit(unsigned bucket,uint32_t start);
void perf_snapshot(unsigned page,uint32_t out[16]);
#endif
