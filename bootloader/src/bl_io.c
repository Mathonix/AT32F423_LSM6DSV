#include "at32f423.h"
#include "at32f423_conf.h"
#include "at32f423_crm.h"
#include "at32f423_gpio.h"
#include "at32f423_usart.h"
#include "at32f423_misc.h"
#include "at32f423_flash.h"
#include "at32f423_pwc.h"
#include "at32f423_acc.h"
#include "usb_conf.h"
#include "usb_core.h"
#include "usbd_int.h"
#include "cdc_class.h"
#include "cdc_desc.h"
#include "boot_config.h"
#include "bl_io.h"
#include <stdint.h>
static otg_core_type core;
static uint8_t usb_packet[64], usb_fifo[512], usb_tx[64];
static uint16_t usb_fifo_r, usb_fifo_w;
static void clock_init(void){crm_reset();crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK,TRUE);pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);flash_psr_set(FLASH_WAIT_CYCLE_4);crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK,TRUE);while(crm_flag_get(CRM_HICK_STABLE_FLAG)!=SET){};crm_ahb_div_set(CRM_AHB_DIV_1);crm_apb1_div_set(CRM_APB1_DIV_2);crm_apb2_div_set(CRM_APB2_DIV_1);crm_hick_divider_select(CRM_HICK48_DIV6);crm_pll_config(CRM_PLL_SOURCE_HICK,75,1,CRM_PLL_FR_2);crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL,TRUE);while(crm_flag_get(CRM_PLL_STABLE_FLAG)!=SET){};crm_sysclk_switch(CRM_SCLK_PLL);while(crm_sysclk_switch_status_get()!=CRM_SCLK_PLL){};system_core_clock_update();}
static void uart_init_local(void){gpio_init_type g;crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK,TRUE);crm_periph_clock_enable(CRM_USART4_PERIPH_CLOCK,TRUE);gpio_default_para_init(&g);g.gpio_pins=GPIO_PINS_0|GPIO_PINS_1;g.gpio_mode=GPIO_MODE_MUX;g.gpio_out_type=GPIO_OUTPUT_PUSH_PULL;g.gpio_pull=GPIO_PULL_NONE;g.gpio_drive_strength=GPIO_DRIVE_STRENGTH_STRONGER;gpio_init(GPIOA,&g);gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE0,GPIO_MUX_8);gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE1,GPIO_MUX_8);usart_init(USART4,BL_UART_BAUD,USART_DATA_8BITS,USART_STOP_1_BIT);usart_transmitter_enable(USART4,TRUE);usart_receiver_enable(USART4,TRUE);usart_enable(USART4,TRUE);}
static void usb_init_local(void){gpio_init_type g;crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK,TRUE);gpio_default_para_init(&g);g.gpio_pins=GPIO_PINS_11|GPIO_PINS_12;g.gpio_mode=GPIO_MODE_MUX;g.gpio_out_type=GPIO_OUTPUT_PUSH_PULL;g.gpio_pull=GPIO_PULL_NONE;g.gpio_drive_strength=GPIO_DRIVE_STRENGTH_STRONGER;gpio_init(GPIOA,&g);gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE11,GPIO_MUX_10);gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE12,GPIO_MUX_10);crm_periph_clock_enable(CRM_OTGFS1_PERIPH_CLOCK,TRUE);crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK,TRUE);acc_write_c1(7980);acc_write_c2(8000);acc_write_c3(8020);acc_calibration_mode_enable(ACC_CAL_HICKTRIM,TRUE);nvic_irq_enable(OTGFS1_IRQn,3,0);usbd_init(&core,USB_FULL_SPEED_CORE_ID,USB_ID,&cdc_class_handler,&cdc_desc_handler);}
void bl_io_init(void){clock_init();uart_init_local();usb_init_local();}
void bl_io_deinit(void)
{
  /* Release OTGFS1 before the application reinitializes the same USB core. */
  if(core.usb_reg != 0)
  {
    usb_interrupt_disable(core.usb_reg);
    usbd_disconnect(&core.dev);
  }
  usb_delay_ms(20U);
  crm_periph_reset(CRM_OTGFS1_PERIPH_RESET, TRUE);
  crm_periph_reset(CRM_OTGFS1_PERIPH_RESET, FALSE);
}
void OTGFS1_IRQHandler(void){usbd_irq_handler(&core);}
int bl_usb_ready(void){return core.dev.dev_config!=0;}
static void usb_poll_rx(void){uint16_t n,i; if(!bl_usb_ready())return; n=usb_vcp_get_rxdata(&core.dev,usb_packet);for(i=0;i<n;i++){uint16_t next=(uint16_t)((usb_fifo_w+1U)&511U);if(next!=usb_fifo_r){usb_fifo[usb_fifo_w]=usb_packet[i];usb_fifo_w=next;}}}
int bl_io_read(uint8_t *b){usb_poll_rx();if(usart_flag_get(USART4,USART_RDBF_FLAG)!=RESET){*b=(uint8_t)usart_data_receive(USART4);return 1;}if(usb_fifo_r!=usb_fifo_w){*b=usb_fifo[usb_fifo_r];usb_fifo_r=(uint16_t)((usb_fifo_r+1U)&511U);return 1;}return 0;}
int bl_io_write(const uint8_t *p,uint16_t n){uint16_t i,off=0;for(i=0;i<n;i++){while(usart_flag_get(USART4,USART_TDBE_FLAG)==RESET){}usart_data_transmit(USART4,p[i]);}while(off<n && bl_usb_ready()){uint16_t k=(uint16_t)((n-off)>64U?64U:(n-off));for(i=0;i<k;i++)usb_tx[i]=p[off+i];if(usb_vcp_send_data(&core.dev,usb_tx,k)!=SUCCESS)break;off=(uint16_t)(off+k);}return 0;}
void bl_io_task(void){usb_poll_rx();}

