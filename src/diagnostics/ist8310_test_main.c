/**
 * IST8310 minimum soldering test for AT32F423KCU7-4.
 *
 * Wiring:
 *   PB6 -> SCL, PB7 <-> SDA, PB2 <- DRDY, PB3 -> RSTN
 *
 * The firmware bit-bangs a slow I2C bus, scans all four legal IST8310
 * addresses (0x0C..0x0F), verifies WIA=0x10, then repeatedly performs
 * single measurements. Results are kept in the volatile mag_live object
 * so they can be inspected through DAPLink even without UART test pads.
 */

#include "at32f423_clock.h"
#include "bsp.h"

#define IST_WIA        0x00U
#define IST_WIA_VAL    0x10U
#define IST_STAT1      0x02U
#define IST_DATAXL     0x03U
#define IST_CNTL1      0x0AU
#define IST_CNTL2      0x0BU
#define IST_AVGCNTL    0x41U
#define IST_PDCNTL     0x42U
#define IST_SINGLE     0x01U
#define IST_SRST       0x01U
#define IST_PDCNTL_VAL 0xC0U

#define MAG_LIVE_MAGIC 0x49535431U /* "IST1" */

/* mag_live.err values */
#define MAG_OK                  0
#define MAG_ERR_NOT_FOUND      -1
#define MAG_ERR_BUS_STUCK      -2
#define MAG_ERR_INIT_WRITE     -3
#define MAG_ERR_INIT_VERIFY    -4
#define MAG_ERR_TRIGGER        -5
#define MAG_ERR_DATA_TIMEOUT   -6
#define MAG_ERR_DATA_READ      -7

typedef struct
{
  uint32_t magic;
  uint32_t seq;
  int32_t  err;
  uint32_t addr7;
  uint32_t who;
  int32_t  mx;
  int32_t  my;
  int32_t  mz;
  uint32_t nack;
  uint32_t millis;
  uint32_t stat1;
  uint32_t drdy;
  uint32_t scl;
  uint32_t sda;
  uint32_t pdcntl;
} mag_live_t;

volatile mag_live_t mag_live;

#define SCL_H()  gpio_bits_set(GPIOB, GPIO_PINS_6) /* release open-drain */
#define SCL_L()  gpio_bits_reset(GPIOB, GPIO_PINS_6)
#define SDA_H()  gpio_bits_set(GPIOB, GPIO_PINS_7) /* release open-drain */
#define SDA_L()  gpio_bits_reset(GPIOB, GPIO_PINS_7)
#define SCL_R()  (gpio_input_data_bit_read(GPIOB, GPIO_PINS_6) != RESET)
#define SDA_R()  (gpio_input_data_bit_read(GPIOB, GPIO_PINS_7) != RESET)
#define DRDY_R() (gpio_input_data_bit_read(GPIOB, GPIO_PINS_2) != RESET)
#define RST_H()  gpio_bits_set(GPIOB, GPIO_PINS_3)
#define RST_L()  gpio_bits_reset(GPIOB, GPIO_PINS_3)

static void i2c_delay(void)
{
  delay_us(5U); /* deliberately slow for first-board/solder testing */
}

static void mag_snapshot_pins(void)
{
  mag_live.scl = SCL_R() ? 1U : 0U;
  mag_live.sda = SDA_R() ? 1U : 0U;
  mag_live.drdy = DRDY_R() ? 1U : 0U;
}

static void mag_pins_init(void)
{
  gpio_init_type gpio;

  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  /* Set the output latch before PB3 becomes an output, avoiding a reset glitch. */
  RST_H();
  gpio_default_para_init(&gpio);
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio.gpio_pins = GPIO_PINS_3;
  gpio_init(GPIOB, &gpio);

  gpio_default_para_init(&gpio);
  gpio.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio.gpio_pins = GPIO_PINS_6 | GPIO_PINS_7;
  gpio_init(GPIOB, &gpio);
  SCL_H();
  SDA_H();

  gpio_default_para_init(&gpio);
  gpio.gpio_pull = GPIO_PULL_DOWN;
  gpio.gpio_mode = GPIO_MODE_INPUT;
  gpio.gpio_pins = GPIO_PINS_2;
  gpio_init(GPIOB, &gpio);
}

static void ist_hard_reset(void)
{
  RST_L();
  delay_ms(5U);
  RST_H();
  delay_ms(50U); /* datasheet POR maximum is 50 ms */
}

static int i2c_bus_recover(void)
{
  uint8_t i;

  SDA_H();
  SCL_H();
  i2c_delay();
  if(!SCL_R())
  {
    return -1;
  }

  if(!SDA_R())
  {
    for(i = 0U; i < 9U; i++)
    {
      SCL_L();
      i2c_delay();
      SCL_H();
      i2c_delay();
      if(SDA_R())
      {
        break;
      }
    }
  }

  /* Generate STOP and verify that both externally pulled-up lines are high. */
  SDA_L();
  i2c_delay();
  SCL_H();
  i2c_delay();
  SDA_H();
  i2c_delay();
  return (SCL_R() && SDA_R()) ? 0 : -1;
}

static void i2c_start(void)
{
  SDA_H();
  SCL_H();
  i2c_delay();
  SDA_L();
  i2c_delay();
  SCL_L();
  i2c_delay();
}

static void i2c_stop(void)
{
  SDA_L();
  i2c_delay();
  SCL_H();
  i2c_delay();
  SDA_H();
  i2c_delay();
}

static uint8_t i2c_write_byte(uint8_t byte)
{
  uint8_t i;
  uint8_t nack;

  for(i = 0U; i < 8U; i++)
  {
    if((byte & 0x80U) != 0U) SDA_H(); else SDA_L();
    byte = (uint8_t)(byte << 1);
    i2c_delay();
    SCL_H();
    i2c_delay();
    SCL_L();
    i2c_delay();
  }
  SDA_H();
  i2c_delay();
  SCL_H();
  i2c_delay();
  nack = SDA_R() ? 1U : 0U;
  SCL_L();
  i2c_delay();
  return nack;
}

static uint8_t i2c_read_byte(uint8_t ack)
{
  uint8_t i;
  uint8_t byte = 0U;

  SDA_H();
  for(i = 0U; i < 8U; i++)
  {
    byte = (uint8_t)(byte << 1);
    i2c_delay();
    SCL_H();
    i2c_delay();
    if(SDA_R()) byte |= 1U;
    SCL_L();
    i2c_delay();
  }
  if(ack != 0U) SDA_L(); else SDA_H();
  i2c_delay();
  SCL_H();
  i2c_delay();
  SCL_L();
  SDA_H();
  i2c_delay();
  return byte;
}

static int ist_write(uint8_t addr7, uint8_t reg, uint8_t val)
{
  i2c_start();
  if(i2c_write_byte((uint8_t)(addr7 << 1)) != 0U ||
     i2c_write_byte(reg) != 0U || i2c_write_byte(val) != 0U)
  {
    i2c_stop();
    mag_live.nack++;
    return -1;
  }
  i2c_stop();
  return 0;
}

static int ist_read(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len)
{
  uint8_t i;

  i2c_start();
  if(i2c_write_byte((uint8_t)(addr7 << 1)) != 0U ||
     i2c_write_byte(reg) != 0U)
  {
    i2c_stop();
    mag_live.nack++;
    return -1;
  }
  i2c_start();
  if(i2c_write_byte((uint8_t)((addr7 << 1) | 1U)) != 0U)
  {
    i2c_stop();
    mag_live.nack++;
    return -1;
  }
  for(i = 0U; i < len; i++)
  {
    buf[i] = i2c_read_byte((i + 1U) < len ? 1U : 0U);
  }
  i2c_stop();
  return 0;
}

static uint8_t ist_scan(void)
{
  uint8_t addr;
  uint8_t who;

  /* CAD1/CAD0 select exactly 0x0C, 0x0D, 0x0E or 0x0F. */
  for(addr = 0x0CU; addr <= 0x0FU; addr++)
  {
    who = 0U;
    if(ist_read(addr, IST_WIA, &who, 1U) == 0)
    {
      mag_live.addr7 = addr;
      mag_live.who = who;
      if(who == IST_WIA_VAL)
      {
        return addr;
      }
    }
  }
  return 0U;
}

static int ist_init(uint8_t addr)
{
  uint8_t value = 0U;

  if(ist_write(addr, IST_CNTL2, IST_SRST) != 0)
  {
    return MAG_ERR_INIT_WRITE;
  }
  delay_ms(50U);

  /* Required performance-optimization setting from the IST8310 datasheet. */
  if(ist_write(addr, IST_PDCNTL, IST_PDCNTL_VAL) != 0)
  {
    return MAG_ERR_INIT_WRITE;
  }
  /* No averaging keeps this soldering test simple and permits 200 Hz operation. */
  if(ist_write(addr, IST_AVGCNTL, 0x00U) != 0)
  {
    return MAG_ERR_INIT_WRITE;
  }
  if(ist_read(addr, IST_PDCNTL, &value, 1U) != 0)
  {
    return MAG_ERR_INIT_VERIFY;
  }
  mag_live.pdcntl = value;
  return (value == IST_PDCNTL_VAL) ? MAG_OK : MAG_ERR_INIT_VERIFY;
}

static int ist_measure_once(uint8_t addr)
{
  uint8_t buf[6];
  uint8_t st = 0U;
  uint32_t timeout;

  if(ist_write(addr, IST_CNTL1, IST_SINGLE) != 0)
  {
    return MAG_ERR_TRIGGER;
  }

  /* Use both the routed PB2 DRDY signal and STAT1; timeout prevents lock-up. */
  for(timeout = 0U; timeout < 20U; timeout++)
  {
    if(DRDY_R())
    {
      break;
    }
    if(ist_read(addr, IST_STAT1, &st, 1U) != 0)
    {
      return MAG_ERR_DATA_READ;
    }
    mag_live.stat1 = st;
    if((st & 0x01U) != 0U)
    {
      break;
    }
    delay_ms(1U);
  }
  if(timeout >= 20U)
  {
    return MAG_ERR_DATA_TIMEOUT;
  }

  if(ist_read(addr, IST_DATAXL, buf, 6U) != 0)
  {
    return MAG_ERR_DATA_READ;
  }
  mag_live.mx = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
  mag_live.my = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
  mag_live.mz = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
  return MAG_OK;
}

int main(void)
{
  uint8_t addr = 0U;
  int result;

  system_clock_config();
  bsp_init();
  mag_pins_init();

  mag_live.magic = MAG_LIVE_MAGIC;
  mag_live.err = MAG_ERR_NOT_FOUND;
  mag_snapshot_pins();

  ist_hard_reset();
  if(i2c_bus_recover() != 0)
  {
    mag_live.err = MAG_ERR_BUS_STUCK;
  }

  while(addr == 0U)
  {
    if(i2c_bus_recover() == 0)
    {
      addr = ist_scan();
      mag_live.err = (addr == 0U) ? MAG_ERR_NOT_FOUND : MAG_OK;
    }
    else
    {
      mag_live.err = MAG_ERR_BUS_STUCK;
    }
    mag_live.millis = millis();
    mag_snapshot_pins();
    mag_live.seq++;
    led_toggle();
    if(addr == 0U) delay_ms(200U);
  }

  result = ist_init(addr);
  mag_live.err = result;

  while(1)
  {
    if(result == MAG_OK)
    {
      result = ist_measure_once(addr);
      mag_live.err = result;
    }
    else
    {
      /* Retry initialization/communication after a transient solder contact. */
      delay_ms(100U);
      result = ist_init(addr);
      mag_live.err = result;
    }
    mag_live.millis = millis();
    mag_snapshot_pins();
    mag_live.seq++;
    led_toggle();
    delay_ms(100U);
  }
}
