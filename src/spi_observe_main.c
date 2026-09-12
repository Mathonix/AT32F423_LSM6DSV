#include "at32f423_clock.h"
#include "bsp.h"

#define MAGIC 0x424F5053U /* SPOB */
#define PIN_HIGH(pin) (GPIOA->scr = (uint32_t)(1U << (pin)))
#define PIN_LOW(pin)  (GPIOA->scr = (uint32_t)(1U << ((pin) + 16U)))
#define CS_HIGH() PIN_HIGH(4U)
#define CS_LOW()  PIN_LOW(4U)
#define SCK_HIGH() PIN_HIGH(5U)
#define SCK_LOW()  PIN_LOW(5U)
#define MOSI_HIGH() PIN_HIGH(7U)
#define MOSI_LOW()  PIN_LOW(7U)

typedef struct {
  uint32_t magic, state, apb2_hz, ref_before, ref_after_m3, ref_after_m0;
  uint32_t m3_ctrl1, m3_ctrl2, m3_addr_edges, m3_addr_flags, m3_addr_rx;
  uint32_t m3_data_edges, m3_data_flags, m3_data_rx;
  uint32_t m0_ctrl1, m0_ctrl2, m0_addr_edges, m0_addr_flags, m0_addr_rx;
  uint32_t m0_data_edges, m0_data_flags, m0_data_rx;
} observe_log_t;
volatile observe_log_t observe_log;

static void bb_pause(void) { delay_us(250U); }
static void gpio_outputs(void) {
  gpio_init_type g;
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  gpio_default_para_init(&g);
  g.gpio_out_type=GPIO_OUTPUT_PUSH_PULL; g.gpio_pull=GPIO_PULL_NONE;
  g.gpio_mode=GPIO_MODE_OUTPUT; g.gpio_drive_strength=GPIO_DRIVE_STRENGTH_STRONGER;
  g.gpio_pins=GPIO_PINS_4|GPIO_PINS_5|GPIO_PINS_7; gpio_init(GPIOA,&g);
  g.gpio_mode=GPIO_MODE_INPUT; g.gpio_pull=GPIO_PULL_UP; g.gpio_pins=GPIO_PINS_6; gpio_init(GPIOA,&g);
}
static void bb_prepare(void) {
  spi_enable(SPI1,FALSE); spi_i2s_reset(SPI1); gpio_outputs();
  GPIOA->muxl &= ~0xFFFF0000U; CS_HIGH(); SCK_HIGH(); MOSI_LOW(); delay_ms(20U);
}
static uint8_t bb_xfer(uint8_t tx) {
  uint8_t rx=0,i; for(i=0;i<8;i++) { if(tx&0x80) MOSI_HIGH(); else MOSI_LOW(); tx<<=1;
    bb_pause(); SCK_LOW(); bb_pause(); SCK_HIGH(); bb_pause(); rx=(uint8_t)((rx<<1)|((GPIOA->idt>>6)&1U)); }
  return rx;
}
static uint8_t bb_read(void) {
  uint8_t r; CS_LOW(); bb_pause(); (void)bb_xfer(0x8FU); r=bb_xfer(0); bb_pause(); CS_HIGH(); bb_pause(); return r;
}
static uint32_t ref_count(void) {
  uint32_t n=0,i; bb_prepare(); (void)bb_read(); for(i=0;i<10;i++) if(bb_read()==0x70U)n++; return n;
}
static void hw_prepare(uint8_t mode3) {
  gpio_init_type g; spi_init_type s;
  spi_enable(SPI1,FALSE); spi_i2s_reset(SPI1); gpio_outputs();
  if(mode3) SCK_HIGH(); else SCK_LOW(); CS_HIGH(); MOSI_LOW();
  gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE5,GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE6,GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE7,GPIO_MUX_5);
  gpio_default_para_init(&g); g.gpio_out_type=GPIO_OUTPUT_PUSH_PULL; g.gpio_pull=GPIO_PULL_NONE;
  g.gpio_mode=GPIO_MODE_MUX; g.gpio_drive_strength=GPIO_DRIVE_STRENGTH_STRONGER;
  g.gpio_pins=GPIO_PINS_5|GPIO_PINS_7; gpio_init(GPIOA,&g);
  g.gpio_mode=GPIO_MODE_MUX; g.gpio_pull=GPIO_PULL_UP; g.gpio_pins=GPIO_PINS_6; gpio_init(GPIOA,&g);
  spi_default_para_init(&s); s.transmission_mode=SPI_TRANSMIT_FULL_DUPLEX; s.master_slave_mode=SPI_MODE_MASTER;
  s.mclk_freq_division=SPI_MCLK_DIV_1024; s.first_bit_transmission=SPI_FIRST_BIT_MSB; s.frame_bit_num=SPI_FRAME_8BIT;
  s.clock_polarity=mode3?SPI_CLOCK_POLARITY_HIGH:SPI_CLOCK_POLARITY_LOW;
  s.clock_phase=mode3?SPI_CLOCK_PHASE_2EDGE:SPI_CLOCK_PHASE_1EDGE;
  s.cs_mode_selection=SPI_CS_SOFTWARE_MODE; spi_init(SPI1,&s); spi_ti_mode_enable(SPI1,FALSE);
  spi_hardware_cs_output_enable(SPI1,FALSE); spi_enable(SPI1,TRUE); delay_us(20U);
}
static uint8_t level_flags(uint32_t idt) {
  uint8_t f=0; if(idt&GPIO_PINS_5)f|=2;else f|=1; if(idt&GPIO_PINS_7)f|=8;else f|=4;
  if(idt&GPIO_PINS_6)f|=32;else f|=16; if(idt&GPIO_PINS_4)f|=128;else f|=64; return f;
}
static uint8_t observe_xfer(uint8_t tx,uint32_t *edges,uint32_t *samples) {
  uint32_t guard=2000000U, last=GPIOA->idt, now, rise=0U, fall=0U;
  uint32_t nr=0U, nf=0U, mr=0U, mf=0U; *edges=0U; (void)level_flags(last);
  while((SPI1->sts&SPI_I2S_TDBE_FLAG)==0U && guard--){}
  *(volatile uint8_t*)&SPI1->dt=tx;
  guard=2000000U;
  while((SPI1->sts&SPI_I2S_RDBF_FLAG)==0U && guard--) {
    now=GPIOA->idt;
    if(((now^last)&GPIO_PINS_5)!=0U) {
      uint32_t bit=((now&GPIO_PINS_6)!=0U)?1U:0U; (*edges)++;
      if((now&GPIO_PINS_5)!=0U){rise=(rise<<1)|bit;mr=(mr<<1)|(((now&GPIO_PINS_7)!=0U)?1U:0U);nr++;}else{fall=(fall<<1)|bit;mf=(mf<<1)|(((now&GPIO_PINS_7)!=0U)?1U:0U);nf++;}
    }
    last=now;
  }
  now=GPIOA->idt;
  if(((now^last)&GPIO_PINS_5)!=0U) {
    uint32_t bit=((now&GPIO_PINS_6)!=0U)?1U:0U; (*edges)++;
    if((now&GPIO_PINS_5)!=0U){rise=(rise<<1)|bit;mr=(mr<<1)|(((now&GPIO_PINS_7)!=0U)?1U:0U);nr++;}else{fall=(fall<<1)|bit;mf=(mf<<1)|(((now&GPIO_PINS_7)!=0U)?1U:0U);nf++;}
  }
  *samples=((rise&0xFFU)<<24)|((fall&0xFFU)<<16)|((mr&0xFFU)<<8)|(mf&0xFFU);
  return *(volatile uint8_t*)&SPI1->dt;
}
static void observe_mode(uint8_t mode3, volatile uint32_t *base) {
  uint32_t e0,f0,e1,f1; uint8_t r0,r1; hw_prepare(mode3); base[0]=SPI1->ctrl1; base[1]=SPI1->ctrl2;
  CS_LOW(); delay_us(10U); r0=observe_xfer(0x8F,&e0,&f0); r1=observe_xfer(0,&e1,&f1);
  while((SPI1->sts&SPI_I2S_BF_FLAG)!=0U){} delay_us(10U); CS_HIGH();
  base[2]=e0; base[3]=f0; base[4]=r0; base[5]=e1; base[6]=f1; base[7]=r1;
}
int main(void) {
  system_clock_config();
  /* Diagnostic: keep APB2 /16 but switch SYSCLK back to 8 MHz HICK.
     SPI /1024 is then about 488 Hz (1.024 ms half-period). */
  crm_sysclk_switch(CRM_SCLK_HICK);
  while(crm_sysclk_switch_status_get() != CRM_SCLK_HICK) { }
  system_core_clock_update();
  bsp_init(); crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK,TRUE);
  observe_log.magic=MAGIC; observe_log.state=1; observe_log.apb2_hz=system_core_clock/16U;
  observe_log.ref_before=ref_count(); if(observe_log.ref_before!=10U){observe_log.state=3;goto done;}
  observe_mode(1,(volatile uint32_t*)&observe_log.m3_ctrl1); observe_log.ref_after_m3=ref_count();
  if(observe_log.ref_after_m3!=10U){observe_log.state=4;goto done;}
  observe_mode(0,(volatile uint32_t*)&observe_log.m0_ctrl1); observe_log.ref_after_m0=ref_count();
  observe_log.state=(observe_log.ref_after_m0==10U)?2U:5U;
done: bb_prepare(); while(1){led_toggle();delay_ms(250U);}
}
