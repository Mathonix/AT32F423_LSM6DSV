/* PB0 EXINT0 -> SPI1 RX/TX DMA -> bounded SPSC ring.
 * Timestamp is ISR entry, not hardware edge capture. Never run other SPI1
 * transactions while armed. No Flash-busy/SRAM execution claim is made.
 */
#include "lsm6dsv_8k.h"
#include "bsp.h"
#include "at32f423_scfg.h"
#include <string.h>
#include "perf_meter.h"

/* EXINT0_IRQHandler below is referenced by the vector table, so an unguarded
 * definition would pull this module -- and its ring -- into every image even
 * when nothing arms it. Compile it only for builds that use the capture. */
#if APP_IMU8K_CAPTURE_TEST || APP_IMU8K_ENGINE

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
/* DMA1 ch1 belongs to UART TX. Only this driver owns ch2 RX / ch3 TX.
 * Equal preemption priority prevents DRDY and completion from modifying the
 * transfer/ring producer concurrently; foreground lifecycle uses PRIMASK. */
static uint16_t dma_tx[8]={0xA000U,0,0,0,0,0,0,0}; /* read OUT_TEMP_L=0x20 */
static volatile uint16_t dma_rx[8];
static uint8_t dma_ready;
static volatile uint8_t dma_busy;
static uint32_t dma_timestamp;
static uint32_t dma_started,dma_completed,dma_busy_n,dma_timeout_n,dma_error_n;
static uint32_t ring_high,max_transfer_cycles,stop_abort_n,spi_not_idle_n;

static void dma_halt(void) {
  spi_i2s_dma_transmitter_enable(SPI1,FALSE);
  spi_i2s_dma_receiver_enable(SPI1,FALSE);
  dma_channel_enable(DMA1_CHANNEL3,FALSE);
  dma_channel_enable(DMA1_CHANNEL2,FALSE);
  dma_flag_clear(DMA1_GL2_FLAG|DMA1_GL3_FLAG);
}
static void dma_abort(void) {
  dma_halt();
  /* Abort the peripheral too: don't leave an incomplete word clocking into
   * the next transaction. Only used on failure/explicit capture stop. */
  spi_enable(SPI1,FALSE);
  GPIOA->scr=GPIO_PINS_4;
  (void)SPI1->dt;(void)SPI1->sts;
  spi_enable(SPI1,TRUE);
  __DMB();dma_busy=0U;
}
static void capture_dma_init(void) {
  dma_init_type d;
  crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK,TRUE);
  dma_reset(DMA1_CHANNEL2);dma_reset(DMA1_CHANNEL3);
  dmamux_enable(DMA1,TRUE);
  dmamux_init(DMA1MUX_CHANNEL2,DMAMUX_DMAREQ_ID_SPI1_RX);
  dmamux_init(DMA1MUX_CHANNEL3,DMAMUX_DMAREQ_ID_SPI1_TX);
  dma_default_para_init(&d);
  d.peripheral_base_addr=(uint32_t)&SPI1->dt;
  d.memory_base_addr=(uint32_t)dma_rx;
  d.direction=DMA_DIR_PERIPHERAL_TO_MEMORY;d.buffer_size=8;
  d.peripheral_inc_enable=FALSE;d.memory_inc_enable=TRUE;
  d.peripheral_data_width=DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
  d.memory_data_width=DMA_MEMORY_DATA_WIDTH_HALFWORD;
  d.loop_mode_enable=FALSE;d.priority=DMA_PRIORITY_VERY_HIGH;
  dma_init(DMA1_CHANNEL2,&d);
  d.memory_base_addr=(uint32_t)dma_tx;d.direction=DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init(DMA1_CHANNEL3,&d);
  dma_interrupt_enable(DMA1_CHANNEL2,DMA_FDT_INT|DMA_DTERR_INT,TRUE);
  dma_interrupt_enable(DMA1_CHANNEL3,DMA_DTERR_INT,TRUE);
  nvic_irq_enable(DMA1_Channel2_IRQn,0,0);
  nvic_irq_enable(DMA1_Channel3_IRQn,0,0);
  dma_ready=1U;
}

/* High-accuracy 8 kHz. These are the same four writes the branch's
 * imu8k_begin() makes; 0x1C is the HA01 8 kHz code, 0x1A was 2 kHz.
 * CTRL1/CTRL2 go to zero first so the ODR change is a clean re-arm. */
int lsm6dsv_8k_set_odr_8k(void)
{
  if(lsm6dsv_write_reg(0x10U,0x00U) || lsm6dsv_write_reg(0x11U,0x00U) ||
     lsm6dsv_write_reg(0x62U,0x01U) || lsm6dsv_write_reg(0x13U,0x0AU)) return -1;
  if(lsm6dsv_write_reg(0x10U,0x1CU) || lsm6dsv_write_reg(0x11U,0x1CU)) return -2;
  return 0;
}

void lsm6dsv_8k_capture_stop(void) {
  uint32_t p=__get_PRIMASK();__disable_irq();
  EXINT_INTEN &= ~LINE0;
  nvic_irq_disable(EXINT0_IRQn);
  active=0U;
  if(dma_ready) {
    nvic_irq_disable(DMA1_Channel2_IRQn);nvic_irq_disable(DMA1_Channel3_IRQn);
    if(dma_busy){stop_abort_n++;dma_abort();}else dma_halt();
    NVIC_ClearPendingIRQ(DMA1_Channel2_IRQn);NVIC_ClearPendingIRQ(DMA1_Channel3_IRQn);
  }
  __set_PRIMASK(p);
}
void lsm6dsv_8k_capture_start(void) {
  lsm6dsv_raw_t dummy;
  lsm6dsv_8k_capture_stop();
  write_n=read_n=overrun_n=irq_n=spi_error_n=0U;
  dma_started=dma_completed=dma_busy_n=dma_timeout_n=dma_error_n=0U;
  ring_high=max_transfer_cycles=stop_abort_n=spi_not_idle_n=0U;
  (void)lsm6dsv_read_raw(&dummy); /* clear latched DRDY before arming */
  capture_dma_init();
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
static void capture_irq_body(void) {
  if((EXINT_PENDING&LINE0)==0U)return;
  EXINT_PENDING=LINE0;
  if(!active)return;
  const uint32_t timestamp=dwt_cycles();
  irq_n++;
  if(dma_busy) {
    dma_busy_n++; /* This DRDY is lost, never relabel the in-flight buffer. */
    if((uint32_t)(timestamp-dma_timestamp)>=system_core_clock/10000U) {
      dma_timeout_n++;spi_error_n++;dma_abort();
    }
    return;
  }
  if(SPI1->sts&(SPI_I2S_BF_FLAG|SPI_I2S_RDBF_FLAG|SPI_I2S_ROERR_FLAG)) {
    spi_not_idle_n++;spi_error_n++;dma_abort();return;
  }
  dma_flag_clear(DMA1_GL2_FLAG|DMA1_GL3_FLAG);
  dma_data_number_set(DMA1_CHANNEL2,8);dma_data_number_set(DMA1_CHANNEL3,8);
  dma_timestamp=timestamp;dma_busy=1U;dma_started++;
  __DMB();
  GPIOA->clr=GPIO_PINS_4;
  /* RX is armed before TX may generate any clocks. Preserve 16-bit SPI mode
   * and the original 16 wire bytes, including the last unused dummy byte. */
  dma_channel_enable(DMA1_CHANNEL2,TRUE);
  spi_i2s_dma_receiver_enable(SPI1,TRUE);
  dma_channel_enable(DMA1_CHANNEL3,TRUE);
  spi_i2s_dma_transmitter_enable(SPI1,TRUE);
}
static void capture_dma_body(void) {
  const uint8_t error=(dma_flag_get(DMA1_DTERR2_FLAG)!=RESET)||
                      (dma_flag_get(DMA1_DTERR3_FLAG)!=RESET);
  if(!dma_busy || !active) {dma_flag_clear(DMA1_GL2_FLAG|DMA1_GL3_FLAG);return;}
  if(error) {dma_error_n++;spi_error_n++;dma_abort();return;}
  if(dma_flag_get(DMA1_FDT2_FLAG)==RESET)return;
  /* RX TC means last RX word exists. Allow at most 2us for trailing hardware
   * busy status, never a whole blocking SPI transfer in this IRQ. */
  uint32_t start=dwt_cycles();
  while(SPI1->sts&SPI_I2S_BF_FLAG) {
    if((uint32_t)(dwt_cycles()-start)>=system_core_clock/500000U) {
      dma_timeout_n++;spi_error_n++;dma_abort();return;
    }
  }
  if((SPI1->sts&SPI_I2S_ROERR_FLAG) || dma_flag_get(DMA1_FDT3_FLAG)==RESET) {
    dma_error_n++;spi_error_n++;dma_abort();return;
  }
  dma_halt();GPIOA->scr=GPIO_PINS_4;
  __DMB(); /* DMA is stopped; main/ISR must not read while it owns RX. */
  uint8_t bytes[14];bytes[0]=(uint8_t)dma_rx[0];
  for(unsigned i=1;i<14;i++)
    bytes[i]=(uint8_t)((i&1U)?dma_rx[(i+1U)/2U]>>8:dma_rx[(i+1U)/2U]);
  lsm6dsv_raw_t raw;
  raw.temp_raw=(int16_t)((uint16_t)bytes[0]|((uint16_t)bytes[1]<<8));
  for(unsigned i=0;i<3;i++) {
    raw.gyr[i]=(int16_t)((uint16_t)bytes[2+2*i]|((uint16_t)bytes[3+2*i]<<8));
    raw.acc[i]=(int16_t)((uint16_t)bytes[8+2*i]|((uint16_t)bytes[9+2*i]<<8));
  }
  dma_completed++;
  const uint32_t elapsed=dwt_cycles()-dma_timestamp;
  if(elapsed>max_transfer_cycles)max_transfer_cycles=elapsed;
  const uint32_t head=write_n;
  if(head-read_n>=RING_LEN)overrun_n++;
  else {
    sample_t *slot=&ring[head&RING_MASK];slot->raw=raw;slot->cycles=dma_timestamp;
    __DMB();write_n=head+1U;
    const uint32_t depth=head+1U-read_n;if(depth>ring_high)ring_high=depth;
  }
  __DMB();dma_busy=0U;
}
void EXINT0_IRQHandler(void) {
  uint32_t mark=perf_irq_enter();
  capture_irq_body();
  perf_irq_exit(PERF_IMU_IRQ,mark);
}
void DMA1_Channel2_IRQHandler(void) {
  uint32_t mark=perf_irq_enter();capture_dma_body();perf_irq_exit(PERF_IMU_DMA,mark);
}
void DMA1_Channel3_IRQHandler(void) {
  uint32_t mark=perf_irq_enter();capture_dma_body();perf_irq_exit(PERF_IMU_DMA,mark);
}
void lsm6dsv_8k_dma_snapshot(uint32_t out[16]) {
  uint32_t p=__get_PRIMASK();__disable_irq();
  const uint32_t d[16]={1U,active,dma_busy,irq_n,dma_started,dma_completed,write_n,
    dma_busy_n,dma_error_n,dma_timeout_n,overrun_n,ring_high,max_transfer_cycles,
    stop_abort_n,spi_not_idle_n,read_n};
  memcpy(out,d,sizeof(d));__set_PRIMASK(p);
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
uint32_t lsm6dsv_8k_available(void){return write_n-read_n;}
#endif /* APP_IMU8K_CAPTURE_TEST */
