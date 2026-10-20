#include "sram_capture.h"
#include "bsp.h"
#include "imu8k_capture.h"
#include "heap_guard.h"
#include <string.h>
extern uint32_t _siramfunc,_sramfunc,_eramfunc,_flash_vectors_end;
extern uint32_t g_pfnVectors[],__heap_limit,_estack;
extern void imu8k_hash_check(void);
volatile uint32_t sram_live[SR_WORDS];
static uint32_t sram_vectors[128] __attribute__((section(".ram_vectors"),aligned(512)));
static uint32_t saved_iser[4],saved_systick,saved_system_pending,flash_depth;
static uint32_t recovering;

void sram_capture_init(void)
{
 heap_guard_init();
 uint32_t *src=&_siramfunc,*dst=&_sramfunc;
 while(dst<&_eramfunc)*dst++=*src++;
 __DSB();__ISB();
 unsigned bytes=(uint32_t)&_flash_vectors_end-(uint32_t)g_pfnVectors;
 for(unsigned i=0;i<128;i++)sram_vectors[i]=0;
 memcpy(sram_vectors,g_pfnVectors,bytes);
 uint32_t mask=__get_PRIMASK();__disable_irq();
 SCB->VTOR=(uint32_t)sram_vectors;__DSB();__ISB();__set_PRIMASK(mask);
 sram_live[SR_MAGIC]=0x53524131U;
 sram_live[SR_RAM_BYTES]=(uint32_t)&_eramfunc-(uint32_t)&_sramfunc;
 sram_live[SR_VECTOR_BYTES]=sizeof(sram_vectors);
 uint32_t sp=__get_MSP();sram_live[SR_STACK_LOWEST]=sp;
 uint32_t *top=(uint32_t *)((sp-64U)&~7U);
 sram_live[SR_STACK_PAINT_LOW]=(uint32_t)&__heap_limit;
 sram_live[SR_STACK_PAINT_TOP]=(uint32_t)top;
 for(uint32_t *p=&__heap_limit;p<top;p++)*p=0xa5a5a5a5U;
}

RAM_CODE int sram_flash_ready(void)
{
 /* At most 8 old samples before a new erase. A full 256-frame ring covers
  * 32 ms; measured settings saves were 17.3 ms, not a guaranteed upper bound. */
 return !__get_PRIMASK() && SCB->VTOR==(uint32_t)sram_vectors && !flash_depth && imu8k_pending()<=8;
}

RAM_CODE int sram_flash_active(void){return flash_depth!=0;}

RAM_CODE void sram_flash_begin(void)
{
 uint32_t mask=__get_PRIMASK();__disable_irq();
 if(!flash_depth++) {
  /* Preserve pending peripheral IRQs, mask their NVIC enables. Only EXINT0
   * and DMA1 CH2 remain able to enter while Flash cannot be fetched. */
  for(unsigned i=0;i<4;i++){
   saved_iser[i]=NVIC->ISER[i];
   uint32_t keep=i==0?((1U<<EXINT0_IRQn)|(1U<<DMA1_Channel2_IRQn)):0U;
   NVIC->ICER[i]=saved_iser[i]&~keep;
  }
  saved_systick=SysTick->CTRL;saved_system_pending=SCB->ICSR&((1U<<26)|(1U<<28));
  SysTick->CTRL=saved_systick&~2U;SCB->ICSR=(1U<<25)|(1U<<27);
  sram_live[SR_SAVE_BEGIN]++;
  uint32_t depth=imu8k_pending();
  if(depth>sram_live[SR_SAVE_DEPTH_MAX])sram_live[SR_SAVE_DEPTH_MAX]=depth;
 }
 __DSB();__ISB();__set_PRIMASK(mask);
}

RAM_CODE void sram_flash_end(void)
{
 uint32_t mask=__get_PRIMASK();__disable_irq();
 if(!flash_depth){sram_live[SR_GUARD_ERRORS]++;__set_PRIMASK(mask);return;}
 if(!--flash_depth) {
  sram_live[SR_SAVE_END]++;
  if(!recovering){recovering=1;sram_live[SR_RECOVERY_START]=dwt_cycles();}
  for(unsigned i=0;i<4;i++)NVIC->ISER[i]=saved_iser[i];
  SysTick->CTRL=saved_systick;SCB->ICSR=saved_system_pending;
 }
 __DSB();__ISB();__set_PRIMASK(mask);
}

/* Called from the existing 100 ms diagnostics, not every IMU sample. */
void sram_capture_poll(void)
{
 uint32_t *p=&__heap_limit,*top=(uint32_t *)sram_live[SR_STACK_PAINT_TOP];
 while(p<top && *p==0xa5a5a5a5U)p++;
 uint32_t low=(uint32_t)p;
 if(low<sram_live[SR_STACK_LOWEST])sram_live[SR_STACK_LOWEST]=low;
 sram_live[SR_STACK_USED]=(uint32_t)&_estack-sram_live[SR_STACK_LOWEST];
 imu8k_hash_check();
}

void sram_recovery_poll(void)
{
 imu8k_hash_check();
 if(recovering && imu8k_pending()<=8){
  uint32_t d=dwt_cycles()-sram_live[SR_RECOVERY_START];
  sram_live[SR_RECOVERY_LAST_CY]=d;
  if(d>sram_live[SR_RECOVERY_MAX_CY])sram_live[SR_RECOVERY_MAX_CY]=d;
  sram_live[SR_RECOVERY_N]++;recovering=0;
 }
}

void sram_pose_generated(uint32_t sample_cycles, int maintenance)
{
 uint32_t now=dwt_cycles(),last=sram_live[SR_POSE_LAST_CY];
 if(last){uint32_t d=now-last;
  if(d>sram_live[SR_POSE_MAX_GAP])sram_live[SR_POSE_MAX_GAP]=d;
  if(!sram_live[SR_POSE_MIN_GAP]||d<sram_live[SR_POSE_MIN_GAP])sram_live[SR_POSE_MIN_GAP]=d;
  if(maintenance){
   sram_live[SR_POSE_MAINT_N]++;
   if(d>sram_live[SR_POSE_MAINT_MAX_GAP])sram_live[SR_POSE_MAINT_MAX_GAP]=d;
  }else{
   sram_live[SR_POSE_STREAM_N]++;
   if(d>sram_live[SR_POSE_STREAM_MAX_GAP])sram_live[SR_POSE_STREAM_MAX_GAP]=d;
   if(!sram_live[SR_POSE_STREAM_MIN_GAP]||d<sram_live[SR_POSE_STREAM_MIN_GAP])
    sram_live[SR_POSE_STREAM_MIN_GAP]=d;
  }
 }
 sram_live[SR_POSE_N]++;sram_live[SR_POSE_LAST_CY]=now;sram_live[SR_POSE_SAMPLE_CY]=sample_cycles;
}
