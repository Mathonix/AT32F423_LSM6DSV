/**
 * Board-level: LED, USART4 DMA TX, DWT delay, 1 ms tick, INT pins.
 */

#include "bsp.h"

#if defined (__CC_ARM)
#pragma import(__use_no_semihosting)
struct __FILE
{
  int handle;
};
FILE __stdout;
void _sys_exit(int x)
{
  (void)x;
}
#endif

static volatile uint32_t g_millis;
static volatile uint8_t uart_dma_running;
static uint32_t g_dwt_last;
static uint64_t g_cycles_hi;

static void dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  g_dwt_last = 0U;
  g_cycles_hi = 0U;
}

uint32_t dwt_cycles(void)
{
  return DWT->CYCCNT;
}

static uint64_t dwt_cycles64(void)
{
  uint32_t now = DWT->CYCCNT;
  if(now < g_dwt_last)
  {
    g_cycles_hi += 0x100000000ULL;
  }
  g_dwt_last = now;
  return g_cycles_hi + (uint64_t)now;
}

void delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = (system_core_clock / 1000000U) * us;
  if(ticks == 0U)
  {
    ticks = 1U;
  }
  while((uint32_t)(DWT->CYCCNT - start) < ticks)
  {
  }
}

void delay_ms(uint32_t ms)
{
  uint32_t ticks = system_core_clock / 1000U;

  SysTick->LOAD = ticks - 1U;
  SysTick->VAL  = 0U;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

  while(ms--)
  {
    while((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0U)
    {
    }
  }

  SysTick->CTRL = 0U;
}

uint32_t millis(void)
{
  return (uint32_t)(dwt_cycles64() / (uint64_t)(system_core_clock / 1000U));
}

void bsp_systick_tick(void)
{
  g_millis++;
}

static void uart_send_byte(uint8_t ch)
{
  while(usart_flag_get(PRINT_UART, USART_TDBE_FLAG) == RESET)
  {
  }
  usart_data_transmit(PRINT_UART, ch);
}

#if defined (__GNUC__) && !defined (__clang__)
int __io_putchar(int ch)
#else
int fputc(int ch, FILE *f)
#endif
{
#if !defined (__GNUC__) || defined (__clang__)
  (void)f;
#endif
  if(ch == '\n')
  {
    uart_send_byte('\r');
  }
  uart_send_byte((uint8_t)ch);
  return ch;
}

#if defined (__GNUC__) && !defined (__CC_ARM)
int _write(int file, char *ptr, int len)
{
  int i;
  (void)file;
  for(i = 0; i < len; i++)
  {
    if(ptr[i] == '\n')
    {
      uart_send_byte('\r');
    }
    uart_send_byte((uint8_t)ptr[i]);
  }
  return len;
}
#endif

void led_on(void)
{
  gpio_bits_set(LED_GPIO, LED_PIN);
}

void led_off(void)
{
  gpio_bits_reset(LED_GPIO, LED_PIN);
}

void led_toggle(void)
{
  gpio_bits_toggle(LED_GPIO, LED_PIN);
}

uint8_t lsm_int1_read(void)
{
  return (gpio_input_data_bit_read(GPIOB, GPIO_PINS_0) == SET) ? 1U : 0U;
}

uint8_t lsm_int2_read(void)
{
  return (gpio_input_data_bit_read(GPIOB, GPIO_PINS_1) == SET) ? 1U : 0U;
}

static void led_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = LED_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(LED_GPIO, &gpio_init_struct);
  led_off();
}

static void int_pin_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pull = GPIO_PULL_DOWN;
  gpio_init(GPIOB, &gpio_init_struct);
}

#if APP_UART_ENABLE
static void uart_dma_init(void)
{
  dma_init_type dma_init_struct;

  crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);
  dma_reset(DMA1_CHANNEL1);
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL1, DMAMUX_DMAREQ_ID_USART4_TX);

  dma_default_para_init(&dma_init_struct);
  dma_init_struct.peripheral_base_addr = (uint32_t)&PRINT_UART->dt;
  dma_init_struct.memory_base_addr = 0;
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.buffer_size = 0;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_MEDIUM;
  dma_init(DMA1_CHANNEL1, &dma_init_struct);
}
#endif

#if !APP_UART_ENABLE
static void uart_pins_low_init(void)
{
  gpio_init_type gpio_init_struct;
  const uint16_t pins = GPIO_PINS_0 | GPIO_PINS_1;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);

  /* Configure PA0/PA1 as push-pull GPIOs, not USART alternate-function pins.
   * Reset the output latch before enabling the output driver so both pins
   * remain low throughout this diagnostic image. */
  gpio_bits_reset(GPIOA, pins);
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = pins;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_bits_reset(GPIOA, pins);
}
#endif

#if !APP_CAN_ENABLE
static void can_pins_low_init(void)
{
  gpio_init_type gpio_init_struct;
  const uint16_t pins = GPIO_PINS_2 | GPIO_PINS_3;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);

  /* Keep CAN2 pins disconnected from the alternate function and actively low. */
  gpio_bits_reset(GPIOA, pins);
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = pins;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_bits_reset(GPIOA, pins);
}
#endif

static void uart_init(void)
{
#if APP_UART_ENABLE
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_USART4_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);

  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE0, GPIO_MUX_8);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE1, GPIO_MUX_8);

  usart_init(PRINT_UART, PRINT_UART_BAUDRATE, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(PRINT_UART, TRUE);
  usart_receiver_enable(PRINT_UART, TRUE);
  usart_enable(PRINT_UART, TRUE);

  uart_dma_init();
#else
  uart_pins_low_init();
#endif
}

int uart_dma_busy(void)
{
#if APP_UART_ENABLE
  if(uart_dma_running)
  {
    if(dma_flag_get(DMA1_FDT1_FLAG) != RESET)
    {
      dma_flag_clear(DMA1_FDT1_FLAG);
      dma_channel_enable(DMA1_CHANNEL1, FALSE);
      uart_dma_running = 0;
    }
  }
  return (int)uart_dma_running;
#else
  return 0;
#endif
}

int uart_dma_send(const uint8_t *data, uint16_t len)
{
#if APP_UART_ENABLE
  if(len == 0U)
  {
    return 0;
  }
  if(uart_dma_busy())
  {
    return -1;
  }

  dma_channel_enable(DMA1_CHANNEL1, FALSE);
  dma_flag_clear(DMA1_FDT1_FLAG);
  dma_flag_clear(DMA1_DTERR1_FLAG);
  DMA1_CHANNEL1->maddr = (uint32_t)data;
  dma_data_number_set(DMA1_CHANNEL1, len);
  usart_dma_transmitter_enable(PRINT_UART, TRUE);
  uart_dma_running = 1;
  dma_channel_enable(DMA1_CHANNEL1, TRUE);
  return 0;
#else
  (void)data;
  (void)len;
  return -1;
#endif
}

void bsp_init(void)
{
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);
  dwt_init();
  led_init();
  int_pin_init();
  uart_init();
#if !APP_CAN_ENABLE
  can_pins_low_init();
#endif
}
