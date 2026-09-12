/**
 * Safe parked image for board power-cycling and DAP GPIO diagnostics.
 * It leaves LSM6DSV deselected: CS=1, SCK=1 (Mode 3 idle), MOSI=0.
 */
#include "at32f423_clock.h"
#include "bsp.h"

int main(void)
{
  gpio_init_type gpio;

  system_clock_config();
  bsp_init();
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);
  spi_enable(SPI1, FALSE);
  spi_i2s_reset(SPI1);

  gpio_default_para_init(&gpio);
  gpio.gpio_pins = GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_7;
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(GPIOA, &gpio);

  gpio.gpio_pins = GPIO_PINS_6;
  gpio.gpio_mode = GPIO_MODE_INPUT;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio);

  GPIOA->muxl &= ~0xFFFF0000U;
  GPIOA->scr = (1U << 4) | (1U << 5) | (1U << (7 + 16));

  while(1)
  {
    led_toggle();
    delay_ms(1000U);
  }
}
