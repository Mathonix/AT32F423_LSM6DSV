/**
 * LSM6DSV 4-wire SPI1.
 * Mode 0, 16-bit frames, software CS on PA4, PA6 pull-up.
 * APB2=75 MHz, DIV_16 => SCK ≈ 4.6875 MHz.
 *
 * PA5 = SPI1_SCK AF5, PA6 = SPI1_MISO AF5, PA7 = SPI1_MOSI AF5.
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
#define REG_FIFO_CTRL4       0x0AU
#define REG_FIFO_STATUS1     0x1BU
#define REG_FIFO_STATUS2     0x1CU
#define REG_FIFO_DATA_OUT_TAG 0x78U
#define REG_OUT_TEMP_L       0x20U
#define REG_OUTX_L_G         0x22U
#define REG_HAODR_CFG        0x62U
#define REG_EMB_FUNC_EN_A    0x04U
#define REG_EMB_FUNC_FIFO_EN_A 0x44U
#define REG_SFLP_ODR         0x5EU

#define IF_CFG_I2C_I3C_DISABLE  0x01U
#define CTRL3_SW_RESET          0x01U
#define CTRL3_IF_INC            0x04U
#define CTRL3_BDU               0x40U
#define CTRL4_DRDY_MASK         0x08U
#define INT1_DRDY_XL            0x01U
#define INT1_DRDY_G             0x02U
#define FS_G_2000DPS            0x04U
#define LPF1_G_EN               0x01U
/* LSM6DSV: HM_MODE=0 selects high-performance mode. */
#define CTRL6_XL_HM_MODE        0x10U
#define CTRL7_G_HM_MODE         0x80U
#define FS_XL_4G                0x01U
#define HA01_2000HZ             0x1AU
#define HAODR_SEL_HA01          0x01U
#define STATUS_XLDA             0x01U
#define STATUS_GDA              0x02U
#define STATUS_XGDA             (STATUS_XLDA | STATUS_GDA)
#define FUNC_CFG_ACCESS_EMB     0x80U
#define SFLP_GAME_EN            0x02U
#define SFLP_GAME_FIFO_EN       0x02U
#define SFLP_GBIAS_FIFO_EN      0x20U
#define SFLP_ODR_30HZ           0x4BU
#define FIFO_MODE_BYPASS        0x00U
#define FIFO_MODE_CONTINUOUS    0x06U
#define FIFO_TAG_SFLP_GBIAS     0x16U
#define SFLP_GBIAS_MDPS_PER_LSB 4.375f

#define CS_HIGH() gpio_bits_set(GPIOA, GPIO_PINS_4)
#define CS_LOW()  gpio_bits_reset(GPIOA, GPIO_PINS_4)

#if LSM6DSV_USE_HW_SPI

#ifndef LSM6DSV_SPI_DIV
/* APB2 = 75 MHz, DIV_16 => SCK ≈ 4.6875 MHz. */
#define LSM6DSV_SPI_DIV SPI_MCLK_DIV_16
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

static int spi_xfer16(uint16_t tx, uint16_t *rx)
{
  uint32_t guard = SPI_TIMEOUT;

  while(((SPI1->sts & SPI_I2S_TDBE_FLAG) == 0U) && (guard-- != 0U))
  {
  }
  if(guard == 0U)
  {
    return -1;
  }
  SPI1->dt = (uint32_t)tx;

  guard = SPI_TIMEOUT;
  while(((SPI1->sts & SPI_I2S_RDBF_FLAG) == 0U) && (guard-- != 0U))
  {
  }
  if(guard == 0U)
  {
    return -2;
  }
  if(rx != 0)
  {
    *rx = (uint16_t)SPI1->dt;
  }
  else
  {
    (void)SPI1->dt;
  }
  return 0;
}

static int spi_wait_idle(void)
{
  uint32_t guard = SPI_TIMEOUT;

  while((((SPI1->sts & SPI_I2S_TDBE_FLAG) == 0U) ||
         ((SPI1->sts & SPI_I2S_BF_FLAG) != 0U)) && (guard-- != 0U))
  {
  }
  return (guard == 0U) ? -1 : 0;
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
  /* 16-bit Motorola Mode 0: one frame is command+first data byte. */
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

int lsm6dsv_spi_recover(void)
{
  lsm6dsv_spi_init();
  return 0;
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

int lsm6dsv_spi_recover(void)
{
  lsm6dsv_spi_init();
  return 0;
}

#endif

int lsm6dsv_read_reg(uint8_t reg, uint8_t *value)
{
  if(value == 0)
  {
    return -1;
  }

#if LSM6DSV_USE_HW_SPI
  uint16_t rx;
  cs_low();
  if(spi_xfer16((uint16_t)((uint16_t)(reg | 0x80U) << 8), &rx) != 0)
  {
    cs_high();
    return -2;
  }
  if(spi_wait_idle() != 0)
  {
    cs_high();
    return -3;
  }
  cs_high();
  *value = (uint8_t)rx;
  return 0;
#else
  CS_LOW();
  pause_gpio();
  (void)spi_xfer8_gpio((uint8_t)(reg | 0x80U));
  *value = spi_xfer8_gpio(0x00U);
  CS_HIGH();
  pause_gpio();
  return 0;
#endif
}

int lsm6dsv_write_reg(uint8_t reg, uint8_t value)
{
#if LSM6DSV_USE_HW_SPI
  cs_low();
  if(spi_xfer16((uint16_t)(((uint16_t)(reg & 0x7FU) << 8) | value), 0) != 0)
  {
    cs_high();
    return -2;
  }
  if(spi_wait_idle() != 0)
  {
    cs_high();
    return -3;
  }
  cs_high();
  return 0;
#else
  CS_LOW();
  pause_gpio();
  (void)spi_xfer8_gpio((uint8_t)(reg & 0x7FU));
  (void)spi_xfer8_gpio(value);
  CS_HIGH();
  pause_gpio();
  return 0;
#endif
}

static int write_reg_mask(uint8_t reg, uint8_t mask, uint8_t bits)
{
  uint8_t cur;

  if(lsm6dsv_read_reg(reg, &cur) != 0)
  {
    return -1;
  }
  cur = (uint8_t)((cur & (uint8_t)~mask) | (bits & mask));
  return lsm6dsv_write_reg(reg, cur);
}

static int lsm6dsv_read_burst(uint8_t reg, uint8_t *buf, uint8_t len)
{
  uint8_t pos = 0U;

  if((buf == 0) || (len == 0U))
  {
    return -1;
  }

#if LSM6DSV_USE_HW_SPI
  uint16_t rx;
  cs_low();
  if(spi_xfer16((uint16_t)((uint16_t)(reg | 0x80U) << 8), &rx) != 0)
  {
    cs_high();
    return -2;
  }
  buf[pos++] = (uint8_t)rx;
  while((uint8_t)(len - pos) >= 2U)
  {
    if(spi_xfer16(0x0000U, &rx) != 0)
    {
      cs_high();
      return -2;
    }
    buf[pos++] = (uint8_t)(rx >> 8);
    buf[pos++] = (uint8_t)rx;
  }
  /* 16-bit frames cannot stop on an odd leftover byte: one extra dummy
   * clocked byte is discarded so only `len` payload bytes are kept. */
  if(pos < len)
  {
    if(spi_xfer16(0x0000U, &rx) != 0)
    {
      cs_high();
      return -2;
    }
    buf[pos] = (uint8_t)(rx >> 8);
  }
  if(spi_wait_idle() != 0)
  {
    cs_high();
    return -3;
  }
  cs_high();
  return 0;
#else
  CS_LOW();
  pause_gpio();
  (void)spi_xfer8_gpio((uint8_t)(reg | 0x80U));
  while(pos < len)
  {
    buf[pos++] = spi_xfer8_gpio(0x00U);
  }
  CS_HIGH();
  pause_gpio();
  return 0;
#endif
}

int lsm6dsv_probe_whoami(uint8_t who_mode[4])
{
  uint8_t i;
  int good = 0;

  if(who_mode == 0)
  {
    return -1;
  }
  (void)lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U);
  for(i = 0U; i < 4U; i++)
  {
    if(lsm6dsv_read_reg(REG_WHO_AM_I, &who_mode[i]) != 0)
    {
      who_mode[i] = 0xFFU;
      continue;
    }
    if(who_mode[i] == LSM6DSV_WHO_AM_I_VAL)
    {
      good++;
    }
  }
  return (good != 0) ? good : -1;
}

static int16_t le16(const uint8_t *p)
{
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int expect_reg(uint8_t reg, uint8_t mask, uint8_t want)
{
  uint8_t v;

  if(lsm6dsv_read_reg(reg, &v) != 0)
  {
    return -1;
  }
  return ((v & mask) == want) ? 0 : -1;
}

int lsm6dsv_init_2khz(void)
{
  uint32_t guard;
  uint8_t who = 0U;
  uint8_t n;
  uint8_t v;

  delay_ms(20U);
  if(lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U) != 0)
  {
    return -1;
  }
  for(n = 0U; n < 8U; n++)
  {
    if((lsm6dsv_read_reg(REG_WHO_AM_I, &who) == 0) &&
       (who == LSM6DSV_WHO_AM_I_VAL))
    {
      break;
    }
    delay_ms(2U);
  }
  if(who != LSM6DSV_WHO_AM_I_VAL)
  {
    return -1;
  }

  (void)lsm6dsv_write_reg(REG_CTRL1, 0x00U);
  (void)lsm6dsv_write_reg(REG_CTRL2, 0x00U);
  if(lsm6dsv_write_reg(REG_CTRL3, CTRL3_SW_RESET) != 0)
  {
    return -2;
  }
  guard = 0U;
  do
  {
    if(lsm6dsv_read_reg(REG_CTRL3, &v) != 0)
    {
      return -2;
    }
    if(++guard > 10000U)
    {
      return -2;
    }
  } while((v & CTRL3_SW_RESET) != 0U);
  delay_ms(10U);

  if(lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U) != 0) return -9;
  if(write_reg_mask(REG_IF_CFG, IF_CFG_I2C_I3C_DISABLE, IF_CFG_I2C_I3C_DISABLE) != 0) return -9;
  if(write_reg_mask(REG_CTRL3, (uint8_t)(CTRL3_BDU | CTRL3_IF_INC),
                    (uint8_t)(CTRL3_BDU | CTRL3_IF_INC)) != 0) return -9;
  if(write_reg_mask(REG_CTRL4, CTRL4_DRDY_MASK, CTRL4_DRDY_MASK) != 0) return -9;
  if(write_reg_mask(REG_CTRL6, (uint8_t)(0x0FU | CTRL6_XL_HM_MODE), FS_G_2000DPS) != 0) return -9;
  /* Explicitly force accelerometer high-performance mode. */
  if(write_reg_mask(REG_CTRL7, (uint8_t)(LPF1_G_EN | CTRL7_G_HM_MODE), 0x00U) != 0) return -9;
  /* Explicitly force gyroscope high-performance mode and keep LPF1 off. */
  if(write_reg_mask(REG_CTRL8, 0x03U, FS_XL_4G) != 0) return -9;
  if(write_reg_mask(REG_HAODR_CFG, 0x03U, HAODR_SEL_HA01) != 0) return -9;
  /* Gyro DRDY is the sample beat; XLDA+GDA are checked before the burst. */
  if(lsm6dsv_write_reg(REG_INT1_CTRL, INT1_DRDY_G) != 0) return -9;
  if(lsm6dsv_write_reg(REG_CTRL1, HA01_2000HZ) != 0) return -9;
  if(lsm6dsv_write_reg(REG_CTRL2, HA01_2000HZ) != 0) return -9;
  if(lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U) != 0) return -9;

  delay_ms(20U);
  if(expect_reg(REG_WHO_AM_I, 0xFFU, LSM6DSV_WHO_AM_I_VAL) != 0) return -3;
  if(expect_reg(REG_CTRL1, 0xFFU, HA01_2000HZ) != 0) return -4;
  if(expect_reg(REG_CTRL2, 0xFFU, HA01_2000HZ) != 0) return -5;
  if(expect_reg(REG_HAODR_CFG, 0x03U, HAODR_SEL_HA01) != 0) return -6;
  if(expect_reg(REG_IF_CFG, IF_CFG_I2C_I3C_DISABLE, IF_CFG_I2C_I3C_DISABLE) != 0) return -10;
  if(expect_reg(REG_CTRL3, (uint8_t)(CTRL3_BDU | CTRL3_IF_INC),
                (uint8_t)(CTRL3_BDU | CTRL3_IF_INC)) != 0) return -10;
  if(expect_reg(REG_CTRL4, CTRL4_DRDY_MASK, CTRL4_DRDY_MASK) != 0) return -10;
  if(expect_reg(REG_CTRL6, (uint8_t)(0x0FU | CTRL6_XL_HM_MODE), FS_G_2000DPS) != 0) return -10;
  if(expect_reg(REG_CTRL7, (uint8_t)(LPF1_G_EN | CTRL7_G_HM_MODE), 0x00U) != 0) return -8;
  if(expect_reg(REG_CTRL8, 0x03U, FS_XL_4G) != 0) return -10;
  if(expect_reg(REG_INT1_CTRL, 0xFFU, INT1_DRDY_G) != 0) return -10;
  if(lsm6dsv_wait_sample(5000U) != 0) return -7;
  return 0;
}

int lsm6dsv_read_sflp_gbias(float gbias_dps[3], uint32_t settle_ms)
{
  uint8_t buf[7];
  uint8_t v = 0U;
  uint8_t level_l;
  uint8_t level_h;
  uint16_t level;
  uint32_t guard;
  uint32_t scan;
  float sum[3] = {0.0f, 0.0f, 0.0f};
  uint32_t n = 0U;

  if(gbias_dps == 0)
  {
    return -1;
  }

  /* This acquisition is startup-only. It leaves the sensor stopped; the
   * normal HAODR initialization follows immediately and resets it again. */
  delay_ms(20U);
  if(lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U) != 0) return -1;

  for(guard = 0U; guard < 8U; guard++)
  {
    if((lsm6dsv_read_reg(REG_WHO_AM_I, &v) == 0) &&
       (v == LSM6DSV_WHO_AM_I_VAL))
      break;
    delay_ms(2U);
  }
  if(v != LSM6DSV_WHO_AM_I_VAL) return -1;

  (void)lsm6dsv_write_reg(REG_CTRL1, 0x00U);
  (void)lsm6dsv_write_reg(REG_CTRL2, 0x00U);
  if(lsm6dsv_write_reg(REG_CTRL3, CTRL3_SW_RESET) != 0) return -2;
  guard = 0U;
  do
  {
    if(lsm6dsv_read_reg(REG_CTRL3, &v) != 0) return -2;
    if(++guard > 10000U) return -2;
  } while((v & CTRL3_SW_RESET) != 0U);
  delay_ms(10U);

  if(lsm6dsv_write_reg(REG_IF_CFG, IF_CFG_I2C_I3C_DISABLE) != 0) return -3;
  if(lsm6dsv_write_reg(REG_CTRL3, (uint8_t)(CTRL3_BDU | CTRL3_IF_INC)) != 0) return -3;
  if(lsm6dsv_write_reg(REG_CTRL8, 0x00U) != 0) return -3;  /* +/-2 g */
  if(lsm6dsv_write_reg(REG_CTRL6, FS_G_2000DPS) != 0) return -3;
  if(lsm6dsv_write_reg(REG_FIFO_CTRL4, FIFO_MODE_BYPASS) != 0) return -3;

  if(lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, FUNC_CFG_ACCESS_EMB) != 0) return -4;
  if(lsm6dsv_write_reg(REG_EMB_FUNC_EN_A, SFLP_GAME_EN) != 0) return -4;
  if(lsm6dsv_write_reg(REG_EMB_FUNC_FIFO_EN_A,
                       (uint8_t)(SFLP_GAME_FIFO_EN | SFLP_GBIAS_FIFO_EN)) != 0) return -4;
  if(lsm6dsv_write_reg(REG_SFLP_ODR, SFLP_ODR_30HZ) != 0) return -4;
  if(lsm6dsv_write_reg(REG_FUNC_CFG_ACCESS, 0x00U) != 0) return -4;

  if(lsm6dsv_write_reg(REG_CTRL1, 0x04U) != 0) return -5;  /* XL 30 Hz */
  if(lsm6dsv_write_reg(REG_CTRL2, 0x04U) != 0) return -5;  /* GY 30 Hz */
  if(lsm6dsv_write_reg(REG_FIFO_CTRL4, FIFO_MODE_CONTINUOUS) != 0) return -5;

  delay_ms(settle_ms);

  /* Drain one settled FIFO batch and average the GBIAS frames. */
  for(scan = 0U; (scan < 512U) && (n < 32U); scan++)
  {
    if(lsm6dsv_read_reg(REG_FIFO_STATUS1, &level_l) != 0) break;
    if(lsm6dsv_read_reg(REG_FIFO_STATUS2, &level_h) != 0) break;
    level = (uint16_t)level_l | (uint16_t)((level_h & 0x01U) << 8);
    if(level == 0U)
    {
      delay_ms(2U);
      continue;
    }
    if(lsm6dsv_read_burst(REG_FIFO_DATA_OUT_TAG, buf, 7U) != 0) break;
    if((buf[0] >> 3) == FIFO_TAG_SFLP_GBIAS)
    {
      int16_t x = (int16_t)((uint16_t)buf[1] | ((uint16_t)buf[2] << 8));
      int16_t y = (int16_t)((uint16_t)buf[3] | ((uint16_t)buf[4] << 8));
      int16_t z = (int16_t)((uint16_t)buf[5] | ((uint16_t)buf[6] << 8));
      sum[0] += (float)x * (SFLP_GBIAS_MDPS_PER_LSB / 1000.0f);
      sum[1] += (float)y * (SFLP_GBIAS_MDPS_PER_LSB / 1000.0f);
      sum[2] += (float)z * (SFLP_GBIAS_MDPS_PER_LSB / 1000.0f);
      n++;
    }
  }

  (void)lsm6dsv_write_reg(REG_CTRL1, 0x00U);
  (void)lsm6dsv_write_reg(REG_CTRL2, 0x00U);
  (void)lsm6dsv_write_reg(REG_FIFO_CTRL4, FIFO_MODE_BYPASS);

  if(n < 5U)
  {
    return -6;
  }
  gbias_dps[0] = sum[0] / (float)n;
  gbias_dps[1] = sum[1] / (float)n;
  gbias_dps[2] = sum[2] / (float)n;
  return 0;
}
int lsm6dsv_data_ready(void)
{
  uint8_t st;

  if(lsm6dsv_read_reg(REG_STATUS, &st) != 0)
  {
    return 0;
  }
  return ((st & STATUS_XGDA) == STATUS_XGDA) ? 1 : 0;
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
      if(lsm6dsv_data_ready() != 0)
      {
        return 0;
      }
    }
    else if(lsm6dsv_data_ready() != 0)
    {
      return 0;
    }
  }
  return -1;
}

int lsm6dsv_read_raw(lsm6dsv_raw_t *raw)
{
  uint8_t buf[14];

  if(raw == 0)
  {
    return -1;
  }
  if(lsm6dsv_read_burst(REG_OUT_TEMP_L, buf, 14U) != 0)
  {
    return -2;
  }
  raw->temp_raw = le16(&buf[0]);
  raw->gyr[0] = le16(&buf[2]);
  raw->gyr[1] = le16(&buf[4]);
  raw->gyr[2] = le16(&buf[6]);
  raw->acc[0] = le16(&buf[8]);
  raw->acc[1] = le16(&buf[10]);
  raw->acc[2] = le16(&buf[12]);
  return 0;
}







