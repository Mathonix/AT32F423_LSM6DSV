/* IMU support for the shared boot/calibration window; no Flash writes here. */
#include "bsp.h"
#include "bl_io.h"
#include "at32f423_gpio.h"
uint32_t dwt_cycles(void) { return DWT->CYCCNT; }
uint8_t lsm_int1_read(void) { return gpio_input_data_bit_read(GPIOB,GPIO_PINS_0)!=RESET; }
void delay_us(uint32_t us)
{
  uint32_t start=dwt_cycles(), ticks=(system_core_clock/1000000U)*us;
  while((uint32_t)(dwt_cycles()-start)<ticks) { }
}
void delay_ms(uint32_t ms)
{
  uint32_t start=millis();
  while((uint32_t)(millis()-start)<ms) bl_io_task();
}
