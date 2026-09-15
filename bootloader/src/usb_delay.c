/* Bootloader-local USB delay implementation. */
#include "at32f423.h"
#include "at32f423_misc.h"
#include "at32f423_conf.h"
#include <stdint.h>

void usb_delay_ms(uint32_t ms)
{
  uint32_t ticks = system_core_clock / 1000U;
  if(ticks == 0U) ticks = 1U;
  SysTick->LOAD = ticks - 1U;
  SysTick->VAL = 0U;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
  while(ms--)
    while((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0U) {}
  SysTick->CTRL = 0U;
}

void usb_delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = (system_core_clock / 1000000U) * us;
  if(ticks == 0U) ticks = 1U;
  while((uint32_t)(DWT->CYCCNT - start) < ticks) {}
}
