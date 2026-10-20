/**
 * Board-level: LED, USART4 DMA TX, DWT delay, 1 ms tick, INT pins.
 */

#include "bsp.h"
#include "protocol.h"
#include "eskf8k_hotspot.h"
#include "uart_rx_dma.h"
#include "sram_capture.h"
#include "uart_tx_diag.h"
#include <string.h>

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
#if APP_SRAM_CAPTURE
static uint16_t uart_dma_active_bytes;
#endif
#define UART_CTRL_SLOTS 8U
#if !APP_UART_TX_BATCH
static uint8_t uart_ctrl_data[UART_CTRL_SLOTS][AHRS_MAX_FRAME_LEN];
static uint16_t uart_ctrl_len[UART_CTRL_SLOTS];
#endif
static uint8_t uart_ctrl_head, uart_ctrl_tail, uart_ctrl_count, uart_ctrl_active;
#if APP_UART_TX_PAD_TEST
static uint8_t uart_tx_pad_to;
int uart_tx_padding_set(unsigned bytes)
{
  if(bytes!=0U && bytes!=64U)return -1;
  uart_tx_pad_to=(uint8_t)bytes;return 0;
}
static uint16_t uart_padding_copy(uint8_t *dest,const uint8_t *data,uint16_t len)
{
  memcpy(dest,data,len);
  if(len<uart_tx_pad_to) {
    memset(dest+len,0,uart_tx_pad_to-len);return uart_tx_pad_to;
  }
  return len;
}
#endif
#if APP_UART_TX_QUEUE_FIX
#if APP_UART_TX_BATCH
/* Combined byte-bounded FIFO: one immutable DMA block, one filling block.
 * This replaces the old8 control+2 pose payload arrays, saving SRAM. */
static uint8_t uart_batch[2][256];
static uint16_t uart_batch_len[2];
static uint8_t uart_batch_state[2],uart_batch_poses[2],uart_batch_controls[2];
static uint32_t uart_batch_start[2],uart_batch_last_seq[2];
static uint8_t uart_batch_fill,uart_batch_send,uart_batch_active=255U;
static uint8_t uart_pose_count,uart_pose_high,uart_tx_owner;
#else
/* Same two 71-byte payload slots formerly held by vofa_dma in main.
 * Enqueue copies; a slot remains owned until DMA TC, including active DMA. */
static uint8_t uart_pose_data[2][AHRS_MAX_FRAME_LEN];
static uint16_t uart_pose_len[2];
static uint32_t uart_pose_seq[2];
static uint8_t uart_pose_head,uart_pose_tail,uart_pose_count,uart_pose_high;
static uint8_t uart_tx_owner,uart_last_control;
#endif
#endif
#if APP_UART_TX_DIAG
volatile uint32_t uart_tx_diag[UT_WORDS] = {[UT_MAGIC]=0x31545855U};
uint32_t uart_tx_diag_snapshot[UT_WORDS];
static uint32_t uart_ctrl_enqueued_cy[UART_CTRL_SLOTS];
static void uart_diag_max(unsigned slot,uint32_t value) {
  if(value>uart_tx_diag[slot])uart_tx_diag[slot]=value;
}
static void uart_diag_queue(void) {
  uint32_t now=DWT->CYCCNT;
  uint32_t age=uart_ctrl_count ? now-uart_ctrl_enqueued_cy[uart_ctrl_tail] : 0U;
  uart_tx_diag[UT_CTRL_COUNT]=uart_ctrl_count;
  uart_tx_diag[UT_CTRL_OLDEST_CY]=age;
  uart_diag_max(UT_CTRL_MAX_AGE_CY,age);
  if(uart_ctrl_count==UART_CTRL_SLOTS)
    uart_diag_max(UT_CTRL_FULL_MAX_CY,now-uart_tx_diag[UT_CTRL_FULL_START_CY]);
}
static void uart_diag_wire(void) {
  if((uart_tx_diag[UT_FLAGS]&1U) && !uart_dma_running &&
#if APP_COMM_POLL_FAST
     (PRINT_UART->sts & USART_TDC_FLAG)!=0U) {
#else
     usart_flag_get(PRINT_UART,USART_TDC_FLAG)!=RESET) {
#endif
    uint32_t now=DWT->CYCCNT;
    uart_tx_diag[UT_WIRE_IDLE]++;
    uart_tx_diag[UT_WIRE_LAST_CY]=now;
    uart_tx_diag[UT_WIRE_LAST_SEQ]=uart_tx_diag[UT_DMA_TC_SEQ];
    uart_diag_max(UT_WIRE_MAX_CY,now-uart_tx_diag[UT_START_CY]);
    uart_tx_diag[UT_FLAGS]&=~1U;
  }
}
void uart_tx_diag_pack(uint16_t len,uint32_t seq,uint32_t us,uint8_t format,uint16_t mask) {
  uart_tx_diag[len?UT_PACK_OK:UT_PACK_FAIL]++;
  uart_tx_diag[UT_PACK_BYTES]+=len;
  uart_tx_diag[UT_POSE_SEQ]=seq;uart_tx_diag[UT_POSE_US]=us;
  uart_tx_diag[UT_LAST_PACK_LEN]=len;
  uart_tx_diag[UT_FORMAT_MASK]=format|((uint32_t)mask<<8);
}
uint32_t uart_tx_diag_freeze(void) {
  /* Command dispatch and all writers share the foreground; the snapshot is
   * coherent without masking acquisition interrupts. Token detects new readers. */
  uart_diag_queue();uart_diag_wire();
#if APP_UART_TX_QUEUE_FIX
  /* Bit20 queue mode,21 byte batches. Count/high are4-bit fields; bit22
   * explicitly reports saturation for small legacy payloads. Full59B poses
   * fit at most8 across both blocks and do not saturate. */
#if APP_UART_TX_BATCH
  uart_tx_diag[UT_FLAGS]=(uart_tx_diag[UT_FLAGS]&~0x40ff00U)|0x300000U|
    ((uint32_t)(uart_pose_count<15U?uart_pose_count:15U)<<8)|
    ((uint32_t)(uart_pose_high<15U?uart_pose_high:15U)<<12)|
    ((uart_pose_count>15U || uart_pose_high>15U)?0x400000U:0U);
#else
  uart_tx_diag[UT_FLAGS]=(uart_tx_diag[UT_FLAGS]&~0x3300U)|0x100000U|
    ((uint32_t)uart_pose_count<<8)|((uint32_t)uart_pose_high<<12);
#endif
#endif
  uart_tx_diag[UT_TOKEN]++;
  for(unsigned i=0;i<UT_WORDS;++i)uart_tx_diag_snapshot[i]=uart_tx_diag[i];
  return uart_tx_diag[UT_TOKEN];
}
#endif

volatile uint32_t uart_rx_overruns, uart_rx_drops;
volatile uint32_t uart_rx_dma_overwrites, uart_rx_dma_errors;
#if APP_UART_RX_DMA
/* One DMA lap at 2 Mbps is 40.96 ms. Measured Flash stalls are 17–25 ms,
 * so one observe after the stall still sees an exact modular distance.
 * A gap of a full buffer or more is the documented blind spot. */
#define UART_DMA_RX_SIZE 8192U
static uint8_t uart_dma_rx[UART_DMA_RX_SIZE] __attribute__((aligned(4)));
static uint16_t uart_dma_read, uart_dma_last_pos;
static uint8_t uart_rx_err_irq_masked;
#else
#define UART_RX_RING_SIZE 1024U
static uint8_t uart_rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t uart_rx_read, uart_rx_write;
#endif
static uint32_t g_dwt_last;
static uint64_t g_cycles_hi;
#if APP_TIMEBASE_FAST
#include "timebase_fast.h"
static timebase_fast_t g_fast_clock;
#endif

static void dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  g_dwt_last = 0U;
  g_cycles_hi = 0U;
#if APP_TIMEBASE_FAST
  memset(&g_fast_clock,0,sizeof(g_fast_clock));
#endif
}

RAM_CODE uint32_t dwt_cycles(void)
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
  uint64_t h=hot_begin(H_COMMON);
#if APP_TIMEBASE_FAST
  uint32_t result=timebase_fast_ms(&g_fast_clock,dwt_cycles64(),system_core_clock/1000U);
#else
  uint32_t result=(uint32_t)(dwt_cycles64() / (uint64_t)(system_core_clock / 1000U));
#endif
  hot_end(H_COMMON,h);return result;
}
uint64_t dwt_time_us(void)
{
  uint64_t cycles = dwt_cycles64();
#if APP_TIMEBASE_FAST
  (void)timebase_fast_ms(&g_fast_clock, cycles, system_core_clock / 1000U);
#endif
  return cycles / (uint64_t)(system_core_clock / 1000000U);
}
uint32_t micros(void)
{
#if APP_TIMEBASE_FAST
  uint64_t cycles=dwt_cycles64();
  (void)timebase_fast_ms(&g_fast_clock,cycles,system_core_clock/1000U);
  return timebase_fast_us(&g_fast_clock,cycles,system_core_clock/1000000U);
#else
  return (uint32_t)dwt_time_us();
#endif
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
#if APP_UART_RX_DMA
  /* DMA1 CH1 is USART4 TX. CH2/CH3 are SPI1 RX/TX at very-high priority.
   * CH4 is free. DMAMUX request 0x1E is USART4_RX in at32f423_dma.h.
   * HIGH, not very-high, so a pending SPI beat stays ahead of UART.
   * SPI raises one request per shifted word, so UART still gets the bus
   * between words, inside one 5 us UART byte time. */
  dma_reset(DMA1_CHANNEL4);
  dmamux_init(DMA1MUX_CHANNEL4, DMAMUX_DMAREQ_ID_USART4_RX);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.peripheral_base_addr = (uint32_t)&PRINT_UART->dt;
  dma_init_struct.memory_base_addr = (uint32_t)uart_dma_rx;
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.buffer_size = UART_DMA_RX_SIZE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.loop_mode_enable = TRUE;
  dma_init_struct.priority = DMA_PRIORITY_HIGH;
  dma_init(DMA1_CHANNEL4, &dma_init_struct);
  dma_flag_clear(DMA1_DTERR4_FLAG);
  dma_channel_enable(DMA1_CHANNEL4, TRUE);
  uart_dma_read = 0U;
  uart_dma_last_pos = 0U;
#endif
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
  nvic_irq_enable(USART4_IRQn, 2, 0);
#if APP_UART_RX_DMA
  /* Drop a byte that arrived before the circular buffer existed. */
  (void)PRINT_UART->sts;
  (void)usart_data_receive(PRINT_UART);
#endif
  uart_dma_init();
#if APP_UART_RX_DMA
  usart_dma_receiver_enable(PRINT_UART, TRUE);
  usart_interrupt_enable(PRINT_UART, USART_ERR_INT, TRUE);
  /* No software ring in this build. Keep the counter so a report can show 0. */
  uart_rx_drops = 0U;
#else
  usart_interrupt_enable(PRINT_UART, USART_RDBF_INT, TRUE);
#endif
#else
  uart_pins_low_init();
#endif
}

int uart_dma_busy(void)
{
#if APP_UART_ENABLE
  if(uart_dma_running)
  {
#if APP_COMM_POLL_FAST
    uint32_t tx_flags=DMA1->sts; /* One read; inspect only channel1 bits. */
#endif
#if APP_UART_TX_DIAG
#if APP_COMM_POLL_FAST
    if((tx_flags&DMA1_DTERR1_FLAG) && !(uart_tx_diag[UT_FLAGS]&2U)) {
#else
    if(dma_flag_get(DMA1_DTERR1_FLAG)!=RESET && !(uart_tx_diag[UT_FLAGS]&2U)) {
#endif
      uart_tx_diag[UT_DMA_ERROR]++;uart_tx_diag[UT_FLAGS]|=2U;
    }
#endif
#if APP_COMM_POLL_FAST
    if(tx_flags&DMA1_FDT1_FLAG)
#else
    if(dma_flag_get(DMA1_FDT1_FLAG) != RESET)
#endif
    {
#if APP_UART_TX_DIAG
      uart_tx_diag[UT_DMA_TC]++;
      uart_tx_diag[UT_DMA_BYTES]+=uart_tx_diag[UT_ACTIVE_LEN];
#if APP_UART_TX_BATCH
      uart_tx_diag[UT_TELEMETRY_DMA_TC]+=uart_batch_poses[uart_batch_active];
      uart_tx_diag[UT_CONTROL_DMA_TC]+=uart_batch_controls[uart_batch_active];
      if(uart_batch_poses[uart_batch_active])
#else
      uart_tx_diag[uart_tx_diag[UT_ACTIVE_KIND]==1U?UT_TELEMETRY_DMA_TC:UT_CONTROL_DMA_TC]++;
      if(uart_tx_diag[UT_ACTIVE_KIND]==1U)
#endif
        uart_tx_diag[UT_DMA_TC_SEQ]=uart_tx_diag[UT_DMA_LAST_SEQ];
      uart_diag_max(UT_DMA_MAX_CY,DWT->CYCCNT-uart_tx_diag[UT_START_CY]);
#endif
#if APP_SRAM_CAPTURE
      /* DMA completion means delivery to USART DT, not an on-wire timestamp.
       * Polling can observe the flag late while foreground Flash work runs. */
      sram_live[SR_TX_DMA_FRAMES]++;
      sram_live[SR_TX_DMA_BYTES]+=uart_dma_active_bytes;
#endif
#if APP_COMM_POLL_FAST
      DMA1->clr=DMA1_FDT1_FLAG; /* W1C never clears SPI/RX flags. */
      DMA1_CHANNEL1->ctrl_bit.chen=0U;
#else
      dma_flag_clear(DMA1_FDT1_FLAG);
      dma_channel_enable(DMA1_CHANNEL1, FALSE);
#endif
      uart_dma_running = 0;
    }
  }
#if APP_UART_TX_DIAG
  if(uart_tx_diag[UT_FLAGS]&1U)uart_diag_wire();
#endif
  return (int)uart_dma_running;
#else
  return 0;
#endif
}

static int uart_dma_send_raw(const uint8_t *data, uint16_t len);
#if APP_UART_TX_BATCH
static void uart_batch_seal(void)
{
  uart_batch_state[uart_batch_fill]=2U;
  uart_batch_fill^=1U;
}
static int uart_batch_enqueue(const uint8_t *data,uint16_t len,int control)
{
  uart_tx_task();
  uint8_t b=uart_batch_fill;
  if(uart_batch_state[b]>=2U)return -1;
  if(uart_batch_len[b]+len>sizeof(uart_batch[0])) {
    uart_batch_seal();uart_tx_task();b=uart_batch_fill;
    if(uart_batch_state[b]>=2U)return -1;
  }
  if(!uart_batch_len[b]) {uart_batch_start[b]=DWT->CYCCNT;uart_batch_state[b]=1U;}
  memcpy(uart_batch[b]+uart_batch_len[b],data,len);uart_batch_len[b]+=len;
  if(control)uart_batch_controls[b]++;
  else {
    uart_batch_poses[b]++;uart_batch_last_seq[b]=uart_tx_diag[UT_POSE_SEQ];
    uart_pose_count++;if(uart_pose_count>uart_pose_high)uart_pose_high=uart_pose_count;
  }
  return 0;
}
#endif

/* All callers execute in the main context; DMA only reads the active slot. */
int uart_control_enqueue(const uint8_t *data, uint16_t len)
{
#if APP_UART_ENABLE
#if APP_UART_TX_BATCH
  if(data==NULL || !len || len>AHRS_MAX_FRAME_LEN || uart_ctrl_count==UART_CTRL_SLOTS ||
     uart_batch_enqueue(data,len,1)) {uart_tx_diag[UT_CTRL_REJECT]++;return -1;}
  uart_ctrl_enqueued_cy[uart_ctrl_head]=DWT->CYCCNT;
  uart_ctrl_head=(uint8_t)((uart_ctrl_head+1U)%UART_CTRL_SLOTS);uart_ctrl_count++;
  uart_tx_diag[UT_CTRL_ENQUEUE]++;uart_diag_max(UT_CTRL_HIGH,uart_ctrl_count);
  if(uart_ctrl_count==UART_CTRL_SLOTS)uart_tx_diag[UT_CTRL_FULL_START_CY]=DWT->CYCCNT;
  uart_diag_queue();return 0;
#else
  if(data == NULL || len == 0U || len > AHRS_MAX_FRAME_LEN ||
     uart_ctrl_count == UART_CTRL_SLOTS) {
#if APP_UART_TX_DIAG
    uart_tx_diag[UT_CTRL_REJECT]++;
#endif
    return -1;
  }
#if APP_UART_TX_DIAG
  uart_ctrl_enqueued_cy[uart_ctrl_head]=DWT->CYCCNT;
  uart_tx_diag[UT_CTRL_ENQUEUE]++;
#endif
#if APP_UART_TX_PAD_TEST
  uart_ctrl_len[uart_ctrl_head]=uart_padding_copy(uart_ctrl_data[uart_ctrl_head],data,len);
#else
  memcpy(uart_ctrl_data[uart_ctrl_head], data, len);
  uart_ctrl_len[uart_ctrl_head] = len;
#endif
  uart_ctrl_head = (uint8_t)((uart_ctrl_head + 1U) % UART_CTRL_SLOTS);
  uart_ctrl_count++;
#if APP_UART_TX_DIAG
  uart_diag_max(UT_CTRL_HIGH,uart_ctrl_count);
  if(uart_ctrl_count==UART_CTRL_SLOTS)uart_tx_diag[UT_CTRL_FULL_START_CY]=DWT->CYCCNT;
  uart_diag_queue();
#endif
  return 0;
#endif
#else
  (void)data; (void)len;
  return -1;
#endif
}

void uart_tx_task(void)
{
#if APP_UART_ENABLE
#if APP_UART_TX_BATCH
  if(uart_ctrl_count)uart_diag_queue();
  if(uart_batch_active!=255U && !uart_dma_busy()) {
    uint8_t b=uart_batch_active;
    uart_pose_count-=uart_batch_poses[b];
    uart_ctrl_count-=uart_batch_controls[b];
    uart_ctrl_tail=(uint8_t)((uart_ctrl_tail+uart_batch_controls[b])%UART_CTRL_SLOTS);
    uart_batch_len[b]=0U;uart_batch_poses[b]=uart_batch_controls[b]=0U;
    uart_batch_state[b]=0U;uart_batch_active=255U;uart_tx_owner=0U;
  }
  if(uart_batch_state[uart_batch_fill]==1U &&
     DWT->CYCCNT-uart_batch_start[uart_batch_fill]>=system_core_clock/250U)
    uart_batch_seal(); /* At most4ms fill wait, independent of packet count. */
  if(uart_batch_active==255U && uart_batch_state[uart_batch_send]==2U) {
    uint8_t b=uart_batch_send;uart_batch_active=b;
    uart_tx_owner=uart_batch_poses[b] ? (uart_batch_controls[b]?3U:1U) : 2U;
    if(!uart_dma_send_raw(uart_batch[b],uart_batch_len[b])) {
      uart_batch_state[b]=3U;uart_batch_send^=1U;
    } else {uart_batch_active=255U;uart_tx_owner=0U;}
  }
#elif APP_UART_TX_QUEUE_FIX
  if(!uart_tx_owner && !uart_ctrl_count && !uart_pose_count)return;
  if(uart_ctrl_count)uart_diag_queue();
  if(uart_dma_busy())return;
  if(uart_tx_owner==2U) {
    uart_ctrl_tail=(uint8_t)((uart_ctrl_tail+1U)%UART_CTRL_SLOTS);
    uart_ctrl_count--;uart_ctrl_active=0U;
  } else if(uart_tx_owner==1U) {
    uart_pose_tail^=1U;uart_pose_count--;
  }
  uart_tx_owner=0U;
  /* At most one control frame before a waiting pose, and vice versa.
   * Neither queue can starve; no blocking wait or ISR packing. */
  if(uart_ctrl_count && (!uart_pose_count || !uart_last_control)) {
    uart_tx_owner=2U;
    if(uart_dma_send_raw(uart_ctrl_data[uart_ctrl_tail],uart_ctrl_len[uart_ctrl_tail])==0) {
      uart_ctrl_active=1U;uart_last_control=1U;
    } else uart_tx_owner=0U;
  } else if(uart_pose_count) {
    uart_tx_owner=1U;
    if(uart_dma_send_raw(uart_pose_data[uart_pose_tail],uart_pose_len[uart_pose_tail])==0)
      uart_last_control=0U;
    else uart_tx_owner=0U;
  }
#else
#if APP_UART_TX_DIAG
  if(uart_ctrl_count)uart_diag_queue();
#endif
  if(uart_dma_busy()) return;
  if(uart_ctrl_active)
  {
    uart_ctrl_tail = (uint8_t)((uart_ctrl_tail + 1U) % UART_CTRL_SLOTS);
    uart_ctrl_count--;
    uart_ctrl_active = 0U;
  }
  if(uart_ctrl_count != 0U)
  {
    /* Send via the raw DMA path below; keep this slot until completion. */
    if(uart_dma_send_raw(uart_ctrl_data[uart_ctrl_tail],
                     uart_ctrl_len[uart_ctrl_tail]) == 0)
      uart_ctrl_active = 1U;
  }
#endif
#endif
}

int uart_tx_idle(void)
{
#if APP_UART_ENABLE
  uart_tx_task();
  return (uart_ctrl_count == 0U &&
#if APP_UART_TX_QUEUE_FIX
          uart_pose_count==0U &&
#endif
          !uart_dma_busy() &&
          usart_flag_get(PRINT_UART, USART_TDC_FLAG) != RESET);
#else
  return 1;
#endif
}

static int uart_dma_send_raw(const uint8_t *data, uint16_t len)
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
#if APP_UART_TX_DIAG
  if(uart_tx_diag[UT_FLAGS]&1U)uart_tx_diag[UT_WIRE_CHAINED]++;
  else uart_tx_diag[UT_WIRE_BURST_BYTES]=0U;
  uart_tx_diag[UT_WIRE_BURST_BYTES]+=len;
  uart_tx_diag[UT_FLAGS]=(uart_tx_diag[UT_FLAGS]&~2U)|1U;
#if APP_UART_TX_QUEUE_FIX
  uart_tx_diag[UT_ACTIVE_KIND]=uart_tx_owner;
#else
  uart_tx_diag[UT_ACTIVE_KIND]=uart_ctrl_count?2U:1U;
#endif
  uart_tx_diag[UT_ACTIVE_LEN]=len;
  uart_tx_diag[UT_START_CY]=DWT->CYCCNT;
  uart_tx_diag[UT_DMA_START]++;
#if APP_UART_TX_QUEUE_FIX
#if APP_UART_TX_BATCH
  if(uart_batch_poses[uart_batch_active])uart_tx_diag[UT_DMA_LAST_SEQ]=uart_batch_last_seq[uart_batch_active];
#else
  if(uart_tx_owner==1U)uart_tx_diag[UT_DMA_LAST_SEQ]=uart_pose_seq[uart_pose_tail];
#endif
#else
  if(!uart_ctrl_count)uart_tx_diag[UT_DMA_LAST_SEQ]=uart_tx_diag[UT_POSE_SEQ];
#endif
#endif

  dma_channel_enable(DMA1_CHANNEL1, FALSE);
  dma_flag_clear(DMA1_FDT1_FLAG);
  dma_flag_clear(DMA1_DTERR1_FLAG);
  DMA1_CHANNEL1->maddr = (uint32_t)data;
  dma_data_number_set(DMA1_CHANNEL1, len);
  usart_dma_transmitter_enable(PRINT_UART, TRUE);
  uart_dma_running = 1;
#if APP_SRAM_CAPTURE
  uart_dma_active_bytes=len;
#endif
  dma_channel_enable(DMA1_CHANNEL1, TRUE);
  return 0;
#else
  (void)data;
  (void)len;
  return -1;
#endif
}

int uart_dma_send(const uint8_t *data, uint16_t len)
{
#if APP_UART_TX_BATCH
  uart_tx_diag[UT_OFFER]++;
  if(data==NULL || !len || len>AHRS_MAX_FRAME_LEN || uart_batch_enqueue(data,len,0)) {
    uart_tx_diag[UT_BUSY_REJECT]++;return -1;
  }
  uart_tx_diag[UT_ACCEPT]++;return 0;
#elif APP_UART_TX_QUEUE_FIX
  uart_tx_diag[UT_OFFER]++;
  uart_tx_task(); /* Retire a completed owner before checking free slots. */
  if(data==NULL || len==0U || len>AHRS_MAX_FRAME_LEN || uart_pose_count==2U) {
    uart_tx_diag[UT_BUSY_REJECT]++;return -1;
  }
#if APP_UART_TX_PAD_TEST
  uart_pose_len[uart_pose_head]=uart_padding_copy(uart_pose_data[uart_pose_head],data,len);
#else
  memcpy(uart_pose_data[uart_pose_head],data,len);
  uart_pose_len[uart_pose_head]=len;
#endif
  uart_pose_seq[uart_pose_head]=uart_tx_diag[UT_POSE_SEQ];
  uart_pose_head^=1U;uart_pose_count++;
  if(uart_pose_count>uart_pose_high)uart_pose_high=uart_pose_count;
  uart_tx_diag[UT_ACCEPT]++;
  uart_tx_task();return 0;
#else
#if APP_UART_TX_DIAG
  uart_tx_diag[UT_OFFER]++;
#endif
  /* Prevent high-rate telemetry from starving a queued control reply. */
  if(uart_ctrl_count != 0U) {
#if APP_UART_TX_DIAG
    uart_tx_diag[UT_CONTROL_YIELD]++;
#endif
    return -1;
  }
  int rc=uart_dma_send_raw(data, len);
#if APP_UART_TX_DIAG
  uart_tx_diag[rc?UT_BUSY_REJECT:UT_ACCEPT]++;
#endif
  return rc;
#endif
}

void uart_rx_isr(void)
{
#if APP_UART_ENABLE && APP_UART_RX_DMA
  /* Per-byte RDBF is off. ERR covers the hardware overrun flag, which stays
   * set until STS and DT are read. DT is consumed here only while that flag
   * is set; a stuck flag disables the IRQ so the main loop cannot livelock. */
  uint32_t sts = PRINT_UART->sts;
  if((sts & (USART_ROERR_FLAG | USART_FERR_FLAG | USART_NERR_FLAG)) == 0U) return;
  if((sts & USART_ROERR_FLAG) != 0U) uart_rx_overruns++;
  (void)usart_data_receive(PRINT_UART);
  if((PRINT_UART->sts & (USART_ROERR_FLAG | USART_FERR_FLAG | USART_NERR_FLAG)) != 0U) {
    usart_interrupt_enable(PRINT_UART, USART_ERR_INT, FALSE);
    uart_rx_err_irq_masked = 1U;
    uart_rx_dma_errors++;
  }
#elif APP_UART_ENABLE
  uint8_t val;
  uint16_t next;
  /* ROERR is cleared only by reading STS then DT, and it keeps the RDBF
     interrupt asserted. Sample STS once and always read DT when either flag
     is set; otherwise a ROERR-without-RDBF state re-fires forever and RX dies. */
  uint32_t sts = PRINT_UART->sts;
  if((sts & (USART_RDBF_FLAG | USART_ROERR_FLAG)) == 0U) return;
  val = (uint8_t)usart_data_receive(PRINT_UART);
  if((sts & USART_ROERR_FLAG) != 0U)
    uart_rx_overruns++;
  /* On overrun DT still holds the last valid byte; keep it if RDBF was set. */
  if((sts & USART_RDBF_FLAG) == 0U) return;
  next = (uint16_t)((uart_rx_write + 1U) & (UART_RX_RING_SIZE - 1U));
  if(next == uart_rx_read)
  {
    uart_rx_drops++;
    return;
  }
  uart_rx_ring[uart_rx_write] = val;
  __DMB();
  uart_rx_write = next;
#endif
}

int uart_read_byte(uint8_t *ch)
{
#if APP_UART_ENABLE && APP_UART_RX_DMA
  uint16_t ndtr;
  uint16_t pos;
  uart_rx_dma_obs_t obs;
  if((DMA1_CHANNEL4->ctrl & 1U) == 0U || dma_flag_get(DMA1_DTERR4_FLAG) != RESET) {
    dma_flag_clear(DMA1_DTERR4_FLAG);
    dma_channel_enable(DMA1_CHANNEL4, FALSE);
    DMA1_CHANNEL4->maddr = (uint32_t)uart_dma_rx;
    dma_data_number_set(DMA1_CHANNEL4, UART_DMA_RX_SIZE);
    dma_channel_enable(DMA1_CHANNEL4, TRUE);
    uart_dma_read = 0U;
    uart_dma_last_pos = 0U;
    uart_rx_dma_errors++;
    uart_rx_dma_overwrites++;
  }
  if(uart_rx_err_irq_masked && (PRINT_UART->sts & USART_ROERR_FLAG) != 0U) {
    uart_rx_overruns++;
    (void)PRINT_UART->sts;
    (void)usart_data_receive(PRINT_UART);
  }
  ndtr = dma_data_number_get(DMA1_CHANNEL4);
  __DMB();
  pos = uart_rx_dma_pos(UART_DMA_RX_SIZE, ndtr);
  obs = uart_rx_dma_observe(UART_DMA_RX_SIZE, uart_dma_read, uart_dma_last_pos, pos);
  uart_rx_dma_overwrites += obs.overwrites;
  uart_dma_read = obs.read;
  uart_dma_last_pos = obs.last_pos;
  if(uart_dma_read == pos) return 0;
  if(ch != NULL) *ch = uart_dma_rx[uart_dma_read];
  uart_dma_read = (uint16_t)((uart_dma_read + 1U) & (UART_DMA_RX_SIZE - 1U));
  return 1;
#elif APP_UART_ENABLE
  uint16_t read = uart_rx_read;
  if(read == uart_rx_write) return 0;
  if(ch != NULL) *ch = uart_rx_ring[read];
  uart_rx_read = (uint16_t)((read + 1U) & (UART_RX_RING_SIZE - 1U));
  return 1;
#else
  (void)ch;
  return 0;
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
