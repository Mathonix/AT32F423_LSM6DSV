#include "ist8310.h"
#include "at32f423_clock.h"
#include "bsp.h"

#define WIA 0x00U
#define STAT1 0x02U
#define DATAXL 0x03U
#define CNTL1 0x0AU
#define CNTL2 0x0BU
#define PDCNTL 0x42U
#define SDA_H() gpio_bits_set(GPIOB, GPIO_PINS_7)
#define SDA_L() gpio_bits_reset(GPIOB, GPIO_PINS_7)
#define SCL_H() gpio_bits_set(GPIOB, GPIO_PINS_6)
#define SCL_L() gpio_bits_reset(GPIOB, GPIO_PINS_6)
#define SDA_R() (gpio_input_data_bit_read(GPIOB, GPIO_PINS_7) != RESET)
#define SCL_R() (gpio_input_data_bit_read(GPIOB, GPIO_PINS_6) != RESET)
#define DRDY_R() (gpio_input_data_bit_read(GPIOB, GPIO_PINS_2) != RESET)
#define RST_H() gpio_bits_set(GPIOB, GPIO_PINS_3)
#define RST_L() gpio_bits_reset(GPIOB, GPIO_PINS_3)
static uint8_t g_addr;
static void dly(void){ delay_us(1U); }
static void pins_init(void){ gpio_init_type g; crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE); gpio_default_para_init(&g); g.gpio_mode=GPIO_MODE_OUTPUT; g.gpio_out_type=GPIO_OUTPUT_PUSH_PULL; g.gpio_pull=GPIO_PULL_NONE; g.gpio_drive_strength=GPIO_DRIVE_STRENGTH_MODERATE; g.gpio_pins=GPIO_PINS_3; RST_H(); gpio_init(GPIOB,&g); gpio_default_para_init(&g); g.gpio_mode=GPIO_MODE_OUTPUT; g.gpio_out_type=GPIO_OUTPUT_OPEN_DRAIN; g.gpio_pull=GPIO_PULL_UP; g.gpio_drive_strength=GPIO_DRIVE_STRENGTH_MODERATE; g.gpio_pins=GPIO_PINS_6|GPIO_PINS_7; gpio_init(GPIOB,&g); SCL_H(); SDA_H(); gpio_default_para_init(&g); g.gpio_mode=GPIO_MODE_INPUT; g.gpio_pull=GPIO_PULL_DOWN; g.gpio_pins=GPIO_PINS_2; gpio_init(GPIOB,&g); }
static void start(void){ SDA_H(); SCL_H(); dly(); SDA_L(); dly(); SCL_L(); dly(); }
static void stop(void){ SDA_L(); dly(); SCL_H(); dly(); SDA_H(); dly(); }
static uint8_t wb(uint8_t b){ uint8_t i,n; for(i=0;i<8;i++){ if(b&0x80U) SDA_H(); else SDA_L(); b<<=1; dly(); SCL_H(); dly(); SCL_L(); dly(); } SDA_H(); dly(); SCL_H(); dly(); n=SDA_R()?1U:0U; SCL_L(); dly(); return n; }
static uint8_t rb(uint8_t ack){ uint8_t i,b=0; SDA_H(); for(i=0;i<8;i++){ b<<=1; dly(); SCL_H(); dly(); if(SDA_R()) b|=1U; SCL_L(); dly(); } if(ack) SDA_L(); else SDA_H(); dly(); SCL_H(); dly(); SCL_L(); SDA_H(); dly(); return b; }
static int read_reg(uint8_t a,uint8_t r,uint8_t *p,uint8_t n){ uint8_t i; start(); if(wb(a<<1)||wb(r)){stop();return -1;} start(); if(wb((a<<1)|1U)){stop();return -1;} for(i=0;i<n;i++)p[i]=rb((i+1U)<n); stop(); return 0; }
static int write_reg(uint8_t a,uint8_t r,uint8_t v){ start(); if(wb(a<<1)||wb(r)||wb(v)){stop();return -1;} stop(); return 0; }
int ist8310_init(void){ uint8_t a,w,v; pins_init(); RST_L(); delay_ms(5); RST_H(); delay_ms(50); if(!SCL_R()||!SDA_R()) return -2; for(a=0x0C;a<=0x0F;a++){ if(read_reg(a,WIA,&w,1)==0 && w==0x10U) break; } if(a>0x0F) return -1; g_addr=a; if(write_reg(a,CNTL2,1U)!=0) return -3; delay_ms(50); if(write_reg(a,PDCNTL,0xC0U)!=0) return -3; if(read_reg(a,PDCNTL,&v,1)!=0 || v!=0xC0U) return -4; return 0; }
int ist8310_start_measurement(void){ if(!g_addr) return -1; return (write_reg(g_addr,CNTL1,1U)==0)?0:-2; }
int ist8310_data_ready(void){ return (g_addr && DRDY_R()) ? 1 : 0; }
int ist8310_read_ready(int16_t mag[3]){ uint8_t b[6]; if(!g_addr||!mag) return -1; if(read_reg(g_addr,DATAXL,b,6)!=0) return -3; mag[0]=(int16_t)((uint16_t)b[0]|((uint16_t)b[1]<<8)); mag[1]=(int16_t)((uint16_t)b[2]|((uint16_t)b[3]<<8)); mag[2]=(int16_t)((uint16_t)b[4]|((uint16_t)b[5]<<8)); return 0; }
int ist8310_read(int16_t mag[3]){ uint8_t s; uint32_t t; int e=ist8310_start_measurement(); if(e) return e; for(t=0;t<20;t++){ if(ist8310_data_ready()) break; if(read_reg(g_addr,STAT1,&s,1)!=0) return -3; if(s&1U) break; delay_ms(1); } if(t>=20) return -4; return ist8310_read_ready(mag); }
uint8_t ist8310_get_addr(void){return g_addr;}


