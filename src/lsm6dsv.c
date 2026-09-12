/**
 * LSM6DSV 4-wire SPI1, aligned with the STM32C5+LSM6DSVE polling article:
 * Mode 0, 8-bit frames, software CS, PA6 pull-up, ~5 MHz SCK.
 *
 * PA4 = GPIO CS, PA5 = SPI1_SCK AF5, PA6 = SPI1_MISO AF5, PA7 = SPI1_MOSI AF5.
 */

#include "lsm6dsv.h"
#include "bsp.h"
#include "at32f423_scfg.h"

#ifndef LSM6DSV_USE_HW_SPI
#define LSM6DSV_USE_HW_SPI 1
#endif

#define REG_FUNC_CFG_ACCESS  0x01U
#define REG_IF_CFG           0x03U
#define REG_INT1_CTRL        0x0DU
#define REG_WHO_AM_I         0x0FU
#define REG_CTRL1            0x10U
#define REG_CTRL2            0x11U
#define REG_CTRL3            0x12U
#define REG_CTRL4            0x13U
#define REG_CTRL6            0x15U
#define REG_CTRL7            0x16U
#define REG_CTRL8            0x17U
#define REG_STATUS           0x1EU
#define REG_OUTX_L_G         0x22U
#define REG_HAODR_CFG        0x62U

#define IF_CFG_I2C_I3C_DISABLE  0x01U
#define CTRL3_SW_RESET          0x01U
#define CTRL3_IF_INC            0x04U
#define CTRL3_BDU               0x40U
#define CTRL4_DRDY_MASK         0x08U
#define INT1_DRDY_XL            0x01U
#define INT1_DRDY_G             0x02U
#define FS_G_2000DPS            0x04U
#define LPF1_G_EN               0x01U
#define LPF1_G_BW_MEDIUM        0x03U
#define FS_XL_4G                0x01U
#define HA01_2000HZ             0x1AU
#define HAODR_SEL_HA01          0x01U
#define STATUS_XLDA             0x01U
#define STATUS_GDA              0x02U
#define STATUS_XGDA             (STATUS_XLDA | STATUS_GDA)

#define CS_HIGH() gpio_bits_set(GPIOA, GPIO_PINS_4)
#define CS_LOW()  gpio_bits_reset(GPIOA, GPIO_PINS_4)

#if LSM6DSV_USE_HW_SPI

#ifndef LSM6DSV_SPI_DIV
/* APB2 = 75 MHz, DIV_8 => SCK ≈ 9.4 MHz (LSM6DSV max 10 MHz). */
#define LSM6DSV_SPI_DIV SPI_MCLK_DIV_8
#endif

#define SPI_TIMEOUT 1000000U

static void cs_low(void)
{
  CS_LOW();
  __NOP();
  __NOP();
  __NOP();
  __NOP();
}

static void cs_high(void)
{
  CS_HIGH();
  __NOP();
  __NOP();
}

static void spi_rx_flush(void)
{
  volatile uint32_t dummy = 0U;

  while((SPI1->sts & SPI_I2S_RDBF_FLAG) != 0U)
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

static uint16_t spi_xfer16(uint16_t tx)
{
  uint32_t guard = SPI_TIMEOUT;

  while(((SPI1->sts & SPI_I2S_TDBE_FLAG) == 0U) && (guard-- != 0U))
  {
  }
  SPI1->dt = (uint32_t)tx;

  guard = SPI_TIMEOUT;
  while(((SPI1->sts & SPI_I2S_RDBF_FLAG) == 0U) && (guard-- != 0U))
  {
  }
  return (uint16_t)SPI1->dt;
}

static void spi_wait_idle(void)
{
  uint32_t guard = SPI_TIMEOUT;

  while((((SPI1->sts & SPI_I2S_TDBE_FLAG) == 0U) ||
         ((SPI1->sts & SPI_I2S_BF_FLAG) != 0U)) && (guard-- != 0U))
  {
  }
}

static void i2s_mck_off(void)
{
  crm_periph_clock_enable(CRM_SCFG_PERIPH_CLOCK, TRUE);
  SCFG->cfg2_bit.i2s_fd = 0U;
  spi_i2s_reset(SPI1);
  SPI1->i2sctrl = 0U;
  SPI1->i2sclk = 0x00000002U;
}

void lsm6dsv_spi_init(void)
{
  gpio_init_type gpio;
  spi_init_type spi;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);
  i2s_mck_off();

  /* Mode 0: SCK idle low before AF. CS idle high. */
  gpio_default_para_init(&gpio);
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_pull = GPIO_PULL_DOWN;
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio.gpio_pins = GPIO_PINS_5;
  gpio_init(GPIOA, &gpio);
  gpio_bits_reset(GPIOA, GPIO_PINS_5);

  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = GPIO_PINS_4 | GPIO_PINS_7;
  gpio_init(GPIOA, &gpio);
  CS_HIGH();
  gpio_bits_reset(GPIOA, GPIO_PINS_7);

  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE5, GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE6, GPIO_MUX_5);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE7, GPIO_MUX_5);

  gpio.gpio_mode = GPIO_MODE_MUX;
  gpio.gpio_pull = GPIO_PULL_DOWN;
  gpio.gpio_pins = GPIO_PINS_5;
  gpio_init(GPIOA, &gpio);

  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_pins = GPIO_PINS_7;
  gpio_init(GPIOA, &gpio);

  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = GPIO_PINS_6;
  gpio_init(GPIOA, &gpio);

  spi_default_para_init(&spi);
  spi.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi.master_slave_mode = SPI_MODE_MASTER;
  spi.mclk_freq_division = LSM6DSV_SPI_DIV;
  spi.first_bit_transmission = SPI_FIRST_BIT_MSB;
  /* One 16-bit frame = article's 8-bit addr + 8-bit dummy in a single CS. */
  spi.frame_bit_num = SPI_FRAME_16BIT;
  spi.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spi.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI1, &spi);
  spi_ti_mode_enable(SPI1, FALSE);
  spi_hardware_cs_output_enable(SPI1, FALSE);
  SPI1->i2sctrl = 0U;
  SPI1->i2sclk = 0x00000002U;
  spi_rx_flush();
  spi_enable(SPI1, TRUE);
  delay_us(20U);
}

#else

#define SETH(pin)  (GPIOA->scr = (uint32_t)(1U << (pin)))
#define CLRH(pin)  (GPIOA->scr = (uint32_t)(1U << ((pin) + 16U)))
#define RD6()      (((GPIOA->idt >> 6) & 1U) != 0U)

static void pause_gpio(void)
{
  volatile uint32_t n = 40U;
  while(n-- != 0U)
  {
  }
}

static uint8_t spi_xfer8_gpio(uint8_t byte)
{
  uint8_t miso = 0U;
  uint8_t i;

  for(i = 0U; i < 8U; i++)
  {
    if((byte & 0x80U) != 0U) SETH(7); else CLRH(7);
    byte = (uint8_t)(byte << 1);
    pause_gpio();
    CLRH(5);
    pause_gpio();
    SETH(5);
    pause_gpio();
    miso = (uint8_t)((miso << 1) | (RD6() ? 1U : 0U));
  }
  return miso;
}

static void spi_wait_idle(void)
{
}

void lsm6dsv_spi_init(void)
{
  gpio_init_type gpio;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  gpio_default_para_init(&gpio);
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio.gpio_pins = GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_7;
  gpio_init(GPIOA, &gpio);
  gpio.gpio_mode = GPIO_MODE_INPUT;
  gpio.gpio_pins = GPIO_PINS_6;
  gpio_init(GPIOA, &gpio);
  GPIOA->muxl &= ~0xFFFF0000U;
  CS_HIGH();
  SETH(5);
  CLRH(7);
}

#endif

uint8_t lsm6dsv_read_reg(uint8_t reg)
{
  uint8_t value;

#if LSM6DSV_USE_HW_SPI
  uint16_t rx;
  spi_rx_flush();
  cs_low();
  rx = spi_xfer16((uint16_t)((uint16_t)(reg | 0x80U) << 8));
  spi_wait_idle();
  cs_high();
  value = (uint8_t)rx;
#else
  CS_LOW();
  pause_gpio();
  (void)spi_xfer8_gpio((uint8_t)(reg | 0x80U));
  value = spi_xfer8_gpio(0x00U);
  spi_wait_idle();
  CS_HIGH();
  pause_gpio();
#endif
  return value;
}

void lsm6dsv_write_reg(uint8_t reg, uint8_t value)
{
#if LSM6DSV_USE_HW_SPI
  spi_rx_flush();
  cs_low();
  (void)spi_xfer16((uint16_t)(((uint16_t)(reg & 0x7FU) << 8) | value));
  spi_wait_idle();
  cs_high();
#else
  CS_LOW();
  pause_gpio();
  (void)spi_xfer8_gpio((uint8_t)(reg & 0x7FU));
  (void)spi_xfer8_gpio(value);
  spi_wait_idle();
  CS_HIGH();
  pause_gpio();
#endif
}

static void lsm6dsv_read_burst(uint8_t reg, uint8_t *buf, uint8_t len)
{
  uint8_t pos = 0U;

  if(len == 0U) return;

#if LSM6DSV_USE_HW_SPI
  uint16_t rx;
  spi_rx_flush();
  cs_low();
  rx = spi_xfer16((uint16_t)((uint16_t)(reg | 0x80U) << 8));
  buf[pos++] = (uint8_t)rx;
  while((uint8_t)(len - pos) >= 2U)
  {
    rx = spi_xfer16(0x0000U);
    buf[pos++] = (uint8_t)(rx >> 8);
    buf[pos++] = (uint8_t)rx;
  }
  if(pos < len)
  {
    rx = spi_xfer16(0x0000U);
    buf[pos] = (uint8_t)(rx >> 8);
  }
  spi_wait_idle();
  cs_high();
#else
  CS_LOW();
  pause_gpio();
  (void)spi_xfer8_gpio((uint8_t)(reg | 0x80U));
  while(pos < len)
  {
    buf[pos++] = spi_xfer8_gpio(0x00U);
  }
  spi_wait_idle();
  CS_HIGH();
  pause_gpio();
#endif
}

int lsm6dsv_probe_spi_modes(uint8_t who_mode[4])
{
  uint8_t i;
  int good = 0;

  lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U);
  for(i = 0U; i < 4U; i++)
  {
    who_mode[i] = lsm6dsv_read_reg(REG_WHO_AM_I);
    if(who_mode[i] == LSM6DSV_WHO_AM_I_VAL) good++;
  }
  return (good != 0) ? good : -1;
}

uint32_t lsm6dsv_probe_tail(void)
{
#if LSM6DSV_USE_HW_SPI
  return SPI1->i2sctrl | (SPI1->i2sclk << 16);
#else
  return 0U;
#endif
}

static int16_t le16(const uint8_t *p)
{
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

int lsm6dsv_init_2khz(void)
{
  uint32_t guard;
  uint8_t who = 0U;
  uint8_t n;

  delay_ms(20U);
  /* FUNC_CFG_ACCESS is available for returning to the main register bank. */
  lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U);
  for(n = 0U; n < 8U; n++)
  {
    who = lsm6dsv_read_reg(REG_WHO_AM_I);
    if(who == LSM6DSV_WHO_AM_I_VAL) break;
    delay_ms(2U);
  }
  if(who != LSM6DSV_WHO_AM_I_VAL) return -1;

  lsm6dsv_write_reg(REG_CTRL1, 0x00U);
  lsm6dsv_write_reg(REG_CTRL2, 0x00U);
  lsm6dsv_write_reg(REG_CTRL3, CTRL3_SW_RESET);
  guard = 0U;
  while((lsm6dsv_read_reg(REG_CTRL3) & CTRL3_SW_RESET) != 0U)
  {
    if(++guard > 10000U) return -2;
  }
  delay_ms(10U);

  lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U);
  lsm6dsv_write_reg(REG_IF_CFG, IF_CFG_I2C_I3C_DISABLE);
  lsm6dsv_write_reg(REG_CTRL3, (uint8_t)(CTRL3_BDU | CTRL3_IF_INC));
  lsm6dsv_write_reg(REG_CTRL4, CTRL4_DRDY_MASK);
  lsm6dsv_write_reg(REG_CTRL6, FS_G_2000DPS);
  lsm6dsv_write_reg(REG_CTRL7, 0x00U);
  lsm6dsv_write_reg(REG_CTRL8, FS_XL_4G);
  lsm6dsv_write_reg(REG_HAODR_CFG, HAODR_SEL_HA01);
  lsm6dsv_write_reg(REG_INT1_CTRL, (uint8_t)(INT1_DRDY_XL | INT1_DRDY_G));
  /* 0x1A = HA ODR mode + nibble 0xA; HAODR_SEL=1 => 2000 Hz. */
  lsm6dsv_write_reg(REG_CTRL1, HA01_2000HZ);
  lsm6dsv_write_reg(REG_CTRL2, HA01_2000HZ);
  lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U);

  delay_ms(20U);
  if(lsm6dsv_read_reg(REG_WHO_AM_I) != LSM6DSV_WHO_AM_I_VAL) return -3;
  if(lsm6dsv_read_reg(REG_CTRL1) != HA01_2000HZ) return -4;
  if(lsm6dsv_read_reg(REG_CTRL2) != HA01_2000HZ) return -5;
  if((lsm6dsv_read_reg(REG_HAODR_CFG) & 0x03U) != HAODR_SEL_HA01) return -6;
  if((lsm6dsv_read_reg(REG_CTRL7) & LPF1_G_EN) != 0U) return -8;
  if(lsm6dsv_wait_sample(5000U) != 0) return -7;
  return 0;
}

int lsm6dsv_data_ready(void)
{
  return ((lsm6dsv_read_reg(REG_STATUS) & STATUS_XGDA) == STATUS_XGDA) ? 1 : 0;
}

int lsm6dsv_wait_sample(uint32_t timeout_us)
{
  uint32_t start = dwt_cycles();
  uint32_t ticks = (system_core_clock / 1000000U) * timeout_us;

  if(ticks == 0U)
  {
    ticks = 1U;
  }

  while((uint32_t)(dwt_cycles() - start) < ticks)
  {
    if(lsm_int1_read() != 0U)
    {
      return 0;
    }
    if(lsm6dsv_data_ready() != 0)
    {
      return 0;
    }
  }
  return -1;
}

int lsm6dsv_read_raw(lsm6dsv_raw_t *raw)
{
  uint8_t buf[12];

  lsm6dsv_read_burst(REG_OUTX_L_G, buf, 12U);
  raw->gyr[0] = le16(&buf[0]);
  raw->gyr[1] = le16(&buf[2]);
  raw->gyr[2] = le16(&buf[4]);
  raw->acc[0] = le16(&buf[6]);
  raw->acc[1] = le16(&buf[8]);
  raw->acc[2] = le16(&buf[10]);
  return 0;
}




