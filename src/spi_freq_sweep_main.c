/**
 * Non-destructive LSM6DSV SPI frequency sweep at 8 MHz SYSCLK.
 *
 * Only legal WHO_AM_I reads are generated:
 *   Selectable SPI Mode 0/3, GPIO software CS, 8-bit DT access
 *   8-bit frame with 8-bit DT access
 *   8-bit frame with 16-bit DT access
 *   16-bit frame with the legal 0x8F00 command
 *   PCLK /1024 through /8, CS setup/hold 100/10/2 us
 *
 * A known-good 250 us GPIO bit-bang check runs after every result row.
 * The probe stops immediately if that reference is not 10/10.
 */

#include "at32f423_clock.h"
#include "bsp.h"
#include "spi_matrix.h"

#define WHO_REG 0x0FU
#define WHO_OK  0x70U

#ifndef SPI_SWEEP_MODE
#define SPI_SWEEP_MODE 3U
#endif

#if (SPI_SWEEP_MODE != 0U) && (SPI_SWEEP_MODE != 3U)
#error "SPI_SWEEP_MODE must be 0 or 3"
#endif

#define PIN_HIGH(pin) (GPIOA->scr = (uint32_t)(1U << (pin)))
#define PIN_LOW(pin)  (GPIOA->scr = (uint32_t)(1U << ((pin) + 16U)))
#define CS_HIGH()     PIN_HIGH(4U)
#define CS_LOW()      PIN_LOW(4U)
#define SCK_HIGH()    PIN_HIGH(5U)
#define SCK_LOW()     PIN_LOW(5U)
#define MOSI_HIGH()   PIN_HIGH(7U)
#define MOSI_LOW()    PIN_LOW(7U)
#define MISO_READ() ((GPIOA->idt & GPIO_PINS_6) != 0U)

#define CFG_MODE_MASK       0x00000003U
#define CFG_FRAME16         0x00000004U
#define CFG_VARIANT         0x00000008U
#define CFG_HWCS            0x00000010U
#define CFG_MODERATE        0x00000020U
#define CFG_DIV_SHIFT       8U
#define CFG_TIMING_SHIFT    12U
#ifndef SPI_SWEEP_USE_PLL
#define SPI_SWEEP_USE_PLL 0U
#endif

#if SPI_SWEEP_USE_PLL
/* At 80 MHz APB2, /8 is a nominal exact 10 MHz and is the sensor limit. */
#define SAFE_RESULT_ROWS      8U
#else
#define SAFE_RESULT_ROWS     10U
#endif

static const spi_mclk_freq_div_type divs[10] = {
  SPI_MCLK_DIV_1024, SPI_MCLK_DIV_512, SPI_MCLK_DIV_256, SPI_MCLK_DIV_128,
  SPI_MCLK_DIV_64, SPI_MCLK_DIV_32, SPI_MCLK_DIV_16, SPI_MCLK_DIV_8,
  SPI_MCLK_DIV_4, SPI_MCLK_DIV_2
};
static const uint16_t timing_us[3] = {100U, 10U, 2U};

volatile spi_matrix_log_t spi_matrix_log;

static void bb_pause(void)
{
  /* Match the known-good very slow DAP bit-bang reference (~10 kHz). */
  delay_us(250U);
}

static void gpio_common(gpio_drive_type drive)
{
  gpio_init_type gpio;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  gpio_default_para_init(&gpio);
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_drive_strength = drive;
  gpio.gpio_pins = GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_7;
  gpio_init(GPIOA, &gpio);

  gpio.gpio_mode = GPIO_MODE_INPUT;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = GPIO_PINS_6;
  gpio_init(GPIOA, &gpio);
}

static void bb_prepare(void)
{
  spi_enable(SPI1, FALSE);
  spi_i2s_reset(SPI1);
  gpio_common(GPIO_DRIVE_STRENGTH_STRONGER);
  GPIOA->muxl &= ~0xFFFF0000U;
  CS_HIGH();
  SCK_HIGH();
  MOSI_LOW();
  delay_ms(20U);
}

static uint8_t bb_xfer_mode3(uint8_t tx)
{
  uint8_t rx = 0U;
  uint8_t i;

  for(i = 0U; i < 8U; i++)
  {
    if((tx & 0x80U) != 0U) MOSI_HIGH(); else MOSI_LOW();
    tx <<= 1;
    bb_pause();
    SCK_LOW();
    bb_pause();
    SCK_HIGH();
    bb_pause();
    rx = (uint8_t)((rx << 1) | (MISO_READ() ? 1U : 0U));
  }
  return rx;
}

static uint8_t bb_read(uint8_t reg)
{
  uint8_t rx;
  CS_LOW();
  bb_pause();
  (void)bb_xfer_mode3((uint8_t)(reg | 0x80U));
  rx = bb_xfer_mode3(0U);
  bb_pause();
  CS_HIGH();
  bb_pause();
  return rx;
}

static void bb_write(uint8_t reg, uint8_t val)
{
  CS_LOW();
  bb_pause();
  (void)bb_xfer_mode3((uint8_t)(reg & 0x7FU));
  (void)bb_xfer_mode3(val);
  bb_pause();
  CS_HIGH();
  bb_pause();
}

static uint16_t reference_read_count(void)
{
  uint16_t ok = 0U;
  uint16_t i;
  uint8_t value;

  bb_prepare();
  /* The first transaction after GPIO/SPI ownership changes is discarded. */
  (void)bb_read(WHO_REG);
  for(i = 0U; i < SPI_MATRIX_REF_READS; i++)
  {
    value = bb_read(WHO_REG);
    if(i < 4U)
    {
      /* Preserve raw reference bytes for DAP diagnostics on early failure. */
      spi_matrix_log.reserved[i] = value;
    }
    if(value == WHO_OK) ok++;
  }
  return ok;
}

static uint16_t reference_restore_and_check(void)
{
  uint16_t ok = reference_read_count();

  /* Never send recovery writes unless reads prove the current bus is valid. */
  if(ok != SPI_MATRIX_REF_READS) return ok;

  bb_write(0x01U, 0x00U); /* FUNC_CFG_ACCESS: main bank */
  bb_write(0x03U, 0x00U); /* IF_CFG: four-wire SPI allowed */
  bb_write(0x12U, 0x01U); /* SW_RESET */
  delay_ms(50U);
  bb_write(0x01U, 0x00U);
  bb_write(0x03U, 0x00U);
  return reference_read_count();
}

static void spi_flush(void)
{
  volatile uint32_t dummy;
  uint32_t guard = 64U;
  while(((SPI1->sts & SPI_I2S_RDBF_FLAG) != 0U) && (guard-- != 0U))
  {
    dummy = SPI1->dt;
  }
  if((SPI1->sts & SPI_I2S_ROERR_FLAG) != 0U)
  {
    dummy = SPI1->dt;
    dummy = SPI1->sts;
  }
  (void)dummy;
}

static uint16_t spi_xfer(uint16_t tx, uint8_t byte_access, uint16_t *timeouts,
                         uint32_t *sts_or)
{
  uint32_t guard = 100000U;
  while(((SPI1->sts & SPI_I2S_TDBE_FLAG) == 0U) && (guard-- != 0U)) { }
  if(guard == 0U) (*timeouts)++;

  if(byte_access != 0U)
  {
    *(volatile uint8_t *)&SPI1->dt = (uint8_t)tx;
  }
  else
  {
    SPI1->dt = tx;
  }

  guard = 100000U;
  while(((SPI1->sts & SPI_I2S_RDBF_FLAG) == 0U) && (guard-- != 0U)) { }
  if(guard == 0U) (*timeouts)++;
  *sts_or |= SPI1->sts;

  if(byte_access != 0U)
  {
    return *(volatile uint8_t *)&SPI1->dt;
  }
  return (uint16_t)SPI1->dt;
}

static void spi_wait_idle(uint16_t *timeouts, uint32_t *sts_or)
{
  uint32_t guard = 100000U;
  while((((SPI1->sts & SPI_I2S_TDBE_FLAG) == 0U) ||
         ((SPI1->sts & SPI_I2S_BF_FLAG) != 0U)) && (guard-- != 0U)) { }
  if(guard == 0U) (*timeouts)++;
  *sts_or |= SPI1->sts;
}

static void hardware_prepare(uint32_t config)
{
  gpio_init_type gpio;
  spi_init_type spi;
  uint8_t mode = (uint8_t)(config & CFG_MODE_MASK);
  uint8_t hwcs = ((config & CFG_HWCS) != 0U) ? 1U : 0U;
  gpio_drive_type drive = ((config & CFG_MODERATE) != 0U) ?
                           GPIO_DRIVE_STRENGTH_MODERATE :
                           GPIO_DRIVE_STRENGTH_STRONGER;
  uint8_t div_i = (uint8_t)((config >> CFG_DIV_SHIFT) & 0x0FU);

  spi_enable(SPI1, FALSE);
  spi_i2s_reset(SPI1);
  gpio_common(drive);

  if(mode == 3U) SCK_HIGH(); else SCK_LOW();
  CS_HIGH();
  MOSI_LOW();

  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE4, GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE5, GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE6, GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE7, GPIO_MUX_5);

  gpio_default_para_init(&gpio);
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_mode = GPIO_MODE_MUX;
  gpio.gpio_drive_strength = drive;
  gpio.gpio_pins = GPIO_PINS_5 | GPIO_PINS_7;
  if(hwcs != 0U) gpio.gpio_pins |= GPIO_PINS_4;
  gpio_init(GPIOA, &gpio);

  gpio.gpio_mode = GPIO_MODE_MUX;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = GPIO_PINS_6;
  gpio_init(GPIOA, &gpio);

  spi_default_para_init(&spi);
  spi.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi.master_slave_mode = SPI_MODE_MASTER;
  spi.mclk_freq_division = divs[div_i];
  spi.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi.frame_bit_num = ((config & CFG_FRAME16) != 0U) ? SPI_FRAME_16BIT : SPI_FRAME_8BIT;
  spi.clock_polarity = (mode == 3U) ? SPI_CLOCK_POLARITY_HIGH : SPI_CLOCK_POLARITY_LOW;
  spi.clock_phase = (mode == 3U) ? SPI_CLOCK_PHASE_2EDGE : SPI_CLOCK_PHASE_1EDGE;
  spi.cs_mode_selection = (hwcs != 0U) ? SPI_CS_HARDWARE_MODE : SPI_CS_SOFTWARE_MODE;
  spi_init(SPI1, &spi);
  spi_ti_mode_enable(SPI1, FALSE);
  spi_hardware_cs_output_enable(SPI1, (hwcs != 0U) ? TRUE : FALSE);
  spi_flush();
  if(hwcs == 0U) spi_enable(SPI1, TRUE);
  delay_us(5U);
}

static uint8_t hardware_read(uint32_t config, uint16_t setup_hold,
                             uint16_t *timeouts, uint32_t *sts_or,
                             uint32_t *rx_pair)
{
  uint8_t hwcs = ((config & CFG_HWCS) != 0U) ? 1U : 0U;
  uint8_t frame16 = ((config & CFG_FRAME16) != 0U) ? 1U : 0U;
  uint8_t variant = ((config & CFG_VARIANT) != 0U) ? 1U : 0U;
  uint16_t r0 = 0U;
  uint16_t r1 = 0U;
  uint8_t value;

  spi_flush();
  if(hwcs != 0U) spi_enable(SPI1, TRUE); else CS_LOW();
  delay_us(setup_hold);

  if(frame16 != 0U)
  {
    r0 = spi_xfer((variant == 0U) ? 0x8F00U : 0x008FU, 0U, timeouts, sts_or);
    value = (variant == 0U) ? (uint8_t)r0 : (uint8_t)(r0 >> 8);
  }
  else
  {
    uint8_t byte_access = (variant == 0U) ? 1U : 0U;
    r0 = spi_xfer(0x008FU, byte_access, timeouts, sts_or);
    r1 = spi_xfer(0x0000U, byte_access, timeouts, sts_or);
    value = (uint8_t)r1;
  }

  spi_wait_idle(timeouts, sts_or);
  delay_us(setup_hold);
  if(hwcs != 0U) spi_enable(SPI1, FALSE); else CS_HIGH();
  delay_us(2U);
  *rx_pair = ((uint32_t)r0 << 16) | r1;
  return value;
}

static void run_one(uint32_t config, uint16_t reference_ok)
{
  spi_matrix_result_t *r;
  uint16_t i;
  uint16_t ok = 0U, ff = 0U, zero = 0U, other = 0U, timeouts = 0U;
  uint8_t first_other = 0U;
  uint8_t value;
  uint32_t first_rx = 0U, rx_pair = 0U, sts_or = 0U;
  uint8_t timing_i = (uint8_t)((config >> CFG_TIMING_SHIFT) & 0x03U);

  if(spi_matrix_log.result_count >= SPI_MATRIX_RESULT_MAX)
  {
    spi_matrix_log.state = 3U;
    spi_matrix_log.fatal_code = 1U;
    return;
  }

  hardware_prepare(config);
  /* Discard ownership-change warm-up; production SPI remains continuously configured. */
  (void)hardware_read(config, timing_us[timing_i], &timeouts, &sts_or, &rx_pair);
  for(i = 0U; i < SPI_MATRIX_READS; i++)
  {
    value = hardware_read(config, timing_us[timing_i], &timeouts, &sts_or, &rx_pair);
    if(i == 0U) first_rx = rx_pair;
    if(value == WHO_OK) ok++;
    else if(value == 0xFFU) ff++;
    else if(value == 0x00U) zero++;
    else
    {
      if(other == 0U) first_other = value;
      other++;
    }
  }

  r = (spi_matrix_result_t *)&spi_matrix_log.result[spi_matrix_log.result_count];
  r->config = config;
  r->ref_total_ok = ((uint32_t)SPI_MATRIX_REF_READS << 16) | reference_ok;
  r->ok_ff = ((uint32_t)ok << 16) | ff;
  r->zero_other = ((uint32_t)zero << 16) | other;
  r->timeout_first = ((uint32_t)timeouts << 16) | first_other;
  r->first_rx = first_rx;
  r->ctrl1 = SPI1->ctrl1;
  r->ctrl2 = SPI1->ctrl2;
  r->sts_or = sts_or;
  r->gpio_cfgr = GPIOA->cfgr;
  r->gpio_muxl = GPIOA->muxl;
  __DMB();
  spi_matrix_log.result_count++;
}

int main(void)
{
  uint32_t div_i;
  uint16_t ref_ok;
  uint32_t config;

  system_clock_config();
#if !SPI_SWEEP_USE_PLL
  crm_sysclk_switch(CRM_SCLK_HICK);
  while(crm_sysclk_switch_status_get() != CRM_SCLK_HICK) { }
  system_core_clock_update();
#endif
  bsp_init();
  crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);

  spi_matrix_log.magic = SPI_MATRIX_MAGIC;
  spi_matrix_log.version = SPI_MATRIX_VERSION;
  spi_matrix_log.state = 1U;
  spi_matrix_log.result_count = 0U;
  spi_matrix_log.result_max = SAFE_RESULT_ROWS;
  spi_matrix_log.reads_per_result = SPI_MATRIX_READS;
  spi_matrix_log.ref_reads = SPI_MATRIX_REF_READS;
  spi_matrix_log.core_hz = system_core_clock;
  spi_matrix_log.apb2_hz = system_core_clock / SYSTEM_APB2_DIV_VALUE;
  spi_matrix_log.current_config = 0U;
  spi_matrix_log.reference_fail_groups = 0U;
  spi_matrix_log.fatal_code = 0U;

  ref_ok = reference_read_count();
  if(ref_ok != SPI_MATRIX_REF_READS)
  {
    spi_matrix_log.reference_fail_groups = 1U;
    spi_matrix_log.state = 3U;
    spi_matrix_log.fatal_code = 0x10U;
    goto done;
  }

  for(div_i = 0U; div_i < SAFE_RESULT_ROWS; div_i++)
  {
    /* Selected mode, 8-bit frame, byte access, GPIO CS, 10 us setup/hold. */
    config = SPI_SWEEP_MODE | (div_i << CFG_DIV_SHIFT) | (1U << CFG_TIMING_SHIFT);
    spi_matrix_log.current_config = config;
    run_one(config, 0U);
    ref_ok = reference_read_count();
    spi_matrix_log.result[spi_matrix_log.result_count - 1U].ref_total_ok =
      ((uint32_t)SPI_MATRIX_REF_READS << 16) | ref_ok;
    __DMB();
    if(ref_ok != SPI_MATRIX_REF_READS)
    {
      spi_matrix_log.reference_fail_groups++;
      spi_matrix_log.state = 3U;
      spi_matrix_log.fatal_code = 0x20U;
      goto done;
    }
  }

done:
  bb_prepare();
  spi_matrix_log.current_config = 0xFFFFFFFFU;
  __DMB();
  if(spi_matrix_log.state != 3U) spi_matrix_log.state = 2U;
  while(1) { led_toggle(); delay_ms(250U); }
}
