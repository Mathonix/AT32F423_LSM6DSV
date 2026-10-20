#include "usb_cdc.h"
#include "usb_diag.h"
#include "usb_tx_control_guard.h"
#include "usb_conf.h"
#include "usb_core.h"
#include "usbd_int.h"
#include "usbd_core.h"
#include "cdc_class.h"
#include "cdc_desc.h"
#include "at32f423_conf.h"
#include "at32f423_crm.h"
#include "at32f423_acc.h"
#include "at32f423_misc.h"
#include <string.h>

/* Official AT32F423 USB device CDC wrapper.
 * USB_VBUS_IGNORE and the CDC control/data state machine are provided by the
 * Artery middleware; this file only adapts it to the existing application API.
 */
static otg_core_type otg_core;
#if APP_USB_BUFFER_FIX
/* Measured normal-host IN gaps reached 43.4 ms; 2048-256 reserve holds
 * only ~30 ms of 59 B frames. 4096 holds ~65 ms. Still bounded/nonblocking. */
#define USB_TX_RING_SIZE 4096U
#else
#define USB_TX_RING_SIZE 2048U
#endif
#define USB_RX_RING_SIZE 512U
/* Keep room for replies even when an unread telemetry endpoint fills up. */
#define USB_TX_CONTROL_RESERVE 256U
static uint8_t tx_ring[USB_TX_RING_SIZE];
static volatile uint16_t tx_r, tx_w;
#if !APP_USB_DIAG
static uint8_t rx_packet[USBD_CDC_OUT_MAXPACKET_SIZE];
#endif
static uint8_t rx_ring[USB_RX_RING_SIZE];
static volatile uint16_t rx_r, rx_w;
static uint8_t usb_tx_packet[USBD_CDC_IN_MAXPACKET_SIZE];
static usb_tx_control_guard_t cdc_control_tx;

/* Interface 2 (WebUSB vendor bulk 0x83/0x03): independent rings so a CDC
 * host and a WebUSB host never block or corrupt each other's stream. */
#define WEBUSB_TX_RING_SIZE 2048U
#define WEBUSB_RX_RING_SIZE 512U
static uint8_t wu_tx_ring[WEBUSB_TX_RING_SIZE];
static volatile uint16_t wu_tx_r, wu_tx_w;
static uint8_t wu_rx_packet[USBD_WEBUSB_MAXPACKET_SIZE];
static uint8_t wu_rx_ring[WEBUSB_RX_RING_SIZE];
static volatile uint16_t wu_rx_r, wu_rx_w;
static uint8_t wu_tx_packet[USBD_WEBUSB_MAXPACKET_SIZE];
static usb_tx_control_guard_t webusb_control_tx;

#if APP_USB_DIAG
#include "bsp.h"
volatile uint32_t usb_diag[UD_WORDS];
_Static_assert(sizeof(usb_diag)==128U,"USB diagnostic ABI");
void usb_diag_event(unsigned event)
{
  if(event<UD_WORDS) usb_diag[event]++;
  if(event==UD_CLEAR||event==UD_RESET) {
    usb_diag[UD_PURGED_BYTES]+=usb_diag[UD_INFLIGHT];
    usb_diag[UD_INFLIGHT]=0U;
  }
}
void usb_diag_complete(void)
{
  /* micros/millis share foreground-only 64-bit epoch state. An ISR must never
   * call them: preemption can make an older foreground CYCCNT look like wrap.
   * Unsigned raw CYCCNT subtraction is safe across one wrap (28.6 s at 150M).
   * Longer endpoint stalls are measured by foreground backpressure counters. */
  uint32_t elapsed=(dwt_cycles()-usb_diag[UD_SUBMIT_CY])/(system_core_clock/1000000U);
  usb_diag[UD_COMPLETE]++;
  usb_diag[UD_COMPLETE_BYTES]+=usb_diag[UD_INFLIGHT];
  usb_diag[UD_INFLIGHT]=0U;
  if(elapsed>usb_diag[UD_BUSY_MAX_US])usb_diag[UD_BUSY_MAX_US]=elapsed;
}
static void diag_unblocked(void)
{
  if(usb_diag[UD_FULL_ACTIVE]) {
    uint32_t elapsed=micros()-usb_diag[UD_FULL_SINCE];
    usb_diag[UD_FULL_US]+=elapsed;
    if(elapsed>usb_diag[UD_FULL_MAX_US])usb_diag[UD_FULL_MAX_US]=elapsed;
    usb_diag[UD_FULL_ACTIVE]=0U;
  }
}
#endif

static uint16_t ring_used(volatile uint16_t r, volatile uint16_t w, uint16_t size)
{
  return (uint16_t)((w - r) & (size - 1U));
}

static void usb_gpio_config_local(void)
{
  gpio_init_type g;
  crm_periph_clock_enable(OTG_PIN_GPIO_CLOCK, TRUE);
  gpio_default_para_init(&g);
  g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  g.gpio_mode = GPIO_MODE_MUX;
  g.gpio_pull = GPIO_PULL_NONE;
  g.gpio_pins = OTG_PIN_DP | OTG_PIN_DM;
  gpio_init(OTG_PIN_GPIO, &g);
  gpio_pin_mux_config(OTG_PIN_GPIO, OTG_PIN_DP_SOURCE, OTG_PIN_MUX);
  gpio_pin_mux_config(OTG_PIN_GPIO, OTG_PIN_DM_SOURCE, OTG_PIN_MUX);
  /* VBUS is intentionally not configured: USB_VBUS_IGNORE is enabled. */
}

static void usb_clock48m_select_local(void)
{
  crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);
  crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK, TRUE);
  acc_write_c1(7980U);
  acc_write_c2(8000U);
  acc_write_c3(8020U);
  acc_calibration_mode_enable(ACC_CAL_HICKTRIM, TRUE);
}

void usb_cdc_init(void)
{
#if APP_USB_DIAG
  memset((void *)usb_diag,0,sizeof(usb_diag));
  usb_diag[UD_MAGIC]=0x32424455U; /* ABI v2: word 18 now raw submit cycles */
#endif
  /* Clear application queues before the USB core starts touching the CDC
   * buffers.  This also makes a second init/re-enumeration deterministic. */
  tx_r = tx_w = rx_r = rx_w = 0U;
  wu_tx_r = wu_tx_w = wu_rx_r = wu_rx_w = 0U;
  memset(&cdc_control_tx,0,sizeof(cdc_control_tx));
  memset(&webusb_control_tx,0,sizeof(webusb_control_tx));
  /* The bootloader also initializes OTGFS1 before jumping to the app.  A
   * peripheral reset here is required for a clean USB disconnect/reconnect;
   * otherwise the app may inherit stale device/endpoint state and Windows
   * will never complete enumeration after the bootloader fast-jump. */
  crm_periph_clock_enable(OTG_CLOCK, TRUE);
  crm_periph_reset(CRM_OTGFS1_PERIPH_RESET, TRUE);
  crm_periph_reset(CRM_OTGFS1_PERIPH_RESET, FALSE);
  usb_gpio_config_local();
  usb_clock48m_select_local();
  /* The official example uses group 4.  Set it here rather than relying on
   * the reset value: the application has other interrupt sources and the USB
   * IRQ must remain preemptible while the main loop services CDC buffers. */
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);
  nvic_irq_enable(OTG_IRQ, 3, 0);
  (void)usbd_init(&otg_core, USB_FULL_SPEED_CORE_ID, USB_ID,
                  &cdc_class_handler, &cdc_desc_handler);
}

void usb_cdc_isr(void)
{
  usbd_irq_handler(&otg_core);
}

int usb_cdc_tx_idle(void)
{
  cdc_struct_type *pcdc;
  if(!usb_cdc_configured()) return 1;
  pcdc = (cdc_struct_type *)otg_core.dev.class_handler->pdata;
  return tx_r == tx_w && pcdc != NULL && pcdc->g_tx_completed != 0U;
}

int usb_cdc_configured(void)
{
  return (otg_core.dev.conn_state == USB_CONN_STATE_CONFIGURED) ? 1 : 0;
}

int usb_cdc_control_tx_idle(void)
{
  if(!usb_cdc_configured()) return 1;
  cdc_struct_type *pcdc=(cdc_struct_type *)otg_core.dev.class_handler->pdata;
  return pcdc!=NULL&&usb_tx_control_idle(&cdc_control_tx,pcdc->g_tx_completed!=0U);
}

#if APP_USB_TX_CHAIN
/* One consumer, serialized between main and this USB IRQ. Producers publish
 * only after complete copies. No waits for a host, protocol packing or FPU. */
static void cdc_tx_pump(void)
{
  if(otg_core.dev.conn_state!=USB_CONN_STATE_CONFIGURED)return;
  uint16_t used=ring_used(tx_r,tx_w,USB_TX_RING_SIZE);
  if(!used)return;
  cdc_struct_type *pcdc=(cdc_struct_type *)otg_core.dev.class_handler->pdata;
  if(!pcdc->g_tx_completed) {usb_diag[UD_BUSY_COPY]++;return;}
  uint16_t n=used>USBD_CDC_IN_MAXPACKET_SIZE?USBD_CDC_IN_MAXPACKET_SIZE:used;
  for(uint16_t i=0;i<n;++i)usb_tx_packet[i]=tx_ring[(tx_r+i)&(USB_TX_RING_SIZE-1U)];
  if(usb_vcp_send_data(&otg_core.dev,usb_tx_packet,n)==SUCCESS) {
    usb_tx_control_packet(&cdc_control_tx,tx_r,n);
    tx_r=(uint16_t)(tx_r+n);
    usb_diag[UD_SUBMIT]++;usb_diag[UD_SUBMIT_BYTES]+=n;
    usb_diag[UD_INFLIGHT]=n;usb_diag[UD_SUBMIT_CY]=dwt_cycles();
    usb_diag[UD_QUEUE_USED]=ring_used(tx_r,tx_w,USB_TX_RING_SIZE);
  } else usb_diag[UD_BUSY_COPY]++;
}
void usb_cdc_tx_complete(void) { cdc_tx_pump(); }
int usb_cdc_reset_peaks(void)
{
  uint32_t enabled=NVIC_GetEnableIRQ(OTG_IRQ);
  NVIC_DisableIRQ(OTG_IRQ);__DSB();__ISB();
  int result=-1;
  if(otg_core.dev.conn_state==USB_CONN_STATE_CONFIGURED&&!usb_diag[UD_FULL_ACTIVE]) {
    usb_diag[UD_QUEUE_HIGH]=ring_used(tx_r,tx_w,USB_TX_RING_SIZE);
    usb_diag[UD_FULL_MAX_US]=0U;usb_diag[UD_BUSY_MAX_US]=0U;
    usb_diag[UD_TASK_MAX_GAP_US]=0U;
    result=0;
  }
  if(enabled)NVIC_EnableIRQ(OTG_IRQ);
  return result;
}
#endif

void usb_cdc_task(void)
{
  uint16_t n, i;
#if APP_USB_DIAG
  uint32_t now=micros(),last=usb_diag[UD_TASK_LAST_US];
  if(last && now-last>usb_diag[UD_TASK_MAX_GAP_US])usb_diag[UD_TASK_MAX_GAP_US]=now-last;
  usb_diag[UD_TASK_LAST_US]=now;
#endif

  webusb_task();

  /* Do not call the class data helpers before SET_CONFIGURATION.  The
   * middleware initializes g_tx_completed/g_rx_completed from the class init
   * callback, so using them earlier can otherwise consume stale endpoint
   * state after unplug/replug.  Drop queued application data while detached
   * instead of replaying an old telemetry burst on the next connection. */
  if(otg_core.dev.conn_state != USB_CONN_STATE_CONFIGURED)
  {
#if APP_USB_DIAG
    usb_diag[UD_PURGED_BYTES]+=ring_used(tx_r,tx_w,USB_TX_RING_SIZE);
    usb_diag[UD_QUEUE_USED]=0U;
    diag_unblocked();
#endif
    tx_r = tx_w;
    rx_r = rx_w;
    memset(&cdc_control_tx,0,sizeof(cdc_control_tx));
    return;
  }

#if APP_USB_TX_CHAIN
  uint32_t enabled=NVIC_GetEnableIRQ(OTG_IRQ);
  NVIC_DisableIRQ(OTG_IRQ);__DSB();__ISB();
  cdc_tx_pump();
  if(enabled)NVIC_EnableIRQ(OTG_IRQ);
#else
  uint16_t used = ring_used(tx_r, tx_w, USB_TX_RING_SIZE);
  if(used != 0U)
  {
#if APP_USB_TX_OWNERSHIP_FIX
    /* The class retains usb_tx_packet until its completion callback. Only
     * foreground submits CDC IN; the ISR can release, but never acquire it. */
    cdc_struct_type *tx_cdc=(cdc_struct_type *)otg_core.dev.class_handler->pdata;
    if(!tx_cdc->g_tx_completed) {
      usb_diag[UD_BUSY_COPY]++; /* busy reject before touching staging */
      goto cdc_receive;
    }
#endif
    n = (used > USBD_CDC_IN_MAXPACKET_SIZE) ? USBD_CDC_IN_MAXPACKET_SIZE : used;
    for(i = 0U; i < n; ++i)
      usb_tx_packet[i] = tx_ring[(tx_r + i) & (USB_TX_RING_SIZE - 1U)];
#if APP_USB_DIAG
    /* Only serialize endpoint ownership, leaving DRDY/DMA preemptible. */
    uint32_t usb_irq_enabled=NVIC_GetEnableIRQ(OTG_IRQ);
    NVIC_DisableIRQ(OTG_IRQ); __DSB(); __ISB();
#endif
    if(
#if APP_USB_TX_OWNERSHIP_FIX
       otg_core.dev.conn_state==USB_CONN_STATE_CONFIGURED &&
#endif
       usb_vcp_send_data(&otg_core.dev, usb_tx_packet, n) == SUCCESS) {
      usb_tx_control_packet(&cdc_control_tx,tx_r,n);
      tx_r = (uint16_t)(tx_r + n);
#if APP_USB_DIAG
      usb_diag[UD_SUBMIT]++;
      usb_diag[UD_SUBMIT_BYTES]+=n;
      usb_diag[UD_INFLIGHT]=n;
      usb_diag[UD_SUBMIT_CY]=dwt_cycles();
      usb_diag[UD_QUEUE_USED]=ring_used(tx_r,tx_w,USB_TX_RING_SIZE);
#endif
    }
#if APP_USB_DIAG
    else usb_diag[UD_BUSY_COPY]++; /* actual send helper rejected as busy */
#endif
#if APP_USB_DIAG
    if(usb_irq_enabled)NVIC_EnableIRQ(OTG_IRQ);
#endif
  }

#if APP_USB_TX_OWNERSHIP_FIX
cdc_receive:;
#endif
#endif /* APP_USB_TX_CHAIN */
  /* Official CDC keeps one OUT packet armed. Move it to the application ring
   * only from the main context, then usb_vcp_get_rxdata re-arms the endpoint. */
#if APP_USB_DIAG
  /* Read class storage before rearming OUT. It cannot be reused by USB until
   * usbd_ept_recv below. Saves a redundant 64 B staging buffer for counters. */
  cdc_struct_type *pcdc=(cdc_struct_type *)otg_core.dev.class_handler->pdata;
  n=pcdc->g_rx_completed?pcdc->g_rxlen:0U;
  const uint8_t *rx_packet=pcdc->g_rx_buff;
#else
  n = usb_vcp_get_rxdata(&otg_core.dev, rx_packet);
#endif
#if APP_USB_DIAG
  if(pcdc->g_rx_completed) /* also rearm a valid zero-length OUT packet */
#else
  if(n != 0U)
#endif
  {
    for(i = 0U; i < n; ++i)
    {
      uint16_t next = (uint16_t)((rx_w + 1U) & (USB_RX_RING_SIZE - 1U));
      if(next == rx_r) {
#if APP_USB_DIAG
        usb_diag[UD_RX_DROP]+=n-i;
#endif
        break;
      }
      rx_ring[rx_w] = rx_packet[i];
      rx_w = next;
    }
#if APP_USB_DIAG
    pcdc->g_rx_completed=0U;
    usbd_ept_recv(&otg_core.dev,USBD_CDC_BULK_OUT_EPT,pcdc->g_rx_buff,USBD_CDC_OUT_MAXPACKET_SIZE);
#endif
  }
}

int usb_cdc_available(void)
{
  return (int)ring_used(rx_r, rx_w, USB_RX_RING_SIZE);
}

int usb_cdc_read_byte(uint8_t *ch)
{
  if(rx_r == rx_w) return 0;
  if(ch != NULL) *ch = rx_ring[rx_r];
  rx_r = (uint16_t)((rx_r + 1U) & (USB_RX_RING_SIZE - 1U));
  return 1;
}

static int usb_cdc_enqueue(const uint8_t *data, uint16_t len,int control)
{
  uint16_t free_n, i;
  if(data == NULL || len == 0U) return 0;
  if(otg_core.dev.conn_state != USB_CONN_STATE_CONFIGURED) {
#if APP_USB_DIAG
    usb_diag[control?UD_CONTROL_FAIL:UD_TELE_DETACHED]++;
#endif
    return -1;
  }
  free_n = (uint16_t)((USB_TX_RING_SIZE - 1U) - ring_used(tx_r, tx_w, USB_TX_RING_SIZE));
  if(len > free_n||(!control&&free_n-len<USB_TX_CONTROL_RESERVE)) {
#if APP_USB_DIAG
    usb_diag[control?UD_CONTROL_FAIL:UD_TELE_FULL]++;
    if(!control&&!usb_diag[UD_FULL_ACTIVE]) {
      usb_diag[UD_FULL_SINCE]=micros();usb_diag[UD_FULL_ACTIVE]=1U;
    }
#endif
    return -1;
  }
  for(i = 0U; i < len; ++i)
    tx_ring[(tx_w + i) & (USB_TX_RING_SIZE - 1U)] = data[i];
#if APP_USB_TX_CHAIN
  /* The ISR may consume the old prefix while copying. Publish control guard
   * before the new head, under only the USB IRQ mask, so it cannot be missed. */
  uint32_t enabled=NVIC_GetEnableIRQ(OTG_IRQ);
  NVIC_DisableIRQ(OTG_IRQ);__DSB();__ISB();
  if(control)usb_tx_control_queued(&cdc_control_tx,(uint16_t)(tx_w+len));
#endif
  __DMB();
  tx_w = (uint16_t)(tx_w + len);
#if !APP_USB_TX_CHAIN
  if(control) usb_tx_control_queued(&cdc_control_tx,tx_w);
#endif
#if APP_USB_DIAG
  usb_diag[control?UD_CONTROL_OK:UD_TELE_OK]++;
  if(!control)diag_unblocked();
  usb_diag[UD_QUEUE_USED]=ring_used(tx_r,tx_w,USB_TX_RING_SIZE);
  if(usb_diag[UD_QUEUE_USED]>usb_diag[UD_QUEUE_HIGH])usb_diag[UD_QUEUE_HIGH]=usb_diag[UD_QUEUE_USED];
#endif
#if APP_USB_TX_CHAIN
  if(enabled)NVIC_EnableIRQ(OTG_IRQ);
#endif
  return 0;
}
int usb_cdc_write(const uint8_t *data,uint16_t len) { return usb_cdc_enqueue(data,len,0); }
int usb_cdc_write_control(const uint8_t *data,uint16_t len) { return usb_cdc_enqueue(data,len,1); }
int webusb_configured(void) { return usb_cdc_configured(); }

int webusb_tx_idle(void)
{
  cdc_struct_type *pcdc;
  if(!usb_cdc_configured()) return 1;
  pcdc = (cdc_struct_type *)otg_core.dev.class_handler->pdata;
  return wu_tx_r == wu_tx_w && pcdc != NULL && pcdc->g_webusb_tx_completed != 0U;
}

int webusb_control_tx_idle(void)
{
  if(!usb_cdc_configured()) return 1;
  cdc_struct_type *pcdc=(cdc_struct_type *)otg_core.dev.class_handler->pdata;
  return pcdc!=NULL&&usb_tx_control_idle(&webusb_control_tx,pcdc->g_webusb_tx_completed!=0U);
}

void webusb_task(void)
{
  uint16_t n, i, used;
  if(otg_core.dev.conn_state != USB_CONN_STATE_CONFIGURED)
  {
    wu_tx_r = wu_tx_w;
    wu_rx_r = wu_rx_w;
    memset(&webusb_control_tx,0,sizeof(webusb_control_tx));
    return;
  }
  used = ring_used(wu_tx_r, wu_tx_w, WEBUSB_TX_RING_SIZE);
  if(used != 0U)
  {
    n = (used > USBD_WEBUSB_MAXPACKET_SIZE) ? USBD_WEBUSB_MAXPACKET_SIZE : used;
    for(i = 0U; i < n; ++i)
      wu_tx_packet[i] = wu_tx_ring[(wu_tx_r + i) & (WEBUSB_TX_RING_SIZE - 1U)];
    if(usb_webusb_send_data(&otg_core.dev, wu_tx_packet, n) == SUCCESS) {
      usb_tx_control_packet(&webusb_control_tx,wu_tx_r,n);
      wu_tx_r = (uint16_t)(wu_tx_r + n);
    }
  }
  /* Copy the packet out only if it fits; otherwise leave the OUT endpoint
   * un-armed (host is NAKed) until the parser has drained the ring. */
  if(ring_used(wu_rx_r, wu_rx_w, WEBUSB_RX_RING_SIZE) <= (WEBUSB_RX_RING_SIZE - 1U - USBD_WEBUSB_MAXPACKET_SIZE))
  {
    n = usb_webusb_get_rxdata(&otg_core.dev, wu_rx_packet);
    for(i = 0U; i < n; ++i)
    {
      wu_rx_ring[wu_rx_w] = wu_rx_packet[i];
      wu_rx_w = (uint16_t)((wu_rx_w + 1U) & (WEBUSB_RX_RING_SIZE - 1U));
    }
  }
}

int webusb_available(void)
{
  return (int)ring_used(wu_rx_r, wu_rx_w, WEBUSB_RX_RING_SIZE);
}

int webusb_read_byte(uint8_t *ch)
{
  if(wu_rx_r == wu_rx_w) return 0;
  if(ch != NULL) *ch = wu_rx_ring[wu_rx_r];
  wu_rx_r = (uint16_t)((wu_rx_r + 1U) & (WEBUSB_RX_RING_SIZE - 1U));
  return 1;
}

static int webusb_enqueue(const uint8_t *data, uint16_t len,int control)
{
  uint16_t free_n, i;
  if(data == NULL || len == 0U) return 0;
  if(otg_core.dev.conn_state != USB_CONN_STATE_CONFIGURED) return -1;
  free_n = (uint16_t)((WEBUSB_TX_RING_SIZE - 1U) - ring_used(wu_tx_r, wu_tx_w, WEBUSB_TX_RING_SIZE));
  if(len > free_n||(!control&&free_n-len<USB_TX_CONTROL_RESERVE)) return -1;
  for(i = 0U; i < len; ++i)
    wu_tx_ring[(wu_tx_w + i) & (WEBUSB_TX_RING_SIZE - 1U)] = data[i];
  __DMB();
  wu_tx_w = (uint16_t)(wu_tx_w + len);
  if(control) usb_tx_control_queued(&webusb_control_tx,wu_tx_w);
  return 0;
}
int webusb_write(const uint8_t *data,uint16_t len) { return webusb_enqueue(data,len,0); }
int webusb_write_control(const uint8_t *data,uint16_t len) { return webusb_enqueue(data,len,1); }

#include "bsp.h"
void usb_delay_ms(uint32_t ms) { delay_ms(ms); }
void usb_delay_us(uint32_t us) { delay_us(us); }
