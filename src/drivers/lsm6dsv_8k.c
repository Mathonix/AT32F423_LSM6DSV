/* LSM6DSV 8 kHz capture: PB0 / EXINT0 -> SPI1 burst -> 64-sample ring.
 * DRDY timestamp is acquired before the SPI transfer. This first revision
 * favors testability over DMA; scope ISR duration and USB/CAN interaction.
 */
#include "lsm6dsv_8k.h"
#include "bsp.h"
#include "at32f423_scfg.h"
#include <string.h>

/* EXINT0_IRQHandler below is referenced by the vector table, so an unguarded
 * definition would pull this module -- and its ring -- into every image even
 * when nothing arms it. Compile it only for builds that use the capture. */
#if APP_IMU8K_CAPTURE_TEST

#if APP_SRAM_CAPTURE
#define RING_LEN 256U
#else
#define RING_LEN 64U
#endif
#define RING_MASK (RING_LEN-1U)
#define LINE0 1U
#define EXINT_INTEN  REG32(EXINT_BASE+0x00U)
#define EXINT_RISING REG32(EXINT_BASE+0x08U)
#define EXINT_FALLING REG32(EXINT_BASE+0x0CU)
#define EXINT_PENDING REG32(EXINT_BASE+0x14U)

typedef struct { lsm6dsv_raw_t raw; uint32_t cycles; } sample_t;
static sample_t ring[RING_LEN];
static volatile uint32_t write_n,read_n,overrun_n,irq_n,spi_error_n;
static volatile uint8_t active;

void lsm6dsv_8k_capture_stop(void) {
  EXINT_INTEN &= ~LINE0;
  nvic_irq_disable(EXINT0_IRQn);
  active=0U;
}
void lsm6dsv_8k_capture_start(void) {
  lsm6dsv_raw_t dummy;
  lsm6dsv_8k_capture_stop();
  write_n=read_n=overrun_n=irq_n=spi_error_n=0U;
  (void)lsm6dsv_read_raw(&dummy); /* clear latched DRDY before arming */
  crm_periph_clock_enable(CRM_SCFG_PERIPH_CLOCK,TRUE);
  scfg_exint_line_config(SCFG_PORT_SOURCE_GPIOB,SCFG_PINS_SOURCE0);
  EXINT_INTEN &= ~LINE0;
  EXINT_RISING |= LINE0;
  EXINT_FALLING &= ~LINE0;
  EXINT_PENDING = LINE0;
  active=1U;
  nvic_irq_enable(EXINT0_IRQn,0,0);
  EXINT_INTEN |= LINE0;
}
void EXINT0_IRQHandler(void) {
  if((EXINT_PENDING&LINE0)==0U)return;
  EXINT_PENDING=LINE0;
  if(!active)return;
  const uint32_t timestamp=dwt_cycles();
  lsm6dsv_raw_t raw;
  irq_n++;
  if(lsm6dsv_read_raw(&raw)!=0){spi_error_n++;return;}
  const uint32_t head=write_n;
  if(head-read_n>=RING_LEN){overrun_n++;return;}
  sample_t *slot=&ring[head&RING_MASK];
  slot->raw=raw;
  slot->cycles=timestamp;
  __DMB();
  write_n=head+1U;
}
int lsm6dsv_8k_pop(lsm6dsv_raw_t *raw,uint32_t *cycles) {
  const uint32_t tail=read_n;
  if(!raw||!cycles||tail==write_n)return 0;
  const sample_t *slot=&ring[tail&RING_MASK];
  *raw=slot->raw;*cycles=slot->cycles;
#if APP_SRAM_CAPTURE
  sram_live[SR_CONSUMED_HASH]=sram_sample_hash(sram_live[SR_CONSUMED_HASH],raw,*cycles);
#endif
  __DMB();
  read_n=tail+1U;
  return 1;
}
uint32_t lsm6dsv_8k_overruns(void){return overrun_n;}
uint32_t lsm6dsv_8k_irq_count(void){return irq_n;}
uint32_t lsm6dsv_8k_spi_errors(void){return spi_error_n;}
#endif /* APP_IMU8K_CAPTURE_TEST */
