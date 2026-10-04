#include "at32f423.h"
#include "at32f423_misc.h"
#include "boot_config.h"
#include "boot_request.h"
#include "bl_io.h"
#include "bl_protocol.h"
#include "boot_startup.h"
#include "fusion_settings.h"
#include "lsm6dsv.h"
#include "gyro_range.h"
#include "acc_calibration.h"
#include "ws2812.h"
#include <string.h>
static boot_cycle_clock_t boot_clock;

/* Use the core cycle counter as a monotonic startup timer.  This avoids
 * counting loop iterations or assuming that one protocol poll is 1 ms. */
static void boot_timer_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  memset(&boot_clock,0,sizeof(boot_clock));
}

static uint32_t boot_time_ms(void)
{
  uint32_t cycles_per_ms = system_core_clock / 1000U;
  if(cycles_per_ms == 0U) cycles_per_ms = 1U;
  return (uint32_t)(boot_cycle_clock_update(&boot_clock,DWT->CYCCNT) / cycles_per_ms);
}
uint32_t millis(void) { return boot_time_ms(); }

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
  volatile boot_startup_handoff_t *mailbox=(volatile boot_startup_handoff_t *)BOOT_STARTUP_ADDR;
  mailbox->magic=0U;
  bl_io_init();
  boot_timer_init();
  /* Initialise the shared PA8 RGB driver before IMU setup and capture. */
  ws2812_init();
  bl_protocol_reset();

  const int force = boot_requested();
  device_settings_t settings;
  (void)device_settings_load(&settings);
  int capture=!force && bl_protocol_can_boot() && settings.gyro_init_ms!=0U;
  if(capture) {
    acc_calibration_load();
    lsm6dsv_spi_init();
    if(lsm6dsv_init_2khz(settings.gyro_range_dps)!=0) capture=0;
  }
  boot_startup_t startup;
  boot_startup_begin(&startup,boot_time_ms(),settings.gyro_init_ms,settings.gyro_range_dps);
  for(;;)
  {
    uint8_t b;
    bl_io_port_t source;
    bl_io_task();
    unsigned budget=64U;
    while(budget-- && bl_io_read(&source, &b)) {
      bl_protocol_feed_from(source, b);
      /* Never hand a pre-upload calibration to a newly uploaded application. */
      if(!bl_protocol_can_boot()) capture=0;
    }
    uint32_t now=boot_time_ms();
    if(bl_protocol_boot_requested() ||
       (!force && (uint32_t)(now-startup.start_ms)>=settings.gyro_init_ms && bl_protocol_can_boot())) {
      boot_startup_handoff_t result;
      boot_startup_finish(&startup,now,capture,&result);
      boot_startup_publish(mailbox,&result);
      __DMB();
      jump_app();
    }
    /* A WS2812 waveform occupies about 30 us. Pause animation while a
     * command is partial or an upload owns the transport, preserving the
     * existing 2-Mbaud UART upgrade path without introducing RX pauses. */
    if(bl_protocol_idle()) ws2812_calibration_task(now);
    if(capture && (uint32_t)(now-startup.start_ms)<settings.gyro_init_ms && lsm6dsv_data_ready()) {
      lsm6dsv_raw_t raw;
      if(lsm6dsv_read_raw(&raw)==0) {
        float gyro[3],acc[3];
        for(unsigned i=0;i<3;++i) {
          gyro[i]=(float)raw.gyr[i]*gyro_range_dps_per_lsb(settings.gyro_range_dps)*0.017453292519943295f;
          acc[i]=acc_calibrate_g(i,(float)raw.acc[i]*.000122f)*9.80665f;
        }
        boot_startup_push(&startup,boot_time_ms(),gyro,acc,25.0f+(float)raw.temp_raw/256.0f);
      }
    }
  }
}
