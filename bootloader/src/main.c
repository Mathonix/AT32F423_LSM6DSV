#include "at32f423.h"
#include "at32f423_misc.h"
#include "boot_config.h"
#include "boot_request.h"
#include "bl_io.h"
#include "bl_protocol.h"

/* Use the core cycle counter as a monotonic startup timer.  This avoids
 * counting loop iterations or assuming that one protocol poll is 1 ms. */
static void boot_timer_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t boot_time_ms(void)
{
  uint32_t cycles_per_ms = system_core_clock / 1000U;
  if(cycles_per_ms == 0U) cycles_per_ms = 1U;
  return DWT->CYCCNT / cycles_per_ms;
}

static int boot_requested(void)
{
  volatile uint32_t *p = (volatile uint32_t *)APP_BOOT_REQUEST_ADDR;
  uint32_t v = *p;
  *p = 0U;
  __DMB();
  return v == APP_BOOT_REQUEST_MAGIC;
}

/* No C stack accesses are permitted after installing the application's MSP.
 * All peripheral interrupts have been disabled before restoring PRIMASK. */
__attribute__((naked, noreturn)) static void branch_app(uint32_t sp, uint32_t pc)
{
  __asm volatile("msr msp, r0\n"
                 "movs r2, #0\n"
                 "msr control, r2\n"
                 "isb\n"
                 "cpsie i\n"
                 "bx r1\n");
}

static void jump_app(void)
{
  const uint32_t sp = *(const uint32_t *)BL_APP_BASE;
  const uint32_t pc = *(const uint32_t *)(BL_APP_BASE + 4U);

  bl_io_deinit();
  __disable_irq();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  for(uint32_t i = 0U; i < 8U; ++i)
  {
    NVIC->ICER[i] = 0xFFFFFFFFU;
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }

  /* Application vector table is located at 0x08008000. */
  SCB->VTOR = BL_APP_BASE;
  __DSB();
  __ISB();
  branch_app(sp, pc);
}

int main(void)
{
  bl_io_init();
  boot_timer_init();
  bl_protocol_reset();

  const int force = boot_requested();
  const uint32_t deadline = boot_time_ms() + BL_BOOT_TIMEOUT_MS;
  for(;;)
  {
    uint8_t b;
    bl_io_port_t source;
    bl_io_task();
    while(bl_io_read(&source, &b))
      bl_protocol_feed_from(source, b);
    if(bl_protocol_boot_requested() ||
       (!force && (int32_t)(boot_time_ms() - deadline) >= 0 &&
        bl_protocol_can_boot()))
      jump_app();
  }
}
