#ifndef ESKF8K_VALIDATION_H
#define ESKF8K_VALIDATION_H
#include "app_config.h"
#include <stdint.h>
#ifndef APP_ESKF8K_VALIDATE
#define APP_ESKF8K_VALIDATE 0
#endif
#if APP_ESKF8K_VALIDATE
enum { CPU_IDLE, CPU_MAIN, CPU_DRDY, CPU_DMA, CPU_USB, CPU_UART, CPU_TICK, CPU_BUCKETS };
void eskf8k_cpu_prepare(void);
void eskf8k_cpu_start(void);
void eskf8k_cpu_main(unsigned active);
void eskf8k_cpu_enter(unsigned bucket);
void eskf8k_cpu_leave(void);
void eskf8k_cpu_snapshot(void);
#else
#define eskf8k_cpu_prepare() ((void)0)
#define eskf8k_cpu_start() ((void)0)
#define eskf8k_cpu_main(active) ((void)0)
#define eskf8k_cpu_enter(bucket) ((void)0)
#define eskf8k_cpu_leave() ((void)0)
#define eskf8k_cpu_snapshot() ((void)0)
#endif
#endif
