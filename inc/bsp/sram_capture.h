#ifndef SRAM_CAPTURE_H
#define SRAM_CAPTURE_H
#include "app_config.h"
#include <stdint.h>
#if APP_SRAM_CAPTURE
#define RAM_CODE __attribute__((section(".ramfunc")))
enum {
 SR_MAGIC,SR_RAM_BYTES,SR_VECTOR_BYTES,SR_FRAME_BYTES,SR_RING_CAPACITY,
 SR_STACK_PAINT_LOW,SR_STACK_PAINT_TOP,SR_STACK_LOWEST,SR_STACK_USED,
 SR_SAVE_BEGIN,SR_SAVE_END,SR_SAVE_DEFERRED,SR_PROTECTED_REJECTS,
 SR_PRODUCED_HASH,SR_CONSUMED_HASH,SR_EMPTY_CHECKS,SR_HASH_ERRORS,
 SR_POSE_N,SR_POSE_LAST_CY,SR_POSE_MAX_GAP,SR_POSE_MIN_GAP,SR_POSE_SAMPLE_CY,
 SR_RECOVERY_N,SR_RECOVERY_START,SR_RECOVERY_MAX_CY,SR_RECOVERY_LAST_CY,
 SR_SAVE_DEPTH_MAX,SR_QUEUE_HIGH,SR_QUEUE_REJECTS,SR_GUARD_ERRORS,
 SR_TX_DMA_FRAMES,SR_TX_DMA_BYTES,
 SR_POSE_SESSION,SR_POSE_STREAM_N,SR_POSE_STREAM_MAX_GAP,SR_POSE_STREAM_MIN_GAP,
 SR_POSE_MAINT_N,SR_POSE_MAINT_MAX_GAP,SR_MAINT_ENTRIES,SR_MAINT_FLASH_N,SR_WORDS
};
extern volatile uint32_t sram_live[SR_WORDS];
void sram_capture_init(void);
void sram_capture_poll(void);
void sram_recovery_poll(void);
RAM_CODE int sram_flash_ready(void);
RAM_CODE int sram_flash_active(void);
RAM_CODE void sram_flash_begin(void);
RAM_CODE void sram_flash_end(void);
void sram_pose_generated(uint32_t sample_cycles, int maintenance);
static inline __attribute__((always_inline)) void sram_stack_probe(void) {
 uint32_t sp;__asm volatile("mrs %0, msp":"=r"(sp));
 if(sp<sram_live[SR_STACK_LOWEST])sram_live[SR_STACK_LOWEST]=sp;
}
static inline __attribute__((always_inline)) uint32_t sram_sample_hash(uint32_t h,const void *raw,uint32_t stamp) {
 const unsigned char *w=(const unsigned char *)raw;
 h=(h^stamp)*16777619U;
 for(unsigned i=0;i<14;i++)h=(h^w[i])*16777619U;
 return h;
}
#else
#define RAM_CODE
#define sram_capture_init() ((void)0)
#define sram_capture_poll() ((void)0)
#define sram_stack_probe() ((void)0)
#endif
#endif
