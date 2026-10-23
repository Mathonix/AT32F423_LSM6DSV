"""Compile the actual DMA driver against fake registers; no board access."""
from pathlib import Path
import subprocess, shutil
root=Path(__file__).resolve().parents[1]
out=root/'build/spi-dma-native';out.mkdir(parents=True,exist_ok=True)
stub=r'''
#ifndef FAKE_H
#define FAKE_H
#include <stdint.h>
#include <string.h>
#define APP_IMU8K_ENGINE 1
#define APP_IMU8K_CAPTURE_TEST 0
#define APP_SRAM_CAPTURE 0
#define TRUE 1
#define FALSE 0
#define RESET 0
typedef struct {int16_t temp_raw,gyr[3],acc[3];}lsm6dsv_raw_t;
typedef struct {volatile uint32_t sts,dt,scr,clr;} reg_t;
static reg_t spi,gpio;
#define SPI1 (&spi)
#define GPIOA (&gpio)
static uint32_t exint[8],flags,clock_now,primask,system_core_clock=150000000;
#define EXINT_BASE ((uintptr_t)exint)
#define REG32(a) (*(volatile uint32_t*)(uintptr_t)(a))
#define GPIO_PINS_4 16
#define SPI_I2S_BF_FLAG 1
#define SPI_I2S_RDBF_FLAG 2
#define SPI_I2S_ROERR_FLAG 4
#define DMA1_CHANNEL2 2
#define DMA1_CHANNEL3 3
#define DMA1 1
#define DMA1MUX_CHANNEL2 2
#define DMA1MUX_CHANNEL3 3
#define DMA1_GL2_FLAG 0xf0
#define DMA1_GL3_FLAG 0xf00
#define DMA1_FDT2_FLAG 0x20
#define DMA1_FDT3_FLAG 0x200
#define DMA1_DTERR2_FLAG 0x80
#define DMA1_DTERR3_FLAG 0x800
#define DMA_FDT_INT 2
#define DMA_DTERR_INT 8
#define DMA_DIR_PERIPHERAL_TO_MEMORY 0
#define DMA_DIR_MEMORY_TO_PERIPHERAL 1
#define DMA_PERIPHERAL_DATA_WIDTH_HALFWORD 1
#define DMA_MEMORY_DATA_WIDTH_HALFWORD 1
#define DMA_PRIORITY_VERY_HIGH 3
#define DMAMUX_DMAREQ_ID_SPI1_RX 10
#define DMAMUX_DMAREQ_ID_SPI1_TX 11
#define CRM_DMA1_PERIPH_CLOCK 1
#define CRM_SCFG_PERIPH_CLOCK 2
#define DMA1_Channel2_IRQn 12
#define DMA1_Channel3_IRQn 13
#define EXINT0_IRQn 6
#define SCFG_PORT_SOURCE_GPIOB 1
#define SCFG_PINS_SOURCE0 0
#define PERF_IMU_IRQ 4
#define PERF_IMU_DMA 15
typedef struct {uint32_t peripheral_base_addr,memory_base_addr,direction,buffer_size,
peripheral_inc_enable,memory_inc_enable,peripheral_data_width,memory_data_width,
loop_mode_enable,priority;}dma_init_type;
static uint32_t channel_on[4],rx_request,tx_request;
static uint32_t __get_PRIMASK(void){return primask;}
static void __disable_irq(void){primask=1;}
static void __set_PRIMASK(uint32_t p){primask=p;}
static void __DMB(void){}
static uint32_t dwt_cycles(void){return ++clock_now;}
static void dma_flag_clear(uint32_t f){flags&=~f;}
static int dma_flag_get(uint32_t f){return !!(flags&f);}
static void dma_channel_enable(int ch,int on){channel_on[ch]=on;}
static void spi_i2s_dma_transmitter_enable(reg_t*s,int on){(void)s;tx_request=on;}
static void spi_i2s_dma_receiver_enable(reg_t*s,int on){(void)s;rx_request=on;}
static void spi_enable(reg_t*s,int on){if(!on)s->sts=0;}
static void dma_reset(int c){channel_on[c]=0;}
static void dma_default_para_init(dma_init_type*d){memset(d,0,sizeof(*d));}
static void dma_init(int c,dma_init_type*d){(void)c;(void)d;}
static void dma_data_number_set(int c,int n){(void)c;(void)n;}
static void dma_interrupt_enable(int c,int mask,int on){(void)c;(void)mask;(void)on;}
static void dmamux_enable(int c,int on){(void)c;(void)on;}
static void dmamux_init(int c,int id){(void)c;(void)id;}
static void crm_periph_clock_enable(int c,int on){(void)c;(void)on;}
static void nvic_irq_enable(int n,int p,int s){(void)n;(void)p;(void)s;}
static void nvic_irq_disable(int n){(void)n;}
static void NVIC_ClearPendingIRQ(int n){(void)n;}
static void scfg_exint_line_config(int n,int m){(void)n;(void)m;}
static uint32_t perf_irq_enter(void){return dwt_cycles();}
static void perf_irq_exit(int b,uint32_t t){(void)b;(void)t;}
static int lsm6dsv_read_raw(lsm6dsv_raw_t*r){memset(r,0,sizeof(*r));return 0;}
static int lsm6dsv_write_reg(int r,int v){(void)r;(void)v;return 0;}
#endif
'''
for name in ['lsm6dsv_8k.h','bsp.h','at32f423_scfg.h','perf_meter.h']:
    (out/name).write_text('#include "fake.h"\n')
(out/'fake.h').write_text(stub)
driver=(root/'src/drivers/lsm6dsv_8k.c').as_posix()
harness=r'''
#include <assert.h>
#include <stdio.h>
#include "DRIVER"
static void edge(void){EXINT_PENDING=1;EXINT0_IRQHandler();}
static void finish(void){flags|=DMA1_FDT2_FLAG|DMA1_FDT3_FLAG;DMA1_Channel2_IRQHandler();}
static void fill(void){
  /* Wire bytes: ignored command reply, 14 payload bytes, ignored tail. */
  const uint8_t payload[14]={0x00,0x80,1,0,0xff,0x7f,0xff,0xff,0x34,0x12,0xcd,0xab,0,0};
  dma_rx[0]=payload[0];
  for(unsigned i=1;i<14;i+=2)dma_rx[(i+1)/2]=(uint16_t)payload[i]<<8 | (i+1<14?payload[i+1]:0x55);
}
int main(void){
  lsm6dsv_raw_t r;uint32_t t,d[16];
  lsm6dsv_8k_capture_start();assert(active&&!dma_busy);
  edge();const uint32_t stamp=dma_timestamp;
  assert(dma_busy&&dma_started==1&&!write_n&&channel_on[2]&&channel_on[3]&&rx_request&&tx_request);
  DMA1_Channel2_IRQHandler();assert(dma_busy&&!write_n); /* no RX TC */
  edge();assert(dma_busy_n==1&&dma_timestamp==stamp&&dma_started==1);
  fill();finish();assert(!dma_busy&&dma_completed==1&&write_n==1);
  assert(lsm6dsv_8k_pop(&r,&t)&&t==stamp);
  assert(r.temp_raw==-32768&&r.gyr[0]==1&&r.gyr[1]==32767&&r.gyr[2]==-1);
  assert(r.acc[0]==0x1234&&r.acc[1]==(int16_t)0xabcd&&r.acc[2]==0);
  /* Late TX-error IRQ must not publish old data again. */
  DMA1_Channel3_IRQHandler();assert(write_n==1);
  edge();flags|=DMA1_DTERR3_FLAG;DMA1_Channel3_IRQHandler();
  assert(!dma_busy&&dma_error_n==1&&spi_error_n==1&&write_n==1);
  edge();clock_now+=15001;edge();assert(dma_timeout_n==1&&!dma_busy&&write_n==1);
  edge();fill();spi.sts=SPI_I2S_BF_FLAG;finish();
  assert(dma_timeout_n==2&&!dma_busy&&write_n==1);
  for(unsigned i=0;i<RING_LEN+1;i++){edge();fill();finish();}
  assert(write_n-read_n==RING_LEN&&overrun_n==1&&ring_high==RING_LEN);
  while(lsm6dsv_8k_pop(&r,&t)){}
  /* Counter/index wrap is safe with the bounded SPSC distance. */
  read_n=write_n=0xffffffffU;edge();fill();finish();
  assert(write_n==0&&lsm6dsv_8k_pop(&r,&t)&&read_n==0);
  edge();lsm6dsv_8k_capture_stop();assert(!active&&!dma_busy&&stop_abort_n==1);
  finish();assert(write_n==0);
  lsm6dsv_8k_capture_start();lsm6dsv_8k_dma_snapshot(d);
  assert(d[0]==1&&d[1]==1&&d[3]==0&&d[4]==0&&d[15]==0&&primask==0);
  primask=1;lsm6dsv_8k_dma_snapshot(d);assert(primask==1);primask=0;
  spi.sts=SPI_I2S_RDBF_FLAG;edge();assert(spi_not_idle_n==1&&!dma_busy&&!write_n);
  puts("PASS actual driver: decode, source time, busy, DMA error, timeout, overflow, wrap, stop/restart, PRIMASK");
}
'''.replace('DRIVER',driver)
(out/'test.c').write_text(harness)
cc=str(Path(shutil.which('mingw32-make')).with_name('gcc.exe'))
cmd=[cc,'-std=gnu11','-O2','-I'+str(out),str(out/'test.c'),'-o',str(out/'test.exe')]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
(out/'build.log').write_text(r.stdout+r.stderr)
if r.returncode:raise RuntimeError(r.stderr)
subprocess.run([str(out/'test.exe')],check=True,timeout=10)
