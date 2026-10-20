#include "flash_gate.h"
#include "at32f423.h"

volatile uint32_t flash_gate_lo;
volatile uint32_t flash_gate_hi;

void flash_gate_enter(uint32_t kind)
{
  uint32_t lo = 0U;
  uint32_t hi = 0U;
  if(kind == FLASH_GATE_SETTINGS) {
    lo = 0x0803D000U;
    hi = 0x0803E000U;
  } else if(kind == FLASH_GATE_RESERVED) {
    lo = 0x0803C000U;
    hi = 0x08040000U;
  }
  flash_gate_hi = hi;
  flash_gate_lo = lo;
  __DMB();
}

void flash_gate_leave(void)
{
  flash_gate_lo = 0U;
  flash_gate_hi = 0U;
  __DMB();
}
