/* Validation-only, mutually exclusive DWT accounting. No estimator changes.
 * Hardware stays awake while polling: IDLE measures available processing time,
 * not sleep or power consumption. ISR wrappers include nested ISR attribution.
 * Software entry/exit boundaries leave small prologue/epilogue uncertainty.
 */
#include "eskf8k_validation.h"
#include "bsp.h"
#include "eskf8k_hotspot.h"
#include "sram_capture.h"
#include <string.h>

/* 32 words cover magic through the last bucket event. The old 40-word object
 * kept eight unused words that the 1 kHz pose record now needs. */
volatile uint32_t eskf8k_cpu_live[32];
static uint64_t cycles[CPU_BUCKETS];
static uint32_t events[CPU_BUCKETS], stack[8], depth, errors;
static uint32_t last, current, enabled, hook_cycles;
static RAM_CODE uint32_t lock(void) {
    uint32_t mask;
    __asm volatile("mrs %0, primask\n\tcpsid i":"=r"(mask)::"memory");
    return mask;
}
static RAM_CODE void unlock(uint32_t mask) {
    __asm volatile("msr primask, %0"::"r"(mask):"memory");
}
static RAM_CODE void account(void) {
    uint32_t now=dwt_cycles();cycles[current]+=(uint32_t)(now-last);last=now;
}
#if APP_ESKF8K_HOTSPOT
uint64_t eskf8k_cpu_exclusive_now(void) {
    uint32_t mask=lock();
    uint64_t value=enabled?cycles[current]+(uint32_t)(dwt_cycles()-last):dwt_cycles();
    unlock(mask);return value;
}
uint64_t eskf8k_cpu_total_now(void) {
    uint32_t mask=lock();uint64_t value=(uint32_t)(dwt_cycles()-last);
    for(unsigned i=0;i<CPU_BUCKETS;i++)value+=cycles[i];
    unlock(mask);return value;
}
#endif
void eskf8k_cpu_main(unsigned active) {
    uint32_t mask=lock();
    if(enabled){account();if(depth)errors++;current=active?CPU_MAIN:CPU_IDLE;events[current]++;}
    unlock(mask);
}
RAM_CODE void eskf8k_cpu_enter(unsigned bucket) {
    uint32_t mask=lock();
    if(enabled){account();if(depth<8)stack[depth++]=current;else errors++;current=bucket;events[bucket]++;}
    unlock(mask);
}
RAM_CODE void eskf8k_cpu_leave(void) {
    uint32_t mask=lock();
    if(enabled){account();if(depth)current=stack[--depth];else errors++;}
    unlock(mask);
}
void eskf8k_cpu_prepare(void) {
    /* Before capture is armed: measure complete hook cost, not PC wall time.
     * Calibration makes no calls into the estimator or sensor acquisition. */
    uint32_t mask=lock();enabled=1;current=CPU_IDLE;last=dwt_cycles();
    uint32_t start=dwt_cycles();
    for(unsigned i=0;i<1000;i++){
        eskf8k_cpu_main(1);eskf8k_cpu_enter(CPU_DMA);
        eskf8k_cpu_leave();eskf8k_cpu_main(0);
    }
    hook_cycles=(dwt_cycles()-start)/4000U;enabled=0;
    memset(cycles,0,sizeof(cycles));memset(events,0,sizeof(events));depth=errors=0;
    hot_prepare();
    unlock(mask);
}
void eskf8k_cpu_start(void) {
    uint32_t mask=lock();current=CPU_IDLE;last=dwt_cycles();enabled=1;unlock(mask);
}
void eskf8k_cpu_snapshot(void) {
    uint32_t mask=lock();account();
    eskf8k_cpu_live[1]++;__DMB();
    eskf8k_cpu_live[0]=0x43505531U;eskf8k_cpu_live[2]=system_core_clock;
    eskf8k_cpu_live[3]=CPU_BUCKETS;eskf8k_cpu_live[4]=hook_cycles;
    eskf8k_cpu_live[5]=depth;eskf8k_cpu_live[6]=errors;
    for(unsigned i=0;i<CPU_BUCKETS;i++){
        eskf8k_cpu_live[8+2*i]=(uint32_t)cycles[i];
        eskf8k_cpu_live[9+2*i]=(uint32_t)(cycles[i]>>32);
        eskf8k_cpu_live[24+i]=events[i];
    }
    __DMB();eskf8k_cpu_live[1]++;unlock(mask);
}

void imu8k_exint_body(void);
void imu8k_dma_body(void);
RAM_CODE void EXINT0_IRQHandler(void) {
    sram_stack_probe();
    eskf8k_cpu_enter(CPU_DRDY);imu8k_exint_body();eskf8k_cpu_leave();
}
RAM_CODE void DMA1_Channel2_IRQHandler(void) {
    sram_stack_probe();
    eskf8k_cpu_enter(CPU_DMA);imu8k_dma_body();eskf8k_cpu_leave();
}
